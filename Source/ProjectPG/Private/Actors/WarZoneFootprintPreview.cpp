// ProjectPG-only visualization for multi-cell level-design footprints.
// 이 파일: 생성자·BeginPlay·Tick·흐름(TryReserveFootprint)·시설 자리 잡기·미리보기 그리기.
// 나머지 책임은 WarZoneFootprint/ 폴더의 파일들에 있다(붕괴·설계도·타일 올리기·그리기·게임 지점·출발 준비·특수 장소·검사).

#include "Actors/WarZoneFootprintPreview.h" // 언리얼 규칙: 클래스 이름과 같은 .cpp 는 자기 헤더를 맨 먼저 포함한다
#include "Net/UnrealNetwork.h"
#include "WarZoneFootprint/WarZoneFootprintPreviewInternal.h"
#include "WarZoneFootprint/PGMapVisualBuilder.h"
#include "WarZoneFootprint/PGRegionCollapse.h"
#include "WarZoneFootprint/PGMapTileSpawner.h"
#include "WarZoneFootprint/PGGameplayPointBuilder.h"
#include "WarZoneFootprint/PGMapVerifier.h"
#include "Objects/PGLevelDoorConverter.h"
#include "Robot/PGRobotCharacter.h"
#include "Components/LocalLightComponent.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// 시설 레벨의 점·스포트 조명 중 그림자를 드리우게 남겨 둘 수(레벨마다) — DA_PGMapVisuals 의 FacilityShadowLights 칸.
	// 왜(9/28 "중앙 건물 박살 낼 때 프레임 드랍"): 워존 중앙 건물(공장 팩 데모에서 떼 온 레벨)에 그림자 드리우는 조명이 52개 있었다.
	//   가상 그림자 맵(VSM)은 조명마다 그림자를 따로 그리는데, 건물이 부서질 때마다 그 자리 그림자를 조명 수만큼 다시 그렸다.
	//   화면 없는 두 사람 시험(PG.SmashCoreTest + PG.NetSmashTest, 8배 로봇이 건물을 부수며 지나감): 부수는 동안 그리기(GPU)
	//   38~40ms → 조명 그림자를 끄자 30~34ms. 에디터 화면의 "VSM 원패스 프로젝션 최대 라이트 오버플로" 경고도 이 조명들 때문이다.
	//   해·하늘빛 그림자는 그대로라 겉모습 차이는 실내 전등 그림자뿐이다. 칸에 1 이상 주면 그만큼은 남긴다.

	int32 TrimFacilityLightShadows(const ULevel* Level)
	{
		if (!IsValid(Level))
			return 0;
		int32 Kept = 0;
		int32 Trimmed = 0;
		const int32 Allowed = FMath::Max(0, UPGMapVisualSet::GetActive()->FacilityShadowLights);
		for (AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor))
				continue;
			TInlineComponentArray<ULocalLightComponent*> Lights(Actor);
			for (ULocalLightComponent* Light : Lights)
			{
				if (!IsValid(Light) || !Light->CastShadows)
					continue;
				if (Kept < Allowed)
				{
					++Kept;
					continue;
				}
				Light->SetCastShadows(false);
				++Trimmed;
			}
		}
		return Trimmed;
	}
}

AWarZoneFootprintPreview::AWarZoneFootprintPreview()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	// 멀티(2026-09-27): 맵 설계도(GridManifest)를 클라이언트에 보내려고 복제한다. 부품(HISM·타일)은 복제하지 않는다 —
	// 클라이언트가 설계도로 스스로 짓는다. 맵은 누구에게나 보여야 하므로 거리와 상관없이 항상 보낸다.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	SetNetUpdateFrequency(1.0f); // 설계도는 판마다 한 번 바뀐다. 자주 볼 필요가 없다

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
	// 땅은 부서지지 않는다. 거대 로봇의 "낮고 넓은 것은 부순다" 규칙(PGPhysicsUtil::TryKnockProp)이 20m 바닥 타일까지 부숴서
	// 밟고 지나간 자리마다 바닥이 꺼졌다(9/19 PIE). 소품 돌·갈대(Dressing)는 태그를 안 붙여 계속 날아간다.
	for (UPrimitiveComponent* Terrain : TArray<UPrimitiveComponent*>{ GroundHISM, WarZoneGroundHISM, TransitionGroundHISM,
		RoadSurfaceHISM, LakeBedHISM, LakeWaterHISM, MountainHISM, NavigationFloor })
		Terrain->ComponentTags.Add(PGPhysicsUtil::TerrainTag);
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
				Component->ComponentTags.Add(PGPhysicsUtil::TerrainTag);
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
		// 충돌이 없는 것(덤불·갈대)은 그림자를 끈다. 가상 그림자 맵은 인스턴스를 하나
		// 지울 때마다 해당 페이지가 무효화되는데, 지형 장식은 수천 개라 그 비용이 크다.
		// 돌·나무는 충돌이 있고 그림자가 실루엣을 만드니 그대로 둔다.
		Component->SetCastShadow(Spec.bCollides);
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
	static const TCHAR* ShoreComponentNames[] = {
		TEXT("ShoreCorner1HISM"), TEXT("ShoreCorner2AdjacentHISM"),
		TEXT("ShoreCorner2DiagonalHISM"), TEXT("ShoreCorner3HISM")
	};
	for (int32 ShoreIndex = 0; ShoreIndex < UE_ARRAY_COUNT(WzfpShoreMeshPaths); ++ShoreIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component =
			CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(
				ShoreComponentNames[ShoreIndex]);
		Component->SetupAttachment(SceneRoot);
		Component->ComponentTags.Add(PGPhysicsUtil::TerrainTag);
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
	// 탈것은 이 판을 무시한다. 왜: 판은 Z≈-10 의 평평한 판이라 움푹 파인 지형(Bowl, 최대 -260cm) 한가운데에 보이지 않는 바닥을 만들어
	// 차가 파인 곳 위를 공중부양하듯 지나갔다(9/19 PIE). 길찾기 생성은 Pawn 채널 막힘만 보므로(IsNavigationRelevant) AI 는 그대로다.
	// 탈것은 실제 땅(Ground HISM·지형 메시·타일·시설 바닥)만 밟는다.
	NavigationFloor->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
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

	// 책임별 협력 객체(검사·붕괴·그리기)를 만든다. 맵 액터는 이들을 순서대로 부르기만 한다(2026-09-26 한 책임 정리).
	CreateCollaborators();

	// 맵 정보 창구에 등록 — 스포너·피날레 등은 이 클래스를 찾지 않고 IPGMapInfo 로만 본다(LevelDesign/PGMapInfo.h).
	// 서버 여부와 상관없이 등록한다: 나중에 클라가 설계도로 맵을 만들 때도 같은 창구를 쓴다.
	if (UPGMapInfoSubsystem* MapInfo = GetWorld()->GetSubsystem<UPGMapInfoSubsystem>())
		MapInfo->RegisterMap(this);

	if (!HasAuthority())
	{
		// 클라이언트: 맵을 짓는 동안(한 번에 크게 지어 몇 초 멈춘다) 로딩 화면으로 가린다. 걷는 곳은 TickLocalPlayerReady.
		if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(this))
			Loading->ShowLoading(NSLOCTEXT("PGFlow", "EnteringField", "필드에 진입 중…"));
		// 클라이언트: 서버가 보낸 설계도로 짓는다. 설계도가 BeginPlay 보다 먼저 와 있으면 바로, 아니면 도착할 때(OnRep).
		if (GridManifest.IsValid())
			BuildFromManifest();
		return;
	}

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

// ---- 멀티: 맵 설계도 (2026-09-27) ----

