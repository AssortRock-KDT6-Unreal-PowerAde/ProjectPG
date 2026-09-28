// 생성 지점 원형 (노션 "생성 지점 원형", OBJ-116 ~ OBJ-133).
//
// 타일·시설 레벨을 만들 때 "여기에 상자가 올 수 있다", "여기는 탈출구 후보"라고 표시해 두는
// 마커다. 마커 자체는 아무 행동도 하지 않고, 서버의 UPGObjectSpawnerSubsystem이
// 시드로 어떤 후보를 실제로 만들지 정한다. 같은 시드면 같은 결과가 나온다.
//
// 컴포넌트 형태: 시설 액터(AProceduralFacilityActor 등)에 여러 개 붙일 때.
// 액터 형태(APGSpawnSocketActor): 레벨에 그냥 놓을 때.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Objects/PGObjectTypes.h"
#include "PGSpawnSocketComponent.generated.h"

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGSpawnSocketComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPGSpawnSocketComponent();

	UFUNCTION(BlueprintCallable, Category = "PG|Socket")
	void Configure(EPGSpawnSocketKind InKind, uint8 InTier = 1, float InSpawnChance = 1.0f);

	UFUNCTION(BlueprintCallable, Category = "PG|Socket")
	bool IsOccupied() const { return SpawnedActor != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "PG|Socket")
	AActor* GetSpawnedActor() const { return SpawnedActor; }

	void SetSpawnedActor(AActor* Actor) { SpawnedActor = Actor; }

	// 소켓 위치·시드 기반 결정적 시드. 월드 시드와 섞어 쓴다.
	int64 MakeSeed(int64 WorldSeed) const;

	// 어떤 종류의 오브젝트가 올 수 있는지.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Socket")
	EPGSpawnSocketKind Kind = EPGSpawnSocketKind::Generic;

	// 비어 있으면 종류만 맞으면 된다. 채우면 이 ObjectId들만 후보다 ("호환 태그").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Socket")
	TArray<FName> AllowedObjectIds;

	// 1~3. 고가치 루팅 지점일수록 높다. 카탈로그 행 Tier가 0이면 무시된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Socket", meta = (ClampMin = "1", ClampMax = "3"))
	uint8 Tier = 1;

	// 이 소켓에 실제로 무언가 생길 확률. 모든 상자 자리가 매판 다 차면 루팅이 지루해진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Socket", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SpawnChance = 1.0f;

	// 같은 그룹 이름을 가진 소켓 중 하나만 채운다 (예: 플레이어 시작 후보 4곳 중 1곳).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Socket")
	FName ExclusiveGroup;

protected:
	UPROPERTY(VisibleInstanceOnly, Category = "PG|Socket")
	TObjectPtr<AActor> SpawnedActor;
};

UCLASS()
class PROJECTPG_API APGSpawnSocketActor : public AActor
{
	GENERATED_BODY()

public:
	APGSpawnSocketActor();

	UFUNCTION(BlueprintCallable, Category = "PG|Socket")
	UPGSpawnSocketComponent* GetSocket() const { return Socket; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Socket")
	TObjectPtr<UPGSpawnSocketComponent> Socket;
};
