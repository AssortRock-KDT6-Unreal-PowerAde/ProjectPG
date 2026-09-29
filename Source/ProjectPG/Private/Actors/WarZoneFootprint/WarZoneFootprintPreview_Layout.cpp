// AWarZoneFootprintPreview — 설계도 만들기 — 논리 칸(AMapTile)을 읽어 칸마다 무엇을 놓을지(FTileDesignPlacement) 정한다. 화면에는 아무것도 만들지 않는다.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 9/28 BuildTileDesignPlacements 를 단계 구조체 FPGLayoutPlanner 로 나눔 — 동작 그대로.)

#include "WarZoneFootprintPreviewInternal.h"

// ---- 설계도 만들기 단계들 (2026-09-28 BuildTileDesignPlacements 나누기) ----
// 왜: BuildTileDesignPlacements 한 함수가 1,017줄이었다(지형 덩어리 → 시설 진입로 → 호수·추가 시작 지역 → 시작·출구 길 잇기 →
//   워존 길 보장 → 칸마다 타일 고르기 → 해시·검사 → 추가 시작 지역 겉모습 → 요약 로그). 단계끼리 주고받는 값(예약 칸, 보조 길 칸·방향,
//   시드, 맵 범위, 호수)을 이 구조체에 모으고, 단계마다 함수 하나로 나눴다. 동작·순서·로그는 그대로다(같은 시드 두 개로 나누기 전·후 로그 비교).
// 왜 맵 클래스 멤버가 아니라 구조체인가: 설계도 만들기는 이 파일 안에서만 쓰는 순서다. 헤더에는 friend 한 줄만 둔다(다른 협력 객체와 같은 방식).
struct FPGLayoutPlanner
{
	AWarZoneFootprintPreview& Map;
	const TMap<FIntPoint, AMapTile*>& TileByCell;

	TSet<FIntPoint> ReservedCells;
	TMap<ETileDesignVisual, int32> Counts;
	int32 InvalidRotationCount = 0;
	TArray<FIntPoint> SortedCells;
	FIntPoint MinCell = FIntPoint::ZeroValue;
	FIntPoint MaxCell = FIntPoint::ZeroValue;
	int64 RaidSeed = 0;
	// 생성기 길 위에 설계도가 더 까는 흙길(시설 진입로·시작/출구 잇기·워존 길 보장). AMapTile 의 종류는 바꾸지 않는다.
	TSet<FIntPoint> SupplementalRoadCells;
	TMap<FIntPoint, uint8> SupplementalRoadMasks;
	FBorderLake BorderLake;
	TSet<FIntPoint> ExtraSpawnCells;
	// 칸마다 타일을 고르며 세고, 요약 로그에서 같이 찍는다.
	int32 SpawnOpeningCount = 0;
	int32 SpawnRoutedOpenings = 0;
	int32 WalledOffSpawnConnections = 0;
	int32 RoadRotationMismatchCount = 0;
	int32 ReciprocalConnectionMismatchCount = 0;

	FPGLayoutPlanner(AWarZoneFootprintPreview& InMap, const TMap<FIntPoint, AMapTile*>& InTileByCell)
		: Map(InMap), TileByCell(InTileByCell)
	{
	}

	void ReserveFacilityCells();
	void PlaceTerrainFeatures();
	void MeasureExtent();
	void AddFacilityAccessSpurs();
	void PickSpawnRegions();
	void RepairEndpointRoads();
	void GuaranteeWarZoneRoutes();
	void BuildPlacements();
	void HashAndValidate();
	void DressExtraSpawnRegions();
	void LogSummary() const;

private:
	bool GetCellType(const FIntPoint& Cell, ETileType& OutType) const;
	void ConnectSupplementalRoadCells(const FIntPoint& A, const FIntPoint& B);
	bool ConnectPathBack(const TMap<FIntPoint, FIntPoint>& ParentByCell, const FIntPoint& Found, const FIntPoint& Start, int32& OutLength);
	bool IsAuthoritativeTraversalNeighbor(const FIntPoint& NeighborCell) const;
	bool HasTraversalWithin(const FIntPoint& Cell, int32 Radius) const;
	void ApplyRoadVisual(FTileDesignPlacement& Placement, uint8 ConnectionMask) const;
	void ApplySpawnVisual(FTileDesignPlacement& Placement, const FIntPoint& Cell, uint8 ConnectionMask);
	void ApplyNatureVisual(FTileDesignPlacement& Placement, const FIntPoint& Cell) const;
	static FIntPoint GetFacilityCenterCell(const FFacilityPlacement& Facility);
};

void AWarZoneFootprintPreview::BuildTileDesignPlacements(
	const TMap<FIntPoint, AMapTile*>& TileByCell)
{
	FPGLayoutPlanner Planner(*this, TileByCell);
	Planner.ReserveFacilityCells();
	Planner.PlaceTerrainFeatures();
	Planner.MeasureExtent();
	Planner.AddFacilityAccessSpurs();
	Planner.PickSpawnRegions();
	Planner.RepairEndpointRoads();
	Planner.GuaranteeWarZoneRoutes();
	Planner.BuildPlacements();
	Planner.HashAndValidate();
	Planner.DressExtraSpawnRegions();
	Planner.LogSummary();
}

bool FPGLayoutPlanner::GetCellType(const FIntPoint& Cell, ETileType& OutType) const
{
	AMapTile* const* TilePtr = TileByCell.Find(Cell);
	if (TilePtr == nullptr || !IsValid(*TilePtr))
		return false;
	OutType = (*TilePtr)->GetType();
	return true;
}

