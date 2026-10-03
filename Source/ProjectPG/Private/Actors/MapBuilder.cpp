// Visual layer for the procedural map: turns the logical AMapTile grid into tiles, facilities and the border lake.

#include "Actors/MapBuilder.h"
#include "Actors/TacticalTileActor.h"
#include "Actors/TacticalTileRoadStraight.h"
#include "Actors/ProceduralFacilityActor.h"

#include "Algo/AnyOf.h"
#include "Algo/Count.h"
#include "Algo/MinElement.h"
#include "Actors/MapTile.h"
#include "GameFramework/Pawn.h"
#include "GameModes/GameModePG.h"
#include "AIController.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/TargetPoint.h"
#include "Engine/StaticMesh.h"
#include "LandscapeProxy.h"
#include "GameFramework/PlayerController.h"
#include "NavigationInvokerComponent.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "Elements/PCGCreatePoints.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TacticalTileNavModifierComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "HAL/PlatformMemory.h"
#include "Materials/MaterialInterface.h"
#include "Actors/MapBuilder/MapVerifier.h"
// 같이 쓰는 숫자·경로·작은 계산은 MapBuildShared.h 한 곳에 있다(일꾼들도 같은 것을 본다).
#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/MapBuilder/MapFacilityPlanner.h"
#include "Actors/MapBuilder/MapTilePlanner.h"
#include "Actors/MapBuilder/MapRoadPlanner.h"
#include "Actors/MapBuilder/MapTileSpawner.h"
#include "Actors/MapBuilder/MapGroundBuilder.h"
#include "Actors/MapBuilder/MapSpawnRegionPlanner.h"
#include "Actors/MapBuilder/MapPointPlanner.h"
using namespace MapBuild;


