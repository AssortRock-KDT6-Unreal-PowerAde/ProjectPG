#include "Actors/MapBuilder/MapTilePlanner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapRoadPlanner.h"
#include "Actors/MapBuilder/MapSpawnRegionPlanner.h"
#include "Actors/MapTile.h"
#include "GameModes/GameModePG.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/World.h"

using namespace MapBuild;

void UMapTilePlanner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapTilePlanner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 순서: 1) 언덕 자리 2) 맵 가장자리·시드 3) 흙길(흙길 담당) 4) 칸마다 모양 5) 지문 6) 길 연결 확인.
// 언덕과 건물이 먼저 칸을 차지해야 흙길이 그 칸을 피해 가고, 흙길이 정해져야 칸 모양(길 타일 종류)이 정해진다.
void UMapTilePlanner::BuildTileDesignPlacements(
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	Map->TileDesignPlacements.Reset();
	TSet<FIntPoint> ReservedCells;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		for (const FIntPoint& OccupiedCell : FacilityPlacement.OccupiedCells)
			ReservedCells.Add(OccupiedCell);
	}

	TMap<ETileDesignVisual, int32> Counts;
	int32 InvalidRotationCount = 0;
	TArray<FIntPoint> SortedCells;
	TileByCell.GetKeys(SortedCells);
	SortedCells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	});

	// 깎은 땅 모양은 시설과 똑같이 칸 단위로 자리를 차지한다: 예약된 칸에는 평판도
	// 타일 액터도 없고 땅 모양 메시가 그 자리를 채운다. 둘레 한 칸까지 자연 칸인
	// 평범한 자연 칸만 쓸 수 있어서, 땅 모양은 도로·시작·탈출·WarZone·시설 자리와
	// 절대 붙지 않는다 - 그 타일들의 소품은 모두 평평한 땅을 전제로 해서 떠 버린다.
	// 1) 언덕: 들판·워존 바깥쪽에 흙 언덕을 최대 28개 솟게 하고 그 위에 돌·나무·덤불을 뿌린다.
	//    언덕이 차지한 칸은 "예약됨"(ReservedCells)이 돼서 평평한 타일을 안 깐다.
	for (UHierarchicalInstancedStaticMeshComponent* FeatureComponent : Map->TerrainFeatureHISMs)
	{
		if (IsValid(FeatureComponent))
			FeatureComponent->ClearInstances();
	}
	for (UHierarchicalInstancedStaticMeshComponent* DressingComponent :
		{ Map->TerrainRockHISM.Get(), Map->TerrainTreeHISM.Get(), Map->TerrainBushHISM.Get() })
	{
		if (IsValid(DressingComponent))
			DressingComponent->ClearInstances();
	}
	const TArray<FTerrainFeatureMesh>& FeatureMeshes = GetTerrainFeatureMeshes();
	auto GetCellType = [&TileByCell](const FIntPoint& Cell, ETileType& OutType)
	{
		AMapTile* const* TilePtr = TileByCell.Find(Cell);
		if (TilePtr == nullptr || !IsValid(*TilePtr))
			return false;
		OutType = (*TilePtr)->GetType();
		return true;
	};
	// 땅·타일 띠가 쓰는 것과 같은 중심. 그래야 땅 모양의 머티리얼이
	// 주변 평평한 칸과 항상 같다.
	FIntPoint FeatureWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			FeatureWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	const int64 FeatureSeed = Map->GetRaidSeed();
	int32 TerrainFeatureCount = 0;
	for (const FIntPoint& FeatureAnchor : SortedCells)
	{
		if (TerrainFeatureCount >= Map->MaxTerrainFeatureCount || FeatureMeshes.IsEmpty())
			break;

		const uint32 FeatureHash = HashCombine(
			GetTypeHash(FeatureSeed),
			HashCombine(GetTypeHash(FeatureAnchor.X), GetTypeHash(FeatureAnchor.Y)));
		if (FeatureHash % 13 != 0)
			continue;

		const int32 MeshIndex = static_cast<int32>((FeatureHash >> 8) % static_cast<uint32>(FeatureMeshes.Num()));
		const FIntPoint Footprint = FeatureMeshes[MeshIndex].Footprint;

		// 자연 칸과 WarZone 칸 둘 다 쓸 수 있지만, 한 땅 모양이 둘에 걸치지는 않는다:
		// 둘레 포함 닿는 모든 칸이 기준 칸과 같은 타입이어야 한다.
		ETileType AnchorType = ETileType::None;
		if (!GetCellType(FeatureAnchor, AnchorType)
			|| (AnchorType != ETileType::None && AnchorType != ETileType::WarZone))
		{
			continue;
		}

		// 공업 중심부는 평평하게 둔다: 중앙 시설, 그 진입로, 가장 촘촘한 엄폐물이 있는 곳이다.
		const int32 CoreDeltaX = FeatureAnchor.X - FeatureWarZoneCenter.X;
		const int32 CoreDeltaY = FeatureAnchor.Y - FeatureWarZoneCenter.Y;
		const int32 CoreDistanceSquared = CoreDeltaX * CoreDeltaX + CoreDeltaY * CoreDeltaY;
		if (AnchorType == ETileType::WarZone && CoreDistanceSquared <= 64)
			continue;

		bool bFits = true;
		for (int32 OffsetX = -1; OffsetX <= Footprint.X && bFits; ++OffsetX)
		{
			for (int32 OffsetY = -1; OffsetY <= Footprint.Y && bFits; ++OffsetY)
			{
				const FIntPoint Cell(FeatureAnchor.X + OffsetX, FeatureAnchor.Y + OffsetY);
				ETileType CellType = ETileType::None;
				bFits = GetCellType(Cell, CellType) && CellType == AnchorType
					&& !ReservedCells.Contains(Cell);
			}
		}
		if (!bFits)
			continue;

		for (int32 OffsetX = 0; OffsetX < Footprint.X; ++OffsetX)
		{
			for (int32 OffsetY = 0; OffsetY < Footprint.Y; ++OffsetY)
				ReservedCells.Add(FIntPoint(FeatureAnchor.X + OffsetX, FeatureAnchor.Y + OffsetY));
		}

		// 손작업 둘레는 로컬 Z=0 이고 공통 걷는 면은 Z=20 이므로,
		// 중심을 거기 두면 이음새가 딱 맞는다.
		const FTerrainFeatureMesh& FeatureMesh = FeatureMeshes[MeshIndex];
		const FVector FeatureCenter(
			(FeatureAnchor.X + (Footprint.X - 1) * 0.5f) * DesignCellSize,
			(FeatureAnchor.Y + (Footprint.Y - 1) * 0.5f) * DesignCellSize,
			20.0f);
		// 띠 판정은 BuildLightweightWorldVisuals 와 같다: d^2=225 안의 WarZone 칸은
		// 전환용 흙 위에 있고, 그보다 바깥은 자연 땅을 같이 쓴다.
		const int32 BandIndex = (AnchorType == ETileType::WarZone && CoreDistanceSquared <= 225) ? 1 : 0;
		const int32 ComponentIndex = BandIndex * FeatureMeshes.Num() + MeshIndex;
		UHierarchicalInstancedStaticMeshComponent* FeatureComponent =
			Map->TerrainFeatureHISMs.IsValidIndex(ComponentIndex) ? Map->TerrainFeatureHISMs[ComponentIndex] : nullptr;
		if (IsValid(FeatureComponent))
		{
			FeatureComponent->AddInstance(
				FTransform(FRotator::ZeroRotator, FeatureCenter, FVector::OneVector), true);
		}

		// 메시를 만들 때 쓴 높이 공식 그대로 비탈을 꾸민다. 이 칸에는 타일 액터가 없어서,
		// 이게 없으면 땅 모양이 맨 흙덩이로만 보인다.
		FRandomStream DressingStream(static_cast<int32>(FeatureHash));
		auto ScatterOnFeature = [&](
			UHierarchicalInstancedStaticMeshComponent* Component,
			int32 Count, float MinScale, float MaxScale, float SinkCm)
		{
			if (!IsValid(Component))
				return;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				// 바깥 1/8 은 피해서 평평한 이음새에 걸치는 것이 없게 한다.
				const float U = DressingStream.FRandRange(0.12f, 0.88f);
				const float V = DressingStream.FRandRange(0.12f, 0.88f);
				const FVector Location = FeatureCenter + FVector(
					(U - 0.5f) * Footprint.X * DesignCellSize,
					(V - 0.5f) * Footprint.Y * DesignCellSize,
					SampleTerrainFeatureHeight(FeatureMesh, U, V) - SinkCm);
				Component->AddInstance(FTransform(
					FRotator(0.0f, DressingStream.FRandRange(0.0f, 360.0f), 0.0f),
					Location,
					FVector(DressingStream.FRandRange(MinScale, MaxScale))), true);
			}
		};
		// 움푹한 곳은 아늑한 덤불숲, 솟은 곳은 드러난 바위 꼭대기로 보이게 한다.
		// WarZone 땅에서는 소나무를 아예 뺀다 - 공업 마당 안의 숲은
		// 이 띠 나누기가 피하려는 바로 그런 이음새다.
		const bool bHollow = FeatureMesh.Amplitude < 0.0f;
		const bool bIndustrial = AnchorType == ETileType::WarZone;
		ScatterOnFeature(Map->TerrainRockHISM,
			bIndustrial ? (bHollow ? 5 : 8) : (bHollow ? 3 : 6), 0.55f, 1.25f, 18.0f);
		ScatterOnFeature(Map->TerrainTreeHISM,
			bIndustrial ? 0 : (bHollow ? 5 : 3), 0.85f, 1.35f, 12.0f);
		ScatterOnFeature(Map->TerrainBushHISM,
			bIndustrial ? 4 : (bHollow ? 11 : 8), 0.70f, 1.30f, 6.0f);
		++TerrainFeatureCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Terrain features: placed=%d kinds=%d"),
		TerrainFeatureCount, FeatureMeshes.Num());
	// 2) 맵 가장자리 칸 번호(MinCell~MaxCell)와 판 시드. 가장자리 숲·호수 위치에 쓴다.
	FIntPoint MinCell = SortedCells.Num() > 0 ? SortedCells[0] : FIntPoint::ZeroValue;
	FIntPoint MaxCell = MinCell;
	for (const FIntPoint& Cell : SortedCells)
	{
		MinCell.X = FMath::Min(MinCell.X, Cell.X);
		MinCell.Y = FMath::Min(MinCell.Y, Cell.Y);
		MaxCell.X = FMath::Max(MaxCell.X, Cell.X);
		MaxCell.Y = FMath::Max(MaxCell.Y, Cell.Y);
	}
	const int64 RaidSeed = Map->GetRaidSeed();

	// 흙길: 시설 입구·시작점·출구가 도로나 워존에 이어지도록 흙길 칸을 정한다(흙길 담당).
	// 결과 두 개: 흙길이 될 칸 목록, 그리고 칸마다 어느 쪽으로 이어지는지(연결 번호 N=1,E=2,S=4,W=8).
	TSet<FIntPoint> SupplementalRoadCells;
	TMap<FIntPoint, uint8> SupplementalRoadMasks;
	Map->RoadPlanner->PlanAccessRoads(
		TileByCell, SortedCells, ReservedCells, RaidSeed, SupplementalRoadCells, SupplementalRoadMasks);

	// 4) 칸마다 모양 정하기. 게임에서 보이는 결과:
	//    도로 칸 → 이웃 연결 수로 직선/꺾임/T자/십자/막다른 길 타일 + 회전,
	//    시작점 → 벽 친 대기소(입구가 워존 가는 길 쪽), 출구·장애물 → 검문소, 워존 → 워존 땅,
	//    나머지 → 호수/가장자리 숲/미로 숲 띠/풀밭·덤불·바위 등(5×5칸 단위로 묶어서 숲 띠·공터처럼 보이게).
	// 아래에서 배치를 만들면서 센다. 배치 반복문 뒤의 요약에서 같이 출력하려고 여기 선언한다.
	int32 SpawnOpeningCount = 0;
	int32 SpawnRoutedOpenings = 0;
	int32 WalledOffSpawnConnections = 0;

	// 이 단계 전체에 호수는 하나다. 모서리는 도로망을 보고 점수 매겨 고르므로
	// 칸마다 다시 구하면 안 된다.
	const FBorderLake BorderLake = GetBorderLake(
		RaidSeed, MinCell, MaxCell, Map->BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	Map->BorderLakeCentreCell = BorderLake.CentreCell;
	Map->bHasBorderLake = Map->BorderLakeRadiusCells > 0.0f;

	for (const FIntPoint& Cell : SortedCells)
	{
		const AMapTile* const* TilePtr = TileByCell.Find(Cell);
		const AMapTile* Tile = TilePtr != nullptr ? *TilePtr : nullptr;
		if (!IsValid(Tile) || ReservedCells.Contains(Cell))
			continue;
		const ETileType Type = Tile->GetType();
		const bool bSupplementalRoad = SupplementalRoadCells.Contains(Cell)
			&& Type != ETileType::Road
			&& Type != ETileType::Spawn
			&& Type != ETileType::Exit
			&& Type != ETileType::WarZone
			&& Type != ETileType::Obstacle;

		auto IsAuthoritativeTraversalNeighbor = [&TileByCell, &SupplementalRoadCells](const FIntPoint& NeighborCell)
		{
			AMapTile* const* Neighbor = TileByCell.Find(NeighborCell);
			if (Neighbor == nullptr || !IsValid(*Neighbor))
				return false;
			const ETileType NeighborType = (*Neighbor)->GetType();
			const bool bNeighborBecomesAccessRoad = SupplementalRoadCells.Contains(NeighborCell)
				&& NeighborType != ETileType::Road
				&& NeighborType != ETileType::Spawn
				&& NeighborType != ETileType::Exit
				&& NeighborType != ETileType::WarZone
				&& NeighborType != ETileType::Obstacle;
			// 진입로가 원래 화면용 칸을 차지한다. 계획된 경로 마스크로만 이어질 수 있다:
			// 우연히 옆에 붙은 것은 연결 자리가 아니며, 한 줄 길을
			// 커다란 사거리로 바꾸면 안 된다.
			if (bNeighborBecomesAccessRoad)
				return false;
			return IsRoadTraversalType((*Neighbor)->GetType());
		};

		// 추가 연결로는 명시된 경로 변이 원본이다. 경로가 생성기 도로를 가로지르면
		// 명시된 변과 그 도로의 원래 이웃을 합쳐서,
		// 양쪽 모두 서로 이어지는 연결 자리를 유지하게 한다.
		uint8 ConnectionMask = SupplementalRoadMasks.FindRef(Cell);
		if (!bSupplementalRoad)
		{
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(0, 1))) ConnectionMask |= NorthConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(1, 0))) ConnectionMask |= EastConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(0, -1))) ConnectionMask |= SouthConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(-1, 0))) ConnectionMask |= WestConnection;
		}

		FTileDesignPlacement& Placement = Map->TileDesignPlacements.AddDefaulted_GetRef();
		Placement.GridCell = Cell;
		Placement.WorldLocation = FVector(
			Cell.X * DesignCellSize,
			Cell.Y * DesignCellSize,
			0.0f);
		Placement.ConnectionMask = ConnectionMask;
		Placement.bSupplementalAccessRoad = bSupplementalRoad;
		Placement.LocalSeed = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(Cell.X), GetTypeHash(Cell.Y)));
		Placement.LayoutVariant = static_cast<uint8>(Placement.LocalSeed % 4);

		if (Type == ETileType::Road || bSupplementalRoad)
		{
			const int32 ConnectionCount = FMath::CountBits(ConnectionMask);
			if (ConnectionCount >= 4)
			{
				Placement.Visual = ETileDesignVisual::RoadCross;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadCrossLevelPath);
			}
			else if (ConnectionCount == 3)
			{
				Placement.Visual = ETileDesignVisual::RoadTJunction;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadTJunctionLevelPath);
				Placement.RotationQuarterTurns = FindPositiveYawRotation(
					NorthConnection | EastConnection | WestConnection,
					ConnectionMask);
			}
			else if (ConnectionCount == 2
				&& (ConnectionMask == (EastConnection | WestConnection)
					|| ConnectionMask == (NorthConnection | SouthConnection)))
			{
				Placement.Visual = ETileDesignVisual::RoadStraight;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadStraightLevelPath);
				Placement.RotationQuarterTurns = ConnectionMask == (EastConnection | WestConnection) ? 0 : 1;
			}
			else if (ConnectionCount == 2)
			{
				Placement.Visual = ETileDesignVisual::RoadCorner;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadCornerLevelPath);
				Placement.RotationQuarterTurns = FindPositiveYawRotation(
					NorthConnection | WestConnection,
					ConnectionMask);
			}
			else
			{
				Placement.Visual = ETileDesignVisual::RoadDeadEnd;
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RoadDeadEndLevelPath);
				const uint8 TargetMask = ConnectionMask != 0 ? ConnectionMask : WestConnection;
				Placement.RotationQuarterTurns = FindPositiveYawRotation(WestConnection, TargetMask);
			}
		}
		else if (Type == ETileType::Spawn)
		{
			Placement.Visual = ETileDesignVisual::Spawn;
			Placement.VisualLevel = TSoftObjectPtr<UWorld>(SpawnLevelPath);
			// LD_Tile_Spawn_Staging 은 담으로 둘러싸인 구역이고 손작업 동쪽 면에만 출입구가 하나 있다.
			// 그래서 여기서 고르는 회전이 칸의 여러 연결 중 플레이어가 실제로 걸어 나갈 수 있는
			// 하나를 정한다. 가장 낮은 비트를 골랐더니 북+동으로 이어진 시작 지점이 북쪽을 보고
			// 동쪽 도로를 담 뒤에 막아 버렸다 - 플레이어 눈엔 WarZone 으로 가는 길이 아예 없었다.
			//
			// 시작 지점→WarZone 보장 경로가 실제로 지나간 변을 우선한다.
			// 그래야 출입구가 항상 어딘가로 이어진다고 약속된 길을 바라본다.
			const uint8 RouteMask = SupplementalRoadMasks.FindRef(Cell) & ConnectionMask;
			const uint8 PreferredMask = RouteMask != 0 ? RouteMask : ConnectionMask;
			const uint8 TargetMask = PreferredMask != 0
				? static_cast<uint8>(1 << FMath::CountTrailingZeros(PreferredMask))
				: EastConnection;
			Placement.RotationQuarterTurns = FindPositiveYawRotation(EastConnection, TargetMask);
			// 남은 연결은 이 타일의 담에 막혀 끝나는 도로다. 경로상으로는 괜찮지만
			// 어디로도 안 가는 길처럼 보이므로, 조용히 넘기지 않고 센다.
			SpawnOpeningCount += ConnectionMask != 0 ? 1 : 0;
			WalledOffSpawnConnections += FMath::Max(0, FMath::CountBits(ConnectionMask) - 1);
			SpawnRoutedOpenings += RouteMask != 0 ? 1 : 0;
		}
		else if (Type == ETileType::Exit || Type == ETileType::Obstacle)
		{
			Placement.Visual = Type == ETileType::Exit
				? ETileDesignVisual::Exit
				: ETileDesignVisual::Obstacle;
			Placement.VisualLevel = TSoftObjectPtr<UWorld>(
				Type == ETileType::Exit ? ExitLevelPath : ObstacleLevelPath);
			Placement.RotationQuarterTurns =
				(ConnectionMask & (NorthConnection | SouthConnection)) != 0
				&& (ConnectionMask & (EastConnection | WestConnection)) == 0
				? 1 : 0;
		}
		else if (Type == ETileType::WarZone)
		{
			Placement.Visual = ETileDesignVisual::WarZoneGround;
		}
		else
		{
			auto HasTraversalWithin = [&TileByCell, &SupplementalRoadCells, &Cell](int32 Radius)
			{
				for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
				{
					for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
					{
						if (FMath::Abs(OffsetX) + FMath::Abs(OffsetY) > Radius)
							continue;
						const FIntPoint CandidateCell = Cell + FIntPoint(OffsetX, OffsetY);
						if (SupplementalRoadCells.Contains(CandidateCell))
							return true;
						AMapTile* const* Candidate = TileByCell.Find(CandidateCell);
						if (Candidate != nullptr && IsValid(*Candidate) && IsRoadTraversalType((*Candidate)->GetType()))
							return true;
					}
				}
				return false;
			};

			const int32 BorderDistance = FMath::Min(
				FMath::Min(Cell.X - MinCell.X, MaxCell.X - Cell.X),
				FMath::Min(Cell.Y - MinCell.Y, MaxCell.Y - Cell.Y));
			const bool bMapBorder = BorderDistance <= 2;
			const bool bNearTraversal = HasTraversalWithin(2);
			const int32 MacroX = FMath::FloorToInt(static_cast<float>(Cell.X) / 5.0f);
			const int32 MacroY = FMath::FloorToInt(static_cast<float>(Cell.Y) / 5.0f);
			const uint32 MacroHash = HashCombine(
				GetTypeHash(RaidSeed),
				HashCombine(GetTypeHash(MacroX), GetTypeHash(MacroY)));
			const uint32 FineHash = HashCombine(
				GetTypeHash(Placement.LocalSeed),
				HashCombine(GetTypeHash(Cell.Y), GetTypeHash(Cell.X)));
			const int32 MacroRoll = MacroHash % 100;
			const int32 FineRoll = FineHash % 100;
			const int32 MazeBandValue = FMath::Abs(MacroX * 3 + MacroY * 5
				+ static_cast<int32>(GetTypeHash(RaidSeed) % 17));
			const bool bMazeBarrierBand = !bNearTraversal && !bMapBorder
				&& MazeBandValue % 9 == 0;

			// 호수가 맵 한 모서리를 파먹는다. 격자는 앞으로도 정사각형이지만 해안선까지 그럴 필요는 없다:
			// 모서리 바깥에 중심을 둔 metaball 에 칸마다 흔들림을 더하면 들쭉날쭉한 물가가 생기고,
			// 그래서 맵 경계가 그려 놓은 상자처럼 보이지 않는다. 물가용 에셋 - 시골 디오라마,
			// 부두, 보트 - 에게도 마른 들판 한가운데가 아닌 어울리는 자리가 생긴다.
			//
			// 지나갈 수 있는 칸은 물로 채우지 않고, 그 이웃 칸도 채우지 않는다.
			// 그래서 생성기가 가장자리 근처에 놓은 시작·탈출 지점은 물에 잠기지 않고
			// 거기까지 이어진 곶(반도)을 갖는다.
			if (Map->BorderLakeRadiusCells > 0.0f
				&& !bNearTraversal && Placement.Visual != ETileDesignVisual::WarZoneGround)
			{
				const float LakeRadius = BorderLake.Radius + GetLakeShoreJitter(RaidSeed, Cell);
				const float DistanceToLakeCentre =
					FVector2D::Distance(FVector2D(Cell.X, Cell.Y), BorderLake.CentreCell);
				if (DistanceToLakeCentre <= LakeRadius)
					Placement.Visual = ETileDesignVisual::Water;
			}

			// 자연 칸을 5x5 칸 큰 묶음(biome)으로 나눠서, 결과가 잡음이 아니라
			// 숲 띠, 공터, 바위 들판처럼 보이게 한다.
			// 도로 옆 칸은 전투 시 잘 보이도록 일부러 더 트이게 둔다.
			if (Placement.Visual == ETileDesignVisual::Water)
			{
				// 위에서 이미 정했다. biome 굴림이 덮어쓰면 안 된다.
			}
			else if (bMapBorder)
			{
				Placement.Visual = FineRoll < 72
					? ETileDesignVisual::NatureForestDense
					: ETileDesignVisual::NatureForestSparse;
			}
			else if (bMazeBarrierBand)
			{
				// 5칸짜리 큰 띠가 미로의 '못 지나갈 것처럼 보이는' 지형이 된다.
				// 원래 있던/생성된 지나갈 수 있는 칸과 그 둘레 2칸은 항상 빼므로,
				// 서버 쪽 연결은 바꾸지 않고 경로 모양만 다듬는다.
				Placement.Visual = FineRoll < 76
					? ETileDesignVisual::NatureForestDense
					: ETileDesignVisual::NatureRocky;
			}
			else if (bNearTraversal)
			{
				if (FineRoll < 32) Placement.Visual = ETileDesignVisual::NatureMeadow;
				else if (FineRoll < 54) Placement.Visual = ETileDesignVisual::NatureScrub;
				else if (FineRoll < 72) Placement.Visual = ETileDesignVisual::NatureForestSparse;
				else if (FineRoll < 84) Placement.Visual = ETileDesignVisual::NatureRocky;
				else if (FineRoll < 94) Placement.Visual = ETileDesignVisual::NatureDitch;
				else Placement.Visual = ETileDesignVisual::NatureServiceCamp;
			}
			else if (MacroRoll < 36)
			{
				Placement.Visual = FineRoll < 74
					? ETileDesignVisual::NatureForestDense
					: ETileDesignVisual::NatureForestSparse;
			}
			else if (MacroRoll < 61)
			{
				Placement.Visual = FineRoll < 72
					? ETileDesignVisual::NatureForestSparse
					: ETileDesignVisual::NatureAmbush;
			}
			else if (MacroRoll < 77)
			{
				Placement.Visual = FineRoll < 64
					? ETileDesignVisual::NatureMeadow
					: ETileDesignVisual::NatureScrub;
			}
			else if (MacroRoll < 88)
			{
				Placement.Visual = FineRoll < 62
					? ETileDesignVisual::NatureRocky
					: ETileDesignVisual::NatureDitch;
			}
			else if (MacroRoll < 96)
			{
				Placement.Visual = FineRoll < 60
					? ETileDesignVisual::NatureScrub
					: ETileDesignVisual::NatureAmbush;
			}
			else
			{
				Placement.Visual = FineRoll < 45
					? ETileDesignVisual::Ruins
					: ETileDesignVisual::NatureServiceCamp;
			}

			if (Placement.Visual == ETileDesignVisual::Ruins)
				Placement.VisualLevel = TSoftObjectPtr<UWorld>(RuinsLevelPath);
		}

		Counts.FindOrAdd(Placement.Visual)++;
		if (Placement.RotationQuarterTurns < 0 || Placement.RotationQuarterTurns > 3)
			++InvalidRotationCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Supplemental POI road design: cells=%d facilities=%d"),
		SupplementalRoadCells.Num(), Map->FacilityPlacements.Num());

	// 5) 지문(LayoutHash): 칸 모양 전부를 숫자 하나로. 같은 시드면 같은 숫자 → 로그로 "같은 맵" 인지 확인.
	Map->LayoutHash = 0;
	TMap<FIntPoint, const FTileDesignPlacement*> PlacementByCell;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		PlacementByCell.Add(Placement.GridCell, &Placement);
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.GridCell.X));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.GridCell.Y));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(static_cast<uint8>(Placement.Visual)));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.ConnectionMask));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.bSupplementalAccessRoad));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.LayoutVariant));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	// routed_openings 는 openings 와 같아야 한다: 모든 시작 지점의 출입구 하나가
	// WarZone 보장 경로가 실제로 계획한 도로를 바라봐야 한다. walled_connections 는
	// 아직 시작 지점 담에 막혀 끝나는 도로 수다 - 타일 출입구가 하나뿐이므로,
	// 0 보다 크면 플레이어가 보기만 하고 쓸 수 없는 길이 있다는 뜻이다.
	UE_LOG(LogTemp, Display,
		TEXT("Spawn opening alignment: spawns=%d routed_openings=%d walled_connections=%d"),
		SpawnOpeningCount, SpawnRoutedOpenings, WalledOffSpawnConnections);

	// 6) 길 연결 확인: 길 타일 회전이 연결 번호와 맞는지, 옆 칸도 나를 향해 열려 있는지 센다(0이어야 정상).
	//    게임에서: 0이 아니면 길이 벽에 막혀 끊겨 보인다.
	int32 RoadRotationMismatchCount = 0;
	int32 ReciprocalConnectionMismatchCount = 0;
	const struct FDirectionCheck
	{
		FIntPoint Offset;
		uint8 Bit;
		uint8 OppositeBit;
	} DirectionChecks[] = {
		{ FIntPoint(0, 1), NorthConnection, SouthConnection },
		{ FIntPoint(1, 0), EastConnection, WestConnection },
		{ FIntPoint(0, -1), SouthConnection, NorthConnection },
		{ FIntPoint(-1, 0), WestConnection, EastConnection }
	};
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		uint8 CanonicalMask = 0;
		const bool bTraversalPlacement =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle
			|| Placement.Visual == ETileDesignVisual::WarZoneGround;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::RoadStraight: CanonicalMask = EastConnection | WestConnection; break;
		case ETileDesignVisual::RoadCorner: CanonicalMask = NorthConnection | WestConnection; break;
		case ETileDesignVisual::RoadTJunction: CanonicalMask = NorthConnection | EastConnection | WestConnection; break;
		case ETileDesignVisual::RoadCross: CanonicalMask = NorthConnection | EastConnection | SouthConnection | WestConnection; break;
		case ETileDesignVisual::RoadDeadEnd: CanonicalMask = WestConnection; break;
		default: break;
		}
		if (CanonicalMask != 0)
		{
			uint8 RotatedMask = CanonicalMask;
			for (int32 RotationIndex = 0; RotationIndex < Placement.RotationQuarterTurns; ++RotationIndex)
				RotatedMask = RotateConnectionMaskPositiveYaw(RotatedMask);
			if (RotatedMask != Placement.ConnectionMask)
				++RoadRotationMismatchCount;
		}

		if (!bTraversalPlacement)
			continue;

		for (const FDirectionCheck& DirectionCheck : DirectionChecks)
		{
			if ((Placement.ConnectionMask & DirectionCheck.Bit) == 0)
				continue;
			const FTileDesignPlacement* const* Neighbor = PlacementByCell.Find(
				Placement.GridCell + DirectionCheck.Offset);
			if (Neighbor != nullptr
				&& ((*Neighbor)->ConnectionMask & DirectionCheck.OppositeBit) == 0)
			{
				++ReciprocalConnectionMismatchCount;
			}
		}
	}
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
	{
		Map->LayoutHash = HashCombine(Map->LayoutHash, FCrc::StrCrc32(*Placement.FacilityId.ToString()));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(static_cast<uint8>(Placement.VisualSet)));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.AnchorCell.X));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.AnchorCell.Y));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.Footprint.X));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.Footprint.Y));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		Map->LayoutHash = HashCombine(Map->LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	// 7) 시작 구역 칸을 "시작 대기소" 로 바꾼다(시작 구역 담당). 지문·길 연결 확인 뒤에 해야 한다
	//    (그 확인은 길 타일 기준이라, 대기소로 바꾼 칸을 보면 틀렸다고 센다).
	Map->SpawnRegionPlanner->DressExtraSpawnRegions(MinCell, MaxCell);

	UE_LOG(LogTemp, Display,
		TEXT("Design placement spec: cells=%d reserved=%d invalid_rotations=%d map_extent_cm=%.0f layout_hash=%08X"),
		Map->TileDesignPlacements.Num(), ReservedCells.Num(), InvalidRotationCount,
		Map->GridCellSpan * DesignCellSize, Map->LayoutHash);
	UE_LOG(LogTemp, Display,
		TEXT("Road connection validation: rotation_mismatches=%d reciprocal_mismatches=%d"),
		RoadRotationMismatchCount,
		ReciprocalConnectionMismatchCount);
	for (const TPair<ETileDesignVisual, int32>& Count : Counts)
	{
		UE_LOG(LogTemp, Display, TEXT("Design placement count: visual=%d count=%d"),
			static_cast<int32>(Count.Key), Count.Value);
	}
}

