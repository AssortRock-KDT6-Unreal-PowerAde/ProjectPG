// 절차 맵의 화면 담당: 논리 칸(AMapTile) 격자를 보고 타일·시설·가장자리 호수를 실제로 깔아 준다.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
// 처음엔 시설 자리 미리보기로 만들어짐
#include "MapBuilder.generated.h"

class AMapTile;
class ANavigationData;
class ATacticalTileActor;
class UHierarchicalInstancedStaticMeshComponent;
class ULevelStreamingDynamic;
class UNavigationInvokerComponent;
class UPCGComponent;
class UPCGGraph;
class UStaticMeshComponent;
class UTacticalTileNavModifierComponent;
class UWorld;

UENUM(BlueprintType)
enum class EFacilityVisualSet : uint8
{
	Warehouse,
	Yard,
	LongBarracks,
	LinearTrench,
	DowntownBlock,
	FactoryConstruction,
	RuralHideout,
	Checkpoint
};

UENUM(BlueprintType)
enum class EFacilityElevationProfile : uint8
{
	Ground,
	RaisedBarracks,
	RaisedCompound,
	WarZoneStronghold
};

UENUM(BlueprintType)
enum class ETileDesignVisual : uint8
{
	OpenGround,
	Ruins,
	NatureMeadow,
	NatureForestSparse,
	NatureForestDense,
	NatureRocky,
	NatureScrub,
	NatureAmbush,
	NatureServiceCamp,
	NatureDitch,
	WarZoneGround,
	RoadStraight,
	RoadCorner,
	RoadTJunction,
	RoadCross,
	RoadDeadEnd,
	Spawn,
	Exit,
	Obstacle,
	// 가장자리 호수. 기획서에서 물은 미로의 '못 지나가는 지형'에 들어가므로
	// 이 칸들은 일부러 걸을 수 없게 둔다: 물가의 둑이 곧 벽이고,
	// 그래서 VerifyTraversableElevation 도 이 칸들을 검사에서 뺀다.
	Water
};

USTRUCT(BlueprintType)
struct FTileDesignPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ETileDesignVisual Visual = ETileDesignVisual::OpenGround;

	// N=1, E=2, S=4, W=8 (디자인 세계 좌표 기준).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 ConnectionMask = 0;

	// 큰 거점(POI)끼리 잇는, 매번 똑같이 정해지는 디자인용 진입로이면 true.
	// 이 길은 가벼운 도로 빌더를 쓴다. 그래야 긴 길에 꾸밈이 가득한
	// 1x1 전시용 타일이 20 m 마다 반복되지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bSupplementalAccessRoad = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RotationQuarterTurns = 0;

	// 고정된 손작업 전투 배치 선택값(0..3). 클라이언트마다 따로 다시 계산하지 않도록
	// 나중에 TileManifest 에 저장해 보낸다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 LayoutVariant = 0;

	// 칸마다 정해지는 소품/꾸미기 시드. 서버 manifest 가 이 값을 그대로 보내고,
	// 클라이언트는 자기 시간값을 쓰거나 따로 다시 계산하지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LocalSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> VisualLevel;
};

UENUM(BlueprintType)
enum class ELevelDesignPointType : uint8
{
	Spawn,
	Loot,
	AISpawn,
	Exit,
	Quest
};

USTRUCT(BlueprintType)
struct FLevelDesignPoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ELevelDesignPointType Type = ELevelDesignPointType::Spawn;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName PointId = NAME_None;

	// 나중에 서버 쪽 스폰/전리품 시스템이 읽어 가는 데이터 약속일 뿐이다.
	// 레벨 디자인 단계에서는 복제되는 게임플레이 액터를 만들지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName ArchetypeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 Tier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float RadiusCm = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Capacity = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 PointSeed = 0;
};

USTRUCT(BlueprintType)
struct FFacilityPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName FacilityId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EFacilityVisualSet VisualSet = EFacilityVisualSet::Warehouse;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint Footprint = FIntPoint(2, 2);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RotationQuarterTurns = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LocalSeed = 0;

	// 높이와 출입구는 매번 똑같이 정해지는 시설 manifest 의 일부다. 클라이언트가
	// 자기 쪽 트레이스나 에셋 크기로 따로 추측하면 안 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EFacilityElevationProfile ElevationProfile = EFacilityElevationProfile::Ground;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float BaseElevationCm = 0.0f;

	// 자리 안쪽 가장자리 칸, 그 옆 땅 칸, 바깥을 향하는 동서남북 방향.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint EntranceCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint AccessCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint EntranceDirection = FIntPoint(0, -1);

	// 이 논리 자리에 쓰기로 고른, 손으로 만든 시설 레벨.
	// 소프트 레퍼런스라서 격자를 만드는 동안 무거운 레벨을 불러오지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> FacilityLevel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FIntPoint> OccupiedCells;
};

