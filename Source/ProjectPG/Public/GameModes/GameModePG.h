// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameMode/GameMode_InGame.h"
#include "GameModePG.generated.h"

// 맵 시험 게임모드(맵 생성기 + 시드).
// 왜 AGameMode_InGame 을 부모로 하나: 팀 인벤토리 동기화(서버가 상자·바닥 물건 내용을 정하고 복제)는
//   게임모드가 InGame 이거나 그 자식일 때만 켜진다. 그래야 인게임 컨트롤러·플레이어 상태도 같이 받는다.
//   예전 부모(엔진 AGameMode)만의 기능(매치 상태 등)은 여기서 안 쓴다.
UCLASS()
class PROJECTPG_API AGameModePG : public AGameMode_InGame
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
