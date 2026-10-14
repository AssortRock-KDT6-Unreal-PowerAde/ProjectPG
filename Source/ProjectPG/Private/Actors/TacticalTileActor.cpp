// 런타임에 스폰하는 20x20m 전술 타일 묶음. 절차 맵의 화면 단계가 쓴다.

#include "Actors/TacticalTileActor.h"

#include "Components/ArrowComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"

namespace TacticalTile
{
	constexpr uint8 North = 1;
	constexpr uint8 East = 2;
	constexpr uint8 South = 4;
	constexpr uint8 West = 8;

	FTransform Box(const FVector& Center, const FVector& Size, float Yaw = 0.0f)
	{
		return FTransform(FRotator(0, Yaw, 0), Center, Size / 100.0f);
	}
}

ATacticalTileActor::ATacticalTileActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Wall(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Solid_2m.SM_Wall_Solid_2m'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HalfWall(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Half_1x3.SM_Wall_Half_1x3'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> QuarterWall(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_wall_quarter_1x3.SM_wall_quarter_1x3'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ShootingWall(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_shooting_hole_1x3x.SM_Wall_shooting_hole_1x3x'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Door(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Door_1x3.SM_Wall_Door_1x3'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Window(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Window.SM_Wall_Window'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Floor(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Floor_2x2.SM_Floor_2x2'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Barrier(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Foundation_block_1_13.SM_Foundation_block_1_13'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Barrel(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Barrel_LP.SM_Barrel_LP'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Grass(TEXT("/Script/Engine.StaticMesh'/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_1_Runtime.SM_GrassPatch_1_Runtime'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassB(TEXT("/Script/Engine.StaticMesh'/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_2_Runtime.SM_GrassPatch_2_Runtime'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassC(TEXT("/Script/Engine.StaticMesh'/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_Long_Runtime.SM_GrassPatch_Long_Runtime'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BushLow(TEXT("/Script/Engine.StaticMesh'/Game/Modular_Rural_Cabin/Meshes/Foliage/Bush_1.Bush_1'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BushAlt(TEXT("/Script/Engine.StaticMesh'/Game/Modular_Rural_Cabin/Meshes/Foliage/Shrubs_1.Shrubs_1'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> HeroShrub(TEXT("/Script/Engine.StaticMesh'/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cylinder.Cylinder'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pine(TEXT("/Script/Engine.StaticMesh'/PCGBiomeSample/Meshes/PCG_Pine_01.PCG_Pine_01'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Spruce(TEXT("/Script/Engine.StaticMesh'/PCGBiomeSample/Meshes/PCG_Spruce_01.PCG_Spruce_01'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Boulder(TEXT("/Script/Engine.StaticMesh'/PCG/SampleContent/SimpleForest/Meshes/PCG_Boulder_02.PCG_Boulder_02'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DowntownRockLarge(TEXT("/Script/Engine.StaticMesh'/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_large_a_low.SM_rock_large_a_low'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DowntownRockMedium(TEXT("/Script/Engine.StaticMesh'/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_medium_a_low.SM_rock_medium_a_low'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FactoryStair(TEXT("/Script/Engine.StaticMesh'/Game/Factory_Pack_V1/Meshes/SM_Stair_1.SM_Stair_1'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Desk(TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Desk.SM_Desk'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FactoryContainer(TEXT("/Script/Engine.StaticMesh'/Game/Factory_Pack_V1/Meshes/SM__Container.SM__Container'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FactoryTank(TEXT("/Script/Engine.StaticMesh'/Game/Factory_Pack_V1/Meshes/SM_Tank.SM_Tank'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FactoryPallet(TEXT("/Script/Engine.StaticMesh'/Game/Factory_Pack_V1/Meshes/SM_pallet.SM_pallet'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FactoryFence(TEXT("/Script/Engine.StaticMesh'/Game/Factory_Pack_V1/Meshes/SM_fence_2_a.SM_fence_2_a'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RoadStraight_Ground.MI_RoadStraight_Ground'"));
	// 외부 팩 머티리얼은 건드리지 않는다. 이 프로젝트용 인스턴스는 Downtown 의 실제 아스팔트
	// 텍스처를 쓰되 물웅덩이/섞기를 끄고 거칠기를 높여서, 절차 도로판이 낙엽 깔린 바닥이 아니라
	// 마른 아스팔트로 보이게 한다.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean.MI_RuntimeRoad_AsphaltClean'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DirtMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/Factory_Pack_V1/Materials/Instance/MI_Dirt_1_Inst.MI_Dirt_1_Inst'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ExitMat(TEXT("/Script/Engine.Material'/Game/PG/LevelDesign/Materials/M_ExitRed.M_ExitRed'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> TrunkMat(TEXT("/Script/Engine.Material'/Game/PG/LevelDesign/Materials/M_NatureTrunk.M_NatureTrunk'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RockMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RuntimeNatureRock.MI_RuntimeNatureRock'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BushMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RuntimeBush_Dark.MI_RuntimeBush_Dark'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShrubsMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RuntimeShrubs_Dark.MI_RuntimeShrubs_Dark'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> HeroShrubMat(TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RuntimeHeroShrub_Dark.MI_RuntimeHeroShrub_Dark'"));

	Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground_20m"));
	Ground->SetupAttachment(SceneRoot);
	Ground->SetStaticMesh(Cube.Object);
	// 담아 둔 LD 타일은 윗면 20 cm 인 SM_Floor_2x2 를 쓴다. 그 높이에 딱 맞춰야
	// C++ 자연 타일이 얕게 꺼진 반짝이는 웅덩이처럼 보이지 않는다.
	// XY 를 살짝 겹쳐서 그리기 이음새를 가리되, 20 m 연결 위치는 바꾸지 않는다.
	Ground->SetRelativeTransform(TacticalTile::Box(FVector(0, 0, 5), FVector(2004, 2004, 30)));
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	if (GroundMat.Succeeded()) Ground->SetMaterial(0, GroundMat.Object);

	auto MakeISM = [this](const TCHAR* Name, UStaticMesh* Mesh, bool bCollision)
	{
		UInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(SceneRoot);
		Component->SetStaticMesh(Mesh);
		Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
		if (bCollision) Component->SetCollisionProfileName(TEXT("BlockAll"));
		// Recast 는 전용 NavigationFloor 만 읽는다. 런타임 ISM 인스턴스 수천 개를 빼야
		// rebuild 중에 무겁고 위험한 길찾기 내보내기가 일어나지 않는다.
		Component->SetCanEverAffectNavigation(false);
		return Component;
	};

	RoadPieces = MakeISM(TEXT("RoadPieces"), Cube.Object, true);
	if (RoadMat.Succeeded()) RoadPieces->SetMaterial(0, RoadMat.Object);
	SolidWalls = MakeISM(TEXT("SolidWalls"), Wall.Object, true);
	HalfWalls = MakeISM(TEXT("HalfWalls"), HalfWall.Object, true);
	QuarterWalls = MakeISM(TEXT("QuarterWalls"), QuarterWall.Object, true);
	ShootingWalls = MakeISM(TEXT("ShootingWalls"), ShootingWall.Object, true);
	DoorWalls = MakeISM(TEXT("DoorWalls"), Door.Object, true);
	WindowWalls = MakeISM(TEXT("WindowWalls"), Window.Object, true);
	RoofFloors = MakeISM(TEXT("RoofFloors"), Floor.Object, true);
	ConcreteCover = MakeISM(TEXT("ConcreteCover"), Barrier.Object, true);
	BarrelCover = MakeISM(TEXT("BarrelCover"), Barrel.Object, true);
	GrassDressing = MakeISM(TEXT("GrassDressing"), Grass.Object, false);
	GrassDressing->SetCullDistances(2500, 8000);
	GrassDressing->SetCastShadow(false);
	GrassDressing->SetEvaluateWorldPositionOffset(false);
	GrassDressingB = MakeISM(TEXT("GrassDressing_B"), GrassB.Object, false);
	GrassDressingB->SetCullDistances(2500, 8000);
	GrassDressingB->SetCastShadow(false);
	GrassDressingB->SetEvaluateWorldPositionOffset(false);
	GrassDressingC = MakeISM(TEXT("GrassDressing_C"), GrassC.Object, false);
	GrassDressingC->SetCullDistances(2500, 8000);
	GrassDressingC->SetCastShadow(false);
	GrassDressingC->SetEvaluateWorldPositionOffset(false);
	TreeTrunks = MakeISM(TEXT("TreeTrunks"), Pine.Object, true);
	TreeTrunks->SetCullDistances(12000, 30000);
	TreeTrunks->SetEvaluateWorldPositionOffset(false);
	TreeCanopies = MakeISM(TEXT("TreeCanopies"), Spruce.Object, true);
	TreeCanopies->SetCullDistances(12000, 30000);
	TreeCanopies->SetEvaluateWorldPositionOffset(false);
	RockCover = MakeISM(TEXT("RockCover"), DowntownRockLarge.Succeeded() ? DowntownRockLarge.Object : Boulder.Object, true);
	RockCover->SetCullDistances(10000, 25000);
	RockCoverB = MakeISM(TEXT("RockCover_B"), DowntownRockMedium.Succeeded() ? DowntownRockMedium.Object : Boulder.Object, true);
	RockCoverB->SetCullDistances(10000, 25000);
	BushDressing = MakeISM(TEXT("BushDressing"), BushLow.Object, false);
	BushDressing->SetCullDistances(2500, 9000);
	BushDressing->SetCastShadow(true);
	BushDressing->SetEvaluateWorldPositionOffset(false);
	if (BushMat.Succeeded()) BushDressing->SetMaterial(0, BushMat.Object);
	BushDressingB = MakeISM(TEXT("BushDressing_B"), BushAlt.Object, false);
	BushDressingB->SetCullDistances(2200, 8000);
	BushDressingB->SetCastShadow(false);
	BushDressingB->SetEvaluateWorldPositionOffset(false);
	if (ShrubsMat.Succeeded()) BushDressingB->SetMaterial(0, ShrubsMat.Object);
	HeroShrubDressing = MakeISM(TEXT("HeroShrubDressing"), HeroShrub.Object, false);
	HeroShrubDressing->SetCullDistances(4000, 14000);
	HeroShrubDressing->SetCastShadow(true);
	HeroShrubDressing->SetEvaluateWorldPositionOffset(false);
	if (HeroShrubMat.Succeeded()) HeroShrubDressing->SetMaterial(0, HeroShrubMat.Object);
	FallenLogs = MakeISM(TEXT("FallenLogs"), Cylinder.Object, true);
	FallenLogs->SetCullDistances(8000, 22000);
	TerrainBerms = MakeISM(TEXT("TerrainBerms"), Cube.Object, true);
	// 런타임 타일 rebuild 는 컴포넌트 등록 뒤에 일어날 수 있다. 그 시점에 바뀌는 ISM 인스턴스
	// 버퍼에서 길찾기를 내보내는 건 위험하다. 호스트의 전용 길찾기 바닥이
	// 계속 Recast 의 원본이다.
	ElevationStairs = MakeISM(TEXT("ElevationStairs"), FactoryStair.Succeeded() ? FactoryStair.Object : Cube.Object, true);
	ElevationStairs->SetCullDistances(10000, 26000);
	MarkingPieces = MakeISM(TEXT("MarkingPieces"), Cube.Object, false);
	UtilityProps = MakeISM(TEXT("UtilityProps"), Desk.Object, true);
	FactoryContainers = MakeISM(TEXT("FactoryContainers"), FactoryContainer.Object, true);
	FactoryContainers->SetCullDistances(10000, 30000);
	FactoryTanks = MakeISM(TEXT("FactoryTanks"), FactoryTank.Object, true);
	FactoryTanks->SetCullDistances(9000, 24000);
	FactoryPallets = MakeISM(TEXT("FactoryPallets"), FactoryPallet.Object, true);
	FactoryPallets->SetCullDistances(6000, 16000);
	FactoryFences = MakeISM(TEXT("FactoryFences"), FactoryFence.Object, true);
	FactoryFences->SetCullDistances(9000, 24000);
	if (ExitMat.Succeeded()) MarkingPieces->SetMaterial(0, ExitMat.Object);
	if (TrunkMat.Succeeded())
	{
		FallenLogs->SetMaterial(0, TrunkMat.Object);
	}
	if (RockMat.Succeeded()) RockCover->SetMaterial(0, RockMat.Object);
	if (DirtMat.Succeeded()) TerrainBerms->SetMaterial(0, DirtMat.Object);
	else if (GroundMat.Succeeded()) TerrainBerms->SetMaterial(0, GroundMat.Object);

	auto MakeChild = [this](const TCHAR* Name)
	{
		UChildActorComponent* Component = CreateDefaultSubobject<UChildActorComponent>(Name);
		Component->SetupAttachment(SceneRoot);
		return Component;
	};
	DynamicPropA = MakeChild(TEXT("DynamicProp_A"));
	DynamicPropB = MakeChild(TEXT("DynamicProp_B"));
	DynamicPropC = MakeChild(TEXT("DynamicProp_C"));

	auto MakeArrow = [this](const TCHAR* Name, const FVector& Location, float Yaw, const FColor& Color)
	{
		UArrowComponent* Arrow = CreateDefaultSubobject<UArrowComponent>(Name);
		Arrow->SetupAttachment(SceneRoot);
		Arrow->SetRelativeLocation(Location);
		Arrow->SetRelativeRotation(FRotator(0, Yaw, 0));
		Arrow->SetArrowColor(Color);
		Arrow->SetHiddenInGame(true);
		return Arrow;
	};
	ConnectionNorth = MakeArrow(TEXT("Connection_North"), FVector(0, 1000, 30), 90, FColor::Green);
	ConnectionEast = MakeArrow(TEXT("Connection_East"), FVector(1000, 0, 30), 0, FColor::Green);
	ConnectionSouth = MakeArrow(TEXT("Connection_South"), FVector(0, -1000, 30), -90, FColor::Green);
	ConnectionWest = MakeArrow(TEXT("Connection_West"), FVector(-1000, 0, 30), 180, FColor::Green);
	SpawnPoint = MakeArrow(TEXT("SpawnPoint"), FVector(-650, 0, 100), 0, FColor::Cyan);
	LootPoint = MakeArrow(TEXT("LootPoint"), FVector(-350, 520, 70), 0, FColor::Yellow);
	AISpawnPoint = MakeArrow(TEXT("AISpawnPoint"), FVector(550, -520, 70), 180, FColor::Red);
	ExitPoint = MakeArrow(TEXT("ExitPoint"), FVector(600, 0, 50), 0, FColor::Emerald);

	CarClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Blueprints/Props/BP_Prop_AbandonedCar.BP_Prop_AbandonedCar_C")));
	BarrelClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Blueprints/Props/BP_Prop_BarrelCluster.BP_Prop_BarrelCluster_C")));
	BarrierClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Blueprints/Props/BP_Prop_ConcreteBarrier.BP_Prop_ConcreteBarrier_C")));
	Tags.Add(TEXT("RuntimeTacticalTile"));
}

void ATacticalTileActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildTile();
}

uint8 ATacticalTileActor::GetCanonicalConnectionMask() const
{
	using namespace TacticalTile;
	switch (TileKind)
	{
	case ETacticalTileKind::RoadStraight: return East | West;
	case ETacticalTileKind::RoadCorner: return North | West;
	case ETacticalTileKind::RoadTJunction: return North | East | West;
	case ETacticalTileKind::RoadCross: return North | East | South | West;
	case ETacticalTileKind::RoadDeadEnd: return West;
	case ETacticalTileKind::SpawnStaging: return East;
	case ETacticalTileKind::ExitCheckpoint:
	case ETacticalTileKind::ObstacleCheckpoint: return East | West;
	case ETacticalTileKind::WarZoneYard:
	case ETacticalTileKind::WarZoneWarehouse: return North | East | South | West;
	default: return 0;
	}
}

FIntPoint ATacticalTileActor::GetFootprintCells() const
{
	return FIntPoint(
		FMath::Max(1, FootprintCells.X),
		FMath::Max(1, FootprintCells.Y));
}

uint8 ATacticalTileActor::GetEffectiveLayoutVariant() const
{
	if (LayoutVariantOverride <= 3)
		return LayoutVariantOverride;

	return static_cast<uint8>(FMath::Abs(LocalSeed) % 4);
}

uint8 ATacticalTileActor::GetEffectiveConnectionMask() const
{
	return ConnectionMaskOverride <= 15 ? ConnectionMaskOverride : GetCanonicalConnectionMask();
}

void ATacticalTileActor::RebuildFromRuntimeSpec(int32 InSeed, uint8 InConnectionMask, uint8 InLayoutVariant)
{
	LocalSeed = InSeed;
	ConnectionMaskOverride = InConnectionMask & 15;
	LayoutVariantOverride = InLayoutVariant;
	RebuildTile();
}

void ATacticalTileActor::AddRoad(uint8 Mask, float Width)
{
	using namespace TacticalTile;
	// 판 전체를 Ground_20m 에서 띄워 둔다. 예전 아랫면은 땅과 딱 같은 높이라
	// 카메라가 낮게 비스듬히 볼 때 깜빡일 수 있었다.
	constexpr float RoadCenterZ = 5.0f;
	constexpr float RoadThickness = 8.0f;
	RoadPieces->AddInstance(Box(FVector(0, 0, RoadCenterZ), FVector(Width, Width, RoadThickness)));
	// 갈래는 가운데 판과 겹치지만 타일 가장자리 +/-1000cm 에서 딱 끝난다.
	// 예전 1320cm 갈래는 +/-1320cm 까지 뻗어 이웃 칸과 겹쳤다.
	const float ArmCenter = (1000.0f + Width * 0.5f) * 0.5f;
	const float ArmLength = 1000.0f - Width * 0.5f;
	if (Mask & North) RoadPieces->AddInstance(Box(FVector(0, ArmCenter, RoadCenterZ), FVector(Width, ArmLength, RoadThickness)));
	if (Mask & East) RoadPieces->AddInstance(Box(FVector(ArmCenter, 0, RoadCenterZ), FVector(ArmLength, Width, RoadThickness)));
	if (Mask & South) RoadPieces->AddInstance(Box(FVector(0, -ArmCenter, RoadCenterZ), FVector(Width, ArmLength, RoadThickness)));
	if (Mask & West) RoadPieces->AddInstance(Box(FVector(-ArmCenter, 0, RoadCenterZ), FVector(ArmLength, Width, RoadThickness)));

	// 안쪽 모서리 최대 네 곳을 작은 45도 아스팔트 조각으로 깎는다.
	// 논리 연결 위치와 정확한 6.5m 가장자리 폭은 그대로지만, 모퉁이/T자/
	// 사거리 타일이 직각으로 딱 꺾인 네모 판끼리 만난 것처럼 보이지 않는다.
	constexpr float BevelOffset = 285.0f;
	constexpr float BevelSize = 290.0f;
	if ((Mask & North) && (Mask & East)) RoadPieces->AddInstance(Box(FVector(BevelOffset, BevelOffset, RoadCenterZ), FVector(BevelSize, BevelSize, RoadThickness), 45.0f));
	if ((Mask & East) && (Mask & South)) RoadPieces->AddInstance(Box(FVector(BevelOffset, -BevelOffset, RoadCenterZ), FVector(BevelSize, BevelSize, RoadThickness), 45.0f));
	if ((Mask & South) && (Mask & West)) RoadPieces->AddInstance(Box(FVector(-BevelOffset, -BevelOffset, RoadCenterZ), FVector(BevelSize, BevelSize, RoadThickness), 45.0f));
	if ((Mask & West) && (Mask & North)) RoadPieces->AddInstance(Box(FVector(-BevelOffset, BevelOffset, RoadCenterZ), FVector(BevelSize, BevelSize, RoadThickness), 45.0f));
}

void ATacticalTileActor::AddBrokenBoundary(uint8 OpenMask, int32 Density)
{
	using namespace TacticalTile;
	const float Steps[] = {-850, -550, -250, 250, 550, 850};
	for (int32 Index = 0; Index < FMath::Clamp(Density * 2, 2, 6); ++Index)
	{
		// 도로 경계는 예전엔 불투명한 3 m CQB 벽 판이었다. 수백 칸에 걸쳐
		// 땅에서도 하늘에서도 눈에 띄는 하얀 미로가 됐다. Factory 팩 울타리는
		// 매번 똑같은 끊긴 경계/충돌 역할은 그대로 하면서
		// 지형 쪽 시야는 열어 둔다.
		const float Scale = 0.88f;
		if (!(OpenMask & North) || FMath::Abs(Steps[Index]) > 390) FactoryFences->AddInstance(FTransform(FRotator(0, 0, 0), FVector(Steps[Index], 970, 0), FVector(Scale, 1, 1)));
		if (!(OpenMask & South) || FMath::Abs(Steps[Index]) > 390) FactoryFences->AddInstance(FTransform(FRotator(0, 180, 0), FVector(Steps[Index] + 200, -970, 0), FVector(Scale, 1, 1)));
		if (!(OpenMask & East) || FMath::Abs(Steps[Index]) > 390) FactoryFences->AddInstance(FTransform(FRotator(0, 90, 0), FVector(970, Steps[Index], 0), FVector(Scale, 1, 1)));
		if (!(OpenMask & West) || FMath::Abs(Steps[Index]) > 390) FactoryFences->AddInstance(FTransform(FRotator(0, -90, 0), FVector(-970, Steps[Index] + 200, 0), FVector(Scale, 1, 1)));
	}
}

void ATacticalTileActor::AddRoadShoulderDressing(uint8 Mask, uint8 Variant, FRandomStream& Stream)
{
	using namespace TacticalTile;
	// 얇은 흙 갓길이 아스팔트 아래, 공통 지형 위에 깔린다.
	// 갈래마다 따로 만들어서, 막다른 길이나 모퉁이가 이웃 칸으로
	// 길이 이어지는 것처럼 잘못 칠하지 않게 한다.
	auto AddArmShoulder = [this](const FVector& Center, const FVector& Size, float Yaw)
	{
		TerrainBerms->AddInstance(Box(Center, Size, Yaw));
	};
	constexpr float ShoulderOffset = 390.0f;
	constexpr float ArmCenter = 662.5f;
	constexpr float ArmLength = 675.0f;
	constexpr float ShoulderWidth = 130.0f;
	constexpr float ShoulderZ = 3.0f;
	constexpr float ShoulderThickness = 6.0f;
	if (Mask & East)
	{
		AddArmShoulder(FVector(ArmCenter, ShoulderOffset, ShoulderZ), FVector(ArmLength, ShoulderWidth, ShoulderThickness), 0.0f);
		AddArmShoulder(FVector(ArmCenter, -ShoulderOffset, ShoulderZ), FVector(ArmLength, ShoulderWidth, ShoulderThickness), 0.0f);
	}
	if (Mask & West)
	{
		AddArmShoulder(FVector(-ArmCenter, ShoulderOffset, ShoulderZ), FVector(ArmLength, ShoulderWidth, ShoulderThickness), 0.0f);
		AddArmShoulder(FVector(-ArmCenter, -ShoulderOffset, ShoulderZ), FVector(ArmLength, ShoulderWidth, ShoulderThickness), 0.0f);
	}
	if (Mask & North)
	{
		AddArmShoulder(FVector(ShoulderOffset, ArmCenter, ShoulderZ), FVector(ShoulderWidth, ArmLength, ShoulderThickness), 0.0f);
		AddArmShoulder(FVector(-ShoulderOffset, ArmCenter, ShoulderZ), FVector(ShoulderWidth, ArmLength, ShoulderThickness), 0.0f);
	}
	if (Mask & South)
	{
		AddArmShoulder(FVector(ShoulderOffset, -ArmCenter, ShoulderZ), FVector(ShoulderWidth, ArmLength, ShoulderThickness), 0.0f);
		AddArmShoulder(FVector(-ShoulderOffset, -ArmCenter, ShoulderZ), FVector(ShoulderWidth, ArmLength, ShoulderThickness), 0.0f);
	}

	// 만든 갓길 바로 바깥에 낮은 흙 패임을 흩뿌린다. 충돌이나 연결 높이는 바꾸지 않고
	// 도로와 지형을 눈으로 이어 붙인다. 이웃 쪽 끝점은 정확하고 막힘없이 둔다.
	auto AddErosionPocket = [this, &Stream](const FVector2D& Along, const FVector2D& Normal, float Distance, float Side)
	{
		const FVector2D Point = Along * Distance + Normal * Side;
		const float Length = Stream.FRandRange(180.0f, 330.0f);
		const float WidthValue = Stream.FRandRange(70.0f, 145.0f);
		const float BaseYaw = FMath::RadiansToDegrees(FMath::Atan2(Along.Y, Along.X));
		TerrainBerms->AddInstance(Box(
			FVector(Point.X, Point.Y, 1.5f),
			FVector(Length, WidthValue, Stream.FRandRange(3.0f, 7.0f)),
			BaseYaw + Stream.FRandRange(-13.0f, 13.0f)));
	};
	const float PocketSide = 520.0f + static_cast<float>(Variant % 2) * 45.0f;
	if (Mask & East)
	{
		AddErosionPocket(FVector2D(1, 0), FVector2D(0, 1), 570.0f, PocketSide);
		AddErosionPocket(FVector2D(1, 0), FVector2D(0, 1), 790.0f, -PocketSide);
	}
	if (Mask & West)
	{
		AddErosionPocket(FVector2D(-1, 0), FVector2D(0, 1), 610.0f, PocketSide);
		AddErosionPocket(FVector2D(-1, 0), FVector2D(0, 1), 820.0f, -PocketSide);
	}
	if (Mask & North)
	{
		AddErosionPocket(FVector2D(0, 1), FVector2D(1, 0), 600.0f, PocketSide);
		AddErosionPocket(FVector2D(0, 1), FVector2D(1, 0), 810.0f, -PocketSide);
	}
	if (Mask & South)
	{
		AddErosionPocket(FVector2D(0, -1), FVector2D(1, 0), 580.0f, PocketSide);
		AddErosionPocket(FVector2D(0, -1), FVector2D(1, 0), 800.0f, -PocketSide);
	}

	// 칸의 네 모서리만 꾸미지 않고 도로가 아닌 부분 전체를 채운다.
	// 덩어리 메시는 인스턴스로 놓이고 나중에 같이 쓰는 HISM 에 담기므로,
	// 풀 액터를 하나씩 스폰하는 것보다 훨씬 싸다.
	// 이 검사는 이어진 모든 도로 갈래 둘레의 흙 갓길 자리도 비워 둔다.
	auto IsRoadOrShoulder = [Mask](const FVector2D& Point)
	{
		constexpr float ClearHalfWidth = 500.0f;
		constexpr float CenterExtent = 460.0f;
		if (FMath::Abs(Point.X) <= CenterExtent && FMath::Abs(Point.Y) <= CenterExtent)
		{
			return true;
		}
		if ((Mask & North) && Point.Y >= 300.0f && FMath::Abs(Point.X) <= ClearHalfWidth) return true;
		if ((Mask & South) && Point.Y <= -300.0f && FMath::Abs(Point.X) <= ClearHalfWidth) return true;
		if ((Mask & East) && Point.X >= 300.0f && FMath::Abs(Point.Y) <= ClearHalfWidth) return true;
		if ((Mask & West) && Point.X <= -300.0f && FMath::Abs(Point.Y) <= ClearHalfWidth) return true;
		return false;
	};

	const int32 GridSize = bIsAccessRoad ? 8 : 10;
	const float CellStep = 2000.0f / static_cast<float>(GridSize);
	int32 GrassIndex = 0;
	for (int32 GridY = 0; GridY < GridSize; ++GridY)
	{
		for (int32 GridX = 0; GridX < GridSize; ++GridX)
		{
			const FVector2D Point(
				-1000.0f + (GridX + 0.5f) * CellStep + Stream.FRandRange(-CellStep * 0.38f, CellStep * 0.38f),
				-1000.0f + (GridY + 0.5f) * CellStep + Stream.FRandRange(-CellStep * 0.38f, CellStep * 0.38f));
			if (IsRoadOrShoulder(Point))
			{
				continue;
			}

			UInstancedStaticMeshComponent* GrassComponent =
				(GrassIndex % 7 == 0) ? GrassDressingC : ((GrassIndex % 3 == 0) ? GrassDressingB : GrassDressing);
			const float HeightRoll = Stream.FRand();
			const float ZScale = HeightRoll < 0.18f
				? Stream.FRandRange(1.25f, 1.65f)
				: (HeightRoll < 0.62f ? Stream.FRandRange(0.92f, 1.28f) : Stream.FRandRange(0.68f, 0.98f));
			const float XYScale = Stream.FRandRange(1.65f, 2.35f);
			const UStaticMesh* Mesh = GrassComponent->GetStaticMesh();
			const float GroundedZ = IsValid(Mesh) ? -Mesh->GetBoundingBox().Min.Z * ZScale + 1.0f : 5.0f;
			GrassComponent->AddInstance(FTransform(
				FRotator(0, Stream.FRandRange(0.0f, 360.0f), 0),
				FVector(Point.X, Point.Y, GroundedZ),
				FVector(XYScale, XYScale, ZScale)));
			++GrassIndex;
		}
	}

	// 불규칙한 가장자리 덩어리를 더 놓아 칸 경계를 살짝 넘게 한다.
	// 그래야 이웃한 도로/자연 칸 사이에 반듯한 20 m 네모 이음새가 안 보인다.
	const FVector2D Corners[4] = {
		FVector2D(-790, -790), FVector2D(-790, 790),
		FVector2D(790, -790), FVector2D(790, 790)
	};
	const int32 PatchCount = bIsAccessRoad ? 8 : 12;
	for (int32 Index = 0; Index < PatchCount; ++Index)
	{
		const FVector2D& Corner = Corners[(Index + Variant) % UE_ARRAY_COUNT(Corners)];
		const FVector2D Point = Corner + FVector2D(Stream.FRandRange(-220.0f, 220.0f), Stream.FRandRange(-220.0f, 220.0f));
		if (IsRoadOrShoulder(Point)) continue;
		UInstancedStaticMeshComponent* GrassComponent = (Index % 5 == 0) ? GrassDressingC : ((Index % 3 == 0) ? GrassDressingB : GrassDressing);
		const float XYScale = Stream.FRandRange(1.8f, 2.65f);
		const float ZScale = Stream.FRandRange(1.1f, 1.65f);
		const UStaticMesh* Mesh = GrassComponent->GetStaticMesh();
		const float GroundedZ = IsValid(Mesh) ? -Mesh->GetBoundingBox().Min.Z * ZScale + 1.0f : 5.0f;
		GrassComponent->AddInstance(FTransform(
			FRotator(0, Stream.FRandRange(0.0f, 360.0f), 0),
			FVector(Point.X, Point.Y, GroundedZ),
			FVector(XYScale, XYScale, ZScale)));
	}
	if (!bIsAccessRoad && Variant % 3 == 0)
	{
		const FVector2D& Corner = Corners[(Variant + 1) % UE_ARRAY_COUNT(Corners)];
		AddBushCluster(FVector(Corner.X, Corner.Y, 0.0f), 4, 135.0f, Stream);
	}
}

void ATacticalTileActor::AddShelter(const FVector& Center, float Yaw, bool bDoor)
{
	const FRotator Rotation(0, Yaw, 0);
	const FVector Right = Rotation.RotateVector(FVector(0, 1, 0));
	const FVector Back = Rotation.RotateVector(FVector(-1, 0, 0));
	SolidWalls->AddInstance(FTransform(Rotation, Center + Right * 210, FVector::OneVector));
	SolidWalls->AddInstance(FTransform(Rotation, Center - Right * 210, FVector::OneVector));
	(bDoor ? DoorWalls : WindowWalls)->AddInstance(FTransform(FRotator(0, Yaw + 90, 0), Center + Back * 210, FVector::OneVector));
	for (int32 X = 0; X < 2; ++X)
		for (int32 Y = 0; Y < 2; ++Y)
			RoofFloors->AddInstance(FTransform(Rotation, Center + Rotation.RotateVector(FVector(X * 200 - 100, Y * 200 - 100, 300)), FVector::OneVector));
}

void ATacticalTileActor::AddGrass(int32 Count, FRandomStream& Stream)
{
	// 자연 칸은 초록 바닥판에 장식 풀 몇 포기가 아니라, 허리 높이까지 자란 버려진 땅으로 보여야 한다.
	// 이것들은 같이 쓰는 HISM 에 담기는 풀 *덩어리* 메시라서,
	// 20 m 칸마다 약 100 개를 놓아도 액터 수천 개가 생기지 않고
	// 빽빽한 엄폐가 된다.
	const bool bDenseNature = TileKind == ETacticalTileKind::NatureMeadow
		|| TileKind == ETacticalTileKind::NatureForestSparse
		|| TileKind == ETacticalTileKind::NatureForestDense
		|| TileKind == ETacticalTileKind::NatureScrub
		|| TileKind == ETacticalTileKind::NatureAmbush;
	const float DensityFactor = bDenseNature ? 0.52f : 0.22f;
	int32 ScaledCount = FMath::Clamp(
		FMath::RoundToInt(Count * FMath::Clamp(DressingDensityScale, 0.0f, 3.0f) * DensityFactor),
		0,
		bDenseNature ? 210 : 72);
	if (bDenseNature && Count > 0)
	{
		const int32 WeightedMinimum = FMath::RoundToInt(
			184.0f * FMath::Clamp(DressingDensityScale, 0.0f, 1.0f));
		ScaledCount = FMath::Max(ScaledCount, WeightedMinimum);
	}

	// 불규칙한 중심 16 개와 1.2 m 겹침으로, 이웃 20 m 칸이 만나는 곳의 네모 윤곽을 깬다.
	// 자연 타일은 일부러 자로 잰 듯한 빈 오솔길을 비워 두지 않는다.
	// 길찾기는 아래 바닥이 맡는다.
	FVector2D PatchCenters[16];
	for (FVector2D& PatchCenter : PatchCenters)
	{
		PatchCenter = FVector2D(Stream.FRandRange(-1040.0f, 1040.0f), Stream.FRandRange(-1040.0f, 1040.0f));
	}
	const bool bVerticalTrail = (LocalSeed & 1) != 0;
	const int32 DenseGridSide = bDenseNature
		? FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ScaledCount))))
		: 1;
	const float DenseGridStep = 2240.0f / DenseGridSide;
	for (int32 Index = 0; Index < ScaledCount; ++Index)
	{
		FVector2D Point;
		if (bDenseNature)
		{
			// 흔든 격자를 쓰면 자연 칸이 실제로 다 덮인다. 순수 무작위 덩어리는
			// 1 m 넓이의 맨땅을 자꾸 남겨서, 플레이어 눈높이에서 20 m 네모가 보였다.
			const int32 GridX = Index % DenseGridSide;
			const int32 GridY = Index / DenseGridSide;
			Point = FVector2D(
				-1120.0f + (GridX + 0.5f) * DenseGridStep + Stream.FRandRange(-0.34f, 0.34f) * DenseGridStep,
				-1120.0f + (GridY + 0.5f) * DenseGridStep + Stream.FRandRange(-0.34f, 0.34f) * DenseGridStep);
			Point.X = FMath::Clamp(Point.X, -1120.0f, 1120.0f);
			Point.Y = FMath::Clamp(Point.Y, -1120.0f, 1120.0f);
		}
		else for (int32 Attempt = 0; Attempt < 4; ++Attempt)
		{
			if (Stream.FRand() < 0.48f)
			{
				const FVector2D& Center = PatchCenters[Index % UE_ARRAY_COUNT(PatchCenters)];
				Point = Center + FVector2D(Stream.FRandRange(-360.0f, 360.0f), Stream.FRandRange(-360.0f, 360.0f));
			}
			else
			{
				Point = FVector2D(Stream.FRandRange(-1120.0f, 1120.0f), Stream.FRandRange(-1120.0f, 1120.0f));
			}
			Point.X = FMath::Clamp(Point.X, -1120.0f, 1120.0f);
			Point.Y = FMath::Clamp(Point.Y, -1120.0f, 1120.0f);
			const bool bInsideTrail = !bDenseNature
				&& (bVerticalTrail ? FMath::Abs(Point.X) < 105.0f : FMath::Abs(Point.Y) < 105.0f);
			if (!bInsideTrail)
				break;
		}

		UInstancedStaticMeshComponent* GrassComponent = GrassDressing;
		const float Choice = Stream.FRand();
		if (Choice > 0.55f) GrassComponent = GrassDressingB;
		if (Choice > 0.80f) GrassComponent = GrassDressingC;
		// 목표 높이를 원본 메시와 따로 고른다. 그래야 타일마다 매번 똑같은
		// 작은/중간/큰 섞임이 생기고, 메시 세 개가 모두 같은 높이로
		// 똑같이 끝나는 실루엣이 되지 않는다.
		const float HeightChoice = Stream.FRand();
		const float TargetHeight = HeightChoice < 0.15f
			? Stream.FRandRange(48.0f, 65.0f)
			: (HeightChoice < 0.70f
				? Stream.FRandRange(70.0f, 92.0f)
				: Stream.FRandRange(98.0f, 125.0f));
		float HorizontalScale = Stream.FRandRange(2.2f, 3.0f);
		if (GrassComponent == GrassDressingB)
		{
			HorizontalScale = Stream.FRandRange(3.2f, 4.4f);
		}
		else if (GrassComponent == GrassDressingC)
		{
			HorizontalScale = Stream.FRandRange(2.4f, 3.4f);
		}
		const UStaticMesh* GrassMesh = GrassComponent->GetStaticMesh();
		const float SourceHeight = IsValid(GrassMesh)
			? FMath::Max(1.0f, GrassMesh->GetBoundingBox().GetSize().Z)
			: 80.0f;
		const float VerticalScale = TargetHeight / SourceHeight;
		const float GroundedZ = IsValid(GrassMesh)
			? -GrassMesh->GetBoundingBox().Min.Z * VerticalScale + 1.0f
			: 5.0f;
		GrassComponent->AddInstance(FTransform(
			FRotator(0, Stream.FRandRange(0, 360), 0),
			FVector(Point.X, Point.Y, GroundedZ),
			FVector(HorizontalScale, HorizontalScale, VerticalScale)));
	}
}

void ATacticalTileActor::AddTree(
	const FVector& Location,
	float Height,
	float CanopyRadius,
	FRandomStream& Stream)
{
	// 공업 전투 중심은 잘 보이게 둔다. 나무는 바깥 완충 띠에는 두지만,
	// 중심/중간 WarZone 칸마다 반복하지는 않는다.
	if (Tags.Contains(TEXT("WarZone_Core")) || Tags.Contains(TEXT("WarZone_Mid")))
		return;

	const bool bUseTallPine = Stream.FRand() < 0.72f;
	UInstancedStaticMeshComponent* TreeComponent = bUseTallPine ? TreeTrunks : TreeCanopies;
	const UStaticMesh* TreeMesh = TreeComponent->GetStaticMesh();
	if (!IsValid(TreeMesh))
		return;

	const FBox MeshBounds = TreeMesh->GetBoundingBox();
	const FVector MeshSize = MeshBounds.GetSize().ComponentMax(FVector(1.0f));
	const float HorizontalScale = FMath::Clamp((CanopyRadius * 2.0f) / FMath::Max(MeshSize.X, MeshSize.Y), 0.35f, 2.5f);
	const float VerticalScale = FMath::Clamp(Height / MeshSize.Z, 0.35f, 2.5f);
	// 런타임 타일 액터 자체는 이미 공통 +20 cm 걷는 높이에 스폰된다. 시골 나무 뿌리는
	// 중심점 주변에서 위로 휘어 있어서, 중심점을 땅에 맞춰도 나무가 떠 보일 수 있다.
	// 줄기 밑동을 살짝 묻는다. 메시는 원래 중심점 아래까지 내려가 있다.
	constexpr float GroundedZ = -30.0f;
	const FVector SafeLocation(
		FMath::Clamp(Location.X, -640.0f, 640.0f),
		FMath::Clamp(Location.Y, -640.0f, 640.0f),
		Location.Z + GroundedZ);
	TreeComponent->AddInstance(FTransform(
		FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f),
		SafeLocation,
		FVector(HorizontalScale, HorizontalScale, VerticalScale)));
}

void ATacticalTileActor::AddRock(const FVector& Location, const FVector& Size, float Yaw)
{
	UInstancedStaticMeshComponent* TargetRock = FMath::Max3(Size.X, Size.Y, Size.Z) >= 300.0f
		? RockCover : RockCoverB;
	const UStaticMesh* RockMesh = TargetRock->GetStaticMesh();
	if (!IsValid(RockMesh))
		return;

	const FBox MeshBounds = RockMesh->GetBoundingBox();
	const FVector MeshSize = MeshBounds.GetSize().ComponentMax(FVector(1.0f));
	const FVector InstanceScale(Size.X / MeshSize.X, Size.Y / MeshSize.Y, Size.Z / MeshSize.Z);
	// 둥근 바위는 범위 상자 최저점에서 바닥에 닿는 일이 거의 없다. 그 최저점을 공통 표면보다
	// 18 cm 아래로 묻어서 보이는 덩어리가 땅에 박혀 보이게 한다.
	const float GroundedZ = -MeshBounds.Min.Z * InstanceScale.Z - 18.0f;
	TargetRock->AddInstance(FTransform(
		FRotator(0.0f, Yaw, 0.0f),
		Location + FVector(0.0f, 0.0f, GroundedZ),
		InstanceScale));
}

void ATacticalTileActor::AddBushCluster(
	const FVector& Center,
	int32 Count,
	float Radius,
	FRandomStream& Stream)
{
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const float Angle = Stream.FRandRange(0.0f, 2.0f * PI);
		const float Distance = FMath::Sqrt(Stream.FRand()) * Radius;
		const FVector Location = Center + FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 8.0f);
		const float Scale = Stream.FRandRange(0.72f, 1.28f);
		UInstancedStaticMeshComponent* BushComponent = Index % 3 == 0 ? BushDressingB : BushDressing;
		BushComponent->AddInstance(FTransform(
			FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f),
			Location,
			FVector(Scale, Scale, Stream.FRandRange(0.82f, 1.18f) * Scale)));
	}
	if (Count >= 7 && IsValid(HeroShrubDressing->GetStaticMesh()))
	{
		const FVector HeroLocation = Center + FVector(
			Stream.FRandRange(-Radius * 0.45f, Radius * 0.45f),
			Stream.FRandRange(-Radius * 0.45f, Radius * 0.45f),
			2.0f);
		const float HeroScale = Stream.FRandRange(0.72f, 1.05f);
		HeroShrubDressing->AddInstance(FTransform(
			FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f),
			HeroLocation,
			FVector(HeroScale)));
	}
}

