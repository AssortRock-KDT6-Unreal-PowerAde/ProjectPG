// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/DataTable.h"
#include "Common/TableData.h"
#include "TableSubSystem.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UTableSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	UTableSubSystem();

protected:
	UPROPERTY()
	TObjectPtr<UDataTable> _tablePath;

	UPROPERTY()
	TMap<FName, TObjectPtr<UDataTable>> _tables;

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UTableSubSystem* Get(const UObject* worldContext);

	TObjectPtr<UDataTable> FindTable(const FName& tableName) const
	{
		if (false == _tables.Contains(tableName))
			return nullptr;

		return *_tables.Find(tableName);
	}

	template<typename T>
	const T* FindTableRow(const FName& tableName, const FName& rowName) const
	{
		TObjectPtr<UDataTable> table = FindTable(tableName);
		if (nullptr == table)
			return nullptr;

		return table->FindRow<T>(rowName, TEXT("not Found row"));
	}

private:
	bool LoadTable();
};
