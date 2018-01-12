#pragma once
#include "Runtime/Engine/Classes/Components/CapsuleComponent.h"
#include "MultiCollisionCapsuleComponent.generated.h"

/** Implements the additional capsule component for movement sweep */
UCLASS(NotBlueprintable, NotBlueprintType, ClassGroup = "Collision", editinlinenew, meta = (BlueprintSpawnableComponent))
class UMultiCollisionCapsuleComponent : public UCapsuleComponent
{
	GENERATED_BODY()
	UMultiCollisionCapsuleComponent(const FObjectInitializer& ObjectInitializer);

public:
	bool SimulateMoveComponent(const class USceneComponent* CharacterRootComponent, const FVector& NewDelta, const FQuat& NewRotation, FHitResult* OutHit = nullptr, EMoveComponentFlags MoveFlags = MOVECOMP_NoFlags);
protected:


private:


};