AMapBuilder::AMapBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// The hidden navigation slab is static. Keep its attachment hierarchy static as
	// well so PIE does not reject the attachment before Recast can consume it.
	SceneRoot->SetMobility(EComponentMobility::Static);

	GroundHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("GroundHISM"));
	WarZoneGroundHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("WarZoneGroundHISM"));
	TransitionGroundHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("TransitionGroundHISM"));
	RoadSurfaceHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("RoadSurfaceHISM"));
	LakeBedHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LakeBedHISM"));
	LakeWaterHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("LakeWaterHISM"));
	MountainHISM = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(TEXT("MountainHISM"));
	NavigationFloor = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NavigationFloor"));
	NavigationInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavigationInvoker"));
	CenterNavigationBlockers = CreateDefaultSubobject<UTacticalTileNavModifierComponent>(TEXT("CenterNavigationBlockers"));
	PlayerNavigationBlockers = CreateDefaultSubobject<UTacticalTileNavModifierComponent>(TEXT("PlayerNavigationBlockers"));
	DressingPCGComponent = CreateDefaultSubobject<UPCGComponent>(TEXT("DressingPCGComponent"));
	NavigationInvoker->SetGenerationRadii(12000.0f, 15000.0f);
	DressingPCGComponent->Seed = 1337;
	DressingPCGComponent->GenerationTrigger = EPCGComponentGenerationTrigger::GenerateOnDemand;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));

	UHierarchicalInstancedStaticMeshComponent* WorldComponents[] = {
		GroundHISM, WarZoneGroundHISM, TransitionGroundHISM, RoadSurfaceHISM,
		LakeBedHISM, LakeWaterHISM
	};
	for (UHierarchicalInstancedStaticMeshComponent* Component : WorldComponents)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(CubeMesh.Object);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetMobility(EComponentMobility::Movable);
	}

	// The basin's mountain ring. Collision-free on purpose: the map edge must
	// still end in a fall, and Recast must never path onto scenery kilometres out.
	// 산 메시는 판 시작 때 ApplyMapAssets 가 DA_MapAssets 에서 끼운다.
	MountainHISM->SetupAttachment(SceneRoot);
	MountainHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MountainHISM->SetCanEverAffectNavigation(false);
	MountainHISM->SetGenerateOverlapEvents(false);
	MountainHISM->SetMobility(EComponentMobility::Movable);
	GroundHISM->SetCanEverAffectNavigation(false);
	WarZoneGroundHISM->SetCanEverAffectNavigation(false);
	TransitionGroundHISM->SetCanEverAffectNavigation(false);
	RoadSurfaceHISM->SetCanEverAffectNavigation(false);
	LakeBedHISM->SetCanEverAffectNavigation(false);
	// The surface is a visual sheet only. Giving it collision would let a player
	// stand on the lake, and giving it navigation would let AI path across it.
	LakeWaterHISM->SetCanEverAffectNavigation(false);
	LakeWaterHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Sculpted ground features: one HISM per authored shape *per ground band*. The mesh
	// is shared and only the component's material is overridden, so a feature dropped
	// into the WarZone reads as the same dirt as the flat cells around it instead of a
	// green patch in an industrial yard. Index is Band * ShapeCount + Shape.
	{
		// 띠별 머티리얼(들판/경계/워존)은 ApplyMapAssets 가 DA_MapAssets 에서 끼운다.
		const TCHAR* BandNames[] = { TEXT("Nature"), TEXT("Transition"), TEXT("WarZone") };
		const TArray<FTerrainFeatureMesh>& FeatureMeshes = GetTerrainFeatureMeshes();
		for (int32 BandIndex = 0; BandIndex < UE_ARRAY_COUNT(BandNames); ++BandIndex)
		{
			for (int32 ShapeIndex = 0; ShapeIndex < FeatureMeshes.Num(); ++ShapeIndex)
			{
				const FTerrainFeatureMesh& Feature = FeatureMeshes[ShapeIndex];
				const FName ComponentName(*FString::Printf(
					TEXT("Terrain%s%sHISM"), BandNames[BandIndex], Feature.ShapeName));
				UHierarchicalInstancedStaticMeshComponent* Component =
					CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(ComponentName);
				Component->SetupAttachment(SceneRoot);
				ConstructorHelpers::FObjectFinder<UStaticMesh> FeatureMesh(Feature.AssetPath);
				if (FeatureMesh.Succeeded())
					Component->SetStaticMesh(FeatureMesh.Object);
				Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
				Component->SetGenerateOverlapEvents(false);
				Component->SetMobility(EComponentMobility::Movable);
				Component->SetCanEverAffectNavigation(false);
				TerrainFeatureHISMs.Add(Component);
			}
		}
	}

	// Rocks and trees block; bushes stay walk-through so a slope never becomes a wall.
	// 메시(돌·소나무·덤불·호숫가 돌·갈대)는 ApplyMapAssets 가 DA_MapAssets 에서 끼운다.
	struct FTerrainDressingSpec
	{
		const TCHAR* ComponentName;
		bool bCollides;
	};
	const FTerrainDressingSpec DressingSpecs[] = {
		{ TEXT("TerrainRockHISM"), true },
		{ TEXT("TerrainTreeHISM"), true },
		{ TEXT("TerrainBushHISM"), false },
		// Shore dressing. The waterline is decided per 20 m cell, so on its own it
		// steps along the grid. Rocks and reeds standing in the shallows break that
		// line up - the same trick the diorama uses along its own bank.
		{ TEXT("ShoreRockHISM"), true },
		{ TEXT("ShoreReedHISM"), false }
	};
	TArray<UHierarchicalInstancedStaticMeshComponent*> DressingComponents;
	for (const FTerrainDressingSpec& Spec : DressingSpecs)
	{
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Spec.ComponentName);
		Component->SetupAttachment(SceneRoot);
		Component->SetCollisionEnabled(Spec.bCollides ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		Component->SetCollisionProfileName(Spec.bCollides
			? UCollisionProfile::BlockAll_ProfileName : UCollisionProfile::NoCollision_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCanEverAffectNavigation(false);
		Component->SetCullDistances(14000, 32000);
		DressingComponents.Add(Component);
	}
	TerrainRockHISM = DressingComponents[0];
	TerrainTreeHISM = DressingComponents[1];
	TerrainBushHISM = DressingComponents[2];
	ShoreRockHISM = DressingComponents[3];
	ShoreReedHISM = DressingComponents[4];

	// Shoreline transition meshes, one HISM per variant. Generated by
	// PG.BuildShoreMeshes; their land-facing edges sit flat on the shared Z=20 datum
	// so they drop in beside ordinary cells, and their interiors curve down under the
	// waterline. This is what actually removes the 20 m staircase - rotated boxes and
	// scattered rocks only ever hid it.
	const TCHAR* ShoreMeshPaths[] = {
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner1.SM_Shore_Corner1"),
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Adjacent.SM_Shore_Corner2Adjacent"),
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Diagonal.SM_Shore_Corner2Diagonal"),
		TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner3.SM_Shore_Corner3")
	};
	static const TCHAR* ShoreComponentNames[] = {
		TEXT("ShoreCorner1HISM"), TEXT("ShoreCorner2AdjacentHISM"),
		TEXT("ShoreCorner2DiagonalHISM"), TEXT("ShoreCorner3HISM")
	};
	for (int32 ShoreIndex = 0; ShoreIndex < UE_ARRAY_COUNT(ShoreMeshPaths); ++ShoreIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
				ShoreComponentNames[ShoreIndex]);
		Component->SetupAttachment(SceneRoot);
		// Deliberately not ConstructorHelpers: these meshes are generated by
		// PG.BuildShoreMeshes, so on a fresh checkout - or right after regenerating
		// them - they do not exist when the CDO is constructed at editor start. A
		// constructor-time finder would fail silently and leave the shore invisible
		// until the next restart. BuildLightweightWorldVisuals loads them instead.
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetCanEverAffectNavigation(false);
		ShoreTransitionHISMs.Add(Component);
	}

	NavigationFloor->SetupAttachment(SceneRoot);
	NavigationFloor->SetStaticMesh(CubeMesh.Object);
	NavigationFloor->SetRelativeLocation(FVector(0.0f, 0.0f, -20.0f));
	NavigationFloor->SetRelativeScale3D(FVector(900.0f, 900.0f, 0.2f));
	NavigationFloor->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	NavigationFloor->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	NavigationFloor->SetGenerateOverlapEvents(false);
	NavigationFloor->SetVisibility(false);
	NavigationFloor->SetHiddenInGame(true);
	NavigationFloor->SetCanEverAffectNavigation(true);
	NavigationFloor->SetMobility(EComponentMobility::Static);

	// 땅·도로·호수 머티리얼도 ApplyMapAssets 가 DA_MapAssets 에서 끼운다.
	// 기본 위치: /Game/PG/LevelDesign/DA_MapAssets. 없으면 MapAssetSet.h 의 기본값을 쓴다.
	MapAssets = TSoftObjectPtr<UMapAssetSet>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/DA_MapAssets.DA_MapAssets")));
}

