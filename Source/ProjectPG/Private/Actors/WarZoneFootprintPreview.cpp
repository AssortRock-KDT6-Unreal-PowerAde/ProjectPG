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

	FloorProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FloorProxy"));
	BackWallProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BackWallProxy"));
	LeftWallProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftWallProxy"));
	RightWallProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightWallProxy"));
	FrontWallLeftProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontWallLeftProxy"));
	FrontWallRightProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FrontWallRightProxy"));
	RoofProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RoofProxy"));
	YardFloorProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardFloorProxy"));
	YardCoverNorthProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverNorthProxy"));
	YardCoverSouthProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverSouthProxy"));
	YardCoverWestProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverWestProxy"));
	YardCoverEastProxy = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("YardCoverEastProxy"));
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
	UStaticMeshComponent* ProxyComponents[] = {
		FloorProxy, BackWallProxy, LeftWallProxy, RightWallProxy,
		FrontWallLeftProxy, FrontWallRightProxy, RoofProxy,
		YardFloorProxy, YardCoverNorthProxy, YardCoverSouthProxy,
		YardCoverWestProxy, YardCoverEastProxy
	};

	for (UStaticMeshComponent* Component : ProxyComponents)
	{
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(CubeMesh.Object);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetGenerateOverlapEvents(false);
		Component->SetVisibility(false);
		Component->SetMobility(EComponentMobility::Movable);
	}

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
	bool bAnyGameWorldRunning = false;
	if (IsValid(GEngine))
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			if (Context.WorldType == EWorldType::PIE || Context.WorldType == EWorldType::Game)
			{
				bAnyGameWorldRunning = true;
				break;
			}
		}
	}
	if (!bAnyGameWorldRunning && IsValid(GetWorld()) && !GetWorld()->IsGameWorld())
	{
		DrawReservation();
		DrawDesignScalePreview();
	}

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
			RefreshNavigationBlockerRegion(PlayerNavigationBlockers, PlayerLocation, 6000.0f);
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
	if (!bUseRuntimeBlueprintTiles)
	{
		ShowWarehouseProxy(GetFootprintCenter(FacilityPlacements[0]));
		ShowYardProxy(GetFootprintCenter(FacilityPlacements[1]));
	}
	else
	{
		UStaticMeshComponent* ProxyComponents[] = {
			FloorProxy, BackWallProxy, LeftWallProxy, RightWallProxy,
			FrontWallLeftProxy, FrontWallRightProxy, RoofProxy,
			YardFloorProxy, YardCoverNorthProxy, YardCoverSouthProxy,
			YardCoverWestProxy, YardCoverEastProxy
		};
		for (UStaticMeshComponent* Component : ProxyComponents)
		{
			if (IsValid(Component))
				Component->SetVisibility(false, true);
		}
	}
	BuildLightweightWorldVisuals();
	SpawnRuntimeBlueprintTiles();
	BuildGameplayPointMarkers();
	// Runs alongside runtime Blueprint tiles: their HISM dressing covers per-tile
	// props, while this pass scatters grass/shrub clumps across meadow, scrub,
	// ruin and open-ground cells to break up the 20m tile seams between them.
	BuildPCGDressingGraph();
	NavigationValidationStartTimeSeconds = FPlatformTime::Seconds();
	FacilityDesignLevelInstances.Reset();
	FacilityLoadRequestTimeSeconds.Reset();
	LoggedFacilityDesignLevelIndices.Reset();
	for (int32 FacilityIndex = 0; FacilityIndex < FacilityPlacements.Num(); ++FacilityIndex)
		LoadFacilityDesignLevel(FacilityPlacements[FacilityIndex], FacilityIndex);
	BuildBorderMountains();

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

void AWarZoneFootprintPreview::BuildPCGDressingGraph()
{
	if (!IsValid(DressingPCGComponent))
		return;

	static const TSoftObjectPtr<UStaticMesh> GrassMeshes[] = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarA.SM_vlkhcbxia_VarA"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarC.SM_vlkhcbxia_VarC"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarF.SM_vlkhcbxia_VarF")))
	};
	// Execution-plan requirement: mix in at least two shrub species so grass-area
	// tiles (Meadow/Scrub) don't read as a single mesh stamped on a grid.
	static const TSoftObjectPtr<UStaticMesh> ShrubMeshes[] = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Shrubs_1.Shrubs_1"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2")))
	};

	RuntimeDressingGraph = NewObject<UPCGGraph>(DressingPCGComponent, TEXT("RuntimeDressingGraph"), RF_Transient);
	UPCGCreatePointsSettings* CreatePointsSettings = nullptr;
	UPCGStaticMeshSpawnerSettings* SpawnerSettings = nullptr;
	UPCGNode* CreatePointsNode = RuntimeDressingGraph->AddNodeOfType<UPCGCreatePointsSettings>(CreatePointsSettings);
	UPCGNode* SpawnerNode = RuntimeDressingGraph->AddNodeOfType<UPCGStaticMeshSpawnerSettings>(SpawnerSettings);
	if (!IsValid(CreatePointsNode) || !IsValid(SpawnerNode) || !IsValid(CreatePointsSettings) || !IsValid(SpawnerSettings))
		return;

	CreatePointsSettings->CoordinateSpace = EPCGCoordinateSpace::World;
	CreatePointsSettings->bCullPointsOutsideVolume = false;
	FRandomStream RandomStream(DressingPCGComponent->Seed);
	int32 CandidateCellCount = 0;
	int32 MeadowCellCount = 0;
	int32 ScrubCellCount = 0;
	// Facility footprints get custom-raised/lowered terrain pads and access ramps
	// that this pass has no visibility into (TileDesignPlacements only carries the
	// flat pre-elevation Z). A clump anchored one cell outside a facility's
	// reserved footprint can end up floating over or sinking into that pad, so
	// skip a one-cell buffer around every facility instead of guessing its height.
	auto IsNearFacilityFootprint = [this](const FIntPoint& Cell)
	{
		for (const FFacilityPlacement& Facility : FacilityPlacements)
		{
			if (Cell.X >= Facility.AnchorCell.X - 1
				&& Cell.X <= Facility.AnchorCell.X + Facility.Footprint.X
				&& Cell.Y >= Facility.AnchorCell.Y - 1
				&& Cell.Y <= Facility.AnchorCell.Y + Facility.Footprint.Y)
			{
				return true;
			}
		}
		return false;
	};
	int32 FacilityAdjacentSkipCount = 0;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (IsNearFacilityFootprint(Placement.GridCell))
		{
			++FacilityAdjacentSkipCount;
			continue;
		}

		// Grass-area visuals only. Forest/rocky/ambush/service-camp tiles author
		// their own HISM dressing in ATacticalTileActor; this pass fills the open
		// meadow, scrub, ruin and clearing tiles that otherwise read as bare,
		// repeated ground between them. Cell divisor controls how many of the
		// eligible cells get a clump at all; clump count controls how many
		// clumps land per chosen cell -- together these approximate the
		// "Meadow reads full, Scrub is patchier" density spread from the level
		// design plan without duplicating the WarZone band-resolution logic
		// that SpawnRuntimeBlueprintTiles uses for its own HISM density scale.
		int32 CellDivisor;
		int32 ClumpCount;
		const bool bIsMeadow = Placement.Visual == ETileDesignVisual::NatureMeadow;
		const bool bIsScrub = Placement.Visual == ETileDesignVisual::NatureScrub;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::NatureMeadow: CellDivisor = 4; ClumpCount = 2; break;
		case ETileDesignVisual::NatureScrub: CellDivisor = 7; ClumpCount = 1; break;
		case ETileDesignVisual::Ruins: CellDivisor = 13; ClumpCount = 2; break;
		case ETileDesignVisual::OpenGround: CellDivisor = 13; ClumpCount = 1; break;
		default: continue;
		}

		const uint32 CellHash = HashCombine(GetTypeHash(Placement.GridCell.X), GetTypeHash(Placement.GridCell.Y));
		if (CellHash % CellDivisor != 0)
			continue;

		++CandidateCellCount;
		if (bIsMeadow) ++MeadowCellCount;
		if (bIsScrub) ++ScrubCellCount;
		for (int32 ClumpIndex = 0; ClumpIndex < ClumpCount; ++ClumpIndex)
		{
			const FVector Location = Placement.WorldLocation + FVector(
				RandomStream.FRandRange(-750.0f, 750.0f),
				RandomStream.FRandRange(-750.0f, 750.0f),
				5.0f);
			const float UniformScale = RandomStream.FRandRange(0.65f, 1.2f);
			const FTransform Transform(
				FRotator(0.0f, RandomStream.FRandRange(0.0f, 360.0f), 0.0f),
				Location,
				FVector(UniformScale));
			FPCGPoint& Point = CreatePointsSettings->PointsToCreate.Emplace_GetRef(
				Transform, 1.0f, RandomStream.RandHelper(MAX_int32));
			Point.SetExtents(FVector(30.0f, 30.0f, 60.0f));
		}
	}

	UPCGMeshSelectorWeighted* MeshSelector = Cast<UPCGMeshSelectorWeighted>(SpawnerSettings->MeshSelectorParameters);
	if (!IsValid(MeshSelector))
		return;
	for (const TSoftObjectPtr<UStaticMesh>& GrassMesh : GrassMeshes)
	{
		FPCGMeshSelectorWeightedEntry& Entry = MeshSelector->MeshEntries.Emplace_GetRef(GrassMesh, 3);
		Entry.Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Entry.Descriptor.bCanEverAffectNavigation = false;
		Entry.Descriptor.InstanceStartCullDistance = 5000;
		Entry.Descriptor.InstanceEndCullDistance = 18000;
		Entry.Descriptor.ComponentTags.Add(TEXT("PCG_Dressing"));
	}
	// Lower relative weight than grass (2 x weight-1 vs 3 x weight-3) so shrubs
	// read as an occasional accent rather than half the ground cover.
	for (const TSoftObjectPtr<UStaticMesh>& ShrubMesh : ShrubMeshes)
	{
		FPCGMeshSelectorWeightedEntry& Entry = MeshSelector->MeshEntries.Emplace_GetRef(ShrubMesh, 1);
		Entry.Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Entry.Descriptor.bCanEverAffectNavigation = false;
		Entry.Descriptor.InstanceStartCullDistance = 6000;
		Entry.Descriptor.InstanceEndCullDistance = 22000;
		Entry.Descriptor.ComponentTags.Add(TEXT("PCG_Dressing"));
	}

	RuntimeDressingGraph->AddLabeledEdge(
		CreatePointsNode,
		PCGPinConstants::DefaultOutputLabel,
		SpawnerNode,
		PCGPinConstants::DefaultInputLabel);
	RuntimeDressingGraph->AddLabeledEdge(
		SpawnerNode,
		PCGPinConstants::DefaultOutputLabel,
		RuntimeDressingGraph->GetOutputNode(),
		PCGPinConstants::DefaultOutputLabel);

	DressingPCGComponent->SetGraph(RuntimeDressingGraph);
	DressingPCGComponent->GenerateLocal(true);
	UE_LOG(LogTemp, Display,
		TEXT("PCG dressing requested: seed=%d candidate_cells=%d (meadow=%d scrub=%d) facility_skipped=%d points=%d meshes=%d collision=false navigation=false"),
		DressingPCGComponent->Seed,
		CandidateCellCount,
		MeadowCellCount,
		ScrubCellCount,
		FacilityAdjacentSkipCount,
		CreatePointsSettings->PointsToCreate.Num(),
		UE_ARRAY_COUNT(GrassMeshes) + UE_ARRAY_COUNT(ShrubMeshes));
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

