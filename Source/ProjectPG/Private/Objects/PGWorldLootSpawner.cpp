#include "Objects/PGWorldLootSpawner.h"

#include "Actors/ItemContainerActor.h"
#include "Actors/MapTile.h"
#include "LevelDesign/PGMapInfo.h"
#include "CollisionQueryParams.h"
#include "Common/GameDefine.h"
#include "Engine/HitResult.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameModes/GameModePG.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGItemValue.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectTypes.h"
#include "Objects/PGRemoteOutpostActor.h"
#include "Objects/PGWearableColors.h"
#include "Weapons/PGWeaponComponent.h"

// 익명 namespace 가 아니라 이름을 붙인다 — 유니티 빌드에서 다른 cpp 의 같은 이름 상수와 부딪힌 사고가 있었다.
namespace PGWorldLootLocal
{
	// 논리 셀 한 칸(cm). AMapTile 간격을 실제로 재서 쓰고, 못 재면 이 값.
	constexpr float DefaultCellSize = 2000.0f;
	// 바깥 땅의 걷는 면 높이(WarZoneFootprintPreview 의 BaseGroundSurfaceZ 와 같은 값). 타일 자리의 기준 바닥.
	constexpr float GroundSurfaceZ = 20.0f;
	// 사람 캡슐(팀 캐릭터 기준 반지름 34, 반높이 92). 이 캡슐이 못 서는 자리에 놓인 아이템은 손이 안 닿는다.
	constexpr float CapsuleRadius = 34.0f;
	constexpr float CapsuleHalfHeight = 92.0f;
	// 아이템끼리 최소 간격. 같은 자리에 두 개가 겹치면 F 대상 고르기가 번거롭다.
	constexpr float MinSpacing = 100.0f;
	const FName WorldLootTag(TEXT("PGWorldLoot"));
	const FName FuelId(TEXT("Fuel"));

	// 논리 타일 간격 = X 좌표끼리의 가장 작은 차이(프리뷰의 GridStep 과 같은 방법). 못 재면 DefaultCellSize.
	float MeasureLogicalCellStep(const TArray<AMapTile*>& Tiles)
	{
		TArray<float> Xs;
		for (const AMapTile* Tile : Tiles)
			if (IsValid(Tile))
				Xs.AddUnique(Tile->GetActorLocation().X);
		Xs.Sort();
		float Step = 0.0f;
		for (int32 Index = 1; Index < Xs.Num(); ++Index)
		{
			const float Difference = Xs[Index] - Xs[Index - 1];
			if (Difference > KINDA_SMALL_NUMBER && (Step <= 0.0f || Difference < Step))
				Step = Difference;
		}
		return Step > 0.0f ? Step : DefaultCellSize;
	}

	// 무엇을 얼마나 놓나는 9/22 부터 등급 표(Docs/DT_PGItemValue.csv)의 LootWeight·LootMin·LootMax·LootCap 열이 정한다.
	// 전에는 여기 cpp 에 Wanted[] 목록이 따로 있어서, 등급 표와 두 군데를 같이 고쳐야 했고 한쪽만 고치면 어긋났다.
	// 구역별 등급 비중은 설정(UPGWorldLootSettings::EdgeGradeWeights 등).
	constexpr int32 GradeCount = 3;

	const TCHAR* GradeLetter(EPGItemGrade Grade)
	{
		switch (Grade)
		{
		case EPGItemGrade::Normal: return TEXT("N");
		case EPGItemGrade::Rare:   return TEXT("R");
		default:                   return TEXT("E");
		}
	}

	// 64비트 시드를 32비트 스트림 시드로. 프로젝트의 다른 시드 규칙(RollLoot)과 같은 접기.
	int32 FoldSeed(int64 Seed)
	{
		return static_cast<int32>(Seed ^ (Seed >> 32));
	}

	// 시설 레벨 이름 끝의 "_WxH" 에서 발자국 셀 수를 읽는다(LD_Facility_DowntownBlock_6x6 → 6,6). 못 읽으면 2x2.
	FIntPoint ParseFootprint(const FString& ShortName)
	{
		int32 Underscore = INDEX_NONE;
		if (ShortName.FindLastChar(TEXT('_'), Underscore))
		{
			const FString Tail = ShortName.Mid(Underscore + 1);
			int32 X = INDEX_NONE;
			if (Tail.FindChar(TEXT('x'), X) && X > 0)
			{
				const int32 W = FCString::Atoi(*Tail.Left(X));
				const int32 H = FCString::Atoi(*Tail.Mid(X + 1));
				if (W > 0 && H > 0)
					return FIntPoint(W, H);
			}
		}
		return FIntPoint(2, 2);
	}

	// 회전한 직사각형 안에 있나(XY 만).
	bool InsideRotatedRect(const FVector& Point, const FVector& Centre, const FVector2D& HalfExtent, float YawDegrees, float Margin)
	{
		const FVector Local = FRotator(0.0f, -YawDegrees, 0.0f).RotateVector(Point - Centre);
		return FMath::Abs(Local.X) <= HalfExtent.X + Margin && FMath::Abs(Local.Y) <= HalfExtent.Y + Margin;
	}

	static FAutoConsoleCommandWithWorld RespawnCommand(
		TEXT("PG.WorldLootRespawn"),
		TEXT("Removes every world-loot floor item and scatters them again from the map seed."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			if (UPGWorldLootSpawner* Spawner = UPGWorldLootSpawner::Get(World))
				Spawner->Restart();
		}));
}

UPGWorldLootSpawner* UPGWorldLootSpawner::Get(const UWorld* World)
{
	return IsValid(World) ? World->GetSubsystem<UPGWorldLootSpawner>() : nullptr;
}

bool UPGWorldLootSpawner::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPGWorldLootSpawner::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPGWorldLootSpawner, STATGROUP_Tickables);
}

void UPGWorldLootSpawner::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 등급 무기 등록은 서버·클라 둘 다 한다. 든 무기 이름(EquippedItemId)이 복제되면 클라도 그 이름으로 무기 정의를 찾기 때문이다.
	if (UWorld* World = GetWorld())
		ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UPGWorldLootSpawner::HandleActorSpawned));
}

void UPGWorldLootSpawner::Deinitialize()
{
	if (UWorld* World = GetWorld())
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	ActorSpawnedHandle.Reset();
	SpawnedItems.Reset();
	Super::Deinitialize();
}

void UPGWorldLootSpawner::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// 레벨에 미리 놓여 있던 폰은 "스폰됨" 알림이 안 온다. 게임 시작 때 한 번 훑어서 같이 등록한다.
	for (TActorIterator<AActor> It(&InWorld); It; ++It)
		HandleActorSpawned(*It);
}

void UPGWorldLootSpawner::HandleActorSpawned(AActor* Actor)
{
	if (!IsValid(Actor))
		return;
	// 무기 컴포넌트가 붙은 액터(검증 캐릭터 등)만. 나머지는 컴포넌트 찾기 한 번으로 끝난다.
	if (UPGWeaponComponent* Weapon = Actor->FindComponentByClass<UPGWeaponComponent>())
		RegisterGradeWeapons(Weapon);
}

void UPGWorldLootSpawner::RegisterGradeWeapons(UPGWeaponComponent* Weapon)
{
	if (!IsValid(Weapon))
		return;
	for (const FPGItemValueRow& Row : UPGItemValueLibrary::GetAllRows())
	{
		// 등급 변형(자기 ID ≠ 원래 ID)인 무기만. 원래 총은 무기 컴포넌트가 이미 안다.
		if (Row.Category != EPGItemCategory::Weapon || Row.BaseItemId.IsNone() || Row.BaseItemId == Row.ItemId || Weapon->IsWeaponItem(Row.ItemId))
			continue;
		const FPGWeaponDef* Base = Weapon->FindDef(Row.BaseItemId);
		if (!Base)
			continue;
		// 성능(사거리·피해·탄약·메시)은 원래 총 그대로 복사하고 이름만 바꾼다 — 등급은 이름·색·값만 다르게 하기로 했다(9/22 사용자).
		FPGWeaponDef Variant = *Base;
		Variant.ItemId = Row.ItemId;
		if (!Row.DisplayName.IsEmpty())
			Variant.DisplayName = FText::FromString(Row.DisplayName);
		Weapon->RegisterWeapon(Variant);
	}
}