// 보이는 것 목록 읽기. 처음 부를 때 DA_MapAssets 를 불러 두고, 없으면 C++ 기본값(예전 경로)을 쓴다.
// 게임에서: 팀원이 DA_MapAssets 에서 숲 타일을 바꾸면 다음 판부터 그 숲이 깔린다(코드·빌드 필요 없음).
const UMapAssetSet& AMapBuilder::GetMapAssets()
{
	if (!LoadedMapAssets)
	{
		LoadedMapAssets = MapAssets.LoadSynchronous();
		if (!LoadedMapAssets)
			LoadedMapAssets = GetMutableDefault<UMapAssetSet>();
		UE_LOG(LogTemp, Display, TEXT("Map assets: source=%s"),
			LoadedMapAssets->HasAnyFlags(RF_ClassDefaultObject) ? TEXT("C++ defaults") : *LoadedMapAssets->GetPathName());
	}
	return *LoadedMapAssets;
}

// 그릇에 메시·머티리얼 끼우기. 예전엔 생성자에서 경로 글자로 끼웠다.
// 그릇들은 Movable 이라 판 중에도 바꿀 수 있다. 칸이 비어 있으면(못 불러오면) 그 그릇은 건드리지 않는다.
void AMapBuilder::ApplyMapAssets()
{
	const UMapAssetSet& Assets = GetMapAssets();
	auto SetMesh = [](UStaticMeshComponent* Component, const TSoftObjectPtr<UStaticMesh>& Mesh)
	{
		if (UStaticMesh* Loaded = Mesh.LoadSynchronous(); IsValid(Component) && Loaded)
			Component->SetStaticMesh(Loaded);
	};
	auto SetMaterial = [](UStaticMeshComponent* Component, const TSoftObjectPtr<UMaterialInterface>& Material)
	{
		if (UMaterialInterface* Loaded = Material.LoadSynchronous(); IsValid(Component) && Loaded)
			Component->SetMaterial(0, Loaded);
	};
	// 바깥 산, 언덕 위 돌·소나무·덤불, 호숫가 돌·갈대
	SetMesh(MountainHISM, Assets.MountainMesh);
	SetMesh(TerrainRockHISM, Assets.TerrainRockMesh);
	SetMesh(TerrainTreeHISM, Assets.TerrainTreeMesh);
	SetMesh(TerrainBushHISM, Assets.TerrainBushMesh);
	SetMesh(ShoreRockHISM, Assets.ShoreRockMesh);
	SetMesh(ShoreReedHISM, Assets.ShoreReedMesh);
	// 땅판(들판/워존/경계), 도로, 호수 물·바닥
	SetMaterial(GroundHISM, Assets.NatureGroundMaterial);
	SetMaterial(WarZoneGroundHISM, Assets.WarZoneGroundMaterial);
	SetMaterial(TransitionGroundHISM, Assets.TransitionGroundMaterial);
	SetMaterial(RoadSurfaceHISM, Assets.RoadMaterial);
	SetMaterial(LakeWaterHISM, Assets.LakeWaterMaterial);
	SetMaterial(LakeBedHISM, Assets.LakeBedMaterial);
	// 언덕: 그릇 순서가 [띠 0 들판 × 모양 4개][띠 1 경계 × 4][띠 2 워존 × 4] 라서 번호 ÷ 모양 수 = 띠.
	const TSoftObjectPtr<UMaterialInterface>* BandMaterials[] = {
		&Assets.NatureGroundMaterial, &Assets.TransitionGroundMaterial, &Assets.WarZoneGroundMaterial };
	const int32 ShapeCount = FMath::Max(1, GetTerrainFeatureMeshes().Num());
	for (int32 Index = 0; Index < TerrainFeatureHISMs.Num(); ++Index)
	{
		const int32 Band = Index / ShapeCount;
		if (Band < UE_ARRAY_COUNT(BandMaterials))
			SetMaterial(TerrainFeatureHISMs[Index], *BandMaterials[Band]);
	}
}