void AWarZoneFootprintPreview::BuildLightweightWorldVisuals()
{
	// Use the engine's batch path so navigation bounds are cached only after the
	// complete HISM instance set exists. Individual AddInstance calls can expose
	// an intermediate invalid bound to the dynamic navigation system.
	RoadSurfaceHISM->SetCanEverAffectNavigation(false);
	GroundHISM->ClearInstances();
	WarZoneGroundHISM->ClearInstances();
	TransitionGroundHISM->ClearInstances();
	RoadSurfaceHISM->ClearInstances();
	LakeBedHISM->ClearInstances();
	LakeWaterHISM->ClearInstances();

	auto MakeCubeTransform = [](UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& WorldLocation, const FVector& WorldSize)
	{
		return FTransform(FRotator::ZeroRotator, WorldLocation, WorldSize / 100.0f)
			.GetRelativeTransform(Component->GetComponentTransform());
	};
	auto MakeOrientedCubeTransform = [](UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& WorldLocation, const FRotator& WorldRotation, const FVector& WorldSize)
	{
		return FTransform(WorldRotation, WorldLocation, WorldSize / 100.0f)
			.GetRelativeTransform(Component->GetComponentTransform());
	};
	TArray<FTransform> GroundTransforms;
	TArray<FTransform> WarZoneGroundTransforms;
	TArray<FTransform> TransitionGroundTransforms;
	TArray<FTransform> RoadTransforms;
	TArray<FTransform> LakeBedTransforms;
	// The water is one sheet over the bounding box of every lake and shore cell,
	// not a sheet per cell. Per-cell sheets met edge to edge, and the material's
	// ripple normals restarted at every cell border - a visible 20 m grid drawn on
	// the water. Where the box overlaps dry land the sheet runs inside the solid
	// ground slabs, so only the lake part of it ever renders.
	TArray<FTransform> LakeWaterTransforms;
	FIntPoint LakeSheetMin(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint LakeSheetMax(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	int32 LakeWaterCellCount = 0;
	GroundTransforms.Reserve(2025);
	WarZoneGroundTransforms.Reserve(256);
	TransitionGroundTransforms.Reserve(512);
	RoadTransforms.Reserve(256);
	TSet<FIntPoint> FacilityCells;
	// One shared underside for every ground slab and facility pad. Each slab used to
	// pick its own bottom just under its own top, so a pad sunk to -70 cm left an
	// open slot between its floor and the -10 cm underside of the neighbouring cell:
	// the pit wall had a gap the player could see and clip through. Taking the
	// deepest authored floor once makes the whole terrain one solid body.
	float TerrainSolidBottomZ = -10.0f;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		for (const FIntPoint& Cell : FacilityPlacement.OccupiedCells)
			FacilityCells.Add(Cell);
		const float FacilitySurfaceZ = FacilityPlacement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : FacilityPlacement.BaseElevationCm;
		TerrainSolidBottomZ = FMath::Min(TerrainSolidBottomZ, FacilitySurfaceZ - 60.0f);
	}
	// The lake floor is the deepest thing in the world, so the slabs that form its
	// banks have to reach past it. Taller boxes cost no extra instances.
	TerrainSolidBottomZ = FMath::Min(TerrainSolidBottomZ, LakeBedZ - 40.0f);

	// Cells that carry a generated shore mesh get no flat slab: the mesh is the
	// ground there, and a slab at Z=20 would cut straight through its beach.
	TSet<FIntPoint> LakeCells;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Water)
			LakeCells.Add(Placement.GridCell);
	TMap<FIntPoint, TPair<int32, int32>> ShoreTileByCell;
	if (LakeCells.Num() > 0)
		BuildShoreTransitionMap(LakeCells, ShoreTileByCell);

	int32 RoadTileCount = 0;
	FIntPoint VisualWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			VisualWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		// Every 20x20 m cell shares one ground renderer. Its upper face is Z=20,
		// matching ATacticalTileActor's authored prop/foliage datum. Exact cell
		// dimensions keep adjacent top faces edge-to-edge without coplanar overlap.
		const int32 WarZoneDeltaX = Placement.GridCell.X - VisualWarZoneCenter.X;
		const int32 WarZoneDeltaY = Placement.GridCell.Y - VisualWarZoneCenter.Y;
		const int32 WarZoneDistanceSquared = WarZoneDeltaX * WarZoneDeltaX + WarZoneDeltaY * WarZoneDeltaY;
		const bool bLogicalWarZoneCell = Placement.Visual == ETileDesignVisual::WarZoneGround;
		const bool bIndustrialCoreGround = bLogicalWarZoneCell && WarZoneDistanceSquared <= 64;
		// Matches the WarZone_Mid tile band in SpawnRuntimeBlueprintTiles, which runs to
		// d^2 <= 225 off the same centre cell. The ground stopped at 144, so cells
		// between radius 12 and 15 received industrial container/factory tiles while
		// standing on green nature ground - the WarZone visibly broke apart before
		// reaching its own edge. Beyond 225 the nature ground is intentional: that is
		// the WarZone_Outer natural buffer band.
		const bool bTransitionGround = bLogicalWarZoneCell
			&& WarZoneDistanceSquared > 64
			&& WarZoneDistanceSquared <= 225;
		UHierarchicalInstancedStaticMeshComponent* GroundComponent = bIndustrialCoreGround
			? WarZoneGroundHISM
			: (bTransitionGround ? TransitionGroundHISM : GroundHISM);
		TArray<FTransform>& TargetGroundTransforms = bIndustrialCoreGround
			? WarZoneGroundTransforms
			: (bTransitionGround ? TransitionGroundTransforms : GroundTransforms);
		// A land cell whose four corners are all wet is submerged: it belongs to the
		// lake floor, not to the walking datum. Without this it fell between both
		// paths and left a flat slab stranded at Z=20 amid the shoreline.
		const TPair<int32, int32>* ShoreEntry = ShoreTileByCell.Find(Placement.GridCell);
		const bool bSubmergedCell = ShoreEntry != nullptr
			&& ShoreEntry->Key == SubmergedShoreVariant;
		if (Placement.Visual == ETileDesignVisual::Water || bSubmergedCell)
		{
			// A sunken bed plus the shared water sheet, in place of the flat cell slab.
			// The bank between LakeSurfaceZ and the neighbouring ground top is what
			// makes the lake impassable, so nothing here reaches the shared datum.
			// Solid from the lake floor down to the shared underside. The height used
			// to be Max(20, LakeBedZ - TerrainSolidBottomZ), and with the bed below
			// the underside that expression is negative - it clamped to a 20 cm sheet
			// hanging in the water with a see-through band above it.
			LakeBedTransforms.Add(MakeCubeTransform(
				LakeBedHISM,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (LakeBedZ + TerrainSolidBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize,
					FMath::Max(40.0f, LakeBedZ - TerrainSolidBottomZ))));
			++LakeWaterCellCount;
			LakeSheetMin = FIntPoint(
				FMath::Min(LakeSheetMin.X, Placement.GridCell.X),
				FMath::Min(LakeSheetMin.Y, Placement.GridCell.Y));
			LakeSheetMax = FIntPoint(
				FMath::Max(LakeSheetMax.X, Placement.GridCell.X),
				FMath::Max(LakeSheetMax.Y, Placement.GridCell.Y));
			continue;
		}

		if (ShoreEntry != nullptr)
		{
			// The shore mesh is now only the beach surface; the solid below it is
			// this bed slab, same as a water cell's. Its top meets the beach exactly
			// at the wet corners and sits under it everywhere else.
			LakeBedTransforms.Add(MakeCubeTransform(
				LakeBedHISM,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (LakeBedZ + TerrainSolidBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize,
					FMath::Max(40.0f, LakeBedZ - TerrainSolidBottomZ))));
			// The beach dips under the waterline near its wet corners, so the shared
			// sheet must span shore cells too - without this every beach ended in a
			// dry olive basin, an empty pool beside the lake.
			LakeSheetMin = FIntPoint(
				FMath::Min(LakeSheetMin.X, Placement.GridCell.X),
				FMath::Min(LakeSheetMin.Y, Placement.GridCell.Y));
			LakeSheetMax = FIntPoint(
				FMath::Max(LakeSheetMax.X, Placement.GridCell.X),
				FMath::Max(LakeSheetMax.Y, Placement.GridCell.Y));
		}

		const float AuthoredElevation = GetSurfaceElevationForCell(Placement.GridCell);
		const float SurfaceZ = AuthoredElevation;
		const float GroundBottomZ = TerrainSolidBottomZ;
		if (!FacilityCells.Contains(Placement.GridCell)
			&& !ShoreTileByCell.Contains(Placement.GridCell))
		{
			TargetGroundTransforms.Add(MakeCubeTransform(
				GroundComponent,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (SurfaceZ + GroundBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize, SurfaceZ - GroundBottomZ)));
		}

		const bool bHasRoadSurface =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle;
		if (!bHasRoadSurface)
			continue;

		++RoadTileCount;
		// The slab used to be 2 cm thick sitting 1 cm above the shared terrain, which
		// put its underside exactly coplanar with the ground top and its walking face
		// only two centimetres clear. Up close the depth buffer separates that fine;
		// across a 900 m map it cannot, and the pair shimmered - the coplanar audit
		// found 174 overlapping cell pairs at a 0.00 cm gap. Lift the walking face to
		// a curb-like 10 cm, well under the 45 cm step height, and bury the underside
		// 20 cm inside the ground cube where it can never be coplanar with anything.
		const FVector Center = Placement.WorldLocation
			+ FVector(0.0f, 0.0f, SurfaceZ + RoadSurfaceLiftCm - RoadSurfaceThicknessCm * 0.5f);
		RoadTransforms.Add(MakeCubeTransform(
			RoadSurfaceHISM, Center, FVector(600.0f, 600.0f, RoadSurfaceThicknessCm)));
		auto AddRoadArm = [this, &Placement, &RoadTransforms, &MakeOrientedCubeTransform](
			const FIntPoint& Direction, uint8 ConnectionBit)
		{
			if ((Placement.ConnectionMask & ConnectionBit) == 0)
				return;
			const float ThisSurfaceZ = GetSurfaceElevationForCell(Placement.GridCell);
			const float NeighbourSurfaceZ = GetSurfaceElevationForCell(Placement.GridCell + Direction);
			const FVector Direction3D(static_cast<float>(Direction.X), static_cast<float>(Direction.Y), 0.0f);
			// Arms share the centre patch's datum so the whole road surface stays one
			// continuous plane at the lifted height.
			const float ArmCenterOffsetZ = RoadSurfaceLiftCm - RoadSurfaceThicknessCm * 0.5f;
			const FVector Start = Placement.WorldLocation + Direction3D * 300.0f
				+ FVector(0.0f, 0.0f, ThisSurfaceZ + ArmCenterOffsetZ);
			const FVector End = Placement.WorldLocation + Direction3D * 1000.0f
				+ FVector(0.0f, 0.0f, (ThisSurfaceZ + NeighbourSurfaceZ) * 0.5f + ArmCenterOffsetZ);
			const FVector Delta = End - Start;
			RoadTransforms.Add(MakeOrientedCubeTransform(
				RoadSurfaceHISM,
				(Start + End) * 0.5f,
				Delta.Rotation(),
				FVector(Delta.Size() + 2.0f, 600.0f, RoadSurfaceThicknessCm)));
		};
		AddRoadArm(FIntPoint(0, 1), NorthConnection);
		AddRoadArm(FIntPoint(1, 0), EastConnection);
		AddRoadArm(FIntPoint(0, -1), SouthConnection);
		AddRoadArm(FIntPoint(-1, 0), WestConnection);
	}

	// Each multi-cell facility owns one continuous terrain tile matching its exact
	// 2x1, 2x2, 3x3 (or rotated) footprint. This removes internal seams and gives
	// the building, props, collision and access pieces one authoritative top Z.
	int32 RaisedPadCount = 0;
	int32 LoweredPadCount = 0;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.OccupiedCells.IsEmpty())
			continue;
		// A facility with its own sculpted ground gets no flat pad: the pad would
		// slice straight through the terrain mesh at the datum height, cutting off
		// everything below it - which for the rural diorama is the shoreline, the
		// water and both boats.
		if (FacilityBringsOwnTerrain(FacilityPlacement.VisualSet))
			continue;
		int32 MinX = MAX_int32;
		int32 MinY = MAX_int32;
		int32 MaxX = MIN_int32;
		int32 MaxY = MIN_int32;
		for (const FIntPoint& Cell : FacilityPlacement.OccupiedCells)
		{
			MinX = FMath::Min(MinX, Cell.X);
			MinY = FMath::Min(MinY, Cell.Y);
			MaxX = FMath::Max(MaxX, Cell.X);
			MaxY = FMath::Max(MaxY, Cell.Y);
		}
		const float SurfaceZ = FacilityPlacement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : FacilityPlacement.BaseElevationCm;
		const float GroundBottomZ = TerrainSolidBottomZ;
		const FVector PadCenter(
			(MinX + MaxX) * DesignCellSize * 0.5f,
			(MinY + MaxY) * DesignCellSize * 0.5f,
			(SurfaceZ + GroundBottomZ) * 0.5f);
		const FVector PadSize(
			(MaxX - MinX + 1) * DesignCellSize,
			(MaxY - MinY + 1) * DesignCellSize,
			SurfaceZ - GroundBottomZ);
		UHierarchicalInstancedStaticMeshComponent* FacilityGroundComponent =
			FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			|| FacilityPlacement.VisualSet == EFacilityVisualSet::DowntownBlock
			|| FacilityPlacement.VisualSet == EFacilityVisualSet::FactoryConstruction
				? WarZoneGroundHISM : GroundHISM;
		TArray<FTransform>& FacilityGroundTransforms = FacilityGroundComponent == WarZoneGroundHISM
			? WarZoneGroundTransforms : GroundTransforms;
		FacilityGroundTransforms.Add(MakeCubeTransform(
			FacilityGroundComponent, PadCenter, PadSize));
		if (SurfaceZ > 21.0f)
			++RaisedPadCount;
		else if (SurfaceZ < 19.0f)
			++LoweredPadCount;
	}
	UE_LOG(LogTemp, Display,
		TEXT("Facility terrain pads: total=%d raised=%d lowered=%d continuous_rectangles=true"),
		FacilityPlacements.Num(), RaisedPadCount, LoweredPadCount);

	// Shoreline dressing. Water membership is a per-cell decision, so the waterline
	// steps along the 20 m grid. River stones and reeds standing in the shallows
	// break that line up.
	//
	// A tilted bank slab was tried here first and removed: a rotated box 27 m long
	// lifts its far end more than 3 m clear of a ground plane at Z=20, so instead of
	// a shore it produced planes jutting out of the terrain. A real curved shoreline
	// needs sub-cell geometry - authored shore meshes in the style of
	// SM_Terrain_Mound_2x2 - not a rotated cube.
	ShoreRockHISM->ClearInstances();
	ShoreReedHISM->ClearInstances();
	TArray<FTransform> ShoreRockTransforms;
	TArray<FTransform> ShoreReedTransforms;
	{
		const AGameModePG* ShoreGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
		const int64 ShoreRaidSeed = IsValid(ShoreGameMode) ? ShoreGameMode->GetMapGenerationSeed() : 0;
		TSet<FIntPoint> WaterCells;
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
			if (Placement.Visual == ETileDesignVisual::Water)
				WaterCells.Add(Placement.GridCell);

		const FIntPoint ShoreOffsets[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		{
			if (Placement.Visual == ETileDesignVisual::Water)
				continue;

			FVector2D WaterDirection = FVector2D::ZeroVector;
			for (const FIntPoint& Offset : ShoreOffsets)
				if (WaterCells.Contains(Placement.GridCell + Offset))
					WaterDirection += FVector2D(Offset.X, Offset.Y);
			if (WaterDirection.IsNearlyZero())
				continue;
			WaterDirection = WaterDirection.GetSafeNormal();

			const uint32 ShoreHash = HashCombine(
				GetTypeHash(ShoreRaidSeed),
				HashCombine(GetTypeHash(Placement.GridCell.X * 31),
					GetTypeHash(Placement.GridCell.Y * 17)));
			FRandomStream ShoreStream(static_cast<int32>(ShoreHash));

			for (int32 RockIndex = 0; RockIndex < 5; ++RockIndex)
			{
				const FVector2D Along(-WaterDirection.Y, WaterDirection.X);
				const FVector Location = Placement.WorldLocation
					+ FVector(WaterDirection.X, WaterDirection.Y, 0.0f)
						* ShoreStream.FRandRange(700.0f, 1450.0f)
					+ FVector(Along.X, Along.Y, 0.0f) * ShoreStream.FRandRange(-950.0f, 950.0f)
					+ FVector(0.0f, 0.0f, LakeSurfaceZ - ShoreStream.FRandRange(10.0f, 70.0f));
				ShoreRockTransforms.Add(FTransform(
					FRotator(0.0f, ShoreStream.FRandRange(0.0f, 360.0f), 0.0f),
					Location,
					FVector(ShoreStream.FRandRange(0.8f, 2.1f)))
					.GetRelativeTransform(ShoreRockHISM->GetComponentTransform()));
			}
			for (int32 ReedIndex = 0; ReedIndex < 9; ++ReedIndex)
			{
				const FVector2D Along(-WaterDirection.Y, WaterDirection.X);
				const FVector Location = Placement.WorldLocation
					+ FVector(WaterDirection.X, WaterDirection.Y, 0.0f)
						* ShoreStream.FRandRange(600.0f, 1300.0f)
					+ FVector(Along.X, Along.Y, 0.0f) * ShoreStream.FRandRange(-980.0f, 980.0f)
					+ FVector(0.0f, 0.0f, LakeSurfaceZ - 20.0f);
				ShoreReedTransforms.Add(FTransform(
					FRotator(0.0f, ShoreStream.FRandRange(0.0f, 360.0f), 0.0f),
					Location,
					FVector(ShoreStream.FRandRange(0.9f, 1.6f)))
					.GetRelativeTransform(ShoreReedHISM->GetComponentTransform()));
			}
		}
	}

	// Place the shore meshes themselves. Their local origin already sits on the cell
	// centre with the land face at Z=20, so the only transform needed is the cell
	// position and the quarter turn that points the authored water side at the lake.
	int32 ShoreTransitionCount = 0;
	for (int32 VariantIndex = 0; VariantIndex < ShoreTransitionHISMs.Num(); ++VariantIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = ShoreTransitionHISMs[VariantIndex];
		if (!IsValid(Component))
			continue;
		if (Component->GetStaticMesh() == nullptr)
		{
			static const TCHAR* ShoreMeshAssetPaths[] = {
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner1.SM_Shore_Corner1"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Adjacent.SM_Shore_Corner2Adjacent"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Diagonal.SM_Shore_Corner2Diagonal"),
				TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner3.SM_Shore_Corner3")
			};
			if (ShoreTransitionHISMs.IsValidIndex(VariantIndex)
				&& VariantIndex < UE_ARRAY_COUNT(ShoreMeshAssetPaths))
			{
				if (UStaticMesh* ShoreMesh = LoadObject<UStaticMesh>(
					nullptr, ShoreMeshAssetPaths[VariantIndex]))
				{
					Component->SetStaticMesh(ShoreMesh);
					static UMaterialInterface* SharedGroundMaterial = LoadObject<UMaterialInterface>(
						nullptr,
						TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
					if (IsValid(SharedGroundMaterial))
						Component->SetMaterial(0, SharedGroundMaterial);
				}
			}
		}
		if (Component->GetStaticMesh() == nullptr)
		{
			// Nothing to place. Say so once rather than leaving a hole where the
			// suppressed flat slabs used to be - run PG.BuildShoreMeshes.
			UE_LOG(LogTemp, Warning,
				TEXT("Shore mesh missing for variant %d - run PG.BuildShoreMeshes"), VariantIndex);
			continue;
		}
		Component->ClearInstances();
		TArray<FTransform> ShoreTransforms;
		for (const TPair<FIntPoint, TPair<int32, int32>>& Entry : ShoreTileByCell)
		{
			if (Entry.Value.Key != VariantIndex)
				continue;
			const FVector ShoreLocation(
				Entry.Key.X * DesignCellSize, Entry.Key.Y * DesignCellSize, 0.0f);
			ShoreTransforms.Add(FTransform(
				FRotator(0.0f, Entry.Value.Value * 90.0f, 0.0f),
				ShoreLocation,
				FVector::OneVector).GetRelativeTransform(Component->GetComponentTransform()));
		}
		Component->AddInstances(ShoreTransforms, false, false, true);
		Component->BuildTreeIfOutdated(false, true);
		ShoreTransitionCount += ShoreTransforms.Num();
	}

	GroundHISM->AddInstances(GroundTransforms, false, false, true);
	ShoreRockHISM->AddInstances(ShoreRockTransforms, false, false, true);
	ShoreReedHISM->AddInstances(ShoreReedTransforms, false, false, true);
	WarZoneGroundHISM->AddInstances(WarZoneGroundTransforms, false, false, true);
	TransitionGroundHISM->AddInstances(TransitionGroundTransforms, false, false, true);
	RoadSurfaceHISM->AddInstances(RoadTransforms, false, false, false);
	if (LakeSheetMax.X >= LakeSheetMin.X && LakeSheetMax.Y >= LakeSheetMin.Y)
	{
		LakeWaterTransforms.Add(MakeCubeTransform(
			LakeWaterHISM,
			FVector(
				(LakeSheetMin.X + LakeSheetMax.X) * 0.5f * DesignCellSize,
				(LakeSheetMin.Y + LakeSheetMax.Y) * 0.5f * DesignCellSize,
				LakeSurfaceZ - 5.0f),
			FVector(
				(LakeSheetMax.X - LakeSheetMin.X + 1) * DesignCellSize,
				(LakeSheetMax.Y - LakeSheetMin.Y + 1) * DesignCellSize,
				10.0f)));
	}
	LakeBedHISM->AddInstances(LakeBedTransforms, false, false, true);
	LakeWaterHISM->AddInstances(LakeWaterTransforms, false, false, false);

	GroundHISM->BuildTreeIfOutdated(false, true);
	WarZoneGroundHISM->BuildTreeIfOutdated(false, true);
	TransitionGroundHISM->BuildTreeIfOutdated(false, true);
	RoadSurfaceHISM->BuildTreeIfOutdated(false, true);
	LakeBedHISM->BuildTreeIfOutdated(false, true);
	LakeWaterHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border lake: cells=%d shore_cells=%d shore_meshes=%d shore_rocks=%d shore_reeds=%d ")
		TEXT("surface_z=%.0f bed_z=%.0f bank_drop_cm=%.0f"),
		LakeWaterCellCount, ShoreRockTransforms.Num() / 5, ShoreTransitionCount,
		ShoreRockTransforms.Num(), ShoreReedTransforms.Num(),
		LakeSurfaceZ, LakeBedZ, BaseGroundSurfaceZ - LakeSurfaceZ);
	GroundHISM->RecreatePhysicsState();
	WarZoneGroundHISM->RecreatePhysicsState();
	TransitionGroundHISM->RecreatePhysicsState();
	RoadSurfaceHISM->RecreatePhysicsState();

	UE_LOG(LogTemp, Display,
		TEXT("HISM design world: ground_instances=%d warzone_ground_instances=%d transition_ground_instances=%d road_tiles=%d road_surface_instances=%d collision=true facility_support_cells=%d"),
		GroundHISM->GetInstanceCount(), WarZoneGroundHISM->GetInstanceCount(), TransitionGroundHISM->GetInstanceCount(), RoadTileCount, RoadSurfaceHISM->GetInstanceCount(),
		2025 - TileDesignPlacements.Num());
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

void AWarZoneFootprintPreview::BuildElevatedFacilityTerrain()
{
	for (UStaticMeshComponent* Component : ElevatedTerrainComponents)
	{
		if (IsValid(Component))
			Component->DestroyComponent();
	}
	ElevatedTerrainComponents.Reset();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	UMaterialInterface* RampMaterial = LoadObject<UMaterialInterface>(
		nullptr, TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean.MI_RuntimeRoad_AsphaltClean"));
	if (!IsValid(Cube))
	{
		UE_LOG(LogTemp, Error, TEXT("Elevated terrain: engine cube missing"));
		return;
	}

	auto AddTerrainComponent = [this, Cube](
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Size,
		UMaterialInterface* Material,
		const FName& TerrainRole)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->ComponentTags.AddUnique(TEXT("ElevatedFacilityTerrain"));
		Component->ComponentTags.AddUnique(TerrainRole);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(Cube);
		Component->SetRelativeLocation(Location);
		Component->SetRelativeRotation(Rotation);
		Component->SetRelativeScale3D(Size / 100.0f);
		if (IsValid(Material))
			Component->SetMaterial(0, Material);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(true);
		AddInstanceComponent(Component);
		Component->RegisterComponent();
		ElevatedTerrainComponents.Add(Component);
		return Component;
	};

	int32 RaisedFacilityCount = 0;
	int32 LoweredFacilityCount = 0;
	int32 RampCount = 0;
	int32 StairStepCount = 0;
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		const float FacilitySurfaceZ = Placement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : Placement.BaseElevationCm;
		if (FMath::IsNearlyEqual(FacilitySurfaceZ, BaseGroundSurfaceZ, 1.0f)
			|| Placement.OccupiedCells.IsEmpty())
			continue;

		TArray<TPair<FIntPoint, FIntPoint>> AccessEdges;
		GetFacilityAccessEdges(Placement, AccessEdges);
		if (AccessEdges.IsEmpty())
			continue;

		const float RampRun = Placement.ElevationProfile == EFacilityElevationProfile::WarZoneStronghold
			? 1800.0f : (Placement.ElevationProfile == EFacilityElevationProfile::RaisedCompound
				? 1400.0f : 1000.0f);
		const float RampWidth = Placement.ElevationProfile == EFacilityElevationProfile::WarZoneStronghold
			? 900.0f : 700.0f;

		for (const TPair<FIntPoint, FIntPoint>& AccessEdge : AccessEdges)
		{
			const FIntPoint& EntranceCell = AccessEdge.Key;
			const FIntPoint& EntranceDirection = AccessEdge.Value;
			const FVector2D Outward(
				static_cast<float>(EntranceDirection.X),
				static_cast<float>(EntranceDirection.Y));
			const FVector EdgePoint(
				EntranceCell.X * DesignCellSize + Outward.X * DesignCellSize * 0.5f,
				EntranceCell.Y * DesignCellSize + Outward.Y * DesignCellSize * 0.5f,
				0.0f);
			const FIntPoint OutsideCell = EntranceCell + EntranceDirection;
			const float OutsideSurfaceZ = GetSurfaceElevationForCell(OutsideCell);
			const FVector GroundPoint = EdgePoint + FVector(Outward.X, Outward.Y, 0.0f) * RampRun
				+ FVector(0.0f, 0.0f, OutsideSurfaceZ);
			const FVector TopPoint = EdgePoint + FVector(0.0f, 0.0f, FacilitySurfaceZ + 1.0f);
			const FVector RampDelta = TopPoint - GroundPoint;
			const FRotator RampRotation = RampDelta.Rotation();
			constexpr float RampThickness = 35.0f;
			// The slab is centred on the line from ground to pad, so its walkable face
			// used to stand half a thickness proud at both ends: an 18 cm lip to climb
			// before the ramp even began, and another 18 cm drop on to the pad at the
			// top. Sink it by that half thickness along its own up axis so the top face,
			// not the centre line, is what meets the ground and the pad.
			const FVector RampUp = RampRotation.RotateVector(FVector::UpVector);
			AddTerrainComponent(
				(GroundPoint + TopPoint) * 0.5f - RampUp * (RampThickness * 0.5f),
				RampRotation,
				FVector(RampDelta.Size(), RampWidth, RampThickness),
				RampMaterial,
				TEXT("VehicleRamp"));
			++RampCount;

			// A parallel infantry stair remains usable if the vehicle ramp is occupied.
			const FVector2D Perpendicular(-Outward.Y, Outward.X);
			const FVector StairSideOffset(Perpendicular.X * (RampWidth * 0.5f + 230.0f),
				Perpendicular.Y * (RampWidth * 0.5f + 230.0f), 0.0f);
			const int32 StepCount = FMath::Clamp(
				FMath::CeilToInt(FMath::Abs(TopPoint.Z - GroundPoint.Z) / 35.0f), 4, 12);
			const float StepRun = RampRun / StepCount;
			const FVector Inward(-Outward.X, -Outward.Y, 0.0f);
			const float StairSolidBottomZ = FMath::Min(-10.0f, FMath::Min(GroundPoint.Z, TopPoint.Z) - 50.0f);
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				const float StepTop = FMath::Lerp(
					GroundPoint.Z, TopPoint.Z, static_cast<float>(StepIndex + 1) / StepCount);
				const float StepHeight = FMath::Max(10.0f, StepTop - StairSolidBottomZ);
				const FVector StepCenter = GroundPoint + StairSideOffset
					+ Inward * (StepRun * (StepIndex + 0.5f))
					+ FVector(0.0f, 0.0f, (StairSolidBottomZ + StepTop) * 0.5f - GroundPoint.Z);
				AddTerrainComponent(
					StepCenter,
					FRotator(0.0f, Inward.Rotation().Yaw, 0.0f),
					FVector(StepRun + 4.0f, 360.0f, StepHeight),
					RampMaterial,
					TEXT("InfantryStair"));
				++StairStepCount;
			}
		}

		if (FacilitySurfaceZ > BaseGroundSurfaceZ)
			++RaisedFacilityCount;
		else
			++LoweredFacilityCount;
		UE_LOG(LogTemp, Display,
			TEXT("Elevated facility: id=%s footprint=%dx%d elevation_cm=%.0f entrance=(%d,%d) access=(%d,%d) direction=(%d,%d) access_points=%d"),
			*Placement.FacilityId.ToString(), Placement.Footprint.X, Placement.Footprint.Y,
			FacilitySurfaceZ, Placement.EntranceCell.X, Placement.EntranceCell.Y,
			Placement.AccessCell.X, Placement.AccessCell.Y,
			Placement.EntranceDirection.X, Placement.EntranceDirection.Y,
			AccessEdges.Num());
	}

	UE_LOG(LogTemp, Display,
		TEXT("Macro elevation terrain: raised_facilities=%d lowered_facilities=%d ramps=%d stair_steps=%d nav_components=%d"),
		RaisedFacilityCount, LoweredFacilityCount, RampCount, StairStepCount, ElevatedTerrainComponents.Num());
}

