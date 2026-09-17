// Runtime-spawnable 20x20m tactical tile family used by the procedural map visual layer.

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
	// Keep third-party materials untouched. This local instance uses Downtown's
	// actual asphalt maps with puddles/blending disabled and a high roughness so
	// procedural road slabs read as dry asphalt rather than leaf litter.
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
	// Packed LD tiles use SM_Floor_2x2 with a 20 cm top surface. Match that datum
	// exactly so C++ nature tiles do not become shallow reflective depressions.
	// The small XY overlap hides raster seams without changing the 20 m sockets.
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
		// Recast is fed by the dedicated NavigationFloor. Excluding thousands of
		// runtime ISM instances prevents expensive/unsafe nav export during rebuild.
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
	// Runtime tile rebuilding can occur after component registration.  Navigation
	// export from mutable ISM instance buffers is unsafe at that point; the host's
	// dedicated navigation floor remains the authoritative Recast source.
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
	// Keep the whole slab clear of Ground_20m. The old bottom face was exactly
	// coplanar with the ground and could flicker at grazing camera angles.
	constexpr float RoadCenterZ = 5.0f;
	constexpr float RoadThickness = 8.0f;
	RoadPieces->AddInstance(Box(FVector(0, 0, RoadCenterZ), FVector(Width, Width, RoadThickness)));
	// Arms overlap the center patch but terminate exactly on the +/-1000cm tile
	// edge. The old 1320cm arms extended to +/-1320cm and overlapped neighbors.
	const float ArmCenter = (1000.0f + Width * 0.5f) * 0.5f;
	const float ArmLength = 1000.0f - Width * 0.5f;
	if (Mask & North) RoadPieces->AddInstance(Box(FVector(0, ArmCenter, RoadCenterZ), FVector(Width, ArmLength, RoadThickness)));
	if (Mask & East) RoadPieces->AddInstance(Box(FVector(ArmCenter, 0, RoadCenterZ), FVector(ArmLength, Width, RoadThickness)));
	if (Mask & South) RoadPieces->AddInstance(Box(FVector(0, -ArmCenter, RoadCenterZ), FVector(Width, ArmLength, RoadThickness)));
	if (Mask & West) RoadPieces->AddInstance(Box(FVector(-ArmCenter, 0, RoadCenterZ), FVector(ArmLength, Width, RoadThickness)));

	// Bevel the four possible inside corners with small 45-degree asphalt pieces.
	// Logical sockets and the exact 6.5m edge width stay unchanged, but corner/T/
	// cross tiles no longer read as square slabs meeting at a hard 90-degree notch.
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
		// Road boundaries used to be opaque 3 m CQB wall panels.  Across hundreds
		// of cells they formed the conspicuous white maze visible from ground and
		// aerial views.  The Factory pack fence keeps the same deterministic broken
		// boundary/collision function while preserving sight lines into the terrain.
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
	// A thin dirt shoulder sits below the asphalt and above the shared terrain.
	// Each arm is authored independently so a dead end or corner never paints a
	// false road continuation into a neighbouring cell.
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

	// Scatter low dirt erosion pockets just outside the engineered shoulder. They
	// visually stitch road and terrain without changing collision or the socket
	// datum. The endpoint nearest the neighbour remains exact and obstruction-free.
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

	// Fill the non-road part of the cell instead of decorating only four corners.
	// Patch meshes are instanced and later packed into shared HISMs, so this is
	// substantially cheaper than spawning individual foliage actors.  The test
	// also reserves the dirt shoulder around every connected road arm.
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

	// Extra irregular edge clumps cross the nominal cell boundary slightly so
	// neighbouring road/nature cells do not reveal a clean 20 m square seam.
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
	// Nature cells are intended to read as waist-high abandoned terrain rather
	// than green floor plates with a few decorative tufts. These are foliage
	// *patch* meshes packed into shared HISM components, so ~100 instances per
	// 20 m cell produces dense cover without creating thousands of actors.
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

	// Sixteen irregular centres plus a 1.2 m overlap break the square silhouette
	// where adjacent 20 m cells meet. Nature tiles deliberately do not reserve a
	// ruler-straight empty trail; navigation is supplied by the underlying floor.
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
			// A jittered grid guarantees that a nature cell is actually covered.
			// Pure random clusters repeatedly left metre-wide bald patches and made
			// the 20 m square visible from player height.
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
		// Select the target world height independently from the source mesh. This
		// gives each tile a deterministic short/medium/tall mixture rather than three
		// meshes that all end at exactly the same silhouette.
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
	// Keep the industrial combat core readable. Trees are permitted in the outer
	// buffer, but not repeated on every core/mid WarZone cell.
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
	// The runtime tile actor itself is already spawned at the shared +20 cm
	// walkable datum. Rural tree roots curve upward around the pivot, so matching
	// the pivot to the surface can still look as if the tree is floating. Sink the
	// trunk collar slightly; the mesh already extends below its pivot.
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
	// Rounded rocks rarely touch the floor at their bounding-box minimum. Sink
	// that minimum 18 cm below the shared surface so the visible mass is planted.
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
	// Cylinder blockout removed. Retain the composition call sites so a proper
	// log mesh can replace this implementation without redesigning every tile.
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
		// Broken low banks. Four crossovers remain walkable, while the alternating
		// heights create genuine crouch/stand elevation changes inside the 20m tile.
		//
		// These were engine cube instances with a dirt material, which on open grass
		// read as loose rectangular slabs dropped on the field rather than as ground
		// forming a ditch - eight of them per tile across every NatureDitch cell.
		// Rock outcrops carry the same footprint, height and cover value while
		// actually looking like terrain.
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
	// These are four different combat rhythms, not four cosmetic scatters:
	// ridge = lateral cover, ditch = staggered approach, wreck = hard landmark,
	// scrub = mostly open long sightline with only emergency cover.
	switch (Variant % 4)
	{
	case 0: // low rubble ridge, two flanks remain open
		AddLowWall(FVector(-320, 80, 0), 18, 1.5f);
		AddLowWall(FVector(80, -40, 0), 18, 1.25f);
		QuarterWalls->AddInstance(FTransform(FRotator(0, 70, 0), FVector(530, -410, 0), FVector(1.0f, 1.0f, 1.2f)));
		AddGrass(120, Stream);
		break;
	case 1: // staggered ditch / approach cover
		for (int32 Index = 0; Index < 3; ++Index)
		{
			const float Yaw = Index % 2 == 0 ? -32.0f : 34.0f;
			AddLowWall(FVector(-520 + Index * 480, Index % 2 == 0 ? -310 : 300, 0), Yaw, 1.15f);
		}
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(650, 500, 0), FVector::OneVector));
		AddGrass(140, Stream);
		break;
	case 2: // recognizable wreck/loot landmark with offset counter-cover
		ConcreteCover->AddInstance(FTransform(FRotator(0, 28, 0), FVector(480, -330, 8), FVector(1.45f)));
		AddLowWall(FVector(-500, 370, 0), -18, 1.35f);
		UtilityProps->AddInstance(FTransform(FRotator(0, 110, 0), FVector(-120, -520, 0), FVector(0.8f)));
		AddGrass(110, Stream);
		break;
	default: // long sightline tile; keep center deliberately readable
		QuarterWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-760, 520, 0), FVector(1.0f, 1.0f, 1.2f)));
		QuarterWalls->AddInstance(FTransform(FRotator(0, -90, 0), FVector(720, -540, 0), FVector(1.0f, 1.0f, 1.2f)));
		// A low lateral berm breaks 200m+ exposed runs while preserving the long
		// sightline above crouch height.
		AddLowWall(FVector(0, Variant % 2 == 0 ? 620 : -620, 0), 90, 1.15f);
		AddGrass(160, Stream);
		break;
	}
	if ((Variant % 4) != 3)
	{
		// Every otherwise-open 20m traversal cell gets one knee-height bailout
		// position. Alternating the side preserves broad sightlines without
		// producing repeated 140m+ completely exposed runs.
		AddLowWall(FVector(0, Variant % 2 == 0 ? 720 : -720, 0), 90, 0.85f);
	}
}

