#pragma once
#include "Runtime/Engine/Classes/GameFramework/CharacterMovementComponent.h"
#include "MultiCollisionMovementComponent.generated.h"

UCLASS()
class UMultiCollisionMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()
	UMultiCollisionMovementComponent(const FObjectInitializer& ObjectInitializer);

public:
	
	virtual void InitializeComponent() override;

	void UpdateAdditionalUpdatedComponents();

	virtual void PhysicsRotation(float DeltaTime) override;

	void SetPendingRotation(const FQuat NewPendingRotation);
	
protected:

	virtual void OnMovementUpdated(float DeltaSeconds, const FVector & OldLocation, const FVector & OldVelocity) override;

	//virtual void PhysFlying(float deltaTime, int32 Iterations) override;
	//virtual void PhysWalking(float deltaTime, int32 Iterations) override;

	virtual bool MoveUpdatedComponentImpl(const FVector& Delta, const FQuat& Rotation, bool bSweep, FHitResult* OutHit = NULL, ETeleportType Teleport = ETeleportType::None);

	virtual bool ResolvePenetrationImpl(const FVector& Adjustment, const FHitResult& Hit, const FQuat& Rotation) override;

	//  this is a movement component CVarPenetrationOverlapCheckInflation copy
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Movement")
	/** Inflation added to object when checking if a location is free of blocking collision. Distance added to inflation in penetration overlap check. */
	float PenetrationOverlapCheckInflation;


private:

	UPROPERTY()
	TArray<class UMultiCollisionCapsuleComponent*> AdditionalUpdatedComponents;

	UPROPERTY()
	class UPrimitiveComponent* LastBlockedComponent;

	bool MoveAdditionalUpdatedComponents(const FVector& Delta, const FQuat& NewRotation, FHitResult* OutHit);

};