void AWarZoneFootprintPreview::SpawnRuntimeBlueprintTiles()
{
	if (!bUseRuntimeBlueprintTiles || !IsValid(GetWorld()))
		return;

	for (AActor* SpawnedTile : SpawnedRuntimeTiles)
	{
		if (IsValid(SpawnedTile))
			SpawnedTile->Destroy();
	}
	SpawnedRuntimeTiles.Reset();
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->DestroyComponent();
	}
	RuntimePackedVisualHISMs.Reset();
	BuildElevatedFacilityTerrain();

	// The reviewed LD_Tile maps are packed into reusable Blueprint classes by
	// PG.BuildPackedTileBlueprints. Spawning those classes keeps the authored
	// meshes/materials/collision while avoiding thousands of streamed UWorlds.
	static const TCHAR* CornerPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Corner.BPP_Tile_Road_Corner_C");
	static const TCHAR* StraightPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Straight.BPP_Tile_Road_Straight_C");
	static const TCHAR* TPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_TJunction.BPP_Tile_Road_TJunction_C");
	static const TCHAR* CrossPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Cross.BPP_Tile_Road_Cross_C");
	static const TCHAR* DeadEndPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_DeadEnd.BPP_Tile_Road_DeadEnd_C");
	static const TCHAR* SpawnPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Spawn_Staging.BPP_Tile_Spawn_Staging_C");
	static const TCHAR* ExitPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Exit_Checkpoint.BPP_Tile_Exit_Checkpoint_C");
	static const TCHAR* ObstaclePath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Obstacle_Checkpoint.BPP_Tile_Obstacle_Checkpoint_C");
	static const TCHAR* OpenPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_OpenGround.BPP_Tile_None_OpenGround_C");
	static const TCHAR* RuinsPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_Ruins.BPP_Tile_None_Ruins_C");
	static const TCHAR* YardPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Yard.BPP_Tile_WarZone_Yard_C");
	static const TCHAR* WarehousePath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Warehouse.BPP_Tile_WarZone_Warehouse_C");
	static const TCHAR* NatureMeadowPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Meadow_V2.BP_Tile_Nature_Meadow_V2_C");
	static const TCHAR* NatureForestSparsePath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestSparse.BP_Tile_Nature_ForestSparse_C");
	static const TCHAR* NatureForestDensePath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestDense.BP_Tile_Nature_ForestDense_C");
	static const TCHAR* NatureRockyPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Rocky.BP_Tile_Nature_Rocky_C");
	static const TCHAR* NatureScrubPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Scrub.BP_Tile_Nature_Scrub_C");
	static const TCHAR* NatureAmbushPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ambush.BP_Tile_Nature_Ambush_C");
	static const TCHAR* NatureServiceCampPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ServiceCamp.BP_Tile_Nature_ServiceCamp_C");
	static const TCHAR* NatureDitchPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ditch.BP_Tile_Nature_Ditch_C");
	static const TCHAR* WarZoneIndustrialOpenPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_IndustrialOpen.BP_Tile_WarZoneV2_IndustrialOpen_C");
	static const TCHAR* WarZoneContainerLanePath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_ContainerLane.BP_Tile_WarZoneV2_ContainerLane_C");
	static const TCHAR* WarZoneFactoryYardPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_FactoryYard.BP_Tile_WarZoneV2_FactoryYard_C");
	static const TCHAR* WarZoneUtilityYardPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_UtilityYard.BP_Tile_WarZoneV2_UtilityYard_C");

	FIntPoint WarZoneCoreCell = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			WarZoneCoreCell = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}

	TMap<FString, UClass*> ClassCache;
	auto ResolveClass = [&ClassCache](const TCHAR* Path)
	{
		const FString Key(Path);
		if (UClass** Existing = ClassCache.Find(Key))
			return *Existing;
		UClass* LoadedClass = LoadClass<AActor>(nullptr, Path);
		ClassCache.Add(Key, LoadedClass);
		return LoadedClass;
	};
	auto DisableCollisionOnHiddenPrimitives = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			// Packed tile Blueprints retain intentionally hidden alternate props.
			// Hidden meshes must never leave an invisible gameplay collision behind.
			if (IsValid(Component) && !Component->IsVisible())
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	};
	auto NormalizePackedBaseGround = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		static UMaterialInterface* UnifiedGround = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
		if (!IsValid(UnifiedGround))
			return;

		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			const UMaterialInterface* Material = IsValid(Component) ? Component->GetMaterial(0) : nullptr;
			if (!IsValid(Mesh) || !IsValid(Material)
				|| Mesh->GetFName() != TEXT("SM_Floor_2x2")
				|| !Material->GetPathName().Contains(TEXT("MI_RoadStraight_Ground")))
			{
				continue;
			}
			// Four 10x10 m packed slabs form the 20x20 m base. Only replace that
			// terrain material; road lanes, roofs and authored industrial floors stay.
			Component->SetMaterial(0, UnifiedGround);
		}
	};
	auto HidePerTileTerrainUnderlay = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return 0;

		int32 HiddenComponentCount = 0;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive))
				continue;

			const FName ComponentName = Primitive->GetFName();
			bool bIsTerrainUnderlay = ComponentName == TEXT("Ground_20m")
				|| ComponentName == TEXT("RoadPieces")
				|| ComponentName == TEXT("Road_6_5m")
				|| ComponentName.ToString().Contains(TEXT("RoadSurface"));
			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Primitive))
			{
				const UStaticMesh* Mesh = Instances->GetStaticMesh();
				const UMaterialInterface* Material = Instances->GetMaterial(0);
				const bool bPackedGroundMesh = IsValid(Mesh) && Mesh->GetFName() == TEXT("SM_Floor_2x2");
				const bool bGroundMaterial = IsValid(Material)
					&& (Material->GetPathName().Contains(TEXT("MI_RoadStraight_Ground"))
						|| Material->GetPathName().Contains(TEXT("MI_RuntimeGround_NatureUnified")));
				// Runtime tactical-tile actors use SM_Floor_2x2 only as their old base
				// terrain/road slab.  Facilities are handled separately and never enter
				// this lambda, so hiding every such slab is both safe and deterministic.
				bIsTerrainUnderlay |= bPackedGroundMesh || bGroundMaterial;
			}

			if (!bIsTerrainUnderlay)
				continue;

			Primitive->SetVisibility(false, true);
			Primitive->SetHiddenInGame(true);
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetCanEverAffectNavigation(false);
			++HiddenComponentCount;
		}
		return HiddenComponentCount;
	};
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> PackedComponentByKey;
	int32 PackedVisualInstanceCount = 0;
	auto PackActorVisuals = [this, &PackedComponentByKey, &PackedVisualInstanceCount](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		for (UStaticMeshComponent* Source : MeshComponents)
		{
			const UStaticMesh* Mesh = IsValid(Source) ? Source->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || !Source->IsVisible())
				continue;
			int32 StartCullDistance = 0;
			int32 EndCullDistance = 0;
			if (const UInstancedStaticMeshComponent* SourceInstances = Cast<UInstancedStaticMeshComponent>(Source))
				SourceInstances->GetCullDistances(StartCullDistance, EndCullDistance);
			// Authored Fab props are often plain StaticMeshComponents and therefore
			// arrive with infinite draw distance. Once packed into the runtime HISM,
			// apply foliage-specific culling while preserving the rare Pivot Painter
			// hero shrubs and their wind close to the player.
			const FString MeshPath = Mesh->GetPathName();
			const bool bIsFreeShrub = MeshPath.Contains(TEXT("/GV_FreeShrubsPack/"));
			const bool bIsRuntimeGrass = MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
				|| MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_"));
			const bool bIsSmallFoliage = bIsRuntimeGrass
				|| MeshPath.Contains(TEXT("/Meshes/Foliage/Bush_"))
				|| MeshPath.Contains(TEXT("/Meshes/Foliage/Shrubs_"))
				|| bIsFreeShrub;
			static UMaterialInterface* RuntimeHeroShrubMaterial = LoadObject<UMaterialInterface>(nullptr,
				TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeHeroShrub_Dark.MI_RuntimeHeroShrub_Dark"));
			auto ResolvePackedMaterial = [bIsFreeShrub](UMaterialInterface* SourceMaterial)
			{
				if (!bIsFreeShrub || !IsValid(SourceMaterial) || !IsValid(RuntimeHeroShrubMaterial))
					return SourceMaterial;
				// Preserve bark while replacing the neon, high-amplitude Pivot Painter
				// leaf material with the local dark, nearly-static gameplay variant.
				return SourceMaterial->GetPathName().Contains(TEXT("Leaf"))
					? RuntimeHeroShrubMaterial
					: SourceMaterial;
			};
			const bool bPackedEvaluateWorldPositionOffset = Source->bEvaluateWorldPositionOffset && !bIsFreeShrub;
			if (bIsRuntimeGrass)
			{
				// Dense grass remains unchanged at player distance; only far-away cells
				// stop drawing earlier. This keeps the Tarkov-like silhouette without
				// submitting tens of thousands of invisible grass cards.
				StartCullDistance = 1800;
				EndCullDistance = 6000;
			}
			else if (MeshPath.Contains(TEXT("/Meshes/Foliage/Bush_"))
				|| MeshPath.Contains(TEXT("/Meshes/Foliage/Shrubs_")))
			{
				StartCullDistance = 2500;
				EndCullDistance = 8000;
			}
			else if (MeshPath.Contains(TEXT("/Meshes/Foliage/SM_Pine_Tree_")))
			{
				StartCullDistance = 10000;
				EndCullDistance = 22000;
			}
			else if (MeshPath.Contains(TEXT("/Assets/props/prop_rocks/")))
			{
				StartCullDistance = 8000;
				EndCullDistance = 18000;
			}
			else if (EndCullDistance <= 0)
			{
				if (MeshPath.Contains(TEXT("/GV_FreeShrubsPack/")))
				{
					StartCullDistance = 4500;
					EndCullDistance = 12000;
				}
				else if (MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
					|| MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_")))
				{
					StartCullDistance = 2500;
					EndCullDistance = 8000;
				}
			}

			FString Key = FString::Printf(TEXT("%s|C%d|P%s|S%d|W%d|D%d-%d"),
				*Mesh->GetPathName(),
				static_cast<int32>(Source->GetCollisionEnabled()),
				*Source->GetCollisionProfileName().ToString(),
				Source->CastShadow ? 1 : 0,
				bPackedEvaluateWorldPositionOffset ? 1 : 0,
				StartCullDistance,
				EndCullDistance);
			for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
			{
				const UMaterialInterface* Material = ResolvePackedMaterial(Source->GetMaterial(MaterialIndex));
				Key += TEXT("|M") + (IsValid(Material) ? Material->GetPathName() : TEXT("None"));
			}

			UHierarchicalInstancedStaticMeshComponent*& Target = PackedComponentByKey.FindOrAdd(Key);
			if (!IsValid(Target))
			{
				Target = NewObject<UHierarchicalInstancedStaticMeshComponent>(
					this,
					*FString::Printf(TEXT("RuntimePackedVisual_%d"), RuntimePackedVisualHISMs.Num()));
				Target->SetupAttachment(SceneRoot);
				Target->SetMobility(EComponentMobility::Movable);
				Target->SetStaticMesh(const_cast<UStaticMesh*>(Mesh));
				Target->SetCollisionProfileName(Source->GetCollisionProfileName());
				Target->SetCollisionEnabled(Source->GetCollisionEnabled());
				Target->SetGenerateOverlapEvents(false);
				Target->SetCanEverAffectNavigation(false);
				Target->SetCastShadow(Source->CastShadow && !bIsSmallFoliage);
				Target->SetEvaluateWorldPositionOffset(bPackedEvaluateWorldPositionOffset);
				Target->SetCullDistances(StartCullDistance, EndCullDistance);
				for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
					Target->SetMaterial(MaterialIndex, ResolvePackedMaterial(Source->GetMaterial(MaterialIndex)));
				Target->RegisterComponent();
				RuntimePackedVisualHISMs.Add(Target);
			}

			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Source))
			{
				for (int32 InstanceIndex = 0; InstanceIndex < Instances->GetInstanceCount(); ++InstanceIndex)
				{
					FTransform WorldTransform;
					if (Instances->GetInstanceTransform(InstanceIndex, WorldTransform, true))
					{
						Target->AddInstance(WorldTransform, true);
						++PackedVisualInstanceCount;
					}
				}
			}
			else
			{
				Target->AddInstance(Source->GetComponentTransform(), true);
				++PackedVisualInstanceCount;
			}
		}
	};
	auto LiftCoplanarPackedRoadSurfaces = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || Mesh->GetFName() != TEXT("SM_Floor_2x2"))
				continue;
			for (int32 InstanceIndex = 0; InstanceIndex < Component->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, false)
					|| InstanceTransform.GetScale3D().Z > 0.1f)
					continue;
				// Packed road slabs started almost coplanar with the common 20 cm
				// terrain surface. Keep their centre at least 25 cm high: this leaves
				// a small, stable render separation without creating a gameplay step.
				FVector Translation = InstanceTransform.GetTranslation();
				Translation.Z = FMath::Max(Translation.Z, 25.0f);
				InstanceTransform.SetTranslation(Translation);
				Component->UpdateInstanceTransform(InstanceIndex, InstanceTransform, false, true, true);
			}
		}
	};
	auto LiftPackedExitMarking = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || Mesh->GetPathName() != TEXT("/Engine/BasicShapes/Plane.Plane"))
				continue;
			for (int32 InstanceIndex = 0; InstanceIndex < Component->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, false))
					continue;
				// The EXIT stencil is authored 2 cm above the terrain datum, which is
				// where the road surface used to sit, and a flat 8 cm lift was enough
				// to clear it. Raising the road to RoadSurfaceLiftCm put the stencil
				// back exactly on the asphalt - the audit caught 16 m2 of it at a
				// 0.00 cm gap. Derive the lift from the road height instead of a
				// constant so the two cannot drift apart again.
				constexpr float AuthoredMarkingHeightCm = 2.0f;
				constexpr float MarkingClearanceCm = 8.0f;
				const float MarkingLiftCm =
					RoadSurfaceLiftCm + MarkingClearanceCm - AuthoredMarkingHeightCm;
				InstanceTransform.AddToTranslation(FVector(0.0f, 0.0f, MarkingLiftCm));
				Component->UpdateInstanceTransform(InstanceIndex, InstanceTransform, false, true, true);
			}
		}
	};

	int32 SpawnFailures = 0;
	int32 PackedTileActorCount = 0;
	int32 WaterTileSkipCount = 0;
	TSet<FIntPoint> ShoreCellsForTileSkip;
	{
		TSet<FIntPoint> LakeCellsForTileSkip;
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
			if (Placement.Visual == ETileDesignVisual::Water)
				LakeCellsForTileSkip.Add(Placement.GridCell);
		if (LakeCellsForTileSkip.Num() > 0)
		{
			TMap<FIntPoint, TPair<int32, int32>> ShoreTiles;
			BuildShoreTransitionMap(LakeCellsForTileSkip, ShoreTiles);
			for (const TPair<FIntPoint, TPair<int32, int32>>& Entry : ShoreTiles)
				ShoreCellsForTileSkip.Add(Entry.Key);
		}
	}
	int32 HiddenTerrainUnderlayCount = 0;
	TMap<ETacticalTileKind, int32> KindCounts;
	TMap<FName, int32> WarZoneVariantCounts;
	const AGameModePG* RuntimeGameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 RuntimeRaidSeed = IsValid(RuntimeGameMode) ? RuntimeGameMode->GetMapGenerationSeed() : 0;
	auto FloorDivide = [](const int32 Value, const int32 Divisor)
	{
		const int32 Quotient = Value / Divisor;
		const int32 Remainder = Value % Divisor;
		return Remainder < 0 ? Quotient - 1 : Quotient;
	};
	auto MakeWarZoneClusterHash = [RuntimeRaidSeed, &FloorDivide](
		const int32 DeltaX,
		const int32 DeltaY,
		const int32 ClusterSize,
		const uint32 BandSalt)
	{
		// Offset by half a cluster so the central 3x3 facility and its immediate
		// apron belong to one coherent patch instead of straddling four quadrants.
		const int32 ClusterX = FloorDivide(DeltaX + ClusterSize / 2, ClusterSize);
		const int32 ClusterY = FloorDivide(DeltaY + ClusterSize / 2, ClusterSize);
		return HashCombine(
			GetTypeHash(RuntimeRaidSeed),
			HashCombine(
				GetTypeHash(ClusterX),
				HashCombine(GetTypeHash(ClusterY), BandSalt)));
	};
	auto GetWarZoneClusterSlot = [&FloorDivide](
		const int32 DeltaX,
		const int32 DeltaY,
		const int32 ClusterSize)
	{
		const int32 OffsetX = DeltaX + ClusterSize / 2;
		const int32 OffsetY = DeltaY + ClusterSize / 2;
		const int32 LocalX = OffsetX - FloorDivide(OffsetX, ClusterSize) * ClusterSize;
		const int32 LocalY = OffsetY - FloorDivide(OffsetY, ClusterSize) * ClusterSize;
		return LocalY * ClusterSize + LocalX;
	};
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		const uint32 StableHash = Placement.LocalSeed;
		// Lake cells get no tile actor at all. Their ground is 2.8 m under the water
		// surface, so a spawned tile would put its cars, containers and walls in open
		// water - which is exactly how the first pass looked.
		//
		// Shore cells are skipped for the same reason: every tile places its props
		// against a flat Z=20 surface, but a shore cell's ground is the generated
		// beach mesh sloping away underneath. The first pass left pine trees and
		// boulders standing in mid-air over the slope. The beach is meant to be open
		// anyway - the shore rock and reed scatter dresses it.
		if (Placement.Visual == ETileDesignVisual::Water
			|| ShoreCellsForTileSkip.Contains(Placement.GridCell))
		{
			++WaterTileSkipCount;
			continue;
		}

		ETacticalTileKind Kind = ETacticalTileKind::OpenGround;
		const TCHAR* ClassPath = OpenPath;
		FName WarZoneBandTag = NAME_None;
		FName WarZoneVisualTag = NAME_None;
		int32 WarZoneRotationQuarterTurns = INDEX_NONE;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::RoadStraight: Kind = ETacticalTileKind::RoadStraight; ClassPath = StraightPath; break;
		case ETileDesignVisual::RoadCorner: Kind = ETacticalTileKind::RoadCorner; ClassPath = CornerPath; break;
		case ETileDesignVisual::RoadTJunction: Kind = ETacticalTileKind::RoadTJunction; ClassPath = TPath; break;
		case ETileDesignVisual::RoadCross: Kind = ETacticalTileKind::RoadCross; ClassPath = CrossPath; break;
		case ETileDesignVisual::RoadDeadEnd: Kind = ETacticalTileKind::RoadDeadEnd; ClassPath = DeadEndPath; break;
		case ETileDesignVisual::Spawn: Kind = ETacticalTileKind::SpawnStaging; ClassPath = SpawnPath; break;
		case ETileDesignVisual::Exit: Kind = ETacticalTileKind::ExitCheckpoint; ClassPath = ExitPath; break;
		case ETileDesignVisual::Obstacle: Kind = ETacticalTileKind::ObstacleCheckpoint; ClassPath = ObstaclePath; break;
		case ETileDesignVisual::Ruins: Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; break;
		case ETileDesignVisual::NatureMeadow: Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; break;
		case ETileDesignVisual::NatureForestSparse: Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; break;
		case ETileDesignVisual::NatureForestDense: Kind = ETacticalTileKind::NatureForestDense; ClassPath = NatureForestDensePath; break;
		case ETileDesignVisual::NatureRocky: Kind = ETacticalTileKind::NatureRocky; ClassPath = NatureRockyPath; break;
		case ETileDesignVisual::NatureScrub: Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; break;
		case ETileDesignVisual::NatureAmbush: Kind = ETacticalTileKind::NatureAmbush; ClassPath = NatureAmbushPath; break;
		case ETileDesignVisual::NatureServiceCamp: Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; break;
		case ETileDesignVisual::NatureDitch: Kind = ETacticalTileKind::NatureDitch; ClassPath = NatureDitchPath; break;
		case ETileDesignVisual::WarZoneGround:
		{
			// WarZone is one large logical region, not the single 3x3 anchor building.
			// Convert it into deterministic concentric combat bands: an unmistakably
			// industrial core, a mixed firefight belt and a natural outer buffer.
			const int32 DeltaX = Placement.GridCell.X - WarZoneCoreCell.X;
			const int32 DeltaY = Placement.GridCell.Y - WarZoneCoreCell.Y;
			const int32 DistanceSquared = DeltaX * DeltaX + DeltaY * DeltaY;
			if (DistanceSquared <= 64)
			{
				WarZoneBandTag = TEXT("WarZone_Core");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 3, 0xA341316Cu);
				const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0x51ED270Bu);
				const int32 ClusterRoll = ClusterHash % 100;
				const int32 DetailRoll = DetailHash % 100;
				const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 3);
				const int32 FeatureSlotA = static_cast<int32>((ClusterHash >> 16) % 9u);
				const int32 FeatureSlotB = (FeatureSlotA + 2 + static_cast<int32>((ClusterHash >> 21) % 5u)) % 9;
				const int32 FeatureSlotC = (FeatureSlotA + 5 + static_cast<int32>((ClusterHash >> 25) % 4u)) % 9;
				const bool bFeatureCell = LocalSlot == FeatureSlotA
					|| LocalSlot == FeatureSlotB
					|| LocalSlot == FeatureSlotC;
				WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
				if (bFeatureCell)
				{
					const int32 FeatureRoll = (ClusterRoll + LocalSlot * 17) % 100;
					if (FeatureRoll < 24) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneContainerLanePath; WarZoneVisualTag = TEXT("WZ_ContainerLane"); }
					else if (FeatureRoll < 47) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarZoneFactoryYardPath; WarZoneVisualTag = TEXT("WZ_FactoryYard"); }
					else if (FeatureRoll < 68) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneUtilityYardPath; WarZoneVisualTag = TEXT("WZ_UtilityYard"); }
					else if (FeatureRoll < 86) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
					else if (FeatureRoll < 96) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarehousePath; WarZoneVisualTag = TEXT("WZ_AuthoredWarehouse"); }
					else { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				}
				// The combat core must read as a broad industrial yard, not a maze of
				// one-cell wall fragments.  Feature cells still provide containers,
				// tanks and authored yards; only a small minority become ruins.
				else if (DetailRoll < 88) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
				else { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
			}
			else if (DistanceSquared <= 225)
			{
				WarZoneBandTag = TEXT("WarZone_Mid");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 4, 0xC8013EA4u);
				const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0x68E31DA4u);
				const int32 ClusterRoll = ClusterHash % 100;
				const int32 DetailRoll = DetailHash % 100;
				const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 4);
				const int32 FeatureSlotA = static_cast<int32>((ClusterHash >> 16) % 16u);
				const int32 FeatureSlotB = (FeatureSlotA + 5 + static_cast<int32>((ClusterHash >> 22) % 7u)) % 16;
				WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
				if (LocalSlot == FeatureSlotA || LocalSlot == FeatureSlotB)
				{
					if (ClusterRoll < 30) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneContainerLanePath; WarZoneVisualTag = TEXT("WZ_ContainerLane"); }
					else if (ClusterRoll < 50) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarZoneFactoryYardPath; WarZoneVisualTag = TEXT("WZ_FactoryYard"); }
					else if (ClusterRoll < 70) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneUtilityYardPath; WarZoneVisualTag = TEXT("WZ_UtilityYard"); }
					else if (ClusterRoll < 85) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
					else if (ClusterRoll < 93) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarehousePath; WarZoneVisualTag = TEXT("WZ_AuthoredWarehouse"); }
					else { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				}
				else if (DetailRoll < 26) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
				else if (DetailRoll < 34) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				else if (DetailRoll < 46) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				else if (DetailRoll < 70) { Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; WarZoneVisualTag = TEXT("WZ_Scrub"); }
				else if (DetailRoll < 91) { Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; WarZoneVisualTag = TEXT("WZ_Meadow"); }
				else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
			}
			else
			{
				WarZoneBandTag = TEXT("WarZone_Outer");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 5, 0xAD90777Du);
				const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0xB5297A4Du);
				const int32 ClusterRoll = ClusterHash % 100;
				const int32 DetailRoll = DetailHash % 100;
				const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 5);
				const int32 FeatureSlot = static_cast<int32>((ClusterHash >> 16) % 25u);
				const int32 BlendedRoll = (ClusterRoll * 3 + DetailRoll) / 4;
				WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
				if (LocalSlot == FeatureSlot)
				{
					if (ClusterRoll < 25) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
					else if (ClusterRoll < 50) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
					else if (ClusterRoll < 62) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
					else if (ClusterRoll < 75) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
					else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
				}
				else if (BlendedRoll < 14) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				else if (BlendedRoll < 27) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				else if (BlendedRoll < 52) { Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; WarZoneVisualTag = TEXT("WZ_Scrub"); }
				else if (BlendedRoll < 79) { Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; WarZoneVisualTag = TEXT("WZ_Meadow"); }
				else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
			}
			break;
		}
		default: break;
		}

		// Long POI access roads deliberately use the C++ tactical road builder.
		// The packed LD_Tile roads remain on generator-authored road cells and at
		// landmarks, while access corridors get clean shoulders plus sparse cover.
		// This prevents walls, barrels and cars from repeating every single cell.
		UClass* TileClass = Placement.bSupplementalAccessRoad
			? ATacticalTileActor::StaticClass()
			: ResolveClass(ClassPath);
		if (!IsValid(TileClass)) { ++SpawnFailures; continue; }
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const bool bSocketOrientedTile =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle;
		const int32 RotationQuarterTurns = bSocketOrientedTile
			? Placement.RotationQuarterTurns
			: (WarZoneRotationQuarterTurns != INDEX_NONE
				? WarZoneRotationQuarterTurns
				: Placement.LayoutVariant);
		const int32 SpawnRotationQuarterTurns = Placement.bSupplementalAccessRoad
			? 0
			: RotationQuarterTurns;
		AActor* Tile = GetWorld()->SpawnActor<AActor>(
			TileClass,
			Placement.WorldLocation,
			FRotator(0.0f, SpawnRotationQuarterTurns * 90.0f, 0.0f),
			Parameters);
		if (!IsValid(Tile)) { ++SpawnFailures; continue; }
		if (ATacticalTileActor* TacticalTile = Cast<ATacticalTileActor>(Tile))
		{
			// Runtime placement is authoritative. Blueprint defaults are only an
			// editor preview and must not override the server/seed-selected tile kind.
			TacticalTile->TileKind = Kind;
			if (!WarZoneVisualTag.IsNone())
			{
				TacticalTile->bShowDynamicProps = false;
				const bool bNatureWarZoneTile = Kind >= ETacticalTileKind::NatureMeadow
					&& Kind <= ETacticalTileKind::NatureDitch;
				if (bNatureWarZoneTile)
				{
					TacticalTile->DressingDensityScale = WarZoneBandTag == TEXT("WarZone_Core") ? 0.55f
						: (WarZoneBandTag == TEXT("WarZone_Mid") ? 0.85f : 1.05f);
				}
				else if (Kind == ETacticalTileKind::WarZoneYard
					|| Kind == ETacticalTileKind::WarZoneWarehouse)
				{
					// Authored industrial cells still need an overgrown shoulder. The old
					// zero multiplier silently removed all grass added by their layouts.
					TacticalTile->DressingDensityScale = 0.70f;
				}
				else if (Kind == ETacticalTileKind::OpenGround
					|| Kind == ETacticalTileKind::Ruins)
				{
					TacticalTile->DressingDensityScale = WarZoneBandTag == TEXT("WarZone_Core") ? 0.50f : 0.75f;
				}
				else
				{
					TacticalTile->DressingDensityScale = 0.20f;
				}
			}
			if (Placement.bSupplementalAccessRoad)
			{
				TacticalTile->bShowDynamicProps = false;
				TacticalTile->bIsAccessRoad = true;
				TacticalTile->DressingDensityScale = 0.35f;
			}
			// Rebuild needs the combat-band tags to choose its ground treatment.
			if (!WarZoneBandTag.IsNone()) TacticalTile->Tags.AddUnique(WarZoneBandTag);
			if (!WarZoneVisualTag.IsNone()) TacticalTile->Tags.AddUnique(WarZoneVisualTag);
			TacticalTile->RebuildFromRuntimeSpec(
				static_cast<int32>(StableHash),
				Placement.ConnectionMask,
				Placement.LayoutVariant);
		}
		NormalizePackedBaseGround(Tile);
		HiddenTerrainUnderlayCount += HidePerTileTerrainUnderlay(Tile);
		DisableCollisionOnHiddenPrimitives(Tile);
		// All authored road/ground underlays are hidden above.  The one shared road
		// HISM is now the only rendered surface, eliminating both the leaf-pattern
		// material and coplanar flicker.
		if (Placement.Visual == ETileDesignVisual::Exit)
			LiftPackedExitMarking(Tile);
		Tile->Tags.AddUnique(TEXT("RuntimeTacticalTile"));
		Tile->Tags.AddUnique(FName(*StaticEnum<ETacticalTileKind>()->GetNameStringByValue(
			static_cast<int64>(Kind))));
		if (!WarZoneBandTag.IsNone())
			Tile->Tags.AddUnique(WarZoneBandTag);
		if (!WarZoneVisualTag.IsNone())
		{
			Tile->Tags.AddUnique(WarZoneVisualTag);
			WarZoneVariantCounts.FindOrAdd(WarZoneVisualTag)++;
		}
		#if WITH_EDITOR
		Tile->SetActorLabel(FString::Printf(TEXT("RuntimeTile_%d_%d"), Placement.GridCell.X, Placement.GridCell.Y));
		Tile->SetFolderPath(TEXT("RuntimeTacticalTiles"));
		#endif
		// The tile actor is only a deterministic construction template. Consolidate
		// its visible static meshes into shared HISM batches, then discard the actor;
		// the authoritative placement manifest and gameplay points remain unchanged.
		PackActorVisuals(Tile);
		Tile->Destroy();
		++PackedTileActorCount;
		KindCounts.FindOrAdd(Kind)++;
	}
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->BuildTreeIfOutdated(false, true);
	}

	int32 SpawnedFacilityCount = 0;
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		// An authored level supplies the whole facility. Spawning the procedural
		// builder as well would stack a second building inside the first.
		if (FacilityUsesAuthoredLevel(Placement.VisualSet, Placement.Footprint))
			continue;

		EProceduralFacilityKind FacilityKind = EProceduralFacilityKind::SatelliteCamp2x2;
		if (Placement.VisualSet == EFacilityVisualSet::Warehouse)
			FacilityKind = EProceduralFacilityKind::IndustrialRaid3x3;
		else if (Placement.VisualSet == EFacilityVisualSet::LongBarracks)
			FacilityKind = EProceduralFacilityKind::LongBarracks2x1;
		else if (Placement.VisualSet == EFacilityVisualSet::LinearTrench)
			FacilityKind = EProceduralFacilityKind::LinearTrench4x1;
		else if (Placement.VisualSet == EFacilityVisualSet::DowntownBlock)
			FacilityKind = EProceduralFacilityKind::DowntownBlock3x3;
		else if (Placement.VisualSet == EFacilityVisualSet::FactoryConstruction)
			FacilityKind = EProceduralFacilityKind::FactoryConstruction2x2;
		else if (Placement.VisualSet == EFacilityVisualSet::RuralHideout)
			FacilityKind = EProceduralFacilityKind::RuralHideout2x2;

		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector FacilityLocation = GetDesignFootprintCenter(Placement);
		const FRotator FacilityRotation(0.0f, Placement.RotationQuarterTurns * 90.0f, 0.0f);

		// An authored Blueprint, when one exists for this facility, is preferred over
		// the code-built version: its walls and props are individual components a
		// designer can select and drag in the editor, which the HISM instances
		// AProceduralFacilityActor emits can never be. The procedural builder stays
		// as the fallback for every kind that has not been authored yet, so the two
		// can coexist while the library is filled in one facility at a time.
		UClass* AuthoredFacilityClass = nullptr;
		if (bUseAuthoredFacilityBlueprints)
		{
			if (const TCHAR* AuthoredPath = GetAuthoredFacilityBlueprintPath(Placement.VisualSet))
				AuthoredFacilityClass = ResolveClass(AuthoredPath);
		}

		AActor* Facility = nullptr;
		if (IsValid(AuthoredFacilityClass))
		{
			Facility = GetWorld()->SpawnActor<AActor>(
				AuthoredFacilityClass, FacilityLocation, FacilityRotation, Parameters);
			if (IsValid(Facility))
			{
				// The authored Blueprint derives from ATacticalTileActor, so its
				// inherited 20 m ground slab and nature dressing would be layered
				// under a 40-60 m footprint. The shared terrain pad already covers
				// this cell range; hide the inherited underlay exactly as the packed
				// tiles do.
				HiddenTerrainUnderlayCount += HidePerTileTerrainUnderlay(Facility);
			}
		}
		if (!IsValid(Facility))
		{
			AProceduralFacilityActor* ProceduralFacility = GetWorld()->SpawnActor<AProceduralFacilityActor>(
				AProceduralFacilityActor::StaticClass(),
				FacilityLocation,
				FacilityRotation,
				Parameters);
			if (IsValid(ProceduralFacility))
			{
				// Rotation controls how the footprint connects to the generated road
				// graph; the facility's deterministic interior variant is derived
				// independently from the server-authored local seed.
				const uint8 FacilityLayoutVariant = static_cast<uint8>(Placement.LocalSeed) & 3;
				ProceduralFacility->Configure(FacilityKind, Placement.LocalSeed, FacilityLayoutVariant);
			}
			Facility = ProceduralFacility;
		}
		if (!IsValid(Facility))
		{
			++SpawnFailures;
			continue;
		}
		DisableCollisionOnHiddenPrimitives(Facility);
		Facility->Tags.AddUnique(TEXT("RuntimeTacticalFacility"));
		Facility->Tags.AddUnique(FName(*StaticEnum<EProceduralFacilityKind>()->GetNameStringByValue(
			static_cast<int64>(FacilityKind))));
		#if WITH_EDITOR
		Facility->SetActorLabel(FString::Printf(TEXT("RuntimeFacility_%s_%d_%d%s"),
			*Placement.FacilityId.ToString(), Placement.AnchorCell.X, Placement.AnchorCell.Y,
			IsValid(AuthoredFacilityClass) ? TEXT("_Authored") : TEXT("")));
		Facility->SetFolderPath(TEXT("RuntimeTacticalFacilities"));
		#endif
		UE_LOG(LogTemp, Display,
			TEXT("Facility visual source: id=%s source=%s class=%s at=(%.0f,%.0f,%.0f) rotation=%.0f"),
			*Placement.FacilityId.ToString(),
			IsValid(AuthoredFacilityClass) ? TEXT("authored_blueprint") : TEXT("procedural_code"),
			*Facility->GetClass()->GetName(),
			FacilityLocation.X, FacilityLocation.Y, FacilityLocation.Z,
			FacilityRotation.Yaw);
		SpawnedRuntimeTiles.Add(Facility);
		++SpawnedFacilityCount;
	}

	// Render a single unified ground layer for the whole generated footprint.
	// Per-tile terrain slabs are hidden above, so there are no coplanar surfaces
	// and no material discontinuity at the 20 m cell boundary.
	GroundHISM->SetVisibility(true, true);
	GroundHISM->SetHiddenInGame(false);
	WarZoneGroundHISM->SetVisibility(true, true);
	WarZoneGroundHISM->SetHiddenInGame(false);
	TransitionGroundHISM->SetVisibility(true, true);
	TransitionGroundHISM->SetHiddenInGame(false);
	RoadSurfaceHISM->SetVisibility(true, true);
	RoadSurfaceHISM->SetHiddenInGame(false);
	GroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	WarZoneGroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TransitionGroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RoadSurfaceHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogTemp, Display, TEXT("Runtime packed tile world: requested=%d packed_tile_actors=%d water_cells_skipped=%d packed_hism_components=%d packed_visual_instances=%d persistent_facilities=%d failures=%d shared_ground_visible=true shared_road_visible=true hidden_tile_ground_components=%d"),
		TileDesignPlacements.Num(), PackedTileActorCount, WaterTileSkipCount,
		RuntimePackedVisualHISMs.Num(), PackedVisualInstanceCount,
		SpawnedFacilityCount, SpawnFailures, HiddenTerrainUnderlayCount);
	for (const TPair<ETacticalTileKind, int32>& Pair : KindCounts)
		UE_LOG(LogTemp, Display, TEXT("Runtime Blueprint tile count: kind=%s count=%d"),
			*StaticEnum<ETacticalTileKind>()->GetNameStringByValue(static_cast<int64>(Pair.Key)), Pair.Value);
	for (const TPair<FName, int32>& Pair : WarZoneVariantCounts)
		UE_LOG(LogTemp, Display, TEXT("WarZone visual count: variant=%s count=%d"),
			*Pair.Key.ToString(), Pair.Value);

	RefreshNavigationBlockerRegion(CenterNavigationBlockers, GetActorLocation(), 6000.0f);
}

