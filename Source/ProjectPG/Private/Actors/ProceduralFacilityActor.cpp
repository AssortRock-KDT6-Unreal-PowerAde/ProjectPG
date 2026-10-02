// Code-authored multi-cell facility used by the procedural map visual layer.

#include "Actors/ProceduralFacilityActor.h"

#include "Components/ArrowComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/StaticMesh.h"

namespace
{
	UStaticMesh* LoadFacilityMesh(const TCHAR* Path, UStaticMesh* Fallback)
	{
		if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, Path))
			return Mesh;
		return Fallback;
	}
}

AProceduralFacilityActor::AProceduralFacilityActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	// These facilities are authored in code after being spawned at runtime.  A
	// registered Static component rejects SetStaticMesh(), leaving an apparently
	// valid instance list with no mesh to render.  Keep the small facility set
	// movable so its meshes can be assigned safely during Configure/Rebuild.
	SceneRoot->SetMobility(EComponentMobility::Movable);

	auto MakeISM = [this](const TCHAR* Name)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = CreateDefaultSubobject<UHierarchicalInstancedStaticMeshComponent>(Name);
		Component->SetupAttachment(SceneRoot);
		Component->SetMobility(EComponentMobility::Movable);
		return Component;
	};
	Floors = MakeISM(TEXT("Floors"));
	SolidWalls = MakeISM(TEXT("SolidWalls"));
	WindowWalls = MakeISM(TEXT("WindowWalls"));
	Roofs = MakeISM(TEXT("Roofs"));
	Stairs = MakeISM(TEXT("Stairs"));
	Covers = MakeISM(TEXT("Covers"));
	Containers = MakeISM(TEXT("Containers"));
	Railings = MakeISM(TEXT("Railings"));
	FactoryHalls = MakeISM(TEXT("FactoryHalls"));
	FactoryChimneys = MakeISM(TEXT("FactoryChimneys"));
	FactoryTanks = MakeISM(TEXT("FactoryTanks"));
	FactoryFences = MakeISM(TEXT("FactoryFences"));
	DowntownStorefronts = MakeISM(TEXT("DowntownStorefronts"));
	DowntownUpperWalls = MakeISM(TEXT("DowntownUpperWalls"));
	DowntownStreetlights = MakeISM(TEXT("DowntownStreetlights"));
	DowntownPlanters = MakeISM(TEXT("DowntownPlanters"));
	DowntownBenches = MakeISM(TEXT("DowntownBenches"));
	RuralWalls = MakeISM(TEXT("RuralWalls"));
	RuralWindowWalls = MakeISM(TEXT("RuralWindowWalls"));
	RuralDoorWalls = MakeISM(TEXT("RuralDoorWalls"));
	RuralRoofs = MakeISM(TEXT("RuralRoofs"));
	RuralCaravans = MakeISM(TEXT("RuralCaravans"));
	RuralFences = MakeISM(TEXT("RuralFences"));
	RuralPicnicTables = MakeISM(TEXT("RuralPicnicTables"));
	FactoryCranes = MakeISM(TEXT("FactoryCranes"));
	FactorySiteHouses = MakeISM(TEXT("FactorySiteHouses"));
	FactoryPipes = MakeISM(TEXT("FactoryPipes"));
	FactoryPallets = MakeISM(TEXT("FactoryPallets"));
	FactoryBarrels = MakeISM(TEXT("FactoryBarrels"));
	FactoryWorkTables = MakeISM(TEXT("FactoryWorkTables"));
	FactoryDoors = MakeISM(TEXT("FactoryDoors"));
	FactoryPowerBoxes = MakeISM(TEXT("FactoryPowerBoxes"));
	FactoryLamps = MakeISM(TEXT("FactoryLamps"));

	auto MakeSocket = [this](const TCHAR* Name, FColor Color)
	{
		UArrowComponent* Socket = CreateDefaultSubobject<UArrowComponent>(Name);
		Socket->SetupAttachment(SceneRoot);
		Socket->ArrowColor = Color;
		Socket->ArrowSize = 1.5f;
		Socket->SetHiddenInGame(true);
		Socket->SetIsVisualizationComponent(true);
		return Socket;
	};
	EntranceNorth = MakeSocket(TEXT("EntranceNorth"), FColor::Green);
	EntranceSouth = MakeSocket(TEXT("EntranceSouth"), FColor::Green);
	LootSocket = MakeSocket(TEXT("LootSocket"), FColor::Yellow);
	UpperLootSocket = MakeSocket(TEXT("UpperLootSocket"), FColor::Orange);
	AISocketA = MakeSocket(TEXT("AISocketA"), FColor::Red);
	AISocketB = MakeSocket(TEXT("AISocketB"), FColor::Red);

	Tags.AddUnique(TEXT("CodeBuiltFacility"));
	Tags.AddUnique(TEXT("ServerManifestCompatible"));
}

void AProceduralFacilityActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Rebuild();
}

void AProceduralFacilityActor::Configure(EProceduralFacilityKind InKind, int64 InSeed, uint8 InVariant)
{
	FacilityKind = InKind;
	LocalSeed = InSeed;
	LayoutVariant = InVariant % 4;
	Rebuild();
}

FIntPoint AProceduralFacilityActor::GetFootprintCells() const
{
	switch (FacilityKind)
	{
	case EProceduralFacilityKind::IndustrialRaid3x3: return FIntPoint(3, 3);
	case EProceduralFacilityKind::SatelliteCamp2x2: return FIntPoint(2, 2);
	case EProceduralFacilityKind::LongBarracks2x1: return FIntPoint(2, 1);
	case EProceduralFacilityKind::LinearTrench4x1: return FIntPoint(4, 1);
	case EProceduralFacilityKind::DowntownBlock3x3: return FIntPoint(3, 3);
	case EProceduralFacilityKind::FactoryConstruction2x2: return FIntPoint(2, 2);
	case EProceduralFacilityKind::RuralHideout2x2: return FIntPoint(2, 2);
	default: return FIntPoint(1, 1);
	}
}

void AProceduralFacilityActor::ConfigureMeshComponent(
	UHierarchicalInstancedStaticMeshComponent* Component,
	bool bCollision,
	bool bNavigation)
{
	Component->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	Component->SetCollisionResponseToAllChannels(bCollision ? ECR_Block : ECR_Ignore);
	Component->SetCanEverAffectNavigation(bNavigation);
	Component->SetGenerateOverlapEvents(false);
	Component->ComponentTags.AddUnique(TEXT("FacilityGeometry"));
}

