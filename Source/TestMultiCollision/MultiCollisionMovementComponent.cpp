#include "MultiCollisionMovementComponent.h"
#include "MultiCollisionCapsuleComponent.h"
#include "MultiCollisionCharacter.h"

#include "Runtime/Engine/Classes/GameFramework/PhysicsVolume.h"


UMultiCollisionMovementComponent::UMultiCollisionMovementComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	PenetrationOverlapCheckInflation = 0.1f;

}

void UMultiCollisionMovementComponent::InitializeComponent()
{
	Super::InitializeComponent();
}

void UMultiCollisionMovementComponent::UpdateAdditionalUpdatedComponents()
{
	class AMultiCollisionCharacter* MultiCollisionOwner = Cast<AMultiCollisionCharacter>(GetCharacterOwner());
	if (!MultiCollisionOwner)
	{
		return;
	}

	// 1. Remove old if exist (for runtime character reconfiguration/changes)
	for (int32 i = 0; i < AdditionalUpdatedComponents.Num(); i++)
	{
		if (auto AdditionalComponent = Cast<UMultiCollisionCapsuleComponent>(AdditionalUpdatedComponents[i]))
		{
			AdditionalComponent->MoveIgnoreActors.Empty();

			if (bEnablePhysicsInteraction)
			{
				AdditionalComponent->OnComponentBeginOverlap.RemoveAll(this);
			}
		}
	}

	AdditionalUpdatedComponents.Empty();

	// SHOULD WE REUPDATE IGNORE ACTORS?
	//UpdatedPrimitive->MoveIgnoreActors = get ignore actors from character owner

	// 2. Collect all additional collisions

	auto CollisionComponents = MultiCollisionOwner->GetComponentsByClass(UMultiCollisionCapsuleComponent::StaticClass());
	for (int32 i = 0; i < CollisionComponents.Num(); i++)
	{
		if (auto AdditionalComponent = Cast<UMultiCollisionCapsuleComponent>(CollisionComponents[i]))
		{
			AdditionalComponent->MoveIgnoreActors.Add(MultiCollisionOwner);
			AdditionalComponent->MoveIgnoreActors += UpdatedPrimitive->MoveIgnoreActors;

			if (bEnablePhysicsInteraction)
			{
				AdditionalComponent->OnComponentBeginOverlap.AddUniqueDynamic(this, &UMultiCollisionMovementComponent::CapsuleTouched);
			}

			AdditionalUpdatedComponents.Add(AdditionalComponent);
		}
	}
}

bool UMultiCollisionMovementComponent::MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation, bool bSweep, FHitResult* OutHit, ETeleportType Teleport)
{
	if (!UpdatedComponent)
	{
		return false;
	}

	FQuat NewRotation = Rotation;

	FVector NewDelta = ConstrainDirectionToPlane(Delta);

	// Delta move and delta rot check - are they much enough for physics sweep
	// ComponentSweepMulti does nothing if moving < KINDA_SMALL_NUMBER in distance, so it's important to not try to sweep distances smaller than that. 
	const float DeltaMoveSizeSq = NewDelta.SizeSquared();
	const float MinMovementDistSq = FMath::Square(4.f * KINDA_SMALL_NUMBER);
	if (DeltaMoveSizeSq <= MinMovementDistSq)
	{
		if (NewRotation.Equals(UpdatedComponent->GetComponentQuat(), SCENECOMPONENT_QUAT_TOLERANCE))
		{
			if (OutHit)
			{
				OutHit->Reset(1.f);
			}
			return true;
		}
		else
		{
			NewDelta = FVector::ZeroVector;
		}
	}

	// test if our move will not hit something with additional collisions
	const bool bMoved = bSweep ? MoveAdditionalUpdatedComponents(NewDelta, NewRotation, OutHit) : true;

	if (!bMoved)
	{
		NewDelta *= OutHit->Time; // adjust delta to move as much as possible to location before the hit based on hit time
		NewRotation = FQuat::Slerp(UpdatedComponent->GetComponentQuat(), Rotation, OutHit->Time); // adjust rotation
	}

	// We move updated component without sweep because sweep is used on additional collisions only
	UpdatedComponent->MoveComponent(NewDelta, NewRotation, false, nullptr, MoveComponentFlags, ETeleportType::TeleportPhysics);

	// Force transform update of AdditionalUpdatedComponents after any move/turn happened
	UpdatedComponent->UpdateChildTransforms(EUpdateTransformFlags::PropagateFromParent, ETeleportType::TeleportPhysics);

	return bMoved;
}

