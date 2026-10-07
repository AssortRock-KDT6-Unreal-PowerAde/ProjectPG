#include "Actors/MapBuilder/MapItemSpawner.h"

#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/WorldItemActor.h"
#include "Actors/LootCrateActor.h"
#include "Components/InventoryComponent.h"
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
// ② 상자 자리마다: 그 자리 씨앗으로 주사위 → 먼저 "상자냐 바닥이냐" 를 정하고(LootCrateChance),
//    자리 등급만큼 아이템을 무게대로 뽑는다(그 등급에서 나올 수 있는 줄만).
// ③ 상자면: 자리 바로 아래 바닥에 상자 하나를 놓고 뽑힌 것을 전부 넣는다.
//    바닥이면: 자리 둘레에 둥글게 벌려 하나씩 놓는다(아이템 모양의 작은 상자, 안에 그 아이템 하나).
//    둘 다 아래로 선을 쏴서 바닥(창고 2층이면 2층 바닥)에 내려놓는다 — 벽 속이 아니라 손 닿는 바닥이어야
//    형님 서버의 "거리·시야 검사" 를 통과해 열 수 있다.
// ④ 결과를 지문(item_hash)으로 남긴다. 같은 시드면 같은 값이어야 한다(검사 기준).
void UMapItemSpawner::SpawnLootOnce()
{
	if (bSpawned || !Map->bResolvedGameplayPointSafety)
		return;
	bSpawned = true;
	// 서버만 놓는다. 들어온 사람은 서버가 놓은 상자·물건을 복제로 받는다(여기서 또 놓으면 두 벌이 된다).
	// 확인용: 몇 초 뒤 받은 상자·물건 수와 그 안의 아이템 수를 로그에 남긴다(가까운 것만 오므로 서버 수보다 적을 수 있다).
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		FTimerHandle CountTimer;
		TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
		GetWorld()->GetTimerManager().SetTimer(CountTimer, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (!WeakWorld.IsValid())
				return;
			// 안 아이템 수: 형님 인벤토리 복제(OnRep)가 채워 준 이 상자 칸(MyActorGuid)의 아이템 수.
			auto CountInside = [](const AInteractActor* Container)
			{
				return Container->InventoryComp && Container->MyActorGuid.IsValid()
					? Container->InventoryComp->GetItems(Container->MyActorGuid).Num() : 0;
			};
			int32 FloorCount = 0, Shaped = 0, FloorFilled = 0, CrateCount = 0, CrateFilled = 0, CrateItems = 0;
			for (TActorIterator<AWorldItemActor> It(WeakWorld.Get()); It; ++It)
			{
				++FloorCount;
				if (!It->GetItemID().IsNone())
					++Shaped;
				if (CountInside(*It) > 0)
					++FloorFilled;
			}
			for (TActorIterator<ALootCrateActor> It(WeakWorld.Get()); It; ++It)
			{
				++CrateCount;
				const int32 Inside = CountInside(*It);
				CrateItems += Inside;
				if (Inside > 0)
					++CrateFilled;
			}
			UE_LOG(LogTemp, Display, TEXT("Item spawn: client received floor_items=%d with_item_id=%d floor_with_contents=%d crates=%d crates_with_contents=%d crate_items=%d"),
				FloorCount, Shaped, FloorFilled, CrateCount, CrateFilled, CrateItems);
		}), 5.0f, false);
		return;
	}

	const UMapAssetSet& Assets = Map->GetMapAssets();
	const UDataTable* LootTable = Assets.LootSpawnTable.LoadSynchronous();
	UClass* ItemClass = Assets.WorldItemClass.LoadSynchronous();
	if (!ItemClass)
		ItemClass = AWorldItemActor::StaticClass();
	UClass* CrateClass = Assets.LootCrateClass.LoadSynchronous();
	if (!CrateClass)
	{
		// BP 가 없으면 C++ 기본 상자(메시 없음)라 안 보인다 → 알린다.
		UE_LOG(LogTemp, Warning, TEXT("Item spawn: crate BP missing (%s) - using plain ALootCrateActor (no mesh)"),
			*Assets.LootCrateClass.ToString());
		CrateClass = ALootCrateActor::StaticClass();
	}
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
	int32 CrateCount = 0, CrateItemCount = 0, FloorItemCount = 0, AddFailures = 0;
	uint32 ItemHash = 0;

	// 지점은 바닥보다 120cm 위에 찍혀 있다 → 정해 둔 바닥 = 지점 - 120cm.
	// 그 높이 ±60cm 안에서만 바닥을 찾는다(바닥이 살짝 기울거나 턱이 있는 만큼만 맞춘다).
	// 왜 끝까지 안 쏘나: 그러면 2층 바닥이 눈 선에 안 걸리는 건물에서 1층까지, 호수 칸에서 숨은 호수 바닥까지 떨어졌다.
	auto FindFloor = [&](const FLevelDesignPoint& Point, const FVector& Spread)
	{
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
		return bFloor ? FVector(Hit.ImpactPoint) : Designed;
	};
	auto HashLocation = [&ItemHash](const FVector& Location)
	{
		ItemHash = HashCombine(ItemHash, GetTypeHash(FMath::RoundToInt(Location.X)));
		ItemHash = HashCombine(ItemHash, GetTypeHash(FMath::RoundToInt(Location.Y)));
		ItemHash = HashCombine(ItemHash, GetTypeHash(FMath::RoundToInt(Location.Z)));
	};

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
		// 상자냐 바닥이냐도 이 자리 씨앗으로 → 같은 시드면 같은 자리가 늘 같은 쪽.
		// 왜 주사위를 따로 두나: 자리 씨앗끼리 비슷해서 같은 주사위의 첫 값이 한쪽으로 쏠렸다(0.6 인데 상자가 3할뿐).
		//   씨앗을 한 번 섞어(CRC) 만든 주사위는 고르게 나온다. 아이템 뽑기 주사위(Stream)는 예전 순서 그대로 둔다.
		FRandomStream ChoiceStream(static_cast<int32>(FCrc::MemCrc32(&Point.PointSeed, sizeof(Point.PointSeed))));
		const bool bCrate = ChoiceStream.FRand() < Map->LootCrateChance;
		const int32 ItemCount = FMath::Clamp<int32>(Point.Tier, 1, 3);
		ItemHash = HashCombine(ItemHash, FCrc::StrCrc32(*Point.PointId.ToString()));
		ItemHash = HashCombine(ItemHash, GetTypeHash(bCrate));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = Map;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// ③-가 상자: 자리 바로 아래 바닥에 하나 놓고 뽑힌 것을 전부 넣는다.
		ALootCrateActor* Crate = nullptr;
		if (bCrate)
		{
			const FVector FloorPoint = FindFloor(Point, FVector::ZeroVector);
			Crate = GetWorld()->SpawnActor<ALootCrateActor>(
				CrateClass, FloorPoint, FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), SpawnParameters);
			if (!IsValid(Crate))
				continue;
			Crate->PlaceOnFloor(FloorPoint);
