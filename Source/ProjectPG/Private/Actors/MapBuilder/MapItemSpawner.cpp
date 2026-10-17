#include "Actors/MapBuilder/MapItemSpawner.h"

#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/LootSlotActor.h"
#include "Actors/LootSlotComponent.h"
#include "Actor/InteractActor.h"
#include "Components/InventoryComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

void UMapItemSpawner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapItemSpawner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 상자를 바닥에 붙인다. 상자 전체(몸통·뚜껑)의 아랫면과 바닥의 높이 차이만큼 올린다.
// 1cm 를 더 올리는 이유: 바닥과 같은 높이면 면이 겹쳐서 깜빡거린다.
// 왜 C++ 상자 함수(ALootCrateActor::PlaceOnFloor)를 안 쓰나:
// 목록의 상자는 부모가 AInteractActor 면 무엇이든 되고, 팩에서 가져온 BP 는 몸통이 MeshComp 가 아니라 따로 단 부품일 수 있다.
// 그래서 상자 전체 크기로 잰다.
void UMapItemSpawner::PlaceOnFloor(AActor* Container, const FVector& FloorPoint)
{
	Container->SetActorLocation(FloorPoint);
	const FBox Bounds = Container->GetComponentsBoundingBox(true);
	if (Bounds.IsValid)
		Container->AddActorWorldOffset(FVector(0.0f, 0.0f, FloorPoint.Z - Bounds.Min.Z + 1.0f));
}

// Start→End 선을 쏴서 처음 닿는 바닥을 찾는다. 눈에 보이고 사람이 밟는 것만 바닥으로 친다.
// 보이는 표면(삼각형)으로 잰다. 시내 보도블록(SM_ground_mod_tile)은 충돌 상자가 보이는 면보다 높아서,
// 충돌 상자 위에 올리면 상자가 떠 보였다.
static bool TraceFloor(const UWorld* World, const FVector& Start, const FVector& End, const FCollisionQueryParams& Params, FHitResult& OutHit)
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Complex = Params;
	Complex.bTraceComplex = true;
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, Start, End, Objects, Complex);
	bool bFound = false;
	for (const FHitResult& Hit : Hits)
	{
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (Hit.bStartPenetrating || !Component || !Component->IsVisible() || Component->bHiddenInGame
			|| Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			continue;
		if (!bFound || Hit.Distance < OutHit.Distance)
		{
			OutHit = Hit;
			bFound = true;
		}
	}
	return bFound;
}

AInteractActor* UMapItemSpawner::SpawnContainer(UClass* ContainerClass, const FVector& FloorPoint, const FRotator& Rotation)
{
	// 주인(Owner)은 두지 않는다. F 키 상호작용(UInteractComponent)은 겨냥한 물체에 주인이 있으면 그 주인을 열려고 한다.
	// 맵을 주인으로 두면 상자를 겨냥해도 맵을 열려다가 아무 일도 안 일어난다.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AInteractActor* Container = GetWorld()->SpawnActor<AInteractActor>(ContainerClass, FloorPoint, Rotation, SpawnParameters);
	if (!IsValid(Container))
		return nullptr;

	// 팩에서 가져온 상자 BP 는 부품(뚜껑 등)이 "움직일 수 없음(Static)"으로 저장돼 있을 수 있다.
	// 그러면 게임 중에 못 움직여서 바닥 맞추기가 무시되고, 상자가 떠 있거나 바닥에 박힌다.
	// 그래서 전부 "움직일 수 있음(Movable)"으로 바꾼다.
	// 길찾기 영역에는 영향을 주지 않게 한다. 상자 수십 개가 판 시작 때 길찾기를 다시 계산하게 만들면 멈칫한다.
	TInlineComponentArray<USceneComponent*> SceneComponents(Container);
	for (USceneComponent* Component : SceneComponents)
	{
		Component->SetMobility(EComponentMobility::Movable);
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			Primitive->SetCanEverAffectNavigation(false);
	}
	PlaceOnFloor(Container, FloorPoint);
#if WITH_EDITOR
	Container->SetFolderPath(TEXT("RuntimeDesign/Items"));
#endif
	SpawnedContainers.Add(Container);
	return Container;
}

