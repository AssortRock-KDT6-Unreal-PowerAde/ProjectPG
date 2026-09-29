// Fill out your copyright notice in the Description page of Project Settings.


#include "GameModes/GameModePG.h"
#include "Combat/PGCombatSettings.h"

#include "Actors/WarZoneFootprintPreview.h"
#include "Actors/MapTile.h"
#include "Components/MapGeneratorComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Combat/PGCombatSpawner.h"
#include "Objects/PGLevelDoorConverter.h"
#include "TimerManager.h"

AGameModePG::AGameModePG()
{
	_mapGenerator = CreateDefaultSubobject<UMapGeneratorComponent>(TEXT("MapGenerator"));
}

void AGameModePG::BeginPlay()
{
	Super::BeginPlay();
	_mapGenerator->Generate();
	// AMapTile is the server-authoritative logical grid, not the shipped visual.
	// Hide it immediately after generation so the red metaball/debug grid cannot
	// flash before the deterministic presentation layer finishes construction.
	for (TActorIterator<AMapTile> It(GetWorld()); It; ++It)
	{
		It->SetActorHiddenInGame(true);
		It->SetActorEnableCollision(false);
		It->SetActorTickEnabled(false);
	}

	// Keep the team's logical AMapTile generation authoritative, then attach the
	// ProjectPG presentation layer automatically. Existing test levels that
	// already contain a preview actor remain valid and do not get a duplicate.
	AWarZoneFootprintPreview* Preview = IsValid(GetWorld())
		? Cast<AWarZoneFootprintPreview>(UGameplayStatics::GetActorOfClass(this, AWarZoneFootprintPreview::StaticClass()))
		: nullptr;
	if (IsValid(GetWorld()) && !IsValid(Preview))
	{
		Preview = GetWorld()->SpawnActor<AWarZoneFootprintPreview>(
			AWarZoneFootprintPreview::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator);
	}

	// 오브젝트 자동 생성은 기본 꺼짐. 프로젝트 설정 > ProjectPG Objects 에서 켠다.
	// 켜지 않아도 콘솔 PG.SpawnObjectsFromPoints 로 수동 생성할 수 있다.
	const UPGObjectSettings* ObjectSettings = GetDefault<UPGObjectSettings>();
	if (IsValid(Preview) && ObjectSettings && ObjectSettings->bAutoSpawnFromLevelDesignPoints)
	{
		if (Preview->AreLevelDesignPointsBuilt())
			HandleLevelDesignPointsBuilt(Preview->GetLevelDesignPoints());
		else
			Preview->OnLevelDesignPointsBuilt.AddUObject(this, &AGameModePG::HandleLevelDesignPointsBuilt);
	}
}

void AGameModePG::HandleLevelDesignPointsBuilt(const TArray<FLevelDesignPoint>& Points)
{
	if (UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(this))
		Spawner->SpawnFromLevelDesignPoints(Points, _mapGenerationSeed);
	// 오브젝트 다음에 몬스터·보스·탈것. 같은 시드라 배치가 고정된다.
	const UPGCombatSettings* CombatSettings = GetDefault<UPGCombatSettings>(); // 9/26: 전투 설정은 ProjectPG Combat 으로 분리
	if (CombatSettings && CombatSettings->bAutoSpawnCombat)
		PGCombatSpawner::SpawnFromPoints(GetWorld(), Points, _mapGenerationSeed);

	// 시설 문: 지금 로드된 것부터 바꾸고, 나중에 들어오는 시설을 위해 10초 간격으로 6번 더.
	_doorConvertRuns = 0;
	ConvertLevelDoorsTick();
	GetWorldTimerManager().SetTimer(_doorConvertTimer, this, &AGameModePG::ConvertLevelDoorsTick, 10.0f, true);
}

void AGameModePG::ConvertLevelDoorsTick()
{
	PGLevelDoorConverter::ConvertLevelDoors(GetWorld());
	if (++_doorConvertRuns >= 7)
		GetWorldTimerManager().ClearTimer(_doorConvertTimer);
}

void AGameModePG::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void AGameModePG::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	_mapGenerationSeed = FDateTime::UtcNow().ToUnixTimestamp();
	FString SeedOption;
	if (FParse::Value(FCommandLine::Get(), TEXT("PGMapSeed="), SeedOption)
		&& !SeedOption.IsEmpty())
	{
		_mapGenerationSeed = FCString::Atoi64(*SeedOption);
	}
	_random.Initialize(_mapGenerationSeed);
	UE_LOG(LogTemp, Display, TEXT("Procedural map seed: %lld source=%s"),
		_mapGenerationSeed,
		SeedOption.IsEmpty() ? TEXT("UtcNow") : TEXT("-PGMapSeed"));
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