#if WITH_EDITOR
			Crate->SetFolderPath(TEXT("RuntimeDesign/Items"));
#endif
			SpawnedItems.Add(Crate);
			FloorQuery.AddIgnoredActor(Crate);
			++CrateCount;
			HashLocation(FloorPoint);
		}

		for (int32 ItemIndex = 0; ItemIndex < ItemCount; ++ItemIndex)
		{
			const FLootSpawnRow* Picked = PickWeighted(Pool, TotalWeight, Stream);
			const int32 Quantity = Stream.RandRange(Picked->MinQuantity, FMath::Max(Picked->MinQuantity, Picked->MaxQuantity));
			// ④ 지문: 무엇이, 몇 개.
			ItemHash = HashCombine(ItemHash, FCrc::StrCrc32(*Picked->ItemID.ToString()));
			ItemHash = HashCombine(ItemHash, GetTypeHash(Quantity));

			if (Crate)
			{
				if (Crate->AddLoot(Picked->ItemID, Quantity))
					++CrateItemCount;
				else
					++AddFailures;
				continue;
			}

			// ③-나 바닥: 여러 개면 자리 둘레 45cm 원 위에 고르게 벌린다(한 점에 겹쳐 쌓이지 않게).
			const float Angle = (ItemIndex * 2.0f * PI) / ItemCount + Stream.FRandRange(0.0f, 0.5f);
			const FVector Spread = ItemCount > 1 ? FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * 45.0f : FVector::ZeroVector;
			const FVector FloorPoint = FindFloor(Point, Spread);
			const float Yaw = Stream.FRandRange(0.0f, 360.0f);

			AWorldItemActor* Item = GetWorld()->SpawnActor<AWorldItemActor>(
				ItemClass, FloorPoint, FRotator(0.0f, Yaw, 0.0f), SpawnParameters);
			if (!IsValid(Item))
				continue;
			// 빈 껍데기가 바닥에 남지 않게: 못 넣었으면 바로 지운다.
			if (!Item->SetItem(Picked->ItemID, Quantity))
			{
				++AddFailures;
				Item->Destroy();
				continue;
			}
			Item->PlaceOnFloor(FloorPoint);
#if WITH_EDITOR
			Item->SetFolderPath(TEXT("RuntimeDesign/Items"));
#endif
			SpawnedItems.Add(Item);
			FloorQuery.AddIgnoredActor(Item);
			++FloorItemCount;
			HashLocation(FloorPoint);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("Item spawn: loot_points=%d crates=%d crate_items=%d floor_items=%d items=%d add_failures=%d table_rows=%d unknown_ids=%d no_floor=%d item_hash=%08X unknown=[%s] no_floor_sample=[%s]"),
		LootPointCount, CrateCount, CrateItemCount, FloorItemCount, CrateItemCount + FloorItemCount, AddFailures,
		Rows.Num(), UnknownIds.Num(), NoFloorCount, ItemHash,
		*FString::Join(UnknownIds, TEXT(",")), *FString::Join(FloorSamples, TEXT(" ")));

	// 서버 쪽 확인: 상자마다 실제로 들어간 아이템 수(형님 인벤토리에서 다시 읽음). 빈 상자가 있으면 이상한 것.
	int32 EmptyCrates = 0, CrateItemsInside = 0, FloorItemsInside = 0;
	for (const TObjectPtr<AActor>& Spawned : SpawnedItems)
	{
		const AInteractActor* Container = Cast<AInteractActor>(Spawned.Get());
		if (!IsValid(Container) || !Container->InventoryComp)
			continue;
		const int32 Inside = Container->InventoryComp->GetItems(Container->MyActorGuid).Num();
		if (Container->IsA<ALootCrateActor>())
		{
			CrateItemsInside += Inside;
			EmptyCrates += Inside == 0 ? 1 : 0;
		}
		else
		{
			FloorItemsInside += Inside;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Item spawn: server containers crate_items_inside=%d floor_items_inside=%d empty_crates=%d"),
		CrateItemsInside, FloorItemsInside, EmptyCrates);
}