void AMapBuilder::BeginPlay()
{
	Super::BeginPlay();
	// 땅판·산·돌·풀 그릇에 DA_MapAssets 의 메시·머티리얼을 먼저 끼운다(타일을 세우기 전에).
	ApplyMapAssets();
	//부모거 다 진행하고 다음 검사기 진행
	if (!Verifier)
	//1. 아직 없으면	
	{
		//2.만들어서 칸에 담고
		//Newobject<>타입을 적으면 그 타입이 T*포인터 타입으로 돌아온다.
		Verifier = NewObject<UMapVerifier>(this,TEXT("Verifier"));
		//3.내 주소를 건덴다.
		Verifier->Init(this);
	}
	// 건물 자리 담당도 같은 방법으로 만든다(만들고 → 칸에 담고 → 내 주소 건네기).
	if (!FacilityPlanner)
	{
		FacilityPlanner = NewObject<UMapFacilityPlanner>(this, TEXT("FacilityPlanner"));
		FacilityPlanner->Init(this);
	}
	// 칸 모양 담당, 흙길 담당도 같은 방법으로.
	if (!TilePlanner)
	{
		TilePlanner = NewObject<UMapTilePlanner>(this, TEXT("TilePlanner"));
		TilePlanner->Init(this);
	}
	if (!RoadPlanner)
	{
		RoadPlanner = NewObject<UMapRoadPlanner>(this, TEXT("RoadPlanner"));
		RoadPlanner->Init(this);
	}
	if (!TileSpawner)
	{
		TileSpawner = NewObject<UMapTileSpawner>(this, TEXT("TileSpawner"));
		TileSpawner->Init(this);
	}
	if (!GroundBuilder)
	{
		GroundBuilder = NewObject<UMapGroundBuilder>(this, TEXT("GroundBuilder"));
		GroundBuilder->Init(this);
	}
	if (!SpawnRegionPlanner)
	{
		SpawnRegionPlanner = NewObject<UMapSpawnRegionPlanner>(this, TEXT("SpawnRegionPlanner"));
		SpawnRegionPlanner->Init(this);
	}
	if (!PointPlanner)
	{
		PointPlanner = NewObject<UMapPointPlanner>(this, TEXT("PointPlanner"));
		PointPlanner->Init(this);
	}
	if (!HasAuthority())
		return;

	// GameMode creates the logical grid during BeginPlay. Retry briefly so this
	// preview does not depend on actor BeginPlay ordering.
	GetWorldTimerManager().SetTimer(
		RetryTimer,
		this,
		&AMapBuilder::TryReserveFootprint,
		0.2f,
		true,
		0.2f);
}

void AMapBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	for (int32 Index = 0; Index < FacilityDesignLevelInstances.Num(); ++Index)
	{
		ULevelStreamingDynamic* Instance = FacilityDesignLevelInstances[Index];
		if (LoggedFacilityDesignLevelIndices.Contains(Index)
			|| !IsValid(Instance)
			|| !Instance->IsLevelLoaded()
			|| !Instance->IsLevelVisible())
		{
			continue;
		}

		LoggedFacilityDesignLevelIndices.Add(Index);
		const ULevel* LoadedLevel = Instance->GetLoadedLevel();
		const int32 LoadedActorCount = IsValid(LoadedLevel) ? LoadedLevel->Actors.Num() : 0;
		const double RequestedAt = FacilityLoadRequestTimeSeconds.IsValidIndex(Index)
			? FacilityLoadRequestTimeSeconds[Index] : FPlatformTime::Seconds();
		UE_LOG(LogTemp, Display,
			TEXT("Facility design level loaded: index=%d type=%d actors=%d visible=true load_ms=%.2f package=%s"),
			Index,
			FacilityPlacements.IsValidIndex(Index) ? static_cast<int32>(FacilityPlacements[Index].VisualSet) : -1,
			LoadedActorCount,
			(FPlatformTime::Seconds() - RequestedAt) * 1000.0,
			*Instance->GetWorldAssetPackageName());
	}
	if (!bLoggedAllFacilityDesignLevelsLoaded
		&& !FacilityPlacements.IsEmpty()
		&& AreAllFacilityLevelsLoaded())
	{
		bLoggedAllFacilityDesignLevelsLoaded = true;
		int32 LoadedLevelCount = 0;
		for (const ULevelStreamingDynamic* Instance : FacilityDesignLevelInstances)
			if (IsValid(Instance) && Instance->IsLevelLoaded() && Instance->IsLevelVisible())
				++LoadedLevelCount;
		UE_LOG(LogTemp, Display,
			TEXT("All facility visuals ready: placements=%d runtime_blueprints=%d streamed_levels=%d"),
			FacilityPlacements.Num(),
			Algo::CountIf(SpawnedRuntimeTiles, [](const AActor* Actor)
			{
				return IsValid(Actor) && Actor->Tags.Contains(TEXT("RuntimeTacticalFacility"));
			}),
			LoadedLevelCount);
	}

	Verifier->VerifyDesignLevelSeparation();
	Verifier->VerifyWorldCollision();
	// 시설 레벨이 다 보이면 벽 속 지점을 빈 곳으로 옮긴다(한 번만).
	PointPlanner->ResolveSafety();
	// 들어온 플레이어를 시작 구역에 나눠 세운다(서버만, 새로 들어온 사람만). 시작 자리 정리가 끝난 뒤부터 일한다.
	SpawnRegionPlanner->PlaceJoinedPlayers();
	//Tactical(전투용)+Layout(배치)+Quality(품질) = 전투하기 좋게 타일이 제대로 놓였니? 
	Verifier->VerifyTacticalLayoutQuality();
	Verifier->VerifyTravelCoverDensity();
	Verifier->VerifyGameplayPointDistribution();
	Verifier->VerifyNavigation();
	Verifier->VerifyCriticalRoutes();
	Verifier->VerifyTraversableElevation();
	Verifier->VerifyCoplanarSurfaces();
	Verifier->VerifyPCGDressing();
	Verifier->VerifyLocalPerformance(DeltaSeconds);

	// 지금 조작 중인 플레이어 폰(팀 캐릭터) 주변 60 m 길찾기 차단을 갱신한다.
	const APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	const APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (IsValid(PlayerPawn) && IsValid(PlayerNavigationBlockers))
	{
		const FVector PlayerLocation = PlayerPawn->GetActorLocation();
		const FIntPoint PlayerCell(
			FMath::FloorToInt(PlayerLocation.X / 4000.0f),
			FMath::FloorToInt(PlayerLocation.Y / 4000.0f));
		if (PlayerCell != LastPlayerNavigationBlockerCell)
		{
			LastPlayerNavigationBlockerCell = PlayerCell;
			TileSpawner->RefreshNavigationBlockerRegion(PlayerNavigationBlockers, PlayerLocation, NavigationBlockerRadiusCm);
			LastPlayerNavigationBlockerUpdateTimeSeconds = FPlatformTime::Seconds();
		}
	}
}