// 시설 발자국 칸은 타일을 안 세운다. 칸은 좌표 순서로 돈다(같은 시드 = 같은 결과).
void FPGLayoutPlanner::ReserveFacilityCells()
{
	Map.TileDesignPlacements.Reset();
	for (const FFacilityPlacement& FacilityPlacement : Map.FacilityPlacements)
	{
		for (const FIntPoint& OccupiedCell : FacilityPlacement.OccupiedCells)
			ReservedCells.Add(OccupiedCell);
	}
	TileByCell.GetKeys(SortedCells);
	SortedCells.Sort([](const FIntPoint& A, const FIntPoint& B)
	{
		return A.X != B.X ? A.X < B.X : A.Y < B.Y;
	});
}

// Sculpted ground features claim whole cells exactly the way facilities do: a
// reserved cell gets no flat slab and no tile actor, and the feature mesh fills it
// instead. Only plain nature cells with a one-cell nature margin qualify, so a
// feature never borders a road, spawn, exit, WarZone or facility footprint - the
// props in those tiles all assume a flat surface and would be left floating.
void FPGLayoutPlanner::PlaceTerrainFeatures()
{
	for (UHierarchicalInstancedStaticMeshComponent* FeatureComponent : Map.TerrainFeatureHISMs)
	{
		if (IsValid(FeatureComponent))
			FeatureComponent->ClearInstances();
	}
	for (UHierarchicalInstancedStaticMeshComponent* DressingComponent :
		{ Map.TerrainRockHISM.Get(), Map.TerrainTreeHISM.Get(), Map.TerrainBushHISM.Get() })
	{
		if (IsValid(DressingComponent))
			DressingComponent->ClearInstances();
	}
	const TArray<FTerrainFeatureMesh>& FeatureMeshes = GetTerrainFeatureMeshes();
	// The same centre the ground and tile bands use, so a feature's material always
	// matches the flat cells it is dropped among.
	FIntPoint FeatureWarZoneCenter = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : Map.FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			FeatureWarZoneCenter = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}
	const int64 FeatureSeed = Map.GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
	int32 TerrainFeatureCount = 0;
	for (const FIntPoint& FeatureAnchor : SortedCells)
	{
		if (TerrainFeatureCount >= 28 || FeatureMeshes.IsEmpty())
			break;

		const uint32 FeatureHash = HashCombine(
			GetTypeHash(FeatureSeed),
			HashCombine(GetTypeHash(FeatureAnchor.X), GetTypeHash(FeatureAnchor.Y)));
		if (FeatureHash % 13 != 0)
			continue;

		const int32 MeshIndex = static_cast<int32>((FeatureHash >> 8) % static_cast<uint32>(FeatureMeshes.Num()));
		const FIntPoint Footprint = FeatureMeshes[MeshIndex].Footprint;

		// Nature and WarZone cells both qualify, but a feature never straddles the two:
		// every cell it touches, margin included, must share the anchor's type.
		ETileType AnchorType = ETileType::None;
		if (!GetCellType(FeatureAnchor, AnchorType)
			|| (AnchorType != ETileType::None && AnchorType != ETileType::WarZone))
		{
			continue;
		}

		// The industrial core stays flat: it holds the central facility, its approach
		// lanes and the densest authored cover.
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

		// The authored perimeter sits at local Z=0 and the shared walking surface is
		// Z=20, so placing the centre there makes the seam exact.
		const FTerrainFeatureMesh& FeatureMesh = FeatureMeshes[MeshIndex];
		const FVector FeatureCenter(
			(FeatureAnchor.X + (Footprint.X - 1) * 0.5f) * DesignCellSize,
			(FeatureAnchor.Y + (Footprint.Y - 1) * 0.5f) * DesignCellSize,
			20.0f);
		// Band mirrors BuildLightweightWorldVisuals: a WarZone cell inside d^2=225 sits
		// on transition dirt, anything further out shares the nature ground.
		const int32 BandIndex = (AnchorType == ETileType::WarZone && CoreDistanceSquared <= 225) ? 1 : 0;
		const int32 ComponentIndex = BandIndex * FeatureMeshes.Num() + MeshIndex;
		UHierarchicalInstancedStaticMeshComponent* FeatureComponent =
			Map.TerrainFeatureHISMs.IsValidIndex(ComponentIndex) ? Map.TerrainFeatureHISMs[ComponentIndex] : nullptr;
		if (IsValid(FeatureComponent))
		{
			FeatureComponent->AddInstance(
				FTransform(FRotator::ZeroRotator, FeatureCenter, FVector::OneVector), true);
		}

		// Dress the slope off the same height field the mesh was generated from. These
		// cells carry no tile actor, so without this a feature reads as a bare lump.
		FRandomStream DressingStream(static_cast<int32>(FeatureHash));
		auto ScatterOnFeature = [&](
			UHierarchicalInstancedStaticMeshComponent* Component,
			int32 Count, float MinScale, float MaxScale, float SinkCm)
		{
			if (!IsValid(Component))
				return;
			for (int32 Index = 0; Index < Count; ++Index)
			{
				// Stay off the outer eighth so nothing straddles the flat seam.
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
		// A hollow reads as a sheltered thicket, a rise as an exposed rocky crown. On
		// WarZone ground the pines are dropped entirely - a stand of forest inside an
		// industrial yard is exactly the kind of seam this band split exists to avoid.
		const bool bHollow = FeatureMesh.Amplitude < 0.0f;
		const bool bIndustrial = AnchorType == ETileType::WarZone;
		ScatterOnFeature(Map.TerrainRockHISM,
			bIndustrial ? (bHollow ? 5 : 8) : (bHollow ? 3 : 6), 0.55f, 1.25f, 18.0f);
		ScatterOnFeature(Map.TerrainTreeHISM,
			bIndustrial ? 0 : (bHollow ? 5 : 3), 0.85f, 1.35f, 12.0f);
		ScatterOnFeature(Map.TerrainBushHISM,
			bIndustrial ? 4 : (bHollow ? 11 : 8), 0.70f, 1.30f, 6.0f);
		++TerrainFeatureCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Terrain features: placed=%d kinds=%d"),
		TerrainFeatureCount, FeatureMeshes.Num());
}

void FPGLayoutPlanner::MeasureExtent()
{
	MinCell = SortedCells.Num() > 0 ? SortedCells[0] : FIntPoint::ZeroValue;
	MaxCell = MinCell;
	for (const FIntPoint& Cell : SortedCells)
	{
		MinCell.X = FMath::Min(MinCell.X, Cell.X);
		MinCell.Y = FMath::Min(MinCell.Y, Cell.Y);
		MaxCell.X = FMath::Max(MaxCell.X, Cell.X);
		MaxCell.Y = FMath::Max(MaxCell.Y, Cell.Y);
	}
	RaidSeed = Map.GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
}

FIntPoint FPGLayoutPlanner::GetFacilityCenterCell(const FFacilityPlacement& Facility)
{
	int32 SumX = 0;
	int32 SumY = 0;
	for (const FIntPoint& OccupiedCell : Facility.OccupiedCells)
	{
		SumX += OccupiedCell.X;
		SumY += OccupiedCell.Y;
	}
	const int32 Divisor = FMath::Max(1, Facility.OccupiedCells.Num());
	return FIntPoint(
		FMath::RoundToInt(static_cast<float>(SumX) / Divisor),
		FMath::RoundToInt(static_cast<float>(SumY) / Divisor));
}

void FPGLayoutPlanner::ConnectSupplementalRoadCells(const FIntPoint& A, const FIntPoint& B)
{
	if (!TileByCell.Contains(A) || !TileByCell.Contains(B))
		return;
	SupplementalRoadCells.Add(A);
	SupplementalRoadCells.Add(B);
	const FIntPoint Delta = B - A;
	if (Delta == FIntPoint(1, 0))
	{
		SupplementalRoadMasks.FindOrAdd(A) |= EastConnection;
		SupplementalRoadMasks.FindOrAdd(B) |= WestConnection;
	}
	else if (Delta == FIntPoint(-1, 0))
	{
		SupplementalRoadMasks.FindOrAdd(A) |= WestConnection;
		SupplementalRoadMasks.FindOrAdd(B) |= EastConnection;
	}
	else if (Delta == FIntPoint(0, 1))
	{
		SupplementalRoadMasks.FindOrAdd(A) |= NorthConnection;
		SupplementalRoadMasks.FindOrAdd(B) |= SouthConnection;
	}
	else if (Delta == FIntPoint(0, -1))
	{
		SupplementalRoadMasks.FindOrAdd(A) |= SouthConnection;
		SupplementalRoadMasks.FindOrAdd(B) |= NorthConnection;
	}
}

// 너비 우선 찾기의 부모 표로 Found → Start 길을 거슬러 올라가, Start 에서 Found 쪽으로 보조 길을 잇는다.
// 세 곳(시설 진입로·시작/출구 잇기·워존 길 보장)이 같은 모양으로 쓰던 것을 모았다. 끝까지 못 거슬러 가면 아무것도 안 깐다.
bool FPGLayoutPlanner::ConnectPathBack(const TMap<FIntPoint, FIntPoint>& ParentByCell, const FIntPoint& Found, const FIntPoint& Start, int32& OutLength)
{
	TArray<FIntPoint> ReversePath;
	FIntPoint PathCell = Found;
	ReversePath.Add(PathCell);
	while (PathCell != Start)
	{
		const FIntPoint* Parent = ParentByCell.Find(PathCell);
		if (Parent == nullptr || *Parent == PathCell)
			break;
		PathCell = *Parent;
		ReversePath.Add(PathCell);
	}
	if (ReversePath.Last() != Start)
		return false;
	for (int32 PathIndex = ReversePath.Num() - 1; PathIndex > 0; --PathIndex)
		ConnectSupplementalRoadCells(ReversePath[PathIndex], ReversePath[PathIndex - 1]);
	OutLength = ReversePath.Num();
	return true;
}

// Facilities used to be wired to one another with a nearest-neighbour spanning
// tree. That laid more road than the generator itself produced - roughly 131
// cells of facility-to-facility web against the authored network - and none of
// it served the route the player actually needs. Two outlying camps do not need
// a paved road between them: the ground is flat and walkable everywhere, so the
// web only buried the generator's deliberate road layout under service lanes.
//
// What genuinely reads as wrong is a compound with no approach at all. So give
// each facility one short spur to the nearest road it can already reach, and
// leave anything beyond the budget unpaved. Spawn/Exit endpoint repair and the
// WarZone route guarantee below are untouched - those are the roads that make
// the raid playable.
//
// (The senior generator remains authoritative for gameplay regions. These cells become
// part of the future visual TileManifest, not a mutation of AMapTile's server-owned type.)
void FPGLayoutPlanner::AddFacilityAccessSpurs()
{
	int32 FacilitySpurCount = 0;
	int32 FacilitySpurCellCount = 0;
	int32 FacilitySpurSkippedCount = 0;
	// How far the skipped facilities actually were. Raising MaxFacilitySpurCells
	// blindly re-creates the old over-roading problem (the facility-to-facility
	// spanning tree once laid 131 cells against a 39-cell authored network), so the
	// budget only moves once these numbers say what it would actually cost.
	int32 NearestUnpavedCells = TNumericLimits<int32>::Max();
	int32 FarthestUnpavedCells = 0;
	int32 UnpavedCellsTotal = 0;
	int32 UnreachableFacilityCount = 0;
	if (!Map.FacilityPlacements.IsEmpty())
	{
		const FIntPoint SpurOffsets[] = {
			FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
		};
		for (const FFacilityPlacement& Facility : Map.FacilityPlacements)
		{
			// Elevated facilities terminate their route at the foot of the ramp, not
			// underneath the building in the reserved footprint.
			const FIntPoint StartCell = Facility.AccessCell != FIntPoint::ZeroValue
				? Facility.AccessCell
				: GetFacilityCenterCell(Facility);
			if (!TileByCell.Contains(StartCell))
			{
				++FacilitySpurSkippedCount;
				continue;
			}

			// Breadth-first so the spur is the shortest legal approach rather than a
			// meander; a service road that wanders 40 m to reach a road 3 cells away
			// is exactly the noise this pass exists to remove.
			TArray<FIntPoint> OpenCells;
			TMap<FIntPoint, FIntPoint> ParentByCell;
			TMap<FIntPoint, int32> DepthByCell;
			OpenCells.Add(StartCell);
			ParentByCell.Add(StartCell, StartCell);
			DepthByCell.Add(StartCell, 0);
			FIntPoint FoundRoad = StartCell;
			bool bFoundRoad = false;
			int32 ReadIndex = 0;
			const uint32 SpurHash = HashCombine(
				GetTypeHash(RaidSeed),
				HashCombine(GetTypeHash(StartCell.X), GetTypeHash(StartCell.Y)));

			while (ReadIndex < OpenCells.Num() && !bFoundRoad)
			{
				const FIntPoint Current = OpenCells[ReadIndex++];
				const int32 CurrentDepth = DepthByCell[Current];
				// The budget is applied to the *result*, not to the search. Cutting the
				// search short reported "no road" for facilities that had one just past
				// the limit, which hid how far off the network they really were.
				if (CurrentDepth >= Map.MaxFacilitySpurSearchCells)
					continue;

				const int32 DirectionOffset = static_cast<int32>((SpurHash + ReadIndex) % 4u);
				for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
				{
					const FIntPoint Neighbor = Current + SpurOffsets[(DirectionIndex + DirectionOffset) % 4];
					if (ParentByCell.Contains(Neighbor) || ReservedCells.Contains(Neighbor))
						continue;
					AMapTile* const* NeighborTilePtr = TileByCell.Find(Neighbor);
					if (NeighborTilePtr == nullptr || !IsValid(*NeighborTilePtr))
						continue;
					const ETileType NeighborType = (*NeighborTilePtr)->GetType();
					if (NeighborType == ETileType::Obstacle)
						continue;

					ParentByCell.Add(Neighbor, Current);
					DepthByCell.Add(Neighbor, CurrentDepth + 1);
					if (NeighborType == ETileType::Road || SupplementalRoadCells.Contains(Neighbor))
					{
						FoundRoad = Neighbor;
						bFoundRoad = true;
						break;
					}
					OpenCells.Add(Neighbor);
				}
			}

			if (!bFoundRoad)
			{
				// No road anywhere within the search horizon - a placement problem,
				// not a budget one.
				++FacilitySpurSkippedCount;
				++UnreachableFacilityCount;
				continue;
			}

			const int32 RoadDistanceCells = DepthByCell.FindRef(FoundRoad);
			if (RoadDistanceCells > Map.MaxFacilitySpurCells)
			{
				// Deliberate: a compound this far from the network stays unpaved
				// rather than dragging a long service lane across open terrain.
				// Named because it matters *which* ones these are: satellite camps,
				// barracks and the trench sit inside the WarZone where no road runs
				// by design, while a stranded Downtown or Factory is a real fault.
				UE_LOG(LogTemp, Display,
					TEXT("  unpaved facility: set=%s cell=(%d,%d) road_cells=%d"),
					*StaticEnum<EFacilityVisualSet>()->GetNameStringByValue(
						static_cast<int64>(Facility.VisualSet)),
					StartCell.X, StartCell.Y, RoadDistanceCells);
				++FacilitySpurSkippedCount;
				NearestUnpavedCells = FMath::Min(NearestUnpavedCells, RoadDistanceCells);
				FarthestUnpavedCells = FMath::Max(FarthestUnpavedCells, RoadDistanceCells);
				UnpavedCellsTotal += RoadDistanceCells;
				continue;
			}

			int32 PathLength = 0;
			if (!ConnectPathBack(ParentByCell, FoundRoad, StartCell, PathLength))
			{
				++FacilitySpurSkippedCount;
				continue;
			}
			++FacilitySpurCount;
			FacilitySpurCellCount += PathLength;
		}
	}
	const int32 BudgetedUnpavedCount = FacilitySpurSkippedCount - UnreachableFacilityCount;
	UE_LOG(LogTemp, Display,
		TEXT("Facility access spurs: facilities=%d spurs=%d unpaved=%d cells=%d budget_cells=%d ")
		TEXT("unpaved_distance_cells=[min=%d avg=%.1f max=%d] unreachable=%d ")
		TEXT("cells_if_budget_covered_all=%d"),
		Map.FacilityPlacements.Num(), FacilitySpurCount, FacilitySpurSkippedCount,
		FacilitySpurCellCount, Map.MaxFacilitySpurCells,
		BudgetedUnpavedCount > 0 ? NearestUnpavedCells : 0,
		BudgetedUnpavedCount > 0 ? static_cast<float>(UnpavedCellsTotal) / BudgetedUnpavedCount : 0.0f,
		FarthestUnpavedCells, UnreachableFacilityCount,
		FacilitySpurCellCount + UnpavedCellsTotal);
}

void FPGLayoutPlanner::PickSpawnRegions()
{
	// 호수는 논리 칸만으로 정해진다(길 수로 모서리를 고른다). 추가 시작 지역이 호수를 피해야 해서 여기서 먼저 잰다.
	BorderLake = GetBorderLake(
		RaidSeed, MinCell, MaxCell, Map.BorderLakeRadiusCells, CollectTraversalCells(TileByCell));
	Map.BorderLakeCentreCell = BorderLake.CentreCell;
	Map.bHasBorderLake = Map.BorderLakeRadiusCells > 0.0f;

	// 멀티 시작 지역(9/28 다시 짬): 길을 깔기 전에 고른다 — 고른 칸에서 워존까지 길을 아래 "워존 길 보장" 으로 깐다.
	//   전에는 설계도를 다 만든 뒤에 골라서 추가 시작 지역에 길이 없었고(9/28 사용자 PIE: "길 생성 알고리즘이 적용 안 된 것 같다"),
	//   에픽템 모서리 바로 뒤에 서기도 했다. 고르는 규칙은 WarZoneFootprintPreview_SpawnRegions.cpp 머리 주석.
	{
		TSet<FIntPoint> Blocked = ReservedCells;
		Blocked.Append(SupplementalRoadCells);
		Map.BuildSpawnRegions(TileByCell, Blocked, BorderLake.CentreCell, Map.bHasBorderLake ? BorderLake.Radius : 0.0f);
	}
	for (int32 RegionIndex = 1; RegionIndex < Map.SpawnRegionCells.Num(); ++RegionIndex)
		ExtraSpawnCells.Add(Map.SpawnRegionCells[RegionIndex]);
}

// The facility tree above does not include server-authored Spawn/Exit cells.
// Repair each endpoint to the nearest real road through valid non-facility
// cells, otherwise one seed can leave a spawn pad visually stranded even
// though the other endpoints happen to touch the generated road network.
void FPGLayoutPlanner::RepairEndpointRoads()
{
	int32 EndpointCount = 0;
	int32 RepairedEndpointCount = 0;
	int32 FailedEndpointCount = 0;
	const FIntPoint NeighborOffsets[] = {
		FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
	};
	for (const FIntPoint& EndpointCell : SortedCells)
	{
		AMapTile* const* EndpointTilePtr = TileByCell.Find(EndpointCell);
		if (EndpointTilePtr == nullptr || !IsValid(*EndpointTilePtr))
			continue;
		const ETileType EndpointType = (*EndpointTilePtr)->GetType();
		if (EndpointType != ETileType::Spawn && EndpointType != ETileType::Exit)
			continue;

		++EndpointCount;
		TArray<FIntPoint> OpenCells;
		TMap<FIntPoint, FIntPoint> ParentByCell;
		OpenCells.Add(EndpointCell);
		ParentByCell.Add(EndpointCell, EndpointCell);
		FIntPoint FoundRoad = EndpointCell;
		bool bFoundRoad = false;
		int32 ReadIndex = 0;
		const uint32 EndpointHash = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(EndpointCell.X), GetTypeHash(EndpointCell.Y)));

		while (ReadIndex < OpenCells.Num() && !bFoundRoad)
		{
			const FIntPoint Current = OpenCells[ReadIndex++];
			const int32 DirectionOffset = static_cast<int32>((EndpointHash + ReadIndex) % 4u);
			for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
			{
				const FIntPoint Neighbor = Current + NeighborOffsets[(DirectionIndex + DirectionOffset) % 4];
				if (ParentByCell.Contains(Neighbor) || ReservedCells.Contains(Neighbor) || ExtraSpawnCells.Contains(Neighbor))
					continue;
				AMapTile* const* NeighborTilePtr = TileByCell.Find(Neighbor);
				if (NeighborTilePtr == nullptr || !IsValid(*NeighborTilePtr))
					continue;
				const ETileType NeighborType = (*NeighborTilePtr)->GetType();
				if (NeighborType == ETileType::Obstacle)
					continue;

				ParentByCell.Add(Neighbor, Current);
				if (NeighborType == ETileType::Road || SupplementalRoadCells.Contains(Neighbor))
				{
					FoundRoad = Neighbor;
					bFoundRoad = true;
					break;
				}
				OpenCells.Add(Neighbor);
			}
		}

		int32 PathLength = 0;
		if (!bFoundRoad || !ConnectPathBack(ParentByCell, FoundRoad, EndpointCell, PathLength))
		{
			++FailedEndpointCount;
			continue;
		}
		++RepairedEndpointCount;
	}
	UE_LOG(LogTemp, Log,
		TEXT("Endpoint access road repair: endpoints=%d repaired=%d failed=%d"),
		EndpointCount, RepairedEndpointCount, FailedEndpointCount);
}