void ATacticalTileActor::AddFallenLog(
	const FVector& Location,
	float Yaw,
	float Length,
	float Radius)
{
	// 원기둥 임시 모양은 뺐다. 호출 위치는 남겨 둬서, 나중에 제대로 된 통나무 메시가
	// 타일을 다시 설계하지 않고 이 구현을 대신할 수 있게 한다.
	(void)Location;
	(void)Yaw;
	(void)Length;
	(void)Radius;
}

void ATacticalTileActor::AddNatureLayout(
	ETacticalTileKind NatureKind,
	uint8 Variant,
	FRandomStream& Stream)
{
	auto Tree = [this, &Stream](float X, float Y, float HeightScale = 1.0f)
	{
		AddTree(
			FVector(X + Stream.FRandRange(-45.0f, 45.0f), Y + Stream.FRandRange(-45.0f, 45.0f), 0.0f),
			Stream.FRandRange(720.0f, 980.0f) * HeightScale,
			Stream.FRandRange(175.0f, 245.0f) * HeightScale,
			Stream);
	};

	switch (NatureKind)
	{
	case ETacticalTileKind::NatureMeadow:
		AddGrass(1000, Stream);
		Tree(Variant % 2 == 0 ? -760.0f : 730.0f, 690.0f, 0.92f);
		if (Variant == 2) Tree(720.0f, -710.0f, 0.85f);
		AddBushCluster(FVector(-610.0f, -580.0f, 0.0f), 5, 150.0f, Stream);
		AddRock(FVector(520.0f, -420.0f, 0.0f), FVector(220.0f, 150.0f, 105.0f), 25.0f);
		if (Variant != 3)
			AddRock(FVector(0.0f, Variant % 2 == 0 ? 520.0f : -520.0f, 0.0f), FVector(270.0f, 230.0f, 210.0f), Variant * 17.0f);
		break;

	case ETacticalTileKind::NatureForestSparse:
		AddGrass(800, Stream);
		Tree(-760.0f, -680.0f); Tree(-690.0f, 570.0f, 0.9f);
		Tree(650.0f, -620.0f, 1.08f); Tree(730.0f, 650.0f);
		if (Variant % 2 == 0) Tree(-120.0f, 760.0f, 0.92f);
		else Tree(190.0f, -760.0f, 0.92f);
		AddBushCluster(FVector(Variant % 2 == 0 ? 430.0f : -430.0f, 120.0f, 0.0f), 7, 210.0f, Stream);
		AddFallenLog(FVector(-120.0f, -190.0f, 0.0f), Variant % 2 == 0 ? 25.0f : -35.0f, 430.0f, 34.0f);
		AddRock(FVector(0.0f, Variant % 2 == 0 ? 510.0f : -510.0f, 0.0f), FVector(260.0f, 220.0f, 205.0f), -15.0f);
		break;

	case ETacticalTileKind::NatureForestDense:
		AddGrass(1200, Stream);
		Tree(-820.0f, -760.0f, 1.08f); Tree(-800.0f, -180.0f); Tree(-760.0f, 650.0f, 0.92f);
		Tree(790.0f, -720.0f); Tree(820.0f, -80.0f, 1.05f); Tree(760.0f, 700.0f, 0.95f);
		Tree(-250.0f, 760.0f, 1.08f); Tree(300.0f, -760.0f, 0.9f);
		if (Variant == 1 || Variant == 3) Tree(340.0f, 520.0f, 0.86f);
		else Tree(-360.0f, -500.0f, 0.88f);
		AddBushCluster(FVector(-430.0f, 260.0f, 0.0f), 8, 240.0f, Stream);
		AddBushCluster(FVector(470.0f, -250.0f, 0.0f), 8, 240.0f, Stream);
		AddRock(FVector(0.0f, Variant % 2 == 0 ? 450.0f : -450.0f, 0.0f), FVector(290.0f, 240.0f, 230.0f), Variant * 12.0f);
		break;

	case ETacticalTileKind::NatureRocky:
		AddGrass(220, Stream);
		Tree(-720.0f, 680.0f, 0.9f); Tree(750.0f, -650.0f, 0.95f); Tree(-730.0f, -690.0f, 0.82f);
		AddRock(FVector(-420.0f, -180.0f, 0.0f), FVector(360.0f, 250.0f, 230.0f), 25.0f);
		AddRock(FVector(40.0f, 360.0f, 0.0f), FVector(470.0f, 310.0f, 270.0f), -18.0f);
		AddRock(FVector(520.0f, 60.0f, 0.0f), FVector(280.0f, 220.0f, 175.0f), 60.0f);
		AddRock(FVector(-120.0f, -610.0f, 0.0f), FVector(230.0f, 180.0f, 135.0f), 5.0f);
		AddFallenLog(FVector(400.0f, 590.0f, 0.0f), 65.0f, 390.0f, 32.0f);
		break;

	case ETacticalTileKind::NatureScrub:
		AddGrass(850, Stream);
		Tree(-780.0f, Variant % 2 == 0 ? 650.0f : -650.0f, 0.84f);
		Tree(760.0f, Variant % 2 == 0 ? -620.0f : 620.0f, 0.9f);
		for (int32 Index = 0; Index < 5; ++Index)
		{
			const float X = -620.0f + Index * 310.0f;
			const float Y = (Index % 2 == 0 ? -1.0f : 1.0f) * (270.0f + Variant * 35.0f);
			AddBushCluster(FVector(X, Y, 0.0f), 5, 135.0f, Stream);
		}
		AddRock(FVector(80.0f, 40.0f, 0.0f), FVector(240.0f, 170.0f, 125.0f), 15.0f);
		AddRock(FVector(0.0f, Variant % 2 == 0 ? 490.0f : -490.0f, 0.0f), FVector(275.0f, 225.0f, 215.0f), -20.0f);
		break;

	case ETacticalTileKind::NatureAmbush:
		AddGrass(1200, Stream);
		Tree(-820.0f, -720.0f); Tree(-800.0f, 680.0f, 0.94f); Tree(810.0f, -660.0f, 1.05f); Tree(790.0f, 720.0f);
		Tree(Variant % 2 == 0 ? -410.0f : 410.0f, Variant % 2 == 0 ? 500.0f : -500.0f, 0.9f);
		AddFallenLog(FVector(-210.0f, 80.0f, 0.0f), 18.0f + Variant * 12.0f, 560.0f, 42.0f);
		AddFallenLog(FVector(420.0f, -230.0f, 0.0f), -38.0f, 420.0f, 35.0f);
		AddRock(FVector(230.0f, 360.0f, 0.0f), FVector(300.0f, 230.0f, 175.0f), -20.0f);
		AddRock(FVector(0.0f, Variant % 2 == 0 ? -500.0f : 500.0f, 0.0f), FVector(280.0f, 230.0f, 220.0f), 20.0f);
		AddBushCluster(FVector(-520.0f, -240.0f, 0.0f), 8, 200.0f, Stream);
		break;

	case ETacticalTileKind::NatureServiceCamp:
		AddGrass(180, Stream);
		Tree(-820.0f, -730.0f); Tree(-800.0f, 720.0f, 0.94f); Tree(820.0f, 690.0f); Tree(790.0f, -720.0f, 0.9f);
		AddShelter(FVector(-420.0f, 390.0f, 0.0f), Variant % 2 == 0 ? 0.0f : 180.0f, true);
		ConcreteCover->AddInstance(FTransform(FRotator(0.0f, 25.0f, 0.0f), FVector(360.0f, -210.0f, 8.0f), FVector(1.45f)));
		ConcreteCover->AddInstance(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(0.0f, Variant % 2 == 0 ? -520.0f : 520.0f, 8.0f), FVector(1.25f)));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(520.0f, 440.0f, 0.0f), FVector::OneVector));
		AddFallenLog(FVector(-100.0f, -590.0f, 0.0f), 12.0f, 460.0f, 34.0f);
		break;

	case ETacticalTileKind::NatureDitch:
		AddGrass(260, Stream);
		// 군데군데 끊긴 낮은 둑. 넘어갈 수 있는 곳 네 군데는 걸을 수 있고,
		// 번갈아 바뀌는 높이가 20m 타일 안에 진짜로 앉기/서기 높이 차이를 만든다.
		//
		// 예전엔 흙 머티리얼을 입힌 엔진 큐브 인스턴스였는데, 트인 풀밭에선 땅이 파인 도랑이 아니라
		// 들판에 떨어뜨린 네모 판처럼 보였다 - NatureDitch 칸마다 타일당 여덟 개씩.
		// 바위 노두는 차지 면적, 높이, 엄폐 효과는 같으면서
		// 실제로 지형처럼 보인다.
		for (int32 Bank = 0; Bank < 4; ++Bank)
		{
			const float Y = -690.0f + Bank * 460.0f;
			const float BankHeight = 65.0f + ((Bank + Variant) % 2) * 25.0f;
			const float Angle = (Bank % 2 == 0 ? 7.0f : -9.0f) * (Variant % 2 == 0 ? 1.0f : -1.0f);
			AddRock(FVector(-470.0f, Y, 0.0f), FVector(540.0f, 330.0f, BankHeight), Angle);
			AddRock(FVector(470.0f, Y + 95.0f, 0.0f), FVector(540.0f, 300.0f, BankHeight), -Angle);
		}
		Tree(-800.0f, -650.0f, 0.94f); Tree(-760.0f, 680.0f); Tree(790.0f, -700.0f); Tree(820.0f, 670.0f, 0.9f);
		AddFallenLog(FVector(0.0f, Variant % 2 == 0 ? 430.0f : -430.0f, 0.0f), 90.0f, 520.0f, 32.0f);
		AddBushCluster(FVector(0.0f, -650.0f, 0.0f), 7, 210.0f, Stream);
		break;

	default:
		break;
	}
}

