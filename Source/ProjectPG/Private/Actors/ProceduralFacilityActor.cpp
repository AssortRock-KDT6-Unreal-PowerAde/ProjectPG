// 코드로 만드는 여러 칸짜리 시설. 절차 맵의 화면 단계가 쓴다.

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
	// 이 시설들은 런타임에 스폰된 뒤 코드로 조립된다. 등록된 Static 컴포넌트는
	// SetStaticMesh() 를 거부해서, 인스턴스 목록은 멀쩡해 보이는데 그릴 메시가 없게 된다.
	// 그래서 이 작은 시설용 컴포넌트들은 Movable 로 둬서
	// Configure/Rebuild 중에 메시를 안전하게 넣을 수 있게 한다.
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
		// 이 목록은 아래 실내 묶음과 맞춰 둔다. 모든 인스턴스는
		// 명시된 시드와 배치 변형에서 다시 만들어진다.
		Component->ClearInstances();
	for (UHierarchicalInstancedStaticMeshComponent* Component : {
		FactoryPipes, FactoryPallets, FactoryBarrels, FactoryWorkTables,
		FactoryDoors, FactoryPowerBoxes, FactoryLamps })
		Component->ClearInstances();
	// 바뀌는 인스턴스 버퍼는 길찾기 내보내기를 끈 채로 만든다. AddInstance() 전에
	// 빈 ISM 을 Recast 에 등록하면, 런타임 시설 조립 중에 길찾기 옥트리가
	// 잘못된 인스턴스 항목을 들여다볼 수 있다.
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
	// SM_Floor_2x2 는 로컬 X/Y 로 -200..0 에 걸쳐 있고, 중심점이 가운데가 아니라 + 쪽 모서리다.
	// 그 모서리를 (+HalfX,+HalfY) 에 두면
	// 판이 정확히 [-HalfX,+HalfX] x [-HalfY,+HalfY] 를 덮는다.
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
		// CQB 벽 메시는 중심점이 바닥에 있는데, 이 함수의 Z 인자는 원하는 벽 가운데 높이다.
		// 만든 높이 300 cm 를 보정해서 1층에서 Z=150 으로 부르면
		// 판 위에 딱 서게 한다.
		const FVector Location = Start + Step * (Index + 0.5f) + FVector(0, 0, Z - 150.0f);
		UHierarchicalInstancedStaticMeshComponent* Target = bWindows && Index % 3 == 1 ? WindowWalls : SolidWalls;
		// 막힌 벽 판은 폭 200 cm, 창문 판은 폭 100 cm 다.
		// 벽 방향으로만 늘려서, 섞어 놓은 벽 줄도 모서리에서 틈이 없게 한다.
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
	// SurfaceLocation.Z 는 소품이 서는 바닥이다. 팩마다 중심점 위치가 제각각이라
	// - 선반은 밑면보다 50 cm 아래, 컨테이너는 밑면에 있다 - 중심점을 믿지 않고 메시 자신의
	// 최저점만큼 보정한다. 예전엔 부르는 쪽에서 눈대중 높이를 넘겼는데, 그래서 컨테이너는 모두
	// 바닥에서 110 cm, 엄폐물은 54 cm 떠 있었다.
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
	// 고정 1.35 배율에서 계단 중심점부터 계단 꼭대기까지의 거리.
	// 부르는 쪽은 이 값으로 계단을 닿아야 할 통로 높이에 맞춘다.
	if (const UStaticMesh* StairMesh = Stairs->GetStaticMesh())
	{
		const FBoxSphereBounds Bounds = StairMesh->GetBounds();
		return static_cast<float>(Bounds.Origin.X + Bounds.BoxExtent.X) * 1.35f;
	}
	return 614.0f;
}

