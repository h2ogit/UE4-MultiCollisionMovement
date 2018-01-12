#pragma once
#include "GameFramework/Character.h"
#include "MultiCollisionCharacter.generated.h"

UCLASS(config=Game)
class AMultiCollisionCharacter : public ACharacter
{
	GENERATED_BODY()
	

public:

	virtual void PostInitializeComponents() override;

	virtual void FaceRotation(FRotator ControlRotation, float DeltaTime) override;

protected:
	AMultiCollisionCharacter(const FObjectInitializer& ObjectInitializer);

private:
	class UMultiCollisionMovementComponent* MultiCollisionMovementComponent;
};