void AWarZoneFootprintPreview::RefreshNavigationBlockerRegion(
	UTacticalTileNavModifierComponent* Modifier,
	const FVector& WorldCenter,
	float RadiusCm)
{
	if (!IsValid(Modifier))
		return;

	TArray<FBox> WorldBlockers;
	const float RadiusSquared = FMath::Square(RadiusCm);
	for (AActor* TileActor : SpawnedRuntimeTiles)
	{
		if (!IsValid(TileActor)
			|| FVector::DistSquared2D(TileActor->GetActorLocation(), WorldCenter) > RadiusSquared)
		{
			continue;
		}

		TArray<AActor*> GeometryActors;
		GeometryActors.Add(TileActor);
		TArray<AActor*> AttachedActors;
		TileActor->GetAttachedActors(AttachedActors, true, true);
		GeometryActors.Append(AttachedActors);
		for (const AActor* GeometryActor : GeometryActors)
		{
			if (!IsValid(GeometryActor))
				continue;
			TArray<UPrimitiveComponent*> PrimitiveComponents;
			GeometryActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (const UPrimitiveComponent* Primitive : PrimitiveComponents)
			{
				if (!IsValid(Primitive)
					|| Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				{
					continue;
				}
				const FString ComponentName = Primitive->GetName();
				if (ComponentName.Contains(TEXT("Ground"))
					|| ComponentName.Contains(TEXT("Road"))
					|| ComponentName.Contains(TEXT("Roof"))
					|| ComponentName.Contains(TEXT("Grass"))
					|| ComponentName.Contains(TEXT("Marking")))
				{
					continue;
				}

				auto AddBlocker = [&WorldBlockers](const FBox& Box)
				{
					if (Box.GetExtent().Z >= 45.0f && Box.GetSize().X * Box.GetSize().Y >= 900.0f)
						WorldBlockers.Add(Box.ExpandBy(FVector(20.0f, 20.0f, 0.0f)));
				};
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Primitive))
				{
					if (!IsValid(ISM->GetStaticMesh()))
						continue;
					const FBox MeshBox = ISM->GetStaticMesh()->GetBoundingBox();
					for (int32 InstanceIndex = 0; InstanceIndex < ISM->GetInstanceCount(); ++InstanceIndex)
					{
						FTransform WorldTransform;
						if (ISM->GetInstanceTransform(InstanceIndex, WorldTransform, true))
							AddBlocker(MeshBox.TransformBy(WorldTransform));
					}
				}
				else
				{
					AddBlocker(Primitive->Bounds.GetBox());
				}
			}
		}
	}

	Modifier->SetWorldBlockers(WorldBlockers);
	UE_LOG(LogTemp, Display,
		TEXT("Local navigation blockers: center=(%.0f,%.0f) radius=%.0f boxes=%d"),
		WorldCenter.X, WorldCenter.Y, RadiusCm, WorldBlockers.Num());
}

