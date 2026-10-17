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

void UMapItemSpawner::PlaceOnFloor(AActor* Container, const FVector& FloorPoint)
{
	Container->SetActorLocation(FloorPoint);
	const FBox Bounds = Container->GetComponentsBoundingBox(true);
	if (Bounds.IsValid)
		Container->AddActorWorldOffset(FVector(0.0f, 0.0f, FloorPoint.Z - Bounds.Min.Z + 1.0f));
}

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

AInteractActor* UMapItemSpawner::SpawnContainer(
	UClass* ContainerClass,
	const FVector& FloorPoint,
	const FRotator& Rotation)
{
	UWorld* World = GetWorld();

	UE_LOG(LogTemp, Warning,
		TEXT("[LootDebug][SpawnContainer] ENTER World=%s Class=%s"),
		*GetNameSafe(World),
		*GetNameSafe(ContainerClass));

	if (!IsValid(World))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[LootDebug][SpawnContainer] FAIL: World invalid"));
		return nullptr;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[LootDebug][SpawnContainer] NetMode=%d IsServer=%d IsClient=%d Location=%s"),
		static_cast<int32>(World->GetNetMode()),
		World->GetNetMode() != NM_Client,
		World->GetNetMode() == NM_Client,
		*FloorPoint.ToString());

	if (!IsValid(ContainerClass))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[LootDebug][SpawnContainer] FAIL: ContainerClass invalid"));
		return nullptr;
	}

	if (World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[LootDebug][SpawnContainer] BLOCKED: client cannot spawn"));
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UE_LOG(LogTemp, Warning,
		TEXT("[LootDebug][SpawnContainer] SpawnActor BEGIN Class=%s"),
		*GetNameSafe(ContainerClass));

	AInteractActor* Container =
		World->SpawnActor<AInteractActor>(
			ContainerClass,
			FloorPoint,
			Rotation,
			SpawnParameters);

	if (!IsValid(Container))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[LootDebug][SpawnContainer] SpawnActor FAILED Class=%s"),
			*GetNameSafe(ContainerClass));
		return nullptr;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[LootDebug][SpawnContainer] SUCCESS Actor=%s Class=%s Authority=%d Replicates=%d Location=%s"),
		*GetNameSafe(Container),
		*GetNameSafe(Container->GetClass()),
		Container->HasAuthority(),
		Container->GetIsReplicated(),
		*Container->GetActorLocation().ToString());

#if WITH_EDITOR
	Container->SetFolderPath(TEXT("RuntimeDesign/Items"));
#endif

	SpawnedContainers.Add(Container);

	return Container;
}