void AProceduralFacilityActor::Rebuild()
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Floors->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/TSL_CQBModularCore/Static_meshes/SM_Floor_2x2.SM_Floor_2x2"), Cube));
	SolidWalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Solid_2m.SM_Wall_Solid_2m"), Cube));
	WindowWalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/TSL_CQBModularCore/Static_meshes/SM_Wall_Window.SM_Wall_Window"), SolidWalls->GetStaticMesh()));
	Roofs->SetStaticMesh(Floors->GetStaticMesh());
	Stairs->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_Stair_2_Closed.SM_Stair_2_Closed"), Cube));
	Covers->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_shelf_1.SM_shelf_1"), Cube));
	Containers->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM__Container.SM__Container"), Cube));
	Railings->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_fence_2_a.SM_fence_2_a"), SolidWalls->GetStaticMesh()));
	FactoryHalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_full_hall.SM_full_hall"), Cube));
	FactoryChimneys->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_chimney.SM_chimney"), Cube));
	FactoryTanks->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_Tank.SM_Tank"), Cube));
	FactoryFences->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_fence_2_a.SM_fence_2_a"), Cube));
	DowntownStorefronts->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Downtown_West/Assets/building_b/SM_build_b_mod_lvl1_storefront_a_5m.SM_build_b_mod_lvl1_storefront_a_5m"), SolidWalls->GetStaticMesh()));
	DowntownUpperWalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Downtown_West/Assets/building_b/SM_build_b_mod_lvl2_doublewindow.SM_build_b_mod_lvl2_doublewindow"), WindowWalls->GetStaticMesh()));
	DowntownStreetlights->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Downtown_West/Assets/props/props_streetlight/SM_light_streetlight_complete.SM_light_streetlight_complete"), Cube));
	DowntownPlanters->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Downtown_West/Assets/ground/SM_ground_mod_garden_planter_tall_a.SM_ground_mod_garden_planter_tall_a"), Cube));
	DowntownBenches->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Downtown_West/Assets/props/prop_bench_wood/SM_bench_wood_a.SM_bench_wood_a"), Cube));
	RuralWalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Modular/Wall_4m.Wall_4m"), SolidWalls->GetStaticMesh()));
	RuralWindowWalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Modular/Wall_Window_4m.Wall_Window_4m"), WindowWalls->GetStaticMesh()));
	RuralDoorWalls->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Modular/Wall_Door_4m.Wall_Door_4m"), SolidWalls->GetStaticMesh()));
	RuralRoofs->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Modular/Roof_4m.Roof_4m"), Roofs->GetStaticMesh()));
	RuralCaravans->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Caravan.Caravan"), Cube));
	RuralFences->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Fence_Old_1_2m.Fence_Old_1_2m"), FactoryFences->GetStaticMesh()));
	RuralPicnicTables->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Picnic_Table.Picnic_Table"), Cube));
	FactoryCranes->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_big_crane_1.SM_big_crane_1"), Cube));
	FactorySiteHouses->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_small_house.SM_small_house"), Cube));
	FactoryPipes->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_Double_pipe.SM_Double_pipe"), Cube));
	FactoryPallets->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_pallet.SM_pallet"), Cube));
	FactoryBarrels->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_barrel_1.SM_barrel_1"), Cube));
	FactoryWorkTables->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_Table_1.SM_Table_1"), Cube));
	FactoryDoors->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_door_frame.SM_door_frame"), Cube));
	FactoryPowerBoxes->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_power_box_1.SM_power_box_1"), Cube));
	FactoryLamps->SetStaticMesh(LoadFacilityMesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_lamp_2.SM_lamp_2"), Cube));

	for (UHierarchicalInstancedStaticMeshComponent* Component : {
		Floors, SolidWalls, WindowWalls, Roofs, Stairs, Covers, Containers,
		Railings, FactoryHalls, FactoryChimneys, FactoryTanks, FactoryFences,
		DowntownStorefronts, DowntownUpperWalls, DowntownStreetlights, DowntownPlanters, DowntownBenches,
		RuralWalls, RuralWindowWalls, RuralDoorWalls, RuralRoofs, RuralCaravans, RuralFences,
		RuralPicnicTables, FactoryCranes, FactorySiteHouses })
		// Keep this list aligned with the interior batches below; all instances are
		// regenerated from the explicit seed and layout variant.
		Component->ClearInstances();
	for (UHierarchicalInstancedStaticMeshComponent* Component : {
		FactoryPipes, FactoryPallets, FactoryBarrels, FactoryWorkTables,
		FactoryDoors, FactoryPowerBoxes, FactoryLamps })
		Component->ClearInstances();
	// Build mutable instance buffers with navigation export disabled.  Registering
	// an empty ISM with Recast before AddInstance() can make the navigation octree
	// inspect an invalid instance entry during runtime facility construction.
	ConfigureMeshComponent(Floors, true, false);
	ConfigureMeshComponent(SolidWalls, true, false);
	ConfigureMeshComponent(WindowWalls, true, false);
	ConfigureMeshComponent(Roofs, true, false);
	ConfigureMeshComponent(Stairs, true, false);
	ConfigureMeshComponent(Covers, true, false);
	ConfigureMeshComponent(Containers, true, false);
	ConfigureMeshComponent(Railings, true, false);
	ConfigureMeshComponent(FactoryHalls, true, false);
	ConfigureMeshComponent(FactoryChimneys, true, false);
	ConfigureMeshComponent(FactoryTanks, true, false);
	ConfigureMeshComponent(FactoryFences, true, false);
	ConfigureMeshComponent(DowntownStorefronts, true, false);
	ConfigureMeshComponent(DowntownUpperWalls, true, false);
	ConfigureMeshComponent(DowntownStreetlights, true, false);
	ConfigureMeshComponent(DowntownPlanters, true, false);
	ConfigureMeshComponent(DowntownBenches, true, false);
	ConfigureMeshComponent(RuralWalls, true, false);
	ConfigureMeshComponent(RuralWindowWalls, true, false);
	ConfigureMeshComponent(RuralDoorWalls, true, false);
	ConfigureMeshComponent(RuralRoofs, true, false);
	ConfigureMeshComponent(RuralCaravans, true, false);
	ConfigureMeshComponent(RuralFences, true, false);
	ConfigureMeshComponent(RuralPicnicTables, true, false);
	ConfigureMeshComponent(FactoryCranes, true, false);
	ConfigureMeshComponent(FactorySiteHouses, true, false);
	ConfigureMeshComponent(FactoryPipes, true, false);
	ConfigureMeshComponent(FactoryPallets, true, false);
	ConfigureMeshComponent(FactoryBarrels, true, false);
	ConfigureMeshComponent(FactoryWorkTables, true, false);
	ConfigureMeshComponent(FactoryDoors, true, false);
	ConfigureMeshComponent(FactoryPowerBoxes, true, false);
	ConfigureMeshComponent(FactoryLamps, false, false);

	switch (FacilityKind)
	{
	case EProceduralFacilityKind::IndustrialRaid3x3: BuildIndustrialRaid(); break;
	case EProceduralFacilityKind::SatelliteCamp2x2: BuildSatelliteCamp(); break;
	case EProceduralFacilityKind::LongBarracks2x1: BuildLongBarracks(); break;
	case EProceduralFacilityKind::LinearTrench4x1: BuildLinearTrench(); break;
	case EProceduralFacilityKind::DowntownBlock3x3: BuildDowntownBlock(); break;
	case EProceduralFacilityKind::FactoryConstruction2x2: BuildFactoryConstruction(); break;
	case EProceduralFacilityKind::RuralHideout2x2: BuildRuralHideout(); break;
	}
	EntranceNorth->SetRelativeLocation(FVector(0, GetFootprintCells().Y * 1000.0f - 200, 120));
	EntranceNorth->SetRelativeRotation(FRotator(0, 90, 0));
	EntranceSouth->SetRelativeLocation(FVector(0, -GetFootprintCells().Y * 1000.0f + 200, 120));
	EntranceSouth->SetRelativeRotation(FRotator(0, -90, 0));
}

void AProceduralFacilityActor::AddFloorRect(float HalfX, float HalfY, float Z, float ModuleSize)
{
	// SM_Floor_2x2 spans -200..0 on local X/Y; its pivot is the positive
	// corner, not its centre. Place that corner at (+HalfX,+HalfY) so the
	// resulting slab covers exactly [-HalfX,+HalfX] x [-HalfY,+HalfY].
	Floors->AddInstance(FTransform(
		FRotator::ZeroRotator,
		FVector(HalfX, HalfY, Z),
		FVector(HalfX / 100.0f, HalfY / 100.0f, 1.0f)));
}

void AProceduralFacilityActor::AddRoofRect(const FVector& Center, float HalfX, float HalfY, float Z, float ModuleSize)
{
	Roofs->AddInstance(FTransform(
		FRotator::ZeroRotator,
		Center + FVector(HalfX, HalfY, Z),
		FVector(HalfX / 100.0f, HalfY / 100.0f, 1.0f)));
}

