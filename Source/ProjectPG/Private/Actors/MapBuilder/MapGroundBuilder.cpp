#include "Actors/MapBuilder/MapGroundBuilder.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapAssetSet.h"
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

using namespace MapBuild;

void UMapGroundBuilder::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapGroundBuilder::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 땅판·도로 판·호수 깔기. 칸마다 땅 종류(들판/워존/경계)에 맞는 판을 한 장씩 HISM 에 넣고,
// 도로 칸에는 연결 방향대로 아스팔트 팔을 붙이고, 호수 칸은 바닥판 + 물 한 장으로 덮는다.
void UMapGroundBuilder::BuildLightweightWorldVisuals()
{
	// 엔진의 한꺼번에 넣기 경로를 쓴다. 그래야 HISM 인스턴스가 다 들어간 뒤에야
	// 길찾기 범위를 저장한다. AddInstance 를 하나씩 부르면 중간의 잘못된 범위가
	// 동적 길찾기 시스템에 보일 수 있다.
	Map->RoadSurfaceHISM->SetCanEverAffectNavigation(false);
	Map->GroundHISM->ClearInstances();
	Map->WarZoneGroundHISM->ClearInstances();
	Map->TransitionGroundHISM->ClearInstances();
	Map->RoadSurfaceHISM->ClearInstances();
	Map->LakeBedHISM->ClearInstances();
	Map->LakeWaterHISM->ClearInstances();

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
	// 물은 칸마다 한 장이 아니라, 모든 호수·물가 칸을 감싸는 상자 위에 한 장이다.
	// 칸마다 깔았더니 칸끼리 맞닿은 곳에서 머티리얼의 물결 무늬가 칸 경계마다 새로 시작해
	// 물 위에 20 m 격자가 그려져 보였다. 상자가 마른 땅과 겹치는 곳에서는
	// 물 판이 단단한 땅판 안쪽으로 지나가므로, 실제로는 호수 부분만 그려진다.
	TArray<FTransform> LakeWaterTransforms;
	FIntPoint LakeSheetMin(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint LakeSheetMax(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	int32 LakeWaterCellCount = 0;
	GroundTransforms.Reserve(2025);
	WarZoneGroundTransforms.Reserve(256);
	TransitionGroundTransforms.Reserve(512);
	RoadTransforms.Reserve(256);
	TSet<FIntPoint> FacilityCells;
	// 모든 땅판과 시설 바닥판이 같이 쓰는 밑면 하나. 예전엔 판마다 자기 윗면 바로 밑을
	// 밑면으로 골라서, -70 cm 로 꺼진 바닥판과 이웃 칸의 -10 cm 밑면 사이에 틈이 났다:
	// 구덩이 벽에 구멍이 보였고 플레이어가 그리로 뚫고 나갈 수 있었다.
	// 가장 깊은 바닥을 한 번 구해서 쓰면 지형 전체가 하나의 단단한 덩어리가 된다.
	float TerrainSolidBottomZ = -10.0f;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		for (const FIntPoint& Cell : FacilityPlacement.OccupiedCells)
			FacilityCells.Add(Cell);
		const float FacilitySurfaceZ = FacilityPlacement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : FacilityPlacement.BaseElevationCm;
		TerrainSolidBottomZ = FMath::Min(TerrainSolidBottomZ, FacilitySurfaceZ - 60.0f);
	}
	// 호수 바닥이 세계에서 가장 깊은 곳이라, 그 둑을 이루는 땅판은 그보다 더 내려가야 한다.
	// 상자를 키워도 인스턴스 수는 늘지 않는다.
	TerrainSolidBottomZ = FMath::Min(TerrainSolidBottomZ, LakeBedZ - 40.0f);

	// 물가 메시가 있는 칸에는 평평한 땅판을 깔지 않는다: 거기선 메시가 곧 땅이고,
	// Z=20 판을 깔면 모래사장을 그대로 뚫고 지나간다.
	TSet<FIntPoint> LakeCells;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Water)
			LakeCells.Add(Placement.GridCell);
	TMap<FIntPoint, TPair<int32, int32>> ShoreTileByCell;
	if (LakeCells.Num() > 0)
		Map->BuildShoreTransitionMap(LakeCells, ShoreTileByCell);

	int32 RoadTileCount = 0;
	FIntPoint VisualWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			VisualWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		// 20x20 m 칸이 모두 땅 렌더러 하나를 같이 쓴다. 윗면은 Z=20 으로
		// ATacticalTileActor 의 소품/풀 기준 높이와 같다. 칸 크기를 딱 맞춰서
		// 이웃 윗면이 겹치지 않고 가장자리끼리 딱 붙는다.
		const int32 WarZoneDeltaX = Placement.GridCell.X - VisualWarZoneCenter.X;
		const int32 WarZoneDeltaY = Placement.GridCell.Y - VisualWarZoneCenter.Y;
		const int32 WarZoneDistanceSquared = WarZoneDeltaX * WarZoneDeltaX + WarZoneDeltaY * WarZoneDeltaY;
		const bool bLogicalWarZoneCell = Placement.Visual == ETileDesignVisual::WarZoneGround;
		const bool bIndustrialCoreGround = bLogicalWarZoneCell && WarZoneDistanceSquared <= 64;
		// SpawnRuntimeBlueprintTiles 의 WarZone_Mid 타일 띠와 맞춘 값이다. 그쪽은 같은 중심 칸에서
		// d^2 <= 225 까지 간다. 예전에 땅은 144 에서 멈춰서, 반지름 12~15 칸은 공업용 컨테이너/공장
		// 타일인데 초록 자연 땅 위에 서 있었다 - WarZone 이 자기 가장자리에 닿기도 전에 눈에 띄게
		// 끊겨 보였다. 225 밖의 자연 땅은 의도한 것이다: 그게 WarZone_Outer 자연 완충 띠다.
		const bool bTransitionGround = bLogicalWarZoneCell
			&& WarZoneDistanceSquared > 64
			&& WarZoneDistanceSquared <= 225;
		UHierarchicalInstancedStaticMeshComponent* GroundComponent = bIndustrialCoreGround
			? Map->WarZoneGroundHISM
			: (bTransitionGround ? Map->TransitionGroundHISM : Map->GroundHISM);
		TArray<FTransform>& TargetGroundTransforms = bIndustrialCoreGround
			? WarZoneGroundTransforms
			: (bTransitionGround ? TransitionGroundTransforms : GroundTransforms);
		// 네 모서리가 다 젖은 땅 칸은 물에 잠긴 것이다: 걷는 높이가 아니라 호수 바닥에 속한다.
		// 이게 없으면 양쪽 처리에서 다 빠져서 물가 한가운데 Z=20 평판이 혼자 남았다.
		const TPair<int32, int32>* ShoreEntry = ShoreTileByCell.Find(Placement.GridCell);
		const bool bSubmergedCell = ShoreEntry != nullptr
			&& ShoreEntry->Key == SubmergedShoreVariant;
		if (Placement.Visual == ETileDesignVisual::Water || bSubmergedCell)
		{
			// 평평한 칸 판 대신 꺼진 바닥 + 같이 쓰는 물 판.
			// LakeSurfaceZ 와 이웃 땅 윗면 사이의 둑이 호수를 못 건너게 만드는 것이므로,
			// 여기서는 아무것도 공통 높이까지 올라오지 않는다.
			// 호수 바닥부터 공통 밑면까지 꽉 채운다. 예전 높이는
			// Max(20, LakeBedZ - TerrainSolidBottomZ) 였는데, 바닥이 밑면보다 아래면 이 식이 음수가 되어
			// 20 cm 판으로 잘려 물속에 떠 있었고 그 위로 속이 비쳐 보이는 띠가 생겼다.
			LakeBedTransforms.Add(MakeCubeTransform(
				Map->LakeBedHISM,
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
			// 이제 물가 메시는 모래사장 표면만이고, 그 아래 단단한 부분은 물 칸과 같은
			// 이 바닥판이다. 윗면은 젖은 모서리에서 모래사장과 딱 만나고
			// 나머지 곳에서는 모래사장 아래에 있다.
			LakeBedTransforms.Add(MakeCubeTransform(
				Map->LakeBedHISM,
				Placement.WorldLocation + FVector(0.0f, 0.0f, (LakeBedZ + TerrainSolidBottomZ) * 0.5f),
				FVector(DesignCellSize, DesignCellSize,
					FMath::Max(40.0f, LakeBedZ - TerrainSolidBottomZ))));
			// 모래사장은 젖은 모서리 근처에서 물 아래로 내려가므로, 같이 쓰는 물 판이 물가 칸까지
			// 덮어야 한다 - 안 그러면 모래사장마다 호수 옆에 물 없는 올리브색 웅덩이가 생겼다.
			LakeSheetMin = FIntPoint(
				FMath::Min(LakeSheetMin.X, Placement.GridCell.X),
				FMath::Min(LakeSheetMin.Y, Placement.GridCell.Y));
			LakeSheetMax = FIntPoint(
				FMath::Max(LakeSheetMax.X, Placement.GridCell.X),
				FMath::Max(LakeSheetMax.Y, Placement.GridCell.Y));
		}

		const float AuthoredElevation = Map->GetSurfaceElevationForCell(Placement.GridCell);
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
		// 예전 도로판은 두께 2 cm 로 공통 지형보다 1 cm 위에 있었다. 그러면 아랫면이 땅 윗면과
		// 딱 같은 높이가 되고 걷는 면은 겨우 2 cm 위다. 가까이선 깊이 버퍼가 구분하지만
		// 900 m 맵 전체에선 못 해서 둘이 깜빡였다 - 겹친 면 검사에서 간격 0.00 cm 인 칸 쌍이
		// 174 개 나왔다. 그래서 걷는 면을 연석처럼 10 cm 올리고(45 cm 턱 높이보다 한참 낮다),
		// 아랫면은 땅 상자 안 20 cm 깊이에 묻어 어떤 면과도 같은 높이가 되지 않게 한다.
		const FVector Center = Placement.WorldLocation
			+ FVector(0.0f, 0.0f, SurfaceZ + RoadSurfaceLiftCm - RoadSurfaceThicknessCm * 0.5f);
		RoadTransforms.Add(MakeCubeTransform(
			Map->RoadSurfaceHISM, Center, FVector(600.0f, 600.0f, RoadSurfaceThicknessCm)));
		auto AddRoadArm = [this, &Placement, &RoadTransforms, &MakeOrientedCubeTransform](
			const FIntPoint& Direction, uint8 ConnectionBit)
		{
			if ((Placement.ConnectionMask & ConnectionBit) == 0)
				return;
			const float ThisSurfaceZ = Map->GetSurfaceElevationForCell(Placement.GridCell);
			const float NeighbourSurfaceZ = Map->GetSurfaceElevationForCell(Placement.GridCell + Direction);
			const FVector Direction3D(static_cast<float>(Direction.X), static_cast<float>(Direction.Y), 0.0f);
			// 갈래 부분도 가운데 판과 같은 높이를 써서, 도로 표면 전체가
			// 올린 높이에서 끊김 없는 한 면으로 이어진다.
			const float ArmCenterOffsetZ = RoadSurfaceLiftCm - RoadSurfaceThicknessCm * 0.5f;
			const FVector Start = Placement.WorldLocation + Direction3D * 300.0f
				+ FVector(0.0f, 0.0f, ThisSurfaceZ + ArmCenterOffsetZ);
			const FVector End = Placement.WorldLocation + Direction3D * 1000.0f
				+ FVector(0.0f, 0.0f, (ThisSurfaceZ + NeighbourSurfaceZ) * 0.5f + ArmCenterOffsetZ);
			const FVector Delta = End - Start;
			RoadTransforms.Add(MakeOrientedCubeTransform(
				Map->RoadSurfaceHISM,
				(Start + End) * 0.5f,
				Delta.Rotation(),
				FVector(Delta.Size() + 2.0f, 600.0f, RoadSurfaceThicknessCm)));
		};
		AddRoadArm(FIntPoint(0, 1), NorthConnection);
		AddRoadArm(FIntPoint(1, 0), EastConnection);
		AddRoadArm(FIntPoint(0, -1), SouthConnection);
		AddRoadArm(FIntPoint(-1, 0), WestConnection);
	}

	// 여러 칸짜리 시설마다 2x1, 2x2, 3x3(또는 회전한) 차지 칸에 딱 맞는 이어진 지형판 하나를 갖는다.
	// 안쪽 이음새가 없어지고, 건물·소품·충돌·진입로 부품이
	// 모두 같은 윗면 높이 하나를 기준으로 삼는다.
	int32 RaisedPadCount = 0;
	int32 LoweredPadCount = 0;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		if (FacilityPlacement.OccupiedCells.IsEmpty())
			continue;
		// 자기 깎은 땅이 있는 시설에는 평평한 바닥판을 깔지 않는다: 바닥판이 기준 높이에서
		// 지형 메시를 그대로 잘라 그 아래가 다 가려진다
		// - 시골 디오라마라면 물가, 물, 보트 두 척이 다 가려진다.
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
				? Map->WarZoneGroundHISM : Map->GroundHISM;
		TArray<FTransform>& FacilityGroundTransforms = FacilityGroundComponent == Map->WarZoneGroundHISM
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
		Map->FacilityPlacements.Num(), RaisedPadCount, LoweredPadCount);

	// 물가 꾸미기. 물인지 아닌지가 칸마다 정해지므로 물가 선이 20 m 격자 따라 계단진다.
	// 얕은 물에 세운 강돌과 갈대가 그 선을 흐트러뜨린다.
	//
	// 처음엔 기울인 둑 판을 써 봤다가 뺐다: 27 m 짜리 상자를 돌리면 먼 쪽 끝이 Z=20 땅보다
	// 3 m 넘게 떠서, 물가가 아니라 땅에서 판이 삐죽 튀어나와 보였다.
	// 진짜 곡선 물가는 칸보다 잘게 나눈 모양이 필요하다
	// - 돌린 상자가 아니라 SM_Terrain_Mound_2x2 같은 손작업 물가 메시.
	Map->ShoreRockHISM->ClearInstances();
	Map->ShoreReedHISM->ClearInstances();
	TArray<FTransform> ShoreRockTransforms;
	TArray<FTransform> ShoreReedTransforms;
	{
		const int64 ShoreRaidSeed = Map->GetRaidSeed();
		TSet<FIntPoint> WaterCells;
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
			if (Placement.Visual == ETileDesignVisual::Water)
				WaterCells.Add(Placement.GridCell);

		const FIntPoint ShoreOffsets[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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
					.GetRelativeTransform(Map->ShoreRockHISM->GetComponentTransform()));
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
					.GetRelativeTransform(Map->ShoreReedHISM->GetComponentTransform()));
			}
		}
	}

	// 물가 메시를 놓는다. 로컬 원점이 이미 칸 중심에 있고 땅 쪽 면이 Z=20 이라,
	// 필요한 건 칸 위치와, 메시의 물 쪽 면이 호수를 향하게 하는
	// 90도 회전뿐이다.
	int32 ShoreTransitionCount = 0;
	for (int32 VariantIndex = 0; VariantIndex < Map->ShoreTransitionHISMs.Num(); ++VariantIndex)
	{
		UHierarchicalInstancedStaticMeshComponent* Component = Map->ShoreTransitionHISMs[VariantIndex];
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
			if (Map->ShoreTransitionHISMs.IsValidIndex(VariantIndex)
				&& VariantIndex < UE_ARRAY_COUNT(ShoreMeshAssetPaths))
			{
				if (UStaticMesh* ShoreMesh = LoadObject<UStaticMesh>(
					nullptr, ShoreMeshAssetPaths[VariantIndex]))
				{
					Component->SetStaticMesh(ShoreMesh);
					// 호숫가 비탈도 들판 땅과 같은 머티리얼(DA_MapAssets 의 NatureGroundMaterial).
					UMaterialInterface* SharedGroundMaterial = Map->GetMapAssets().NatureGroundMaterial.LoadSynchronous();
					if (IsValid(SharedGroundMaterial))
						Component->SetMaterial(0, SharedGroundMaterial);
				}
			}
		}
		if (Component->GetStaticMesh() == nullptr)
		{
			// 놓을 메시가 없다. 평판을 뺀 자리에 구멍만 남기지 말고 한 번 알린다
			// - PG.BuildShoreMeshes 를 돌릴 것.
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

	Map->GroundHISM->AddInstances(GroundTransforms, false, false, true);
	Map->ShoreRockHISM->AddInstances(ShoreRockTransforms, false, false, true);
	Map->ShoreReedHISM->AddInstances(ShoreReedTransforms, false, false, true);
	Map->WarZoneGroundHISM->AddInstances(WarZoneGroundTransforms, false, false, true);
	Map->TransitionGroundHISM->AddInstances(TransitionGroundTransforms, false, false, true);
	Map->RoadSurfaceHISM->AddInstances(RoadTransforms, false, false, false);
	if (LakeSheetMax.X >= LakeSheetMin.X && LakeSheetMax.Y >= LakeSheetMin.Y)
	{
		LakeWaterTransforms.Add(MakeCubeTransform(
			Map->LakeWaterHISM,
			FVector(
				(LakeSheetMin.X + LakeSheetMax.X) * 0.5f * DesignCellSize,
				(LakeSheetMin.Y + LakeSheetMax.Y) * 0.5f * DesignCellSize,
				LakeSurfaceZ - 5.0f),
			FVector(
				(LakeSheetMax.X - LakeSheetMin.X + 1) * DesignCellSize,
				(LakeSheetMax.Y - LakeSheetMin.Y + 1) * DesignCellSize,
				10.0f)));
	}
	Map->LakeBedHISM->AddInstances(LakeBedTransforms, false, false, true);
	Map->LakeWaterHISM->AddInstances(LakeWaterTransforms, false, false, false);

	Map->GroundHISM->BuildTreeIfOutdated(false, true);
	Map->WarZoneGroundHISM->BuildTreeIfOutdated(false, true);
	Map->TransitionGroundHISM->BuildTreeIfOutdated(false, true);
	Map->RoadSurfaceHISM->BuildTreeIfOutdated(false, true);
	Map->LakeBedHISM->BuildTreeIfOutdated(false, true);
	Map->LakeWaterHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border lake: cells=%d shore_cells=%d shore_meshes=%d shore_rocks=%d shore_reeds=%d ")
		TEXT("surface_z=%.0f bed_z=%.0f bank_drop_cm=%.0f"),
		LakeWaterCellCount, ShoreRockTransforms.Num() / 5, ShoreTransitionCount,
		ShoreRockTransforms.Num(), ShoreReedTransforms.Num(),
		LakeSurfaceZ, LakeBedZ, BaseGroundSurfaceZ - LakeSurfaceZ);
	Map->GroundHISM->RecreatePhysicsState();
	Map->WarZoneGroundHISM->RecreatePhysicsState();
	Map->TransitionGroundHISM->RecreatePhysicsState();
	Map->RoadSurfaceHISM->RecreatePhysicsState();

	UE_LOG(LogTemp, Display,
		TEXT("HISM design world: ground_instances=%d warzone_ground_instances=%d transition_ground_instances=%d road_tiles=%d road_surface_instances=%d collision=true facility_support_cells=%d"),
		Map->GroundHISM->GetInstanceCount(), Map->WarZoneGroundHISM->GetInstanceCount(), Map->TransitionGroundHISM->GetInstanceCount(), RoadTileCount, Map->RoadSurfaceHISM->GetInstanceCount(),
		2025 - Map->TileDesignPlacements.Num());
}