void ATacticalTileActor::AddLowWall(const FVector& Location, float Yaw, float LengthScale)
{
	HalfWalls->AddInstance(FTransform(
		FRotator(0.0f, Yaw, 0.0f),
		Location,
		FVector(1.0f, FMath::Clamp(LengthScale, 0.65f, 2.5f), 0.75f)));
}

void ATacticalTileActor::AddOpenGroundLayout(uint8 Variant, FRandomStream& Stream)
{
	// 이 넷은 겉모양만 다른 흩뿌리기가 아니라 서로 다른 전투 리듬이다:
	// 능선 = 옆 방향 엄폐, 도랑 = 엇갈린 접근, 잔해 = 단단한 랜드마크,
	// 덤불 = 비상 엄폐만 있는 대체로 트인 긴 시야.
	switch (Variant % 4)
	{
	case 0: // 낮은 잔해 능선, 양옆 두 방향은 트여 있음
		AddLowWall(FVector(-320, 80, 0), 18, 1.5f);
		AddLowWall(FVector(80, -40, 0), 18, 1.25f);
		QuarterWalls->AddInstance(FTransform(FRotator(0, 70, 0), FVector(530, -410, 0), FVector(1.0f, 1.0f, 1.2f)));
		AddGrass(120, Stream);
		break;
	case 1: // 엇갈린 도랑 / 접근용 엄폐
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Yaw = Index % 2 == 0 ? -32.0f : 34.0f;
			AddLowWall(FVector(-520 + Index * 480, Index % 2 == 0 ? -310 : 300, 0), Yaw, 1.15f);
		}
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(650, 500, 0), FVector::OneVector));
		AddGrass(140, Stream);
		break;
	case 2: // 눈에 띄는 잔해/전리품 랜드마크와 비켜 놓은 맞엄폐물
		ConcreteCover->AddInstance(FTransform(FRotator(0, 28, 0), FVector(480, -330, 8), FVector(1.45f)));
		AddLowWall(FVector(-500, 370, 0), -18, 1.35f);
		UtilityProps->AddInstance(FTransform(FRotator(0, 110, 0), FVector(-120, -520, 0), FVector(0.8f)));
		AddGrass(110, Stream);
		break;
	default: // 긴 시야 타일, 가운데는 일부러 잘 보이게 비움
		QuarterWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-760, 520, 0), FVector(1.0f, 1.0f, 1.2f)));
		QuarterWalls->AddInstance(FTransform(FRotator(0, -90, 0), FVector(720, -540, 0), FVector(1.0f, 1.0f, 1.2f)));
		// 낮은 옆 방향 둑으로 200m 넘게 노출된 구간을 끊되,
		// 앉은 높이 위로는 긴 시야를 남긴다.
		AddLowWall(FVector(0, Variant % 2 == 0 ? 620 : -620, 0), 90, 1.15f);
		AddGrass(160, Stream);
		break;
	}
	if ((Variant % 4) != 3)
	{
		// 그 밖에 트인 20m 이동 칸마다 무릎 높이 피신 자리를 하나씩 준다.
		// 좌우를 번갈아 두면 넓은 시야는 남기면서
		// 140m 넘게 완전히 노출되는 구간이 반복되지 않는다.
		AddLowWall(FVector(0, Variant % 2 == 0 ? 720 : -720, 0), 90, 0.85f);
	}
}

