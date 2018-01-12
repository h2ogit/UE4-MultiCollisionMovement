#include "MultiCollisionCharacter.h"
#include "MultiCollisionMovementComponent.h"

AMultiCollisionCharacter::AMultiCollisionCharacter(const FObjectInitializer& ObjectInitializer) : 
	Super(ObjectInitializer.SetDefaultSubobjectClass<UMultiCollisionMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	
}

void AMultiCollisionCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	MultiCollisionMovementComponent = Cast<UMultiCollisionMovementComponent>(GetCharacterMovement());
	if (MultiCollisionMovementComponent)
	{
		MultiCollisionMovementComponent->UpdateAdditionalUpdatedComponents();
	}
}

void AMultiCollisionCharacter::FaceRotation(FRotator ControlRotation, float DeltaTime)
{
	// No super call. No character rotation set
}