int64 AWarZoneFootprintPreview::GetRaidSeed() const
{
	// 서버(혼자 할 때 포함): 게임모드의 판 시드. 클라이언트에는 게임모드가 없으므로 서버가 보낸 설계도의 시드.
	if (const AGameModePG* GameMode = GetWorld() ? Cast<AGameModePG>(GetWorld()->GetAuthGameMode()) : nullptr)
		return GameMode->GetMapGenerationSeed();
	return GridManifest.RaidSeed;
}

void AWarZoneFootprintPreview::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWarZoneFootprintPreview, GridManifest);
}

void AWarZoneFootprintPreview::FillGridManifest(const TMap<FIntPoint, AMapTile*>& TileByCell, float TileZ)
{
	FIntPoint MinCell(MAX_int32, MAX_int32);
	FIntPoint MaxCell(MIN_int32, MIN_int32);
	for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
	{
		MinCell = FIntPoint(FMath::Min(MinCell.X, Pair.Key.X), FMath::Min(MinCell.Y, Pair.Key.Y));
		MaxCell = FIntPoint(FMath::Max(MaxCell.X, Pair.Key.X), FMath::Max(MaxCell.Y, Pair.Key.Y));
	}
	FPGLogicalGridManifest Manifest;
	Manifest.RaidSeed = GetRaidSeed();
	Manifest.GridStep = GridStep;
	Manifest.TileZ = TileZ;
	Manifest.MinCell = MinCell;
	Manifest.Width = MaxCell.X - MinCell.X + 1;
	Manifest.Height = MaxCell.Y - MinCell.Y + 1;
	Manifest.Types.Init(255, Manifest.Width * Manifest.Height);
	for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
		if (IsValid(Pair.Value))
			Manifest.Types[(Pair.Key.Y - MinCell.Y) * Manifest.Width + (Pair.Key.X - MinCell.X)] = static_cast<uint8>(Pair.Value->GetType());
	// 생성기의 시작 구역 크기(원격 거점이 쓴다). 팀원 코드에 getter 를 더하지 않으려고 리플렉션으로 읽는다(PlaceRemoteOutpost 와 같은 방식).
	if (const AGameModePG* GameMode = Cast<AGameModePG>(GetWorld()->GetAuthGameMode()))
	{
		const UMapGeneratorComponent* Generator = GameMode->FindComponentByClass<UMapGeneratorComponent>();
		const FIntProperty* RangeProperty = FindFProperty<FIntProperty>(UMapGeneratorComponent::StaticClass(), TEXT("_startPositionRangeSize"));
		if (Generator && RangeProperty)
			Manifest.StartRangeSize = FMath::Max(1, RangeProperty->GetPropertyValue_InContainer(Generator));
	}
	GridManifest = Manifest;
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("Map manifest: server sends %dx%d cells from (%d,%d) seed=%lld step=%.0f bytes=%d"),
		Manifest.Width, Manifest.Height, MinCell.X, MinCell.Y, Manifest.RaidSeed, Manifest.GridStep, Manifest.Types.Num());
}

void AWarZoneFootprintPreview::OnRep_GridManifest()
{
	// BeginPlay 전에 오면 BeginPlay 가 짓는다(아직 협력 객체가 없다).
	if (HasActorBegunPlay())
		BuildFromManifest();
}

void AWarZoneFootprintPreview::BuildFromManifest()
{
	UWorld* World = GetWorld();
	if (bBuiltFromManifest || HasAuthority() || !World || !GridManifest.IsValid())
		return;
	bBuiltFromManifest = true;
	// 서버의 팀원 생성기가 만든 칸과 같은 자리·종류의 칸을 이 컴퓨터에만 숨겨 깐다. 아래 맵 짓기는 이 칸들을 읽는다 —
	// 서버와 같은 코드가 같은 입력으로 돌므로 같은 맵이 나온다(칸 종류·시설 자리·호수·타일·풀 모두).
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	for (int32 Y = 0; Y < GridManifest.Height; ++Y)
	{
		for (int32 X = 0; X < GridManifest.Width; ++X)
		{
			const uint8 Type = GridManifest.Types[Y * GridManifest.Width + X];
			if (Type == 255)
				continue;
			const FIntPoint Cell = GridManifest.MinCell + FIntPoint(X, Y);
			const FVector Location(Cell.X * GridManifest.GridStep, Cell.Y * GridManifest.GridStep, GridManifest.TileZ);
			AMapTile* Tile = World->SpawnActor<AMapTile>(AMapTile::StaticClass(), Location, FRotator::ZeroRotator, Params);
			if (!IsValid(Tile))
				continue;
			Tile->SetType(static_cast<ETileType>(Type));
			Tile->SetActorHiddenInGame(true);
			Tile->SetActorEnableCollision(false);
			ClientGridTiles.Add(Tile);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("Map manifest: client builds from %d cells seed=%lld"), ClientGridTiles.Num(), GridManifest.RaidSeed);
	TryReserveFootprint();
}

void AWarZoneFootprintPreview::TickLocalPlayerReady()
{
	UWorld* World = GetWorld();
	// 혼자 하는 판은 StartSinglePlayerValidation 이 한다. 전용 서버에는 이 컴퓨터 사람이 없다.
	if (bLocalPlayerReady || !IsValid(World) || !World->IsGameWorld()
		|| GetNetMode() == NM_Standalone || GetNetMode() == NM_DedicatedServer)
		return;
	// 맵이 다 지어졌나: 클라는 설계도로 지은 뒤, 듣기 서버(서버도 사람)는 서버가 사람들을 세우기 시작한 뒤.
	if (HasAuthority() ? !bStartedSinglePlayerValidation : !bBuiltFromManifest)
		return;
	APlayerController* LocalPC = World->GetFirstPlayerController(); // 클라·듣기 서버에서는 이 컴퓨터 사람
	APawn* Pawn = IsValid(LocalPC) && LocalPC->IsLocalController() ? LocalPC->GetPawn() : nullptr;
	if (!IsValid(Pawn) || Pawn->IsA<ADefaultPawn>())
	{
		LocalPawnSeenAt = -1.0;
		return;
	}
	// 캐릭터를 받은 직후에는 아직 시작 자리로 옮겨지기 전일 수 있다(게임모드가 맵 가운데에 만든 뒤 서버가 옮긴다).
	// 그래서 "시작 자리(Spawn 지점) 30m 안에 왔나" 를 본다 — 클라도 같은 맵 짓기로 같은 지점 목록을 갖고 있다.
	// 0.5초만 기다렸을 때는 한 사람이 맵 가운데에서 로딩이 걷힌 뒤 시작 자리로 튀었다(9/27 시험). 15초가 지나면 그냥 걷는다.
	const double Now = World->GetTimeSeconds();
	if (LocalPawnSeenAt < 0.0)
		LocalPawnSeenAt = Now;
	const FVector At = Pawn->GetActorLocation();
	const bool bAtSeat = LevelDesignPoints.ContainsByPredicate([&At](const FLevelDesignPoint& Point)
	{
		return Point.Type == ELevelDesignPointType::Spawn && FVector::DistSquared2D(Point.WorldLocation, At) < FMath::Square(3000.0f);
	});
	if ((!bAtSeat && Now - LocalPawnSeenAt < 15.0) || Now - LocalPawnSeenAt < 0.3)
		return;
	bLocalPlayerReady = true;
	HideLoadingWhenFacilitiesVisible(TEXT("multiplayer: map built and local character placed"));
	LocalPC->SetInputMode(FInputModeGameOnly());
	LocalPC->bShowMouseCursor = false;
	UE_LOG(LogTemp, Display, TEXT("Local player ready (%s): %s at %s"),
		HasAuthority() ? TEXT("listen host") : TEXT("client"), *GetNameSafe(Pawn), *Pawn->GetActorLocation().ToCompactString());

	// 멀티 확인용(9/27): 클라이언트 화면에 물건이 제 모양으로 왔나 센다. 10초 뒤(물건 복제가 다 올 시간) 한 번.
	// "모양 없는 물건" 이 많으면 겉모습 복제(APGInteractableActorBase::CatalogLook)가 안 온 것이다 — 헤드리스 멀티 시험이 이 줄을 본다.
	if (!HasAuthority())
	{
		FTimerHandle AuditTimer;
		GetWorldTimerManager().SetTimer(AuditTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			int32 Total = 0, WithLook = 0, Visible = 0;
			TMap<FName, int32> Missing;
			for (TActorIterator<APGInteractableActorBase> It(GetWorld()); It; ++It)
			{
				++Total;
				bool bHasMesh = false;
				It->ForEachComponent<UStaticMeshComponent>(false, [&bHasMesh](const UStaticMeshComponent* Mesh)
				{
					bHasMesh |= Mesh->GetStaticMesh() != nullptr && Mesh->IsVisible();
				});
				Visible += bHasMesh ? 1 : 0;
				if (!It->GetObjectId().IsNone())
					++WithLook;
				else if (!bHasMesh)
					Missing.FindOrAdd(It->GetClass()->GetFName())++;
			}
			FString MissingList;
			for (const TPair<FName, int32>& Pair : Missing)
				MissingList += FString::Printf(TEXT("%s=%d "), *Pair.Key.ToString(), Pair.Value);
			UE_LOG(LogTemp, Display, TEXT("Client object audit: interactables=%d with_object_id=%d with_visible_mesh=%d no_id_no_mesh=[%s]"),
				Total, WithLook, Visible, *MissingList);
			// 몬스터·로봇 모양(멀티 점검 A5): 메시 이름별 수와 로봇 역할·크기. 서버 로그의 CombatSpawner 줄과 비교한다.
			TMap<FString, int32> MonsterMeshes;
			FString Robots;
			for (TActorIterator<APGMonsterCharacter> It(GetWorld()); It; ++It)
			{
				const USkeletalMeshComponent* Body = It->GetMesh();
				MonsterMeshes.FindOrAdd(GetNameSafe(Body ? Body->GetSkeletalMeshAsset() : nullptr))++;
				if (const APGRobotCharacter* Robot = Cast<APGRobotCharacter>(*It))
					Robots += FString::Printf(TEXT("%s(%s scale %.1f, capsule %.0f, mesh z %.0f, actor z %.0f, mesh world z %.0f) "), *It->GetName(), Robot->IsRideable() ? TEXT("ride") : TEXT("boss"), It->GetActorScale3D().X,
						It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Body ? Body->GetRelativeLocation().Z : 0.0f, It->GetActorLocation().Z, Body ? Body->GetComponentLocation().Z : 0.0f);
			}
			FString MeshList;
			for (const TPair<FString, int32>& Pair : MonsterMeshes)
				MeshList += FString::Printf(TEXT("%s=%d "), *Pair.Key, Pair.Value);
			UE_LOG(LogTemp, Display, TEXT("Client monster audit: meshes=[%s] robots=[%s]"), *MeshList, *Robots);
		}), 10.0f, false);
	}
}

