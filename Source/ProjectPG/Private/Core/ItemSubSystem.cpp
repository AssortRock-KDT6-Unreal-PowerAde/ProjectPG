// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/ItemSubSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Core/TableSubSystem.h"
#include "Objects/PGItemRowFallback.h"

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

	// (2026-09-20 개인 브랜치) 팀 표에 없는 오브젝트 쪽 아이템(연료통·옷·무기 등)은 대체 행으로. 표에 같은 ID 가 생기면 표가 우선.
	// 없는 ID 를 FindTableRow 로 찾으면 인벤토리 격자를 다시 만들 때마다 "not Found row" 경고가 찍혀서, 먼저 조용히 있는지만 본다.
	if (const UDataTable* ItemTable = subSystem->FindTable(TEXT("ItemTable")); ItemTable && !ItemTable->GetRowMap().Contains(ItemID))
	{
		if (const FItemTableRow* Fallback = PGItemRowFallback::Find(ItemID))
			return Fallback;
	}

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

const FEquipTableRow* UItemSubSystem::GetEquip(FName ItemID)
{
	UTableSubSystem* subSystem = UTableSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return nullptr;

	const FEquipTableRow* ItemRow = subSystem->FindTableRow<FEquipTableRow>(TEXT("EquipTable"), ItemID);
	if (nullptr == ItemRow) {
		UE_LOG(LogTemp, Warning, TEXT("Equip Table Row Data Can't Find"));
		return nullptr;
	}
	return ItemRow;
}
