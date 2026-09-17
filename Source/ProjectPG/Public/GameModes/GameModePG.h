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
