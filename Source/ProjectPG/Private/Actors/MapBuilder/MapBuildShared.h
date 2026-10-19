// 맵 액터와 일꾼들(건물 자리 담당 등)이 같이 쓰는 숫자·경로·작은 계산.
// 왜 따로 뺐나: 원래는 맵 cpp 파일 안에만 있어서, 다른 파일(일꾼)에서는 쓸 수 없었다.
//   일꾼 파일마다 복사해 두면 한쪽만 고쳤을 때 값이 어긋난다.
//   예) 호수 위치 계산이 두 벌이면, 호수와 호숫가 마을이 서로 다른 모서리에 생긴다.
//   그래서 한 벌만 여기 두고 맵 cpp 와 일꾼 cpp 가 같이 include 한다.
// MapBuild 라는 이름 묶음(namespace) 안에 넣는 이유: 다른 파일의 같은 이름과 섞이지 않게.
//   (맵 검사기를 만들 때 파일들이 하나로 합쳐 빌드되면서 이름이 겹쳐 충돌한 적이 있다.)
#pragma once

#include "CoreMinimal.h"
#include "UObject/SoftObjectPath.h"
#include "Actors/MapTile.h"
#include "Actors/MapBuilder.h"

namespace MapBuild
{
// 미리 만들어 둔 땅 모양(언덕, 웅덩이 등). 각각 20 m 칸의 정수 배 크기로 만들었다.
// 둘레는 전부 평평하고 안쪽만 볼록/오목해서, 이웃 칸과 가장자리를 맞출 필요가 없다.
// 어디에 놓아도 주변 평평한 땅과 같은 높이로 이어진다.
struct FTerrainFeatureMesh
{
	const TCHAR* ShapeName;
	const TCHAR* AssetPath;
	FIntPoint Footprint;
	// 높이 차이(Amplitude)와 능선 모양 여부는 땅 메시를 만들 때 쓴 값과 같아야 한다.
	// 그래야 아래 꾸미기 배치가 소품을 이 땅 표면 위에 정확히 올린다.
	float Amplitude;
	bool bRidgeProfile;
};

inline const TArray<FTerrainFeatureMesh>& GetTerrainFeatureMeshes()
{
	static const TArray<FTerrainFeatureMesh> Meshes = {
		{ TEXT("Mound2x2"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Mound_2x2.SM_Terrain_Mound_2x2"), FIntPoint(2, 2), 320.0f, false },
		{ TEXT("Bowl2x2"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Bowl_2x2.SM_Terrain_Bowl_2x2"), FIntPoint(2, 2), -260.0f, false },
		{ TEXT("Ridge1x3"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Ridge_1x3.SM_Terrain_Ridge_1x3"), FIntPoint(1, 3), 280.0f, true },
		{ TEXT("Saddle2x1"), TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Terrain_Saddle_2x1.SM_Terrain_Saddle_2x1"), FIntPoint(2, 1), 240.0f, false }
	};
	return Meshes;
}

// 땅 모양 메시를 만들 때 쓴 높이 공식을 그대로 옮긴 것이다.
// sin^2 은 양 끝에서 높이도 0, 기울기도 0 이라서 둘레가 주변 평평한 땅에 매끄럽게 이어진다.
// 충돌 검사로 높이를 재지 않고 여기서 똑같이 계산하면,
// 꾸미기 배치가 매번 같은 결과로 나오고 땅 하나하나마다 높이를 재는 선을 쏠 필요도 없다.
inline float SampleTerrainFeatureHeight(const FTerrainFeatureMesh& Feature, float U, float V)
{
	auto Bump = [](float T)
	{
		const float S = FMath::Sin(PI * FMath::Clamp(T, 0.0f, 1.0f));
		return S * S;
	};
	const float Profile = Feature.bRidgeProfile
		? Bump(U) * FMath::Pow(Bump(V), 0.35f)
		: Bump(U) * Bump(V);
	return Feature.Amplitude * Profile;
}

	const FName ReservedTag(TEXT("Reserved_Facility"));
	const FName WarZoneFacilityTag(TEXT("WarZoneFacility"));
	const FName WarehouseTag(TEXT("Facility_Compound_3x3"));
	const FName YardTag(TEXT("Facility_Yard_2x2"));
	const FName BarracksTag(TEXT("Facility_Barracks_2x1"));
	const FName TrenchTag(TEXT("Facility_Trench_4x1"));
	const FName CheckpointTag(TEXT("Facility_Checkpoint_1x2"));
	const FName DowntownTag(TEXT("Facility_Downtown_3x3"));
	const FName FactoryConstructionTag(TEXT("Facility_FactoryConstruction_2x2"));
	const FName RuralHideoutTag(TEXT("Facility_RuralHideout_2x2"));

	// WarZone 중심 시설이 차지하는 칸 수. 3x5 인 이유:
	// 가져온 공장 구역은 남북으로 100 m 쯤, 건물 줄 네 개가 붙어 있다.
	// 3x3 으로 자르면 가운데 두 줄만 들어가고 바깥 줄이 반으로 잘렸다.
	// (시내 구역도 6x6 으로 키우기 전에 같은 문제가 있었다.)
	// 시설 구분 검사, 자리 찾기, 중심 계산이 모두 이 상수 하나를 읽는다.
	// 시설 레벨과 공장 단지 BP 경로는 MapAssetSet.h(데이터 에셋 DA_MapAssets)로 옮겼다. 에디터에서 고른다.
	const FIntPoint WarZoneCoreFootprint(3, 5);
	const FIntPoint WarZoneCoreCentreOffset(
		(WarZoneCoreFootprint.X - 1) / 2, (WarZoneCoreFootprint.Y - 1) / 2);

	// 건물 모양을 코드로 만들지 않고(AProceduralFacilityActor), 손으로 만들어 둔 레벨에서 가져오는 시설 종류.
	// 한곳에 모아 둬서 자리 예약, 땅 바닥판, 게임 중 생성 반복문이
	// 서로 다르게 판단하는 일이 없게 한다.
	inline bool FacilityUsesAuthoredLevel(EFacilityVisualSet VisualSet, const FIntPoint& Footprint)
	{
		// Warehouse 는 차지하는 칸 수로 구분한다.
		// - 3x3 → 자기 레벨이 있는 WarZone 중심
		// - 2x2 창고 → 코드로 만드는 주변 거점
		return VisualSet == EFacilityVisualSet::Checkpoint
			|| VisualSet == EFacilityVisualSet::RuralHideout
			|| VisualSet == EFacilityVisualSet::FactoryConstruction
			|| VisualSet == EFacilityVisualSet::DowntownBlock
			|| (VisualSet == EFacilityVisualSet::Warehouse && Footprint == WarZoneCoreFootprint);
	}

	// 자기 땅 모양을 직접 가지고 있는 손작업 레벨인지. 그런 레벨 밑에는 평평한 바닥판을 깔면 안 된다.
	//
	// 지금은 해당하는 시설이 없다. 일부러 그렇게 했다. 예전엔 시골 디오라마가 해당했다.
	// - 자기 섬과 수면을 가져왔는데 주변 평평한 칸과 높이가 안 맞았다.
	// - 그렇다고 밑 바닥판을 빼면 경계에 틈이 벌어졌다.
	// 지금은 가져온 레벨의 땅을 지우고 공통 타일 지형이 그대로 지나가게 한다. 시설이 맵의 일부로 보인다.
	inline bool FacilityBringsOwnTerrain(EFacilityVisualSet VisualSet)
	{
		return false;
	}

	const FSoftObjectPath RoadStraightLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Straight.LD_Tile_Road_Straight"));
	const FSoftObjectPath RoadCornerLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Corner.LD_Tile_Road_Corner"));
	const FSoftObjectPath RoadTJunctionLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_TJunction.LD_Tile_Road_TJunction"));
	const FSoftObjectPath RoadCrossLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Cross.LD_Tile_Road_Cross"));
	const FSoftObjectPath RoadDeadEndLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_DeadEnd.LD_Tile_Road_DeadEnd"));
	const FSoftObjectPath SpawnLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Spawn_Staging.LD_Tile_Spawn_Staging"));
	const FSoftObjectPath ExitLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Exit_Checkpoint.LD_Tile_Exit_Checkpoint"));
	const FSoftObjectPath ObstacleLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Obstacle_Checkpoint.LD_Tile_Obstacle_Checkpoint"));
	const FSoftObjectPath OpenGroundLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_None_OpenGround.LD_Tile_None_OpenGround"));
	const FSoftObjectPath RuinsLevelPath(TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_None_Ruins.LD_Tile_None_Ruins"));
	// 칸 크기(2000cm).
	constexpr float DesignCellSize = 2000.0f;
	// 모든 평평한 칸, 타일 소품, 도로판이 기준으로 삼는 하나뿐인 걷는 높이.
	// 여기서 벗어나도 되는 건 시설 자리뿐이다.
	constexpr float BaseGroundSurfaceZ = 20.0f;
	// 걸어서 넘을 수 있는 가장 높은 턱(cm). 캐릭터 이동 기본값(MaxStepHeight)과 같고,
	// 검사용 캐릭터(ALevelDesignValidationCharacter)도 이 값을 바꾸지 않는다.
	// 이보다 높은 턱은 걸어서 못 넘고 점프해야 한다.
	constexpr float MaxTraversableStepCm = 45.0f;
	// 도로 윗면은 공통 땅 윗면보다 이만큼 높다.
	// 도로판은 충분히 두꺼워서, 아랫면이 땅 안쪽 깊숙이 들어간다.
	constexpr float RoadSurfaceLiftCm = 10.0f;
	constexpr float RoadSurfaceThicknessCm = 30.0f;
	// 가장자리 호수.
	// - 바닥은 수면보다 충분히 아래라서 얕은 웅덩이가 아니라 깊은 물로 보인다.
	// - 수면은 공통 땅 윗면보다 낮아서 물가 둑이 진짜 낭떠러지가 된다. 플레이어가 그냥 걸어 들어갈 수 없다.
	// 물가 모양 번호 -2 는 "네 모서리가 전부 물에 잠김"이라는 뜻이다.
	// 미리 만들어 둔 물가 메시 네 개에는 없는 번호이고, 이 칸은 호수 바닥으로 처리한다.
	constexpr int32 SubmergedShoreVariant = -2;
	constexpr float LakeSurfaceZ = -35.0f;
	// 미리 만든 물가 메시에서 "물에 잠긴 모서리"의 높이와 같아야 한다.
	// 다르면 모래사장은 -110 에서 끝나는데 평평한 바닥은 다른 깊이에서 시작해, 만나는 곳에 턱이 생긴다.
	constexpr float LakeBedZ = -110.0f;

	// 호수는 맵 한 모서리 바로 바깥에 중심을 둔 둥근 덩어리(metaball) 하나다.
	// 그래서 가장자리를 따라 띠처럼 생기지 않고, 둥근 만처럼 맵 안쪽으로 파고든다.
	// 칸을 물로 채우는 타일 단계와, 호숫가 마을 자리를 잡는 시설 예약이
	// 호수 위치에 대해 같은 답을 내야 하므로 여기 둔다.
	struct FBorderLake
	{
		FVector2D CentreCell = FVector2D::ZeroVector;
		float Radius = 0.0f;
	};

	inline FBorderLake GetBorderLake(int64 RaidSeed, const FIntPoint& MinCell, const FIntPoint& MaxCell,
		float RadiusCells, const TSet<FIntPoint>& TraversalCells)
	{
		// 예전에는 모서리를 랜덤 값(시드)만으로 골랐다. 그런데 하필 그 모서리로 길이나 시작 지점이 나오면,
		// 물 채우기 단계가 "지나갈 수 있는 칸은 물로 안 채움" 규칙을 칸마다 적용해서
		// 호수가 자잘한 웅덩이로 쪼개졌다. 호수 크기를 RadiusCells 가 아니라 도로 배치가 정하고 있었던 것이다.
		// (반지름은 9 로 고정인데, 판마다 물 칸 수가 28/25/5/3/1 로 제각각 나왔다.)
		// 그래서 네 모서리에 모두 점수를 매겨서, 도로망이 가장 덜 닿는 모서리를 고른다.
		// 시드는 "어느 모서리부터 볼지"만 정한다. 점수가 똑같이 깨끗한 모서리끼리는 판마다 여전히 달라진다.
		FBorderLake Lake;
		Lake.Radius = RadiusCells;
		const int32 FirstCorner = static_cast<int32>(GetTypeHash(RaidSeed) % 4u);
		int32 BestCount = TNumericLimits<int32>::Max();
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const int32 Corner = (FirstCorner + Index) % 4;
			const FVector2D Centre(
				(Corner & 1) ? MaxCell.X + 3.0f : MinCell.X - 3.0f,
				(Corner & 2) ? MaxCell.Y + 3.0f : MinCell.Y - 3.0f);
			int32 Count = 0;
			for (const FIntPoint& Cell : TraversalCells)
				if (FVector2D::Distance(FVector2D(Cell.X, Cell.Y), Centre) <= RadiusCells + 2.0f)
					++Count;
			// "보다 작다"만 쓴다. 점수가 같으면 먼저 본 모서리가 이기므로,
			// 두 군데서 불러도 똑같은 답이 나온다.
			if (Count < BestCount)
			{
				BestCount = Count;
				Lake.CentreCell = Centre;
			}
		}
		return Lake;
	}

	// 칸마다 물가 선을 조금씩 흔든다. 안 흔들면 물가가 깔끔한 원호가 되는데,
	// 그건 직선만큼이나 기계로 만든 티가 난다.
	inline float GetLakeShoreJitter(int64 RaidSeed, const FIntPoint& Cell)
	{
		const uint32 ShoreHash = HashCombine(
			GetTypeHash(RaidSeed),
			HashCombine(GetTypeHash(Cell.X * 7), GetTypeHash(Cell.Y * 13)));
		return (static_cast<int32>(ShoreHash % 33u) - 16) * 0.1f;
	}
	constexpr uint8 NorthConnection = 1 << 0;
	constexpr uint8 EastConnection = 1 << 1;
	constexpr uint8 SouthConnection = 1 << 2;
	constexpr uint8 WestConnection = 1 << 3;

	inline uint8 RotateConnectionMaskPositiveYaw(uint8 Mask)
	{
		uint8 Rotated = 0;
		if (Mask & NorthConnection) Rotated |= WestConnection;
		if (Mask & EastConnection) Rotated |= NorthConnection;
		if (Mask & SouthConnection) Rotated |= EastConnection;
		if (Mask & WestConnection) Rotated |= SouthConnection;
		return Rotated;
	}

	inline int32 FindPositiveYawRotation(uint8 CanonicalMask, uint8 TargetMask)
	{
		uint8 RotatedMask = CanonicalMask;
		for (int32 QuarterTurns = 0; QuarterTurns < 4; ++QuarterTurns)
		{
			if (RotatedMask == TargetMask)
				return QuarterTurns;
			RotatedMask = RotateConnectionMaskPositiveYaw(RotatedMask);
		}
		return 0;
	}

	inline bool IsRoadTraversalType(ETileType Type)
	{
		return Type == ETileType::Road
			|| Type == ETileType::Obstacle
			|| Type == ETileType::Spawn
			|| Type == ETileType::Exit
			|| Type == ETileType::WarZone;
	}

	// 시설 예약과 물 채우기 단계 둘 다 호수 위치를 물어보고, 같은 답을 받아야 한다.
	// 그래서 모서리 선택은 둘이 똑같이 보는 자료(칸별 타일 종류 표)에만 기대야 한다.
	// 추가 갈래길은 나중에 화면용으로 덧붙이는 것이라, 점수 계산에서 일부러 뺀다.
	inline TSet<FIntPoint> CollectTraversalCells(const TMap<FIntPoint, AMapTile*>& TileByCell)
	{
		TSet<FIntPoint> Cells;
		for (const TPair<FIntPoint, AMapTile*>& Pair : TileByCell)
			if (IsValid(Pair.Value) && IsRoadTraversalType(Pair.Value->GetType()))
				Cells.Add(Pair.Key);
		return Cells;
	}

	const FIntPoint FootprintOffsets[] = {
		FIntPoint(0, 0), FIntPoint(1, 0),
		FIntPoint(0, 1), FIntPoint(1, 1)
	};
}
