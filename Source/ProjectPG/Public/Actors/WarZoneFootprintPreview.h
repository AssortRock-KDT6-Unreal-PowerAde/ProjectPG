// ProjectPG-only visualization for multi-cell level-design footprints.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "LevelDesign/PGLevelDesignTypes.h"
#include "LevelDesign/PGMapInfo.h"
#include "WarZoneFootprintPreview.generated.h"

class AMapTile;
class ANavigationData;
class APawn;
class ALevelDesignValidationCharacter;
class ATacticalTileActor;
class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class ULevelStreamingDynamic;
class UNavigationInvokerComponent;
class UPCGComponent;
class UPCGGraph;
class UParticleSystem;
class APlayerController;
class UStaticMeshComponent;
class UTacticalTileNavModifierComponent;
class UWorld;


UCLASS()
class PROJECTPG_API AWarZoneFootprintPreview : public AActor, public IPGMapInfo
{
	GENERATED_BODY()

public:
	AWarZoneFootprintPreview();

	// ---- IPGMapInfo (맵 정보 약속) — 다른 시스템은 이 클래스가 아니라 UPGMapInfoSubsystem::FindMap 으로 이것만 본다 ----
	// 데이터 전용 계약. 스폰·루팅 시스템이 읽기만 한다.
	virtual const TArray<FLevelDesignPoint>& GetLevelDesignPoints() const override { return LevelDesignPoints; }
	virtual bool AreLevelDesignPointsBuilt() const override { return bLevelDesignPointsBuilt; }
	virtual FOnLevelDesignPointsBuilt& OnLevelDesignPointsBuiltEvent() override { return OnLevelDesignPointsBuilt; }

	// 워존 바닥 칸들의 가운데(월드 좌표). 거래소 부스를 워존 끝 네 방향에 나눠 세울 때 쓴다(9/23, UPGObjectSpawnerSubsystem::SpawnBooths).
	virtual TArray<FVector> GetWarZoneCellCentres() const override
	{
		TArray<FVector> Out;
		for (const FTileDesignPlacement& Placement : TileDesignPlacements)
			if (Placement.Visual == ETileDesignVisual::WarZoneGround)
				Out.Add(Placement.WorldLocation);
		return Out;
	}
	virtual FVector GetMapCentre() const override { return GetActorLocation(); }

	FOnLevelDesignPointsBuilt OnLevelDesignPointsBuilt;

	// 한 구역을 통째로 박살내 구덩이로 만든다(피날레: 드래곤이 솟는 자리).
	// 떨림 → 가운데부터 터지며 조각이 튀고 기울며 꺼짐 → 지우기. 그 밑에는 산 재질의 구덩이 바닥·벽을 깐다.
	// 자세한 이유는 WarZoneFootprint/WarZoneFootprintPreview_Collapse.cpp 주석 참고.
	virtual void CollapseRegion(const FVector& WorldCentre, float RadiusCm, float Seconds = 3.0f) override;
	virtual bool IsCollapsing() const override { return bCollapsing; }
	// 멀티(9/27): 무너짐을 모든 컴퓨터에서 돌린다. 땅 타일·시설 레벨은 컴퓨터마다 각자 지은 것이라 서버에서만 무너뜨리면
	//   클라이언트에는 땅과 건물이 그대로 남았다(발밑은 서버 기준으로 꺼져 허공을 걷는다). CollapseRegion 이 서버에서 이걸 부른다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastCollapseRegion(FVector_NetQuantize WorldCentre, float RadiusCm, float Seconds);

	// 이번 판 시드. 서버는 게임모드에서, 클라이언트는 서버가 보낸 설계도(GridManifest)에서 읽는다 — 양쪽이 같은 값이어야 같은 맵이 된다.
	int64 GetRaidSeed() const;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	// ---- 멀티: 맵 설계도 (2026-09-27) ----
	// 서버가 논리 격자를 읽은 순간 채운다 → 모든 클라이언트에 복제 → 클라이언트는 받자마자 같은 맵을 스스로 짓는다(BuildFromManifest).
	// 전에는 맵을 서버에서만 지어서, 전용 서버(Play As Client)로 접속한 사람 화면에 바닥·길·시설이 하나도 없었다(9/27 사용자 시험).
	UPROPERTY(ReplicatedUsing = OnRep_GridManifest)
	FPGLogicalGridManifest GridManifest;
	UFUNCTION()
	void OnRep_GridManifest();
	// 서버: 논리 칸 액터에서 설계도를 채운다.
	void FillGridManifest(const TMap<FIntPoint, AMapTile*>& TileByCell, float TileZ);
	// 클라이언트: 설계도대로 숨긴 칸 사본을 깔고 서버와 같은 맵 짓기(TryReserveFootprint)를 돌린다. 한 번만.
	void BuildFromManifest();
	bool bBuiltFromManifest = false;
	// 클라이언트가 깐 칸 사본(서버의 팀원 생성기가 만든 칸과 같은 자리·종류, 이 컴퓨터에만 있다).
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMapTile>> ClientGridTiles;