void AWarZoneFootprintPreview::LoadFacilityDesignLevel(
	const FFacilityPlacement& Placement,
	int32 PlacementIndex)
{
	if (Placement.FacilityLevel.IsNull() || PlacementIndex < 0)
		return;

	while (FacilityDesignLevelInstances.Num() <= PlacementIndex)
		FacilityDesignLevelInstances.Add(nullptr);
	while (FacilityLoadRequestTimeSeconds.Num() <= PlacementIndex)
		FacilityLoadRequestTimeSeconds.Add(0.0);

	const FVector Location = GetDesignFootprintCenter(Placement);
	const FRotator Rotation(0.0f, Placement.RotationQuarterTurns * 90.0f, 0.0f);
	bool bRequested = false;
	FacilityLoadRequestTimeSeconds[PlacementIndex] = FPlatformTime::Seconds();
	const FString OptionalName = FString::Printf(TEXT("Facility_%02d_Type_%d"),
		PlacementIndex, static_cast<int32>(Placement.VisualSet));
	FacilityDesignLevelInstances[PlacementIndex] = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
		this,
		Placement.FacilityLevel,
		Location,
		Rotation,
		bRequested,
		OptionalName);
	if (IsValid(FacilityDesignLevelInstances[PlacementIndex]))
	{
		FacilityDesignLevelInstances[PlacementIndex]->SetShouldBeLoaded(true);
		FacilityDesignLevelInstances[PlacementIndex]->SetShouldBeVisible(true);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Facility design level instance: index=%d type=%d %s at=(%.0f,%.0f,%.0f) rotation=%.0f"),
		PlacementIndex,
		static_cast<int32>(Placement.VisualSet),
		bRequested ? TEXT("requested") : TEXT("failed"),
		Location.X, Location.Y, Location.Z, Rotation.Yaw);
}