void AWarZoneFootprintPreview::CreateCollaborators()
{
	if (!VisualBuilder)
	{
		VisualBuilder = NewObject<UPGMapVisualBuilder>(this, TEXT("VisualBuilder"));
		VisualBuilder->Init(this);
	}
	if (!TileSpawner)
	{
		TileSpawner = NewObject<UPGMapTileSpawner>(this, TEXT("TileSpawner"));
		TileSpawner->Init(this);
	}
	if (!PointBuilder)
	{
		PointBuilder = NewObject<UPGGameplayPointBuilder>(this, TEXT("PointBuilder"));
		PointBuilder->Init(this);
	}
	if (!Collapse)
	{
		Collapse = NewObject<UPGRegionCollapse>(this, TEXT("Collapse"));
		Collapse->Init(this);
	}
	if (!Verifier)
	{
		Verifier = NewObject<UPGMapVerifier>(this, TEXT("Verifier"));
		Verifier->Init(this);
	}
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGRegionCollapse(협력 객체)가 한다.
void AWarZoneFootprintPreview::CollapseRegion(const FVector& WorldCentre, float RadiusCm, float Seconds)
{
	// 멀티(9/27): 서버면 방송 — 서버 자신도 방송을 받아 같은 함수로 무너뜨린다. 혼자 하는 판은 방송이 곧 바로 부르기다.
	if (HasAuthority())
		MulticastCollapseRegion(WorldCentre, RadiusCm, Seconds);
}

void AWarZoneFootprintPreview::MulticastCollapseRegion_Implementation(FVector_NetQuantize WorldCentre, float RadiusCm, float Seconds)
{
	if (Collapse)
		Collapse->CollapseRegion(WorldCentre, RadiusCm, Seconds);
	if (GetNetMode() == NM_Client)
		UE_LOG(LogTemp, Display, TEXT("PGCollapse: collapsing on this screen at %s (%.0fm)"), *WorldCentre.ToCompactString(), RadiusCm * 0.01f);
}

void AWarZoneFootprintPreview::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
		if (UPGMapInfoSubsystem* MapInfo = World->GetSubsystem<UPGMapInfoSubsystem>())
			MapInfo->UnregisterMap(this);
	Super::EndPlay(EndPlayReason);
}