void AMapBuilder::TryReserveFootprint()
{
	TArray<AMapTile*> AllTiles;
	TArray<float> UniqueX;

	for (TActorIterator<AMapTile> It(GetWorld()); It; ++It)
	{
		AMapTile* Tile = *It;
		AllTiles.Add(Tile);
		UniqueX.AddUnique(Tile->GetActorLocation().X);
	}

	// Runtime Blueprint mode treats AMapTile and the HISM world as logical/build
	// data only. Hide them before the relatively expensive placement conversion
	// starts so the coloured metaball preview cannot flash for a frame at PIE start.
	if (bUseRuntimeBlueprintTiles)
	{
		GroundHISM->SetVisibility(false, true);
		WarZoneGroundHISM->SetVisibility(false, true);
		TransitionGroundHISM->SetVisibility(false, true);
		RoadSurfaceHISM->SetVisibility(false, true);
		GroundHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WarZoneGroundHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TransitionGroundHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RoadSurfaceHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		for (AMapTile* Tile : AllTiles)
		{
			if (!IsValid(Tile))
				continue;
			Tile->SetActorHiddenInGame(true);
			Tile->SetActorEnableCollision(false);
			Tile->SetActorTickEnabled(false);
		}
	}

	if (AllTiles.Num() == 0)
		return;

	UniqueX.Sort();
	GridStep = 0.0f;
	for (int32 Index = 1; Index < UniqueX.Num(); ++Index)
	{
		const float Difference = UniqueX[Index] - UniqueX[Index - 1];
		if (Difference > KINDA_SMALL_NUMBER && (GridStep <= 0.0f || Difference < GridStep))
			GridStep = Difference;
	}

	if (GridStep <= 0.0f)
		return;

	TMap<FIntPoint, AMapTile*> WarZoneByCell;
	TMap<FIntPoint, AMapTile*> TileByCell;
	for (AMapTile* Tile : AllTiles)
	{
		const FVector Location = Tile->GetActorLocation();
		const FIntPoint Cell(
			FMath::RoundToInt(Location.X / GridStep),
			FMath::RoundToInt(Location.Y / GridStep));
		TileByCell.Add(Cell, Tile);
		if (Tile->GetType() == ETileType::WarZone)
			WarZoneByCell.Add(Cell, Tile);
	}

	// 큰 건물(창고·마당·막사·참호·검문소·다운타운·공장·호숫가 마을) 자리는 건물 자리 담당이 고른다.
	// 결과는 맵의 FacilityPlacements 목록에 들어간다. 자리를 못 찾으면(워존이 너무 작음) 다음 틱에 다시 시도.
	if (!FacilityPlanner->PlanFacilities(TileByCell, WarZoneByCell, AllTiles.Num()))
		return;
	// 길찾기 바닥판을 맵 크기(GridCellSpan, 건물 자리 담당이 재 둠)에 맞춘다.
	// Recast is fed by this single floor rather than by geometry exported from every
	// tile, so it has to cover the generated grid and no more. Sized for a fixed
	// 900 m it left walkable void hanging off the edge of a smaller map.
	if (IsValid(NavigationFloor) && GridCellSpan > 0)
	{
		const float NavigationFloorScale = GridCellSpan * DesignCellSize / 100.0f;
		NavigationFloor->SetRelativeScale3D(
			FVector(NavigationFloorScale, NavigationFloorScale, 0.2f));
	}

	// 칸마다 어떤 타일을 몇 도 돌려 놓을지는 칸 모양 담당이 정한다(흙길은 그 안에서 흙길 담당에게 묻는다).
	TilePlanner->BuildTileDesignPlacements(TileByCell);

	Tags.AddUnique(ReservedTag);
	Tags.AddUnique(TEXT("FacilityPlacementPreview"));
	GroundBuilder->BuildLightweightWorldVisuals();
	TileSpawner->SpawnRuntimeBlueprintTiles();
	// 시작 자리·상자 자리·몬스터 자리·출구·퀘스트 자리는 지점 담당이 찍는다.
	PointPlanner->BuildPoints();
	// Runs alongside runtime Blueprint tiles: their HISM dressing covers per-tile
	// props, while this pass scatters grass/shrub clumps across meadow, scrub,
	// ruin and open-ground cells to break up the 20m tile seams between them.
	GroundBuilder->BuildPCGDressingGraph();
	NavigationValidationStartTimeSeconds = FPlatformTime::Seconds();
	FacilityDesignLevelInstances.Reset();
	FacilityLoadRequestTimeSeconds.Reset();
	LoggedFacilityDesignLevelIndices.Reset();
	for (int32 FacilityIndex = 0; FacilityIndex < FacilityPlacements.Num(); ++FacilityIndex)
		TileSpawner->LoadFacilityDesignLevel(FacilityPlacements[FacilityIndex], FacilityIndex);
	GroundBuilder->BuildBorderMountains();

	// The brother's AMapTile actors remain authoritative logical records. Once
	// converted, disable only their runtime presentation/collision so HISM and
	// Level Instances become the visible/playable world without destroying data.
	for (AMapTile* Tile : AllTiles)
	{
		if (!IsValid(Tile))
			continue;
		Tile->SetActorHiddenInGame(true);
		Tile->SetActorEnableCollision(false);
		Tile->SetActorTickEnabled(false);
	}

	GetWorldTimerManager().ClearTimer(RetryTimer);

	UE_LOG(LogTemp, Display,
		TEXT("Reserved facility set: total=%d Compound3x3 origin=(%d,%d) Camps2x2=%d Checkpoint found=%s grid_step=%.1f"),
		FacilityPlacements.Num(),
		FacilityPlanner->GetCompoundAnchor().X, FacilityPlanner->GetCompoundAnchor().Y, FacilityPlanner->GetCampCount(),
		FacilityPlanner->FoundCheckpoint() ? TEXT("true") : TEXT("false"), GridStep);
}

