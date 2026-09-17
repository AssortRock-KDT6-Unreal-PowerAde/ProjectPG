// Fill out your copyright notice in the Description page of Project Settings.


#include "GameModes/GameModePG.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "Actors/MapTile.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Components/MapGeneratorComponent.h"

AGameModePG::AGameModePG()
{
	_mapGenerator = CreateDefaultSubobject<UMapGeneratorComponent>(TEXT("MapGenerator"));
}

void AGameModePG::BeginPlay()
{
	Super::BeginPlay();
	_mapGenerator->Generate();
	
	// 논리용 빨간 타일은 숨기고, 보이는 맵은 WarZoneFootprintPreview 가 그린다.
	for (TActorIterator<AMapTile> It(GetWorld()); It; ++It)
	{
		It->SetActorHiddenInGame(true);    // 안 보이게 해라 → true
		It->SetActorEnableCollision(false);// 충돌 켜기 → false = 충돌 끄기
		It->SetActorTickEnabled(false);    // 매 프레임 업데이트 켜기 → false = 끄기
	}

	// 레벨에 Preview 가 없으면 하나 만든다. 이미 있으면 중복으로 만들지 않는다.
	if (IsValid(GetWorld())                        // 월드가 있고
		&& !IsValid(                               // 그리고 못 찾았으면
			UGameplayStatics::GetActorOfClass(     //   "이 종류 액터 하나 찾아줘"
				this,                              //   나(GameMode)가 있는 월드에서
				AWarZoneFootprintPreview::StaticClass())))  //   찾을 종류 = Preview
	{
		GetWorld()->SpawnActor<AWarZoneFootprintPreview>(   // 새로 만들어라
			AWarZoneFootprintPreview::StaticClass(),        // 이 종류로
			FVector::ZeroVector,                            // 위치 (0,0,0)
			FRotator::ZeroRotator);                         // 회전 0
	}
}

void AGameModePG::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

void AGameModePG::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);

	_mapGenerationSeed = FDateTime::UtcNow().ToUnixTimestamp(); // 기본값: 1970년부터 지금까지 흐른 초
	FString SeedOption;                                         // 빈 글자 상자 준비
	if (FParse::Value(                 // 글자 뭉치에서 값 찾기 함수
			FCommandLine::Get(),       //   에디터 실행 때 붙인 옵션 글자 전체
			TEXT("PGMapSeed="),        //   이 글자 뒤에 있는 걸
			SeedOption)                //   상자에 담아라 (찾으면 true)
		&& !SeedOption.IsEmpty())      // 그리고 상자가 비어있지 않으면
	{
		_mapGenerationSeed = FCString::Atoi64(*SeedOption); // 글자 "123" → 숫자 123
	}
	_random.Initialize(_mapGenerationSeed);  // 주사위를 이 시드로 세팅
	UE_LOG(LogTemp, Display, TEXT("Procedural map seed: %lld source=%s"),  // %lld = int64 숫자 출력 자리
		_mapGenerationSeed,
		SeedOption.IsEmpty() ? TEXT("UtcNow") : TEXT("-PGMapSeed"));      // 시드가 어디서 왔는지
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