void ATacticalTileActor::AddRuinsLayout(uint8 Variant, FRandomStream& Stream)
{
	// 폐허 덩어리는 네 면을 다 막지 않고 타일 안쪽에만 둔다.
	// 첫 전체 맵 시도에서 보인 빽빽한 바둑판 벽 무늬를 피하기 위해서다.
	switch (Variant % 4)
	{
	case 0: // L자로 무너진 방, 트인 진입로 두 개
		for (int32 Index = 0; Index < 4; ++Index)
			SolidWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-550 + Index * 200, 520, 0), FVector::OneVector));
		HalfWalls->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-550, 320, 0), FVector(1, 1.6f, 1)));
		QuarterWalls->AddInstance(FTransform(FRotator(0, 35, 0), FVector(220, 130, 0), FVector(1.2f)));
		break;
	case 1: // 나란한 무너진 통로 두 줄과 비스듬한 사격 틈
		for (int32 Index = 0; Index < 3; ++Index)
		{
			HalfWalls->AddInstance(FTransform(FRotator(0, 18, 0), FVector(-500 + Index * 260, -280, 0), FVector::OneVector));
			QuarterWalls->AddInstance(FTransform(FRotator(0, -20, 0), FVector(100 + Index * 230, 350, 0), FVector::OneVector));
		}
		ShootingWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-610, 350, 0), FVector::OneVector));
		break;
	case 2: // 작은 안마당. 2.4m 출입구가 서로 반대 대각선에 남는다
		for (int32 Index = -2; Index <= 2; ++Index)
		{
			if (Index != 0) SolidWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(Index * 200, 610, 0), FVector::OneVector));
			if (Index != 1) HalfWalls->AddInstance(FTransform(FRotator(0, -90, 0), FVector(Index * 200, -610, 0), FVector::OneVector));
		}
		AddLowWall(FVector(-590, 0, 0), 0, 1.8f);
		break;
	default: // 대각선 잔해 줄기. 엄폐물로 쓰이되 미로 벽이 되지는 않음
		for (int32 Index = 0; Index < 5; ++Index)
			HalfWalls->AddInstance(FTransform(FRotator(0, 42, 0), FVector(-520 + Index * 250, -480 + Index * 220, 0), FVector::OneVector));
		ConcreteCover->AddInstance(FTransform(FRotator(0, -42, 0), FVector(420, -430, 8), FVector(1.35f)));
		break;
	}
	AddGrass(80 + (Variant % 3) * 15, Stream);
}