bool UMultiCollisionMovementComponent::MoveAdditionalUpdatedComponents(const FVector& Delta, const FQuat& NewRotation, FHitResult* OutHit)
{
	// init array of blocking hits
	TArray<FHitResult> BlockedHits;
	BlockedHits.Empty();

	// init array of blocked components related to blocked hits
	TArray<UMultiCollisionCapsuleComponent*> BlockedComponents;
	BlockedComponents.Empty();

	// init current movement blocked component
	LastBlockedComponent = nullptr;

	// we are checking if any of the additional components goes in block of another object and saving the results of every component test
	for (int32 i = 0; i < AdditionalUpdatedComponents.Num(); i++)
	{
		if (AdditionalUpdatedComponents[i])
		{
			FHitResult BlockedHit = FHitResult(1.f);
			const bool bMoveResult = AdditionalUpdatedComponents[i]->SimulateMoveComponent(UpdatedComponent, Delta, NewRotation, &BlockedHit, MoveComponentFlags);

			if (!bMoveResult)
			{
				BlockedHits.Add(BlockedHit);
				BlockedComponents.Add(AdditionalUpdatedComponents[i]);
			}
		}
	}

	if (BlockedHits.Num() > 0)
	{
		// there is situation when we move forward and ship wings can be larger in front of the trunk. but the move delta can very high especially when boosting or dodging
		// large move delta can cause that several components will penetrate another object. we need to find the one which is the most far penetrated the object.
		// if we just use the order of AdditionalUpdatedComponents then trunk will ever be first which can get wrong result on large delta.

		// select hit by the most small hit time to find the most first colision hit
		int32 BadIndex = 0;
		float BadTime = 1.f;

		for (int32 i = 0; i < BlockedHits.Num(); i++)
		{
			const float TestTime = BlockedHits[i].Time;
			if (TestTime < BadTime)
			{
				BadTime = TestTime;
				BadIndex = i;
			}
		}

		*OutHit = BlockedHits[BadIndex]; // save the most bad hit result
		LastBlockedComponent = BlockedComponents[BadIndex]; // save component which caused the most bad hit

		return false; // our move was blocked. the performed move failed and requires a correction.
	}
	else
	{
		OutHit->Reset(1.f); // zero out hit on success move
		return true; // there was nothing blocked on move and turn - we get successful move. no correction needed.
	}
}

bool UMultiCollisionMovementComponent::ResolvePenetrationImpl(const FVector& ProposedAdjustment, const FHitResult& Hit, const FQuat& Rotation)
{
	if (!LastBlockedComponent)
	{
		return false; // process only with valid last blocked component.
	}

	bool bMoved = false;

	// SceneComponent can't be in penetration, so this function really only applies to PrimitiveComponent.
	const FVector Adjustment = ConstrainDirectionToPlane(ProposedAdjustment);

	// Set rotation to use
	const FQuat MoveRotation = Rotation; // use new rotation when resolving penetration
	//const FQuat MoveRotation = UpdatedComponent->GetComponentQuat(); // use current rotation when resolving penetration - means no rotation change if resolving penetration

	if (!Adjustment.IsZero())
	{
		// We really want to make sure that precision differences or differences between the overlap test and sweep tests don't put us into another overlap,
		// so make the overlap test a bit more restrictive.

		bool bEncroached = false;
		bEncroached = OverlapTest(Hit.TraceStart + Adjustment, LastBlockedComponent->GetComponentQuat(), LastBlockedComponent->GetCollisionObjectType(), LastBlockedComponent->GetCollisionShape(PenetrationOverlapCheckInflation), LastBlockedComponent->GetOwner());

		if (!bEncroached)
		{
			// Move without sweeping.
			FHitResult EncroachHit(1.f);
			bMoved = MoveUpdatedComponent(Adjustment, MoveRotation, false, &EncroachHit, ETeleportType::TeleportPhysics);
		}
		else
		{
			// Disable MOVECOMP_NeverIgnoreBlockingOverlaps if it is enabled, otherwise we wouldn't be able to sweep out of the object to fix the penetration.
			TGuardValue<EMoveComponentFlags> ScopedFlagRestore(MoveComponentFlags, EMoveComponentFlags(MoveComponentFlags & (~MOVECOMP_NeverIgnoreBlockingOverlaps)));

			// Try sweeping as far as possible...
			FHitResult SweepOutHit(1.f);

			bMoved = MoveUpdatedComponent(Adjustment, MoveRotation, true, &SweepOutHit, ETeleportType::TeleportPhysics);

			// Still stuck?
			if (!bMoved && SweepOutHit.bStartPenetrating)
			{
				// Combine two MTD results to get a new direction that gets out of multiple surfaces.
				const FVector SecondMTD = GetPenetrationAdjustment(SweepOutHit);
				const FVector CombinedMTD = Adjustment + SecondMTD;
				if (SecondMTD != Adjustment && !CombinedMTD.IsZero())
				{					
					bMoved = MoveUpdatedComponent(CombinedMTD, MoveRotation, true, &SweepOutHit, ETeleportType::TeleportPhysics);
				}
			}

			// Still stuck?
			if (!bMoved)
			{
				// Try moving the proposed adjustment plus the attempted move direction. This can sometimes get out of penetrations with multiple objects
				const FVector MoveDelta = ConstrainDirectionToPlane(Hit.TraceEnd - Hit.TraceStart);
				if (!MoveDelta.IsZero())
				{
					FHitResult FallBacktHit(1.f);
					bMoved = MoveUpdatedComponent(Adjustment + MoveDelta, MoveRotation, true, &FallBacktHit, ETeleportType::TeleportPhysics);
				}
			}
		}
	}

	bJustTeleported |= bMoved;
	return bJustTeleported;
}