void AMapBuilder::BuildShoreTransitionMap(
	const TSet<FIntPoint>& LakeCells,
	TMap<FIntPoint, TPair<int32, int32>>& OutShoreTileByCell) const
{
	OutShoreTileByCell.Reset();
	if (LakeCells.IsEmpty())
		return;

	// Marching squares. A cell corner is wet when any of the four cells meeting there
	// is water, so the waterline cuts across cells instead of running along their
	// edges. Two neighbours always share two corners, and the generated meshes
	// interpolate each edge purely from that edge's two corner heights, so their
	// profiles are identical from both sides - no wall can appear between them.
	//
	// Choosing by *sides* instead was the earlier mistake: a beach cell's edge runs
	// -110 to +20 along its length while the flat cell beside it is +20 throughout,
	// which put a 130 cm step wherever the shoreline ended.
	auto IsCornerWet = [&LakeCells](const FIntPoint& Cell, int32 CornerIndex)
	{
		// Corner order SW, SE, NE, NW - matching the generated meshes.
		static const FIntPoint CornerOffsets[4][4] = {
			{ FIntPoint(0, 0), FIntPoint(-1, 0), FIntPoint(0, -1), FIntPoint(-1, -1) },
			{ FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(1, -1) },
			{ FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(1, 1) },
			{ FIntPoint(0, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(-1, 1) }
		};
		for (int32 Index = 0; Index < 4; ++Index)
			if (LakeCells.Contains(Cell + CornerOffsets[CornerIndex][Index]))
				return true;
		return false;
	};

	// Base masks in the same order the generator writes its assets.
	static const uint8 BaseMasks[] = { 0b0001, 0b0011, 0b0101, 0b0111 };
	auto RotateMask = [](uint8 Mask, int32 QuarterTurns)
	{
		uint8 Rotated = 0;
		for (int32 Bit = 0; Bit < 4; ++Bit)
			if (Mask & (1 << Bit))
				Rotated |= 1 << ((Bit + QuarterTurns) % 4);
		return Rotated;
	};

	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		uint8 CornerMask = 0;
		for (int32 CornerIndex = 0; CornerIndex < 4; ++CornerIndex)
			if (IsCornerWet(Placement.GridCell, CornerIndex))
				CornerMask |= 1 << CornerIndex;

		// All dry is ordinary flat ground. All wet means fully submerged, which the
		// lake bed and water sheet already cover - but only for cells the generator
		// actually marked as water. A *land* cell touching water on two or more sides
		// also ends up with four wet corners, and it used to fall between both paths:
		// no shore mesh, and a flat slab left at Z=20 while everything around it
		// dropped to the lake floor. Those were the holes along the shoreline.
		if (CornerMask == 0)
			continue;
		if (CornerMask == 0b1111)
		{
			OutShoreTileByCell.Add(Placement.GridCell, TPair<int32, int32>(SubmergedShoreVariant, 0));
			continue;
		}

		for (int32 VariantIndex = 0; VariantIndex < UE_ARRAY_COUNT(BaseMasks); ++VariantIndex)
		{
			bool bMatched = false;
			for (int32 QuarterTurns = 0; QuarterTurns < 4 && !bMatched; ++QuarterTurns)
			{
				if (RotateMask(BaseMasks[VariantIndex], QuarterTurns) != CornerMask)
					continue;
				OutShoreTileByCell.Add(
					Placement.GridCell, TPair<int32, int32>(VariantIndex, QuarterTurns));
				bMatched = true;
			}
			if (bMatched)
				break;
		}
	}
}






float AMapBuilder::GetSurfaceElevationForCell(const FIntPoint& Cell) const
{
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		if (!Placement.OccupiedCells.Contains(Cell))
			continue;
		return Placement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : Placement.BaseElevationCm;
	}
	// Everything outside a facility footprint shares the one ground datum.
	//
	// This used to blend a one- and two-cell "shoulder" ring around every raised or
	// lowered pad. That reads as a slope on paper only: ground is rendered as one
	// flat 20 m slab per cell, so the ring was really a stack of 20 m terraces whose
	// risers measured 30-90 cm. Against a 45 cm MaxStepHeight that produced hard
	// lips out in the open - including across roads that merely passed within two
	// cells of a facility - and the rings of neighbouring facilities stacked into
	// still taller ones. Deliberate elevation now exists only on facility
	// footprints, which own an explicit vehicle ramp and infantry stair built by
	// BuildElevatedFacilityTerrain.
	return BaseGroundSurfaceZ;
}