void ATacticalTileActor::AddRoadTacticalCover(uint8 Mask, uint8 Variant, FRandomStream& Stream)
{
	using namespace TacticalTile;
	if ((Variant % 4) == 3 && !bIsAccessRoad)
		return; // 일부러 깨끗하게 트인 도로 구간도 필요하다

	const bool bHorizontal = (Mask & (East | West)) != 0;
	const float Side = (Variant % 2 == 0) ? 1.0f : -1.0f;
	const FVector CoverLocation = bHorizontal
		? FVector(Stream.FRandRange(-520, 520), Side * 520, 8)
		: FVector(Side * 520, Stream.FRandRange(-520, 520), 8);
	ConcreteCover->AddInstance(FTransform(
		FRotator(0, bHorizontal ? Stream.FRandRange(-25, 25) : 90 + Stream.FRandRange(-25, 25), 0),
		CoverLocation,
		FVector(Stream.FRandRange(1.15f, 1.55f))));

	if (bIsAccessRoad)
	{
		// 연결로는 수백 미터 이어질 수 있다. 양쪽 갓길에 번갈아 진짜 앉은 높이 피신 자리를
		// 하나씩 두고 풀/나무를 섞는다. 그러면 치명적인 200m+ 노출 구간을 끊으면서도
		// 벽으로 된 터널을 다시 만들지 않는다.
		const FVector ShoulderCover = bHorizontal
			? FVector(0.0f, -Side * 690.0f, 0.0f)
			: FVector(-Side * 690.0f, 0.0f, 0.0f);
		AddLowWall(ShoulderCover, bHorizontal ? Stream.FRandRange(-18.0f, 18.0f) : 90.0f + Stream.FRandRange(-18.0f, 18.0f), 0.9f);
		const FVector RockLocation = bHorizontal
			? FVector(Stream.FRandRange(-700.0f, 700.0f), Side * 790.0f, 0.0f)
			: FVector(Side * 790.0f, Stream.FRandRange(-700.0f, 700.0f), 0.0f);
		const float RockScale = Variant % 2 == 0 ? 1.0f : 0.82f;
		AddRock(
			RockLocation,
			FVector(260.0f, 220.0f, 230.0f) * RockScale,
			Stream.FRandRange(0.0f, 180.0f));
		AddGrass(50, Stream);
	}
}

