// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/CustomPlayerState.h"
#include "Components/InventoryComponent.h"
#include "Components/DataComponent.h"
#include "Components/EquipComponent.h"
ACustomPlayerState::ACustomPlayerState()
{
	InvenComp = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
	DataComp = CreateDefaultSubobject<UDataComponent>(TEXT("DataComponent"));
	EquipComp = CreateDefaultSubobject<UEquipComponent>(TEXT("EquipComponent"));

	if(InvenComp) InvenComp->SetIsReplicated(true);
	if (DataComp) DataComp->SetIsReplicated(true);
	if (EquipComp) EquipComp->SetIsReplicated(true);
}

void ACustomPlayerState::BeginPlay()
{
	Super::BeginPlay();
	//if (InvenComp) InvenComp->AddItemByID(FName("1001"));
	//if (InvenComp) InvenComp->AddItemByPosition(FName("1002"),1,FIntPoint(2,2));

}
