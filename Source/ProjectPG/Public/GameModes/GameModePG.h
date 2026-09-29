// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameMode.h"
#include "GameModePG.generated.h"

/**
 * 
 */
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

	// Test2 accepts -PGMapSeed=<number>. With no option it keeps the team's
	// timestamp behavior. Dedicated server can later write this same value.
	int64 _mapGenerationSeed = 0;

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

public:
	void SetRandomSeed(int64 seed);
	int64 GetMapGenerationSeed() const { return _mapGenerationSeed; }

	int32 GenerateRandomNumber(int32 min, int32 max);
	bool CheckPossibility(int32 numerator, int32 denominator);

private:
	// 레벨 디자인 포인트가 확정되면 오브젝트 스포너를 부른다 (설정에서 켠 경우만).
	void HandleLevelDesignPointsBuilt(const TArray<struct FLevelDesignPoint>& Points);

	// 시설 레벨은 비동기로 하나씩 들어오므로, 문 메시 → 문 액터 변환을 잠시 동안 반복한다.
	void ConvertLevelDoorsTick();
	FTimerHandle _doorConvertTimer;
	int32 _doorConvertRuns = 0;
};