void AProceduralFacilityActor::AddStair(const FVector& Location, float Yaw, float RiseCm)
{
	// Location 은 첫 계단이 놓이는 지점, RiseCm 은 올라가야 할 높이라서
	// 계단을 세로로 늘려 그 높이차를 딱 채운다. 예전의 1.35 고정 배율은 260 cm 까지만 닿아서,
	// 330 cm 다리용 계단을 꼭대기가 통로에 닿을 때까지 위로 밀어 올렸다 - 그러자 첫 계단이
	// 바닥판보다 80 cm 위에 떠서 캐릭터 턱 높이 45 cm 를 한참 넘었다.
	// 그래서 겉보기엔 이어져 있어도 모든 위층에 걸어서 갈 수 없었다.
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

	// Center.Z 는 판의 중심점이고 걷는 면은 그보다 20 cm 위다. 예전엔 소품을 중심점에 놓아서
	// 전부 바닥에 20 cm 파묻혔고 - 중심점이 밑면이 아니라 윗면 근처에 있는 팔레트는
	// 바닥 밑으로 완전히 사라졌다. 이제는 각 소품을 자기 메시 최저점 기준으로 표면 위에 올린다.
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

	// 작업 칸은 일부러 한쪽으로 치우친다: 한쪽은 뒤질 수 있는 잡동사니,
	// 다른 쪽은 2.4m 이상 이동 통로로 남긴다. 변형은 입구나 차지 칸은 그대로 두고
	// 어느 모서리가 빽빽한지만 바꾼다.
	PlaceOnFloor(FactoryWorkTables, FVector(-180.0f, 0.0f, 0.0f), FVector(0.82f), 0.0f);
	PlaceOnFloor(FactoryPallets, FVector(210.0f, 170.0f, 0.0f), FVector(0.86f), 12.0f);
	PlaceOnFloor(FactoryPallets, FVector(225.0f, -55.0f, 0.0f), FVector(0.78f), -8.0f);
	PlaceOnFloor(FactoryBarrels, FVector(-260.0f, 190.0f, 0.0f), FVector(0.88f), 0.0f);
	if ((Variant & 1) == 0)
		PlaceOnFloor(FactoryBarrels, FVector(-265.0f, -135.0f, 0.0f), FVector(0.82f), 0.0f);
	// SM_power_box_1 은 251 cm 짜리 트인 철제 설비 틀이고 중심점이 밑면에 있다.
	// 바닥 표면부터 310 cm 위 천장까지 설비 기둥처럼 세운다
	// - 1층(20 에서 330)과 2층(350 에서 660)이 같은 높이 여유를 가진다.
	PlaceOnFloor(FactoryPowerBoxes, FVector(0.0f, 285.0f, 0.0f), FVector(0.62f, 0.62f, 1.2336f), 0.0f);
	// 등은 천장에 매달리므로 절대 오프셋을 그대로 쓴다.
	FactoryLamps->AddInstance(FTransform(Rotation, RotateOffset(FVector(0.0f, 0.0f, 285.0f)), FVector(0.82f)));
}

