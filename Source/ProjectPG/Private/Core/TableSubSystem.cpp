// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/TableSubSystem.h"
#include "Kismet/GameplayStatics.h"
UTableSubSystem::UTableSubSystem()
{
}

void UTableSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{

	Super::Initialize(Collection);
	LoadTable();
}

void UTableSubSystem::Deinitialize()
{
	Super::Deinitialize();
}

UTableSubSystem* UTableSubSystem::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;

	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;

	return inst->GetSubsystem<UTableSubSystem>();
}

bool UTableSubSystem::LoadTable()
{
	_tablePath = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *FString("/Game/PG/Table/TableLoader")));

	if (false == IsValid(_tablePath)) return false;

	bool result = true;

	_tablePath->ForeachRow<FTablePathRow>(TEXT("Not Found Table.."),
	[this,&result](const FName&Key, const FTablePathRow& Value)
	{
		if (Value.UseThis)
		{
			TObjectPtr<UDataTable> loadTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *Value.Path));
			if (loadTable)
			{
				_tables.Add(Key, loadTable);
			}
			else 
			{
				result = false;
			}
		}

	});

	return result;
}