// A spawn touching an arbitrary nearby road is not sufficient: that road can
// still belong to a disconnected outer branch.  Build one deterministic
// shortest visual route from every spawn to the nearest WarZone boundary.
// This does not change the server-owned tile types; it only guarantees that
// the client-side design manifest has a continuous readable approach route.
// Exits need the same guarantee. Covering only spawns left exit routes ending in
// a dead-end branch that never reached the centre, so a player who found an exit
// pad had no road leading back toward the WarZone.
void FPGLayoutPlanner::GuaranteeWarZoneRoutes()
{
	int32 SpawnRouteCount = 0;
	int32 ConnectedSpawnRouteCount = 0;
	int32 FailedSpawnRouteCount = 0;
	int32 LongestSpawnRouteCells = 0;
	const FIntPoint NeighborOffsets[] = {
		FIntPoint(0, 1), FIntPoint(1, 0), FIntPoint(0, -1), FIntPoint(-1, 0)
	};
	for (const FIntPoint& SpawnCell : SortedCells)
	{
		AMapTile* const* SpawnTilePtr = TileByCell.Find(SpawnCell);
		if (SpawnTilePtr == nullptr || !IsValid(*SpawnTilePtr))
			continue;
		const ETileType EndpointType = (*SpawnTilePtr)->GetType();
		// 멀티 추가 시작 지역도 워존까지 길을 받는다(9/28). 생성기 시작·출구와 같은 길 모양 규칙을 탄다.
		if (EndpointType != ETileType::Spawn && EndpointType != ETileType::Exit && !ExtraSpawnCells.Contains(SpawnCell))
			continue;

		++SpawnRouteCount;
		TArray<FIntPoint> OpenCells;
		TMap<FIntPoint, FIntPoint> ParentByCell;
		OpenCells.Add(SpawnCell);
		ParentByCell.Add(SpawnCell, SpawnCell);
		FIntPoint FoundWarZone = SpawnCell;
		bool bFoundWarZone = false;
		int32 ReadIndex = 0;
		const uint32 SpawnHash = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(SpawnCell.X), GetTypeHash(SpawnCell.Y)));

		while (ReadIndex < OpenCells.Num() && !bFoundWarZone)
		{
			const FIntPoint Current = OpenCells[ReadIndex++];
			const int32 DirectionOffset = static_cast<int32>((SpawnHash + ReadIndex) % 4u);
			for (int32 DirectionIndex = 0; DirectionIndex < 4; ++DirectionIndex)
			{
				const FIntPoint Neighbor = Current + NeighborOffsets[(DirectionIndex + DirectionOffset) % 4];
				// 다른 추가 시작 지역을 지나가면 그 담장에 입구가 둘이 필요해진다 — 돌아간다.
				if (ParentByCell.Contains(Neighbor) || ExtraSpawnCells.Contains(Neighbor))
					continue;
				AMapTile* const* NeighborTilePtr = TileByCell.Find(Neighbor);
				if (NeighborTilePtr == nullptr || !IsValid(*NeighborTilePtr))
					continue;
				const ETileType NeighborType = (*NeighborTilePtr)->GetType();
				if (NeighborType == ETileType::Obstacle
					|| (ReservedCells.Contains(Neighbor) && NeighborType != ETileType::WarZone))
				{
					continue;
				}

				ParentByCell.Add(Neighbor, Current);
				if (NeighborType == ETileType::WarZone)
				{
					FoundWarZone = Neighbor;
					bFoundWarZone = true;
					break;
				}
				OpenCells.Add(Neighbor);
			}
		}

		int32 PathLength = 0;
		if (!bFoundWarZone || !ConnectPathBack(ParentByCell, FoundWarZone, SpawnCell, PathLength))
		{
			++FailedSpawnRouteCount;
			continue;
		}
		LongestSpawnRouteCells = FMath::Max(LongestSpawnRouteCells, PathLength);
		++ConnectedSpawnRouteCount;
	}
	UE_LOG(LogTemp, Log,
		TEXT("Spawn/Exit to WarZone road guarantee: endpoints=%d connected=%d failed=%d longest_cells=%d"),
		SpawnRouteCount, ConnectedSpawnRouteCount, FailedSpawnRouteCount, LongestSpawnRouteCells);
}