void ATacticalTileActor::ApplyDynamicProps(FRandomStream& Stream)
{
	UClass* Car = CarClass.LoadSynchronous();
	UClass* Barrels = BarrelClass.LoadSynchronous();
	UClass* Barrier = BarrierClass.LoadSynchronous();
	if (!bShowDynamicProps)
	{
		DynamicPropA->SetChildActorClass(nullptr); DynamicPropB->SetChildActorClass(nullptr); DynamicPropC->SetChildActorClass(nullptr); return;
	}
	const bool bNaturalTile = TileKind >= ETacticalTileKind::NatureMeadow
		&& TileKind <= ETacticalTileKind::NatureDitch;
	if (bNaturalTile && TileKind != ETacticalTileKind::NatureServiceCamp)
	{
		DynamicPropA->SetChildActorClass(nullptr);
		DynamicPropB->SetChildActorClass(nullptr);
		DynamicPropC->SetChildActorClass(nullptr);
		return;
	}
	DynamicPropA->SetChildActorClass(
		TileKind == ETacticalTileKind::WarZoneYard
		|| TileKind == ETacticalTileKind::SpawnStaging
		|| TileKind == ETacticalTileKind::NatureServiceCamp
			? Car : Barrels);
	DynamicPropB->SetChildActorClass(Stream.RandRange(0, 1) ? Barrier : Barrels);
	DynamicPropC->SetChildActorClass(
		TileKind == ETacticalTileKind::OpenGround ? nullptr : Barrier);
}