void AWarZoneFootprintPreview::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bCollapsing)
		Collapse->TickRegionCollapse(DeltaSeconds);
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
	TickLocalPlayerReady();
	if (!HasAuthority() && bBuiltFromManifest && !bClientDoorsDone && GetWorld()->GetTimeSeconds() >= NextClientDoorHideAt)
	{
		NextClientDoorHideAt = GetWorld()->GetTimeSeconds() + 2.0;
		const bool bAllLoaded = TileSpawner->AreAllFacilityLevelsLoaded(); // 먼저 재고 숨긴다 — 다 로드된 상태에서 여러 번 돈다
		ClientDoorsHidden += PGLevelDoorConverter::HideConvertedDoorsOnClient(GetWorld());
		// "로드됨" 표시가 레벨 안 액터가 다 붙기 전에 켜질 때가 있어(9/27 시험: 한 클라만 0개) 다 로드된 뒤 다섯 번(10초) 더 본다.
		if (bAllLoaded && ++ClientDoorPassesAfterLoad >= 5)
		{
			bClientDoorsDone = true;
			UE_LOG(LogTemp, Display, TEXT("Client level doors: done, hid %d original door mesh(es) in total"), ClientDoorsHidden);
		}
	}
	// 시설 레벨 조명 그림자 줄이기(화면을 그리는 모든 컴퓨터 — 위 TrimFacilityLightShadows 주석). 전용 서버는 그리지 않는다.
	if (GetNetMode() != NM_DedicatedServer)
	{
		for (int32 Index = 0; Index < FacilityDesignLevelInstances.Num(); ++Index)
		{
			const ULevelStreamingDynamic* Instance = FacilityDesignLevelInstances[Index];
			if (LightTrimmedFacilityIndices.Contains(Index) || !IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
				continue;
			LightTrimmedFacilityIndices.Add(Index);
			if (const int32 Trimmed = TrimFacilityLightShadows(Instance->GetLoadedLevel()); Trimmed > 0)
				UE_LOG(LogTemp, Display, TEXT("Facility lights: %d point/spot light shadows switched off in %s (DA_PGMapVisuals FacilityShadowLights keeps %d)"),
					Trimmed, *Instance->GetWorldAssetPackageName(), UPGMapVisualSet::GetActive()->FacilityShadowLights);
		}
	}
	// 클라이언트는 여기까지만: 아래는 전부 서버의 일이다(경계 밖 되밀기·연료 다시 놓기·검사·플레이어 배치·시설 로그).
	// 캐릭터 위치·물건은 서버가 정해 복제해 준다. 클라이언트의 맵은 BuildFromManifest 가 한 번 짓고 끝난다.
	if (!HasAuthority())
		return;

	// 맵 밖으로 나간 플레이어 되밀기. 0.1초 간격 Tick 이라 따로 시간을 재지 않는다.
	VisualBuilder->EnforceMapBoundary();
	TickStarterFuelRespawn(DeltaSeconds);
	if (CollapsedAreas.Num() > 0 && HasAuthority() && GetWorld()->GetTimeSeconds() >= NextCollapsedSweepAt)
	{
		NextCollapsedSweepAt = GetWorld()->GetTimeSeconds() + 0.5;
		Collapse->SweepCollapsedAreas();
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
		if (FacilityPlacements.IsValidIndex(Index) && FacilityPlacements[Index].VisualSet == EFacilityVisualSet::RuralHideout)
			AttachBoatExits(LoadedLevel);
	}
	if (!bLoggedAllFacilityDesignLevelsLoaded
		&& !FacilityPlacements.IsEmpty()
		&& TileSpawner->AreAllFacilityLevelsLoaded())
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
	PointBuilder->ResolveGameplayPointSafety();
	PointBuilder->BroadcastLevelDesignPointsWhenReady();
	Verifier->VerifyTacticalLayoutQuality();
	Verifier->VerifyTravelCoverDensity();
	Verifier->VerifyGameplayPointDistribution();
	Verifier->VerifyNavigation();
	Verifier->VerifyCriticalRoutes();
	Verifier->VerifyTraversableElevation();
	Verifier->VerifyCoplanarSurfaces();
	Verifier->VerifyPCGDressing();
	// Dynamic nav modifiers can finish rebuilding a frame after the first
	// projection audit. Retry until a complete local validation route exists.
	if (bLoggedNavigation && !bStartedSinglePlayerValidation)
		StartSinglePlayerValidation();
	// 멀티: 폰이 준비된 플레이어를 각자 시작 지역에 세운다(새로 들어온 사람만). WarZoneFootprintPreview_SpawnRegions.cpp
	PlaceJoinedPlayers();
	Verifier->VerifySinglePlayerValidation();
	Verifier->VerifyLocalPerformance(DeltaSeconds);

	// 플레이어 주변 길찾기 막이 갱신: 검증 캐릭터가 아니라 "지금 플레이어가 조종하는 폰"(팀 캐릭터·탈것 포함) 기준.
	if (APawn* PlayerPawn = GetValidationPlayerPawn(); IsValid(PlayerPawn) && IsValid(PlayerNavigationBlockers))
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
	TryIssueSinglePlayerValidationMove();
}

void AWarZoneFootprintPreview::TryReserveFootprint()
{
	// 9/28: 596줄이던 것을 단계 함수로 나눴다(동작·순서 그대로). 1 격자 읽기 → 2 워존 시설 → 3 출구 검문소 → 4 테마 구역 → 5 짓기.
	TArray<AMapTile*> AllTiles;
	TMap<FIntPoint, AMapTile*> WarZoneByCell;
	TMap<FIntPoint, AMapTile*> TileByCell;
	if (!ReadLogicalGrid(AllTiles, WarZoneByCell, TileByCell))
		return;

	TSet<FIntPoint> SelectedFacilityCells;
	int32 CampCount = 0;
	if (!ReserveWarZoneFacilities(AllTiles.Num(), WarZoneByCell, TileByCell, SelectedFacilityCells, CampCount))
		return;
	const bool bFoundCheckpoint = ReserveExitCheckpoint(TileByCell, SelectedFacilityCells);
	ReserveThemedDistricts(TileByCell, SelectedFacilityCells);

	// 5단계: 설계도(칸마다 타일) → 타일·시설·꾸밈을 실제로 세운다.
	BuildTileDesignPlacements(TileByCell);

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
	VisualBuilder->BuildLightweightWorldVisuals();
	TileSpawner->SpawnRuntimeBlueprintTiles();
	ClearSpawnSurroundings();
	PointBuilder->BuildGameplayPointMarkers();
	// Runs alongside runtime Blueprint tiles: their HISM dressing covers per-tile
	// props, while this pass scatters grass/shrub clumps across meadow, scrub,
	// ruin and open-ground cells to break up the 20m tile seams between them.
	VisualBuilder->BuildPCGDressingGraph();
	NavigationValidationStartTimeSeconds = FPlatformTime::Seconds();
	FacilityDesignLevelInstances.Reset();
	FacilityLoadRequestTimeSeconds.Reset();
	LoggedFacilityDesignLevelIndices.Reset();
	for (int32 FacilityIndex = 0; FacilityIndex < FacilityPlacements.Num(); ++FacilityIndex)
		TileSpawner->LoadFacilityDesignLevel(FacilityPlacements[FacilityIndex], FacilityIndex);
	VisualBuilder->BuildBorderMountains();
	VisualBuilder->BuildGroundSkirt();
	// 안개는 가리기만 한다. 실제로 못 나가게 막는 것은 이쪽이다.
	VisualBuilder->BuildMapBoundaryWall();
	SpawnLakeBoats();
	// 호수·시설·지형·출구가 모두 정해진 뒤라야 "빈 모서리" 를 판단할 수 있다. 타일 소품도 이미 묶여 있어 걷어 낼 수 있다.
	PlaceRemoteOutpost(TileByCell);

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
		AnchorCell.X, AnchorCell.Y, CampCount, // AnchorCell = 가운데 창고 자리(2단계)
		bFoundCheckpoint ? TEXT("true") : TEXT("false"), GridStep);
}

// 1단계: 형님 맵 생성기가 만든 논리 격자(AMapTile)를 읽어 칸 → 타일 표로 만든다. 격자가 아직 없으면 false(다음 Tick 에 다시).
// 9/28 TryReserveFootprint 에서 떼어 냄(동작 그대로).
bool AWarZoneFootprintPreview::ReadLogicalGrid(TArray<AMapTile*>& AllTiles, TMap<FIntPoint, AMapTile*>& WarZoneByCell, TMap<FIntPoint, AMapTile*>& TileByCell)
{
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
		return false;

	UniqueX.Sort();
	GridStep = 0.0f;
	for (int32 Index = 1; Index < UniqueX.Num(); ++Index)
	{
		const float Difference = UniqueX[Index] - UniqueX[Index - 1];
		if (Difference > KINDA_SMALL_NUMBER && (GridStep <= 0.0f || Difference < GridStep))
			GridStep = Difference;
	}

	if (GridStep <= 0.0f)
		return false;

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
	// 멀티: 서버는 여기서 읽은 논리 격자를 설계도로 채워 클라이언트에 보낸다. 아래 과정이 쓰는 입력은 이것과 시드뿐이다.
	if (HasAuthority() && !TileByCell.IsEmpty())
		FillGridManifest(TileByCell, AllTiles[0]->GetActorLocation().Z);
	return true;
}