void UPGWorldLootSpawner::Restart()
{
	ClearSpawned();
	Jobs.Reset();
	Stats.Reset();
	KnownFacilityJobs.Reset();
	ProcessedFacilityKeys.Reset();
	PlacedLocations.Reset();
	WarZoneCells.Reset();
	bWarZoneCellsGathered = false;
	JobCursor = ItemCursor = AttemptCursor = 0;
	TotalPlaced = FacilityPlaced = TilePlaced = GeometryOnlyCount = NavCheckedCount = FuelPlaced = 0;
	AmmoWithGuns = ContainersPlaced = 0;
	OutpostBoxesPlanned = OutpostBoxesPlaced = OutpostFloorPlanned = OutpostFloorPlaced = 0;
	FMemory::Memzero(OutpostGradeCounts);
	OutpostCorner.Reset();
	OutpostBoxNotes.Reset();
	FMemory::Memzero(GradeCounts);
	WarnedEmptyGrades = 0;
	bLoggedSummary = false;
	bHasNavReference = false;
	Phase = EPhase::WaitingForMap;
	NextMapSearchTime = 0.0;
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: restart requested"));
}

void UPGWorldLootSpawner::Tick(float DeltaTime)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || Phase == EPhase::Done || Phase == EPhase::Disabled)
		return;
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();
	const double Now = World->GetTimeSeconds();

	switch (Phase)
	{
	case EPhase::WaitingForMap:
	{
		// 서버만 놓는다. 클라이언트는 복제로 받는다. 스모크 테스트(-PGObjectSmokeTest)는 검사 개수가 흔들리면 안 되므로 끈다.
		if (World->GetNetMode() == NM_Client || !Settings || !Settings->bEnabled
			|| FParse::Param(FCommandLine::Get(), TEXT("PGObjectSmokeTest")))
		{
			Phase = EPhase::Disabled;
			return;
		}
		if (Now < NextMapSearchTime)
			return;
		NextMapSearchTime = Now + 0.5;
		// 절차 맵이 아닌 레벨(타이틀·로비·Entry)에는 AGameModePG 가 없다 → 조용히 끈다.
		AGameModeBase* AnyGameMode = World->GetAuthGameMode();
		if (!IsValid(AnyGameMode))
			return;
		const AGameModePG* GameMode = Cast<AGameModePG>(AnyGameMode);
		if (!GameMode)
		{
			Phase = EPhase::Disabled;
			return;
		}
		// 맵은 창구(UPGMapInfoSubsystem)로 찾는다 — 액터 순회도, 맵 클래스 이름도 필요 없다(9/26 의존 역전 정리).
		const IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(World);
		// "지점 확정" = 시설 레벨이 다 뜨고 벽 속 지점을 옮긴 뒤(또는 20초 타임아웃). 그 전에는 바닥이 없는 곳이 많다.
		if (!Map || !Map->AreLevelDesignPointsBuilt())
			return;
		MapSeed = GameMode->GetMapGenerationSeed();
		Phase = EPhase::WaitingForNav;
		PhaseStartTime = Now;
		return;
	}
	case EPhase::WaitingForNav:
	{
		// 길찾기 지도가 굽는 중이면 기다린다. 굽기 전에 물으면 전부 "못 닿음" 으로 나와 아이템이 안 놓인다.
		const double Waited = Now - PhaseStartTime;
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		const bool bBuilding = NavSys && NavSys->IsNavigationBuildInProgress();
		if (Waited < Settings->StartDelaySeconds || (bBuilding && Waited < Settings->NavWaitTimeoutSeconds))
			return;
		Plan();
		Phase = EPhase::Spawning;
		SpawnStartTime = Now;
		return;
	}
	case EPhase::Spawning:
	{
		ProcessJobs();
		if (JobCursor < Jobs.Num())
			return;
		// 계획한 것을 다 놓았다. 늦게 뜬 시설이 있으면 그것도 채우고, 창이 닫히면 끝.
		if (PlanNewFacilities(Jobs))
			return;
		if (Now - SpawnStartTime < Settings->LateFacilityWindowSeconds && TotalPlaced < Settings->MaxItemsPerMap)
			return;
		Phase = EPhase::Done;
		LogSummary();
		return;
	}
	default:
		return;
	}
}

// ---- 계획 ----

void UPGWorldLootSpawner::Plan()
{
	using namespace PGWorldLootLocal;
	UWorld* World = GetWorld();
	UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(World);
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();
	JobStream.Initialize(FoldSeed(MapSeed ^ 0x100775EEDLL));

	// ---- 후보 만들기: 가치표에 등급이 있고, 바닥에 놓을 메시가 있는 것만.
	Pool.Reset();
	TMap<FName, FPGObjectCatalogRow> CatalogByItem;
	if (Spawner)
	{
		TArray<FPGObjectCatalogRow> Rows;
		Spawner->GetAllCatalogRows(Rows);
		// TMap 순서는 믿을 수 없다. 같은 ItemId 가 두 행(붕대 OBJ-035/036)이면 번호가 앞선 것을 쓴다.
		Rows.Sort([](const FPGObjectCatalogRow& A, const FPGObjectCatalogRow& B) { return A.ObjectId.LexicalLess(B.ObjectId); });
		for (const FPGObjectCatalogRow& Row : Rows)
		{
			if (Row.Archetype == EPGObjectArchetype::FloorItem && Row.bIncluded && !Row.Mesh.IsNull() && !Row.ItemId.IsNone()
				&& !CatalogByItem.Contains(Row.ItemId))
				CatalogByItem.Add(Row.ItemId, Row);
		}
	}
	int32 PoolByGrade[GradeCount] = { 0, 0, 0 };
	for (const FPGItemValueRow& ValueRow : UPGItemValueLibrary::GetAllRows())
	{
		// 표에서 LootWeight 가 0 인 것은 바닥에 안 떨어진다(금고·보스 전용, 거래 금지, 9/17 무기 범위 밖, 메시 없음).
		if (ValueRow.LootWeight <= 0.0f || !ValueRow.bTradable)
			continue;
		const FName ItemId = ValueRow.ItemId;
		FPoolItem Item;
		Item.ItemId = ItemId;
		Item.Grade = ValueRow.Grade;
		Item.Weight = ValueRow.LootWeight;
		Item.MinCount = FMath::Max(1, ValueRow.LootMin);
		Item.MaxCount = FMath::Max(Item.MinCount, ValueRow.LootMax);
		Item.RemainingCap = ValueRow.LootCap == -2 ? Settings->MaxFuel : ValueRow.LootCap;
		Item.AmmoItemId = ValueRow.AmmoItemId;
		const FPGObjectCatalogRow* AliasRow = (!ValueRow.LootMeshObjectId.IsNone() && Spawner) ? Spawner->FindCatalogRow(ValueRow.LootMeshObjectId) : nullptr;
		// 무기는 카탈로그 행을 안 쓴다: 카탈로그 이름("총기")이 등급 이름("소총 AR70 (레어)")을 덮고, 레어·에픽은 카탈로그 행이 아예 없다.
		// 표의 FloorMesh + SpawnDrop 으로 놓으면 노말·레어·에픽이 같은 길로 나와 이름·색 규칙이 하나로 맞는다.
		const FPGObjectCatalogRow* CatalogRow = ValueRow.Category == EPGItemCategory::Weapon ? nullptr : CatalogByItem.Find(ItemId);
		if (CatalogRow)
			Item.CatalogObjectId = CatalogRow->ObjectId;
		else if (AliasRow && !AliasRow->Mesh.IsNull())
		{
			Item.AliasMeshObjectId = AliasRow->ObjectId;
			Item.AliasDisplayName = ValueRow.DisplayName.IsEmpty() ? FText::FromName(ItemId) : FText::FromString(ValueRow.DisplayName);
		}
		else if (UPGWearableColorLibrary::FindItemFloorMesh(ItemId).IsNull())
		{
			// 카탈로그 행도, 빌릴 메시도, 표의 FloorMesh 도 없다. 보이지 않는 아이템을 놓으면 "F 눌러도 아무것도 없는" 자리가 생기므로 뺀다.
			// Tools/check_item_tables.py 가 이 경우를 미리 잡는다(표에서 LootWeight 를 0 으로 두거나 메시를 채울 것).
			UE_LOG(LogPGObjects, Warning, TEXT("PGLoot: %s (grade %s) has no floor mesh - not placed"),
				*ItemId.ToString(), GradeLetter(ValueRow.Grade));
			continue;
		}
		++PoolByGrade[FMath::Clamp(static_cast<int32>(Item.Grade), 0, GradeCount - 1)];
		Pool.Add(Item);
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: seed=%lld pool=%d items (N/R/E = %d/%d/%d) table=%s"),
		MapSeed, Pool.Num(), PoolByGrade[0], PoolByGrade[1], PoolByGrade[2], *UPGItemValueLibrary::GetTableSource());

	// ---- 길찾기 출발점: 워존 가운데(프리뷰 액터 자리)를 지도에 투영. 지도는 그 둘레 120m 에만 있다.
	bHasNavReference = false;
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		FNavLocation Projected;
		const IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(World);
		const FVector Origin = Map ? Map->GetMapCentre() : FVector::ZeroVector;
		if (NavSys->ProjectPointToNavigation(Origin, Projected, FVector(1500.0f, 1500.0f, 500.0f)))
		{
			bHasNavReference = true;
			NavReference = Projected.Location;
		}
	}

	// ---- 시설 먼저(예산을 먼저 잡게), 그다음 타일.
	Jobs.Reset();
	Stats.Reset();
	JobCursor = ItemCursor = AttemptCursor = 0;
	PlanRemoteOutpost(Jobs);
	PlanNewFacilities(Jobs);
	PlanTiles(KnownFacilityJobs, Jobs);
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: planned jobs=%d facilities=%d nav_reference=%s"),
		Jobs.Num(), KnownFacilityJobs.Num(), bHasNavReference ? *NavReference.ToCompactString() : TEXT("none"));
}