bool FPGLayoutPlanner::IsAuthoritativeTraversalNeighbor(const FIntPoint& NeighborCell) const
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
	// An access road consumes the original visual cell. Only the explicit
	// planned route mask may connect to it; incidental side adjacency is not
	// a socket and must not turn a one-lane path into a giant crossroad.
	if (bNeighborBecomesAccessRoad)
		return false;
	return IsRoadTraversalType((*Neighbor)->GetType());
}

bool FPGLayoutPlanner::HasTraversalWithin(const FIntPoint& Cell, int32 Radius) const
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
}

// 칸마다 무엇을 놓을지(FTileDesignPlacement) 정한다. 종류별로 아래 Apply*Visual 이 겉모습·회전을 고른다.
void FPGLayoutPlanner::BuildPlacements()
{
	// One lake for the whole pass (BorderLake, computed above before the spawn regions).
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

		// Explicit route edges are authoritative for supplemental connectors. If a
		// route crosses an existing generator road, union those explicit edges with
		// that road's normal neighbours so both sides keep reciprocal sockets.
		uint8 ConnectionMask = SupplementalRoadMasks.FindRef(Cell);
		if (!bSupplementalRoad)
		{
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(0, 1))) ConnectionMask |= NorthConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(1, 0))) ConnectionMask |= EastConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(0, -1))) ConnectionMask |= SouthConnection;
			if (IsAuthoritativeTraversalNeighbor(Cell + FIntPoint(-1, 0))) ConnectionMask |= WestConnection;
		}

		FTileDesignPlacement& Placement = Map.TileDesignPlacements.AddDefaulted_GetRef();
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
			ApplyRoadVisual(Placement, ConnectionMask);
		else if (Type == ETileType::Spawn)
			ApplySpawnVisual(Placement, Cell, ConnectionMask);
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
			ApplyNatureVisual(Placement, Cell);

		Counts.FindOrAdd(Placement.Visual)++;
		if (Placement.RotationQuarterTurns < 0 || Placement.RotationQuarterTurns > 3)
			++InvalidRotationCount;
	}
	UE_LOG(LogTemp, Display, TEXT("Supplemental POI road design: cells=%d facilities=%d"),
		SupplementalRoadCells.Num(), Map.FacilityPlacements.Num());
}