// 2단계: 워존에 가운데 창고(3x3)·위성 캠프(2x2)·긴 막사(2x1)·참호(4x1)를 잡는다. 자리가 없으면 false.
// 9/28 TryReserveFootprint 에서 떼어 냄(동작 그대로).
bool AWarZoneFootprintPreview::ReserveWarZoneFacilities(const int32 AllTileCount, TMap<FIntPoint, AMapTile*>& WarZoneByCell, TMap<FIntPoint, AMapTile*>& TileByCell,
	TSet<FIntPoint>& SelectedFacilityCells, int32& OutCampCount)
{
	TArray<FIntPoint> Candidates;
	TArray<FIntPoint> CompoundCandidates;

	for (const TPair<FIntPoint, AMapTile*>& Pair : WarZoneByCell)
	{
		const FIntPoint Cell = Pair.Key;
		const bool bFits2x2 = WarZoneByCell.Contains(Cell + FIntPoint(1, 0))
			&& WarZoneByCell.Contains(Cell + FIntPoint(0, 1))
			&& WarZoneByCell.Contains(Cell + FIntPoint(1, 1));
		if (bFits2x2)
			Candidates.Add(Cell);

		bool bFitsCore = true;
		for (int32 X = 0; X < WarZoneCoreFootprint.X && bFitsCore; ++X)
		{
			for (int32 Y = 0; Y < WarZoneCoreFootprint.Y; ++Y)
			{
				if (!WarZoneByCell.Contains(Cell + FIntPoint(X, Y)))
				{
					bFitsCore = false;
					break;
				}
			}
		}
		if (bFitsCore)
			CompoundCandidates.Add(Cell);
	}

	if (Candidates.IsEmpty() || CompoundCandidates.IsEmpty())
	{
		// This runs from Tick and used to return silently, so a WarZone that came
		// out too small to hold the compound produced an empty world and not one
		// line of explanation. Report it once instead of every frame.
		if (!bLoggedMissingWarZoneFootprint)
		{
			bLoggedMissingWarZoneFootprint = true;
			UE_LOG(LogTemp, Error,
				TEXT("WarZone footprint unavailable: tiles=%d warzone_cells=%d fits_2x2=%d fits_core=%d ")
				TEXT("grid_step=%.1f - generation cannot proceed without a %dx%d WarZone block"),
				AllTileCount, WarZoneByCell.Num(), Candidates.Num(), CompoundCandidates.Num(), GridStep,
				WarZoneCoreFootprint.X, WarZoneCoreFootprint.Y);
		}
		return false;
	}

	auto SortByCenterDistance = [](const FIntPoint& A, const FIntPoint& B)
	{
		const int64 DistanceA = FMath::Square(static_cast<int64>(A.X))
			+ FMath::Square(static_cast<int64>(A.Y));
		const int64 DistanceB = FMath::Square(static_cast<int64>(B.X))
			+ FMath::Square(static_cast<int64>(B.Y));
		if (DistanceA != DistanceB)
			return DistanceA < DistanceB;
		if (A.X != B.X)
			return A.X < B.X;
		return A.Y < B.Y;
	};
	Candidates.Sort(SortByCenterDistance);
	CompoundCandidates.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		// Compare footprint centres, not anchors, so the largest facility actually
		// occupies the middle of the authoritative WarZone.
		const int64 CenterAX = static_cast<int64>(A.X) + WarZoneCoreCentreOffset.X;
		const int64 CenterAY = static_cast<int64>(A.Y) + WarZoneCoreCentreOffset.Y;
		const int64 CenterBX = static_cast<int64>(B.X) + WarZoneCoreCentreOffset.X;
		const int64 CenterBY = static_cast<int64>(B.Y) + WarZoneCoreCentreOffset.Y;
		const int64 DistanceA = CenterAX * CenterAX + CenterAY * CenterAY;
		const int64 DistanceB = CenterBX * CenterBX + CenterBY * CenterBY;
		if (DistanceA != DistanceB)
			return DistanceA < DistanceB;
		if (A.X != B.X)
			return A.X < B.X;
		return A.Y < B.Y;
	});

	// Reserve one 3x3 anchor facility, then several well-spaced 2x2 satellite
	// camps. AMapTile remains the logical authority; the future server manifest
	// only needs anchor, footprint, rotation, visual type and stable seed.
	TArray<FIntPoint> SelectedFacilityAnchors;
	const FIntPoint CompoundAnchor = CompoundCandidates[0];
	TArray<FIntPoint> CompoundOccupiedCells;
	for (int32 X = 0; X < WarZoneCoreFootprint.X; ++X)
	{
		for (int32 Y = 0; Y < WarZoneCoreFootprint.Y; ++Y)
		{
			const FIntPoint Cell = CompoundAnchor + FIntPoint(X, Y);
			SelectedFacilityCells.Add(Cell);
			CompoundOccupiedCells.Add(Cell);
		}
	}
	auto CanSelectFacility = [&SelectedFacilityCells](const FIntPoint& Candidate)
	{
		for (const FIntPoint& Offset : FootprintOffsets)
		{
			const FIntPoint Cell = Candidate + Offset;
			if (SelectedFacilityCells.Contains(Cell))
				return false;
			for (int32 X = -2; X <= 2; ++X)
				for (int32 Y = -2; Y <= 2; ++Y)
					if (SelectedFacilityCells.Contains(Cell + FIntPoint(X, Y)))
						return false;
		}
		return true;
	};
	auto SelectFacility = [&SelectedFacilityAnchors, &SelectedFacilityCells](const FIntPoint& Anchor)
	{
		SelectedFacilityAnchors.Add(Anchor);
		for (const FIntPoint& Offset : FootprintOffsets)
			SelectedFacilityCells.Add(Anchor + Offset);
	};
	while (SelectedFacilityAnchors.Num() < 3)
	{
		bool bFoundNext = false;
		int64 BestMinimumDistanceSquared = -1;
		FIntPoint BestCandidate = FIntPoint::ZeroValue;
		for (const FIntPoint& Candidate : Candidates)
		{
			if (!CanSelectFacility(Candidate))
				continue;
			int64 MinimumDistanceSquared = TNumericLimits<int64>::Max();
			for (const FIntPoint& Existing : SelectedFacilityAnchors)
			{
				const int64 DX = Candidate.X - Existing.X;
				const int64 DY = Candidate.Y - Existing.Y;
				MinimumDistanceSquared = FMath::Min(MinimumDistanceSquared, DX * DX + DY * DY);
			}
			if (MinimumDistanceSquared > BestMinimumDistanceSquared)
			{
				bFoundNext = true;
				BestMinimumDistanceSquared = MinimumDistanceSquared;
				BestCandidate = Candidate;
			}
		}
		if (!bFoundNext)
			break;
		SelectFacility(BestCandidate);
	}

	if (SelectedFacilityAnchors.IsEmpty())
		return false;

	AnchorCell = CompoundAnchor;
	ReservedTiles.Reset();
	FacilityPlacements.Reset();
	ReserveFacility(
		EFacilityVisualSet::Warehouse,
		CompoundAnchor,
		WarZoneCoreFootprint,
		0,
		CompoundOccupiedCells,
		TileByCell);
	for (int32 FacilityIndex = 0; FacilityIndex < SelectedFacilityAnchors.Num(); ++FacilityIndex)
	{
		const FIntPoint FacilityAnchor = SelectedFacilityAnchors[FacilityIndex];
		TArray<FIntPoint> OccupiedCells;
		for (const FIntPoint& Offset : FootprintOffsets)
			OccupiedCells.Add(FacilityAnchor + Offset);
		ReserveFacility(
			EFacilityVisualSet::Yard,
			FacilityAnchor,
			FIntPoint(2, 2),
			FacilityIndex % 4,
			OccupiedCells,
			TileByCell);
	}

	// The design contract also needs genuinely multi-cell shapes, not only square
	// compounds. Reserve two 2x1 interiors and one 4x1 linear encounter while the
	// brother's WarZone cells remain the authoritative eligibility mask.
	auto ReserveBestRect = [this, &WarZoneByCell, &TileByCell, &SelectedFacilityCells](
		EFacilityVisualSet VisualSet,
		FIntPoint Footprint,
		int32 RotationQuarterTurns)
	{
		TArray<FIntPoint> SearchCells;
		WarZoneByCell.GetKeys(SearchCells);
		SearchCells.Sort([](const FIntPoint& A, const FIntPoint& B)
		{
			const int64 DA = static_cast<int64>(A.X) * A.X + static_cast<int64>(A.Y) * A.Y;
			const int64 DB = static_cast<int64>(B.X) * B.X + static_cast<int64>(B.Y) * B.Y;
			return DA != DB ? DA < DB : (A.X != B.X ? A.X < B.X : A.Y < B.Y);
		});

		for (const FIntPoint& Anchor : SearchCells)
		{
			TArray<FIntPoint> Occupied;
			bool bValid = true;
			for (int32 X = 0; X < Footprint.X && bValid; ++X)
			{
				for (int32 Y = 0; Y < Footprint.Y; ++Y)
				{
					const FIntPoint Cell = Anchor + FIntPoint(X, Y);
					if (!WarZoneByCell.Contains(Cell)) { bValid = false; break; }
					for (int32 PadX = -2; PadX <= 2 && bValid; ++PadX)
						for (int32 PadY = -2; PadY <= 2; ++PadY)
							if (SelectedFacilityCells.Contains(Cell + FIntPoint(PadX, PadY)))
							{ bValid = false; break; }
					Occupied.Add(Cell);
				}
			}
			if (!bValid)
				continue;
			for (const FIntPoint& Cell : Occupied)
				SelectedFacilityCells.Add(Cell);
			ReserveFacility(VisualSet, Anchor, Footprint, RotationQuarterTurns, Occupied, TileByCell);
			return true;
		}
		return false;
	};
	ReserveBestRect(EFacilityVisualSet::LongBarracks, FIntPoint(2, 1), 0);
	ReserveBestRect(EFacilityVisualSet::LongBarracks, FIntPoint(1, 2), 1);
	ReserveBestRect(EFacilityVisualSet::LinearTrench, FIntPoint(4, 1), 0);
	OutCampCount = SelectedFacilityAnchors.Num();
	return true;
}

