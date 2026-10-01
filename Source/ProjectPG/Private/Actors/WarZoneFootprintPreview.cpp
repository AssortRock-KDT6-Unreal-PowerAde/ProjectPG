// Visual layer for the procedural map: turns the logical AMapTile grid into tiles, facilities and the border lake.

#include "Actors/WarZoneFootprintPreview.h"
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
#include "Actors/WarZoneFootprint/MapVerifier.h"
// 같이 쓰는 숫자·경로·작은 계산은 MapBuildShared.h 한 곳에 있다(일꾼들도 같은 것을 본다).
#include "Actors/WarZoneFootprint/MapBuildShared.h"
#include "Actors/WarZoneFootprint/MapFacilityPlanner.h"
#include "Actors/WarZoneFootprint/MapTilePlanner.h"
#include "Actors/WarZoneFootprint/MapRoadPlanner.h"
#include "Actors/WarZoneFootprint/MapTileSpawner.h"
#include "Actors/WarZoneFootprint/MapGroundBuilder.h"
using namespace MapBuild;


AWarZoneFootprintPreview::AWarZoneFootprintPreview()
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
	static ConstructorHelpers::FObjectFinder<UStaticMesh> MountainMesh(
		TEXT("/Game/Downtown_West/Assets/background_mountain/SM_background_mountains"));
	MountainHISM->SetupAttachment(SceneRoot);
	if (MountainMesh.Succeeded())
		MountainHISM->SetStaticMesh(MountainMesh.Object);
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
		const TCHAR* BandMaterialPaths[] = {
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified"),
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_Transition"),
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_WarZone")
		};
		const TCHAR* BandNames[] = { TEXT("Nature"), TEXT("Transition"), TEXT("WarZone") };
		const TArray<FTerrainFeatureMesh>& FeatureMeshes = GetTerrainFeatureMeshes();
		for (int32 BandIndex = 0; BandIndex < UE_ARRAY_COUNT(BandNames); ++BandIndex)
		{
			ConstructorHelpers::FObjectFinder<UMaterialInterface> BandMaterial(BandMaterialPaths[BandIndex]);
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
				if (BandMaterial.Succeeded())
					Component->SetMaterial(0, BandMaterial.Object);
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
	struct FTerrainDressingSpec
	{
		const TCHAR* ComponentName;
		const TCHAR* AssetPath;
		bool bCollides;
	};
	const FTerrainDressingSpec DressingSpecs[] = {
		{ TEXT("TerrainRockHISM"), TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_medium_a_low.SM_rock_medium_a_low"), true },
		{ TEXT("TerrainTreeHISM"), TEXT("/PCGBiomeSample/Meshes/PCG_Pine_01.PCG_Pine_01"), true },
		{ TEXT("TerrainBushHISM"), TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2"), false },
		// Shore dressing. The waterline is decided per 20 m cell, so on its own it
		// steps along the grid. Rocks and reeds standing in the shallows break that
		// line up - the same trick the diorama uses along its own bank.
		{ TEXT("ShoreRockHISM"), TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/River_Stone_2.River_Stone_2"), true },
		{ TEXT("ShoreReedHISM"), TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Cat_Tail.Cat_Tail"), false }
	};
	TArray<UHierarchicalInstancedStaticMeshComponent*> DressingComponents;
	for (const FTerrainDressingSpec& Spec : DressingSpecs)
	{
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Spec.ComponentName);
		Component->SetupAttachment(SceneRoot);
		ConstructorHelpers::FObjectFinder<UStaticMesh> DressingMesh(Spec.AssetPath);
		if (DressingMesh.Succeeded())
			Component->SetStaticMesh(DressingMesh.Object);
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

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WarZoneGroundMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_WarZone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TransitionGroundMaterial(
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_Transition"));
	if (GroundMaterial.Succeeded())
		GroundHISM->SetMaterial(0, GroundMaterial.Object);
	if (WarZoneGroundMaterial.Succeeded())
		WarZoneGroundHISM->SetMaterial(0, WarZoneGroundMaterial.Object);
	if (TransitionGroundMaterial.Succeeded())
		TransitionGroundHISM->SetMaterial(0, TransitionGroundMaterial.Object);
	if (RoadMaterial.Succeeded())
		RoadSurfaceHISM->SetMaterial(0, RoadMaterial.Object);
	// Reuse the rural diorama's own lake material so the border water and the
	// water already inside that facility read as one body.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LakeWaterMaterial(
		TEXT("/Game/Modular_Rural_Cabin/Materials/Instances/Water_Lake"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> LakeBedMaterial(
		TEXT("/Game/Modular_Rural_Cabin/Materials/Instances/Diorama_Ground"));
	if (LakeWaterMaterial.Succeeded())
		LakeWaterHISM->SetMaterial(0, LakeWaterMaterial.Object);
	if (LakeBedMaterial.Succeeded())
		LakeBedHISM->SetMaterial(0, LakeBedMaterial.Object);
}

void AWarZoneFootprintPreview::BeginPlay()
{
	Super::BeginPlay();
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
	if (!HasAuthority())
		return;

	// GameMode creates the logical grid during BeginPlay. Retry briefly so this
	// preview does not depend on actor BeginPlay ordering.
	GetWorldTimerManager().SetTimer(
		RetryTimer,
		this,
		&AWarZoneFootprintPreview::TryReserveFootprint,
		0.2f,
		true,
		0.2f);
}

void AWarZoneFootprintPreview::Tick(float DeltaSeconds)
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
	ResolveGameplayPointSafety();
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
			TileSpawner->RefreshNavigationBlockerRegion(PlayerNavigationBlockers, PlayerLocation, 6000.0f);
			LastPlayerNavigationBlockerUpdateTimeSeconds = FPlatformTime::Seconds();
		}
	}
}

void AWarZoneFootprintPreview::TryReserveFootprint()
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
	BuildGameplayPointMarkers();
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

void AWarZoneFootprintPreview::BuildGameplayPointMarkers()
{
	LevelDesignPoints.Reset();
	TMap<ELevelDesignPointType, int32> Counts;
	auto IsProtectedFromAI = [this](const FIntPoint& Cell)
	{
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		{
			const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
				+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
			if (Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
				return true; // 60m minimum spawn safe band
			if (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2)
				return true; // do not camp directly on the extraction stencil
		}
		// The boat landing is a spawn too, but it is a gameplay point on the rural
		// hamlet, not a Spawn tile, so the band above cannot see it. Without this
		// the natural encounters crept to 49.5 m of the landing and the hamlet
		// itself was tallied as a facility missing its resident AI.
		if (bHasBorderLake)
		{
			for (const FFacilityPlacement& Facility : FacilityPlacements)
			{
				if (Facility.VisualSet != EFacilityVisualSet::RuralHideout)
					continue;
				for (const FIntPoint& Occupied : Facility.OccupiedCells)
					if (FMath::Abs(Cell.X - Occupied.X) + FMath::Abs(Cell.Y - Occupied.Y) < 3)
						return true;
			}
		}
		return false;
	};

	auto AddPoint = [this, &Counts](
		ELevelDesignPointType Type,
		const FIntPoint& Cell,
		const FVector& Offset,
		const TCHAR* Prefix,
		const TCHAR* Archetype,
		uint8 Tier,
		float RadiusCm,
		int32 Capacity)
	{
		const int32 Index = Counts.FindOrAdd(Type)++;
		FLevelDesignPoint& Point = LevelDesignPoints.AddDefaulted_GetRef();
		Point.Type = Type;
		Point.GridCell = Cell;
		FVector TacticalOffset = Offset;
		if (const FTileDesignPlacement* Placement = TileDesignPlacements.FindByPredicate(
			[Cell](const FTileDesignPlacement& Candidate) { return Candidate.GridCell == Cell; }))
		{
			const FRotator Rotation(0.0f, Placement->RotationQuarterTurns * 90.0f, 0.0f);
			TacticalOffset = Rotation.RotateVector(Offset);
			switch (Type)
			{
			case ELevelDesignPointType::Spawn:
				TacticalOffset += Rotation.RotateVector(FVector(-600.0f, 0.0f, 0.0f)); break;
			case ELevelDesignPointType::Exit:
				TacticalOffset += Rotation.RotateVector(FVector(550.0f, 0.0f, 0.0f)); break;
			case ELevelDesignPointType::Loot:
				TacticalOffset += FVector(-320.0f, 480.0f, 0.0f); break;
			case ELevelDesignPointType::AISpawn:
				TacticalOffset += FVector(420.0f, -420.0f, 0.0f); break;
			case ELevelDesignPointType::Quest:
				TacticalOffset += FVector(0.0f, 0.0f, 0.0f); break;
			}
		}
		Point.WorldLocation = FVector(
			Cell.X * DesignCellSize,
			Cell.Y * DesignCellSize,
			120.0f + GetSurfaceElevationForCell(Cell)) + TacticalOffset;
		// The authored offsets are preferred, but random tactical variants can place
		// cover there. Search a deterministic 3x3 pocket so every emitted point is
		// actually spawnable without changing the selected tile or layout hash.
		const FVector CandidateOffsets[] = {
			FVector::ZeroVector,
			FVector(320, 0, 0), FVector(-320, 0, 0), FVector(0, 320, 0), FVector(0, -320, 0),
			FVector(320, 320, 0), FVector(-320, 320, 0), FVector(320, -320, 0), FVector(-320, -320, 0)
		};
		FCollisionQueryParams PointQuery(SCENE_QUERY_STAT(LevelDesignPointPlacement), false);
		const FCollisionShape PointCapsule = FCollisionShape::MakeCapsule(55.0f, 95.0f);
		for (const FVector& CandidateOffset : CandidateOffsets)
		{
			const FVector Candidate = Point.WorldLocation + CandidateOffset;
			TArray<FOverlapResult> Overlaps;
			const bool bOverlap = GetWorld()->OverlapMultiByChannel(
				Overlaps,
				Candidate + FVector(0, 0, 95.0f),
				FQuat::Identity,
				ECC_Pawn,
				PointCapsule,
				PointQuery);
			const bool bBlocked = bOverlap && Overlaps.ContainsByPredicate(
				[this](const FOverlapResult& Result)
				{
					const AActor* HitActor = Result.GetActor();
					const UPrimitiveComponent* HitComponent = Result.GetComponent();
					return IsValid(HitActor) && HitActor != this
						&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
						&& !(bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
						&& IsValid(HitComponent)
						&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
				});
			const bool bTooCloseToSpawn = Type == ELevelDesignPointType::Spawn
				&& LevelDesignPoints.ContainsByPredicate(
					[&Point, &Candidate](const FLevelDesignPoint& Existing)
					{
						return &Existing != &Point
							&& Existing.Type == ELevelDesignPointType::Spawn
							&& FVector::DistSquared2D(Existing.WorldLocation, Candidate) < FMath::Square(250.0f);
					});
			if (!bBlocked && !bTooCloseToSpawn)
			{
				Point.WorldLocation = Candidate;
				break;
			}
		}
		Point.PointId = FName(*FString::Printf(TEXT("%s_%02d"), Prefix, Index));
		Point.ArchetypeId = FName(Archetype);
		Point.Tier = FMath::Clamp<uint8>(Tier, 1, 3);
		Point.RadiusCm = FMath::Max(50.0f, RadiusCm);
		Point.Capacity = FMath::Max(1, Capacity);
		Point.PointSeed = static_cast<int64>(HashCombine(
			LayoutHash,
			HashCombine(
				GetTypeHash(static_cast<uint8>(Type)),
				HashCombine(GetTypeHash(Cell.X), HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Index))))));

		FActorSpawnParameters SpawnParameters;
		SpawnParameters.Owner = this;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ATargetPoint* Marker = GetWorld()->SpawnActor<ATargetPoint>(
			ATargetPoint::StaticClass(),
			Point.WorldLocation,
			FRotator::ZeroRotator,
			SpawnParameters);
		if (IsValid(Marker))
		{
			Marker->SetActorLabel(Point.PointId.ToString());
			Marker->Tags.AddUnique(TEXT("LevelDesignPoint"));
			Marker->Tags.AddUnique(FName(Prefix));
			Marker->Tags.AddUnique(Point.PointId);
			Marker->SetFolderPath(TEXT("RuntimeDesign/Points"));
		}
	};

	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		// Nothing gameplay-facing belongs in the lake.
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (Placement.Visual == ETileDesignVisual::Spawn)
		{
			// Four candidates prevent a future squad from stacking into one capsule.
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(0, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, -430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
			AddPoint(ELevelDesignPointType::Spawn, Placement.GridCell, FVector(360, 430, 0), TEXT("SpawnPoint"), TEXT("PlayerSquad"), 1, 130, 1);
		}
		else if (Placement.Visual == ETileDesignVisual::Exit)
			AddPoint(ELevelDesignPointType::Exit, Placement.GridCell, FVector::ZeroVector, TEXT("ExitPoint"), TEXT("Extraction"), 1, 600, 8);
		else if (((Placement.Visual == ETileDesignVisual::WarZoneGround && Placement.LocalSeed % 17 == 0)
				|| ((Placement.Visual == ETileDesignVisual::OpenGround
						|| Placement.Visual == ETileDesignVisual::Ruins)
					&& Placement.LocalSeed % 31 == 0))
			&& !IsProtectedFromAI(Placement.GridCell))
		{
			const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::WarZoneGround
				? TEXT("ScavPatrol") : TEXT("PerimeterPatrol");
			AddPoint(ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector, TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
		}
		else if (Placement.Visual == ETileDesignVisual::Ruins
			&& Placement.LocalSeed % 3 == 0)
		{
			const uint8 LootTier = Placement.LocalSeed % 19 == 0 ? 3 : (Placement.LocalSeed % 5 == 0 ? 2 : 1);
			AddPoint(ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector, TEXT("LootPoint"), TEXT("RuinsLooseLoot"), LootTier, 100, 1);
		}

		const bool bNaturalTile = Placement.Visual == ETileDesignVisual::NatureMeadow
			|| Placement.Visual == ETileDesignVisual::NatureForestSparse
			|| Placement.Visual == ETileDesignVisual::NatureForestDense
			|| Placement.Visual == ETileDesignVisual::NatureRocky
			|| Placement.Visual == ETileDesignVisual::NatureScrub
			|| Placement.Visual == ETileDesignVisual::NatureAmbush
			|| Placement.Visual == ETileDesignVisual::NatureServiceCamp
			|| Placement.Visual == ETileDesignVisual::NatureDitch;
		if (bNaturalTile)
		{
			// A deterministic low-density lattice makes the entire 900m field useful
			// for a loot-shooter. The server can serialize these markers in the future
			// manifest; no client-side random choice is involved.
			const int32 StableX = Placement.GridCell.X + 64;
			const int32 StableY = Placement.GridCell.Y + 64;
			const bool bNaturalLoot = (StableX % 7 == 0 && StableY % 7 == 0)
				|| (Placement.Visual == ETileDesignVisual::NatureServiceCamp && Placement.LocalSeed % 3 == 0);
			if (bNaturalLoot)
			{
				const bool bHighValue = Placement.Visual == ETileDesignVisual::NatureServiceCamp
					|| Placement.Visual == ETileDesignVisual::NatureRocky;
				AddPoint(ELevelDesignPointType::Loot, Placement.GridCell, FVector::ZeroVector,
					TEXT("LootPoint"), bHighValue ? TEXT("FieldCache") : TEXT("HiddenStash"),
					bHighValue ? 2 : 1, 120, 1);
			}

			const bool bNaturalEncounter = (StableX % 8 == 4 && StableY % 8 == 4)
				|| (Placement.Visual == ETileDesignVisual::NatureAmbush && Placement.LocalSeed % 11 == 0);
			if (bNaturalEncounter && !IsProtectedFromAI(Placement.GridCell))
			{
				const TCHAR* AIProfile = Placement.Visual == ETileDesignVisual::NatureAmbush
					? TEXT("ForestAmbush") : TEXT("WildernessPatrol");
				AddPoint(ELevelDesignPointType::AISpawn, Placement.GridCell, FVector::ZeroVector,
					TEXT("AISpawnPoint"), AIProfile, 1, 350, 3);
			}
		}
	}

	auto AddFacilityPoint = [this, &AddPoint](
		const FFacilityPlacement& Facility,
		ELevelDesignPointType Type,
		const FVector& LocalSocket,
		const TCHAR* Prefix,
		const TCHAR* Archetype,
		uint8 Tier,
		float RadiusCm,
		int32 Capacity)
	{
		if (Facility.OccupiedCells.IsEmpty())
			return;
		const FVector FacilityCenter = GetDesignFootprintCenter(Facility);
		const FRotator FacilityRotation(0.0f, Facility.RotationQuarterTurns * 90.0f, 0.0f);
		const FVector DesiredWorld = FacilityCenter + FacilityRotation.RotateVector(LocalSocket);
		const FIntPoint SocketCell = *Algo::MinElementBy(
			Facility.OccupiedCells,
			[&DesiredWorld](const FIntPoint& Cell)
			{
				return FVector2D(
					Cell.X * DesignCellSize - DesiredWorld.X,
					Cell.Y * DesignCellSize - DesiredWorld.Y).SizeSquared();
			});
		const FVector CellBase(
			SocketCell.X * DesignCellSize,
			SocketCell.Y * DesignCellSize,
			120.0f + Facility.BaseElevationCm);
		AddPoint(Type, SocketCell, DesiredWorld - CellBase, Prefix, Archetype, Tier, RadiusCm, Capacity);
	};

	for (const FFacilityPlacement& Facility : FacilityPlacements)
	{
		if (Facility.VisualSet == EFacilityVisualSet::Warehouse)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1850, -830, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 3, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(2100, 1850, 580), TEXT("LootPoint"), TEXT("WarehouseContainer"), 3, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-500, 1500, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1400, -1200, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			// The 3x5 compound's outer hall rows. Growing the core ate WarZone_Mid
			// band cells and their tile loot with them - the map-wide count fell
			// under the verifier's 40 - so the halls that replaced those cells carry
			// the loot instead.
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(0, -3900, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(0, 3800, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1400, 2600, 120), TEXT("LootPoint"), TEXT("WarehouseContainer"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-900, -1650, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(900, 650, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(2100, 1850, 580), TEXT("QuestPoint"), TEXT("WarZonePrimaryObjective"), 3, 160, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::Yard)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, -700, 120), TEXT("LootPoint"), TEXT("YardStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1050, -950, 360), TEXT("LootPoint"), TEXT("YardStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(1200, 900, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-1200, -900, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::LongBarracks)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1100, 350, 120), TEXT("LootPoint"), TEXT("BarracksLocker"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1100, -350, 120), TEXT("LootPoint"), TEXT("BarracksLocker"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-1500, -450, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
			if ((Facility.LocalSeed & 1u) == 0u)
				AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(1100, -350, 120), TEXT("QuestPoint"), TEXT("OptionalFacilityObjective"), 2, 140, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::LinearTrench)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-2600, 0, 120), TEXT("LootPoint"), TEXT("TrenchCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(2600, 0, 120), TEXT("LootPoint"), TEXT("TrenchCache"), 1, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-3400, 0, 120), TEXT("AISpawnPoint"), TEXT("FacilityPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::DowntownBlock)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-1600, 700, 120), TEXT("LootPoint"), TEXT("DowntownShopCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(1450, -750, 120), TEXT("LootPoint"), TEXT("DowntownBackroom"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(0, 1400, 120), TEXT("AISpawnPoint"), TEXT("DowntownPatrol"), 2, 300, 2);
			AddFacilityPoint(Facility, ELevelDesignPointType::Quest, FVector(1500, 850, 120), TEXT("QuestPoint"), TEXT("DowntownObjective"), 2, 140, 1);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::FactoryConstruction)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-800, -700, 120), TEXT("LootPoint"), TEXT("FactoryToolCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, 650, 120), TEXT("LootPoint"), TEXT("FactoryOfficeCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(900, -900, 120), TEXT("AISpawnPoint"), TEXT("FactoryPatrol"), 2, 300, 2);
		}
		else if (Facility.VisualSet == EFacilityVisualSet::RuralHideout)
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(-900, 250, 120), TEXT("LootPoint"), TEXT("RuralCabinStash"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector(900, -250, 120), TEXT("LootPoint"), TEXT("RuralCaravanStash"), 1, 100, 1);
			// The hamlet hosts the boat-landing spawn whenever the lake exists, and
			// a facility that spawns players keeps no resident AI - the same rule
			// the spawn safe band applies everywhere else. A 40 m hamlet cannot hold
			// both a spawn and an ambush 50 m apart (measured 26 m: pass=false).
			if (!bHasBorderLake)
				AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(0, -900, 120), TEXT("AISpawnPoint"), TEXT("RuralAmbush"), 1, 260, 2);
			if (bHasBorderLake)
			{
				// "Arrived by boat": a spawn on the hamlet's water-facing edge. The
				// lake corner moves per seed, so aim at the recorded lake centre and
				// undo the facility's yaw - a fixed socket would face the water only
				// on the seeds that happen to rotate the hamlet the authored way.
				const FVector FacilityCentre = GetDesignFootprintCenter(Facility);
				const FVector2D ToLake = (BorderLakeCentreCell * DesignCellSize
					- FVector2D(FacilityCentre.X, FacilityCentre.Y)).GetSafeNormal();
				const FRotator FacilityYaw(0.0f, Facility.RotationQuarterTurns * 90.0f, 0.0f);
				const FVector LakeSocket = FacilityYaw.UnrotateVector(
					FVector(ToLake.X, ToLake.Y, 0.0f) * 1900.0f) + FVector(0.0f, 0.0f, 120.0f);
				AddFacilityPoint(Facility, ELevelDesignPointType::Spawn, LakeSocket,
					TEXT("SpawnPoint"), TEXT("BoatLanding"), 2, 150, 2);
			}
		}
		else
		{
			AddFacilityPoint(Facility, ELevelDesignPointType::Loot, FVector::ZeroVector, TEXT("LootPoint"), TEXT("CheckpointCache"), 2, 100, 1);
			AddFacilityPoint(Facility, ELevelDesignPointType::AISpawn, FVector(-500, 500, 120), TEXT("AISpawnPoint"), TEXT("CheckpointGuard"), 2, 300, 2);
		}
	}

	RebuildGameplayPointHash();

	// 오브젝트 스포너 등 데이터 소비자에게 알린다. 재시도로 다시 만들어져도 첫 확정만 알린다.
	if (!bLevelDesignPointsBuilt)
	{
		bLevelDesignPointsBuilt = true;
		OnLevelDesignPointsBuilt.Broadcast(LevelDesignPoints);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Design points: total=%d spawn=%d loot=%d ai=%d exit=%d quest=%d point_hash=%08X"),
		LevelDesignPoints.Num(),
		Counts.FindRef(ELevelDesignPointType::Spawn),
		Counts.FindRef(ELevelDesignPointType::Loot),
		Counts.FindRef(ELevelDesignPointType::AISpawn),
		Counts.FindRef(ELevelDesignPointType::Exit),
		Counts.FindRef(ELevelDesignPointType::Quest),
		GameplayPointHash);
}