void FPGLayoutPlanner::ApplyRoadVisual(FTileDesignPlacement& Placement, uint8 ConnectionMask) const
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

void FPGLayoutPlanner::ApplySpawnVisual(FTileDesignPlacement& Placement, const FIntPoint& Cell, uint8 ConnectionMask)
{
	Placement.Visual = ETileDesignVisual::Spawn;
	Placement.VisualLevel = TSoftObjectPtr<UWorld>(SpawnLevelPath);
	// LD_Tile_Spawn_Staging is a walled compound with a single opening on its
	// authored east face, so the rotation chosen here decides which one of the
	// cell's connections the player can actually walk out through. Picking the
	// lowest set bit meant a spawn wired north+east faced north and sealed the
	// east road behind a wall - the player saw no way to the WarZone at all.
	//
	// Prefer the edge the spawn-to-WarZone guarantee actually routed through,
	// so the opening always faces the path that is promised to lead somewhere.
	const uint8 RouteMask = SupplementalRoadMasks.FindRef(Cell) & ConnectionMask;
	const uint8 PreferredMask = RouteMask != 0 ? RouteMask : ConnectionMask;
	const uint8 TargetMask = PreferredMask != 0
		? static_cast<uint8>(1 << FMath::CountTrailingZeros(PreferredMask))
		: EastConnection;
	Placement.RotationQuarterTurns = FindPositiveYawRotation(EastConnection, TargetMask);
	// Any remaining connection is a road that dead-ends against this tile's
	// wall. Harmless for routing, but it reads as a road to nowhere, so count
	// them instead of letting them pass silently.
	SpawnOpeningCount += ConnectionMask != 0 ? 1 : 0;
	WalledOffSpawnConnections += FMath::Max(0, FMath::CountBits(ConnectionMask) - 1);
	SpawnRoutedOpenings += RouteMask != 0 ? 1 : 0;
}

