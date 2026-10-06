#include "Actors/MapBuilder/MapItemSpawner.h"

#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/WorldItemActor.h"
#include "Core/ItemSubSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Components/PrimitiveComponent.h"

// 무게대로 한 줄 뽑기: 0 ~ 무게합-1 사이 숫자를 굴려, 줄 무게를 빼 가다 음수가 되는 줄.
// 예: 붕대 5·권총 3·소총 2 → 0~9 를 굴려 0~4 붕대, 5~7 권총, 8~9 소총.
const FLootSpawnRow* UMapItemSpawner::PickWeighted(const TArray<const FLootSpawnRow*>& Pool, int32 TotalWeight, FRandomStream& Stream)
{
	int32 Roll = Stream.RandRange(0, TotalWeight - 1);
	for (const FLootSpawnRow* Row : Pool)
	{
		Roll -= Row->Weight;
		if (Roll < 0)
			return Row;
	}
	return Pool.Last();
}

void UMapItemSpawner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapItemSpawner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 상자 자리마다 아이템 놓기.
// ① 아이템 나오는 표(DT_LootSpawn)를 읽고, 아이템 표(ItemTable)에 없는 번호는 빼 둔다(로그로 알림).
// ② 상자 자리마다: 그 자리 씨앗으로 주사위 → 자리 등급만큼 아이템을 무게대로 뽑는다(그 등급에서 나올 수 있는 줄만).
// ③ 자리 둘레에 둥글게 벌려 놓고, 아래로 선을 쏴서 바닥(창고 2층이면 2층 바닥)에 내려놓는다.
// ④ 결과를 지문(item_hash)으로 남긴다. 같은 시드면 같은 값이어야 한다(검사 기준).
void UMapItemSpawner::SpawnLootOnce()
{
	if (bSpawned || !Map->bResolvedGameplayPointSafety)
		return;
	bSpawned = true;
	// 서버만 놓는다. 들어온 사람은 서버가 놓은 아이템을 복제로 받는다(여기서 또 놓으면 두 벌이 된다).
	// 확인용: 몇 초 뒤 받은 아이템 수를 로그에 남긴다(가까운 것만 오므로 서버 수보다 적을 수 있다).
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		FTimerHandle CountTimer;
		TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
		GetWorld()->GetTimerManager().SetTimer(CountTimer, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (!WeakWorld.IsValid())
				return;
			int32 Count = 0, Shaped = 0;
			for (TActorIterator<AWorldItemActor> It(WeakWorld.Get()); It; ++It)
			{
				++Count;
				if (!It->GetItemID().IsNone())
					++Shaped;
			}
			UE_LOG(LogTemp, Display, TEXT("Item spawn: client received items=%d with_item_id=%d"), Count, Shaped);
		}), 5.0f, false);
		return;
	}

	const UMapAssetSet& Assets = Map->GetMapAssets();
	const UDataTable* LootTable = Assets.LootSpawnTable.LoadSynchronous();
	UClass* ItemClass = Assets.WorldItemClass.LoadSynchronous();
	if (!ItemClass)
		ItemClass = AWorldItemActor::StaticClass();
	if (!LootTable)
	{
		UE_LOG(LogTemp, Error, TEXT("Item spawn: loot table missing (%s) - no items placed"),
			*Assets.LootSpawnTable.ToString());
		return;
	}

	// ① 표에 없는 아이템 번호는 놓아 봐야 줍지도 못한다 → 미리 뺀다.
	UItemSubSystem* Items = UItemSubSystem::Get(Map);
	TArray<FLootSpawnRow*> AllRows;
	LootTable->GetAllRows<FLootSpawnRow>(TEXT("MapItemSpawner"), AllRows);
	TArray<const FLootSpawnRow*> Rows;
	TArray<FString> UnknownIds;
	for (const FLootSpawnRow* Row : AllRows)
	{
		if (!Row || Row->Weight <= 0)
			continue;
		if (!Items || !Items->GetItem(Row->ItemID))
		{
			UnknownIds.Add(Row->ItemID.ToString());
			continue;
		}
		Rows.Add(Row);
	}

	// 맵 액터는 빼면 안 된다: 타일 바닥(묶음 HISM)이 맵 액터의 부품이라, 빼면 들판 바닥을 못 찾는다.
	FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MapItemFloor), false);

	int32 LootPointCount = 0;
	// 정해 둔 바닥 높이 ±60cm 안에서 바닥을 못 찾은 수(그때는 정해 둔 높이에 그대로 놓는다).
	int32 NoFloorCount = 0;
	TArray<FString> FloorSamples;
	uint32 ItemHash = 0;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Loot)
			continue;
		++LootPointCount;

		// ② 이 자리에서 나올 수 있는 줄(자리 등급 ≥ 줄의 최소 등급)과 무게 합.
		TArray<const FLootSpawnRow*> Pool;
		int32 TotalWeight = 0;
		for (const FLootSpawnRow* Row : Rows)
		{
			if (Row->MinTier <= Point.Tier)
			{
				Pool.Add(Row);
				TotalWeight += Row->Weight;
			}
		}
		if (TotalWeight <= 0)
			continue;

		// 자리 씨앗(64비트)을 32비트로 접어 주사위를 만든다. 시각·순서와 상관없이 자리마다 늘 같은 주사위.
		FRandomStream Stream(static_cast<int32>(Point.PointSeed ^ (Point.PointSeed >> 32)));
		const int32 ItemCount = FMath::Clamp<int32>(Point.Tier, 1, 3);
		for (int32 ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex)
		{
			const FLootSpawnRow* Picked = PickWeighted(Pool, TotalWeight, Stream);
			const int32 Quantity = Stream.RandRange(Picked->MinQuantity, FMath::Max(Picked->MinQuantity, Picked->MaxQuantity));

			// ③ 여러 개면 자리 둘레 45cm 원 위에 고르게 벌린다(한 점에 겹쳐 쌓이지 않게).
			const float Angle = (ItemIndex * 2.0f * PI) / ItemCount + Stream.FRandRange(0.0f, 0.5f);
			const FVector Spread = ItemCount > 1 ? FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 45.0f : FVector::ZeroVector;
			// 지점은 바닥보다 120cm 위에 찍혀 있다 → 정해 둔 바닥 = 지점 - 120cm.
			// 그 높이 ±60cm 안에서만 바닥을 찾는다(바닥이 살짝 기울거나 턱이 있는 만큼만 맞춘다).
			// 왜 끝까지 안 쏘나: 그러면 2층 바닥이 눈 선에 안 걸리는 건물에서 1층까지, 호수 칸에서 숨은 호수 바닥까지 떨어졌다.
			const FVector Designed = Point.WorldLocation + Spread - FVector(0.0f, 0.0f, 120.0f);
			FHitResult Hit;
			const bool bFloor = GetWorld()->LineTraceSingleByChannel(
				Hit, Designed + FVector(0.0f, 0.0f, 60.0f), Designed - FVector(0.0f, 0.0f, 60.0f), ECC_Visibility, FloorQuery);
			if (!bFloor)
			{
				++NoFloorCount;
				if (FloorSamples.Num() < 6)
					FloorSamples.Add(Point.PointId.ToString());
			}
			const FVector FloorPoint = bFloor ? Hit.ImpactPoint : Designed;

			FActorSpawnParameters SpawnParameters;
			SpawnParameters.Owner = Map;
			SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AWorldItemActor* Item = GetWorld()->SpawnActor<AWorldItemActor>(
				ItemClass, FloorPoint, FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), SpawnParameters);
			if (!IsValid(Item))
				continue;
			Item->SetItem(Picked->ItemID, Quantity);
			Item->PlaceOnFloor(FloorPoint);