// 게임플레이 포인트(Spawn/Loot/AI/Exit/Quest)가 확정됐을 때 한 번 알린다.
// 오브젝트 스포너(UPGObjectSpawnerSubsystem)가 여기에 붙어 Loot/Exit/Quest 자리에 실제 오브젝트를 만든다.
DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelDesignPointsBuilt, const TArray<FLevelDesignPoint>&);

UCLASS()
class PROJECTPG_API AMapBuilder : public AActor
{
	GENERATED_BODY()

public:
	AMapBuilder();

	// 데이터 전용 계약. 스폰·루팅 시스템이 읽기만 한다.
	const TArray<FLevelDesignPoint>& GetLevelDesignPoints() const { return LevelDesignPoints; }
	bool AreLevelDesignPointsBuilt() const { return bLevelDesignPointsBuilt; }

	FOnLevelDesignPointsBuilt OnLevelDesignPointsBuilt;

	// 판 시드. 서버는 게임모드(형님 생성기)의 시드, 들어온 사람은 맵 설계도(AMapManifestActor)로 받은 시드.
	// 맵 계산(언덕·호수·시설 씨앗·흙길·산)은 전부 이걸 쓴다 — 예전엔 게임모드에서 직접 읽어서 클라에서는 0 이었다.
	int64 GetRaidSeed() const;
	// 형님 생성기의 시작 구역 상자 크기. 같은 이유로 서버는 생성기에서, 클라는 설계도에서.
	int32 GetStartRangeSize() const;
	// 들어온 사람: 맵 설계도가 도착하면 시드·상자 크기를 넣는다(칸 쪽지를 만들기 전에).
	void ApplyReplicatedManifest(int64 InSeed, int32 InStartRange);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void TryReserveFootprint();
	// 시설이 가진 경사로+계단 진입로마다 입구 칸 / 바깥 방향.
	// 땅 빌더와 VerifyTraversableElevation 이 같이 쓴다.
	void GetFacilityAccessEdges(
		const FFacilityPlacement& Placement,
		TArray<TPair<FIntPoint, FIntPoint>>& OutAccessEdges) const;
	float GetSurfaceElevationForCell(const FIntPoint& Cell) const;
	// 호수에 닿는 땅 칸마다 물가 메시 모양과 90도 회전 횟수를 고른다.
	// 값은 (모양 번호, 90도 회전 횟수).
	void BuildShoreTransitionMap(
		const TSet<FIntPoint>& LakeCells,
		TMap<FIntPoint, TPair<int32, int32>>& OutShoreTileByCell) const;
	bool AreAllFacilityLevelsLoaded() const;
	FVector GetDesignFootprintCenter(const FFacilityPlacement& Placement) const;

	// 협력객체 : 맵 자체 검사
	// friend class : 검사기가 private볼 수 있게.
	friend class UMapVerifier;
	// Transient : 레벨에 저장하지 않음(판마다 새로 만들어서) 
	UPROPERTY(Transient)
	// 검사기 주소를 담는 칸. 검사기쪽 Map과 서로 반대 방향
	// (맵->검사기,검사기-> 맵)
	TObjectPtr<class UMapVerifier> Verifier;

	// 보이는 것 목록(데이터 에셋 DA_MapAssets). 타일 BP·머티리얼·산·풀·시설 레벨을 여기서 고른다.
	// 비어 있거나 못 찾으면 MapAssetSet.h 의 C++ 기본값(예전 경로)을 쓴다.
	UPROPERTY(EditAnywhere, Category = "Map Assets")
	TSoftObjectPtr<class UMapAssetSet> MapAssets;
	// 한 번 불러온 목록을 들고 있는 칸(판마다 새로).
	UPROPERTY(Transient)
	TObjectPtr<class UMapAssetSet> LoadedMapAssets;
	// 목록 읽기. 일꾼들도 Map->GetMapAssets() 로 읽는다.
	const UMapAssetSet& GetMapAssets();
	// 판 시작 때 맵에 붙은 그릇(땅판·산·돌·풀 HISM)에 메시·머티리얼을 끼운다.
	void ApplyMapAssets();

