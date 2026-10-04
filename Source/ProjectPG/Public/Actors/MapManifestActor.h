// 맵 설계도를 들어온 사람(클라)에게 보내는 액터 — 리슨 서버.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Common/GameDefine.h"
#include "MapManifestActor.generated.h"

// 칸 쪽지 한 장(형님 AMapTile 의 위치·종류).
USTRUCT()
struct FMapTileRecord
{
	GENERATED_BODY()

	UPROPERTY()
	FVector_NetQuantize Location = FVector::ZeroVector;

	UPROPERTY()
	ETileType Type = ETileType::None;
};

// 맵 설계도 복제 담당.
// 왜 있나: 맵은 서버에서만 만들어진다 — 형님 생성기(게임모드)가 칸 쪽지(AMapTile) 900장을 쓰고, 맵 클래스가 그걸 읽어 세운다.
//          게임모드·칸 쪽지는 복제가 안 돼서, 들어온 사람 화면에는 바닥도 타일도 없었다.
// 하는 일: 서버가 칸 쪽지 900장(위치·종류) + 판 시드 + 시작 구역 상자 크기를 이 액터에 담으면, 복제로 들어온 사람에게 간다.
//          들어온 사람 쪽에서는 같은 칸 쪽지를 자기 컴퓨터에 다시 만들고 → 맵 클래스가 서버와 같은 방법으로 맵을 세운다.
//          같은 쪽지 + 같은 시드 = 같은 맵(맵 만드는 계산에는 시각·순서에 따른 값이 없다). 확인은 양쪽 layout_hash 비교.
// 큰 메시·레벨을 보내지 않고 900장 쪽지(약 7KB)만 보낸다.
UCLASS()
class PROJECTPG_API AMapManifestActor : public AActor
{
	GENERATED_BODY()

public:
	AMapManifestActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버: 칸 쪽지·시드를 담는다(맵 클래스가 쪽지를 다 읽은 직후 한 번).
	void SetManifest(const TArray<FMapTileRecord>& InTiles, int64 InSeed, int32 InStartRange);

	int64 GetSeed() const { return Seed; }
	int32 GetStartRange() const { return StartRange; }

private:
	// 들어온 사람: 설계도가 도착하면 칸 쪽지를 만들고 맵 클래스에 시드를 알려 준다.
	UFUNCTION()
	void OnRep_Tiles();

	UPROPERTY(ReplicatedUsing = OnRep_Tiles)
	TArray<FMapTileRecord> Tiles;

	UPROPERTY(Replicated)
	int64 Seed = 0;

	UPROPERTY(Replicated)
	int32 StartRange = 4;

	bool bBuiltLocalTiles = false;
};