uint8 UPGWorldLootSpawner::ZoneTierForCell(const FIntPoint& Cell) const
{
	if (WarZoneCells.Contains(Cell))
		return 3;
	const int32 Radius = GetDefault<UPGWorldLootSettings>()->MidZoneRadiusCells;
	for (const FIntPoint& WarZone : WarZoneCells)
	{
		if (FMath::Max(FMath::Abs(WarZone.X - Cell.X), FMath::Abs(WarZone.Y - Cell.Y)) <= Radius)
			return 2;
	}
	return 1;
}

bool UPGWorldLootSpawner::IsInsideAnyFacility(const FVector& Point, const TArray<FJob>& FacilityJobs, float Margin) const
{
	for (const FJob& Facility : FacilityJobs)
		if (PGWorldLootLocal::InsideRotatedRect(Point, Facility.Centre, Facility.HalfExtent, Facility.YawDegrees, Margin))
			return true;
	return false;
}

bool UPGWorldLootSpawner::PlanNewFacilities(TArray<FJob>& OutJobs)
{
	using namespace PGWorldLootLocal;
	UWorld* World = GetWorld();
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();

	// 워존 셀은 타일 계획보다 먼저 필요하다(시설 등급 계산). 한 번만 모은다.
	if (!bWarZoneCellsGathered)
	{
		bWarZoneCellsGathered = true;
		// 논리 타일 좌표는 맵 좌표가 아니다(PlanTiles 주석) — 간격을 재서 칸 번호로 바꾼다. 예전에는 2000 으로 나눠 워존이 늘 1칸이었다.
		TArray<AMapTile*> AllTiles;
		for (TActorIterator<AMapTile> It(World); It; ++It)
			AllTiles.Add(*It);
		const float Step = MeasureLogicalCellStep(AllTiles);
		for (const AMapTile* Tile : AllTiles)
		{
			if (!IsValid(Tile) || Tile->GetType() != ETileType::WarZone)
				continue;
			const FVector Location = Tile->GetActorLocation();
			WarZoneCells.Add(FIntPoint(FMath::RoundToInt(Location.X / Step), FMath::RoundToInt(Location.Y / Step)));
		}
	}

	// 시설 레벨은 프리뷰가 ULevelStreamingDynamic 으로 띄운다(LD_Facility_*). 프리뷰의 목록은 private 이라
	// 월드의 스트리밍 레벨을 직접 훑는다 — 프리뷰 코드는 다른 작업자가 고치는 중이라 손대지 않는다.
	// "보이는" 레벨만: 로드만 되고 안 보이는 레벨은 액터가 아직 월드에 없어 바닥 검사가 실패한다.
	int32 FacilityPlannedSoFar = 0;
	for (const FJob& Known : KnownFacilityJobs)
		FacilityPlannedSoFar += Known.Count;
	bool bAdded = false;
	for (ULevelStreaming* Level : World->GetStreamingLevels())
	{
		ULevelStreamingDynamic* Dynamic = Cast<ULevelStreamingDynamic>(Level);
		if (!IsValid(Dynamic) || !Dynamic->IsLevelLoaded() || !Dynamic->IsLevelVisible())
			continue;
		// 원래 레벨 이름(LD_Facility_*)은 PackageNameToLoad 에 있다. GetWorldAssetPackageName 은 띄울 때 붙인 새 이름
		// (".../UEDPIE_0_Facility_07_Type_7")이라 "LD_Facility_" 가 없어서, 예전에는 시설을 하나도 못 찾았다(9/22 로그 facilities 0).
		const FString Package = Dynamic->PackageNameToLoad.IsNone() ? Dynamic->GetWorldAssetPackageName() : Dynamic->PackageNameToLoad.ToString();
		int32 Marker = Package.Find(TEXT("LD_Facility_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		if (Marker == INDEX_NONE)
			continue;
		const FString Key = Dynamic->GetPathName();
		if (ProcessedFacilityKeys.Contains(Key))
			continue;
		ProcessedFacilityKeys.Add(Key);

		const FString ShortName = Package.Mid(Marker + 12); // "LD_Facility_" 뒤
		const FIntPoint Footprint = ParseFootprint(ShortName);
		const FVector Centre = Dynamic->LevelTransform.GetLocation();
		const float Yaw = Dynamic->LevelTransform.Rotator().Yaw;
		const FIntPoint CentreCell(FMath::RoundToInt(Centre.X / DefaultCellSize), FMath::RoundToInt(Centre.Y / DefaultCellSize));

		FJob Job;
		Job.Label = ShortName;
		Job.bFacility = true;
		Job.Centre = Centre;
		// 발자국에서 1m 안쪽. 경계에 놓으면 옆 타일 바닥과 시설 바닥의 이음새(높이 1cm 차) 위에 걸린다.
		Job.HalfExtent = FVector2D(Footprint.X * DefaultCellSize * 0.5f - 100.0f, Footprint.Y * DefaultCellSize * 0.5f - 100.0f);
		Job.YawDegrees = Yaw;
		Job.BaseZ = Centre.Z; // 시설은 땅+1 에 띄우므로 이게 시설 바닥
		Job.ZoneTier = ZoneTierForCell(CentreCell);
		Job.Count = FMath::Clamp(FMath::RoundToInt(Footprint.X * Footprint.Y * Settings->FacilityItemsPerCell),
			Settings->FacilityMinItems, Settings->FacilityMaxItems);
		// 시설 몫 상한. 넘치면 뒤에 뜬 시설이 덜 받는다(순서는 스트리밍 목록 순 = 프리뷰가 띄운 순 = 시드 순).
		Job.Count = FMath::Min(Job.Count, FMath::Max(0, Settings->FacilityBudget - FacilityPlannedSoFar));
		FacilityPlannedSoFar += Job.Count;
		// 같은 시설이 두 번 놓여도(같은 이름) 자리가 다르므로 좌표를 시드에 섞는다.
		Job.Seed = static_cast<int64>(HashCombine(HashCombine(GetTypeHash(MapSeed), FCrc::StrCrc32(*ShortName)),
			HashCombine(GetTypeHash(FMath::RoundToInt(Centre.X)), GetTypeHash(FMath::RoundToInt(Centre.Y)))));

		FStats Stat;
		Stat.Label = ShortName;
		Stat.Wanted = Job.Count;
		Stat.ZoneTier = Job.ZoneTier;
		Job.StatsIndex = Stats.Add(Stat);
		KnownFacilityJobs.Add(Job);
		if (Job.Count > 0)
		{
			OutJobs.Add(Job);
			bAdded = true;
		}
		UE_LOG(LogPGObjects, Display, TEXT("PGLoot: facility %s footprint=%dx%d at=(%.0f,%.0f,%.0f) yaw=%.0f tier=%d planned=%d"),
			*ShortName, Footprint.X, Footprint.Y, Centre.X, Centre.Y, Centre.Z, Yaw, Job.ZoneTier, Job.Count);
	}
	return bAdded;
}

// 외진 보상 거점(APGRemoteOutpostActor)의 상자 자리와 바닥 아이템.
// 왜 여기서: 상자·등급 시스템을 따로 만들지 않으려고(사용자 요구). 거점 액터는 "어디에" 만 알려 주고, 무엇을 놓을지는
//   이 스포너의 기존 상자 경로(카탈로그 행 → 상자 원형 → 루팅 표)와 등급 굴리기(PickItem)가 정한다. 등급만 4단(거점용 비중)을 쓴다.
// 왜 맨 먼저: 자리 간격 검사(MinSpacing)는 먼저 놓인 것이 이긴다. 타일 몫이 거점 자리를 먼저 차지하면 상자가 빠진다.
void UPGWorldLootSpawner::PlanRemoteOutpost(TArray<FJob>& OutJobs)
{
	UWorld* World = GetWorld();
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();
	APGRemoteOutpostActor* Outpost = nullptr;
	for (TActorIterator<APGRemoteOutpostActor> It(World); It; ++It)
	{
		Outpost = *It;
		break;
	}
	if (!IsValid(Outpost))
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGLoot: no remote outpost on this map"));
		return;
	}
	OutpostCorner = Outpost->GetCornerLabel();
	const TArray<FPGRemoteOutpostLootSlot>& Slots = Outpost->GetLootSlots();
	for (int32 Index = 0; Index < Slots.Num(); ++Index)
	{
		FJob Box;
		Box.Label = TEXT("outpost_box");
		Box.bContainer = true;
		Box.Centre = Slots[Index].Location;
		// 자리는 거점 배치가 정했다. 40cm 안에서만 흔든다 — 부품 사이 빈틈을 벗어나면 헬기·텐트에 막힌다.
		Box.HalfExtent = FVector2D(40.0f, 40.0f);
		Box.YawDegrees = Outpost->GetFloorLootYaw();
		Box.BaseZ = Slots[Index].Location.Z;
		Box.ZoneTier = 4;
		Box.Count = 1;
		Box.ForcedObjectId = Settings->RemoteOutpostBoxes.IsValidIndex(Index) && !Settings->RemoteOutpostBoxes[Index].IsNone()
			? Settings->RemoteOutpostBoxes[Index] : Slots[Index].PreferredObjectId;
		// 상자 속은 여는 순간 이 시드로 굴린다. 판 시드 + 자리 번호라 같은 판이면 같은 내용물.
		Box.Seed = (static_cast<int64>(HashCombine(GetTypeHash(MapSeed), GetTypeHash(Index))) << 8) | static_cast<int64>(Index + 1);
		Box.StatsIndex = INDEX_NONE;
		OutJobs.Add(Box);
		++OutpostBoxesPlanned;
	}
	if (Settings->RemoteOutpostFloorItems > 0)
	{
		FJob Floor;
		Floor.Label = TEXT("outpost_floor");
		Floor.Centre = Outpost->GetFloorLootCentre();
		Floor.HalfExtent = Outpost->GetFloorLootHalfExtent();
		Floor.YawDegrees = Outpost->GetFloorLootYaw();
		Floor.BaseZ = Floor.Centre.Z;
		Floor.ZoneTier = 4;
		Floor.Count = Settings->RemoteOutpostFloorItems;
		Floor.Seed = (static_cast<int64>(HashCombine(GetTypeHash(MapSeed), GetTypeHash(OutpostCorner))) << 4) | 0x9;
		FStats Stat;
		Stat.Label = TEXT("RemoteOutpost");
		Stat.Wanted = Floor.Count;
		Stat.ZoneTier = 4;
		Floor.StatsIndex = Stats.Add(Stat);
		OutJobs.Add(Floor);
		OutpostFloorPlanned = Floor.Count;
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: remote outpost corner=%s planned boxes=%d floor=%d grade weights N/R/E=%.0f/%.0f/%.0f"),
		*OutpostCorner, OutpostBoxesPlanned, OutpostFloorPlanned,
		Settings->RemoteOutpostGradeWeights.Normal, Settings->RemoteOutpostGradeWeights.Rare, Settings->RemoteOutpostGradeWeights.Epic);
}

void UPGWorldLootSpawner::PlanTiles(const TArray<FJob>& FacilityJobs, TArray<FJob>& OutJobs)
{
	using namespace PGWorldLootLocal;
	UWorld* World = GetWorld();
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();

	// 논리 셀(AMapTile)을 읽는다. 시각 타일이 아니라 논리 타입(워존·길·장애물·빈 땅)만 알면 된다.
	TArray<AMapTile*> Tiles;
	for (TActorIterator<AMapTile> It(World); It; ++It)
		Tiles.Add(*It);
	if (Tiles.IsEmpty())
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGLoot: no AMapTile in world - tile pass skipped"));
		return;
	}
	// 논리 타일(AMapTile)은 실제 맵 자리에 있지 않다 — 팀원 격자는 10cm 간격의 작은 판이고, 프리뷰가 "칸 번호 × 20m" 로 옮겨 그린다
	// (WarZoneFootprintPreview 의 GridStep). 그래서 같은 방법으로 간격을 재서 칸 번호를 구하고, 자리는 칸 번호 × DefaultCellSize 로 쓴다.
	// 예전에는 액터 위치를 그대로 써서 900칸이 전부 맵 한가운데 3m 안에 몰렸다 — 110개 계획 중 18개만 놓이고(391곳 막힘),
	// 시설을 찾게 고치자 900칸이 전부 가운데 워존 시설 "안" 으로 읽혀 0개가 됐다(9/22).
	const float CellSize = MeasureLogicalCellStep(Tiles);

	struct FCell { FIntPoint Cell; FVector Location; ETileType Type; };
	TArray<FCell> Cells;
	Cells.Reserve(Tiles.Num());
	for (const AMapTile* Tile : Tiles)
	{
		const FVector Logical = Tile->GetActorLocation();
		const FIntPoint Cell(FMath::RoundToInt(Logical.X / CellSize), FMath::RoundToInt(Logical.Y / CellSize));
		Cells.Add({ Cell, FVector(Cell.X * DefaultCellSize, Cell.Y * DefaultCellSize, GroundSurfaceZ), Tile->GetType() });
	}
	// 액터 순회 순서는 믿지 않는다 → 셀 좌표로 정렬한 뒤 시드로 섞는다.
	// 섞는 이유: 상한에 걸려 뒤쪽을 자를 때 늘 같은 구석이 비지 않게.
	Cells.Sort([](const FCell& A, const FCell& B) { return A.Cell.X != B.Cell.X ? A.Cell.X < B.Cell.X : A.Cell.Y < B.Cell.Y; });
	for (int32 Index = Cells.Num() - 1; Index > 0; --Index)
		Cells.Swap(Index, JobStream.RandRange(0, Index));

	FStats TileStat;
	TileStat.Label = TEXT("tiles");
	const int32 TileStatsIndex = Stats.Add(TileStat);
	const int32 TileBudget = FMath::Max(0, Settings->MaxItemsPerMap - Settings->FacilityBudget);
	int32 Planned = 0;
	int32 Skipped = 0;
	for (const FCell& Cell : Cells)
	{
		// 플레이어 시작·탈출 칸은 비워 둔다(시작 연료통·탈출 세트가 있다). 시설 발자국 안은 시설 몫.
		if (Cell.Type == ETileType::Spawn || Cell.Type == ETileType::Exit
			|| IsInsideAnyFacility(Cell.Location, FacilityJobs, DefaultCellSize * 0.5f))
		{
			++Skipped;
			continue;
		}
		float Chance = 0.0f;
		int32 MaxItems = Settings->CellMaxItems;
		switch (Cell.Type)
		{
		case ETileType::WarZone:  Chance = Settings->WarZoneCellChance; MaxItems = Settings->WarZoneCellMaxItems; break;
		case ETileType::Road:     Chance = Settings->RoadCellChance; break;
		case ETileType::Obstacle: Chance = Settings->NatureCellChance; break; // 숲·바위 — 자리가 자주 막히지만 몇 개는 들어간다
		default:                  Chance = Settings->OpenCellChance; break;   // 들판·폐허
		}
		// 셀마다 자기 시드. 판 시드 + 셀 좌표라 같은 판이면 같은 셀에 같은 개수.
		FRandomStream CellStream(FoldSeed(static_cast<int64>(HashCombine(GetTypeHash(MapSeed),
			HashCombine(GetTypeHash(Cell.Cell.X), GetTypeHash(Cell.Cell.Y)))) ^ 0x715E5LL));
		if (CellStream.FRand() > Chance)
			continue;
		// 칸마다 최소 CellMinItems 개. 9/22 사용자: "칸마다 1~3개 이상씩 모두 둬 버려" — 600m 맵 900칸에 80칸만 받아 거의 안 보였다.
		int32 Count = CellStream.RandRange(FMath::Max(1, Settings->CellMinItems), FMath::Max(Settings->CellMinItems, MaxItems));
		if (Planned + Count > TileBudget)
			Count = TileBudget - Planned;
		if (Count <= 0)
			break;
		Planned += Count;

		FJob Job;
		Job.Label = TEXT("tile");
		Job.bFacility = false;
		Job.Centre = FVector(Cell.Location.X, Cell.Location.Y, GroundSurfaceZ);
		// 셀 가장자리 1.5m 는 뺀다. 타일 경계에는 담·도랑·길 턱이 온다.
		Job.HalfExtent = FVector2D(DefaultCellSize * 0.5f - 150.0f, DefaultCellSize * 0.5f - 150.0f);
		Job.BaseZ = GroundSurfaceZ;
		Job.ZoneTier = ZoneTierForCell(Cell.Cell);
		Job.Count = Count;
		Job.Seed = static_cast<int64>(CellStream.GetUnsignedInt()) | (static_cast<int64>(CellStream.GetUnsignedInt()) << 32);
		Job.StatsIndex = TileStatsIndex;
		OutJobs.Add(Job);
		++Stats[TileStatsIndex].Cells;

		// 같은 칸에 상자 하나(확률). 같은 칸 시드에서 이어 굴려서 같은 판이면 같은 칸에 같은 상자.
		if (CellStream.FRand() < Settings->ContainerCellChance)
		{
			FJob Box = Job;
			Box.Label = TEXT("container");
			Box.bContainer = true;
			Box.Count = 1;
			Box.Seed = Job.Seed ^ 0xB0C5LL;
			Box.StatsIndex = INDEX_NONE; // 바닥 아이템 집계에 섞지 않는다(ContainersPlaced 로 따로 센다)
			OutJobs.Add(Box);
		}
	}
	Stats[TileStatsIndex].Wanted = Planned;
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: tiles cells=%d (skipped %d spawn/exit/facility) cell_size=%.0f warzone_cells=%d planned=%d budget=%d"),
		Cells.Num(), Skipped, CellSize, WarZoneCells.Num(), Planned, TileBudget);
}