void UMultiCollisionMovementComponent::PhysicsRotation(float DeltaTime)
{
	if (!(bOrientRotationToMovement || bUseControllerDesiredRotation))
	{
		return;
	}

	if (!HasValidData() || (!CharacterOwner->Controller && !bRunPhysicsWithNoController))
	{
		return;
	}

	FRotator CurrentRotation = UpdatedComponent->GetComponentRotation(); // Normalized
	CurrentRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): CurrentRotation"));

	FRotator DeltaRot = GetDeltaRotation(DeltaTime);
	DeltaRot.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): GetDeltaRotation"));

	FRotator DesiredRotation = CurrentRotation;
	if (bOrientRotationToMovement)
	{
		DesiredRotation = ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRot);
	}
	else if (CharacterOwner->Controller && bUseControllerDesiredRotation)
	{
		DesiredRotation = CharacterOwner->Controller->GetDesiredRotation();
	}
	else
	{
		return;
	}

	if (ShouldRemainVertical())
	{
		DesiredRotation.Pitch = 0.f;
		DesiredRotation.Yaw = FRotator::NormalizeAxis(DesiredRotation.Yaw);
		DesiredRotation.Roll = 0.f;
	}
	else
	{
		DesiredRotation.Normalize();
	}

	// Accumulate a desired new rotation.
	const float AngleTolerance = 1e-3f;

	if (!CurrentRotation.Equals(DesiredRotation, AngleTolerance))
	{
		// PITCH
		if (!FMath::IsNearlyEqual(CurrentRotation.Pitch, DesiredRotation.Pitch, AngleTolerance))
		{
			DesiredRotation.Pitch = FMath::FixedTurn(CurrentRotation.Pitch, DesiredRotation.Pitch, DeltaRot.Pitch);
		}

		// YAW
		if (!FMath::IsNearlyEqual(CurrentRotation.Yaw, DesiredRotation.Yaw, AngleTolerance))
		{
			DesiredRotation.Yaw = FMath::FixedTurn(CurrentRotation.Yaw, DesiredRotation.Yaw, DeltaRot.Yaw);
		}

		// ROLL
		if (!FMath::IsNearlyEqual(CurrentRotation.Roll, DesiredRotation.Roll, AngleTolerance))
		{
			DesiredRotation.Roll = FMath::FixedTurn(CurrentRotation.Roll, DesiredRotation.Roll, DeltaRot.Roll);
		}

		// Set the new rotation.
		DesiredRotation.DiagnosticCheckNaN(TEXT("CharacterMovementComponent::PhysicsRotation(): DesiredRotation"));

		FHitResult RotationHit(1.f);
		MoveUpdatedComponent(FVector::ZeroVector, DesiredRotation, true, &RotationHit);
	}
}

void UMultiCollisionMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
	Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);

	// If character was moved we need to notify all world objects about overlaps of additional collisions.
	// Update overlaps for additional components after they were teleported with root capsule component
	for (int32 i = 0; i < AdditionalUpdatedComponents.Num(); i++)
	{
		if (AdditionalUpdatedComponents[i])
		{
			AdditionalUpdatedComponents[i]->UpdateOverlaps();
			AdditionalUpdatedComponents[i]->UpdatePhysicsVolume(true);
		}
	}

	LastBlockedComponent = nullptr; // zero
}