// 3단계: 출구 검문소(1x2) 하나를 잡는다. 찾았으면 true. 9/28 TryReserveFootprint 에서 떼어 냄(동작 그대로).
bool AWarZoneFootprintPreview::ReserveExitCheckpoint(TMap<FIntPoint, AMapTile*>& TileByCell, TSet<FIntPoint>& SelectedFacilityCells)
{
	bool bFoundCheckpoint = false;
	float BestCheckpointDistanceSquared = TNumericLimits<float>::Max();
	FIntPoint CheckpointAnchor = FIntPoint::ZeroValue;
	FIntPoint CheckpointFootprint(1, 2);
	int32 CheckpointRotationQuarterTurns = 0;
	TArray<FIntPoint> CheckpointOccupiedCells;
	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(0, 1),
		FIntPoint(-1, 0), FIntPoint(0, -1)
	};

	TArray<FIntPoint> CheckpointSearchCells;
	TileByCell.GetKeys(CheckpointSearchCells);
	CheckpointSearchCells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	});
	for (const FIntPoint& SearchCell : CheckpointSearchCells)
	{
		AMapTile* const* SearchTilePtr = TileByCell.Find(SearchCell);
		if (SearchTilePtr == nullptr || !IsValid(*SearchTilePtr)
			|| (*SearchTilePtr)->GetType() != ETileType::Obstacle)
			continue;

		for (const FIntPoint& Direction : Directions)
		{
			const FIntPoint NeighborCell = SearchCell + Direction;
			if (SelectedFacilityCells.Contains(SearchCell)
				|| SelectedFacilityCells.Contains(NeighborCell))
			{
				continue;
			}
			AMapTile* const* NeighborPtr = TileByCell.Find(NeighborCell);
			if (NeighborPtr == nullptr || (*NeighborPtr)->GetType() != ETileType::Road)
				continue;

			const FVector Center = ((*SearchTilePtr)->GetActorLocation()
				+ (*NeighborPtr)->GetActorLocation()) * 0.5f;
			const float DistanceSquared = FVector2D(Center.X, Center.Y).SizeSquared();
			if (DistanceSquared >= BestCheckpointDistanceSquared)
				continue;

			bFoundCheckpoint = true;
			BestCheckpointDistanceSquared = DistanceSquared;
			CheckpointAnchor = FIntPoint(
				FMath::Min(SearchCell.X, NeighborCell.X),
				FMath::Min(SearchCell.Y, NeighborCell.Y));
			CheckpointRotationQuarterTurns = Direction.X != 0 ? 1 : 0;
			CheckpointFootprint = FIntPoint(1, 2);
			CheckpointOccupiedCells = { SearchCell, NeighborCell };
		}
	}

	if (bFoundCheckpoint)
	{
		ReserveFacility(
			EFacilityVisualSet::Checkpoint,
			CheckpointAnchor,
			CheckpointFootprint,
			CheckpointRotationQuarterTurns,
			CheckpointOccupiedCells,
			TileByCell);
	}
	return bFoundCheckpoint;
}