void FPGLayoutPlanner::ApplyNatureVisual(FTileDesignPlacement& Placement, const FIntPoint& Cell) const
{
	const int32 BorderDistance = FMath::Min(
		FMath::Min(Cell.X - MinCell.X, MaxCell.X - Cell.X),
		FMath::Min(Cell.Y - MinCell.Y, MaxCell.Y - Cell.Y));
	const bool bMapBorder = BorderDistance <= 2;
	const bool bNearTraversal = HasTraversalWithin(Cell, 2);
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

	// A lake eats into one corner of the map. The grid is square and always
	// will be, but the coastline does not have to be: a metaball centred
	// outside the corner plus per-cell jitter gives a ragged shore, which is
	// what stops the border reading as a drawn box. It also gives the shore
	// assets - the rural diorama, its pier and boats - somewhere they belong
	// instead of sitting in the middle of a dry field.
	//
	// Traversal cells are never flooded, and neither are their neighbours, so
	// a spawn or exit that the generator placed near the edge keeps a
	// peninsula out to it rather than drowning.
	if (Map.BorderLakeRadiusCells > 0.0f
		&& !bNearTraversal && Placement.Visual != ETileDesignVisual::WarZoneGround)
	{
		const float LakeRadius = BorderLake.Radius + GetLakeShoreJitter(RaidSeed, Cell);
		const float DistanceToLakeCentre =
			FVector2D::Distance(FVector2D(Cell.X, Cell.Y), BorderLake.CentreCell);
		if (DistanceToLakeCentre <= LakeRadius)
			Placement.Visual = ETileDesignVisual::Water;
	}

	// Natural cells are grouped into 5x5-cell macro biomes so the result reads
	// as forest belts, clearings and rocky fields rather than visual noise.
	// Road-adjacent cells deliberately remain more open for combat readability.
	if (Placement.Visual == ETileDesignVisual::Water)
	{
		// Already decided above; the biome roll must not overwrite it.
	}
	else if (bMapBorder)
	{
		Placement.Visual = FineRoll < 72
			? ETileDesignVisual::NatureForestDense
			: ETileDesignVisual::NatureForestSparse;
	}
	else if (bMazeBarrierBand)
	{
		// Five-cell macro belts become the Maze's impassable-looking terrain.
		// Existing/generated traversal cells and a two-cell shoulder are always
		// exempt, so this shapes routes without changing server connectivity.
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

// 설계도 해시(서버=접속자 확인용) + 길 모양 검사(회전이 연결과 맞나, 이웃이 서로 이어졌나).
void FPGLayoutPlanner::HashAndValidate()
{
	Map.LayoutHash = 0;
	TMap<FIntPoint, const FTileDesignPlacement*> PlacementByCell;
	for (const FTileDesignPlacement& Placement : Map.TileDesignPlacements)
	{
		PlacementByCell.Add(Placement.GridCell, &Placement);
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.GridCell.X));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.GridCell.Y));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(static_cast<uint8>(Placement.Visual)));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.ConnectionMask));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.bSupplementalAccessRoad));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.LayoutVariant));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.LocalSeed));
	}

	// routed_openings should equal openings: every spawn's single doorway ought to
	// face the road the WarZone guarantee actually planned. walled_connections is
	// the count of roads that still dead-end against a spawn's wall - the tile has
	// one opening, so anything above zero is a road the player can see but not use.
	UE_LOG(LogTemp, Display,
		TEXT("Spawn opening alignment: spawns=%d routed_openings=%d walled_connections=%d"),
		SpawnOpeningCount, SpawnRoutedOpenings, WalledOffSpawnConnections);

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
	for (const FTileDesignPlacement& Placement : Map.TileDesignPlacements)
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
		CanonicalMask = GetTileVisualTraits(Placement.Visual).RoadCanonicalMask; // 길 모양 표(공용 헤더)
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
	for (const FFacilityPlacement& Placement : Map.FacilityPlacements)
	{
		Map.LayoutHash = HashCombine(Map.LayoutHash, FCrc::StrCrc32(*Placement.FacilityId.ToString()));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(static_cast<uint8>(Placement.VisualSet)));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.AnchorCell.X));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.AnchorCell.Y));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.Footprint.X));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.Footprint.Y));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.RotationQuarterTurns));
		Map.LayoutHash = HashCombine(Map.LayoutHash, GetTypeHash(Placement.LocalSeed));
	}
}