	// 멀티: 이 컴퓨터 사람의 "판 시작" 처리(로딩 화면 걷기·"게임이 시작되었습니다"·마우스 모드 되돌리기).
	// 혼자 하는 판은 StartSinglePlayerValidation 이 하고, 멀티(서버와 클라가 다른 컴퓨터)는 각자 여기서 한다 — 로딩 화면과
	// 입력 모드는 그 사람 컴퓨터에만 있다. 전에는 서버에서만 불러서 클라에는 안내도 없고, 흐름 화면에서 넘어오면 마우스를 눌러야
	// 시야가 돌았다(9/22 버그 재발). 맵이 다 지어지고 자기 캐릭터를 받은 뒤 한 번.
	void TickLocalPlayerReady();
	bool bLocalPlayerReady = false;
	double LocalPawnSeenAt = -1.0;
	// 클라이언트: 서버가 문 액터로 바꾼 시설 레벨의 원래 문 메시 숨기기(PGLevelDoorConverter::HideConvertedDoorsOnClient).
	// 시설 레벨이 하나씩 로드되므로 2초마다, 다 로드된 뒤 한 번 더 하고 멈춘다.
	double NextClientDoorHideAt = 0.0;
	bool bClientDoorsDone = false;
	int32 ClientDoorPassesAfterLoad = 0;
	int32 ClientDoorsHidden = 0;

	// 책임별 협력 객체를 만든다(BeginPlay 맨 앞). 맵 액터는 이들을 순서대로 부르기만 한다.
	void CreateCollaborators();
	// 출격 로딩 화면을 시설 레벨이 다 올라와 보일 때 걷는다(최대 20초). 걷히면 "게임이 시작되었습니다" 를 띄운다. (_PlayerStart.cpp)
	void HideLoadingWhenFacilitiesVisible(const TCHAR* Why);
	// ---- 협력 객체: 맵 그리기 (PGMapVisualBuilder.h) ----
	friend class UPGMapVisualBuilder;
	UPROPERTY(Transient)
	TObjectPtr<class UPGMapVisualBuilder> VisualBuilder;
	// ---- 협력 객체: 세우기 — 타일·시설 스폰, 시설 레벨 불러오기, 길찾기 막이 (PGMapTileSpawner.h, 9/28) ----
	friend class UPGMapTileSpawner;
	UPROPERTY(Transient)
	TObjectPtr<class UPGMapTileSpawner> TileSpawner;
	// ---- 협력 객체: 게임 지점 — 시작·출구·루팅·몬스터·퀘스트 자리 (PGGameplayPointBuilder.h, 9/28) ----
	friend class UPGGameplayPointBuilder;
	UPROPERTY(Transient)
	TObjectPtr<class UPGGameplayPointBuilder> PointBuilder;
	// ---- 협력 객체: 구역 붕괴 (PGRegionCollapse.h) ----
	friend class UPGRegionCollapse;
	UPROPERTY(Transient)
	TObjectPtr<class UPGRegionCollapse> Collapse;
	// ---- 협력 객체: 맵 자체 검사 (PGMapVerifier.h) ----
	friend class UPGMapVerifier;
	UPROPERTY(Transient)
	TObjectPtr<class UPGMapVerifier> Verifier;
	// ---- 설계도 만들기 단계들 (WarZoneFootprintPreview_Layout.cpp, 9/28 BuildTileDesignPlacements 나누기) ----
	friend struct FPGLayoutPlanner;
	bool bCollapsing = false;
	// 무너진 자리(XYZ 중심, W 반지름). 무너진 뒤에 걸어 들어온 몹·굴러온 물건을 치운다(SweepCollapsedAreas).
	// 왜 필요하나: 길찾기용 투명 바닥판(NavigationFloor)은 맵 전체 한 장이라 구멍을 못 낸다. 그 위에 몹이 서서 허공에 떠 있었다(9/22).
	TArray<FVector4> CollapsedAreas;
	// 무너진 칸(9/28 칸 모양 구덩이). 구덩이 위 몹·물건 치우기가 원 대신 이것을 본다.
	TSet<FIntPoint> CollapsedCells;
	// 조명 그림자를 이미 정리한 시설 레벨 번호(9/28 — .cpp TrimFacilityLightShadows).
	TSet<int32> LightTrimmedFacilityIndices;
	double NextCollapsedSweepAt = 0.0;