void AProceduralFacilityActor::AddWallRun(const FVector& Start, const FVector& End, float Z, bool bDoorGap, bool bWindows)
{
	const FVector Delta = End - Start;
	const float Length = Delta.Size2D();
	const int32 Segments = FMath::Max(1, FMath::RoundToInt(Length / 200.0f));
	const FVector Step = Delta / Segments;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) - 90.0f;
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		if (bDoorGap && FMath::Abs(Index - Segments / 2) <= 1)
			continue;
		// CQB wall meshes are authored with their pivot at the bottom, while this
		// helper's Z argument describes the desired wall centre. Compensate for the
		// 300 cm authored height so ground-floor calls at Z=150 sit on the slab.
		const FVector Location = Start + Step * (Index + 0.5f) + FVector(0, 0, Z - 150.0f);
		UHierarchicalInstancedStaticMeshComponent* Target = bWindows && Index % 3 == 1 ? WindowWalls : SolidWalls;
		// The solid panel is 200 cm wide but the window panel is 100 cm wide.
		// Stretch only along the wall run so mixed runs remain sealed at corners.
		const float AuthoredWidth = Target == WindowWalls ? 100.0f : 200.0f;
		Target->AddInstance(FTransform(
			FRotator(0, Yaw, 0),
			Location,
			FVector(1.0f, Step.Size2D() / AuthoredWidth, 1.0f)));
	}
}

void AProceduralFacilityActor::AddRailingRun(const FVector& Start, const FVector& End, float Z)
{
	const FVector Delta = End - Start;
	const float Length = Delta.Size2D();
	const int32 Segments = FMath::Max(1, FMath::CeilToInt(Length / 300.0f));
	const FVector Step = Delta / Segments;
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X));
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		Railings->AddInstance(FTransform(
			FRotator(0.0f, Yaw, 0.0f),
			Start + Step * (Index + 0.5f) + FVector(0.0f, 0.0f, Z),
			FVector(FMath::Max(0.25f, Step.Size2D() / 320.0f), 1.0f, 0.52f)));
	}
}

