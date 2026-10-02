// Fill out your copyright notice in the Description page of Project Settings.


#include "Animations/AnimNotify_Fire.h"

#include "Characters/CustomCharacter.h"

void UAnimNotify_Fire::Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
                              const FAnimNotifyEventReference& EventReference)
{
	Super::Notify(MeshComp, Animation, EventReference);
	
	if (!IsValid(MeshComp))
		return;
	
	ACustomCharacter* character = Cast<ACustomCharacter>(MeshComp->GetAttachParentActor());
	if (!IsValid(character))
		return;
	
	character->Fire();
}