void UMapItemSpawner::SpawnTestContainerInFront(const FVector& SeatLocation, float Yaw)
{
#if WITH_EDITOR
	// 에디터에서 플레이(PIE)할 때만. 에디터 실행 파일로 -game 을 돌릴 때도 안 놓는다.
	if (GetWorld()->WorldType != EWorldType::PIE || GetWorld()->GetNetMode() == NM_Client || ContainerClasses.IsEmpty())
		return;

	// 바라보는 쪽 2.5m 앞. 시작 구역 담당이 트인 쪽(입구)을 보게 세우므로 벽 속이 아니다.
	const FVector Front = SeatLocation + FRotator(0.0f, Yaw, 0.0f).Vector() * 250.0f;
	FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MapTestContainerFloor), false);
	FloorQuery.AddIgnoredActors(SpawnedContainers);
	FHitResult Hit;
	const bool bFloor = TraceFloor(GetWorld(), Front + FVector(0.0f, 0.0f, 150.0f), Front - FVector(0.0f, 0.0f, 400.0f), FloorQuery, Hit);
	const FVector FloorPoint = bFloor ? FVector(Hit.ImpactPoint) : Front - FVector(0.0f, 0.0f, 120.0f);

	// 상자 종류는 돌아가며: 플레이어마다 다른 상자를 열어 볼 수 있게.
	UClass* ContainerClass = ContainerClasses[TestContainerCount % ContainerClasses.Num()];
	++TestContainerCount;
	// 상자가 나를 보게 돌린다(뚜껑 앞면이 보이게).
	if (const AInteractActor* Container = SpawnContainer(ContainerClass, FloorPoint, FRotator(0.0f, Yaw + 180.0f, 0.0f)))
	{
		UE_LOG(LogTemp, Display, TEXT("Loot spawn: test container %s placed 2.5m in front of a start seat (PIE only) floor=%d"),
			*Container->GetName(), bFloor);
	}
#endif
}

void UMapItemSpawner::AppendSlotsOf(AActor* Owner, TArray<FLootSlotRecord>& OutSlots, TSet<FIntPoint>& OutCells)
{
	if (!IsValid(Owner))
		return;
	TInlineComponentArray<ULootSlotComponent*> Slots(Owner);
	if (Slots.IsEmpty())
		return;
	auto CellOf = [](float X, float Y)
	{
		return FIntPoint(FMath::RoundToInt(X / MapBuild::DesignCellSize), FMath::RoundToInt(Y / MapBuild::DesignCellSize));
	};
	const bool bLevelSlot = Owner->IsA<ALootSlotActor>();
	if (!bLevelSlot)
	{
		// BP 하나 = 타일 하나(또는 여러 칸짜리 시설). 가장자리 25% 는 빼고 센다(옆 칸으로 삐져나온 소품 때문에 칸 수가 늘지 않게).
		const FBox Bounds = Owner->GetComponentsBoundingBox(true);
		const float Margin = MapBuild::DesignCellSize * 0.25f;
		const FIntPoint MinCell = CellOf(Bounds.Min.X + Margin, Bounds.Min.Y + Margin);
		const FIntPoint MaxCell = CellOf(Bounds.Max.X - Margin, Bounds.Max.Y - Margin);
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
			for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
				OutCells.Add(FIntPoint(X, Y));
	}
	// 묶음 = 자리 표시가 달린 BP 하나(같은 SlotGroup 끼리), 또는 레벨에 놓인 Loot Slot 들(같은 레벨·같은 SlotGroup 끼리).
	// BP 쪽 묶음 이름에 위치를 붙이는 이유: 지워진 타일의 이름은 나중에 다른 타일이 다시 쓸 수 있어서, 이름만으로는 구분이 안 된다.
	const FString Owned = bLevelSlot
		? Owner->GetLevel()->GetOutermost()->GetName()
		: FString::Printf(TEXT("%s@%d,%d"), *Owner->GetClass()->GetName(),
			FMath::RoundToInt(Owner->GetActorLocation().X), FMath::RoundToInt(Owner->GetActorLocation().Y));
	for (const ULootSlotComponent* Slot : Slots)
	{
		FLootSlotRecord& Record = OutSlots.AddDefaulted_GetRef();
		Record.SlotGroup = Slot->SlotGroup;
		Record.GroupKey = Owned + TEXT("|") + Slot->SlotGroup.ToString();
		// 노란 선 아래면 = 상자 바닥.
		Record.Bottom = Slot->GetComponentLocation() - Slot->GetUpVector() * Slot->GetScaledBoxExtent().Z;
		Record.Yaw = Slot->GetComponentRotation().Yaw;
		Record.ContainerClasses = Slot->ContainerClasses;
		if (bLevelSlot)
			OutCells.Add(CellOf(Record.Bottom.X, Record.Bottom.Y));
	}
}