FVector AProceduralFacilityActor::RestOnSurface(
	const UHierarchicalInstancedStaticMeshComponent* Component,
	const FVector& SurfaceLocation,
	const FVector& Scale) const
{
	// SurfaceLocation.Z is the floor the prop stands on. Pack pivots are inconsistent
	// - the shelf's sits 50 cm below its base, the container's at its base - so offset
	// by the mesh's own minimum rather than trusting the pivot. Call sites used to
	// pass eyeballed heights instead, which floated every container 110 cm and every
	// cover 54 cm off the floor.
	FVector Result = SurfaceLocation;
	if (const UStaticMesh* Mesh = Component->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		Result.Z -= static_cast<float>(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale.Z;
	}
	return Result;
}

void AProceduralFacilityActor::AddCover(const FVector& Location, const FVector& Scale, float Yaw)
{
	Covers->AddInstance(FTransform(
		FRotator(0, Yaw, 0), RestOnSurface(Covers, Location, Scale), Scale));
}

void AProceduralFacilityActor::AddContainer(const FVector& Location, float Yaw, const FVector& Scale)
{
	Containers->AddInstance(FTransform(
		FRotator(0, Yaw, 0), RestOnSurface(Containers, Location, Scale), Scale));
}

float AProceduralFacilityActor::GetStairTopOffsetCm() const
{
	// Distance from a stair's pivot to the top of the flight at the fixed 1.35 plan
	// scale. Callers use it to line a flight up with the walkway it has to reach.
	if (const UStaticMesh* StairMesh = Stairs->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = StairMesh->GetBounds();
		return static_cast<float>(Bounds.Origin.X + Bounds.BoxExtent.X) * 1.35f;
	}
	return 614.0f;
}

void AProceduralFacilityActor::AddStair(const FVector& Location, float Yaw, float RiseCm)
{
	// Location is the bottom step's resting point and RiseCm the height it must
	// climb, so the stair is stretched vertically to span exactly that gap. The
	// previous fixed 1.35 scale only reached 260 cm, so a stair serving the 330 cm
	// bridge was pushed up until its top met the walkway - leaving its first step
	// 80 cm above the floor slab, far past a character's 45 cm step height. That
	// made every upper floor unreachable on foot even though it looked connected.
	float MeshHeight = 192.36f;
	if (const UStaticMesh* StairMesh = Stairs->GetStaticMesh())
	{
		const float AuthoredHeight = StairMesh->GetBounds().BoxExtent.Z * 2.0f;
		if (AuthoredHeight > KINDA_SMALL_NUMBER)
			MeshHeight = AuthoredHeight;
	}
	const float VerticalScale = FMath::Max(RiseCm, 1.0f) / MeshHeight;
	Stairs->AddInstance(FTransform(
		FRotator(0, Yaw, 0), Location, FVector(1.35f, 1.35f, VerticalScale)));
}

void AProceduralFacilityActor::AddIndustrialWorkCell(const FVector& Center, float Yaw, uint8 Variant)
{
	const FRotator Rotation(0.0f, Yaw, 0.0f);
	auto RotateOffset = [&Rotation, &Center](const FVector& Offset)
	{
		return Center + Rotation.RotateVector(Offset);
	};

	// Center.Z is the slab pivot; the walkable surface sits 20 cm above it. Props used
	// to be placed on the pivot, so every one sank 20 cm into the floor - and the
	// pallet, whose pivot sits near its top rather than its base, vanished under it
	// completely. Rest each prop on the surface using its own mesh minimum.
	auto PlaceOnFloor = [this, &Rotation, &RotateOffset, &Center](
		UHierarchicalInstancedStaticMeshComponent* Component,
		const FVector& Offset,
		const FVector& Scale,
		float ExtraYaw)
	{
		FVector Location = RotateOffset(Offset);
		Location.Z = Center.Z + 20.0f;
		if (const UStaticMesh* Mesh = Component->GetStaticMesh())
		{
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			Location.Z -= static_cast<float>(Bounds.Origin.Z - Bounds.BoxExtent.Z) * Scale.Z;
		}
		Component->AddInstance(FTransform(Rotation + FRotator(0.0f, ExtraYaw, 0.0f), Location, Scale));
	};

	// A work cell is deliberately asymmetrical: one side is searchable clutter,
	// the other remains a 2.4m+ circulation lane. Variant changes which corner is
	// dense without changing entrances or the facility footprint.
	PlaceOnFloor(FactoryWorkTables, FVector(-180.0f, 0.0f, 0.0f), FVector(0.82f), 0.0f);
	PlaceOnFloor(FactoryPallets, FVector(210.0f, 170.0f, 0.0f), FVector(0.86f), 12.0f);
	PlaceOnFloor(FactoryPallets, FVector(225.0f, -55.0f, 0.0f), FVector(0.78f), -8.0f);
	PlaceOnFloor(FactoryBarrels, FVector(-260.0f, 190.0f, 0.0f), FVector(0.88f), 0.0f);
	if ((Variant & 1) == 0)
		PlaceOnFloor(FactoryBarrels, FVector(-265.0f, -135.0f, 0.0f), FVector(0.82f), 0.0f);
	// SM_power_box_1 is a 251 cm open steel service frame with its pivot at the base.
	// Run it as a utility column from the floor surface to the ceiling 310 cm above -
	// the same clearance on the ground floor (20 to 330) and upstairs (350 to 660).
	PlaceOnFloor(FactoryPowerBoxes, FVector(0.0f, 285.0f, 0.0f), FVector(0.62f, 0.62f, 1.2336f), 0.0f);
	// The lamp hangs from the ceiling, so it keeps its absolute offset.
	FactoryLamps->AddInstance(FTransform(Rotation, RotateOffset(FVector(0.0f, 0.0f, 285.0f)), FVector(0.82f)));
}

void AProceduralFacilityActor::BuildIndustrialRaid()
{
	AddFloorRect(3000, 3000, 0);
	const uint8 Variant = LayoutVariant & 3;
	const float BridgeYByVariant[4] = { -850.0f, 0.0f, 850.0f, 0.0f };
	const float RightRoomYByVariant[4] = { -1850.0f, 1850.0f, 1850.0f, -1850.0f };
	// Ground-floor patrol anchors for the AI sockets. These used to double as the
	// stair positions, which is how both flights ended up far off the bridge deck.
	const float LeftHallYByVariant[4] = { -1750.0f, 1550.0f, -1650.0f, 650.0f };
	const float RightHallYByVariant[4] = { 1650.0f, -1750.0f, 650.0f, -1650.0f };
	// Where each flight meets the deck. Varying X keeps the two routes up asymmetric
	// per seed while staying inside the bridge's -1600..1600 span.
	const float LeftStairXByVariant[4] = { -1050.0f, -600.0f, -1250.0f, -900.0f };
	const float RightStairXByVariant[4] = { 1050.0f, 1250.0f, 600.0f, 900.0f };
	const float BridgeY = BridgeYByVariant[Variant];
	const float RightRoomY = RightRoomYByVariant[Variant];
	const float LeftHallY = LeftHallYByVariant[Variant];
	const float RightHallY = RightHallYByVariant[Variant];
	const float LeftStairX = LeftStairXByVariant[Variant];
	const float RightStairX = RightStairXByVariant[Variant];

	// Two genuine 30 m Factory Pack halls, scaled to 19.6 m, fill the east and
	// west wings without crossing the 14 m central assault lane.  Their authored
	// roof trusses, doors and interior structure replace the old pink slab-roof
	// blockout while the code-built perimeter continues to enforce fair routes.
	constexpr float HallScale = 0.65f;
	FactoryHalls->AddInstance(FTransform(
		FRotator::ZeroRotator,
		FVector(-2650.0f, -980.0f, 0.0f),
		FVector(HallScale)));
	FactoryHalls->AddInstance(FTransform(
		FRotator::ZeroRotator,
		FVector(700.0f, -980.0f, 0.0f),
		FVector(HallScale)));
	// This 3x3 is the only facility at the WarZone centre, so its stack doubles as the
	// map's wayfinding landmark: every seed reshuffles the roads, but a 41 m silhouette
	// still tells a player on the outskirts which way the middle is. SM_chimney is
	// authored 40.9 m tall and was scaled down to 18.8 m, which reads as just another
	// prop from the 450 m edge-to-centre distance. Facility components carry no cull
	// distance, so it stays drawn across the whole 900 m map. The shorter second stack
	// gives the skyline a recognisable profile instead of one lone pole.
	const float ChimneyY = Variant % 2 == 0 ? 2140.0f : -2140.0f;
	FactoryChimneys->AddInstance(FTransform(
		FRotator(0.0f, Variant * 18.0f, 0.0f),
		FVector(2320.0f, ChimneyY, 20.0f),
		FVector(1.0f)));
	FactoryChimneys->AddInstance(FTransform(
		FRotator(0.0f, 40.0f - Variant * 12.0f, 0.0f),
		FVector(1450.0f, ChimneyY * 0.82f, 20.0f),
		FVector(0.58f)));
	// Tanks occupy offset service pockets rather than forming another repeated grid.
	FactoryTanks->AddInstance(FTransform(FRotator(0.0f, 18.0f, 0.0f), FVector(-2050.0f, 2050.0f, 0.0f), FVector(1.20f)));
	FactoryTanks->AddInstance(FTransform(FRotator(0.0f, -32.0f, 0.0f), FVector(-1550.0f, 2220.0f, 0.0f), FVector(0.92f)));
	FactoryTanks->AddInstance(FTransform(FRotator(0.0f, 64.0f, 0.0f), FVector(1650.0f, -2220.0f, 0.0f), FVector(1.05f)));

	// Use the Factory pack's authored metal fence instead of a solid ring of CQB
	// blockout walls.  Short, separated runs establish the secure perimeter while
	// leaving four readable breaches and keeping both real halls visible on approach.
	auto AddFactoryFenceRun = [this](const FVector& Start, const FVector& End)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size2D();
		if (Length < KINDA_SMALL_NUMBER)
			return;
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(Length / 318.0f));
		const FVector Direction = Delta.GetSafeNormal2D();
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Direction.Y, Direction.X));
		const float Step = Length / SegmentCount;
		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			FactoryFences->AddInstance(FTransform(
				FRotator(0.0f, Yaw, 0.0f),
				Start + Direction * (Step * Index),
				FVector(Step / 320.0f, 1.0f, 1.0f)));
		}
	};
	AddFactoryFenceRun(FVector(-2800, -2700, 0), FVector(-1450, -2700, 0));
	AddFactoryFenceRun(FVector(1550, -2700, 0), FVector(2800, -2700, 0));
	AddFactoryFenceRun(FVector(-2800, 2700, 0), FVector(-1750, 2700, 0));
	AddFactoryFenceRun(FVector(1300, 2700, 0), FVector(2800, 2700, 0));
	AddFactoryFenceRun(FVector(-2800, -2700, 0), FVector(-2800, -1150, 0));
	AddFactoryFenceRun(FVector(-2800, 1050, 0), FVector(-2800, 2700, 0));
	AddFactoryFenceRun(FVector(2800, -2700, 0), FVector(2800, -1450, 0));
	AddFactoryFenceRun(FVector(2800, 1250, 0), FVector(2800, 2700, 0));
	// A narrow cross-bridge links both authored halls. Its Y position changes per
	// seed so repeated 3x3 POIs do not expose the same firing angle every match.
	AddRoofRect(FVector(0.0f, BridgeY, 0.0f), 1600.0f, 220.0f, 330.0f);
	// Each rail breaks around the stair that arrives at it. A continuous run would
	// leave the flight landing against a solid barrier.
	auto AddBridgeRailing = [this, BridgeY](float EdgeOffsetY, float GapCentreX)
	{
		const float RailY = BridgeY + EdgeOffsetY;
		AddRailingRun(FVector(-1600.0f, RailY, 0.0f), FVector(GapCentreX - 230.0f, RailY, 0.0f), 350.0f);
		AddRailingRun(FVector(GapCentreX + 230.0f, RailY, 0.0f), FVector(1600.0f, RailY, 0.0f), 350.0f);
	};
	AddBridgeRailing(-230.0f, LeftStairX);
	AddBridgeRailing(230.0f, RightStairX);

	// Both flights run perpendicular to the deck and finish flush with its edge, the
	// left climbing from -Y and the right from +Y. They previously reused the hall
	// patrol Y values, so they topped out 6 m and 22 m clear of the bridge in open
	// air - the upper level looked connected but nothing could actually reach it.
	const float StairTopOffset = GetStairTopOffsetCm();
	AddStair(FVector(LeftStairX, BridgeY - 220.0f - StairTopOffset, 20.0f), 90.0f, 330.0f);
	AddStair(FVector(RightStairX, BridgeY + 220.0f + StairTopOffset, 20.0f), -90.0f, 330.0f);

	// Complete upper control rooms terminate the bridge at both ends. Their inner
	// walls have wide door gaps, so the bridge is a traversable second combat loop
	// rather than a decorative catwalk. Roof silhouettes also break up the two
	// otherwise identical hall masses in the aerial view.
	for (const float RoomX : { -1950.0f, 1950.0f })
	{
		const float MinX = RoomX - 430.0f;
		const float MaxX = RoomX + 430.0f;
		const float MinY = BridgeY - 480.0f;
		const float MaxY = BridgeY + 480.0f;
		AddRoofRect(FVector(RoomX, BridgeY, 0.0f), 430.0f, 480.0f, 330.0f);
		AddWallRun(FVector(MinX, MinY, 0.0f), FVector(MaxX, MinY, 0.0f), 480.0f, false, true);
		AddWallRun(FVector(MaxX, MinY, 0.0f), FVector(MaxX, MaxY, 0.0f), 480.0f, RoomX < 0.0f, true);
		AddWallRun(FVector(MaxX, MaxY, 0.0f), FVector(MinX, MaxY, 0.0f), 480.0f, false, true);
		AddWallRun(FVector(MinX, MaxY, 0.0f), FVector(MinX, MinY, 0.0f), 480.0f, RoomX > 0.0f, true);
		AddRoofRect(FVector(RoomX, BridgeY, 0.0f), 500.0f, 500.0f, 660.0f, 500.0f);
		FactoryDoors->AddInstance(FTransform(
			FRotator(0.0f, RoomX < 0.0f ? 90.0f : -90.0f, 0.0f),
			FVector(RoomX < 0.0f ? MaxX : MinX, BridgeY, 330.0f),
			FVector(0.92f)));
		AddIndustrialWorkCell(FVector(RoomX, BridgeY, 330.0f), RoomX < 0.0f ? 90.0f : -90.0f, Variant);
	}

	// Ground-floor partition spines create short searchable bays inside each hall
	// while keeping the north/south assault lane and both flank routes open.
	const float PartitionShift = Variant % 2 == 0 ? 180.0f : -180.0f;
	AddWallRun(FVector(-2050.0f, -1650.0f, 0.0f), FVector(-2050.0f, 1550.0f, 0.0f), 150.0f, true, true);
	AddWallRun(FVector(2050.0f, -1550.0f, 0.0f), FVector(2050.0f, 1650.0f, 0.0f), 150.0f, true, true);
	AddWallRun(FVector(-2550.0f, PartitionShift, 0.0f), FVector(-1450.0f, PartitionShift, 0.0f), 150.0f, true, false);
	AddWallRun(FVector(1450.0f, -PartitionShift, 0.0f), FVector(2550.0f, -PartitionShift, 0.0f), 150.0f, true, false);
	FactoryDoors->AddInstance(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(-2050.0f, 0.0f, 0.0f), FVector(0.92f)));
	FactoryDoors->AddInstance(FTransform(FRotator(0.0f, -90.0f, 0.0f), FVector(2050.0f, 0.0f, 0.0f), FVector(0.92f)));
	AddIndustrialWorkCell(FVector(-2350.0f, -900.0f, 0.0f), 0.0f, Variant);
	AddIndustrialWorkCell(FVector(-1650.0f, 950.0f, 0.0f), 180.0f, Variant + 1);
	AddIndustrialWorkCell(FVector(2350.0f, 850.0f, 0.0f), 180.0f, Variant + 2);
	AddIndustrialWorkCell(FVector(1650.0f, -950.0f, 0.0f), 0.0f, Variant + 3);
	// Overhead pipe lines make the halls feel occupied and guide the eye toward
	// the bridge. They stay above the character capsule and never cross a doorway.
	FactoryPipes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-2100.0f, -350.0f, 290.0f), FVector(1.35f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-2100.0f, 850.0f, 290.0f), FVector(1.10f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(2100.0f, 350.0f, 290.0f), FVector(1.35f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(2100.0f, -850.0f, 290.0f), FVector(1.10f, 1.0f, 1.0f)));

	// Seeded cover pockets create short fights off the main north/south lane.
	// Their anchors remain outside the clear central corridor and entrances.
	FRandomStream Stream(static_cast<int32>(LocalSeed ^ (static_cast<int64>(Variant) << 24)));
	const FVector ContainerAnchors[8] =
	{
		FVector(-2050.0f, -2050.0f, 20.0f), FVector(-1250.0f, -1050.0f, 20.0f),
		FVector(-2050.0f, 250.0f, 20.0f), FVector(-1350.0f, 1750.0f, 20.0f),
		FVector(2050.0f, 2050.0f, 20.0f), FVector(1250.0f, 1050.0f, 20.0f),
		FVector(2050.0f, -250.0f, 20.0f), FVector(1350.0f, -1750.0f, 20.0f)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ContainerAnchors); ++Index)
	{
		FVector Location = ContainerAnchors[(Index + Variant * 2) % UE_ARRAY_COUNT(ContainerAnchors)];
		Location.X += Stream.FRandRange(-90.0f, 90.0f);
		Location.Y += Stream.FRandRange(-90.0f, 90.0f);
		AddContainer(Location, (Index + Variant) % 3 == 0 ? 0.0f : 90.0f);
	}
	const float CoverShift = Variant % 2 == 0 ? 260.0f : -260.0f;
	AddCover(FVector(-1700.0f, BridgeY - 650.0f, 20.0f), FVector(0.72f), 90.0f);
	AddCover(FVector(1700.0f, BridgeY + 650.0f, 20.0f), FVector(0.72f), 90.0f);
	AddCover(FVector(-1050.0f, CoverShift, 20.0f), FVector(0.58f), 0.0f);
	AddCover(FVector(1050.0f, -CoverShift, 20.0f), FVector(0.58f), 0.0f);
	AddCover(FVector(-350.0f, -1450.0f, 20.0f), FVector(0.50f), 90.0f);
	AddCover(FVector(350.0f, 1450.0f, 20.0f), FVector(0.50f), 90.0f);

	LootSocket->SetRelativeLocation(FVector(-1850.0f, -RightRoomY * 0.45f, 120.0f));
	UpperLootSocket->SetRelativeLocation(FVector(1950.0f, BridgeY, 450.0f));
	AISocketA->SetRelativeLocation(FVector(-900.0f, LeftHallY, 120.0f));
	AISocketB->SetRelativeLocation(FVector(900.0f, RightHallY, 120.0f));
}