// ---- 놓기 ----

void UPGWorldLootSpawner::ProcessJobs()
{
	using namespace PGWorldLootLocal;
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();
	int32 Attempts = 0;
	while (Attempts < Settings->AttemptsPerFrame && JobCursor < Jobs.Num())
	{
		FJob& Job = Jobs[JobCursor];
		FStats* Stat = Stats.IsValidIndex(Job.StatsIndex) ? &Stats[Job.StatsIndex] : nullptr;
		if (TotalPlaced >= Settings->MaxItemsPerMap)
		{
			UE_LOG(LogPGObjects, Display, TEXT("PGLoot: cap %d reached, %d jobs left unfilled"), Settings->MaxItemsPerMap, Jobs.Num() - JobCursor);
			JobCursor = Jobs.Num();
			break;
		}
		if (ItemCursor >= Job.Count)
		{
			if (Job.bFacility && Stat)
			{
				UE_LOG(LogPGObjects, Display, TEXT("PGLoot: facility %s %d items (wanted %d, rejected %d: no floor %d / blocked %d / unreachable %d) tier=%d nav_checked=%d"),
					*Stat->Label, Stat->Placed, Stat->Wanted, Stat->NoFloor + Stat->Blocked + Stat->Unreachable,
					Stat->NoFloor, Stat->Blocked, Stat->Unreachable, Stat->ZoneTier, Stat->NavChecked);
			}
			++JobCursor;
			ItemCursor = 0;
			AttemptCursor = 0;
			continue;
		}
		++Attempts;
		// 시도마다 새 스트림: 자리 시드 + 몇 번째 아이템 + 몇 번째 시도. 프레임을 어떻게 나눠도 같은 값이 나온다.
		FRandomStream Stream(FoldSeed(Job.Seed ^ static_cast<int64>(HashCombine(GetTypeHash(ItemCursor), GetTypeHash(AttemptCursor)))));
		FVector Location;
		bool bNavChecked = false;
		const ESpotResult Result = TryFindSpot(Job, Stream, Location, bNavChecked);
		if (Result != ESpotResult::Ok)
		{
			if (Stat)
			{
				if (Result == ESpotResult::NoFloor) ++Stat->NoFloor;
				else if (Result == ESpotResult::Blocked) ++Stat->Blocked;
				else ++Stat->Unreachable;
			}
			if (++AttemptCursor >= Settings->AttemptsPerItem)
			{
				++ItemCursor;
				AttemptCursor = 0;
			}
			continue;
		}
		if (Job.bContainer)
		{
			// 구역이 좋을수록 좋은 상자. 가장자리 = 나무·식량·공구, 중간 = + 군용 보급·탄약·의료, 워존 = + 무기·잠긴 상자.
			// 금고는 넣지 않는다(시설 안 금고 자리 몫이다). 행이 꺼져 있거나 없으면 다른 것을 고른다.
			static const TCHAR* const EdgeBoxes[] = { TEXT("OBJ-001"), TEXT("OBJ-006"), TEXT("OBJ-007") };
			static const TCHAR* const MidBoxes[] = { TEXT("OBJ-001"), TEXT("OBJ-002"), TEXT("OBJ-003"), TEXT("OBJ-005"), TEXT("OBJ-006"), TEXT("OBJ-007") };
			static const TCHAR* const WarBoxes[] = { TEXT("OBJ-002"), TEXT("OBJ-003"), TEXT("OBJ-004"), TEXT("OBJ-005"), TEXT("OBJ-009") };
			TArray<const TCHAR*> Choices;
			if (Job.ZoneTier >= 3) Choices.Append(WarBoxes, UE_ARRAY_COUNT(WarBoxes));
			else if (Job.ZoneTier == 2) Choices.Append(MidBoxes, UE_ARRAY_COUNT(MidBoxes));
			else Choices.Append(EdgeBoxes, UE_ARRAY_COUNT(EdgeBoxes));
			UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(GetWorld());
			AActor* Box = nullptr;
			FName BoxId;
			// 외진 보상 거점처럼 자리마다 상자가 정해진 묶음은 그 행을 먼저 쓴다. 행이 꺼져 있거나 없으면 아래 무작위 목록(워존 상자)으로.
			if (!Job.ForcedObjectId.IsNone() && Spawner)
			{
				const FPGObjectCatalogRow* Row = Spawner->FindCatalogRow(Job.ForcedObjectId);
				if (Row && Row->bIncluded)
				{
					BoxId = Job.ForcedObjectId;
					Box = Spawner->SpawnFromCatalog(BoxId, FTransform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), Location), Job.Seed);
				}
			}
			for (int32 Try = 0; Try < Choices.Num() && Spawner && !Box; ++Try)
			{
				const FName Id(Choices[(Stream.RandRange(0, Choices.Num() - 1) + Try) % Choices.Num()]);
				const FPGObjectCatalogRow* Row = Spawner->FindCatalogRow(Id);
				if (Row && Row->bIncluded)
				{
					BoxId = Id;
					Box = Spawner->SpawnFromCatalog(Id, FTransform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), Location), Job.Seed);
				}
			}
			if (IsValid(Box))
			{
				Box->Tags.AddUnique(WorldLootTag);
				SpawnedItems.Add(Box);
				PlacedLocations.Add(Location);
				++ContainersPlaced;
				if (Job.ZoneTier >= 4 && Spawner)
				{
					// 로그용으로 상자 속을 미리 굴려 본다. 상자는 여는 순간 같은 시드(Job.Seed)로 같은 표를 굴리므로 결과가 같다.
					++OutpostBoxesPlaced;
					const FPGObjectCatalogRow* Row = Spawner->FindCatalogRow(BoxId);
					const FName TableId = Row ? Row->LootTableId : NAME_None;
					TArray<FString> Contents;
					for (const FPGItemStack& Stack : Spawner->RollLoot(TableId, Job.Seed))
						Contents.Add(FString::Printf(TEXT("%s x%d %s"), *Stack.ItemId.ToString(), Stack.Count, GradeLetter(UPGItemValueLibrary::GetItemGrade(Stack.ItemId))));
					OutpostBoxNotes.Add(FString::Printf(TEXT("%s %s [%s]"), *BoxId.ToString(), *TableId.ToString(), *FString::Join(Contents, TEXT(", "))));
				}
			}
			else if (Job.ZoneTier >= 4)
			{
				UE_LOG(LogPGObjects, Warning, TEXT("PGLoot: remote outpost box %s could not be spawned at %s"), *Job.ForcedObjectId.ToString(), *Location.ToCompactString());
			}
			++ItemCursor;
			AttemptCursor = 0;
			continue;
		}
		const FPoolItem* Item = PickItem(Job.ZoneTier, Stream);
		if (!Item)
		{
			++ItemCursor;
			AttemptCursor = 0;
			continue;
		}
		const int64 ItemSeed = Job.Seed + ItemCursor * 977 + 1;
		AActor* Actor = SpawnItem(*Item, Location, Stream, ItemSeed);
		if (IsValid(Actor))
		{
			SpawnedItems.Add(Actor);
			PlacedLocations.Add(Location);
			++TotalPlaced;
			if (Job.bFacility) ++FacilityPlaced; else ++TilePlaced;
			++GradeCounts[FMath::Clamp(static_cast<int32>(Item->Grade), 0, GradeCount - 1)];
			if (Job.ZoneTier >= 4)
			{
				++OutpostFloorPlaced;
				++OutpostGradeCounts[FMath::Clamp(static_cast<int32>(Item->Grade), 0, GradeCount - 1)];
			}
			if (bNavChecked) ++NavCheckedCount; else ++GeometryOnlyCount;
			if (Item->ItemId == FuelId) ++FuelPlaced;
			if (Stat)
			{
				++Stat->Placed;
				if (bNavChecked) ++Stat->NavChecked;
			}
			// 상한이 있는 아이템(연료통)은 하나 놓을 때마다 줄인다. Pool 은 멤버라 const 를 벗긴다.
			for (FPoolItem& Mutable : Pool)
				if (Mutable.ItemId == Item->ItemId && Mutable.RemainingCap > 0)
					--Mutable.RemainingCap;

			// 총 옆에는 그 총의 탄을 같이 둔다. 총만 주우면 쏠 수가 없다(사용자 9/22 "총 옆에 총알 같이 스폰되게").
			// 어떤 탄인지는 등급 표의 AmmoItemId 열 — 레어·에픽 총도 원래 총과 같은 탄을 적어 두었다.
			// 자리는 총에서 60cm 옆 바닥(같은 방법으로 바닥을 찾는다). 못 찾으면 총 자리 바로 옆에 놓는다.
			// 개수 상한(MaxItemsPerMap)에는 세지 않는다 — 총 한 자루의 부속이다.
			const FName AmmoId = Item->AmmoItemId;
			if (!AmmoId.IsNone())
			{
				if (const FPoolItem* Ammo = Pool.FindByPredicate([&AmmoId](const FPoolItem& P) { return P.ItemId == AmmoId; }))
				{
					const float Angle = Stream.FRandRange(0.0f, 2.0f * PI);
					FVector AmmoAt = Location + FVector(FMath::Cos(Angle) * 60.0f, FMath::Sin(Angle) * 60.0f, 0.0f);
					FHitResult AmmoHit;
					if (GetWorld()->LineTraceSingleByChannel(AmmoHit, AmmoAt + FVector(0, 0, 80.0f), AmmoAt - FVector(0, 0, 120.0f), ECC_Visibility,
						FCollisionQueryParams(SCENE_QUERY_STAT(PGWorldLootAmmo), false, Actor)))
						AmmoAt = AmmoHit.ImpactPoint;
					else
						AmmoAt = Location;
					if (AActor* AmmoActor = SpawnItem(*Ammo, AmmoAt, Stream, ItemSeed + 7))
					{
						SpawnedItems.Add(AmmoActor);
						++AmmoWithGuns;
					}
				}
			}
		}
		++ItemCursor;
		AttemptCursor = 0;
	}
}