void AWarZoneFootprintPreview::BuildBorderMountains()
{
	if (!IsValid(MountainHISM) || MountainHISM->GetStaticMesh() == nullptr)
		return;
	MountainHISM->ClearInstances();
	if (!bBuildBorderMountains || TileDesignPlacements.IsEmpty())
		return;

	FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint MaxCell(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
	{
		MinCell = FIntPoint(
			FMath::Min(MinCell.X, Placement.GridCell.X), FMath::Min(MinCell.Y, Placement.GridCell.Y));
		MaxCell = FIntPoint(
			FMath::Max(MaxCell.X, Placement.GridCell.X), FMath::Max(MaxCell.Y, Placement.GridCell.Y));
	}
	const FVector2D GridCentre(
		(MinCell.X + MaxCell.X) * 0.5f * DesignCellSize,
		(MinCell.Y + MaxCell.Y) * 0.5f * DesignCellSize);
	const float GridHalfSpanCm = FMath::Max(
		(MaxCell.X - MinCell.X + 1) * DesignCellSize,
		(MaxCell.Y - MinCell.Y + 1) * DesignCellSize) * 0.5f;

	// Two staggered rings of the pack's background-mountain mesh. The numbers
	// follow how Downtown_West's own demo dressed its horizon: instances 550 m+
	// from centre, scales 0.4-1.5, sunk 15-34 m so only ridgelines rise over the
	// valley floor. The inner ring carries the silhouette; the sparser, larger
	// outer ring gives the range depth so it does not read as a fence of hills.
	const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode());
	const int64 RaidSeed = IsValid(GameMode) ? GameMode->GetMapGenerationSeed() : 0;
	FRandomStream MountainStream(static_cast<int32>(GetTypeHash(RaidSeed) ^ 0x304Au));
	TArray<FTransform> MountainTransforms;
	const struct { int32 Count; float Radius; float ScaleMin; float ScaleMax; float BaseZ; } Rings[] = {
		{ 14, GridHalfSpanCm + 24000.0f, 0.9f, 1.3f, -2000.0f },
		{  8, GridHalfSpanCm + 46000.0f, 1.3f, 1.7f, -2600.0f },
	};
	for (const auto& Ring : Rings)
	{
		for (int32 Index = 0; Index < Ring.Count; ++Index)
		{
			const float Angle = (2.0f * PI * Index) / Ring.Count
				+ MountainStream.FRandRange(-0.12f, 0.12f);
			const float Radius = Ring.Radius + MountainStream.FRandRange(-6000.0f, 6000.0f);
			const FVector Location(
				GridCentre.X + FMath::Cos(Angle) * Radius,
				GridCentre.Y + FMath::Sin(Angle) * Radius,
				Ring.BaseZ + MountainStream.FRandRange(-600.0f, 200.0f));
			const float Scale = MountainStream.FRandRange(Ring.ScaleMin, Ring.ScaleMax);
			MountainTransforms.Add(FTransform(
				FRotator(0.0f, MountainStream.FRandRange(0.0f, 360.0f), 0.0f),
				Location,
				FVector(Scale, Scale, Scale * MountainStream.FRandRange(0.9f, 1.25f)))
				.GetRelativeTransform(MountainHISM->GetComponentTransform()));
		}
	}
	MountainHISM->AddInstances(MountainTransforms, false, false, true);
	MountainHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border mountains: instances=%d inner_radius_cm=%.0f outer_radius_cm=%.0f grid_half_span_cm=%.0f"),
		MountainTransforms.Num(), Rings[0].Radius, Rings[1].Radius, GridHalfSpanCm);
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

