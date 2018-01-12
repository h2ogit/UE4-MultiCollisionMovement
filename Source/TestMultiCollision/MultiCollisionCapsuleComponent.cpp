//#include "TestMultiCollision.h"
#include "MultiCollisionCapsuleComponent.h"

UMultiCollisionCapsuleComponent::UMultiCollisionCapsuleComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	BodyInstance.SetCollisionProfileName(FName("Pawn"));
	bShouldUpdatePhysicsVolume = true;
	CanCharacterStepUpOn = ECB_Yes;
	SetNotifyRigidBodyCollision(false);
	SetEnableGravity(false);
}

static float InitialOverlapToleranceCVar = 0.0f;
static FAutoConsoleVariableRef CVarInitialOverlapTolerance(
	TEXT("p.InitialOverlapTolerance"),
	InitialOverlapToleranceCVar,
	TEXT("Tolerance for initial overlapping test in PrimitiveComponent movement.\n")
	TEXT("Normals within this tolerance are ignored if moving out of the object.\n")
	TEXT("Dot product of movement direction and surface normal."),
	ECVF_Default);

static void PullBackHit(FHitResult& Hit, const FVector& Start, const FVector& End, const float Dist)
{
	const float DesiredTimeBack = FMath::Clamp(0.1f, 0.1f / Dist, 1.f / Dist) + 0.001f;
	Hit.Time = FMath::Clamp(Hit.Time - DesiredTimeBack, 0.f, 1.f);
}

static bool ShouldIgnoreHitResult(const UWorld* InWorld, FHitResult const& TestHit, FVector const& MovementDirDenormalized, const AActor* MovingActor, EMoveComponentFlags MoveFlags)
{
	if (TestHit.bBlockingHit)
	{
		// check "ignore bases" functionality
		if ((MoveFlags & MOVECOMP_IgnoreBases) && MovingActor)	//we let overlap components go through because their overlap is still needed and will cause beginOverlap/endOverlap events
		{
			// ignore if there's a base relationship between moving actor and hit actor
			AActor const* const HitActor = TestHit.GetActor();
			if (HitActor)
			{
				if (MovingActor->IsBasedOnActor(HitActor) || HitActor->IsBasedOnActor(MovingActor))
				{
					return true;
				}
			}
		}

		// If we started penetrating, we may want to ignore it if we are moving out of penetration.
		// This helps prevent getting stuck in walls.
		if (TestHit.bStartPenetrating && !(MoveFlags & MOVECOMP_NeverIgnoreBlockingOverlaps))
		{
			const float DotTolerance = InitialOverlapToleranceCVar;

			// Dot product of movement direction against 'exit' direction
			const FVector MovementDir = MovementDirDenormalized.GetSafeNormal();
			const float MoveDot = (TestHit.ImpactNormal | MovementDir);

			const bool bMovingOut = MoveDot > DotTolerance;

			// If we are moving out, ignore this result!
			if (bMovingOut)
			{
				return true;
			}
		}
	}

	return false;
}