// 자리 하나를 검사한다. 순서대로 싼 검사부터:
//  1. 직사각형 안에서 XY 를 뽑고, 위(기준 바닥 + 9m/시설 15m)에서 아래(기준 바닥 - 3m)로 선을 쏴 처음 닿는 면 = 바닥.
//     안 닿으면 바닥 없음. 기울어진 면(법선 Z < 0.75)·걸을 수 없는 면(폰을 막지 않는 것)·이미 아이템/상자/사람이 있는 자리는 뺀다.
//  2. 바닥에서 위로 2m 선: 닿으면 머리 위가 막힌 것(낮은 틈·선반 밑) → 사람이 못 서니 뺀다.
//  3. 바닥 위 1m 에 사람 캡슐(반지름 34, 반높이 92)을 놓고 겹침 검사: 벽 속·기둥 옆·닫힌 상자 속을 걸러 낸다.
//  4. 길찾기 지도(NavMesh): 그 자리에 지도 타일이 있으면 지도에 투영 → 실패면 못 닿는 곳(지붕·막힌 방).
//     투영이 되면 워존 가운데(NavReference)에서 길이 이어지는지도 묻는다 — 투영만으로는 "떨어진 지붕 위" 를 못 거른다.
//     지도가 없는 곳(워존 120m 밖)은 기하로만 판단한다: 기준 바닥보다 1.5m 높은 면(폐허 지붕·2층)은 검증 못 하니 뺀다,
//     머리 위 9m 안에 지붕이 있으면(실내) 무릎 높이 16방향 12m 선 중 5m 이상 뚫린 방향이 하나는 있어야 한다(문·열린 벽).
//     16방향으로 1m 문을 못 찾아 버리는 자리는 있어도, 막힌 방에 놓는 일은 없다 — 놓치는 쪽이 낫다.
UPGWorldLootSpawner::ESpotResult UPGWorldLootSpawner::TryFindSpot(const FJob& Job, FRandomStream& Stream, FVector& OutLocation, bool& bOutNavChecked) const
{
	using namespace PGWorldLootLocal;
	UWorld* World = GetWorld();
	bOutNavChecked = false;

	// 1. 자리와 바닥
	const FVector Local(Stream.FRandRange(-Job.HalfExtent.X, Job.HalfExtent.X), Stream.FRandRange(-Job.HalfExtent.Y, Job.HalfExtent.Y), 0.0f);
	const FVector XY = Job.Centre + FRotator(0.0f, Job.YawDegrees, 0.0f).RotateVector(Local);
	const FVector Top(XY.X, XY.Y, Job.BaseZ + (Job.bFacility ? 1500.0f : 900.0f));
	const FVector Bottom(XY.X, XY.Y, Job.BaseZ - 300.0f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGWorldLootSpot), false);
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Top, Bottom, ECC_Visibility, Params))
		return ESpotResult::NoFloor;
	const AActor* HitActor = Hit.GetActor();
	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (IsValid(HitActor) && (HitActor->IsA<APGFloorItemActor>() || HitActor->IsA<AItemContainerActor>() || HitActor->IsA<APawn>()))
	{
		++BlockedBy[0];
		return ESpotResult::Blocked;
	}
	if (Hit.ImpactNormal.Z < 0.75f)
		return ESpotResult::NoFloor;
	if (!IsValid(HitComponent) || HitComponent->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
		return ESpotResult::NoFloor;
	const FVector Floor = Hit.ImpactPoint;

	// 2. 머리 위 2m
	FHitResult Overhead;
	auto NoteBlocker = [this](const UPrimitiveComponent* Component)
	{
		const FString Name = IsValid(Component) ? FString::Printf(TEXT("%s.%s(%s)"), *GetNameSafe(Component->GetOwner()), *Component->GetName(),
			Component->IsA<UStaticMeshComponent>() ? *GetNameSafe(Cast<UStaticMeshComponent>(Component)->GetStaticMesh()) : TEXT("-")) : TEXT("?");
		++BlockerNames.FindOrAdd(Name);
	};
	if (World->LineTraceSingleByChannel(Overhead, Floor + FVector(0.0f, 0.0f, 10.0f), Floor + FVector(0.0f, 0.0f, 210.0f), ECC_Visibility, Params))
	{
		++BlockedBy[1];
		NoteBlocker(Overhead.GetComponent());
		return ESpotResult::Blocked;
	}

	// 3. 사람 캡슐
	{
		FHitResult CapsuleHit;
		const FVector CapsuleAt = Floor + FVector(0.0f, 0.0f, CapsuleHalfHeight + 8.0f);
		if (World->SweepSingleByChannel(CapsuleHit, CapsuleAt, CapsuleAt + FVector(0.0f, 0.0f, 1.0f), FQuat::Identity, ECC_Pawn,
			FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight), Params) && CapsuleHit.bStartPenetrating)
		{
			++BlockedBy[2];
			NoteBlocker(CapsuleHit.GetComponent());
			return ESpotResult::Blocked;
		}
	}

	// 아이템끼리 간격(바닥 아이템은 폰 채널을 무시해서 캡슐 검사에 안 잡힌다).
	for (const FVector& Placed : PlacedLocations)
		if (FVector::DistSquared2D(Placed, Floor) < FMath::Square(MinSpacing))
		{
			++BlockedBy[3];
			return ESpotResult::Blocked;
		}

	// 4. 길찾기
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const ARecastNavMesh* NavData = NavSys ? Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance()) : nullptr;
	bool bHasNavTile = false;
	if (NavData)
	{
		int32 TileX = 0, TileY = 0;
		if (NavData->GetNavMeshTileXY(Floor, TileX, TileY))
		{
			TArray<FNavTileRef> TileRefs;
			NavData->GetNavMeshTilesAt(TileX, TileY, TileRefs);
			bHasNavTile = TileRefs.Num() > 0;
		}
	}
	if (bHasNavTile)
	{
		bOutNavChecked = true;
		FNavLocation Projected;
		if (!NavSys->ProjectPointToNavigation(Floor, Projected, FVector(80.0f, 80.0f, 150.0f), NavData))
			return ESpotResult::Unreachable;
		if (bHasNavReference)
		{
			FPathFindingQuery Query(nullptr, *NavData, NavReference, Projected.Location);
			if (!NavSys->TestPathSync(Query))
				return ESpotResult::Unreachable;
		}
	}
	else
	{
		if (Floor.Z > Job.BaseZ + 150.0f)
			return ESpotResult::Unreachable;
		FHitResult Roof;
		if (World->LineTraceSingleByChannel(Roof, Floor + FVector(0.0f, 0.0f, 100.0f), Floor + FVector(0.0f, 0.0f, 900.0f), ECC_Visibility, Params))
		{
			bool bHasExit = false;
			const FVector Knee = Floor + FVector(0.0f, 0.0f, 90.0f);
			for (int32 Direction = 0; Direction < 16 && !bHasExit; ++Direction)
			{
				const FVector Dir = FRotator(0.0f, Direction * 22.5f, 0.0f).Vector();
				FHitResult Wall;
				const float Free = World->LineTraceSingleByChannel(Wall, Knee, Knee + Dir * 1200.0f, ECC_Visibility, Params) ? Wall.Distance : 1200.0f;
				bHasExit = Free >= 500.0f;
			}
			if (!bHasExit)
				return ESpotResult::Unreachable;
		}
	}
	OutLocation = Floor;
	return ESpotResult::Ok;
}