void AProceduralFacilityActor::BuildIndustrialRaid()
{
	AddFloorRect(3000, 3000, 0);
	const uint8 Variant = LayoutVariant & 3;
	const float BridgeYByVariant[4] = { -850.0f, 0.0f, 850.0f, 0.0f };
	const float RightRoomYByVariant[4] = { -1850.0f, 1850.0f, 1850.0f, -1850.0f };
	// AI 자리용 1층 순찰 기준점. 예전엔 이 값을 계단 위치로도 같이 써서,
	// 계단 두 개가 모두 다리 바닥에서 멀리 떨어져 버렸다.
	const float LeftHallYByVariant[4] = { -1750.0f, 1550.0f, -1650.0f, 650.0f };
	const float RightHallYByVariant[4] = { 1650.0f, -1750.0f, 650.0f, -1650.0f };
	// 각 계단이 다리 바닥과 만나는 곳. X 를 바꿔서 시드마다 올라가는 두 길이
	// 비대칭이 되게 하되, 다리의 -1600..1600 범위 안에 둔다.
	const float LeftStairXByVariant[4] = { -1050.0f, -600.0f, -1250.0f, -900.0f };
	const float RightStairXByVariant[4] = { 1050.0f, 1250.0f, 600.0f, 900.0f };
	const float BridgeY = BridgeYByVariant[Variant];
	const float RightRoomY = RightRoomYByVariant[Variant];
	const float LeftHallY = LeftHallYByVariant[Variant];
	const float RightHallY = RightHallYByVariant[Variant];
	const float LeftStairX = LeftStairXByVariant[Variant];
	const float RightStairX = RightStairXByVariant[Variant];

	// 진짜 30 m Factory Pack 건물 두 동을 19.6 m 로 줄여 동쪽과 서쪽 날개를 채우고,
	// 가운데 14 m 돌격 통로는 넘지 않게 한다. 손작업 지붕 트러스, 문, 내부 구조가
	// 예전의 분홍 판 지붕 임시 모양을 대신하고,
	// 코드로 만든 둘레는 계속 공정한 경로를 보장한다.
	constexpr float HallScale = 0.65f;
	FactoryHalls->AddInstance(FTransform(
		FRotator::ZeroRotator,
		FVector(-2650.0f, -980.0f, 0.0f),
		FVector(HallScale)));
	FactoryHalls->AddInstance(FTransform(
		FRotator::ZeroRotator,
		FVector(700.0f, -980.0f, 0.0f),
		FVector(HallScale)));
	// 이 3x3 은 WarZone 중심에 있는 유일한 시설이라 굴뚝이 맵의 길잡이 랜드마크 역할도 한다:
	// 시드마다 도로는 바뀌어도 41 m 실루엣이 외곽 플레이어에게 가운데가 어느 쪽인지 알려 준다.
	// SM_chimney 는 원래 40.9 m 인데 18.8 m 로 줄였더니, 가장자리에서 중심까지 450 m 거리에선
	// 그냥 소품 하나로 보였다. 시설 컴포넌트에는 컬링 거리가 없어서 900 m 맵 전체에서
	// 계속 그려진다. 더 낮은 두 번째 굴뚝을 둬서 하늘선이 장대 하나가 아니라
	// 알아볼 수 있는 모양이 되게 한다.
	const float ChimneyY = Variant % 2 == 0 ? 2140.0f : -2140.0f;
	FactoryChimneys->AddInstance(FTransform(
		FRotator(0.0f, Variant * 18.0f, 0.0f),
		FVector(2320.0f, ChimneyY, 20.0f),
		FVector(1.0f)));
	FactoryChimneys->AddInstance(FTransform(
		FRotator(0.0f, 40.0f - Variant * 12.0f, 0.0f),
		FVector(1450.0f, ChimneyY * 0.82f, 20.0f),
		FVector(0.58f)));
	// 탱크는 또 하나의 반복 격자가 되지 않게 엇갈린 정비 공간에 놓는다.
	FactoryTanks->AddInstance(FTransform(FRotator(0.0f, 18.0f, 0.0f), FVector(-2050.0f, 2050.0f, 0.0f), FVector(1.20f)));
	FactoryTanks->AddInstance(FTransform(FRotator(0.0f, -32.0f, 0.0f), FVector(-1550.0f, 2220.0f, 0.0f), FVector(0.92f)));
	FactoryTanks->AddInstance(FTransform(FRotator(0.0f, 64.0f, 0.0f), FVector(1650.0f, -2220.0f, 0.0f), FVector(1.05f)));

	// CQB 임시 벽으로 꽉 막은 고리 대신 Factory 팩의 손작업 철 울타리를 쓴다.
	// 짧게 끊어 놓은 울타리 줄로 둘레를 지키면서, 잘 보이는 뚫린 곳 네 군데를 남기고
	// 다가갈 때 진짜 건물 두 동이 다 보이게 한다.
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
	// 좁은 연결 다리가 손작업 건물 두 동을 잇는다. Y 위치가 시드마다 바뀌어서
	// 3x3 POI 가 반복돼도 판마다 같은 사격 각도가 나오지 않는다.
	AddRoofRect(FVector(0.0f, BridgeY, 0.0f), 1600.0f, 220.0f, 330.0f);
	// 난간은 거기 닿는 계단 자리에서 끊긴다. 쭉 이어지면
	// 계단 끝이 막힌 난간에 부딪힌다.
	auto AddBridgeRailing = [this, BridgeY](float EdgeOffsetY, float GapCentreX)
	{
		const float RailY = BridgeY + EdgeOffsetY;
		AddRailingRun(FVector(-1600.0f, RailY, 0.0f), FVector(GapCentreX - 230.0f, RailY, 0.0f), 350.0f);
		AddRailingRun(FVector(GapCentreX + 230.0f, RailY, 0.0f), FVector(1600.0f, RailY, 0.0f), 350.0f);
	};
	AddBridgeRailing(-230.0f, LeftStairX);
	AddBridgeRailing(230.0f, RightStairX);

	// 두 계단은 다리 바닥과 수직으로 놓이고 끝이 바닥 가장자리에 딱 맞는다.
	// 왼쪽은 -Y 에서, 오른쪽은 +Y 에서 올라간다. 예전엔 건물 순찰 Y 값을 같이 써서
	// 꼭대기가 다리에서 6 m, 22 m 떨어진 허공에서 끝났다
	// - 위층은 이어져 보였지만 실제로는 아무도 못 올라갔다.
	const float StairTopOffset = GetStairTopOffsetCm();
	AddStair(FVector(LeftStairX, BridgeY - 220.0f - StairTopOffset, 20.0f), 90.0f, 330.0f);
	AddStair(FVector(RightStairX, BridgeY + 220.0f + StairTopOffset, 20.0f), -90.0f, 330.0f);

	// 다리 양 끝에 완성된 위층 통제실이 있다. 안쪽 벽에 넓은 문 틈이 있어서
	// 다리가 장식용 통로가 아니라 실제로 다닐 수 있는 두 번째 교전 고리가 된다.
	// 지붕 실루엣은 하늘에서 볼 때 똑같은 두 건물 덩어리를
	// 구분해 주는 역할도 한다.
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

	// 1층 칸막이 벽 줄이 건물마다 뒤질 수 있는 짧은 칸을 만들면서,
	// 남북 돌격 통로와 양옆 우회로는 열어 둔다.
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
	// 천장 배관 줄이 건물이 사용 중인 것처럼 보이게 하고 눈길을 다리 쪽으로 이끈다.
	// 캐릭터 캡슐보다 위에 있고 출입구를 가로지르지 않는다.
	FactoryPipes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-2100.0f, -350.0f, 290.0f), FVector(1.35f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator::ZeroRotator, FVector(-2100.0f, 850.0f, 290.0f), FVector(1.10f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(2100.0f, 350.0f, 290.0f), FVector(1.35f, 1.0f, 1.0f)));
	FactoryPipes->AddInstance(FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(2100.0f, -850.0f, 290.0f), FVector(1.10f, 1.0f, 1.0f)));

	// 시드로 정한 엄폐 공간이 남북 주 통로에서 벗어난 짧은 교전을 만든다.
	// 기준점은 가운데 트인 통로와 입구 밖에 둔다.
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
	// 고른 쪽이 두 번째 뚫린 곳이 되고 반대쪽은 막힌 채 남아서,
	// 모든 캠프가 알아보기 쉬운 강한 쪽과 우회로를 갖는다.
	AddWallRun(FVector(-1800, -1700, 0), FVector(-1800, 1700, 0), 150, DeckX < 0.0f, false);
	AddWallRun(FVector(1800, -1700, 0), FVector(1800, 1700, 0), 150, DeckX > 0.0f, false);
	AddContainer(FVector(-SignX * 900.0f, SignY * 700.0f, 20.0f), 90.0f);
	AddContainer(FVector(SignX * 850.0f, -SignY * 750.0f, 20.0f), Variant % 2 == 0 ? 90.0f : 0.0f);
	AddCover(FVector(-SignX * 250.0f, -SignY * 200.0f, 20.0f), FVector(0.68f), Variant * 90.0f);
	AddCover(FVector(SignX * 1050.0f, SignY * 50.0f, 20.0f), FVector(0.52f), Variant % 2 ? 90.0f : 0.0f);
	AddCover(FVector(-SignX * 1150.0f, -SignY * 900.0f, 20.0f), FVector(0.48f), Variant % 2 ? 0.0f : 90.0f);
	// 높인 관측대 하나가 2x2 POI 에 세로 랜드마크와 다른 사격 각도를 준다.
	// 차지하는 모서리는 LayoutVariant 에 따라 바뀐다.
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
	// 캠프 판(윗면 Z=20) 위에 서서 관측대 표면(윗면 Z=300)까지 올라간다.
	AddStair(FVector(DeckX - SignX * 580.0f, DeckY, 20.0f), SignX > 0.0f ? 0.0f : 180.0f, 280.0f);
	// 반대 모서리에는 방이 실제로 나뉘고 작업대가 있는 작은 작전 오두막이 있다.
	// 그래서 이 2x2 가 울타리 친 소품 마당이 아니라
	// 실내/실외 교전을 고를 수 있는 POI 가 된다.
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
	// 판 하나가 1040 x 860 cm 벽 영역에 딱 맞는다. 예전의 타일식 지붕은
	// 모듈 배수가 아닌 크기를 한쪽으로 치우치게 반올림해서, 한쪽 가장자리는 모자라고
	// 반대쪽엔 받침 없이 크게 튀어나온 처마가 생겼다.
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
	// 짧은 꺾인 벽 두 개가 막다른 벽장 없이 침실/창고 칸을 만든다.
	// 번갈아 비킨 위치 덕분에 길이 방향의 트인 통로가 남는다.
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
	// 끊긴 평행 벽이 긴 시야와 반복되는 건너가는 자리를 만든다.
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

	// 따로 떨어진, 들어갈 수 있는 2층 건물 세 채가 거리와 골목을 만든다.
	// 바닥은 3x3 전체가 아니라 건물마다만 깐다.
	// 그래야 높인 지형이 떠 있는 네모 기초가 아니라
	// 바깥 마을 거리로 보인다.
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
			// 손작업 상점 앞면은 딱 폭 500 cm, 높이 약 397 cm 다.
			// 조각마다 자기 줄 길이에 맞추고 340 cm 2층 바닥판에서 끝내서
			// 겹침이나 높이 충돌이 없게 한다.
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

		// 정확하고 이어진 판. 2층 바닥은 실제 계단 자리를 둘러싼 네모 네 개라서,
		// 반올림된 500 cm 타일이 빈 공간으로 튀어나오지 않는다.
		AddSlabRect(MinX, MaxX, MinY, MaxY, 0.0f);
		AddSlabRect(MinX, VoidMinX, MinY, MaxY, 340.0f);
		AddSlabRect(VoidMaxX, MaxX, MinY, MaxY, 340.0f);
		AddSlabRect(VoidMinX, VoidMaxX, MinY, VoidMinY, 340.0f);
		AddSlabRect(VoidMinX, VoidMaxX, VoidMaxY, MaxY, 340.0f);

		// 1층 방: 끊긴 가로벽이 뒤질 수 있는 방 두 개를 만들고,
		// 길 쪽 입구에서 계단까지 가운데 길을 남긴다.
		AddWallRun(
			FVector(MinX + 180.0f, Shell.Center.Y, 0.0f),
			FVector(MaxX - 180.0f, Shell.Center.Y, 0.0f),
			150.0f, true, ShellIndex % 2 == 0);
		AddWallRun(
			FVector(Shell.Center.X, MinY + 180.0f, 0.0f),
			FVector(Shell.Center.X, MaxY - 180.0f, 0.0f),
			150.0f, true, ShellIndex % 2 == 1);

		// 2층은 믿을 만한 CQB 모듈 벽을 쓴다. 예전 Fab 앞면은 중심점 때문에
		// 판들이 허공에 놓였다. 이제 받쳐진 바닥판과 벽으로
		// 2층 전체에 들어갈 수 있고 충돌도 맞다.
		AddWallRun(FVector(MinX, MinY, 0.0f), FVector(MaxX, MinY, 0.0f), 490.0f, true, true);
		AddWallRun(FVector(MaxX, MinY, 0.0f), FVector(MaxX, MaxY, 0.0f), 490.0f, false, true);
		AddWallRun(FVector(MaxX, MaxY, 0.0f), FVector(MinX, MaxY, 0.0f), 490.0f, false, true);
		AddWallRun(FVector(MinX, MaxY, 0.0f), FVector(MinX, MinY, 0.0f), 490.0f, false, true);
		AddWallRun(
			FVector(MinX + 200.0f, Shell.Center.Y, 0.0f),
			FVector(MaxX - 200.0f, Shell.Center.Y, 0.0f),
			490.0f, true, true);
		AddRoofRect(Shell.Center, Shell.HalfSize.X, Shell.HalfSize.Y, 640.0f);

		// 2층 판은 중심점 Z=340 이라 걷는 면은 360 이다. 예전의 1.665 고정 배율은
		// 340 까지만 올라가서 꼭대기에 20 cm 턱이 남았다.
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

	// 거리 소품이 8-12 m 골목을 막지 않으면서 마을다운 느낌을 만든다.
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
	// 크기를 줄인 손작업 건물 하나가 그럴듯한 겉모습을 주고, 크레인·컨테이너·현장 사무실은
	// 그 둘레에 다닐 수 있는 공사 마당을 남긴다.
	FactoryHalls->AddInstance(FTransform(FRotator(0.0f, Variant % 2 ? 90.0f : 0.0f, 0.0f), FVector(-950.0f, -950.0f, 0.0f), FVector(0.58f)));
	FactoryCranes->AddInstance(FTransform(FRotator(0.0f, 18.0f + Variant * 17.0f, 0.0f), FVector(-1500.0f, 1150.0f, 50.0f), FVector(0.78f, 1.0f, 1.7f)));
	FactorySiteHouses->AddInstance(FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(1050.0f, 1200.0f, 0.0f), FVector(0.82f)));
	AddContainer(FVector(1250.0f, -1150.0f, 20.0f), 90.0f, FVector(0.62f));
	AddContainer(FVector(850.0f, -1150.0f, 20.0f), 90.0f, FVector(0.62f));
	AddContainer(FVector(1450.0f, 250.0f, 20.0f), 0.0f, FVector(0.58f));
	AddCover(FVector(250.0f, 1450.0f, 20.0f), FVector(0.72f), 0.0f);
	AddCover(FVector(1450.0f, 800.0f, 20.0f), FVector(0.55f), 90.0f);

	// 끊긴 둘레 울타리: 모든 면에 진짜 문이 있고, 우연히 뚫린 구멍은 없다.
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
		// 8x8 m 건물. 트인 출입구 하나와 세 면의 창문.
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