	void TryReserveFootprint();
	// TryReserveFootprint 의 단계(9/28 떼어 냄).
	bool ReadLogicalGrid(TArray<AMapTile*>& AllTiles, TMap<FIntPoint, AMapTile*>& WarZoneByCell, TMap<FIntPoint, AMapTile*>& TileByCell);
	bool ReserveWarZoneFacilities(const int32 AllTileCount, TMap<FIntPoint, AMapTile*>& WarZoneByCell, TMap<FIntPoint, AMapTile*>& TileByCell, TSet<FIntPoint>& SelectedFacilityCells, int32& OutCampCount);
	bool ReserveExitCheckpoint(TMap<FIntPoint, AMapTile*>& TileByCell, TSet<FIntPoint>& SelectedFacilityCells);
	void ReserveThemedDistricts(TMap<FIntPoint, AMapTile*>& TileByCell, TSet<FIntPoint>& SelectedFacilityCells);
	void BuildTileDesignPlacements(const TMap<FIntPoint, AMapTile*>& TileByCell);
	// Entrance cell / outward cardinal direction for every ramp-and-stair approach a
	// facility owns. Shared by the terrain builder and VerifyTraversableElevation.
	void GetFacilityAccessEdges(
		const FFacilityPlacement& Placement,
		TArray<TPair<FIntPoint, FIntPoint>>& OutAccessEdges) const;
	float GetSurfaceElevationForCell(const FIntPoint& Cell) const;
	// Chooses a shore mesh variant and quarter-turn per land cell touching the lake.
	// Value is (variant index, quarter turns).
	void BuildShoreTransitionMap(
		const TSet<FIntPoint>& LakeCells,
		TMap<FIntPoint, TPair<int32, int32>>& OutShoreTileByCell) const;
	// 호수 마을의 나룻배를 연료통 탈출구로(시설 레벨이 보일 때 한 번). cpp 주석 참고.
	void AttachBoatExits(const class ULevel* Level);
	// 바깥 호숫가에 나룻배를 띄우고 연료통 탈출구로 만든다(맵을 만든 직후 한 번). cpp 주석 참고.
	void SpawnLakeBoats();
	// 시작 지점 둘레를 비워 넓은 집결지로 만든다(담장·소품·나무를 걷어 낸다). cpp 주석 참고.
	void ClearSpawnSurroundings();
	// 네모 구역들 안의 키 큰 타일 소품(담·나무·바위·폐차)을 걷어 낸다. 바닥 판·길은 남긴다. 걷어 낸 인스턴스 수를 돌려준다.
	// 시작 지점 비우기와 외진 보상 거점이 같이 쓴다.
	int32 RemoveTallDressingInZones(const TArray<FBox2D>& Zones, int32& OutTouchedComponents);
	// 외진 보상 거점: 길·시작·출구·호수·시설이 없는 빈 모서리 중 시작 지점에서 가장 먼 곳에 추락 헬기 캠프를 세운다. cpp 주석 참고.
	void PlaceRemoteOutpost(const TMap<FIntPoint, AMapTile*>& TileByCell);
	// 끄면 외진 보상 거점을 세우지 않는다.
	UPROPERTY(EditAnywhere, Category = "PG|RemoteOutpost")
	bool bPlaceRemoteOutpost = true;
	// 이번 판에 세운 거점(없으면 비어 있다). 월드 루팅은 액터를 직접 찾으므로 이것은 중복 생성 방지용.
	UPROPERTY(Transient)
	TObjectPtr<class APGRemoteOutpostActor> RemoteOutpost;
	// 시작 칸 가운데에서 이 반폭(cm) 네모 안을 비운다. 0 이면 끈다.
	// 9/22 에 2000(40m) 으로 켰다가 같은 날 사용자 결정으로 0: "스폰 지역은 전의 알고리즘(담장 구역)이 훨씬 낫다" —
	//   담장을 걷자 워존이 훤히 보여 가까워 보였다. 코드는 남겨 둔다(값만 넣으면 다시 켜진다).
	UPROPERTY(EditAnywhere, Category = "PG|Spawn")
	float SpawnClearHalfExtentCm = 0.0f;
	// 논리 격자가 준 출구에 더해 맵 가장자리에 세울 출구 수. 0 이면 끈다(위와 같은 날 같은 결정으로 되돌림).
	UPROPERTY(EditAnywhere, Category = "PG|Exits")
	int32 ExtraExitCount = 0;
	// 호숫가 배를 몇 척까지 띄우나. 서로 80m 넘게 떨어뜨린다.
	UPROPERTY(EditAnywhere, Category = "PG|Lake")
	int32 LakeBoatCount = 3;

