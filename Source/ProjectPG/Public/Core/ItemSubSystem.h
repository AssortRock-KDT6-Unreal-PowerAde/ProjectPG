// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Common/TableData.h"
#include "Common/GameData.h"
#include "ItemSubSystem.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UItemSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	static UItemSubSystem* Get(const UObject* worldContext);

	const FItemTableRow* GetItem(FName ItemID);
	const FDropTableaRow* GetDrop(FName MonsterID);

};
