#pragma once

#include "CoreMinimal.h"
#include "GameModes/GameModePG.h"  // 팀 합의(10/7): 맵 게임모드를 부모로
#include "Common/GameData.h"
#include "GameMode_InGame.generated.h"

class UInventorySubSystem;

UCLASS()
class PROJECTPG_API AGameMode_InGame : public AGameModePG
{
	GENERATED_BODY()

public:
	AGameMode_InGame();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
};