// 멀티 시작 지역(9/27): 추가 시작 지역 칸을 논리 시작 칸과 똑같은 "출발 구역" 타일(담장 두른 LD_Tile_Spawn_Staging)로 바꾼다.
// 왜: 전에는 추가 지역이 게임 지점(자리 4개)뿐이라 겉모습은 숲·풀밭 그대로였다 — 두 번째 사람이 풀숲 한가운데서 나왔다
//   (9/27 사용자: "스폰이 스폰지역에 안되고 무슨 풀숲이여"). 칸 종류가 Spawn 이 되면 타일·아스팔트 바닥·주변 나무 걷기·
//   괴물 안전 띠·검증기가 논리 시작 칸과 같은 길을 탄다.
// 여기(설계도를 다 만든 직후, 타일을 세우기 전)에서 고르는 이유: 타일 종류가 정해진 뒤면 이미 숲 타일이 서 버린다.
//   서버·클라가 같은 설계도로 같은 칸을 고른다(BuildSpawnRegions 는 시간·로컬 난수를 안 쓴다).
//   (9/28) 고르기는 길을 깔기 전(위, BuildSpawnRegions)으로 옮겼다. 여기서는 겉모습만 바꾼다.
void FPGLayoutPlanner::DressExtraSpawnRegions()
{
	for (int32 RegionIndex = 1; RegionIndex < Map.SpawnRegionCells.Num(); ++RegionIndex)
	{
		const FIntPoint RegionCell = Map.SpawnRegionCells[RegionIndex];
		FTileDesignPlacement* Placement = Map.TileDesignPlacements.FindByPredicate(
			[&RegionCell](const FTileDesignPlacement& Candidate) { return Candidate.GridCell == RegionCell; });
		if (!Placement)
			continue;
		Placement->Visual = ETileDesignVisual::Spawn;
		Placement->VisualLevel = TSoftObjectPtr<UWorld>(SpawnLevelPath);
		// 담장의 유일한 출입구(타일 원본의 동쪽 면)를 워존 길이 나가는 쪽으로 돌린다(9/28). 길이 없으면(워존 길 실패) 예전처럼 맵 가운데 쪽.
		const uint8 RouteMask = Placement->ConnectionMask;
		uint8 FacingMask = 0;
		for (const uint8 Bit : { NorthConnection, EastConnection, SouthConnection, WestConnection })
			if ((RouteMask & Bit) != 0 && FacingMask == 0)
				FacingMask = Bit;
		if (FacingMask == 0)
		{
			const FIntPoint ToCentre = (MinCell + MaxCell) / 2 - RegionCell;
			FacingMask = FMath::Abs(ToCentre.X) >= FMath::Abs(ToCentre.Y)
				? (ToCentre.X >= 0 ? EastConnection : WestConnection)
				: (ToCentre.Y >= 0 ? NorthConnection : SouthConnection);
		}
		Placement->ConnectionMask = FacingMask;
		Placement->RotationQuarterTurns = FindPositiveYawRotation(EastConnection, FacingMask);
		UE_LOG(LogTemp, Display, TEXT("Spawn region %d at (%d,%d): road mask %d, opening faces %d"),
			RegionIndex, RegionCell.X, RegionCell.Y, RouteMask, FacingMask);
		Map.LayoutHash = HashCombine(Map.LayoutHash, HashCombine(GetTypeHash(RegionCell), GetTypeHash(Placement->RotationQuarterTurns)));
	}
}

void FPGLayoutPlanner::LogSummary() const
{
	UE_LOG(LogTemp, Display,
		TEXT("Design placement spec: cells=%d reserved=%d invalid_rotations=%d map_extent_cm=%.0f layout_hash=%08X"),
		Map.TileDesignPlacements.Num(), ReservedCells.Num(), InvalidRotationCount,
		Map.GridCellSpan * DesignCellSize, Map.LayoutHash);
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