#if WITH_EDITOR
			Item->SetFolderPath(TEXT("RuntimeDesign/Items"));
#endif
			SpawnedItems.Add(Item);
			FloorQuery.AddIgnoredActor(Item);

			// ④ 지문: 어느 자리에, 무엇이, 몇 개, 어디(cm 반올림)에.
			ItemHash = HashCombine(ItemHash, FCrc::StrCrc32(*Point.PointId.ToString()));
			ItemHash = HashCombine(ItemHash, FCrc::StrCrc32(*Picked->ItemID.ToString()));
			ItemHash = HashCombine(ItemHash, GetTypeHash(Quantity));
			ItemHash = HashCombine(ItemHash, GetTypeHash(FMath::RoundToInt(FloorPoint.X)));
			ItemHash = HashCombine(ItemHash, GetTypeHash(FMath::RoundToInt(FloorPoint.Y)));
			ItemHash = HashCombine(ItemHash, GetTypeHash(FMath::RoundToInt(FloorPoint.Z)));
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("Item spawn: loot_points=%d items=%d table_rows=%d unknown_ids=%d no_floor=%d item_hash=%08X unknown=[%s] no_floor_sample=[%s]"),
		LootPointCount, SpawnedItems.Num(), Rows.Num(), UnknownIds.Num(), NoFloorCount, ItemHash,
		*FString::Join(UnknownIds, TEXT(",")), *FString::Join(FloorSamples, TEXT(" ")));
}