// 4단계: 전투 구역 밖에 테마 구역 셋(도심 6x6·공장 2x2·호숫가 마을 2x2)을 잡는다. 9/28 TryReserveFootprint 에서 떼어 냄(동작 그대로).
void AWarZoneFootprintPreview::ReserveThemedDistricts(TMap<FIntPoint, AMapTile*>& TileByCell, TSet<FIntPoint>& SelectedFacilityCells)
{
	// Reserve three themed districts outside the combat core.  They deliberately
	// consume ordinary None cells: the brother's metaball/road result stays the
	// authority, while the design manifest replaces only open visual cells with a
	// multi-cell POI.  Roads to each entrance are added by BuildTileDesignPlacements.
	int32 MinCellX = MAX_int32;
	int32 MinCellY = MAX_int32;
	int32 MaxCellX = MIN_int32;
	int32 MaxCellY = MIN_int32;
	FIntPoint SpawnCellSum = FIntPoint::ZeroValue;
	int32 SpawnCellCount = 0;
	for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
	{
		MinCellX = FMath::Min(MinCellX, Pair.Key.X);
		MinCellY = FMath::Min(MinCellY, Pair.Key.Y);
		MaxCellX = FMath::Max(MaxCellX, Pair.Key.X);
		MaxCellY = FMath::Max(MaxCellY, Pair.Key.Y);
		if (IsValid(Pair.Value) && Pair.Value->GetType() == ETileType::Spawn)
		{
			SpawnCellSum += Pair.Key;
			++SpawnCellCount;
		}
	}
	GridCellSpan = FMath::Max(MaxCellX - MinCellX + 1, MaxCellY - MinCellY + 1);
	// Recast is fed by this single floor rather than by geometry exported from every
	// tile, so it has to cover the generated grid and no more. Sized for a fixed
	// 900 m it left walkable void hanging off the edge of a smaller map.
	if (IsValid(NavigationFloor) && GridCellSpan > 0)
	{
		const float NavigationFloorScale = GridCellSpan * DesignCellSize / 100.0f;
		NavigationFloor->SetRelativeScale3D(
			FVector(NavigationFloorScale, NavigationFloorScale, 0.2f));
	}

	const FIntPoint AverageSpawnCell = SpawnCellCount > 0
		? FIntPoint(SpawnCellSum.X / SpawnCellCount, SpawnCellSum.Y / SpawnCellCount)
		: FIntPoint(MinCellX, MinCellY);
	auto LerpCell = [](int32 MinValue, int32 MaxValue, float Alpha)
	{
		return FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinValue), static_cast<float>(MaxValue), Alpha));
	};
	const FIntPoint DowntownTarget(
		AverageSpawnCell.X + FMath::Sign(-AverageSpawnCell.X) * 6,
		AverageSpawnCell.Y + FMath::Sign(-AverageSpawnCell.Y) * 6);
	const FIntPoint FactoryTarget(LerpCell(MinCellX, MaxCellX, 0.76f), LerpCell(MinCellY, MaxCellY, 0.26f));
	// The rural settlement is a lakeside hamlet - cabins, a pier and two rowing
	// boats. Dropped in the middle of a dry field it reads as a diorama someone
	// parked there, which is what the first placement looked like. Aim it just
	// inland of the border lake's waterline so the pier has water to sit on and the
	// boats have somewhere to be. ReserveThemedDistrict picks the nearest legal 2x2
	// to this point, and flooding never touches reserved cells, so the settlement
	// itself always ends up on dry land.
	const int64 LakeRaidSeed = GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
	const FBorderLake RuralLake = GetBorderLake(
		LakeRaidSeed, FIntPoint(MinCellX, MinCellY), FIntPoint(MaxCellX, MaxCellY),
		BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	const FVector2D InlandDirection =
		(FVector2D((MinCellX + MaxCellX) * 0.5f, (MinCellY + MaxCellY) * 0.5f)
			- RuralLake.CentreCell).GetSafeNormal();
	const FVector2D RuralShorePoint =
		RuralLake.CentreCell + InlandDirection * (RuralLake.Radius + 1.5f);
	const FIntPoint RuralTarget(
		FMath::Clamp(FMath::RoundToInt(RuralShorePoint.X), MinCellX, MaxCellX),
		FMath::Clamp(FMath::RoundToInt(RuralShorePoint.Y), MinCellY, MaxCellY));
	// Cells-to-nearest-road for every cell, as one multi-source breadth-first sweep
	// from the whole network at once. Facility placement used to score only the
	// distance to its thematic target, so compounds landed an average of 13 cells
	// from any road and the access-spur pass then had to drag a 260 m service lane
	// out to each one - or give up, which it did for 7 to 10 of the 11 facilities.
	// Paying for the road afterwards was the wrong order; sitting near one is free.
	TMap<FIntPoint, int32> RoadDistanceByCell;
	{
		TArray<FIntPoint> Frontier;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
		{
			if (!IsValid(Pair.Value))
				continue;
			// The WarZone is deliberately excluded even though it is traversable:
			// it is a wide region, and counting it as road would mark most of the
			// middle of the map as road-adjacent and defeat the whole term.
			const ETileType Type = Pair.Value->GetType();
			if (Type == ETileType::Road || Type == ETileType::Spawn
				|| Type == ETileType::Exit || Type == ETileType::Obstacle)
			{
				RoadDistanceByCell.Add(Pair.Key, 0);
				Frontier.Add(Pair.Key);
			}
		}
		const FIntPoint RoadDistanceSteps[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (int32 Index = 0; Index < Frontier.Num(); ++Index)
		{
			const FIntPoint Current = Frontier[Index];
			const int32 NextDistance = RoadDistanceByCell[Current] + 1;
			for (const FIntPoint& Step : RoadDistanceSteps)
			{
				const FIntPoint Neighbor = Current + Step;
				if (!TileByCell.Contains(Neighbor) || RoadDistanceByCell.Contains(Neighbor))
					continue;
				RoadDistanceByCell.Add(Neighbor, NextDistance);
				Frontier.Add(Neighbor);
			}
		}
	}

	auto ReserveThemedDistrict = [this, &TileByCell, &SelectedFacilityCells, &RoadDistanceByCell](
		EFacilityVisualSet VisualSet,
		const FIntPoint& Footprint,
		const FIntPoint& Target)
	{
		bool bFound = false;
		int64 BestScore = TNumericLimits<int64>::Max();
		FIntPoint BestAnchor = FIntPoint::ZeroValue;
		TArray<FIntPoint> BestOccupied;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
		{
			const FIntPoint Candidate = Pair.Key;
			TArray<FIntPoint> Occupied;
			bool bValid = true;
			for (int32 X = 0; X < Footprint.X && bValid; ++X)
			{
				for (int32 Y = 0; Y < Footprint.Y; ++Y)
				{
					const FIntPoint Cell = Candidate + FIntPoint(X, Y);
					AMapTile* const* Tile = TileByCell.Find(Cell);
					if (Tile == nullptr || !IsValid(*Tile) || (*Tile)->GetType() != ETileType::None)
					{
						bValid = false;
						break;
					}
					for (int32 PadX = -1; PadX <= 1 && bValid; ++PadX)
						for (int32 PadY = -1; PadY <= 1; ++PadY)
							if (SelectedFacilityCells.Contains(Cell + FIntPoint(PadX, PadY)))
							{ bValid = false; break; }
					Occupied.Add(Cell);
				}
			}
			if (!bValid)
				continue;
			const int32 CenterX2 = Candidate.X * 2 + Footprint.X - 1;
			const int32 CenterY2 = Candidate.Y * 2 + Footprint.Y - 1;
			const int64 DX = static_cast<int64>(CenterX2) - Target.X * 2;
			const int64 DY = static_cast<int64>(CenterY2) - Target.Y * 2;
			// Only the part of the road distance a spur cannot cover is charged for.
			// Anything already inside the spur budget gets a paved approach for free,
			// so penalising it would push districts around for no gain; beyond the
			// budget the cost grows quadratically, the same shape as the target term
			// so the two stay comparable. DX/DY are in half-cells, hence the *4.
			const int32 RoadCells = RoadDistanceByCell.Contains(Candidate)
				? RoadDistanceByCell[Candidate]
				: MaxFacilitySpurSearchCells;
			const int64 RoadExcess = FMath::Max(0, RoadCells - MaxFacilitySpurCells);
			const int64 Score = DX * DX + DY * DY
				+ RoadExcess * RoadExcess * 4 * FacilityRoadProximityWeight;
			if (Score < BestScore)
			{
				bFound = true;
				BestScore = Score;
				BestAnchor = Candidate;
				BestOccupied = MoveTemp(Occupied);
			}
		}
		if (!bFound)
		{
			UE_LOG(LogTemp, Warning, TEXT("Themed district reservation failed: set=%s footprint=%dx%d"),
				*StaticEnum<EFacilityVisualSet>()->GetNameStringByValue(static_cast<int64>(VisualSet)),
				Footprint.X, Footprint.Y);
			return false;
		}
		for (const FIntPoint& Cell : BestOccupied)
			SelectedFacilityCells.Add(Cell);
		// road_cells is what the access-spur pass will have to bridge. Anything at or
		// under MaxFacilitySpurCells gets paved; above it the district is stranded,
		// which is exactly what this scoring term exists to stop happening.
		UE_LOG(LogTemp, Display,
			TEXT("Themed district placed: set=%s anchor=(%d,%d) road_cells=%d target_cells=%.1f"),
			*StaticEnum<EFacilityVisualSet>()->GetNameStringByValue(static_cast<int64>(VisualSet)),
			BestAnchor.X, BestAnchor.Y,
			RoadDistanceByCell.Contains(BestAnchor) ? RoadDistanceByCell[BestAnchor] : -1,
			FMath::Sqrt(static_cast<float>(
				FMath::Square(BestAnchor.X - Target.X) + FMath::Square(BestAnchor.Y - Target.Y))));
		ReserveFacility(VisualSet, BestAnchor, Footprint, 0, BestOccupied, TileByCell);
		return true;
	};
	ReserveThemedDistrict(EFacilityVisualSet::DowntownBlock, FIntPoint(6, 6), DowntownTarget);
	ReserveThemedDistrict(EFacilityVisualSet::FactoryConstruction, FIntPoint(2, 2), FactoryTarget);
	ReserveThemedDistrict(EFacilityVisualSet::RuralHideout, FIntPoint(2, 2), RuralTarget);
}

void AWarZoneFootprintPreview::ReserveFacility(
	EFacilityVisualSet VisualSet,
	const FIntPoint& Anchor,
	const FIntPoint& Footprint,
	int32 RotationQuarterTurns,
	const TArray<FIntPoint>& OccupiedCells,
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	FFacilityPlacement& Placement = FacilityPlacements.AddDefaulted_GetRef();
	Placement.VisualSet = VisualSet;
	Placement.FacilityId = GetFacilityId(VisualSet); // 시설 이름 표(공용 헤더)
	Placement.AnchorCell = Anchor;
	Placement.Footprint = Footprint;
	Placement.RotationQuarterTurns = RotationQuarterTurns;
	const int64 RaidSeed = GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
	Placement.LocalSeed = static_cast<int64>(HashCombine(
		GetTypeHash(RaidSeed),
		HashCombine(GetTypeHash(Anchor.X), HashCombine(GetTypeHash(Anchor.Y), GetTypeHash(static_cast<uint8>(VisualSet))))));
	const bool bLoweredVariant = (Placement.LocalSeed & 1ll) != 0;

	if (VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint)
	{
		// Ground, not WarZoneStronghold: the +220 pad and its four ramps existed to
		// give the code-built core silhouette and approaches. The harvested factory
		// compound is authored flat, and raising it would hang its yard fences and
		// barrel rows over the pad edge the way the rural island once hung over its.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
	}
	else if (VisualSet == EFacilityVisualSet::Yard)
	{
		Placement.ElevationProfile = bLoweredVariant
			? EFacilityElevationProfile::RaisedCompound
			: EFacilityElevationProfile::RaisedCompound;
		Placement.BaseElevationCm = bLoweredVariant ? -60.0f : 120.0f;
	}
	else if (VisualSet == EFacilityVisualSet::LongBarracks)
	{
		Placement.ElevationProfile = bLoweredVariant
			? EFacilityElevationProfile::RaisedCompound
			: EFacilityElevationProfile::RaisedBarracks;
		Placement.BaseElevationCm = bLoweredVariant ? -50.0f : 100.0f;
	}
	else if (VisualSet == EFacilityVisualSet::DowntownBlock)
	{
		// The authored block sits on its own paving just above the shared datum, so
		// the pad beneath it must stay there too. A 160 cm raise would leave the cafe
		// terrace on a plinth with its kerbs hanging over the edge.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}
	else if (VisualSet == EFacilityVisualSet::FactoryConstruction)
	{
		// The authored hall level was trimmed so its own floor slabs are gone and its
		// building bases sit at local Z=0. Raising or lowering the pad under it would
		// leave the halls standing on a step instead of on the surrounding ground.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}
	else if (VisualSet == EFacilityVisualSet::RuralHideout)
	{
		// Its own ground is deleted, so the cabins stand on the shared terrain.
		Placement.ElevationProfile = EFacilityElevationProfile::Ground;
		Placement.BaseElevationCm = 0.0f;
	}

	// Pick an edge that approaches the closest generator-authored traversal cell.
	// This is deterministic and is serialized with the facility, so the visual ramp,
	// the access-road endpoint and a future server manifest all agree.
	const FIntPoint CardinalDirections[] = {
		FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
	};
	int32 BestAccessScore = MAX_int32;
	uint32 BestAccessTieBreak = MAX_uint32;
	for (const FIntPoint& OccupiedCell : OccupiedCells)
	{
		for (const FIntPoint& Direction : CardinalDirections)
		{
			if (VisualSet == EFacilityVisualSet::LongBarracks)
			{
				const bool bLongAlongX = Footprint.X > Footprint.Y;
				if ((bLongAlongX && Direction.X != 0)
					|| (!bLongAlongX && Direction.Y != 0))
				{
					continue;
				}
			}
			const FIntPoint CandidateAccess = OccupiedCell + Direction;
			if (OccupiedCells.Contains(CandidateAccess) || !TileByCell.Contains(CandidateAccess))
				continue;

			int32 NearestTraversalDistance = MAX_int32;
			for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
			{
				if (!IsValid(Pair.Value) || !IsRoadTraversalType(Pair.Value->GetType()))
					continue;
				const int32 Distance = FMath::Abs(Pair.Key.X - CandidateAccess.X)
					+ FMath::Abs(Pair.Key.Y - CandidateAccess.Y);
				NearestTraversalDistance = FMath::Min(NearestTraversalDistance, Distance);
			}
			const uint32 TieBreak = HashCombine(
				GetTypeHash(Placement.LocalSeed),
				HashCombine(GetTypeHash(CandidateAccess.X), GetTypeHash(CandidateAccess.Y)));
			if (NearestTraversalDistance < BestAccessScore
				|| (NearestTraversalDistance == BestAccessScore && TieBreak < BestAccessTieBreak))
			{
				BestAccessScore = NearestTraversalDistance;
				BestAccessTieBreak = TieBreak;
				Placement.EntranceCell = OccupiedCell;
				Placement.AccessCell = CandidateAccess;
				Placement.EntranceDirection = Direction;
			}
		}
	}
	if (Placement.ElevationProfile != EFacilityElevationProfile::Ground)
	{
		// Procedural facilities expose north/south entrances in local space. Rotate
		// local south toward the chosen road-facing edge so the ramp never meets a wall.
		if (Placement.EntranceDirection == FIntPoint(0, -1)) Placement.RotationQuarterTurns = 0;
		else if (Placement.EntranceDirection == FIntPoint(1, 0)) Placement.RotationQuarterTurns = 1;
		else if (Placement.EntranceDirection == FIntPoint(0, 1)) Placement.RotationQuarterTurns = 2;
		else if (Placement.EntranceDirection == FIntPoint(-1, 0)) Placement.RotationQuarterTurns = 3;
	}

	FName FacilityTag = WarehouseTag;
	Placement.FacilityLevel = TSoftObjectPtr<UWorld>(
		VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint
			? WarZoneCoreLevelPath : WarehouseLevelPath);
	if (VisualSet == EFacilityVisualSet::Yard)
	{
		FacilityTag = YardTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(YardLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::LongBarracks)
	{
		FacilityTag = BarracksTag;
	}
	else if (VisualSet == EFacilityVisualSet::LinearTrench)
	{
		FacilityTag = TrenchTag;
	}
	else if (VisualSet == EFacilityVisualSet::Checkpoint)
	{
		FacilityTag = CheckpointTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(CheckpointLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::DowntownBlock)
	{
		FacilityTag = DowntownTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(DowntownBlockLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::FactoryConstruction)
	{
		FacilityTag = FactoryConstructionTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(FactoryHallLevelPath);
	}
	else if (VisualSet == EFacilityVisualSet::RuralHideout)
	{
		FacilityTag = RuralHideoutTag;
		Placement.FacilityLevel = TSoftObjectPtr<UWorld>(RuralDioramaLevelPath);
	}
	if (bUseRuntimeBlueprintTiles && !FacilityUsesAuthoredLevel(VisualSet, Footprint))
		Placement.FacilityLevel.Reset();

	for (const FIntPoint& Cell : OccupiedCells)
	{
		AMapTile* Tile = TileByCell.FindChecked(Cell);
		Tile->Tags.AddUnique(ReservedTag);
		const bool bIsWarZoneFacility = VisualSet == EFacilityVisualSet::Warehouse
			|| VisualSet == EFacilityVisualSet::Yard
			|| VisualSet == EFacilityVisualSet::LongBarracks
			|| VisualSet == EFacilityVisualSet::LinearTrench;
		if (bIsWarZoneFacility)
			Tile->Tags.AddUnique(WarZoneFacilityTag);
		Tile->Tags.AddUnique(FacilityTag);
		ReservedTiles.Add(Tile);
		Placement.OccupiedCells.Add(Cell);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Facility mapping: %s -> %s (Anchor=%d,%d Footprint=%d,%d Rotation=%d)"),
		*FacilityTag.ToString(),
		*Placement.FacilityLevel.ToSoftObjectPath().ToString(),
		Anchor.X, Anchor.Y, Footprint.X, Footprint.Y,
		RotationQuarterTurns * 90);
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
