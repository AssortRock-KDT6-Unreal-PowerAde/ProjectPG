#include "Actors/MapManifestActor.h"

#include "Actors/MapBuilder.h"
#include "Actors/MapTile.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

AMapManifestActor::AMapManifestActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// 맵 전체의 설계도라 어디 있든 모두에게 보낸다(거리로 끊지 않음).
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AMapManifestActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// 시드·상자 크기가 칸 쪽지보다 먼저(같은 묶음으로) 도착해야 OnRep_Tiles 에서 바로 쓸 수 있다 — 같은 액터의 첫 복제에 함께 실린다.
	DOREPLIFETIME_CONDITION(AMapManifestActor, Seed, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMapManifestActor, StartRange, COND_InitialOnly);
	DOREPLIFETIME_CONDITION(AMapManifestActor, Tiles, COND_InitialOnly);
}

void AMapManifestActor::SetManifest(const TArray<FMapTileRecord>& InTiles, int64 InSeed, int32 InStartRange)
{
	Tiles = InTiles;
	Seed = InSeed;
	StartRange = InStartRange;
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("Map manifest: tiles=%d seed=%lld start_range=%d (server)"), Tiles.Num(), Seed, StartRange);
}

// 들어온 사람 쪽.
// ① 맵 클래스가 없으면(레벨에 안 놓인 맵이면) 하나 만든다 — 서버는 게임모드가 만들지만 클라에는 게임모드가 없다.
// ② 시드·상자 크기를 맵 클래스에 알려 준다(맵 계산이 게임모드 대신 이 값을 쓴다).
// ③ 칸 쪽지를 같은 자리에 다시 만든다(숨김·충돌 없음 — 서버의 형님 쪽지와 같은 모습).
//    맵 클래스는 0.2초마다 쪽지를 찾다가, 이게 생기면 서버와 같은 순서로 맵을 세운다.
void AMapManifestActor::OnRep_Tiles()
{
	UWorld* World = GetWorld();
	if (bBuiltLocalTiles || !World || World->GetNetMode() != NM_Client || Tiles.IsEmpty())
		return;
	bBuiltLocalTiles = true;

	AMapBuilder* Map = nullptr;
	for (TActorIterator<AMapBuilder> It(World); It; ++It)
	{
		Map = *It;
		break;
	}
	if (!Map)
		Map = World->SpawnActor<AMapBuilder>(AMapBuilder::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
	if (Map)
		Map->ApplyReplicatedManifest(Seed, StartRange);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const FMapTileRecord& Record : Tiles)
	{
		AMapTile* Tile = World->SpawnActor<AMapTile>(AMapTile::StaticClass(), Record.Location, FRotator::ZeroRotator, Params);
		if (!Tile)
			continue;
		Tile->SetType(Record.Type);
		Tile->SetActorHiddenInGame(true);
		Tile->SetActorEnableCollision(false);
		Tile->SetActorTickEnabled(false);
	}
	UE_LOG(LogTemp, Display, TEXT("Map manifest: tiles=%d seed=%lld start_range=%d (client rebuilt)"), Tiles.Num(), Seed, StartRange);
}