	// 협력객체 : 건물 자리 담당. 고른 결과는 맵의 FacilityPlacements 에 넣는다.
	friend class UMapFacilityPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapFacilityPlanner> FacilityPlanner;

	// 협력객체 : 칸 모양 담당(칸마다 어떤 타일, 몇 도). 결과는 TileDesignPlacements·LayoutHash.
	friend class UMapTilePlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapTilePlanner> TilePlanner;

	// 협력객체 : 흙길 담당(시설·시작점·출구를 도로·워존에 잇는 흙길). 칸 모양 담당이 부른다.
	friend class UMapRoadPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapRoadPlanner> RoadPlanner;

	// 협력객체 : 공사 담당.
	friend class UMapTileSpawner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapTileSpawner> TileSpawner;

	// 협력객체 : 바닥 담당.
	friend class UMapGroundBuilder;
	UPROPERTY(Transient)
	TObjectPtr<class UMapGroundBuilder> GroundBuilder;

	// 협력객체 : 시작 구역 담당(멀티 최대 4명 — 가장자리에 시작 대기소를 더 고른다).
	friend class UMapSpawnRegionPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapSpawnRegionPlanner> SpawnRegionPlanner;

	// 협력객체 : 지점 담당(시작·상자·몬스터·출구·퀘스트 자리 찍기 + 끼임 정리). 결과는 LevelDesignPoints.
	friend class UMapPointPlanner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapPointPlanner> PointPlanner;

	// 협력객체 : 아이템 담당(상자 자리마다 바닥 아이템 놓기).
	friend class UMapItemSpawner;
	UPROPERTY(Transient)
	TObjectPtr<class UMapItemSpawner> ItemSpawner;