void UMapItemSpawner::RecordLootSlots(AActor* Tile)
{
	AppendSlotsOf(Tile, RecordedSlots, RecordedSlotCells);
}

void UMapItemSpawner::ResetRecordedLootSlots()
{
	RecordedSlots.Reset();
	RecordedSlotCells.Reset();
}

// 상자 자리마다 상자 놓기.
// ① 상자 BP 목록(DA_MapAssets 의 LootContainerClasses)을 읽는다. 못 읽는 칸은 빼고 로그로 알린다.
// ② 사람이 타일·시설 BP 에 찍어 둔 상자 자리(LootSlot): 타일(묶음)마다 판 시드로 몇 개를 골라서 그 자리에 놓는다.
//    예) 자리 3개짜리 타일 → 어떤 판은 0개, 어떤 판은 그중 1개.
//    개수 범위는 DA_MapAssets 의 MinActiveSlotsPerGroup / MaxActiveSlotsPerGroup 칸에 있다.
//    왜 이렇게 바꿨나: 예전 지점은 코드가 칸 가운데에서 숫자로 정한 위치라, 언덕·소품이 있으면 상자가 묻히거나 떠 있었다.
// ③ 예전 방식(코드가 정한 Loot 지점): 자리를 아직 안 찍은 칸에만 쓴다. 그 자리의 씨앗으로 상자를 고르고, 바로 아래 바닥에 놓는다.
//    ②와 ③ 모두 아래로 선을 쏴서 바닥(창고 2층이면 2층 바닥)에 내려놓는다.
//    벽 속이 아니라 손 닿는 바닥에 있어야, 형님 서버의 "거리·시야 검사"를 통과해서 상자를 열 수 있다.
// 상자 안은 채우지 않는다. 안에 무엇이 들지는 형님 쪽(AInteractActor)이 정한다.
// 결과를 지문(loot_hash)으로 남긴다. 같은 시드면 같은 값이어야 한다(검사 기준).
void UMapItemSpawner::SpawnLootOnce()
{
	// 맵이 "시설 레벨 불러오기 끝남 → 끼임 정리 끝남" 이 된 다음에 한 번 부른다(AMapBuilder::CheckFacilityDesignLevels).
	// 왜 그 뒤: 레벨에 놓은 상자 자리(Loot Slot)는 그 레벨이 다 불러와져야 찾을 수 있다.
	if (bSpawned || !Map->bResolvedGameplayPointSafety || !Map->bFacilityLevelsSettled)
		return;
	bSpawned = true;
	// 서버만 놓는다. 접속한 사람은 서버가 놓은 상자를 전달(복제)받는다. 여기서 또 놓으면 상자가 두 벌이 된다.
	// 확인용: 몇 초 뒤 받은 상자 수와 그 안의 아이템 수를 로그에 남긴다(가까운 것만 오므로 서버가 놓은 수보다 적을 수 있다).
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		// 상자 종류(DA_MapAssets 목록)로 센다. 이름표(Tags)는 전달(복제)이 안 돼서 접속한 쪽에서는 늘 비어 있다.
		TArray<UClass*> LootClasses;
		for (const TSoftClassPtr<AInteractActor>& Entry : Map->GetMapAssets().LootContainerClasses)
			if (UClass* Class = Entry.LoadSynchronous())
				LootClasses.Add(Class);
		FTimerHandle CountTimer;
		TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
		GetWorld()->GetTimerManager().SetTimer(CountTimer, FTimerDelegate::CreateLambda([WeakWorld, LootClasses]()
		{
			if (!WeakWorld.IsValid())
				return;
			int32 ContainerCount = 0, Filled = 0, ItemsInside = 0;
			for (TActorIterator<AInteractActor> It(WeakWorld.Get()); It; ++It)
			{
				// 서버에서 전달(복제)받은 상자만 센다(내가 직접 만든 상자는 뺀다).
				if (It->HasAuthority() || !LootClasses.ContainsByPredicate([&It](const UClass* Class) { return It->IsA(Class); }))
					continue;
				++ContainerCount;
				// 상자 안 아이템 수: 형님 쪽 인벤토리가 서버에서 전달받아(OnRep) 채워 준, 이 상자 칸(MyActorGuid)의 아이템 수.
				const int32 Inside = It->InventoryComp && It->MyActorGuid.IsValid()
					? It->InventoryComp->GetItems(It->MyActorGuid).Num() : 0;
				ItemsInside += Inside;
				Filled += Inside > 0 ? 1 : 0;
			}
			UE_LOG(LogTemp, Display, TEXT("Loot spawn: client received containers=%d with_contents=%d items_inside=%d"),
				ContainerCount, Filled, ItemsInside);
		}), 5.0f, false);
		return;
	}

	// ① 상자 BP 목록.
	const UMapAssetSet& Assets = Map->GetMapAssets();
	ContainerClasses.Reset();
	TArray<FString> MissingClasses;
	for (const TSoftClassPtr<AInteractActor>& Entry : Assets.LootContainerClasses)
	{
		if (UClass* Class = Entry.LoadSynchronous())
			ContainerClasses.Add(Class);
		else
			MissingClasses.Add(Entry.ToString());
	}
	if (!MissingClasses.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Loot spawn: %d container BP(s) not found - skipped [%s]"),
			MissingClasses.Num(), *FString::Join(MissingClasses, TEXT(", ")));
	}
	if (ContainerClasses.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Loot spawn: no container BP in DA_MapAssets LootContainerClasses - no containers placed"));
		return;
	}

	// 바닥 찾기에서 맵 액터는 빼면 안 된다. 타일 바닥(HISM)이 맵 액터의 부품이라, 빼면 들판 바닥을 못 찾는다.
	FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MapItemFloor), false);

	int32 LootPointCount = 0;
	// 정해 둔 바닥 높이 ±60cm 안에서 바닥을 못 찾은 수(그때는 정해 둔 높이에 그대로 놓는다).
	int32 NoFloorCount = 0;
	TArray<FString> FloorSamples;
	int32 ContainerCount = 0;
	TMap<FString, int32> CountByClass;
	uint32 LootHash = 0;

	// 지점은 바닥보다 120cm 위에 찍혀 있다. 그래서 정해 둔 바닥 = 지점 - 120cm.
	// 그 높이 ±60cm 안에서만 바닥을 찾는다(바닥이 살짝 기울었거나 턱이 있는 만큼만 맞춘다).
	// 왜 끝까지 안 쏘나: 그러면 2층 바닥이 선에 안 걸리는 건물에서는 1층까지 떨어지고,
	// 호수 칸에서는 물속 호수 바닥까지 떨어졌다.
	auto FindFloor = [&](const FLevelDesignPoint& Point)
	{
		const FVector Designed = Point.WorldLocation - FVector(0.0f, 0.0f, 120.0f);
		FHitResult Hit;
		const bool bFloor = TraceFloor(GetWorld(), Designed + FVector(0.0f, 0.0f, 60.0f), Designed - FVector(0.0f, 0.0f, 60.0f), FloorQuery, Hit);
		if (!bFloor)
		{
			++NoFloorCount;
			if (FloorSamples.Num() < 6)
				FloorSamples.Add(Point.PointId.ToString());
		}
		return bFloor ? FVector(Hit.ImpactPoint) : Designed;
	};

	// ② 사람이 BP 에 찍어 둔 상자 자리(LootSlot). 묶음(타일)마다 몇 개를 골라 그 자리에 놓는다.
	// 자리 = 지워진 타일에서 옮겨 적은 것(RecordLootSlots) + 아직 살아 있는 액터(시설 BP, 레벨의 Loot Slot)에 달린 것.
	TArray<FLootSlotRecord> AllSlots = RecordedSlots;
	// 자리를 찍어 둔 칸. 이 칸의 예전 방식 지점은 건너뛴다(같은 칸에 두 방식이 겹치지 않게).
	TSet<FIntPoint> SlotCells = RecordedSlotCells;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		AppendSlotsOf(*It, AllSlots, SlotCells);

	TMap<FString, TArray<const FLootSlotRecord*>> SlotGroups;
	for (const FLootSlotRecord& Slot : AllSlots)
		SlotGroups.FindOrAdd(Slot.GroupKey).Add(&Slot);

	// 묶음 순서를 이름순으로 고정한다.
	// 왜: 액터를 찾는 순서가 판마다 달라서, 같은 상자인데도 loot_hash(결과 지문)가 판마다 달라졌다.
	SlotGroups.KeySort(TLess<FString>());
	int32 SlotContainerCount = 0;
	const int64 RaidSeed = Map->GetRaidSeed();
	for (TPair<FString, TArray<const FLootSlotRecord*>>& Group : SlotGroups)
	{
		TArray<const FLootSlotRecord*>& Slots = Group.Value;
		// 자리 순서를 위치로 고정한다(찾는 순서가 판마다 달라도 같은 시드면 같은 자리가 뽑히게).
		Slots.Sort([](const FLootSlotRecord& A, const FLootSlotRecord& B)
		{
			return A.Bottom.X != B.Bottom.X ? A.Bottom.X < B.Bottom.X
				: (A.Bottom.Y != B.Bottom.Y ? A.Bottom.Y < B.Bottom.Y : A.Bottom.Z < B.Bottom.Z);
		});
		// 묶음 주사위: 판 시드 + 첫 자리 위치 + 묶음 이름.
		const FVector First = Slots[0]->Bottom;
		uint32 GroupSeed = GetTypeHash(RaidSeed);
		GroupSeed = HashCombine(GroupSeed, GetTypeHash(FMath::RoundToInt(First.X)));
		GroupSeed = HashCombine(GroupSeed, GetTypeHash(FMath::RoundToInt(First.Y)));
		GroupSeed = HashCombine(GroupSeed, GetTypeHash(FMath::RoundToInt(First.Z)));
		GroupSeed = HashCombine(GroupSeed, GetTypeHash(Slots[0]->SlotGroup));
		FRandomStream Stream(static_cast<int32>(GroupSeed));

		// 몇 개: 최소~최대(0 이면 자리 수) 사이. 어떤 것: 섞어서 앞에서부터.
		const int32 MaxActive = Assets.MaxActiveSlotsPerGroup > 0 ? FMath::Min(Assets.MaxActiveSlotsPerGroup, Slots.Num()) : Slots.Num();
		const int32 MinActive = FMath::Min(Assets.MinActiveSlotsPerGroup, MaxActive);
		const int32 ActiveCount = Stream.RandRange(MinActive, MaxActive);
		for (int32 Index = Slots.Num() - 1; Index > 0; --Index)
			Slots.Swap(Index, Stream.RandRange(0, Index));

		for (int32 Index = 0; Index < ActiveCount; ++Index)
		{
			const FLootSlotRecord& Slot = *Slots[Index];
			// 상자 종류: 이 자리에만 정한 목록이 있으면 거기서, 없으면 DA_MapAssets 목록에서.
			TArray<UClass*> Choices;
			for (const TSoftClassPtr<AInteractActor>& Entry : Slot.ContainerClasses)
				if (UClass* Class = Entry.LoadSynchronous())
					Choices.Add(Class);
			if (Choices.IsEmpty())
				for (UClass* Class : ContainerClasses)
					Choices.Add(Class);
			UClass* ContainerClass = Choices[Stream.RandRange(0, Choices.Num() - 1)];

			// 언덕 등으로 땅이 살짝 다르면 ±60cm 안에서 맞춘다.
			FHitResult Hit;
			const bool bFloor = TraceFloor(GetWorld(), Slot.Bottom + FVector(0.0f, 0.0f, 60.0f), Slot.Bottom - FVector(0.0f, 0.0f, 60.0f), FloorQuery, Hit);
			if (!bFloor)
				++NoFloorCount;
			const FVector FloorPoint = bFloor ? FVector(Hit.ImpactPoint) : Slot.Bottom;

			AInteractActor* Container = SpawnContainer(ContainerClass, FloorPoint, FRotator(0.0f, Slot.Yaw, 0.0f));
			if (!Container)
				continue;
			FloorQuery.AddIgnoredActor(Container);
			++ContainerCount;
			++SlotContainerCount;
			++CountByClass.FindOrAdd(ContainerClass->GetName());

			const FVector Placed = Container->GetActorLocation();
			LootHash = HashCombine(LootHash, FCrc::StrCrc32(*ContainerClass->GetName()));
			LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.X)));
			LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.Y)));
			LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.Z)));
		}
	}

	// ③ 예전 방식: 코드가 정한 Loot 지점. 자리를 찍은 칸은 건너뛴다. 끄면 전부 건너뛴다.
	int32 LegacySkipped = 0;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Loot)
			continue;
		++LootPointCount;
		if (!Assets.bUseLegacyLootPoints || SlotCells.Contains(Point.GridCell))
		{
			++LegacySkipped;
			continue;
		}

		// 이 자리의 씨앗으로만 주사위를 굴린다. 그러면 시각·순서와 상관없이 같은 시드면 같은 자리에 같은 상자가 나온다.
		// 상자 종류: 씨앗을 한 번 섞어서(CRC) 만든 주사위. 자리 씨앗끼리 값이 비슷해서, 그대로 쓰면 한쪽으로 쏠렸다.
		FRandomStream ChoiceStream(static_cast<int32>(FCrc::MemCrc32(&Point.PointSeed, sizeof(Point.PointSeed))));
		UClass* ContainerClass = ContainerClasses[ChoiceStream.RandRange(0, ContainerClasses.Num() - 1)];
		// 돌림 방향: 자리 씨앗(64비트)을 32비트로 접어 만든 주사위.
		FRandomStream Stream(static_cast<int32>(Point.PointSeed ^ (Point.PointSeed >> 32)));
		const FRotator Rotation(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f);

		const FVector FloorPoint = FindFloor(Point);
		AInteractActor* Container = SpawnContainer(ContainerClass, FloorPoint, Rotation);
		if (!Container)
			continue;
		FloorQuery.AddIgnoredActor(Container);
		++ContainerCount;
		++CountByClass.FindOrAdd(ContainerClass->GetName());

		// ④ 지문: 어느 자리에, 어떤 상자가, 어디에.
		const FVector Placed = Container->GetActorLocation();
		LootHash = HashCombine(LootHash, FCrc::StrCrc32(*Point.PointId.ToString()));
		LootHash = HashCombine(LootHash, FCrc::StrCrc32(*ContainerClass->GetName()));
		LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.X)));
		LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.Y)));
		LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.Z)));
	}

	TArray<FString> ClassSummary;
	for (const TPair<FString, int32>& Pair : CountByClass)
		ClassSummary.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key, Pair.Value));
	UE_LOG(LogTemp, Display,
		TEXT("Loot spawn: containers=%d (slots: %d of %d in %d groups, slot_cells=%d, from_removed_tiles=%d) (legacy points: %d of %d, %s) container_types=%d no_floor=%d loot_hash=%08X by_type=[%s] no_floor_sample=[%s]"),
		ContainerCount, SlotContainerCount, AllSlots.Num(), SlotGroups.Num(), SlotCells.Num(), RecordedSlots.Num(),
		LootPointCount - LegacySkipped, LootPointCount, Assets.bUseLegacyLootPoints ? TEXT("on") : TEXT("off"),
		ContainerClasses.Num(), NoFloorCount, LootHash,
		*FString::Join(ClassSummary, TEXT(" ")), *FString::Join(FloorSamples, TEXT(" ")));
}