FVector AWarZoneFootprintPreview::GetFootprintCenter(const FFacilityPlacement& Placement) const
{
	if (Placement.OccupiedCells.IsEmpty())
		return FVector::ZeroVector;

	FVector Center = FVector::ZeroVector;
	for (const FIntPoint& Cell : Placement.OccupiedCells)
	{
		Center.X += Cell.X * GridStep;
		Center.Y += Cell.Y * GridStep;
	}
	return Center / Placement.OccupiedCells.Num();
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

void AWarZoneFootprintPreview::ConfigureProxyMesh(
	UStaticMeshComponent* Component,
	const FVector& RelativeLocation,
	const FVector& Size)
{
	if (!IsValid(Component))
		return;

	Component->SetRelativeLocation(RelativeLocation);
	Component->SetRelativeScale3D(Size / 100.0f);
	Component->SetVisibility(true);
}

void AWarZoneFootprintPreview::ShowWarehouseProxy(const FVector& FootprintCenter)
{
	const float FootprintSize = GridStep * 1.8f;
	const float HalfSize = FootprintSize * 0.5f;
	const float WallThickness = GridStep * 0.12f;
	const float WallHeight = GridStep * 1.0f;
	const float FloorThickness = GridStep * 0.12f;
	const float DoorWidth = GridStep * 0.65f;
	const float FrontSegmentWidth = (FootprintSize - DoorWidth) * 0.5f;

	const FVector BaseOffset = FootprintCenter - GetActorLocation();
	ConfigureProxyMesh(
		FloorProxy,
		BaseOffset + FVector(0.0f, 0.0f, FloorThickness * 0.5f),
		FVector(FootprintSize, FootprintSize, FloorThickness));
	ConfigureProxyMesh(
		BackWallProxy,
		BaseOffset + FVector(0.0f, HalfSize - WallThickness * 0.5f, WallHeight * 0.5f),
		FVector(FootprintSize, WallThickness, WallHeight));
	ConfigureProxyMesh(
		LeftWallProxy,
		BaseOffset + FVector(-HalfSize + WallThickness * 0.5f, 0.0f, WallHeight * 0.5f),
		FVector(WallThickness, FootprintSize, WallHeight));
	ConfigureProxyMesh(
		RightWallProxy,
		BaseOffset + FVector(HalfSize - WallThickness * 0.5f, 0.0f, WallHeight * 0.5f),
		FVector(WallThickness, FootprintSize, WallHeight));

	const float FrontSegmentOffset = DoorWidth * 0.5f + FrontSegmentWidth * 0.5f;
	ConfigureProxyMesh(
		FrontWallLeftProxy,
		BaseOffset + FVector(-FrontSegmentOffset, -HalfSize + WallThickness * 0.5f, WallHeight * 0.5f),
		FVector(FrontSegmentWidth, WallThickness, WallHeight));
	ConfigureProxyMesh(
		FrontWallRightProxy,
		BaseOffset + FVector(FrontSegmentOffset, -HalfSize + WallThickness * 0.5f, WallHeight * 0.5f),
		FVector(FrontSegmentWidth, WallThickness, WallHeight));
	ConfigureProxyMesh(
		RoofProxy,
		BaseOffset + FVector(0.0f, 0.0f, WallHeight + FloorThickness * 0.5f),
		FVector(FootprintSize, FootprintSize, FloorThickness));
}

void AWarZoneFootprintPreview::ShowYardProxy(const FVector& FootprintCenter)
{
	const FVector BaseOffset = FootprintCenter - GetActorLocation();
	const float FootprintSize = GridStep * 1.8f;
	const float FloorThickness = GridStep * 0.08f;
	const float CoverLength = GridStep * 0.65f;
	const float CoverThickness = GridStep * 0.18f;
	const float CoverHeight = GridStep * 0.45f;
	const float CoverOffset = GridStep * 0.48f;

	ConfigureProxyMesh(
		YardFloorProxy,
		BaseOffset + FVector(0.0f, 0.0f, FloorThickness * 0.5f),
		FVector(FootprintSize, FootprintSize, FloorThickness));
	ConfigureProxyMesh(
		YardCoverNorthProxy,
		BaseOffset + FVector(0.0f, CoverOffset, CoverHeight * 0.5f),
		FVector(CoverLength, CoverThickness, CoverHeight));
	ConfigureProxyMesh(
		YardCoverSouthProxy,
		BaseOffset + FVector(0.0f, -CoverOffset, CoverHeight * 0.5f),
		FVector(CoverLength, CoverThickness, CoverHeight));
	ConfigureProxyMesh(
		YardCoverWestProxy,
		BaseOffset + FVector(-CoverOffset, 0.0f, CoverHeight * 0.5f),
		FVector(CoverThickness, CoverLength, CoverHeight));
	ConfigureProxyMesh(
		YardCoverEastProxy,
		BaseOffset + FVector(CoverOffset, 0.0f, CoverHeight * 0.5f),
		FVector(CoverThickness, CoverLength, CoverHeight));
}

void AWarZoneFootprintPreview::DrawReservation() const
{
	if (FacilityPlacements.Num() < 2 || GridStep <= 0.0f)
		return;

	const FVector CellExtent(GridStep * 0.42f, GridStep * 0.42f, GridStep * 0.12f);
	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		FColor Color = FColor::Cyan;
		const TCHAR* Label = TEXT("WAREHOUSE 2x2");
		if (Placement.VisualSet == EFacilityVisualSet::Yard)
		{
			Color = FColor::Yellow;
			Label = TEXT("YARD 2x2");
		}
		else if (Placement.VisualSet == EFacilityVisualSet::Checkpoint)
		{
			Color = FColor::Green;
			Label = TEXT("CHECKPOINT 1x2");
		}
		for (const FIntPoint& Cell : Placement.OccupiedCells)
		{
			const FVector Location(Cell.X * GridStep, Cell.Y * GridStep, GridStep * 0.7f);
			DrawDebugBox(GetWorld(), Location, CellExtent, Color, false, 0.12f, 0, 1.5f);
		}

		const FVector Center = GetFootprintCenter(Placement) + FVector(0.0f, 0.0f, GridStep * 0.7f);
		DrawDebugBox(
			GetWorld(), Center,
			FVector(GridStep * 0.92f, GridStep * 0.92f, GridStep * 0.22f),
			Color, false, 0.12f, 0, 3.0f);
		DrawDebugString(
			GetWorld(), Center + FVector(0.0f, 0.0f, GridStep * 0.5f),
			Label, nullptr, Color, 0.12f, true, 1.0f);
	}
}