void AProceduralFacilityActor::BuildSatelliteCamp()
{
	AddFloorRect(2000, 2000, 0);
	const uint8 Variant = LayoutVariant & 3;
	const float CornerXByVariant[4] = { 1050.0f, -1050.0f, -1050.0f, 1050.0f };
	const float CornerYByVariant[4] = { 950.0f, 950.0f, -950.0f, -950.0f };
	const float DeckX = CornerXByVariant[Variant];
	const float DeckY = CornerYByVariant[Variant];
	const float SignX = FMath::Sign(DeckX);
	const float SignY = FMath::Sign(DeckY);
	AddWallRun(FVector(-1800, -1700, 0), FVector(1800, -1700, 0), 150, true, true);
	AddWallRun(FVector(-1800, 1700, 0), FVector(1800, 1700, 0), 150, true, true);
	// The selected side becomes a secondary breach; the opposite side remains
	// defended so every camp has a readable strong side and a flank route.
	AddWallRun(FVector(-1800, -1700, 0), FVector(-1800, 1700, 0), 150, DeckX < 0.0f, false);
	AddWallRun(FVector(1800, -1700, 0), FVector(1800, 1700, 0), 150, DeckX > 0.0f, false);
	AddContainer(FVector(-SignX * 900.0f, SignY * 700.0f, 20.0f), 90.0f);
	AddContainer(FVector(SignX * 850.0f, -SignY * 750.0f, 20.0f), Variant % 2 == 0 ? 90.0f : 0.0f);
	AddCover(FVector(-SignX * 250.0f, -SignY * 200.0f, 20.0f), FVector(0.68f), Variant * 90.0f);
	AddCover(FVector(SignX * 1050.0f, SignY * 50.0f, 20.0f), FVector(0.52f), Variant % 2 ? 90.0f : 0.0f);
	AddCover(FVector(-SignX * 1150.0f, -SignY * 900.0f, 20.0f), FVector(0.48f), Variant % 2 ? 0.0f : 90.0f);
	// One raised observation deck gives the 2x2 POI a vertical landmark and an
	// alternate firing angle.  The occupied corner changes with LayoutVariant.
	AddRoofRect(FVector(DeckX, DeckY, 0.0f), 400.0f, 400.0f, 280.0f);
	const float MinX = DeckX - 400.0f;
	const float MaxX = DeckX + 400.0f;
	const float MinY = DeckY - 400.0f;
	const float MaxY = DeckY + 400.0f;
	const float OuterX = DeckX + SignX * 400.0f;
	const float OuterY = DeckY + SignY * 400.0f;
	const float InnerY = DeckY - SignY * 400.0f;
	AddRailingRun(FVector(OuterX, MinY, 0.0f), FVector(OuterX, MaxY, 0.0f), 300.0f);
	AddRailingRun(FVector(MinX, OuterY, 0.0f), FVector(MaxX, OuterY, 0.0f), 300.0f);
	AddRailingRun(FVector(MinX, InnerY, 0.0f), FVector(MaxX, InnerY, 0.0f), 300.0f);
	// Rests on the camp slab (top Z=20) and climbs to the deck surface (top Z=300).
	AddStair(FVector(DeckX - SignX * 580.0f, DeckY, 20.0f), SignX > 0.0f ? 0.0f : 180.0f, 280.0f);
	// The opposite corner contains a compact operations hut with an actual room
	// split and workbench, turning this 2x2 from a fenced prop yard into a POI with
	// indoor/outdoor combat choices.
	const FVector HutCenter(-SignX * 950.0f, -SignY * 850.0f, 0.0f);
	const float HutMinX = HutCenter.X - 520.0f;
	const float HutMaxX = HutCenter.X + 520.0f;
	const float HutMinY = HutCenter.Y - 430.0f;
	const float HutMaxY = HutCenter.Y + 430.0f;
	AddRoofRect(HutCenter, 520.0f, 430.0f, 0.0f);
	AddWallRun(FVector(HutMinX, HutMinY, 0.0f), FVector(HutMaxX, HutMinY, 0.0f), 150.0f, SignY < 0.0f, true);
	AddWallRun(FVector(HutMaxX, HutMinY, 0.0f), FVector(HutMaxX, HutMaxY, 0.0f), 150.0f, SignX > 0.0f, true);
	AddWallRun(FVector(HutMaxX, HutMaxY, 0.0f), FVector(HutMinX, HutMaxY, 0.0f), 150.0f, SignY > 0.0f, true);
	AddWallRun(FVector(HutMinX, HutMaxY, 0.0f), FVector(HutMinX, HutMinY, 0.0f), 150.0f, SignX < 0.0f, true);
	AddWallRun(FVector(HutCenter.X, HutMinY + 120.0f, 0.0f), FVector(HutCenter.X, HutMaxY - 120.0f, 0.0f), 150.0f, true, false);
	// One slab matches the 1040 x 860 cm wall footprint exactly.  The old tiled
	// roof rounded the non-module dimensions asymmetrically, leaving one edge
	// short while producing a large unsupported overhang on the opposite edge.
	AddRoofRect(HutCenter, 520.0f, 430.0f, 330.0f);
	AddIndustrialWorkCell(HutCenter + FVector(-SignX * 140.0f, SignY * 40.0f, 0.0f), SignX > 0.0f ? 180.0f : 0.0f, Variant);
	LootSocket->SetRelativeLocation(FVector(-SignX * 900.0f, SignY * 700.0f, 120.0f));
	UpperLootSocket->SetRelativeLocation(FVector(DeckX, DeckY, 360.0f));
	AISocketA->SetRelativeLocation(FVector(-SignX * 1200.0f, -SignY * 900.0f, 120.0f));
	AISocketB->SetRelativeLocation(FVector(SignX * 1200.0f, SignY * 900.0f, 120.0f));
}