// this is a simitation of UPrimitiveComponent::MoveComponentImpl() without actual move, just sweep checks
bool UMultiCollisionCapsuleComponent::SimulateMoveComponent(const class USceneComponent* CharacterRootComponent, const FVector& NewDelta, const FQuat& NewRotation, FHitResult* OutHit, EMoveComponentFlags MoveFlags)
{
	// static things can move before they are registered (e.g. immediately after streaming), but not after.
	if (IsPendingKill() || !IsRegistered() || !GetWorld())
	{
		if (OutHit)
		{
			OutHit->Init();
		}

		return false; // skip simulation
	}

	ConditionalUpdateComponentToWorld();

	// Additional component attached to te socket and has relative rotation, so the root rotation != additional component rotation
	// We need to find new additional component rotation which will be with NewRotationQuat applied to root component.
	const FQuat DeltaQuat = NewRotation * CharacterRootComponent->GetComponentQuat().Inverse(); // find delta rotation of the root component
	const FQuat NewCompQuat = DeltaQuat * GetComponentQuat(); // calc new rotation for this component

	// debug
	// UE_LOG(LogClass, Log, TEXT("							SimulateMoveComponent %s ROT Current %s New = %s"), *GetNameSafe(GetOwner()), *GetComponentQuat().Rotator().ToString(), *NewCompQuat.Rotator().ToString());
	// debug

	// Additional component has translation from the root component. That means any turn of the root is also the location change for additional component
	const FVector RootComponentLocation = CharacterRootComponent->GetComponentLocation();

	const FVector TraceStart = GetComponentLocation();

	// In modular system component can not be attached directly to the root component, and can be attached to the child of the child of the child
	// This means we can not find relative translation directly from transform
	// For this component we need to find translation from the root center and calc new location on new root rotation
	const FVector DeltaLocation = TraceStart - RootComponentLocation;
	const FVector DeltaDir = DeltaLocation.GetSafeNormal();
	const float DeltaSize = DeltaLocation.Size();
	const FVector NewDir = DeltaQuat.RotateVector(DeltaDir); // turn direction vector on delta rotation
	const FVector NewComponentLocation = RootComponentLocation + NewDir * DeltaSize;

	const FVector TraceEnd = NewComponentLocation + NewDelta;

	// debug
	// UE_LOG(LogClass, Log, TEXT("							SimulateMoveComponent %s Loc Current %s New = %s"), *GetNameSafe(GetOwner()), *TraceStart.ToString(), *TraceEnd.ToString());
	// debug

	TArray<FHitResult> Hits;
	Hits.Empty();

	FComponentQueryParams QueryParams(TEXT("SimulateMoveComponent"), GetOwner());
	FCollisionResponseParams ResponseParam;
	InitSweepCollisionParams(QueryParams, ResponseParam);

	const bool bHadBlockingHit = GetWorld()->ComponentSweepMulti(Hits, this, TraceStart, TraceEnd, NewCompQuat, QueryParams);

	if (Hits.Num() > 0)
	{
		const float NewDeltaSize = NewDelta.Size();
		for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
		{
			PullBackHit(Hits[HitIdx], TraceStart, TraceEnd, NewDeltaSize);
		}
	}

	if (bHadBlockingHit)
	{
		FHitResult BlockingHit(NoInit);
		BlockingHit.bBlockingHit = false;
		BlockingHit.Time = 1.f;

		int32 BlockingHitIndex = INDEX_NONE;
		float BlockingHitNormalDotDelta = BIG_NUMBER;
		for (int32 HitIdx = 0; HitIdx < Hits.Num(); HitIdx++)
		{
			const FHitResult& TestHit = Hits[HitIdx];

			if (TestHit.bBlockingHit)
			{
				if (!ShouldIgnoreHitResult(GetWorld(), TestHit, NewDelta, GetOwner(), MoveFlags))
				{
					if (TestHit.Time == 0.f)
					{
						// We may have multiple initial hits, and want to choose the one with the normal most opposed to our movement.
						const float NormalDotDelta = (TestHit.ImpactNormal | NewDelta);
						if (NormalDotDelta < BlockingHitNormalDotDelta)
						{
							BlockingHitNormalDotDelta = NormalDotDelta;
							BlockingHitIndex = HitIdx;
						}
					}
					else if (BlockingHitIndex == INDEX_NONE)
					{
						// First non-overlapping blocking hit should be used, if an overlapping hit was not.
						// This should be the only non-overlapping blocking hit, and last in the results.
						BlockingHitIndex = HitIdx;
						break;
					}
				}
			}
		}

		// Update blocking hit, if there was a valid one.
		if (BlockingHitIndex >= 0)
		{
			BlockingHit = Hits[BlockingHitIndex];

			if (OutHit)
			{
				*OutHit = BlockingHit;
			}

			return false;
		}
		else
		{
			return true;
		}
	}

	return true;
}