void AWarZoneFootprintPreview::DrawDesignScalePreview() const
{
	if (FacilityPlacements.Num() < 2)
		return;

	for (const FFacilityPlacement& Placement : FacilityPlacements)
	{
		FVector PreviewCenter = FVector::ZeroVector;
		FColor Color = FColor::Cyan;
		const TCHAR* Label = TEXT("DESIGN: WAREHOUSE 2x2");
		if (Placement.VisualSet == EFacilityVisualSet::Warehouse)
		{
			PreviewCenter = FVector(-5000.0f, 4000.0f, 100.0f);
		}
		else if (Placement.VisualSet == EFacilityVisualSet::Yard)
		{
			PreviewCenter = FVector(0.0f, 4000.0f, 100.0f);
			Color = FColor::Yellow;
			Label = TEXT("DESIGN: YARD 2x2");
		}
		else
		{
			PreviewCenter = FVector(4500.0f, 4000.0f, 100.0f);
			Color = FColor::Green;
			Label = TEXT("DESIGN: CHECKPOINT 1x2");
		}

		int32 MinX = TNumericLimits<int32>::Max();
		int32 MinY = TNumericLimits<int32>::Max();
		int32 MaxX = TNumericLimits<int32>::Lowest();
		int32 MaxY = TNumericLimits<int32>::Lowest();
		for (const FIntPoint& Cell : Placement.OccupiedCells)
		{
			MinX = FMath::Min(MinX, Cell.X);
			MinY = FMath::Min(MinY, Cell.Y);
			MaxX = FMath::Max(MaxX, Cell.X);
			MaxY = FMath::Max(MaxY, Cell.Y);
		}

		const float Width = (MaxX - MinX + 1) * DesignCellSize;
		const float Height = (MaxY - MinY + 1) * DesignCellSize;
		for (const FIntPoint& Cell : Placement.OccupiedCells)
		{
			const float LocalX = (Cell.X - MinX + 0.5f) * DesignCellSize - Width * 0.5f;
			const float LocalY = (Cell.Y - MinY + 0.5f) * DesignCellSize - Height * 0.5f;
			DrawDebugBox(
				GetWorld(),
				PreviewCenter + FVector(LocalX, LocalY, 0.0f),
				FVector(DesignCellSize * 0.48f, DesignCellSize * 0.48f, 100.0f),
				Color, false, 0.12f, 0, 10.0f);
		}

		DrawDebugBox(
			GetWorld(), PreviewCenter,
			FVector(Width * 0.5f, Height * 0.5f, 180.0f),
			Color, false, 0.12f, 0, 15.0f);
		DrawDebugString(
			GetWorld(), PreviewCenter + FVector(0.0f, 0.0f, 350.0f),
			FString::Printf(TEXT("%s | CELL=20m | ROT=%d"), Label, Placement.RotationQuarterTurns * 90),
			nullptr, Color, 0.12f, true, 1.5f);
	}
}
