// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/ItemSubSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Core/TableSubSystem.h"

UItemSubSystem* UItemSubSystem::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;

	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;


	return inst->GetSubsystem<UItemSubSystem>();
}

const FItemTableRow* UItemSubSystem::GetItem(FName ItemID)
{
	UTableSubSystem* subSystem = UTableSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return nullptr;

	const FItemTableRow* ItemRow = subSystem->FindTableRow<FItemTableRow>(TEXT("ItemTable"), ItemID);
	if(nullptr == ItemRow)
	    return nullptr;

	return ItemRow;
}

const FDropTableaRow* UItemSubSystem::GetDrop(FName MonsterID)
{
	UTableSubSystem* subSystem = UTableSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return nullptr;

	const FDropTableaRow* ItemRow = subSystem->FindTableRow<FDropTableaRow>(TEXT("DropTable"), MonsterID);

	if (nullptr == ItemRow) {
		UE_LOG(LogTemp, Warning, TEXT("Drop Table Row Data Can't Find"));
		return nullptr;
	}
	return ItemRow;
}