const UPGWorldLootSpawner::FPoolItem* UPGWorldLootSpawner::PickItem(uint8 ZoneTier, FRandomStream& Stream)
{
	using namespace PGWorldLootLocal;
	if (Pool.IsEmpty())
		return nullptr;
	// 등급 먼저 굴린다(구역별 비중), 그다음 그 등급 안에서 아이템 비중으로.
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();
	// 4 = 외진 보상 거점(워존보다 좋게), 3 = 워존, 2 = 중간, 1 = 가장자리.
	const FPGGradeWeights& Zone = ZoneTier >= 4 ? Settings->RemoteOutpostGradeWeights
		: (ZoneTier == 3 ? Settings->WarZoneGradeWeights : (ZoneTier == 2 ? Settings->MidGradeWeights : Settings->EdgeGradeWeights));
	const float Weights[GradeCount] = { FMath::Max(0.0f, Zone.Normal), FMath::Max(0.0f, Zone.Rare), FMath::Max(0.0f, Zone.Epic) };
	float Total = 0.0f;
	for (int32 Grade = 0; Grade < GradeCount; ++Grade)
		Total += Weights[Grade];
	float Roll = Stream.FRandRange(0.0f, Total);
	int32 Picked = 0;
	for (int32 Grade = 0; Grade < GradeCount; ++Grade)
	{
		Roll -= Weights[Grade];
		if (Roll <= 0.0f && Weights[Grade] > 0.0f)
		{
			Picked = Grade;
			break;
		}
	}
	// 그 등급에 놓을 것이 없으면(메시가 없어서 다 빠졌거나 연료통 상한) 한 단계씩 내려가고, 바닥까지 없으면 올라간다.
	// 내려가는 쪽이 먼저인 이유: 없는 등급 대신 더 좋은 것을 주면 가장자리가 워존보다 후해질 수 있다.
	auto CollectGrade = [this](int32 Grade, TArray<const FPoolItem*>& Out)
	{
		Out.Reset();
		for (const FPoolItem& Item : Pool)
			if (static_cast<int32>(Item.Grade) == Grade && Item.RemainingCap != 0)
				Out.Add(&Item);
	};
	TArray<const FPoolItem*> Candidates;
	CollectGrade(Picked, Candidates);
	if (Candidates.IsEmpty() && !(WarnedEmptyGrades & (1 << Picked)))
	{
		WarnedEmptyGrades |= (1 << Picked);
		UE_LOG(LogPGObjects, Warning, TEXT("PGLoot: no placeable item of grade %s - falling back to a neighbouring grade"),
			*StaticEnum<EPGItemGrade>()->GetNameStringByValue(Picked));
	}
	for (int32 Grade = Picked - 1; Grade >= 0 && Candidates.IsEmpty(); --Grade)
		CollectGrade(Grade, Candidates);
	for (int32 Grade = Picked + 1; Grade < GradeCount && Candidates.IsEmpty(); ++Grade)
		CollectGrade(Grade, Candidates);
	if (Candidates.IsEmpty())
		return nullptr;

	float WeightTotal = 0.0f;
	for (const FPoolItem* Item : Candidates)
		WeightTotal += FMath::Max(0.0f, Item->Weight);
	float Pick = Stream.FRandRange(0.0f, WeightTotal);
	for (const FPoolItem* Item : Candidates)
	{
		Pick -= FMath::Max(0.0f, Item->Weight);
		if (Pick <= 0.0f)
			return Item;
	}
	return Candidates.Last();
}

