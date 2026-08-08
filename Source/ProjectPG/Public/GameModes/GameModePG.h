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

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaSeconds) override;
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

public:
	void SetRandomSeed(int64 seed);

	int32 GenerateRandomNumber(int32 min, int32 max);
	bool CheckPossibility(int32 numerator, int32 denominator);
};