void AProceduralFacilityActor::BuildLongBarracks()
{
	AddFloorRect(2000, 1000, 0);
	AddRoofRect(FVector::ZeroVector, 2000, 1000, 330, 1000);
	AddWallRun(FVector(-1900, -850, 0), FVector(1900, -850, 0), 150, true, true);
	AddWallRun(FVector(-1900, 850, 0), FVector(1900, 850, 0), 150, true, true);
	AddWallRun(FVector(-1900, -850, 0), FVector(-1900, 850, 0), 150, true, false);
	AddWallRun(FVector(1900, -850, 0), FVector(1900, 850, 0), 150, true, false);
	const uint8 Variant = LayoutVariant & 3;
	const float PartitionX = Variant % 2 == 0 ? -250.0f : 250.0f;
	AddWallRun(FVector(PartitionX, -850, 0), FVector(PartitionX, 850, 0), 150, true, false);
	// Two short return walls form bedrooms/storage bays without creating dead-end
	// closets. The alternating offset preserves a clear longitudinal route.
	AddWallRun(FVector(-1450, 120, 0), FVector(-650, 120, 0), 150, true, true);
	AddWallRun(FVector(650, -120, 0), FVector(1450, -120, 0), 150, true, true);
	FactoryDoors->AddInstance(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(PartitionX, 0.0f, 0.0f), FVector(0.92f)));
	AddIndustrialWorkCell(FVector(-1150.0f, 420.0f, 0.0f), 90.0f, Variant);
	AddIndustrialWorkCell(FVector(1050.0f, -420.0f, 0.0f), -90.0f, Variant + 1);
	AddCover(FVector(-650, -350, 20), FVector(0.52f), 90);
	AddCover(FVector(650, 350, 20), FVector(0.52f), 90);
	FactoryPipes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-750.0f, 690.0f, 285.0f), FVector(0.9f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(750.0f, -690.0f, 285.0f), FVector(0.9f, 1.0f, 1.0f)));
	LootSocket->SetRelativeLocation(FVector(-1100, 350, 120));
	UpperLootSocket->SetRelativeLocation(FVector(1100, -350, 120));
	AISocketA->SetRelativeLocation(FVector(-1500, -450, 120));
	AISocketB->SetRelativeLocation(FVector(1500, 450, 120));
}

void AProceduralFacilityActor::BuildLinearTrench()
{
	AddFloorRect(4000, 1000, 0);
	// Broken parallel walls produce a long sightline with repeatable crossovers.
	for (int32 Segment = 0; Segment < 4; ++Segment)
	{
		const float X0 = -3800.0f + Segment * 2000.0f;
		AddWallRun(FVector(X0, -650, 0), FVector(X0 + 1400, -650, 0), 60, false, false);
		AddWallRun(FVector(X0 + 600, 650, 0), FVector(X0 + 2000, 650, 0), 60, false, false);
		AddCover(FVector(X0 + 1000, Segment % 2 ? -250 : 250, 20), FVector(0.55f), 90);
	}
	LootSocket->SetRelativeLocation(FVector(-2600, 0, 120));
	UpperLootSocket->SetRelativeLocation(FVector(2600, 0, 120));
	AISocketA->SetRelativeLocation(FVector(-3400, 0, 120));
	AISocketB->SetRelativeLocation(FVector(3400, 0, 120));
}