AActor* UPGWorldLootSpawner::SpawnItem(const FPoolItem& Item, const FVector& Location, FRandomStream& Stream, int64 Seed)
{
	using namespace PGWorldLootLocal;
	UWorld* World = GetWorld();
	UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(World);
	const FTransform Transform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), Location);
	const int32 Count = Stream.RandRange(FMath::Min(Item.MinCount, Item.MaxCount), FMath::Max(Item.MinCount, Item.MaxCount));

	AActor* Actor = nullptr;
	if (!Item.CatalogObjectId.IsNone() && Spawner)
	{
		// 카탈로그 행으로: 메시·이름·색 변형(옷)·연료통 색이 스포너 규칙대로 따라온다. 개수만 우리 것으로 바꾼다.
		Actor = Spawner->SpawnFromCatalog(Item.CatalogObjectId, Transform, Seed);
		APGFloorItemActor* FloorItem = Cast<APGFloorItemActor>(Actor);
		const FPGObjectCatalogRow* Row = Spawner->FindCatalogRow(Item.CatalogObjectId);
		// GetItemId() 를 다시 넣는 이유: 스포너가 색 변형(Pants_Black)으로 바꿔 둔 ItemId 를 원래색으로 되돌리지 않으려고.
		if (IsValid(FloorItem) && Row && Row->ItemCount != Count)
			FloorItem->SetItem(FloorItem->GetItemId(), Count);
	}
	else
	{
		// 카탈로그 행을 안 쓰는 것(모든 무기·셔츠·파우치·권총집·권총탄·샷건탄): SpawnDrop. 옷은 색을 시드로 고른다.
		// 메시·이름·등급 색 테두리는 APGFloorItemActor::SetItem 이 등급 표를 보고 입힌다.
		const FName ColoredId = UPGWearableColorLibrary::PickColorVariant(Item.ItemId, Seed);
		APGFloorItemActor* FloorItem = APGFloorItemActor::SpawnDrop(World, ColoredId, Count, Transform);
		if (IsValid(FloorItem) && !Item.AliasMeshObjectId.IsNone() && Spawner)
		{
			// 남의 행에서 메시만 빌린다. ItemId·개수·이름은 자기 것(권총탄 x15 로 줍히고 인벤토리에도 권총탄으로 들어간다).
			if (const FPGObjectCatalogRow* AliasRow = Spawner->FindCatalogRow(Item.AliasMeshObjectId))
			{
				FPGObjectCatalogRow Borrowed = *AliasRow;
				Borrowed.ObjectId = NAME_None;
				Borrowed.ItemId = ColoredId;
				Borrowed.ItemCount = Count;
				Borrowed.DisplayName = Item.AliasDisplayName;
				Borrowed.QuestTag = NAME_None;
				FloorItem->ApplyCatalogRow(Borrowed);
			}
		}
		Actor = FloorItem;
	}
	if (IsValid(Actor))
		Actor->Tags.AddUnique(WorldLootTag);
	return Actor;
}

