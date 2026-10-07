// 절차 맵의 화면 담당: 논리 칸(AMapTile) 격자를 보고 타일·시설·가장자리 호수를 실제로 깔아 준다.

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
#include "Actors/MapBuilder/MapItemSpawner.h"
#include "Actors/MapManifestActor.h"
#include "Components/MapGeneratorComponent.h"
using namespace MapBuild;


AMapBuilder::AMapBuilder()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// 숨겨 둔 길찾기(navigation) 바닥판은 Static 이다. 붙는 부모 쪽도 Static 으로 맞춰야
	// PIE 가 Recast 가 읽기 전에 붙이기(attachment)를 거부하지 않는다.
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

	// 분지를 둘러싼 산 고리. 일부러 충돌이 없다: 맵 끝은 여전히 떨어지는 곳이어야 하고,
	// Recast 가 몇 km 밖 배경 위로 길을 내면 안 되기 때문이다.
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
	// 물 표면은 보이기만 하는 판이다. 충돌을 주면 플레이어가 호수 위에 서고,
	// 길찾기를 주면 AI 가 호수를 가로질러 길을 잡는다.
	LakeWaterHISM->SetCanEverAffectNavigation(false);
	LakeWaterHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 손으로 깎은 땅 모양(언덕·구덩이 등): 모양 하나 * 땅 종류(band) 하나마다 HISM 하나.
	// 메시는 같이 쓰고 컴포넌트 머티리얼만 바꾼다. 그래야 WarZone 안에 놓인 언덕도
	// 주변 평평한 칸과 같은 흙으로 보이고, 공장 마당 한가운데 초록 풀밭처럼 튀지 않는다.
	// 인덱스는 Band * ShapeCount + Shape.
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

	// 바위·나무는 막고, 덤불은 지나갈 수 있게 둔다. 그래야 비탈이 벽이 되지 않는다.
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
		// 호숫가 꾸미기. 물가 선은 20 m 칸마다 정해지므로 그대로 두면 격자 따라
		// 계단처럼 보인다. 얕은 물에 바위·갈대를 세워 그 선을 흐트러뜨린다
		// - 디오라마 맵이 자기 물가에서 쓰는 것과 같은 방법.
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

	// 물가 이어 붙이는 메시, 모양마다 HISM 하나. PG.BuildShoreMeshes 로 만든다.
	// 땅 쪽 가장자리는 공통 높이 Z=20 에 평평하게 맞춰져 있어 보통 칸 옆에 그냥 붙고,
	// 안쪽은 물 아래로 휘어져 내려간다. 20 m 계단을 실제로 없애는 건 이 메시다
	// - 돌린 박스나 흩뿌린 바위는 계단을 가리기만 했다.
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
		// 일부러 ConstructorHelpers 를 쓰지 않는다: 이 메시들은 PG.BuildShoreMeshes 로 만들어지므로
		// 새로 받은 직후나 막 다시 만든 직후에는 에디터 시작 때 CDO 를 만들 시점에 아직 없다.
		// 생성자에서 찾으면 조용히 실패해서 다음 재시작까지 물가가 안 보인다.
		// 그래서 BuildLightweightWorldVisuals 에서 불러온다.
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
	if (!ItemSpawner)
	{
		ItemSpawner = NewObject<UMapItemSpawner>(this, TEXT("ItemSpawner"));
		ItemSpawner->Init(this);
	}
	// (10/4 리슨 서버) 예전엔 여기서 "서버가 아니면 끝" 이었다. 이제 들어온 사람도 맵 설계도(AMapManifestActor)로
	// 칸 쪽지를 받아 같은 맵을 직접 세우므로 양쪽 다 기다린다. 쪽지가 생길 때까지 TryReserveFootprint 가 빈손으로 돌아온다.
	// 서버만 해야 하는 일(설계도 만들기·아이템 놓기·플레이어 세우기)은 각자 넷 모드로 거른다.

	// GameMode 가 BeginPlay 중에 논리 격자를 만든다. 잠깐씩 다시 시도해서
	// 액터들의 BeginPlay 순서에 기대지 않게 한다.
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
	// 끼임 정리가 끝나면 상자 자리마다 아이템을 놓는다(한 번만).
	ItemSpawner->SpawnLootOnce();
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

