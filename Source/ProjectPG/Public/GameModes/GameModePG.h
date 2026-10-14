// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "GameModePG.generated.h"

// 맵 게임모드(맵 생성기 + 시드). 박경용 님이 만든 것.
// 팀 합의(10/7 오후): 인게임 게임모드(AGameMode_InGame)가 이걸 부모로 물려받는다.
//   → 인게임 모드 하나로 "맵 만들기 + 인벤토리 동기화"가 같이 된다.
UCLASS()
class PROJECTPG_API AGameModePG : public AGameMode
{
	GENERATED_BODY()

public:
	AGameModePG();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UMapGeneratorComponent> _mapGenerator;

private:
	UPROPERTY()
	FRandomStream _random;
	// -PGMapSeed=<숫자> 로 맵 시드를 고정. 옵션이 없으면 기존처럼 현재 시각.
	int64 _mapGenerationSeed = 0;

protected:
	// 시작할 때 맵(600m)을 만들지. 인게임 모드가 이걸 물려받아서, 캐릭터·인벤토리 시험 레벨처럼
	// 맵이 필요 없는 레벨은 그 레벨 게임모드(BP)에서 끄면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Map")
	bool bBuildMapOnBeginPlay = true;

	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	int64 GetMapGenerationSeed() const { return _mapGenerationSeed; }
public:
	void SetRandomSeed(int64 seed);

	int32 GenerateRandomNumber(int32 min, int32 max);
	bool CheckPossibility(int32 numerator, int32 denominator);
};
