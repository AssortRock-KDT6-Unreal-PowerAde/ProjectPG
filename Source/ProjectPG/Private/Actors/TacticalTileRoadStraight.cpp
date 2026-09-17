// Playable 20x20m straight-road tile prototype. Does not modify the team's generator.

#include "Actors/TacticalTileRoadStraight.h"

#include "Components/ChildActorComponent.h"
#include "Components/ArrowComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	FTransform BoxTransform(const FVector& Center, const FVector& Size, float Yaw = 0.0f)
	{
		return FTransform(FRotator(0.0f, Yaw, 0.0f), Center, Size / 100.0f);
	}
}

ATacticalTileRoadStraight::ATacticalTileRoadStraight()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
		TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BarrierMesh(
		TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Foundation_block_1_13.SM_Foundation_block_1_13'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SolidWallMesh(
		TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Solid_2m.SM_Wall_Solid_2m'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DoorWallMesh(
		TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Door_1x3.SM_Wall_Door_1x3'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FloorMesh(
		TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Floor_2x2.SM_Floor_2x2'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> BarrelMesh(
		TEXT("/Script/Engine.StaticMesh'/Game/TSL_CQBModularCore/Static_meshes/SM_Barrel_LP.SM_Barrel_LP'"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> GrassMesh(
		TEXT("/Script/Engine.StaticMesh'/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarC.SM_vlkhcbxia_VarC'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GroundMaterial(
		TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RoadStraight_Ground.MI_RoadStraight_Ground'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> RoadMaterial(
		TEXT("/Script/Engine.MaterialInstanceConstant'/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean.MI_RuntimeRoad_AsphaltClean'"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> WallMaterial(
		TEXT("/Script/Engine.MaterialInstanceConstant'/Game/TSL_CQBModularCore/Material/Material_instances/MI_Main_4.MI_Main_4'"));

	Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground_20m"));
	Ground->SetupAttachment(SceneRoot);
	Ground->SetStaticMesh(CubeMesh.Object);
	Ground->SetRelativeTransform(BoxTransform(FVector(0, 0, -15), FVector(2000, 2000, 30)));
	Ground->SetCollisionProfileName(TEXT("BlockAll"));
	Ground->SetCanEverAffectNavigation(false);
	if (GroundMaterial.Succeeded()) Ground->SetMaterial(0, GroundMaterial.Object);

	Road = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Road_6_5m"));
	Road->SetupAttachment(SceneRoot);
	Road->SetStaticMesh(CubeMesh.Object);
	Road->SetRelativeTransform(BoxTransform(FVector(0, 0, 3), FVector(2000, 650, 6)));
	Road->SetCollisionProfileName(TEXT("BlockAll"));
	Road->SetCanEverAffectNavigation(false);
	if (RoadMaterial.Succeeded()) Road->SetMaterial(0, RoadMaterial.Object);

	LaneMarkLeft = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaneEdge_North"));
	LaneMarkLeft->SetupAttachment(SceneRoot);
	LaneMarkLeft->SetStaticMesh(CubeMesh.Object);
	LaneMarkLeft->SetRelativeTransform(BoxTransform(FVector(0, 285, 8), FVector(2000, 8, 2)));
	LaneMarkLeft->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LaneMarkLeft->SetCanEverAffectNavigation(false);
	if (WallMaterial.Succeeded()) LaneMarkLeft->SetMaterial(0, WallMaterial.Object);

	LaneMarkRight = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LaneEdge_South"));
	LaneMarkRight->SetupAttachment(SceneRoot);
	LaneMarkRight->SetStaticMesh(CubeMesh.Object);
	LaneMarkRight->SetRelativeTransform(BoxTransform(FVector(0, -285, 8), FVector(2000, 8, 2)));
	LaneMarkRight->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LaneMarkRight->SetCanEverAffectNavigation(false);
	if (WallMaterial.Succeeded()) LaneMarkRight->SetMaterial(0, WallMaterial.Object);

	BoundaryWalls = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BoundaryWalls"));
	BoundaryWalls->SetupAttachment(SceneRoot);
	BoundaryWalls->SetStaticMesh(SolidWallMesh.Object);
	BoundaryWalls->SetCollisionProfileName(TEXT("BlockAll"));
	BoundaryWalls->SetCanEverAffectNavigation(false);

	DoorWalls = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DoorWalls"));
	DoorWalls->SetupAttachment(SceneRoot);
	DoorWalls->SetStaticMesh(DoorWallMesh.Object);
	DoorWalls->SetCollisionProfileName(TEXT("BlockAll"));
	DoorWalls->SetCanEverAffectNavigation(false);

	ShelterRoofs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ShelterRoofs"));
	ShelterRoofs->SetupAttachment(SceneRoot);
	ShelterRoofs->SetStaticMesh(FloorMesh.Object);
	ShelterRoofs->SetCollisionProfileName(TEXT("BlockAll"));
	ShelterRoofs->SetCanEverAffectNavigation(false);

	ConcreteCover = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ConcreteCover"));
	ConcreteCover->SetupAttachment(SceneRoot);
	ConcreteCover->SetStaticMesh(BarrierMesh.Object);
	ConcreteCover->SetCollisionProfileName(TEXT("BlockAll"));
	ConcreteCover->SetCanEverAffectNavigation(false);

	BarrelCover = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BarrelCover"));
	BarrelCover->SetupAttachment(SceneRoot);
	BarrelCover->SetStaticMesh(BarrelMesh.Object);
	BarrelCover->SetCollisionProfileName(TEXT("BlockAll"));
	BarrelCover->SetCanEverAffectNavigation(false);

	GrassDressing = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassDressing"));
	GrassDressing->SetupAttachment(SceneRoot);
	GrassDressing->SetStaticMesh(GrassMesh.Object);
	GrassDressing->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GrassDressing->SetCanEverAffectNavigation(false);

	DynamicPropNorth = CreateDefaultSubobject<UChildActorComponent>(TEXT("DynamicProp_North"));
	DynamicPropNorth->SetupAttachment(SceneRoot);
	DynamicPropNorth->SetRelativeLocation(FVector(-520, 610, 10));
	DynamicPropNorth->SetRelativeRotation(FRotator(0, 93, 0));

	DynamicPropSouth = CreateDefaultSubobject<UChildActorComponent>(TEXT("DynamicProp_South"));
	DynamicPropSouth->SetupAttachment(SceneRoot);
	DynamicPropSouth->SetRelativeLocation(FVector(560, -630, 10));
	DynamicPropSouth->SetRelativeRotation(FRotator(0, -78, 0));

	DynamicPropMid = CreateDefaultSubobject<UChildActorComponent>(TEXT("DynamicProp_Mid"));
	DynamicPropMid->SetupAttachment(SceneRoot);
	DynamicPropMid->SetRelativeLocation(FVector(140, 465, 10));
	DynamicPropMid->SetRelativeRotation(FRotator(0, 12, 0));

	ConnectionWest = CreateDefaultSubobject<UArrowComponent>(TEXT("Connection_West"));
	ConnectionWest->SetupAttachment(SceneRoot);
	ConnectionWest->SetRelativeLocation(FVector(-1000, 0, 30));
	ConnectionWest->SetRelativeRotation(FRotator(0, 180, 0));
	ConnectionWest->SetArrowColor(FColor::Green);
	ConnectionWest->SetHiddenInGame(true);

	ConnectionEast = CreateDefaultSubobject<UArrowComponent>(TEXT("Connection_East"));
	ConnectionEast->SetupAttachment(SceneRoot);
	ConnectionEast->SetRelativeLocation(FVector(1000, 0, 30));
	ConnectionEast->SetArrowColor(FColor::Green);
	ConnectionEast->SetHiddenInGame(true);

	LootSpawnSlot = CreateDefaultSubobject<UArrowComponent>(TEXT("LootSpawnSlot"));
	LootSpawnSlot->SetupAttachment(SceneRoot);
	LootSpawnSlot->SetRelativeLocation(FVector(-520, 720, 65));
	LootSpawnSlot->SetArrowColor(FColor::Yellow);
	LootSpawnSlot->SetHiddenInGame(true);

	AISpawnSlot = CreateDefaultSubobject<UArrowComponent>(TEXT("AISpawnSlot"));
	AISpawnSlot->SetupAttachment(SceneRoot);
	AISpawnSlot->SetRelativeLocation(FVector(690, -690, 65));
	AISpawnSlot->SetRelativeRotation(FRotator(0, 180, 0));
	AISpawnSlot->SetArrowColor(FColor::Red);
	AISpawnSlot->SetHiddenInGame(true);

	AbandonedCarClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Blueprints/Props/BP_Prop_AbandonedCar.BP_Prop_AbandonedCar_C")));
	BarrelClusterClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Blueprints/Props/BP_Prop_BarrelCluster.BP_Prop_BarrelCluster_C")));
	ConcreteBarrierClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Blueprints/Props/BP_Prop_ConcreteBarrier.BP_Prop_ConcreteBarrier_C")));

	Tags.Add(TEXT("TacticalTile"));
	Tags.Add(TEXT("RuntimeTacticalTile"));
	Tags.Add(TEXT("RoadStraight"));
	Tags.Add(TEXT("Footprint_1x1_20m"));
}

uint8 ATacticalTileRoadStraight::GetEffectiveLayoutVariant() const
{
	return LayoutVariantOverride <= 3
		? LayoutVariantOverride
		: static_cast<uint8>(FMath::Abs(LocalSeed) % 4);
}

void ATacticalTileRoadStraight::RebuildFromRuntimeSpec(int32 InSeed, uint8 InLayoutVariant)
{
	LocalSeed = InSeed;
	LayoutVariantOverride = InLayoutVariant;
	RebuildFixedInstances();
	ApplyDynamicPropClasses();
}

void ATacticalTileRoadStraight::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RebuildFixedInstances();
	ApplyDynamicPropClasses();
}

void ATacticalTileRoadStraight::RebuildFixedInstances()
{
	const uint8 Variant = GetEffectiveLayoutVariant();
	BoundaryWalls->ClearInstances();
	DoorWalls->ClearInstances();
	ShelterRoofs->ClearInstances();
	ConcreteCover->ClearInstances();
	BarrelCover->ClearInstances();
	GrassDressing->ClearInstances();

	// Broken perimeter: endpoints at X +/-1000 remain open for tile-to-tile travel.
	const float WallX[] = {-900, -620, -340, -60, 300, 660};
	for (int32 Index = 0; Index < 6; ++Index)
	{
		if (Index != static_cast<int32>((Variant + 2) % 5))
			BoundaryWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(WallX[Index], 950, 0), FVector(Index == 4 ? 1.0f : 1.15f, 1.0f, Index == 4 ? 0.65f : 1.0f)));
		if (Index != static_cast<int32>((Variant + 3) % 5))
			BoundaryWalls->AddInstance(FTransform(FRotator(0, -90, 0), FVector(WallX[Index] + 210, -950, 0), FVector(Index == 1 ? 1.0f : 1.15f, 1.0f, Index == 1 ? 0.65f : 1.0f)));
	}

	// Four authored combat silhouettes share the exact same 20m footprint and
	// west/east road sockets. They vary shoulder use without closing the lane.
	if (Variant == 0 || Variant == 1)
	{
		const float Side = Variant == 0 ? 1.0f : -1.0f;
		const float CenterX = Variant == 0 ? -520.0f : 430.0f;
		for (const float X : {CenterX - 300.0f, CenterX + 300.0f})
		{
			BoundaryWalls->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Side * 820.0f, 0), FVector::OneVector));
			BoundaryWalls->AddInstance(FTransform(FRotator::ZeroRotator, FVector(X, Side * 610.0f, 0), FVector::OneVector));
		}
		DoorWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(CenterX - 280.0f, Side * 420.0f, 0), FVector::OneVector));
		DoorWalls->AddInstance(FTransform(FRotator(0, 90, 0), FVector(CenterX + 120.0f, Side * 420.0f, 0), FVector::OneVector));
		for (int32 XIndex = 0; XIndex < 3; ++XIndex)
			for (int32 YIndex = 0; YIndex < 2; ++YIndex)
				ShelterRoofs->AddInstance(FTransform(FRotator::ZeroRotator,
					FVector(CenterX + 300.0f - XIndex * 200.0f, Side * (820.0f - YIndex * 200.0f), 300), FVector::OneVector));
	}

	switch (Variant)
	{
	case 0: // roadside service shelter and staggered counter-cover
		ConcreteCover->AddInstance(FTransform(FRotator(0, 78, 0), FVector(-40, -455, 8), FVector(1.6f)));
		ConcreteCover->AddInstance(FTransform(FRotator(0, -68, 0), FVector(610, 475, 8), FVector(1.35f)));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-690, -555, 10), FVector::OneVector));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-625, -585, 10), FVector::OneVector));
		break;
	case 1: // mirrored lay-by with an open flanking shoulder
		ConcreteCover->AddInstance(FTransform(FRotator(0, 70, 0), FVector(-600, 470, 8), FVector(1.55f)));
		ConcreteCover->AddInstance(FTransform(FRotator(0, -82, 0), FVector(70, 430, 8), FVector(1.3f)));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(710, 565, 10), FVector::OneVector));
		break;
	case 2: // vehicle ambush lane: alternating cover, no enclosed room
		ConcreteCover->AddInstance(FTransform(FRotator(0, 82, 0), FVector(-610, 440, 8), FVector(1.55f)));
		ConcreteCover->AddInstance(FTransform(FRotator(0, -75, 0), FVector(10, -440, 8), FVector(1.8f)));
		ConcreteCover->AddInstance(FTransform(FRotator(0, 15, 0), FVector(690, 520, 8), FVector(1.25f)));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-760, -570, 10), FVector::OneVector));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(420, 600, 10), FVector::OneVector));
		break;
	default: // long sightline: sparse hard cover keeps the road readable
		ConcreteCover->AddInstance(FTransform(FRotator(0, 88, 0), FVector(-680, -455, 8), FVector(1.35f)));
		ConcreteCover->AddInstance(FTransform(FRotator(0, -88, 0), FVector(680, 455, 8), FVector(1.35f)));
		BarrelCover->AddInstance(FTransform(FRotator::ZeroRotator, FVector(0, -565, 10), FVector::OneVector));
		break;
	}

	FRandomStream Stream(LocalSeed ^ 0x51A8);
	for (int32 Index = 0; Index < 44; ++Index)
	{
		const float X = Stream.FRandRange(-900, 900);
		const float Y = Stream.FRandRange(390, 870) * (Stream.RandRange(0, 1) == 1 ? 1.0f : -1.0f);
		const float Scale = Stream.FRandRange(0.65f, 1.15f);
		GrassDressing->AddInstance(FTransform(FRotator(0, Stream.FRandRange(0, 360), 0), FVector(X, Y, 6), FVector(Scale)));
	}
}

void ATacticalTileRoadStraight::ApplyDynamicPropClasses()
{
	UClass* Car = AbandonedCarClass.LoadSynchronous();
	UClass* Barrels = BarrelClusterClass.LoadSynchronous();
	UClass* Barrier = ConcreteBarrierClass.LoadSynchronous();
	if (!bShowDynamicProps)
	{
		DynamicPropNorth->SetChildActorClass(nullptr);
		DynamicPropSouth->SetChildActorClass(nullptr);
		DynamicPropMid->SetChildActorClass(nullptr);
		return;
	}

	const uint8 Variant = GetEffectiveLayoutVariant();
	FRandomStream Stream(LocalSeed);
	DynamicPropNorth->SetChildActorClass((Variant == 1 || Variant == 2) ? Car : nullptr);
	DynamicPropSouth->SetChildActorClass(Variant == 0 ? Barrels : (Variant == 3 ? Barrier : nullptr));
	DynamicPropMid->SetChildActorClass(Variant == 2 ? Barrels : (Stream.RandRange(0, 3) == 0 ? Barrier : nullptr));
}