void ATacticalTileActor::RebuildTile()
{
	using namespace TacticalTile;
	// 기존 Nature 블루프린트는 컴포넌트 기본값을 저장하고 있으므로, 런타임 지형에서
	// 잘 보이는 것이 확인된 Rural 풀 세 종류로 직접 되돌린다.
	static UStaticMesh* GrassPatchA = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_2_Runtime.SM_GrassPatch_2_Runtime"));
	static UStaticMesh* GrassPatchB = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_1_Runtime.SM_GrassPatch_1_Runtime"));
	static UStaticMesh* GrassPatchLong = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_Long_Runtime.SM_GrassPatch_Long_Runtime"));
	static UStaticMesh* DowntownRockLarge = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_large_a_low.SM_rock_large_a_low"));
	static UStaticMesh* DowntownRockMedium = LoadObject<UStaticMesh>(nullptr,
		TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_medium_a_low.SM_rock_medium_a_low"));
	if (IsValid(GrassPatchA)) GrassDressing->SetStaticMesh(GrassPatchA);
	if (IsValid(GrassPatchB)) GrassDressingB->SetStaticMesh(GrassPatchB);
	if (IsValid(GrassPatchLong)) GrassDressingC->SetStaticMesh(GrassPatchLong);
	// 처음의 빽빽한 버전은 움직이지 않게 둔다. 담아 둔 HISM 에서도 안전한 상호작용 머티리얼은
	// 보이는 것과 성능을 확인한 뒤 따로 추가할 수 있다.
	GrassDressing->SetEvaluateWorldPositionOffset(false);
	GrassDressingB->SetEvaluateWorldPositionOffset(false);
	GrassDressingC->SetEvaluateWorldPositionOffset(false);
	BushDressing->SetEvaluateWorldPositionOffset(false);
	BushDressingB->SetEvaluateWorldPositionOffset(false);
	// 예전 블루프린트 자식은 아주 작은 Rural Rock_1 컴포넌트 메시를 저장해 두었다.
	// AddRock 이 크기를 계산하기 전에 Downtown 바위의 실제 크기를 다시 넣는다.
	// 안 그러면 3 m 바위를 요청해도 눈에 띄게 8배 큰 인스턴스가 된다.
	if (IsValid(DowntownRockLarge)) RockCover->SetStaticMesh(DowntownRockLarge);
	if (IsValid(DowntownRockMedium)) RockCoverB->SetStaticMesh(DowntownRockMedium);
	// Rural 소나무 가지 머티리얼은 이 담아 둔 런타임 배치에서 HISM 범위 전체를 위아래로 움직인다.
	// 나무의 WPO 를 꺼서 줄기가 땅에 박혀 있게 한다.
	TreeTrunks->SetEvaluateWorldPositionOffset(false);
	TreeCanopies->SetEvaluateWorldPositionOffset(false);

	static UMaterialInterface* UnifiedGround = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
	// Rural 에셋 머티리얼은 그대로 둔다. 예전 런타임 색 입히기는
	// 레벨의 밝은 직사광 아래서 풀을 분필처럼 하얗게 보이게 했다.
	if (IsValid(GrassPatchA) && IsValid(GrassPatchA->GetMaterial(0)))
		GrassDressing->SetMaterial(0, GrassPatchA->GetMaterial(0));
	if (IsValid(GrassPatchB) && IsValid(GrassPatchB->GetMaterial(0)))
		GrassDressingB->SetMaterial(0, GrassPatchB->GetMaterial(0));
	if (IsValid(GrassPatchLong) && IsValid(GrassPatchLong->GetMaterial(0)))
		GrassDressingC->SetMaterial(0, GrassPatchLong->GetMaterial(0));

	// 모든 타일은 마르고 반사 없는 밑판 하나에서 시작한다. 도로와 손작업 시설 바닥은
	// 그 위에 겹쳐 놓이므로, 갓길에 블루프린트마다 다른 무작위 머티리얼이나
	// 반짝이는 Diorama_Ground 물 섞기가 더는 드러나지 않는다.
	if (IsValid(UnifiedGround)) Ground->SetMaterial(0, UnifiedGround);
	// 기본 자연/시설 타일과 담아 둔 LD 타일은 같은 +20 cm 걷는 면을 써야 한다.
	// 블루프린트에서 온 기본 컴포넌트는 예전 CDO 오프셋을 들고 있을 수 있으므로,
	// 기본값에 기대지 말고 rebuild 할 때마다 높이를 강제로 맞춘다.
	FVector GroundLocation = Ground->GetRelativeLocation();
	GroundLocation.Z = 5.0f;
	Ground->SetRelativeLocation(GroundLocation);
	// 도로/시설 타일이 연결 마스크에 맞춰 회전해도 밑판 UV 방향은 똑같이 유지한다.
	// 네모 메시는 방향과 상관없지만, 텍스처가 돌아가면
	// 20 m 경계가 눈에 띈다.
	Ground->SetWorldRotation(FRotator::ZeroRotator);
	const bool bWarZoneCore = Tags.Contains(TEXT("WarZone_Core"));
	const bool bWarZoneMid = Tags.Contains(TEXT("WarZone_Mid"));
	// WarZone 은 주변 맵과 같은 지형 밑판을 쓴다. WarZone 다운 느낌은 구조물, 공업 소품,
	// 엄폐물 밀도, 위험 표시에서 나온다.
	// 1x1 땅 머티리얼을 다르게 쓰면 절차 격자가 그대로 드러날 수밖에 없다.

	for (UInstancedStaticMeshComponent* Component : {
		RoadPieces, SolidWalls, HalfWalls, QuarterWalls, ShootingWalls,
		DoorWalls, WindowWalls, RoofFloors, ConcreteCover, BarrelCover,
		GrassDressing, GrassDressingB, GrassDressingC,
		TreeTrunks, TreeCanopies, RockCover,
		RockCoverB,
		BushDressing, BushDressingB, HeroShrubDressing,
		FallenLogs, TerrainBerms, ElevationStairs, MarkingPieces, UtilityProps,
		FactoryContainers, FactoryTanks, FactoryPallets, FactoryFences})
	{
		FVector ComponentLocation = Component->GetRelativeLocation();
		ComponentLocation.Z = 20.0f;
		Component->SetRelativeLocation(ComponentLocation);
		Component->ClearInstances();
	}
	FRandomStream Stream(LocalSeed + static_cast<int32>(TileKind) * 7919);
	const uint8 Mask = GetEffectiveConnectionMask();
	const uint8 Variant = GetEffectiveLayoutVariant();
	ConnectionNorth->SetVisibility((Mask & North) != 0);
	ConnectionEast->SetVisibility((Mask & East) != 0);
	ConnectionSouth->SetVisibility((Mask & South) != 0);
	ConnectionWest->SetVisibility((Mask & West) != 0);
	SpawnPoint->SetVisibility(TileKind == ETacticalTileKind::SpawnStaging);
	ExitPoint->SetVisibility(TileKind == ETacticalTileKind::ExitCheckpoint);

	const bool bUsesAuthoredRoadSurface =
		TileKind == ETacticalTileKind::RoadStraight
		|| TileKind == ETacticalTileKind::RoadCorner
		|| TileKind == ETacticalTileKind::RoadTJunction
		|| TileKind == ETacticalTileKind::RoadCross
		|| TileKind == ETacticalTileKind::RoadDeadEnd
		|| TileKind == ETacticalTileKind::SpawnStaging
		|| TileKind == ETacticalTileKind::ExitCheckpoint
		|| TileKind == ETacticalTileKind::ObstacleCheckpoint;
	if (Mask && bUsesAuthoredRoadSurface)
	{
		AddRoad(Mask, 650.0f);
		AddRoadShoulderDressing(Mask, Variant, Stream);
	}

	switch (TileKind)
	{
	case ETacticalTileKind::RoadStraight:
		AddRoadTacticalCover(Mask, Variant, Stream); break;
	case ETacticalTileKind::RoadCorner:
		AddBrokenBoundary(Mask, 2); AddShelter(FVector(570, -570, 0), 180, true); AddRoadTacticalCover(Mask, Variant, Stream); break;
	case ETacticalTileKind::RoadTJunction:
		AddBrokenBoundary(Mask, 2); AddShelter(FVector(0, -650, 0), 90, false); AddRoadTacticalCover(Mask, Variant, Stream); break;
	case ETacticalTileKind::RoadCross:
		ConcreteCover->AddInstance(FTransform(FRotator(0, Variant % 2 ? 25 : 45, 0), FVector(-500, 500, 8), FVector(1.35f)));
		if (Variant != 3) ConcreteCover->AddInstance(FTransform(FRotator(0, Variant % 2 ? -65 : -45, 0), FVector(500, -500, 8), FVector(1.35f))); break;
	case ETacticalTileKind::RoadDeadEnd:
		AddBrokenBoundary(Mask, 3); AddShelter(FVector(520, 0, 0), 180, true); ConcreteCover->AddInstance(FTransform(FRotator(0, 90, 0), FVector(180, Variant % 2 ? 120 : -120, 8), FVector(2.0f))); break;
	case ETacticalTileKind::SpawnStaging:
		AddBrokenBoundary(Mask, 2);
		AddShelter(FVector(-620, Variant % 2 == 0 ? 520 : 430, 0), 0, true);
		AddShelter(FVector(-620, Variant % 2 == 0 ? -520 : -610, 0), 0, false);
		AddLowWall(FVector(-120, Variant % 2 == 0 ? 520 : -520, 0), 90, 1.4f);
		break;
	case ETacticalTileKind::ExitCheckpoint:
		AddBrokenBoundary(Mask, 2);
		AddShelter(FVector(100, Variant % 2 == 0 ? 650 : -650, 0), Variant % 2 == 0 ? -90 : 90, true);
		// 깔끔한 앞쪽 화살표 하나: 네모 몸통과 겹치지 않는 머리 두 개.
		// 예전 텍스처식 표시는 서로 교차하는 빨간 면들을 만들었다.
		MarkingPieces->AddInstance(Box(FVector(180, 0, 9), FVector(430, 105, 2)));
		MarkingPieces->AddInstance(Box(FVector(465, 105, 9), FVector(300, 95, 2), 35));
		MarkingPieces->AddInstance(Box(FVector(465, -105, 9), FVector(300, 95, 2), -35));
		// 빨간 경계 막대 두 개로 타일의 발동 절반을 멀리서도 알아보게 한다.
		// 복제되는 글자나 빌보드 액터가 필요 없다.
		MarkingPieces->AddInstance(Box(FVector(650, 285, 9), FVector(70, 300, 2)));
		MarkingPieces->AddInstance(Box(FVector(650, -285, 9), FVector(70, 300, 2)));
		break;
	case ETacticalTileKind::ObstacleCheckpoint:
		AddBrokenBoundary(Mask, 2);
		for (int32 Index = 0; Index < 4; ++Index)
			ConcreteCover->AddInstance(FTransform(
				FRotator(0, Index % 2 ? 20 + Variant * 3 : -20 - Variant * 3, 0),
				FVector(-520 + Index * 340, Index % 2 ? 190 : -190, 8), FVector(1.8f)));
		UtilityProps->AddInstance(FTransform(FRotator(0, 90, 0), FVector(Variant % 2 ? 650 : -650, 520, 0), FVector(0.8f)));
		break;
	case ETacticalTileKind::OpenGround:
		AddOpenGroundLayout(Variant, Stream); break;
	case ETacticalTileKind::Ruins:
		AddRuinsLayout(Variant, Stream); break;
	case ETacticalTileKind::NatureMeadow:
	case ETacticalTileKind::NatureForestSparse:
	case ETacticalTileKind::NatureForestDense:
	case ETacticalTileKind::NatureRocky:
	case ETacticalTileKind::NatureScrub:
	case ETacticalTileKind::NatureAmbush:
	case ETacticalTileKind::NatureServiceCamp:
	case ETacticalTileKind::NatureDitch:
		AddNatureLayout(TileKind, Variant, Stream); break;
	case ETacticalTileKind::WarZoneYard:
		AddBrokenBoundary(Mask, Variant % 2 == 0 ? 2 : 1);
		// 칸마다 눈에 띄는 Fab 소품 하나가 예전의 반복되는 CQB 쉼터 한 쌍을 대신한다.
		// 변형은 컨테이너 통로, 탱크 정비장, 팔레트 마당, 무너진 울타리 마당으로 보이고,
		// 가운데 전투 통로는 남겨 둔다.
		if (Variant == 0)
		{
			FactoryContainers->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-620, -250, 5), FVector(0.72f)));
			FactoryPallets->AddInstance(FTransform(FRotator(0, 12, 0), FVector(520, 520, 12), FVector(1.15f)));
		}
		else if (Variant == 1)
		{
			FactoryTanks->AddInstance(FTransform(FRotator(0, -20, 0), FVector(-420, 250, 0), FVector(1.15f)));
			FactoryFences->AddInstance(FTransform(FRotator(0, 90, 0), FVector(720, -480, 0), FVector(1.6f, 1.0f, 1.0f)));
		}
		else if (Variant == 2)
		{
			for (int32 PalletIndex = 0; PalletIndex < 4; ++PalletIndex)
				FactoryPallets->AddInstance(FTransform(FRotator(0, 8.0f * PalletIndex, 0), FVector(-520 + 320 * PalletIndex, 520 - 90 * (PalletIndex & 1), 12), FVector(1.1f)));
		}
		else
		{
			FactoryFences->AddInstance(FTransform(FRotator(0, 0, 0), FVector(-720, -600, 0), FVector(1.7f, 1.0f, 1.0f)));
			FactoryFences->AddInstance(FTransform(FRotator(0, 90, 0), FVector(480, 720, 0), FVector(1.7f, 1.0f, 1.0f)));
		}
		for (int32 Index = 0; Index < 3 + Variant % 2; ++Index)
			ConcreteCover->AddInstance(FTransform(FRotator(0, Stream.FRandRange(0, 180), 0), FVector(Stream.FRandRange(-680, 680), Stream.FRandRange(-680, 680), 8), FVector(Stream.FRandRange(1.15f, 1.65f))));
		// 짧게 끊긴 위험 표시 막대로 플레이어 높이에서 전투 구역임을 알려 준다.
		// 20 m 칸 둘레에 네모 테두리를 그리지 않는다.
		MarkingPieces->AddInstance(Box(FVector(-760, -760, 9), FVector(260, 32, 2), 18));
		MarkingPieces->AddInstance(Box(FVector(720, 690, 9), FVector(220, 32, 2), -24));
		AddGrass(150 + Variant * 18, Stream);
		AddBushCluster(FVector(Variant % 2 == 0 ? 860.0f : -860.0f, Variant < 2 ? -720.0f : 720.0f, 0.0f), 5, 250.0f, Stream);
		break;
	case ETacticalTileKind::WarZoneWarehouse:
		AddBrokenBoundary(Mask, 2);
		// 1x1 창고 별채가 두 모서리를 차지하고, 가운데에 잘 보이는 십자 이동로를 남긴다.
		// 완전히 막힌 창고는 손작업 2x2 POI 로 남는다.
		AddShelter(FVector(Variant % 2 == 0 ? -610 : 610, 570, 0), Variant % 2 == 0 ? 0 : 180, true);
		// 반대쪽 모서리에는 Factory 컨테이너/탱크가 정비용 별채를 이룬다.
		// 창고 칸마다 똑같은 쉼터 두 개가 반복되지 않게 한다.
		if (Variant % 2 == 0)
			FactoryContainers->AddInstance(FTransform(FRotator(0, 0, 0), FVector(360, -690, 5), FVector(0.62f)));
		else
			FactoryTanks->AddInstance(FTransform(FRotator(0, 12, 0), FVector(-520, -320, 0), FVector(1.05f)));
		MarkingPieces->AddInstance(Box(FVector(-700, -730, 9), FVector(300, 38, 2), 12));
		ShootingWalls->AddInstance(FTransform(FRotator(0, Variant % 2 == 0 ? 0 : 180, 0), FVector(Variant % 2 == 0 ? -760 : 760, -120, 0), FVector::OneVector));
		if (Variant < 2) UtilityProps->AddInstance(FTransform(FRotator(0, 35, 0), FVector(350, 420, 0), FVector(0.85f)));
		AddGrass(120 + Variant * 15, Stream);
		AddBushCluster(FVector(Variant % 2 == 0 ? 820.0f : -820.0f, -760.0f, 0.0f), 4, 220.0f, Stream);
		break;
	}

	// 원래 자연/트인 중심 칸에도 공업 느낌을 이어 줘서, 격자 칸을 전부 검게 칠하지 않고도
	// WarZone 이 하나의 구역으로 보이게 한다.
	// 듬성듬성 매번 똑같이 놓아서 예전의 도미노 같은 반복 무늬를 피한다.
	if (bWarZoneCore
		&& TileKind != ETacticalTileKind::WarZoneYard
		&& TileKind != ETacticalTileKind::WarZoneWarehouse
		&& !bUsesAuthoredRoadSurface)
	{
		const int32 IndustrialVariant = FMath::Abs(LocalSeed) % 13;
		if (IndustrialVariant == 0)
		{
			FactoryContainers->AddInstance(FTransform(
				FRotator(0, Variant % 2 == 0 ? 90.0f : 0.0f, 0),
				FVector(Variant % 2 == 0 ? -560.0f : 280.0f, -620.0f, 5.0f),
				FVector(0.58f)));
		}
		else if (IndustrialVariant <= 2)
		{
			FactoryFences->AddInstance(FTransform(
				FRotator(0, Variant * 90.0f, 0),
				FVector(Variant % 2 == 0 ? -720.0f : 720.0f, Variant < 2 ? 540.0f : -540.0f, 0),
				FVector(1.45f, 1.0f, 1.0f)));
		}
		else if (IndustrialVariant <= 5)
		{
			FactoryPallets->AddInstance(FTransform(
				FRotator(0, Stream.FRandRange(-18.0f, 18.0f), 0),
				FVector(Stream.FRandRange(-620.0f, 620.0f), Stream.FRandRange(-620.0f, 620.0f), 12.0f),
				FVector(Stream.FRandRange(0.9f, 1.2f))));
		}
		// 잡초가 20 m 칸 가장자리에서 끊기지 않고 공업 잡동사니 주변에 모인다.
		// 작고 불규칙한 덤불 자리로 WarZone 띠 경계를 부드럽게 하되,
		// 주요 교전 통로를 앞이 안 보이는 풀밭으로 만들지는 않는다.
		if (IndustrialVariant >= 7)
		{
			const float EdgeX = Variant % 2 == 0 ? 900.0f : -900.0f;
			const float EdgeY = Variant < 2 ? 720.0f : -720.0f;
			AddBushCluster(FVector(EdgeX, EdgeY, 0.0f), 4 + (IndustrialVariant & 1), 250.0f, Stream);
			AddGrass(120, Stream);
		}
	}
	else if (bWarZoneMid && FMath::Abs(LocalSeed) % 3 != 1)
	{
		// 불규칙한 덤불 자리가 전투 구역 둘레에 부드러운 시각적 전환을 만든다.
		// 둥근 띠 경계를 가리려고 일부러 칸 가장자리를 넘는다.
		const float EdgeX = Variant % 2 == 0 ? 930.0f : -930.0f;
		const float EdgeY = Variant < 2 ? 760.0f : -760.0f;
		AddBushCluster(FVector(EdgeX, EdgeY, 0.0f), 7, 330.0f, Stream);
		AddGrass(110, Stream);
	}

	DynamicPropA->SetRelativeLocation(TileKind == ETacticalTileKind::WarZoneWarehouse ? FVector(250, 350, 30) : FVector(-520, 580, 30));
	DynamicPropA->SetRelativeRotation(FRotator(0, 90, 0));
	DynamicPropB->SetRelativeLocation(FVector(540, -560, 30));
	DynamicPropB->SetRelativeRotation(FRotator(0, -70, 0));
	DynamicPropC->SetRelativeLocation(FVector(180, 480, 30));
	ApplyDynamicProps(Stream);

}
