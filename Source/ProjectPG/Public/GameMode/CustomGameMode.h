// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Common/GameDefine.h"
#include "CustomGameMode.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ACustomGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	void ChangeScene(ESceneType scene);
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
};