void ATacticalTileActor::AddRuinsLayout(uint8 Variant, FRandomStream& Stream)
{
	// Keep every ruin cluster inside the tile instead of sealing all four borders.
	// This avoids the dense checkerboard wall pattern seen in the first full-map pass.
	switch (Variant % 4)
	{
	case 0: // L-shaped collapsed room, two clear approaches
		for (int32 Index = 0; Index < 4; ++Index)
			SolidWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-550 + Index * 200, 520, 0), FVector::OneVector));
		HalfWalls->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-550, 320, 0), FVector(1, 1.6f, 1)));
		QuarterWalls->AddInstance(FTransform(FRotator(0, 35, 0), FVector(220, 130, 0), FVector(1.2f)));
		break;
	case 1: // parallel broken lanes with a cross-angle firing gap
		for (int32 Index = 0; Index < 3; ++Index)
		{
			HalfWalls->AddInstance(FTransform(FRotator(0, 18, 0), FVector(-500 + Index * 260, -280, 0), FVector::OneVector));
			QuarterWalls->AddInstance(FTransform(FRotator(0, -20, 0), FVector(100 + Index * 230, 350, 0), FVector::OneVector));
		}
		ShootingWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-610, 350, 0), FVector::OneVector));
		break;
	case 2: // compact courtyard; 2.4m doors remain on opposite diagonals
		for (int32 Index = -2; Index <= 2; ++Index)
		{
			if (Index != 0) SolidWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(Index * 200, 610, 0), FVector::OneVector));
			if (Index != 1) HalfWalls->AddInstance(FTransform(FRotator(0, -90, 0), FVector(Index * 200, -610, 0), FVector::OneVector));
		}
		AddLowWall(FVector(-590, 0, 0), 0, 1.8f);
		break;
	default: // diagonal rubble spine, useful as cover without becoming a maze wall
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
		return; // an intentionally clean/open road segment is also necessary

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
		// A connector can run for hundreds of metres. Alternate one true
		// crouch-height bailout position between shoulders and mix in vegetation;
		// this breaks lethal 200m+ exposure without recreating a wall tunnel.
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
	// Existing Nature Blueprints serialize component defaults, so runtime terrain
	// explicitly restores the three proven-visible Rural grass variants.
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
	// Keep the first dense pass static. A packed-HISM-safe interaction material
	// can be added separately after visibility and performance are validated.
	GrassDressing->SetEvaluateWorldPositionOffset(false);
	GrassDressingB->SetEvaluateWorldPositionOffset(false);
	GrassDressingC->SetEvaluateWorldPositionOffset(false);
	BushDressing->SetEvaluateWorldPositionOffset(false);
	BushDressingB->SetEvaluateWorldPositionOffset(false);
	// Older Blueprint children serialized the tiny Rural Rock_1 component mesh.
	// Reassert real-world Downtown rock dimensions before AddRock computes scale;
	// otherwise a requested 3 m boulder becomes a visibly oversized 8x instance.
	if (IsValid(DowntownRockLarge)) RockCover->SetStaticMesh(DowntownRockLarge);
	if (IsValid(DowntownRockMedium)) RockCoverB->SetStaticMesh(DowntownRockMedium);
	// The Rural pine branch material animates its full HISM bounds vertically in
	// this packed runtime layout. Disable WPO on trees so trunks remain planted.
	TreeTrunks->SetEvaluateWorldPositionOffset(false);
	TreeCanopies->SetEvaluateWorldPositionOffset(false);

	static UMaterialInterface* UnifiedGround = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
	// Preserve the Rural asset materials. The old runtime tint made grass appear
	// chalk-white under the level's bright directional light.
	if (IsValid(GrassPatchA) && IsValid(GrassPatchA->GetMaterial(0)))
		GrassDressing->SetMaterial(0, GrassPatchA->GetMaterial(0));
	if (IsValid(GrassPatchB) && IsValid(GrassPatchB->GetMaterial(0)))
		GrassDressingB->SetMaterial(0, GrassPatchB->GetMaterial(0));
	if (IsValid(GrassPatchLong) && IsValid(GrassPatchLong->GetMaterial(0)))
		GrassDressingC->SetMaterial(0, GrassPatchLong->GetMaterial(0));

	// Every tile starts from one dry, matte underlay. Roads and authored facility
	// floors are layered above it, so their shoulders no longer expose a random
	// per-Blueprint material or the reflective Diorama_Ground water blend.
	if (IsValid(UnifiedGround)) Ground->SetMaterial(0, UnifiedGround);
	// Native nature/facility tiles and packed LD tiles must share the same +20 cm
	// walkable surface. Blueprint-derived native components may retain old CDO
	// offsets, so enforce the datum every rebuild rather than relying on defaults.
	FVector GroundLocation = Ground->GetRelativeLocation();
	GroundLocation.Z = 5.0f;
	Ground->SetRelativeLocation(GroundLocation);
	// Keep the underlay UV direction identical even when a road/facility tile is
	// rotated to satisfy its connection mask. The square mesh is orientation
	// independent, while a rotated texture makes the 20 m boundary conspicuous.
	Ground->SetWorldRotation(FRotator::ZeroRotator);
	const bool bWarZoneCore = Tags.Contains(TEXT("WarZone_Core"));
	const bool bWarZoneMid = Tags.Contains(TEXT("WarZone_Mid"));
	// WarZone uses the same terrain underlay as the surrounding map. Its identity
	// comes from structures, industrial props, cover density and hazard markings;
	// a different 1x1 ground material inevitably exposes the procedural grid.

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
		// One clean forward arrow: rectangular shaft and two non-overlapping heads.
		// The previous texture-style marking produced intersecting red planes.
		MarkingPieces->AddInstance(Box(FVector(180, 0, 9), FVector(430, 105, 2)));
		MarkingPieces->AddInstance(Box(FVector(465, 105, 9), FVector(300, 95, 2), 35));
		MarkingPieces->AddInstance(Box(FVector(465, -105, 9), FVector(300, 95, 2), -35));
		// Two red threshold bars identify the trigger half of the tile from a
		// distance without requiring replicated text or a billboard actor.
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
		// A single dominant Fab prop per cell replaces the previous repeated pair
		// of CQB shelters. Variants read as container lane, tank service bay,
		// pallet yard or broken fence court while preserving a central combat lane.
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
		// Short broken hazard bars identify the combat district at player height
		// without drawing a square outline around the 20 m cell.
		MarkingPieces->AddInstance(Box(FVector(-760, -760, 9), FVector(260, 32, 2), 18));
		MarkingPieces->AddInstance(Box(FVector(720, 690, 9), FVector(220, 32, 2), -24));
		AddGrass(150 + Variant * 18, Stream);
		AddBushCluster(FVector(Variant % 2 == 0 ? 860.0f : -860.0f, Variant < 2 ? -720.0f : 720.0f, 0.0f), 5, 250.0f, Stream);
		break;
	case ETacticalTileKind::WarZoneWarehouse:
		AddBrokenBoundary(Mask, 2);
		// A 1x1 warehouse annex occupies two corners and leaves a readable central
		// movement cross. The full enclosed warehouse remains the authored 2x2 POI.
		AddShelter(FVector(Variant % 2 == 0 ? -610 : 610, 570, 0), Variant % 2 == 0 ? 0 : 180, true);
		// Factory container/tank forms the service annex on the opposite corner;
		// this avoids repeating two identical shelters on every warehouse cell.
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

	// Carry the industrial language across otherwise-natural/open core cells so
	// WarZone reads as one district without painting every grid square black.
	// Sparse deterministic placement avoids the previous repeated domino pattern.
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
		// Weeds gather around industrial clutter instead of stopping at the
		// 20 m cell edge.  The small irregular pockets soften the WarZone band
		// without turning the main firefight lanes into an opaque meadow.
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
		// Irregular shrub pockets form a soft visual transition around the combat
		// district. They deliberately cross the cell edge to hide the radial band.
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
