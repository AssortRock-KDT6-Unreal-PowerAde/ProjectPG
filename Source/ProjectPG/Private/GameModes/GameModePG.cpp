// Fill out your copyright notice in the Description page of Project Settings.


#include "GameModes/GameModePG.h"

#include "Components/MapGeneratorComponent.h"

AGameModePG::AGameModePG()
{
	_mapGenerator = CreateDefaultSubobject<UMapGeneratorComponent>(TEXT("MapGenerator"));
}

void AGameModePG::BeginPlay()
{
	Super::BeginPlay();
	_mapGenerator->Generate();
}

void AGameModePG::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void AGameModePG::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	int64 timestamp = FDateTime::UtcNow().ToUnixTimestamp();
	_random.Initialize(timestamp);
}

void AGameModePG::SetRandomSeed(int64 seed)
{
	_random.Initialize(seed);
}

int32 AGameModePG::GenerateRandomNumber(int32 min, int32 max)
{
	return _random.RandRange(min, max);
}

bool AGameModePG::CheckPossibility(int32 numerator, int32 denominator)
{
	int32 num = _random.RandRange(0, denominator - 1);
	return num < numerator;
}