int64 AMapBuilder::GetRaidSeed() const
{
	if (const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr))
		return GameMode->GetMapGenerationSeed();
	return ReplicatedRaidSeed;
}

// 형님 생성기의 시작 구역 상자 크기(_startPositionRangeSize). 형님 코드에 getter 를 더하지 않으려고 이름으로 찾아 읽는다(리플렉션).
// 이름이 바뀌면 4 로 돌아간다.
int32 AMapBuilder::GetStartRangeSize() const
{
	if (const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr))
	{
		const UMapGeneratorComponent* Generator = GameMode->FindComponentByClass<UMapGeneratorComponent>();
		const FIntProperty* RangeProperty = FindFProperty<FIntProperty>(UMapGeneratorComponent::StaticClass(), TEXT("_startPositionRangeSize"));
		if (Generator && RangeProperty)
			return FMath::Max(1, RangeProperty->GetPropertyValue_InContainer(Generator));
		return 4;
	}
	return ReplicatedStartRange;
}

void AMapBuilder::ApplyReplicatedManifest(int64 InSeed, int32 InStartRange)
{
	bHasReplicatedManifest = true;
	ReplicatedRaidSeed = InSeed;
	ReplicatedStartRange = InStartRange;
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

	// 런타임 블루프린트 방식에서는 AMapTile 과 HISM 세계를 만들기용 데이터로만 쓴다.
	// 비교적 무거운 배치 변환을 시작하기 전에 먼저 숨겨야
	// PIE 시작 때 색칠된 metaball 미리보기가 한 프레임 번쩍 보이지 않는다.
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

	// 서버: 칸 쪽지를 다 읽었으면 설계도(칸 위치·종류 + 시드)를 만들어 들어온 사람에게 보낸다(리슨 서버).
	if (GetWorld()->GetNetMode() != NM_Client && !IsValid(Manifest))
	{
		TArray<FMapTileRecord> Records;
		Records.Reserve(AllTiles.Num());
		for (const AMapTile* Tile : AllTiles)
			Records.Add({ FVector_NetQuantize(Tile->GetActorLocation()), Tile->GetType() });
		Manifest = GetWorld()->SpawnActor<AMapManifestActor>(AMapManifestActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		if (Manifest)
			Manifest->SetManifest(Records, GetRaidSeed(), GetStartRangeSize());
	}

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
	// Recast 는 타일마다 모양을 뽑아 쓰지 않고 이 바닥판 하나만 읽는다.
	// 그래서 바닥판은 만들어진 격자만큼만 덮어야 한다. 예전에 900 m 로 고정했더니
	// 맵이 작을 때 가장자리 밖에 걸어갈 수 있는 빈 공간이 생겼다.
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
	// 런타임 블루프린트 타일과 같이 돈다: 타일별 소품은 그 타일의 HISM 꾸미기가 맡고,
	// 이 단계는 풀밭·덤불·폐허·빈 땅 칸에 풀/덤불 덩어리를 흩뿌려
	// 칸 사이 20 m 이음새를 흐리게 한다.
	GroundBuilder->BuildPCGDressingGraph();
	NavigationValidationStartTimeSeconds = FPlatformTime::Seconds();
	FacilityDesignLevelInstances.Reset();
	FacilityLoadRequestTimeSeconds.Reset();
	LoggedFacilityDesignLevelIndices.Reset();
	for (int32 FacilityIndex = 0; FacilityIndex < FacilityPlacements.Num(); ++FacilityIndex)
		TileSpawner->LoadFacilityDesignLevel(FacilityPlacements[FacilityIndex], FacilityIndex);
	GroundBuilder->BuildBorderMountains();

	// 팀원이 만든 AMapTile 액터는 계속 논리 기록의 원본이다. 변환이 끝나면
	// 보이는 것·충돌만 꺼서, 데이터는 지우지 않고 HISM 과 Level Instance 가
	// 실제로 보이고 플레이하는 세계가 되게 한다.
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

	// 마칭 스퀘어(marching squares). 칸 모서리는 거기 모이는 네 칸 중 하나라도 물이면 '젖은' 것으로 본다.
	// 그래서 물가 선이 칸 테두리를 따라가지 않고 칸을 가로지른다. 이웃한 두 칸은 모서리 두 개를
	// 항상 같이 쓰고, 만들어진 메시는 각 변을 그 변의 두 모서리 높이만으로 이어 그리므로
	// 양쪽에서 본 단면이 똑같다 - 둘 사이에 벽이 생길 수 없다.
	//
	// 예전에는 *변* 기준으로 골랐던 게 실수였다: 모래사장 칸의 변은 길이 방향으로 -110 에서 +20 까지
	// 내려가는데 옆 평지 칸은 내내 +20 이라, 물가 선이 끝나는 곳마다 130 cm 턱이 생겼다.
	auto IsCornerWet = [&LakeCells](const FIntPoint& Cell, int32 CornerIndex)
	{
		// 모서리 순서는 SW, SE, NE, NW - 만들어진 메시와 같은 순서.
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

	// 기본 마스크. 생성기가 에셋을 쓰는 순서와 같다.
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

		// 모서리가 다 마르면 보통 평지. 다 젖으면 완전히 물속이라 호수 바닥과 물 판이 이미 덮는다
		// - 단, 생성기가 실제로 물로 정한 칸만 그렇다. 두 면 이상 물에 닿은 *땅* 칸도
		// 네 모서리가 다 젖게 되는데, 예전엔 이게 양쪽 처리에서 다 빠졌다:
		// 물가 메시도 없고, 주변은 다 호수 바닥으로 내려갔는데 혼자 Z=20 평판으로 남았다.
		// 물가를 따라 보이던 구멍이 이것이었다.
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
	// 시설 자리 밖은 모두 같은 땅 높이 하나를 쓴다.
	//
	// 예전에는 높이거나 낮춘 시설 바닥 둘레에 1~2칸짜리 '어깨' 고리를 섞어 경사를 만들었다.
	// 문서상으로만 경사였다: 땅은 칸마다 20 m 평판 하나로 그려지므로 실제로는
	// 30~90 cm 높이차의 20 m 계단이 쌓인 것이었다. MaxStepHeight 45 cm 기준으로는
	// 빈 들판 한가운데 넘을 수 없는 턱이 생겼고 - 시설 2칸 안을 지나는 길 위에도 생겼다 -
	// 이웃한 시설들의 고리가 겹치면 더 높은 턱이 됐다. 지금은 일부러 높이를 주는 곳은
	// 시설 자리뿐이고, 그 시설이 BuildElevatedFacilityTerrain 이 만드는
	// 차량 경사로와 보병 계단을 직접 갖는다.
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

	// WarZone 중심은 판의 마지막 교전 장소이고 바닥이 3x3 칸이라, 경사로가 하나뿐이면
	// 240 m 둘레 중 5% 정도만 올라갈 수 있어 공격하는 쪽이 전부 한 줄로 몰린다.
	// 그래서 사방에 하나씩 올라가는 길을 준다. 바깥의 2x2·2x1 시설은 입구 하나를 유지한다:
	// 그 크기에선 입구 하나가 병목이 아니라 '지키기 좋은 거점'으로 읽힌다.
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
		// 직접 만든 입구가 이미 그 면을 맡고 있다. 같은 방향에 가운데 칸을 또 더하면
		// 첫 경사로 위에 두 번째 경사로가 겹친다.
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
	// 직접 만든 바닥 메시를 공통 땅 윗면보다 아주 살짝만 띄워 z-fighting(겹쳐서 깜빡임)을 막는다.
	// 구조물이 떠 보이지는 않을 만큼만 띄운다.
	// 겹친 면의 깜빡임은 없애면서 게임 중에 느껴지는 턱은 생기지 않는다.
	const float SurfaceZ = Placement.ElevationProfile == EFacilityElevationProfile::Ground
		? BaseGroundSurfaceZ : Placement.BaseElevationCm;
	Center.Z = SurfaceZ + 1.0f;
	return Center;
}