// 맵 바깥 산 두르기. 시드로 정한 간격·크기로 산 메시를 몇 겹의 원으로 놓는다.
void UMapGroundBuilder::BuildBorderMountains()
{
	if (!IsValid(Map->MountainHISM) || Map->MountainHISM->GetStaticMesh() == nullptr)
		return;
	Map->MountainHISM->ClearInstances();
	if (!Map->bBuildBorderMountains || Map->TileDesignPlacements.IsEmpty())
		return;

	FIntPoint MinCell(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
	FIntPoint MaxCell(TNumericLimits<int32>::Min(), TNumericLimits<int32>::Min());
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
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

	// 팩의 배경 산 메시로 엇갈린 고리 두 겹을 만든다. 숫자는 Downtown_West 데모가
	// 자기 지평선을 꾸민 방식을 따랐다: 중심에서 550 m 이상, 크기 0.4-1.5,
	// 15-34 m 묻어서 능선만 골짜기 바닥 위로 솟게 한다. 안쪽 고리가 윤곽을 만들고,
	// 더 듬성하고 큰 바깥 고리가 산줄기에 깊이를 줘서
	// 언덕 울타리처럼 보이지 않게 한다.
	const int64 RaidSeed = Map->GetRaidSeed();
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
				.GetRelativeTransform(Map->MountainHISM->GetComponentTransform()));
		}
	}
	Map->MountainHISM->AddInstances(MountainTransforms, false, false, true);
	Map->MountainHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display,
		TEXT("Border mountains: instances=%d inner_radius_cm=%.0f outer_radius_cm=%.0f grid_half_span_cm=%.0f"),
		MountainTransforms.Num(), Rings[0].Radius, Rings[1].Radius, GridHalfSpanCm);
}