	// ---- 기획 숫자(에디터에서 맵 액터를 골라 바꾼다) ----
	// 워존에 놓는 2×2 마당 시설 수.
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WarZoneYardCount = 3;
	// 들판에 놓는 언덕(땅 모양) 최대 수.
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "0", ClampMax = "100"))
	int32 MaxTerrainFeatureCount = 28;
	// 몬스터 길찾기 막힘을 갱신하는 반경(cm). 플레이어 주변·맵 가운데 둘 다 이 값.
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "1000"))
	float NavigationBlockerRadiusCm = 6000.0f;
	// 상자 자리 하나가 "상자 하나(뽑힌 아이템 전부 안에)" 가 될 확률. 나머지는 자리 둘레 바닥에 아이템을 흩어 놓는다.
	// 예: 0.6 = 상자 자리 10곳 중 6곳쯤은 상자. 같은 시드면 같은 자리가 같은 쪽으로 정해진다(자리 씨앗 주사위).
	UPROPERTY(EditAnywhere, Category = "Design Numbers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LootCrateChance = 0.6f;

	// ---- 시작 구역(멀티) ---- 고르는 규칙은 MapSpawnRegionPlanner.cpp 주석.
	// 시작 구역 최대 수. 1번은 형님 생성기가 준 시작 칸, 나머지는 가장자리 빈 땅에서 더 고른다. 1 이면 예전처럼 한 곳.
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SpawnRegionCount = 4;
	// 시작 구역끼리 최소 거리(칸, 1칸 = 20m). 가까우면 나오자마자 마주친다.
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "2"))
	int32 MinSpawnRegionSpacingCells = 10;
	// 시작 구역과 출구 사이 최소 거리(칸). 나오자마자 탈출하는 걸 막는다.
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "0"))
	int32 MinSpawnToExitCells = 5;
	// 맵 가장자리에서 이 칸 수 안쪽만 후보(출발은 바깥, 워존은 가운데).
	UPROPERTY(EditAnywhere, Category = "Spawn Regions", meta = (ClampMin = "1"))
	int32 SpawnRegionEdgeBandCells = 5;
	// 이번 판 시작 구역 칸(0번 = 형님 시작 칸).
	UPROPERTY(VisibleInstanceOnly, Category = "Spawn Regions")
	TArray<FIntPoint> SpawnRegionCells;
	
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GroundHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WarZoneGroundHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TransitionGroundHISM;

	// 가장자리 호수: 꺼진 바닥과 그 위의 물 판.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LakeBedHISM;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LakeWaterHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MountainHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RoadSurfaceHISM;

	// 손으로 깎은 여러 칸짜리 땅 모양. 메시마다 둘레 전체는 평평하고 안쪽만 휘게 만들어서
	// 가장자리를 맞출 필요 없이 예약된 칸에 그냥 놓는다:
	// 이웃 타일은 모두 공통 높이에서 만난다.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TerrainFeatureHISMs;

	// 그 땅 모양 위의 꾸미기. 땅 모양 칸에는 타일 액터가 없어서 이것들이 유일한 소품이고,
	// 손으로 만든 높이 정보에서 바로 높이를 읽어 놓는다.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainRockHISM;

	// 얕은 물에 세우는 바위·갈대. 칸 따라 반듯한 물가 선을 흐트러뜨리는 용도.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShoreRockHISM;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShoreReedHISM;

	// 일자 / 바깥 모서리 / 안쪽 모서리, 이 순서.
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ShoreTransitionHISMs;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainTreeHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainBushHISM;

	// 안 보이는 Static 바닥 하나가 Recast 의 입력이다. 칸마다의 보이는 모양·충돌은
	// HISM 에 두어서, 길찾기용 모양을 수천 개 뽑아내지 않게 한다.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UStaticMeshComponent> NavigationFloor;

	// 코드로 만든 소수의 단상/경사로/계단 컴포넌트가 큰 높이차와 길찾기를 맡는다.
	// 그래야 20 m 칸 하나하나가 다 길찾기 입력이 되지 않는다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ElevatedTerrainComponents;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;

	// 작은 지역 장애물 묶음 두 개로 Recast 가 전술용 벽을 알게 한다.
	// 런타임 타일 2,000개를 한꺼번에 길찾기용으로 뽑아내지 않기 위해서다.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UTacticalTileNavModifierComponent> CenterNavigationBlockers;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UTacticalTileNavModifierComponent> PlayerNavigationBlockers;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UPCGComponent> DressingPCGComponent;

	UPROPERTY(VisibleInstanceOnly, Category = "Footprint Preview")
	TArray<TObjectPtr<AMapTile>> ReservedTiles;

	UPROPERTY(VisibleInstanceOnly, Category = "Footprint Preview")
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleInstanceOnly, Category = "Footprint Preview")
	float GridStep = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Footprint Preview", meta = (AllowPrivateAccess = "true"))
	TArray<FFacilityPlacement> FacilityPlacements;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Design Placements", meta = (AllowPrivateAccess = "true"))
	TArray<FTileDesignPlacement> TileDesignPlacements;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Design Points", meta = (AllowPrivateAccess = "true"))
	TArray<FLevelDesignPoint> LevelDesignPoints;

	bool bLevelDesignPointsBuilt = false;

	// 만들어진 격자의 칸 수. 짐작하지 않고 논리 단계가 실제로 만든 타일에서 잰다.
	// UMapGeneratorComponent::_mapSize 는 바꿀 수 있으므로, 뒤 단계 어디서도
	// 45칸 / 900 m 세계를 고정값으로 쓰면 안 된다.
	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	int32 GridCellSpan = 45;

	// 이번 판에 가장자리 호수가 놓인 곳(칸 좌표). 어느 모서리일지는 시드마다 도로망을 보고
	// 정하므로, 물을 바라봐야 하는 것 - 마을의 보트 상륙 시작 지점 같은 것 - 은
	// 방향을 짐작하지 말고 이 값을 읽어야 한다.
	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	FVector2D BorderLakeCentreCell = FVector2D::ZeroVector;

	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	bool bHasBorderLake = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	uint32 LayoutHash = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	uint32 GameplayPointHash = 0;

	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bUseRuntimeBlueprintTiles = true;

	// 기본은 꺼짐: 이 검사는 완성된 세계의 모든 인스턴스를 다 돈다.
	// z-fighting(겹쳐서 깜빡임)을 찾을 때 켠다.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bRunCoplanarSurfaceAudit = false;

	// Downtown 팩의 배경 산 메시로 맵을 빙 둘러 세계가 분지처럼 보이게 한다:
	// 타일 맵은 골짜기 바닥, 사방 지평선엔 산.
	// 보이기만 하고 일부러 충돌이 없다 - 타일 밖으로 나가면 예전처럼 떨어진다.
	// 배치는 판의 시드로 정해지므로 모든 클라이언트가 복제할 것 없이
	// 똑같은 산 고리를 계산한다.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bBuildBorderMountains = true;

	// 손으로 만든 시설 블루프린트가 있으면 코드로 만드는 AProceduralFacilityActor 대신 그걸 놓는다.
	// 끄면 모든 시설이 한 번에 코드 빌더로 돌아간다.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bUseAuthoredFacilityBlueprints = true;

	// 시설이 포장된 진입로를 찾아 뻗을 수 있는 거리(20 m 칸 단위). 이 거리 안에 도로가 없는
	// 시설은 일부러 포장 안 한 채로 둔다. 0 이면 시설 갈래길을 아예 없애고
	// 시작/탈출 길과 WarZone 길만 남긴다.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0", ClampMax = "20"))
	int32 MaxFacilitySpurCells = 6;

	// 갈래길 탐색이 포기하기 전까지 보는 거리. 위 거리 제한과는 따로다. 탐색이 제한보다 더
	// 멀리 봐야 포장 안 된 시설이 실제로 얼마나 멀었는지 로그에 남길 수 있다
	// - 그 숫자가 없으면 제한값을 감으로만 정해야 한다.
	// 진단용 거리일 뿐이고, 이 값만으로 길을 깔지는 않는다.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "1", ClampMax = "40"))
	int32 MaxFacilitySpurSearchCells = 20;

	// 시설 배치가 원래 테마상 목표 위치를 두고 도로망 쪽으로 얼마나 끌려가는지.
	// MaxFacilitySpurCells 를 넘는 거리만 계산에 넣으므로, 0 이면 예전처럼 목표 위치만 보고,
	// 값이 클수록 구역이 원래 있어야 할 자리를 내주고 도로 옆 자리를 택한다.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0", ClampMax = "8"))
	int32 FacilityRoadProximityWeight = 1;

	// 가장자리 호수의 반지름(20 m 칸 단위). 맵 한 모서리 바로 바깥 점에서 잰다.
	// 0 이면 호수를 아예 끈다: 물 칸도, 물가 메시도 없고,
	// 시골 마을은 내륙 자리로 대신 간다.
	//
	// 기본은 꺼짐. 위에서 보면 호수가 보기 좋지만, 20 m 타일 격자에 높이 변화를 맞추다 보니
	// 물가에서 새 이음새가 계속 생겼고, 호수 없이도 맵은 문제없이 돌아가는 상태다.
	// 다시 작업하려면 9 로 두면 된다 - 그 전에 PG.BuildShoreMeshes 를
	// 돌려서 물가 메시를 저장해 둬야 한다.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0.0", ClampMax = "14.0"))
	float BorderLakeRadiusCells = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedRuntimeTiles;

	// 런타임 타일 블루프린트는 정해진 순서로 다 만든 뒤 메시별 HISM 묶음으로 바꾼다.
	// 보이는 결과는 그대로 두면서, 클라이언트마다 계속 남는 타일 액터 약 2천 개와
	// 씬 컴포넌트 수만 개를 없앤다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RuntimePackedVisualHISMs;

	// manifest 시설 배치 하나마다 이 컴퓨터에서만 보이는 Level Instance 하나.
	// 같은 손작업 시설 맵이 회전만 다르게 여러 번 나올 수 있다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> FacilityDesignLevelInstances;

	UPROPERTY(Transient)
	TObjectPtr<UPCGGraph> RuntimeDressingGraph;

	TSet<int32> LoggedFacilityDesignLevelIndices;
	// 길찾기 검사 끝남
	bool bLoggedAllFacilityDesignLevelsLoaded = false;
	bool bResolvedGameplayPointSafety = false;
	double LastPlayerNavigationBlockerUpdateTimeSeconds = -BIG_NUMBER;
	double NavigationValidationStartTimeSeconds = 0.0;
	FIntPoint LastPlayerNavigationBlockerCell = FIntPoint(MAX_int32, MAX_int32);
	TArray<double> FacilityLoadRequestTimeSeconds;

	FTimerHandle RetryTimer;

	// 들어온 사람이 설계도로 받은 값(서버에서는 안 씀).
	bool bHasReplicatedManifest = false;
	int64 ReplicatedRaidSeed = 0;
	int32 ReplicatedStartRange = 4;
	// 서버: 설계도를 이미 만들었나(쪽지를 읽는 재시도 때 두 번 만들지 않게).
	UPROPERTY(Transient)
	TObjectPtr<class AMapManifestActor> Manifest;
};