void AMapBuilder::GetFacilityAccessEdges(
	const FFacilityPlacement& Placement,
	TArray<TPair<FIntPoint, FIntPoint>>& OutAccessEdges) const
{
	OutAccessEdges.Reset();
	if (Placement.OccupiedCells.IsEmpty() || Placement.EntranceDirection == FIntPoint::ZeroValue)
		return;

	OutAccessEdges.Emplace(Placement.EntranceCell, Placement.EntranceDirection);

	// The WarZone core is the raid's final engagement and its pad is 3x3 cells, so
	// one ramp leaves roughly 5% of a 240 m perimeter climbable and funnels every
	// attacker into a single lane. Give it an approach on each side instead. The
	// outlying 2x2 and 2x1 facilities keep their single entrance: at that size one
	// way in reads as a defensible outpost rather than a bottleneck.
	const bool bWarZoneCore = Placement.VisualSet == EFacilityVisualSet::Warehouse
		&& Placement.Footprint == WarZoneCoreFootprint;
	if (!bWarZoneCore)
		return;

	int32 MinX = MAX_int32;
	int32 MinY = MAX_int32;
	int32 MaxX = MIN_int32;
	int32 MaxY = MIN_int32;
	for (const FIntPoint& Cell : Placement.OccupiedCells)
	{
		MinX = FMath::Min(MinX, Cell.X);
		MinY = FMath::Min(MinY, Cell.Y);
		MaxX = FMath::Max(MaxX, Cell.X);
		MaxY = FMath::Max(MaxY, Cell.Y);
	}
	const int32 MidX = (MinX + MaxX) / 2;
	const int32 MidY = (MinY + MaxY) / 2;
	const TPair<FIntPoint, FIntPoint> SideCandidates[] = {
		{ FIntPoint(MidX, MaxY), FIntPoint(0, 1) },
		{ FIntPoint(MaxX, MidY), FIntPoint(1, 0) },
		{ FIntPoint(MidX, MinY), FIntPoint(0, -1) },
		{ FIntPoint(MinX, MidY), FIntPoint(-1, 0) }
	};
	for (const TPair<FIntPoint, FIntPoint>& Candidate : SideCandidates)
	{
		// The authored entrance already covers its own side. Adding the mid-edge cell
		// for that same direction would stack a second ramp on top of the first.
		if (Candidate.Value == Placement.EntranceDirection)
			continue;
		if (!Placement.OccupiedCells.Contains(Candidate.Key))
			continue;
		OutAccessEdges.Add(Candidate);
	}
}

bool AMapBuilder::AreAllFacilityLevelsLoaded() const
{
	if (FacilityPlacements.IsEmpty())
		return false;

	if (bUseRuntimeBlueprintTiles)
	{
		for (int32 Index = 0; Index < FacilityPlacements.Num(); ++Index)
		{
			if (FacilityPlacements[Index].VisualSet != EFacilityVisualSet::Checkpoint)
				continue;
			if (!FacilityDesignLevelInstances.IsValidIndex(Index))
				return false;
			const ULevelStreamingDynamic* Instance = FacilityDesignLevelInstances[Index];
			if (!IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
				return false;
		}
		return true;
	}

	if (FacilityDesignLevelInstances.Num() != FacilityPlacements.Num()
		|| FacilityDesignLevelInstances.IsEmpty())
		return false;
	for (const ULevelStreamingDynamic* Instance : FacilityDesignLevelInstances)
		if (!IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
			return false;
	return true;
}

FVector AMapBuilder::GetDesignFootprintCenter(
	const FFacilityPlacement& Placement) const
{
	if (Placement.OccupiedCells.IsEmpty())
		return FVector::ZeroVector;

	FVector Center = FVector::ZeroVector;
	for (const FIntPoint& Cell : Placement.OccupiedCells)
	{
		Center.X += Cell.X * DesignCellSize;
		Center.Y += Cell.Y * DesignCellSize;
	}
	Center /= Placement.OccupiedCells.Num();
	// Keep authored floor meshes just clear of the shared terrain top to avoid z-fighting
	// without making the structure appear to float.
	// This removes coplanar shimmer without producing a visible gameplay step.
	const float SurfaceZ = Placement.ElevationProfile == EFacilityElevationProfile::Ground
		? BaseGroundSurfaceZ : Placement.BaseElevationCm;
	Center.Z = SurfaceZ + 1.0f;
	return Center;
}