	// ---- 여러 시작 지역 (멀티, 9/26) ---- 자세한 이유는 WarZoneFootprintPreview_SpawnRegions.cpp 맨 위 주석.
	// 시작 지역 최대 수. 1번은 논리 격자(팀원 생성기)가 준 시작 칸이고, 나머지는 우리 층이 가장자리 길 칸에서 더 고른다.
	// 4 = 최대 4명이 각자 다른 곳에서 출발(타르코프식). 1 이면 예전처럼 한 곳.
	UPROPERTY(EditAnywhere, Category = "PG|Spawn", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SpawnRegionCount = 4;
	// 켜면 모두 1번 지역에서 같이 출발(한 분대, 자리 4개). 끄면 들어온 순서대로 지역을 하나씩 나눠 준다.
	UPROPERTY(EditAnywhere, Category = "PG|Spawn")
	bool bSquadSharesSpawnRegion = false;
	// 시작 지역끼리 최소 거리(칸, 1칸 = 20m). 가까우면 출발하자마자 마주친다.
	UPROPERTY(EditAnywhere, Category = "PG|Spawn", meta = (ClampMin = "2"))
	int32 MinSpawnRegionSpacingCells = 10;
	// 시작 지역과 출구 사이 최소 거리(칸). 태어나자마자 탈출하는 걸 막는다.
	UPROPERTY(EditAnywhere, Category = "PG|Spawn", meta = (ClampMin = "0"))
	int32 MinSpawnToExitCells = 5;
	// 맵 가장자리에서 이 칸 수 안쪽만 시작 지역 후보(출발은 바깥, 워존은 가운데라는 판 구조를 지킨다).
	UPROPERTY(EditAnywhere, Category = "PG|Spawn", meta = (ClampMin = "1"))
	int32 SpawnRegionEdgeBandCells = 5;
	// 이번 판 시작 지역 칸(0번 = 논리 격자의 시작 칸). 서버·클라가 같은 설계도에서 같은 값을 얻는다.
	TArray<FIntPoint> SpawnRegionCells;
	// 이미 시작 자리에 세운 플레이어(늦게 들어온 사람만 새로 세운다).
	TSet<TWeakObjectPtr<APlayerController>> PlacedPlayers;
	// 들어온 순서 번호. 지역·자리 배정에 쓴다.
	int32 NextPlayerJoinIndex = 0;
	// 캐릭터 없이 들어와 있는 사람을 처음 본 시각(게임모드가 곧 만들 수도 있어 잠깐 기다렸다가 우리가 자리에 만든다).
	TMap<TWeakObjectPtr<APlayerController>, double> PawnlessSinceSeconds;
	// 설계도(TileDesignPlacements)에서 시작 지역을 고른다. 게임 지점을 만들기 전에 한 번.
	// 9/28: 설계도의 길을 깔기 전에 논리 칸으로 고른다(길을 워존까지 이어 주려고). BlockedCells = 시설·지형 굴곡·이미 진입로인 칸.
	void BuildSpawnRegions(const TMap<FIntPoint, AMapTile*>& TileByCell, const TSet<FIntPoint>& BlockedCells,
		const FVector2D& LakeCentreCell, float LakeRadiusCells);
	bool IsSpawnRegionCell(const FIntPoint& Cell) const { return SpawnRegionCells.Contains(Cell); }
	// 서버: 폰이 준비된 플레이어를 지역·자리에 세운다. 매 틱 불러도 새로 들어온 사람만 처리한다.
	void PlaceJoinedPlayers();

	// 시작 칸(출발 구역 담장)의 출구가 보는 방향(도). 시작 칸이 아니면 false. 9/28 "시작할 때 벽 보고 선다".
	bool GetSpawnOpeningYaw(const FIntPoint& Cell, float& OutYaw) const;
	// From 자리에서 그 시작 칸 출구 문 쪽으로 가장 멀리 트인 방향(출구 방향 ±45도 안). 시작 칸이 아니면 false.
	bool GetSpawnDoorwayYaw(const FIntPoint& Cell, const FVector& From, float& OutYaw, const AActor* Ignore = nullptr) const;
	// 시작 방향 확인용: 이 자리 눈높이에서 Yaw 쪽으로 막힘 없이 몇 cm 트였나(최대 MaxCm).
	float MeasureOpenAhead(const FVector& From, float Yaw, float MaxCm, const AActor* Ignore, FString* OutBlocker = nullptr) const;
	// 검증용 플레이어 폰: 검증 캐릭터를 만들었으면 그것, 아니면 게임모드가 준 플레이어 폰(팀 캐릭터 등).
	APawn* GetValidationPlayerPawn() const;
	void StartSinglePlayerValidation();
	// 스타터 지역: 플레이어 시작 자리 앞에 연료통과 변신 여고생 NPC 를 둔다(9/20 사용자 결정). 서버에서 판마다 시작 지역마다 한 번(9/27 멀티).
	void SpawnStarterTransformKit(const FVector& PlayerStart, float PlayerYaw);
	// 시작 자리 주변에서 사람 캡슐이 들어가는 땅 한 점을 찾는다(벽 속·건물 안에 두지 않게).
	bool FindStarterGround(const FVector& Near, FVector& OutGround, bool bCheckOverlap = true) const;
	// 마지막으로 스타터 자리를 막은 것(진단용 로그)
	mutable FString LastStarterBlocker;
	void TryIssueSinglePlayerValidationMove();
	void DrawReservation() const;
	void ConfigureProxyMesh(
		UStaticMeshComponent* Component,
		const FVector& RelativeLocation,
		const FVector& Size);
	void ShowWarehouseProxy(const FVector& FootprintCenter);
	void ShowYardProxy(const FVector& FootprintCenter);
	void ReserveFacility(
		EFacilityVisualSet VisualSet,
		const FIntPoint& Anchor,
		const FIntPoint& Footprint,
		int32 RotationQuarterTurns,
		const TArray<FIntPoint>& OccupiedCells,
		const TMap<FIntPoint, AMapTile*>& TileByCell);
	FVector GetFootprintCenter(const FFacilityPlacement& Placement) const;
	FVector GetDesignFootprintCenter(const FFacilityPlacement& Placement) const;
	void DrawDesignScalePreview() const;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> FloorProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> BackWallProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> LeftWallProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> RightWallProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> FrontWallLeftProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> FrontWallRightProxy;

	UPROPERTY(VisibleAnywhere, Category = "Warehouse Proxy")
	TObjectPtr<UStaticMeshComponent> RoofProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardFloorProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverNorthProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverSouthProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverWestProxy;

	UPROPERTY(VisibleAnywhere, Category = "Yard Proxy")
	TObjectPtr<UStaticMeshComponent> YardCoverEastProxy;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GroundHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> WarZoneGroundHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TransitionGroundHISM;

	// Border lake: a sunken bed and the water sheet above it.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LakeBedHISM;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> LakeWaterHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> MountainHISM;

	// 맵 둘레 바닥판(치마). BuildBorderMountains 가 판마다 새로 채운다.
	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> GroundSkirtHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> RoadSurfaceHISM;

	// Sculpted multi-cell ground features. Each mesh is authored flat around its whole
	// perimeter and only curves inside, so it drops into reserved cells without any
	// edge matching: every neighbouring tile still meets it at the shared surface.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> TerrainFeatureHISMs;

	// Dressing for those features. Terrain cells carry no tile actor, so these are the
	// only props on them and they are sampled straight off the authored height field.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainRockHISM;

	// Rocks and reeds standing in the shallows, used to break up the cell-aligned
	// waterline.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShoreRockHISM;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> ShoreReedHISM;

	// Straight / outer corner / inner corner, in that order.
	UPROPERTY(VisibleAnywhere)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> ShoreTransitionHISMs;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainTreeHISM;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> TerrainBushHISM;

	// One invisible static surface feeds Recast. Per-cell visuals/collision stay
	// on HISM, avoiding thousands of navigation geometry exports.
	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UStaticMeshComponent> NavigationFloor;

	// A small number of code-authored platform/ramp/stair components provide true
	// macro elevation and navigation without turning every 20m visual cell into a
	// navigation source.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> ElevatedTerrainComponents;

	UPROPERTY(VisibleAnywhere, Category = "Design World")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;

	// Two small local obstacle sets keep Recast aware of tactical walls without
	// exporting all 2,000 runtime tiles at once.
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

	// Generated = 지점 목록이 만들어짐. Built = 시설 레벨이 다 뜨고 벽 속 지점을 옮긴 뒤 소비자(스포너)에게 넘겨도 되는 상태.
	bool bLevelDesignPointsGenerated = false;
	float LevelDesignPointsGeneratedTime = 0.0f;
	bool bLevelDesignPointsBuilt = false;

	// Cells across the generated grid, measured from the tiles the logical layer
	// actually produced rather than assumed. UMapGeneratorComponent::_mapSize is
	// editable, so nothing downstream may hardcode a 45-cell / 900 m world.
	UPROPERTY(VisibleInstanceOnly, Category = "Design Placements")
	int32 GridCellSpan = 45;

	// Where the border lake ended up this raid, in cell coordinates. The corner is
	// chosen against the road network per seed, so anything that wants to face the
	// water - the hamlet's boat-landing spawn point - must read this rather than
	// assume a direction.
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

	// Off by default: the audit walks every instance in the finished world. Turn it
	// on when hunting z-fighting.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bRunCoplanarSurfaceAudit = false;

	// Ring the map with the Downtown pack's background-mountain mesh so the world
	// reads as a basin: tile map on the valley floor, mountains on every horizon.
	// Purely visual and deliberately collision-free - leaving the tiles still ends
	// in a fall, exactly as before. Placement is derived from the raid seed, so
	// every client computes the identical ring with nothing to replicate.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bBuildBorderMountains = true;
	// 맵 둘레에 산 재질의 바닥판을 깔아 타일 끝과 산 사이 틈(아래 하늘이 비치는 곳)을 메운다(9/21 사용자: 공중에서 경계가 티 난다).
	// 산은 둥글게, 맵은 네모라 모서리·변에 산이 안 닿는 틈이 생긴다. 판 한 장 크기(cm)와 타일 끝에서 바깥으로 까는 폭(cm).
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bBuildGroundSkirt = true;
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "1000.0"))
	float GroundSkirtWidthCm = 70000.0f;
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "1000.0"))
	float GroundSkirtTileCm = 5000.0f; // 9/22: 200m -> 50m. 드래곤 등장 때 구역과 함께 무너지려면 판이 작아야 한다(맵 폭 600m 를 나눠떨어지게)

	// 맵 밖으로 못 나가게 하는 경계.
	//
	// 안개는 **가리기만 하고 막지는 못한다.** 날 수 있는 차가 경계를 넘어 산 너머로 나가면
	// 아무것도 없는 곳에 떨어진다(9/21 사용자 요구).
	//
	// 막는 상자를 세우지 않는 이유는 cpp 주석에 있다 — 채널로 막으면 드래곤도 같이 막힌다.
	// 대신 조종 중인 폰만 Tick 에서 안쪽으로 되민다.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bBuildMapBoundaryWall = true;
	// 이 높이까지만 되민다(cm). 그보다 위는 피날레의 배가 지나는 하늘이라 건드리지 않는다.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	float BoundaryWallHeightCm = 60000.0f;
	// 시작 연료통 다시 놓기. 줍거나(인벤토리로) 터지면 몇 초 뒤 처음 자리에 새로 놓는다(9/21 사용자: "먹거나 없어지면 다시 스폰").
	// 연료통은 이제 차·총에 맞아 날아가므로 잃어버릴 수 있다 — 시작 동선(변신차 NPC)이 막히면 안 된다.
	// 멀티(9/27): 시작 지역마다 하나씩(지역 순서대로). 한 지역 연료통만 다시 놓이면 그 지역 사람만 유리하다.
	void TickStarterFuelRespawn(float DeltaSeconds);
	struct FStarterFuelSlot
	{
		TWeakObjectPtr<class APGFloorItemActor> Fuel;
		FTransform Home = FTransform::Identity;
		float MissingSeconds = 0.0f;
	};
	TArray<FStarterFuelSlot> StarterFuelSlots;

	// Spawn the hand-authored facility Blueprint wherever one exists instead of the
	// code-built AProceduralFacilityActor. Turn off to put every facility back on
	// the procedural builder in one step.
	UPROPERTY(EditAnywhere, Category = "Design Placements")
	bool bUseAuthoredFacilityBlueprints = true;

	// How far a facility may reach for a paved approach, in 20 m cells. A facility
	// with no road inside this budget is left unpaved on purpose. Set to 0 to drop
	// facility spurs entirely and keep only the Spawn/Exit and WarZone routes.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0", ClampMax = "20"))
	int32 MaxFacilitySpurCells = 6;

	// How far the spur search looks before giving up, independent of the budget
	// above. The search has to run past the budget or the log cannot report how far
	// the unpaved facilities actually were - and without that number the budget can
	// only be guessed at. Purely diagnostic reach; it never lays road by itself.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "1", ClampMax = "40"))
	int32 MaxFacilitySpurSearchCells = 20;

	// How hard facility placement is pulled toward the road network, against its
	// thematic target position. Only distance past MaxFacilitySpurCells is charged,
	// so 0 restores the old target-only behaviour and larger values will trade a
	// district's intended part of the map for a spot beside a road.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0", ClampMax = "8"))
	int32 FacilityRoadProximityWeight = 1;

	// Radius of the border lake in 20 m cells, measured from a point just outside one
	// map corner. 0 disables the lake entirely: no water cells, no shoreline meshes,
	// and the rural settlement falls back to an inland site.
	//
	// Defaulted off. The lake reads well from above but marrying a height change to a
	// 20 m tile grid keeps producing new seams at the waterline, and the map is in a
	// known-good state without it. Set this to 9 to work on it again - the shoreline
	// meshes need PG.BuildShoreMeshes to have been run and saved first.
	UPROPERTY(EditAnywhere, Category = "Design Placements", meta = (ClampMin = "0.0", ClampMax = "14.0"))
	float BorderLakeRadiusCells = 0.0f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedRuntimeTiles;

	// Runtime tile Blueprints are converted into mesh-keyed HISM batches after
	// deterministic construction. This keeps the visual result while avoiding
	// roughly two thousand persistent tile actors and tens of thousands of scene
	// components on every client.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> RuntimePackedVisualHISMs;

	// One local visual Level Instance per manifest facility placement. The same
	// authored facility map may appear several times with different rotations.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULevelStreamingDynamic>> FacilityDesignLevelInstances;


	UPROPERTY(Transient)
	TObjectPtr<ALevelDesignValidationCharacter> ValidationPlayerCharacter;

	UPROPERTY(Transient)
	TObjectPtr<ALevelDesignValidationCharacter> ValidationAICharacter;

	TSet<int32> LoggedFacilityDesignLevelIndices;
	bool bLoggedAllFacilityDesignLevelsLoaded = false;
	bool bLoggedNavigation = false;
	bool bResolvedGameplayPointSafety = false;
	bool bLoggedMissingWarZoneFootprint = false;
	bool bStartedSinglePlayerValidation = false;
	bool bIssuedSinglePlayerValidationMove = false;
	FVector ValidationAIStartLocation = FVector::ZeroVector;
	FVector ValidationAITargetLocation = FVector::ZeroVector;
	double SinglePlayerValidationStartTimeSeconds = 0.0;
	double LastSinglePlayerValidationAttemptTimeSeconds = -BIG_NUMBER;
	double LastPlayerNavigationBlockerUpdateTimeSeconds = -BIG_NUMBER;
	double NavigationValidationStartTimeSeconds = 0.0;
	FIntPoint LastPlayerNavigationBlockerCell = FIntPoint(MAX_int32, MAX_int32);
	TArray<double> FacilityLoadRequestTimeSeconds;

	FTimerHandle RetryTimer;
};