void UPGWorldLootSpawner::ClearSpawned()
{
	int32 Removed = 0;
	for (const TWeakObjectPtr<AActor>& Weak : SpawnedItems)
	{
		if (AActor* Actor = Weak.Get())
		{
			Actor->Destroy();
			++Removed;
		}
	}
	SpawnedItems.Reset();
	if (Removed > 0)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGLoot: removed %d items"), Removed);
	}
}

void UPGWorldLootSpawner::LogSummary()
{
	if (bLoggedSummary)
		return;
	bLoggedSummary = true;
	const UPGWorldLootSettings* Settings = GetDefault<UPGWorldLootSettings>();
	int32 TileNoFloor = 0, TileBlocked = 0, TileUnreachable = 0, TileCells = 0, TileWanted = 0;
	int32 FacilityCount = 0;
	for (const FStats& Stat : Stats)
	{
		if (Stat.Label == TEXT("tiles"))
		{
			TileNoFloor = Stat.NoFloor; TileBlocked = Stat.Blocked; TileUnreachable = Stat.Unreachable; TileCells = Stat.Cells; TileWanted = Stat.Wanted;
		}
		else if (Stat.Label != TEXT("RemoteOutpost")) // 거점은 시설 레벨이 아니다 — 아래 따로 한 줄
			++FacilityCount;
	}
	// 시드와 함께 한 줄. PIE 에서 이 줄만 찾으면 된다.
	UE_LOG(LogPGObjects, Display,
		TEXT("PGLoot: seed=%lld tiles %d items (cells %d, wanted %d, rejected %d: no floor %d / blocked %d / unreachable %d), facilities %d items in %d levels, total %d/%d, grade mix N/R/E=%d/%d/%d, fuel %d/%d, nav_checked %d geometry_only %d, %.1fs"),
		MapSeed, TilePlaced, TileCells, TileWanted, TileNoFloor + TileBlocked + TileUnreachable, TileNoFloor, TileBlocked, TileUnreachable,
		FacilityPlaced, FacilityCount, TotalPlaced, Settings->MaxItemsPerMap,
		GradeCounts[0], GradeCounts[1], GradeCounts[2],
		FuelPlaced, Settings->MaxFuel, NavCheckedCount, GeometryOnlyCount,
		IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() - SpawnStartTime : 0.0);
	// 막힘의 내역. 가장 많이 막은 것 5개.
	TArray<TPair<FString, int32>> Top;
	for (const TPair<FString, int32>& Pair : BlockerNames)
		Top.Add(Pair);
	Top.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B) { return A.Value > B.Value; });
	FString TopText;
	for (int32 Index = 0; Index < FMath::Min(5, Top.Num()); ++Index)
		TopText += FString::Printf(TEXT("%s%s x%d"), Index > 0 ? TEXT(", ") : TEXT(""), *Top[Index].Key, Top[Index].Value);
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: blocked by - on item/box/pawn %d, overhead %d, capsule %d, spacing %d; top blockers: %s"),
		BlockedBy[0], BlockedBy[1], BlockedBy[2], BlockedBy[3], TopText.IsEmpty() ? TEXT("none") : *TopText);
	UE_LOG(LogPGObjects, Display, TEXT("PGLoot: containers %d placed in cells (chance %.2f), ammo beside guns %d"),
		ContainersPlaced, Settings->ContainerCellChance, AmmoWithGuns);
	// 외진 보상 거점 한 줄: 상자 몇 개(무엇, 속에 뭐가 들었나), 바닥 아이템 등급 몇 대 몇.
	if (OutpostBoxesPlanned > 0 || OutpostFloorPlanned > 0)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGLoot: remote outpost corner=%s containers=%d/%d floor=%d/%d floor_grades N/R/E=%d/%d/%d boxes: %s"),
			*OutpostCorner, OutpostBoxesPlaced, OutpostBoxesPlanned, OutpostFloorPlaced, OutpostFloorPlanned,
			OutpostGradeCounts[0], OutpostGradeCounts[1], OutpostGradeCounts[2],
			OutpostBoxNotes.Num() > 0 ? *FString::Join(OutpostBoxNotes, TEXT(" | ")) : TEXT("none"));
	}
}