void AWarZoneFootprintPreview::RebuildGameplayPointHash()
{
	GameplayPointHash = 0;
	for (const FLevelDesignPoint& Point : LevelDesignPoints)
	{
		// FName's runtime comparison index is process-local. Hash the serialized
		// string contents so server and clients agree after independent launches.
		GameplayPointHash = HashCombine(GameplayPointHash, FCrc::StrCrc32(*Point.PointId.ToString()));
		GameplayPointHash = HashCombine(GameplayPointHash, FCrc::StrCrc32(*Point.ArchetypeId.ToString()));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.GridCell.X));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.GridCell.Y));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.Tier));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.X)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Y)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.WorldLocation.Z)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(FMath::RoundToInt(Point.RadiusCm)));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.Capacity));
		GameplayPointHash = HashCombine(GameplayPointHash, GetTypeHash(Point.PointSeed));
	}
}

void AWarZoneFootprintPreview::ResolveGameplayPointSafety()
{
	if (bResolvedGameplayPointSafety || !AreAllFacilityLevelsLoaded() || LevelDesignPoints.IsEmpty())
		return;

	TArray<FVector> CandidateOffsets;
	for (int32 X = -4; X <= 4; ++X)
		for (int32 Y = -4; Y <= 4; ++Y)
			CandidateOffsets.Add(FVector(X * 225.0f, Y * 225.0f, 0.0f));
	CandidateOffsets.Sort([](const FVector& A, const FVector& B)
	{
		const float ADistance = A.SizeSquared2D();
		const float BDistance = B.SizeSquared2D();
		if (!FMath::IsNearlyEqual(ADistance, BDistance)) return ADistance < BDistance;
		return !FMath::IsNearlyEqual(A.X, B.X) ? A.X < B.X : A.Y < B.Y;
	});

	auto IsBlocked = [this](const FVector& Location)
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(ResolveGameplayPointSafety), false);
		const bool bOverlap = GetWorld()->OverlapMultiByChannel(
			Overlaps,
			Location + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			FCollisionShape::MakeCapsule(55.0f, 95.0f),
			Query);
		return bOverlap && Overlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != this
					&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
					&& !(bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
					&& IsValid(HitComponent)
					&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			});
	};

	int32 RelocatedCount = 0;
	int32 UnresolvedCount = 0;
	TArray<FString> UnresolvedIds;
	TArray<FVector> ResolvedSpawnLocations;
	for (FLevelDesignPoint& Point : LevelDesignPoints)
	{
		const bool bSpawnTooClose = Point.Type == ELevelDesignPointType::Spawn
			&& ResolvedSpawnLocations.ContainsByPredicate(
				[&Point](const FVector& Existing)
				{
					return FVector::DistSquared2D(Existing, Point.WorldLocation) < FMath::Square(250.0f);
				});
		if (!IsBlocked(Point.WorldLocation) && !bSpawnTooClose)
		{
			if (Point.Type == ELevelDesignPointType::Spawn)
				ResolvedSpawnLocations.Add(Point.WorldLocation);
			continue;
		}

		const FVector CellCenter(
			Point.GridCell.X * DesignCellSize,
			Point.GridCell.Y * DesignCellSize,
			120.0f + GetSurfaceElevationForCell(Point.GridCell));
		bool bFound = false;
		for (const FVector& Offset : CandidateOffsets)
		{
			const FVector Candidate = CellCenter + Offset;
			if (IsBlocked(Candidate))
				continue;
			if (Point.Type == ELevelDesignPointType::Spawn
				&& ResolvedSpawnLocations.ContainsByPredicate(
					[&Candidate](const FVector& Existing)
					{
						return FVector::DistSquared2D(Existing, Candidate) < FMath::Square(250.0f);
					}))
			{
				continue;
			}
			Point.WorldLocation = Candidate;
			if (Point.Type == ELevelDesignPointType::Spawn)
				ResolvedSpawnLocations.Add(Point.WorldLocation);
			bFound = true;
			++RelocatedCount;
			for (TActorIterator<ATargetPoint> It(GetWorld()); It; ++It)
			{
				if (It->Tags.Contains(Point.PointId))
				{
					It->SetActorLocation(Point.WorldLocation, false, nullptr, ETeleportType::TeleportPhysics);
					break;
				}
			}
			break;
		}
		if (!bFound)
		{
			++UnresolvedCount;
			if (UnresolvedIds.Num() < 8)
				UnresolvedIds.Add(Point.PointId.ToString());
		}
	}

	bResolvedGameplayPointSafety = true;
	RebuildGameplayPointHash();
	UE_LOG(LogTemp, Display,
		TEXT("Gameplay point safety resolution: relocated=%d unresolved=%d final_point_hash=%08X pass=%s sample=[%s]"),
		RelocatedCount,
		UnresolvedCount,
		GameplayPointHash,
		UnresolvedCount == 0 ? TEXT("true") : TEXT("false"),
		*FString::Join(UnresolvedIds, TEXT(",")));
}



void AWarZoneFootprintPreview::BuildShoreTransitionMap(
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






float AWarZoneFootprintPreview::GetSurfaceElevationForCell(const FIntPoint& Cell) const
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

void AWarZoneFootprintPreview::GetFacilityAccessEdges(
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

bool AWarZoneFootprintPreview::AreAllFacilityLevelsLoaded() const
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

FVector AWarZoneFootprintPreview::GetDesignFootprintCenter(
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