void UMapItemSpawner::SpawnTestContainerInFront(const FVector& SeatLocation, float Yaw)
{
#if WITH_EDITOR
	if (GetWorld()->WorldType != EWorldType::PIE || GetWorld()->GetNetMode() == NM_Client || ContainerClasses.IsEmpty())
		return;

	const FVector Front = SeatLocation + FRotator(0.0f, Yaw, 0.0f).Vector() * 250.0f;
	FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MapTestContainerFloor), false);
	FloorQuery.AddIgnoredActors(SpawnedContainers);
	FHitResult Hit;
	const bool bFloor = TraceFloor(GetWorld(), Front + FVector(0.0f, 0.0f, 150.0f), Front - FVector(0.0f, 0.0f, 400.0f), FloorQuery, Hit);
	const FVector FloorPoint = bFloor ? FVector(Hit.ImpactPoint) : Front - FVector(0.0f, 0.0f, 120.0f);

	UClass* ContainerClass = ContainerClasses[TestContainerCount % ContainerClasses.Num()];
	++TestContainerCount;

	if (const AInteractActor* Container = SpawnContainer(ContainerClass, FloorPoint, FRotator(0.0f, Yaw + 180.0f, 0.0f)))
	{
		UE_LOG(LogTemp, Display, TEXT("Loot spawn: test container %s placed 2.5m in front of a start seat (PIE only) floor=%d"), *Container->GetName(), bFloor);
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
		const FBox Bounds = Owner->GetComponentsBoundingBox(true);
		const float Margin = MapBuild::DesignCellSize * 0.25f;
		const FIntPoint MinCell = CellOf(Bounds.Min.X + Margin, Bounds.Min.Y + Margin);
		const FIntPoint MaxCell = CellOf(Bounds.Max.X - Margin, Bounds.Max.Y - Margin);
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
			for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
				OutCells.Add(FIntPoint(X, Y));
	}

	const FString Owned = bLevelSlot
		? Owner->GetLevel()->GetOutermost()->GetName()
		: FString::Printf(TEXT("%s@%d,%d"), *Owner->GetClass()->GetName(),
			FMath::RoundToInt(Owner->GetActorLocation().X), FMath::RoundToInt(Owner->GetActorLocation().Y));

	for (const ULootSlotComponent* Slot : Slots)
	{
		FLootSlotRecord& Record = OutSlots.AddDefaulted_GetRef();
		Record.SlotGroup = Slot->SlotGroup;
		Record.GroupKey = Owned + TEXT("|") + Slot->SlotGroup.ToString();
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

void UMapItemSpawner::SpawnLootOnce()
{
    UWorld* World = GetWorld();

    if (!IsValid(World))
    {
        return;
    }
    const ENetMode NetMode = World->GetNetMode();

    UE_LOG(
        LogTemp,
        Warning,
        TEXT("[LootDebug] ENTER NetMode=%d MapAuthority=%d"),
        static_cast<int32>(NetMode),
        IsValid(Map) && Map->HasAuthority() ? 1 : 0
    );

    if (NetMode == NM_Client)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LootDebug] Client return"));
        return;
    }

    if (!IsValid(World))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[LootDebug] EXIT: World invalid"));
        return;
    }

    if (!IsValid(Map))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[LootDebug] EXIT: Map invalid"));
        return;
    }


    UE_LOG(LogTemp, Warning,
        TEXT("[LootDebug] NetMode=%d Authority=%d bSpawned=%d Safety=%d StreamingCount=%d"),
        static_cast<int32>(NetMode),
        Map->HasAuthority(),
        bSpawned,
        Map->bResolvedGameplayPointSafety,
        Map->FacilityDesignLevelInstances.Num());

    // =========================================================
    // 1. CLIENT: 서버에서 생성된 컨테이너 수신 확인
    // =========================================================
    if (NetMode == NM_Client)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Client] Starting receive check"));

        const UMapAssetSet& Assets = Map->GetMapAssets();

        TArray<UClass*> LootClasses;

        for (const TSoftClassPtr<AInteractActor>& Entry :
            Assets.LootContainerClasses)
        {
            if (UClass* Class = Entry.LoadSynchronous())
            {
                LootClasses.Add(Class);

                UE_LOG(LogTemp, Warning,
                    TEXT("[LootDebug][Client] Class loaded: %s"),
                    *GetNameSafe(Class));
            }
            else
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[LootDebug][Client] Class load FAILED: %s"),
                    *Entry.ToString());
            }
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Client] ExpectedClassCount=%d"),
            LootClasses.Num());

        FTimerHandle CountTimer;
        TWeakObjectPtr<UWorld> WeakWorld(World);

        World->GetTimerManager().SetTimer(
            CountTimer,
            FTimerDelegate::CreateLambda(
                [WeakWorld, LootClasses]()
                {
                    if (!WeakWorld.IsValid())
                    {
                        return;
                    }

                    int32 TotalInteractActors = 0;
                    int32 MatchingContainers = 0;
                    int32 FilledContainers = 0;
                    int32 ItemsInside = 0;

                    for (TActorIterator<AInteractActor> It(
                        WeakWorld.Get()); It; ++It)
                    {
                        AInteractActor* Actor = *It;

                        if (!IsValid(Actor))
                        {
                            continue;
                        }

                        ++TotalInteractActors;

                        const bool bClassMatches =
                            LootClasses.ContainsByPredicate(
                                [Actor](const UClass* Class)
                                {
                                    return IsValid(Class) &&
                                        Actor->IsA(Class);
                                });

                        UE_LOG(LogTemp, Warning,
                            TEXT("[LootDebug][Client] Actor=%s Class=%s Authority=%d Replicates=%d Matches=%d GUID=%s"),
                            *GetNameSafe(Actor),
                            *GetNameSafe(Actor->GetClass()),
                            Actor->HasAuthority(),
                            Actor->GetIsReplicated(),
                            bClassMatches,
                            *Actor->MyActorGuid.ToString());

                        if (!bClassMatches)
                        {
                            continue;
                        }

                        ++MatchingContainers;

                        const int32 Inside =
                            Actor->InventoryComp &&
                            Actor->MyActorGuid.IsValid()
                            ? Actor->InventoryComp
                            ->GetItems(Actor->MyActorGuid).Num()
                            : 0;

                        ItemsInside += Inside;

                        if (Inside > 0)
                        {
                            ++FilledContainers;
                        }
                    }

                    UE_LOG(LogTemp, Warning,
                        TEXT("[LootDebug][Client] RESULT TotalInteractActors=%d MatchingContainers=%d Filled=%d Items=%d"),
                        TotalInteractActors,
                        MatchingContainers,
                        FilledContainers,
                        ItemsInside);
                }),
            5.0f,
            false);

        return;
    }

    // =========================================================
    // 2. SERVER: 스폰 조건 검사
    // =========================================================
    if (NetMode == NM_Client)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[LootDebug][Server] Unexpected client execution"));
        return;
    }

    if (bSpawned)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Server] EXIT: Already spawned"));
        return;
    }

    if (!Map->bResolvedGameplayPointSafety)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Server] EXIT: GameplayPointSafety=false"));
        return;
    }

    for (const ULevelStreamingDynamic* Instance :
        Map->FacilityDesignLevelInstances)
    {
        if (!IsValid(Instance))
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[LootDebug][Server] Streaming instance invalid; skipped"));
            continue;
        }

        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Server] Level=%s Loaded=%d Visible=%d"),
            *GetNameSafe(Instance),
            Instance->IsLevelLoaded(),
            Instance->IsLevelVisible());

        if (!Instance->IsLevelLoaded() ||
            !Instance->IsLevelVisible())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[LootDebug][Server] EXIT: Streaming level not ready"));
            return;
        }
    }

    // =========================================================
    // 3. 컨테이너 BP 클래스 로드
    // =========================================================
    const UMapAssetSet& Assets = Map->GetMapAssets();

    ContainerClasses.Reset();

    TArray<FString> MissingClasses;

    for (const TSoftClassPtr<AInteractActor>& Entry :
        Assets.LootContainerClasses)
    {
        if (UClass* Class = Entry.LoadSynchronous())
        {
            ContainerClasses.Add(Class);

            UE_LOG(LogTemp, Warning,
                TEXT("[LootDebug][Server] Class loaded: %s"),
                *GetNameSafe(Class));
        }
        else
        {
            MissingClasses.Add(Entry.ToString());

            UE_LOG(LogTemp, Error,
                TEXT("[LootDebug][Server] Class load FAILED: %s"),
                *Entry.ToString());
        }
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[LootDebug][Server] ContainerClasses=%d MissingClasses=%d"),
        ContainerClasses.Num(),
        MissingClasses.Num());

    if (!MissingClasses.IsEmpty())
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Server] Missing BP list: %s"),
            *FString::Join(MissingClasses, TEXT(", ")));
    }

    if (ContainerClasses.IsEmpty())
    {
        UE_LOG(LogTemp, Error,
            TEXT("[LootDebug][Server] EXIT: No container classes loaded"));
        return;
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[LootDebug][Server] Spawn conditions passed"));

    // =========================================================
    // 4. 바닥 탐색 및 슬롯 수집
    // =========================================================
    FCollisionQueryParams FloorQuery(
        SCENE_QUERY_STAT(MapItemFloor), false);

    int32 LootPointCount = 0;
    int32 NoFloorCount = 0;

    TArray<FString> FloorSamples;

    int32 ContainerCount = 0;
    int32 SlotContainerCount = 0;
    int32 SpawnFailureCount = 0;

    TMap<FString, int32> CountByClass;

    uint32 LootHash = 0;

    auto FindFloor = [&](const FLevelDesignPoint& Point)
        {
            const FVector Designed =
                Point.WorldLocation - FVector(0.0f, 0.0f, 120.0f);

            FHitResult Hit;

            const bool bFloor = TraceFloor(
                World,
                Designed + FVector(0.0f, 0.0f, 60.0f),
                Designed - FVector(0.0f, 0.0f, 60.0f),
                FloorQuery,
                Hit);

            if (!bFloor)
            {
                ++NoFloorCount;

                if (FloorSamples.Num() < 6)
                {
                    FloorSamples.Add(Point.PointId.ToString());
                }

                UE_LOG(LogTemp, Warning,
                    TEXT("[LootDebug][Server] No floor for point=%s"),
                    *Point.PointId.ToString());
            }

            return bFloor ? FVector(Hit.ImpactPoint) : Designed;
        };

    TArray<FLootSlotRecord> AllSlots = RecordedSlots;
    TSet<FIntPoint> SlotCells = RecordedSlotCells;

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AppendSlotsOf(*It, AllSlots, SlotCells);
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[LootDebug][Server] RecordedSlots=%d AllSlots=%d SlotCells=%d"),
        RecordedSlots.Num(),
        AllSlots.Num(),
        SlotCells.Num());

    // =========================================================
    // 5. 슬롯 그룹 생성
    // =========================================================
    TMap<FString, TArray<const FLootSlotRecord*>> SlotGroups;

    for (const FLootSlotRecord& Slot : AllSlots)
    {
        SlotGroups.FindOrAdd(Slot.GroupKey).Add(&Slot);
    }

    int32 SlotGroupIndex = 0;

    const int64 RaidSeed = Map->GetRaidSeed();

    for (TPair<FString, TArray<const FLootSlotRecord*>>& Group :
        SlotGroups)
    {
        TArray<const FLootSlotRecord*>& Slots = Group.Value;

        if (Slots.IsEmpty())
        {
            continue;
        }

        Slots.Sort(
            [](const FLootSlotRecord& A,
                const FLootSlotRecord& B)
            {
                return A.Bottom.X != B.Bottom.X
                    ? A.Bottom.X < B.Bottom.X
                    : (A.Bottom.Y != B.Bottom.Y
                        ? A.Bottom.Y < B.Bottom.Y
                        : A.Bottom.Z < B.Bottom.Z);
            });

        const FVector First = Slots[0]->Bottom;

        uint32 GroupSeed = GetTypeHash(RaidSeed);

        GroupSeed = HashCombine(
            GroupSeed,
            GetTypeHash(FMath::RoundToInt(First.X)));

        GroupSeed = HashCombine(
            GroupSeed,
            GetTypeHash(FMath::RoundToInt(First.Y)));

        GroupSeed = HashCombine(
            GroupSeed,
            GetTypeHash(FMath::RoundToInt(First.Z)));

        GroupSeed = HashCombine(
            GroupSeed,
            GetTypeHash(Slots[0]->SlotGroup));

        FRandomStream Stream(static_cast<int32>(GroupSeed));

        const int32 MaxActive =
            Assets.MaxActiveSlotsPerGroup > 0
            ? FMath::Min(
                Assets.MaxActiveSlotsPerGroup,
                Slots.Num())
            : Slots.Num();

        const int32 MinActive =
            FMath::Min(
                Assets.MinActiveSlotsPerGroup,
                MaxActive);

        const int32 ActiveCount =
            Stream.RandRange(MinActive, MaxActive);

        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Server] SlotGroup[%d] Key=%s Slots=%d Active=%d"),
            SlotGroupIndex++,
            *Group.Key,
            Slots.Num(),
            ActiveCount);

        for (int32 Index = Slots.Num() - 1; Index > 0; --Index)
        {
            Slots.Swap(Index, Stream.RandRange(0, Index));
        }

        for (int32 Index = 0; Index < ActiveCount; ++Index)
        {
            const FLootSlotRecord& Slot = *Slots[Index];

            TArray<UClass*> Choices;

            for (const TSoftClassPtr<AInteractActor>& Entry :
                Slot.ContainerClasses)
            {
                if (UClass* Class = Entry.LoadSynchronous())
                {
                    Choices.Add(Class);
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[LootDebug][Server] Slot class load FAILED: %s"),
                        *Entry.ToString());
                }
            }

            if (Choices.IsEmpty())
            {
                Choices = ContainerClasses;
            }

            if (Choices.IsEmpty())
            {
                UE_LOG(LogTemp, Error,
                    TEXT("[LootDebug][Server] No class choices for slot group=%s"),
                    *Group.Key);
                continue;
            }

            UClass* ContainerClass =
                Choices[Stream.RandRange(0, Choices.Num() - 1)];

            FHitResult Hit;

            const bool bFloor = TraceFloor(
                World,
                Slot.Bottom + FVector(0.0f, 0.0f, 60.0f),
                Slot.Bottom - FVector(0.0f, 0.0f, 60.0f),
                FloorQuery,
                Hit);

            if (!bFloor)
            {
                ++NoFloorCount;
            }

            const FVector FloorPoint =
                bFloor ? FVector(Hit.ImpactPoint) : Slot.Bottom;

            UE_LOG(LogTemp, Warning,
                TEXT("[LootDebug][Server] Slot spawn attempt Class=%s Floor=%d Point=%s"),
                *GetNameSafe(ContainerClass),
                bFloor,
                *FloorPoint.ToString());

            AInteractActor* Container = SpawnContainer(
                ContainerClass,
                FloorPoint,
                FRotator(0.0f, Slot.Yaw, 0.0f));

            if (!IsValid(Container))
            {
                ++SpawnFailureCount;

                UE_LOG(LogTemp, Error,
                    TEXT("[LootDebug][Server] Slot spawn FAILED Class=%s"),
                    *GetNameSafe(ContainerClass));
                continue;
            }

            FloorQuery.AddIgnoredActor(Container);

            ++ContainerCount;
            ++SlotContainerCount;
            ++CountByClass.FindOrAdd(ContainerClass->GetName());

            const FVector Placed = Container->GetActorLocation();

            LootHash = HashCombine(
                LootHash,
                FCrc::StrCrc32(*ContainerClass->GetName()));

            LootHash = HashCombine(
                LootHash,
                GetTypeHash(FMath::RoundToInt(Placed.X)));

            LootHash = HashCombine(
                LootHash,
                GetTypeHash(FMath::RoundToInt(Placed.Y)));

            LootHash = HashCombine(
                LootHash,
                GetTypeHash(FMath::RoundToInt(Placed.Z)));
        }
    }

    // =========================================================
    // 6. 레거시 Loot 포인트
    // =========================================================
    int32 LegacySkipped = 0;

    for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
    {
        if (Point.Type != ELevelDesignPointType::Loot)
        {
            continue;
        }

        ++LootPointCount;

        if (!Assets.bUseLegacyLootPoints ||
            SlotCells.Contains(Point.GridCell))
        {
            ++LegacySkipped;
            continue;
        }

        FRandomStream ChoiceStream(
            static_cast<int32>(
                FCrc::MemCrc32(
                    &Point.PointSeed,
                    sizeof(Point.PointSeed))));

        UClass* ContainerClass =
            ContainerClasses[
                ChoiceStream.RandRange(
                    0,
                    ContainerClasses.Num() - 1)];

        FRandomStream Stream(
            static_cast<int32>(
                Point.PointSeed ^ (Point.PointSeed >> 32)));

        const FRotator Rotation(
            0.0f,
            Stream.FRandRange(0.0f, 360.0f),
            0.0f);

        const FVector FloorPoint = FindFloor(Point);

        UE_LOG(LogTemp, Warning,
            TEXT("[LootDebug][Server] Legacy spawn attempt Point=%s Class=%s Location=%s"),
            *Point.PointId.ToString(),
            *GetNameSafe(ContainerClass),
            *FloorPoint.ToString());

        AInteractActor* Container = SpawnContainer(
            ContainerClass,
            FloorPoint,
            Rotation);

        if (!IsValid(Container))
        {
            ++SpawnFailureCount;

            UE_LOG(LogTemp, Error,
                TEXT("[LootDebug][Server] Legacy spawn FAILED Point=%s"),
                *Point.PointId.ToString());
            continue;
        }

        FloorQuery.AddIgnoredActor(Container);

        ++ContainerCount;
        ++CountByClass.FindOrAdd(ContainerClass->GetName());

        const FVector Placed = Container->GetActorLocation();

        LootHash = HashCombine(
            LootHash,
            FCrc::StrCrc32(*Point.PointId.ToString()));

        LootHash = HashCombine(
            LootHash,
            FCrc::StrCrc32(*ContainerClass->GetName()));

        LootHash = HashCombine(
            LootHash,
            GetTypeHash(FMath::RoundToInt(Placed.X)));

        LootHash = HashCombine(
            LootHash,
            GetTypeHash(FMath::RoundToInt(Placed.Y)));

        LootHash = HashCombine(
            LootHash,
            GetTypeHash(FMath::RoundToInt(Placed.Z)));
    }

    // =========================================================
    // 7. 최종 결과 로그
    // =========================================================
    TArray<FString> ClassSummary;

    for (const TPair<FString, int32>& Pair : CountByClass)
    {
        ClassSummary.Add(
            FString::Printf(
                TEXT("%s=%d"),
                *Pair.Key,
                Pair.Value));
    }

    UE_LOG(LogTemp, Warning,
        TEXT("[LootDebug][Server] FINAL containers=%d slot_containers=%d "
            "spawn_failures=%d slots=%d groups=%d slot_cells=%d "
            "legacy_spawned=%d legacy_points=%d legacy_enabled=%d "
            "container_types=%d no_floor=%d loot_hash=%08X by_type=[%s] "
            "no_floor_sample=[%s]"),
        ContainerCount,
        SlotContainerCount,
        SpawnFailureCount,
        AllSlots.Num(),
        SlotGroups.Num(),
        SlotCells.Num(),
        LootPointCount - LegacySkipped,
        LootPointCount,
        Assets.bUseLegacyLootPoints,
        ContainerClasses.Num(),
        NoFloorCount,
        LootHash,
        *FString::Join(ClassSummary, TEXT(" ")),
        *FString::Join(FloorSamples, TEXT(" ")));

    // 스폰 시도를 마쳤으므로 중복 실행 방지
    bSpawned = true;

    UE_LOG(LogTemp, Warning,
        TEXT("[LootDebug][Server] SpawnLootOnce COMPLETE bSpawned=%d"),
        bSpawned);
}