// 풀·덤불 뿌리기. 들판·덤불·폐허 칸 위에 PCG 그래프로 풀 덩어리를 뿌린다(시설 주변은 비운다).
void UMapGroundBuilder::BuildPCGDressingGraph()
{
	if (!IsValid(Map->DressingPCGComponent))
		return;

	// 풀·덤불 메시는 DA_MapAssets 에서 고른다(풀 2종 이상, 덤불 2종 이상 섞어야 도장 찍은 것처럼 안 보인다).
	const TArray<TSoftObjectPtr<UStaticMesh>>& GrassMeshes = Map->GetMapAssets().GrassMeshes;
	const TArray<TSoftObjectPtr<UStaticMesh>>& ShrubMeshes = Map->GetMapAssets().ShrubMeshes;

	Map->RuntimeDressingGraph = NewObject<UPCGGraph>(Map->DressingPCGComponent, TEXT("RuntimeDressingGraph"), RF_Transient);
	UPCGCreatePointsSettings* CreatePointsSettings = nullptr;
	UPCGStaticMeshSpawnerSettings* SpawnerSettings = nullptr;
	UPCGNode* CreatePointsNode = Map->RuntimeDressingGraph->AddNodeOfType<UPCGCreatePointsSettings>(CreatePointsSettings);
	UPCGNode* SpawnerNode = Map->RuntimeDressingGraph->AddNodeOfType<UPCGStaticMeshSpawnerSettings>(SpawnerSettings);
	if (!IsValid(CreatePointsNode) || !IsValid(SpawnerNode) || !IsValid(CreatePointsSettings) || !IsValid(SpawnerSettings))
		return;

	CreatePointsSettings->CoordinateSpace = EPCGCoordinateSpace::World;
	CreatePointsSettings->bCullPointsOutsideVolume = false;
	FRandomStream RandomStream(Map->DressingPCGComponent->Seed);
	int32 CandidateCellCount = 0;
	int32 MeadowCellCount = 0;
	int32 ScrubCellCount = 0;
	// 시설 자리에는 따로 올리거나 내린 지형판과 진입 경사로가 있는데, 이 단계는 그걸 모른다
	// (TileDesignPlacements 에는 높이 조정 전 평평한 Z 만 있다). 시설 자리 한 칸 바깥에
	// 놓인 풀 덩어리가 그 판 위에 뜨거나 파묻힐 수 있으므로,
	// 높이를 짐작하지 말고 시설마다 둘레 한 칸을 비워 둔다.
	auto IsNearFacilityFootprint = [this](const FIntPoint& Cell)
	{
		for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
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
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		if (IsNearFacilityFootprint(Placement.GridCell))
		{
			++FacilityAdjacentSkipCount;
			continue;
		}

		// 풀밭용 꾸미기만 한다. 숲/바위/매복/정비 캠프 타일은 ATacticalTileActor 에서
		// 자기 HISM 꾸미기를 직접 갖는다. 이 단계는 그 사이에서 맨땅이 반복돼 보이는
		// 열린 풀밭, 덤불, 폐허, 공터 타일을 채운다. 칸 나누기 값(cell divisor)은 해당 칸 중
		// 몇 칸에 덩어리를 놓을지, 덩어리 수(clump count)는 고른 칸 하나에 몇 개를 놓을지 정한다.
		// 둘을 합쳐 레벨 디자인 계획의 "풀밭은 꽉 차 보이고, 덤불 지대는 듬성듬성" 밀도 차이를
		// 흉내 낸다. SpawnRuntimeBlueprintTiles 가 자기 HISM 밀도용으로 쓰는
		// WarZone 띠 판정 로직을 복사하지 않기 위해서다.
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
	// 풀보다 비중을 낮게 준다(2 x 비중1 대 3 x 비중3). 그래야 덤불이 땅의 절반이 아니라
	// 가끔 보이는 포인트로 보인다.
	for (const TSoftObjectPtr<UStaticMesh>& ShrubMesh : ShrubMeshes)
	{
		FPCGMeshSelectorWeightedEntry& Entry = MeshSelector->MeshEntries.Emplace_GetRef(ShrubMesh, 1);
		Entry.Descriptor.BodyInstance.SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
		Entry.Descriptor.bCanEverAffectNavigation = false;
		Entry.Descriptor.InstanceStartCullDistance = 6000;
		Entry.Descriptor.InstanceEndCullDistance = 22000;
		Entry.Descriptor.ComponentTags.Add(TEXT("PCG_Dressing"));
	}

	Map->RuntimeDressingGraph->AddLabeledEdge(
		CreatePointsNode,
		PCGPinConstants::DefaultOutputLabel,
		SpawnerNode,
		PCGPinConstants::DefaultInputLabel);
	Map->RuntimeDressingGraph->AddLabeledEdge(
		SpawnerNode,
		PCGPinConstants::DefaultOutputLabel,
		Map->RuntimeDressingGraph->GetOutputNode(),
		PCGPinConstants::DefaultOutputLabel);

	Map->DressingPCGComponent->SetGraph(Map->RuntimeDressingGraph);
	Map->DressingPCGComponent->GenerateLocal(true);
	UE_LOG(LogTemp, Display,
		TEXT("PCG dressing requested: seed=%d candidate_cells=%d (meadow=%d scrub=%d) facility_skipped=%d points=%d meshes=%d collision=false navigation=false"),
		Map->DressingPCGComponent->Seed,
		CandidateCellCount,
		MeadowCellCount,
		ScrubCellCount,
		FacilityAdjacentSkipCount,
		CreatePointsSettings->PointsToCreate.Num(),
		GrassMeshes.Num() + ShrubMeshes.Num());
}