void AProceduralFacilityActor::BuildDowntownBlock()
{
	const uint8 Variant = LayoutVariant & 3;

	// Three separate, enterable two-storey shells create streets and cross alleys.
	// Floors are limited to each shell instead of covering the whole 3x3 footprint,
	// so the raised terrain remains visible as an outdoor district rather than a
	// floating square foundation.
	struct FBlockShell { FVector Center; FVector2D HalfSize; };
	const FBlockShell Shells[3] =
	{
		{ FVector(-1500.0f, -1300.0f, 0.0f), FVector2D(900.0f, 750.0f) },
		{ FVector(1450.0f, -1250.0f, 0.0f), FVector2D(850.0f, 800.0f) },
		{ FVector(150.0f, 1550.0f, 0.0f), FVector2D(1200.0f, 700.0f) }
	};
	auto AddSlabRect = [this](float MinX, float MaxX, float MinY, float MaxY, float Z)
	{
		const float Width = MaxX - MinX;
		const float Depth = MaxY - MinY;
		if (Width <= 1.0f || Depth <= 1.0f)
			return;
		Floors->AddInstance(FTransform(
			FRotator::ZeroRotator,
			FVector(MaxX, MaxY, Z),
			FVector(Width / 200.0f, Depth / 200.0f, 1.0f)));
	};
	auto AddFacadeRun = [this](const FVector& Start, const FVector& End, bool bDoor)
	{
		const FVector Delta = End - Start;
		const float Length = Delta.Size2D();
		const int32 Segments = FMath::Max(1, FMath::RoundToInt(Length / 500.0f));
		const FVector Step = Delta / Segments;
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(Delta.Y, Delta.X)) - 90.0f;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			if (bDoor && Index == Segments / 2)
				continue;
			const FVector Base = Start + Step * (Index + 0.5f);
			// The authored storefront is exactly 500 cm wide and about 397 cm
			// tall. Fit every segment to its run and terminate it at the 340 cm
			// second-floor slab instead of allowing overlaps and height clashes.
			DowntownStorefronts->AddInstance(FTransform(
				FRotator(0.0f, Yaw, 0.0f),
				Base,
				FVector(1.0f, Step.Size2D() / 500.0f, 0.8567f)));
		}
	};
	for (int32 ShellIndex = 0; ShellIndex < UE_ARRAY_COUNT(Shells); ++ShellIndex)
	{
		const FBlockShell& Shell = Shells[ShellIndex];
		const float MinX = Shell.Center.X - Shell.HalfSize.X;
		const float MaxX = Shell.Center.X + Shell.HalfSize.X;
		const float MinY = Shell.Center.Y - Shell.HalfSize.Y;
		const float MaxY = Shell.Center.Y + Shell.HalfSize.Y;
		AddFacadeRun(FVector(MinX, MinY, 0.0f), FVector(MaxX, MinY, 0.0f), true);
		AddFacadeRun(FVector(MaxX, MinY, 0.0f), FVector(MaxX, MaxY, 0.0f), ShellIndex == 1);
		AddFacadeRun(FVector(MaxX, MaxY, 0.0f), FVector(MinX, MaxY, 0.0f), ShellIndex == 2);
		AddFacadeRun(FVector(MinX, MaxY, 0.0f), FVector(MinX, MinY, 0.0f), ShellIndex == 0);

		const FVector StairCenter = Shell.Center + FVector(
			ShellIndex == 1 ? -250.0f : 250.0f,
			ShellIndex == 2 ? -180.0f : 180.0f,
			0.0f);
		const bool bStairFacesPositiveX = ShellIndex % 2 == 0;
		const float VoidMinX = FMath::Max(MinX, StairCenter.X + (bStairFacesPositiveX ? -100.0f : -700.0f));
		const float VoidMaxX = FMath::Min(MaxX, StairCenter.X + (bStairFacesPositiveX ? 700.0f : 100.0f));
		const float VoidMinY = FMath::Max(MinY, StairCenter.Y + (bStairFacesPositiveX ? -300.0f : -150.0f));
		const float VoidMaxY = FMath::Min(MaxY, StairCenter.Y + (bStairFacesPositiveX ? 150.0f : 300.0f));

		// Exact, continuous slabs. The upper floor is four rectangles around the
		// actual stair footprint, so no rounded 500 cm tile can jut into the void.
		AddSlabRect(MinX, MaxX, MinY, MaxY, 0.0f);
		AddSlabRect(MinX, VoidMinX, MinY, MaxY, 340.0f);
		AddSlabRect(VoidMaxX, MaxX, MinY, MaxY, 340.0f);
		AddSlabRect(VoidMinX, VoidMaxX, MinY, VoidMinY, 340.0f);
		AddSlabRect(VoidMinX, VoidMaxX, VoidMaxY, MaxY, 340.0f);

		// Ground floor rooms: a broken cross-wall gives two searchable rooms and
		// preserves a central route from the street entrance to the stair.
		AddWallRun(
			FVector(MinX + 180.0f, Shell.Center.Y, 0.0f),
			FVector(MaxX - 180.0f, Shell.Center.Y, 0.0f),
			150.0f, true, ShellIndex % 2 == 0);
		AddWallRun(
			FVector(Shell.Center.X, MinY + 180.0f, 0.0f),
			FVector(Shell.Center.X, MaxY - 180.0f, 0.0f),
			150.0f, true, ShellIndex % 2 == 1);

		// The upper storey uses the reliable CQB modular walls.  The previous Fab
		// facade pivot placed these panels in mid-air; supported slabs and walls now
		// make the complete floor enterable and collision-correct.
		AddWallRun(FVector(MinX, MinY, 0.0f), FVector(MaxX, MinY, 0.0f), 490.0f, true, true);
		AddWallRun(FVector(MaxX, MinY, 0.0f), FVector(MaxX, MaxY, 0.0f), 490.0f, false, true);
		AddWallRun(FVector(MaxX, MaxY, 0.0f), FVector(MinX, MaxY, 0.0f), 490.0f, false, true);
		AddWallRun(FVector(MinX, MaxY, 0.0f), FVector(MinX, MinY, 0.0f), 490.0f, false, true);
		AddWallRun(
			FVector(MinX + 200.0f, Shell.Center.Y, 0.0f),
			FVector(MaxX - 200.0f, Shell.Center.Y, 0.0f),
			490.0f, true, true);
		AddRoofRect(Shell.Center, Shell.HalfSize.X, Shell.HalfSize.Y, 640.0f);

		// The upper slabs sit at pivot Z=340, so their walking surface is 360. The old
		// hard-coded 1.665 scale only climbed to 340, leaving a 20 cm lip at the top.
		AddStair(StairCenter + FVector(0.0f, 0.0f, 20.0f),
			bStairFacesPositiveX ? 0.0f : 180.0f, 340.0f);
		AddRailingRun(FVector(VoidMinX, VoidMinY, 0.0f), FVector(VoidMaxX, VoidMinY, 0.0f), 340.0f);
		AddRailingRun(FVector(VoidMinX, VoidMaxY, 0.0f), FVector(VoidMaxX, VoidMaxY, 0.0f), 340.0f);
		if (bStairFacesPositiveX)
			AddRailingRun(FVector(VoidMinX, VoidMinY, 0.0f), FVector(VoidMinX, VoidMaxY, 0.0f), 340.0f);
		else
			AddRailingRun(FVector(VoidMaxX, VoidMinY, 0.0f), FVector(VoidMaxX, VoidMaxY, 0.0f), 340.0f);
		AddCover(Shell.Center + FVector(-280.0f, 180.0f, 20.0f), FVector(0.42f), ShellIndex * 90.0f);
		AddCover(Shell.Center + FVector(260.0f, -170.0f, 360.0f), FVector(0.38f), 90.0f + ShellIndex * 90.0f);
	}

	// Street furniture creates a town identity without blocking the 8-12 m alleys.
	const FVector StreetlightLocations[] =
	{
		FVector(-2600, -150, 0), FVector(-900, 150, 0), FVector(900, 150, 0),
		FVector(2600, -150, 0), FVector(-1550, 2600, 0), FVector(1600, 2600, 0)
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(StreetlightLocations); ++Index)
		DowntownStreetlights->AddInstance(FTransform(FRotator(0.0f, Index % 2 ? 180.0f : 0.0f, 0.0f), StreetlightLocations[Index], FVector(0.62f)));
	DowntownPlanters->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-450, 50, 0), FVector(0.72f)));
	DowntownPlanters->AddInstance(FTransform(FRotator(0, 0, 0), FVector(500, -50, 0), FVector(0.65f)));
	DowntownPlanters->AddInstance(FTransform(FRotator(0, Variant * 90.0f, 0), FVector(0, 2450, 0), FVector(0.62f)));
	DowntownBenches->AddInstance(FTransform(FRotator(0, 90, 0), FVector(-700, 650, 0), FVector(1.0f)));
	DowntownBenches->AddInstance(FTransform(FRotator(0, -90, 0), FVector(700, 650, 0), FVector(1.0f)));

	LootSocket->SetRelativeLocation(FVector(-1500.0f, -1300.0f, 120.0f));
	UpperLootSocket->SetRelativeLocation(FVector(150.0f, 1550.0f, 455.0f));
	AISocketA->SetRelativeLocation(FVector(-500.0f, 100.0f, 120.0f));
	AISocketB->SetRelativeLocation(FVector(900.0f, 450.0f, 120.0f));
}

void AProceduralFacilityActor::BuildFactoryConstruction()
{
	AddFloorRect(2000.0f, 2000.0f, 0.0f);
	const uint8 Variant = LayoutVariant & 3;
	// One scaled authored hall gives the site a believable shell while the crane,
	// containers and site office leave a traversable construction yard around it.
	FactoryHalls->AddInstance(FTransform(FRotator(0.0f, Variant % 2 ? 90.0f : 0.0f, 0.0f), FVector(-950.0f, -950.0f, 0.0f), FVector(0.58f)));
	FactoryCranes->AddInstance(FTransform(FRotator(0.0f, 18.0f + Variant * 17.0f, 0.0f), FVector(-1500.0f, 1150.0f, 50.0f), FVector(0.78f, 1.0f, 1.7f)));
	FactorySiteHouses->AddInstance(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(1050.0f, 1200.0f, 0.0f), FVector(0.82f)));
	AddContainer(FVector(1250.0f, -1150.0f, 20.0f), 90.0f, FVector(0.62f));
	AddContainer(FVector(850.0f, -1150.0f, 20.0f), 90.0f, FVector(0.62f));
	AddContainer(FVector(1450.0f, 250.0f, 20.0f), 0.0f, FVector(0.58f));
	AddCover(FVector(250.0f, 1450.0f, 20.0f), FVector(0.72f), 0.0f);
	AddCover(FVector(1450.0f, 800.0f, 20.0f), FVector(0.55f), 90.0f);

	// Broken perimeter runs: every side has a real gate, never an accidental hole.
	AddRailingRun(FVector(-1850, -1850, 0), FVector(-500, -1850, 0), 0.0f);
	AddRailingRun(FVector(500, -1850, 0), FVector(1850, -1850, 0), 0.0f);
	AddRailingRun(FVector(-1850, 1850, 0), FVector(-500, 1850, 0), 0.0f);
	AddRailingRun(FVector(500, 1850, 0), FVector(1850, 1850, 0), 0.0f);
	AddRailingRun(FVector(-1850, -1850, 0), FVector(-1850, -450, 0), 0.0f);
	AddRailingRun(FVector(-1850, 450, 0), FVector(-1850, 1850, 0), 0.0f);
	AddRailingRun(FVector(1850, -1850, 0), FVector(1850, -450, 0), 0.0f);
	AddRailingRun(FVector(1850, 450, 0), FVector(1850, 1850, 0), 0.0f);

	LootSocket->SetRelativeLocation(FVector(1100.0f, 1200.0f, 120.0f));
	UpperLootSocket->SetRelativeLocation(FVector(-250.0f, -250.0f, 500.0f));
	AISocketA->SetRelativeLocation(FVector(1200.0f, -900.0f, 120.0f));
	AISocketB->SetRelativeLocation(FVector(-1200.0f, 1000.0f, 120.0f));
}

void AProceduralFacilityActor::BuildRuralHideout()
{
	AddFloorRect(2000.0f, 2000.0f, 0.0f);
	const uint8 Variant = LayoutVariant & 3;
	auto BuildCabin = [this](const FVector& Center, float Yaw)
	{
		const FRotator Rotation(0.0f, Yaw, 0.0f);
		auto Place = [&Rotation, &Center](UHierarchicalInstancedStaticMeshComponent* Component, const FVector& Local, float LocalYaw)
		{
			Component->AddInstance(FTransform(Rotation + FRotator(0.0f, LocalYaw, 0.0f), Center + Rotation.RotateVector(Local), FVector::OneVector));
		};
		// 8x8 m shell with one clear doorway and windows on three sides.
		Place(RuralDoorWalls, FVector(0.0f, -400.0f, 0.0f), 90.0f);
		Place(RuralWindowWalls, FVector(-400.0f, -400.0f, 0.0f), 90.0f);
		Place(RuralWindowWalls, FVector(400.0f, -400.0f, 0.0f), 90.0f);
		Place(RuralWindowWalls, FVector(-400.0f, 400.0f, 0.0f), -90.0f);
		Place(RuralWalls, FVector(0.0f, 400.0f, 0.0f), -90.0f);
		Place(RuralWindowWalls, FVector(400.0f, 400.0f, 0.0f), -90.0f);
		Place(RuralWalls, FVector(-400.0f, 0.0f, 0.0f), 0.0f);
		Place(RuralWindowWalls, FVector(400.0f, 0.0f, 0.0f), 180.0f);
		for (float X : { -200.0f, 200.0f })
			for (float Y : { -250.0f, 250.0f })
				Place(RuralRoofs, FVector(X, Y, 335.0f), 0.0f);
	};
	BuildCabin(FVector(-850.0f, -700.0f, 0.0f), Variant % 2 ? 90.0f : 0.0f);
	BuildCabin(FVector(850.0f, 750.0f, 0.0f), Variant % 2 ? -90.0f : 180.0f);
	RuralCaravans->AddInstance(FTransform(FRotator(0.0f, 25.0f + Variant * 31.0f, 0.0f), FVector(950.0f, -1050.0f, 0.0f), FVector(0.72f)));
	RuralPicnicTables->AddInstance(FTransform(FRotator(0.0f, 35.0f, 0.0f), FVector(-150.0f, 1050.0f, 0.0f), FVector(1.0f)));
	for (int32 Segment = 0; Segment < 5; ++Segment)
	{
		RuralFences->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-1600.0f + Segment * 300.0f, -1750.0f, 0.0f), FVector(1.35f)));
		RuralFences->AddInstance(FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(400.0f + Segment * 300.0f, 1750.0f, 0.0f), FVector(1.35f)));
	}
	AddCover(FVector(-200.0f, -100.0f, 20.0f), FVector(0.55f), 20.0f);
	AddCover(FVector(350.0f, 250.0f, 20.0f), FVector(0.48f), -35.0f);

	LootSocket->SetRelativeLocation(FVector(-850.0f, -700.0f, 120.0f));
	UpperLootSocket->SetRelativeLocation(FVector(850.0f, 750.0f, 120.0f));
	AISocketA->SetRelativeLocation(FVector(-1300.0f, 900.0f, 120.0f));
	AISocketB->SetRelativeLocation(FVector(1250.0f, -900.0f, 120.0f));
}
