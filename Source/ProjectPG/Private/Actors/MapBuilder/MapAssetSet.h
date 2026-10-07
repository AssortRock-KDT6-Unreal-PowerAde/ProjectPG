#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Actors/WorldItemActor.h"
#include "Actors/LootCrateActor.h"
#include "MapAssetSet.generated.h"

class UDataTable;
class UMaterialInterface;
class UStaticMesh;

// 맵에 쓰이는 "보이는 것" 목록 (데이터 에셋).
// 게임에서: 숲 타일이 어떤 BP 인지, 땅이 어떤 색 머티리얼인지, 바깥 산·풀이 어떤 메시인지, 창고가 어떤 레벨인지.
// 왜 따로 뺐나: 예전엔 이 경로 77줄이 C++ 에 글자로 박혀 있어서, 숲 모양 하나 바꾸려 해도 코드를 고치고 빌드해야 했다.
//               이제 에디터에서 DA_MapAssets 를 열어 드롭다운으로 고르면 된다(코드·빌드 필요 없음).
// 아래 기본값은 예전 코드에 있던 경로 그대로다. DA_MapAssets 가 없거나 칸이 비어도 예전과 똑같이 돈다.
// 여기 없는 것(엔진 큐브, 언덕 메시, 호숫가 메시)은 코드의 숫자(높이·모양)와 짝이라 C++ 에 남겼다.
UCLASS(BlueprintType)
class UMapAssetSet : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	// ---------- 타일 BP (공사 담당이 칸마다 세운다) ----------
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadCornerTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Corner.BPP_Tile_Road_Corner_C")));
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadStraightTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Straight.BPP_Tile_Road_Straight_C")));
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadTJunctionTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_TJunction.BPP_Tile_Road_TJunction_C")));
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadCrossTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Cross.BPP_Tile_Road_Cross_C")));
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadDeadEndTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_DeadEnd.BPP_Tile_Road_DeadEnd_C")));

	// 시작 대기소(벽 친 칸, 입구 하나), 출구 검문소, 길 막는 장애물.
	UPROPERTY(EditAnywhere, Category = "타일|시작·출구")
	TSoftClassPtr<AActor> SpawnTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Spawn_Staging.BPP_Tile_Spawn_Staging_C")));
	UPROPERTY(EditAnywhere, Category = "타일|시작·출구")
	TSoftClassPtr<AActor> ExitTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Exit_Checkpoint.BPP_Tile_Exit_Checkpoint_C")));
	UPROPERTY(EditAnywhere, Category = "타일|시작·출구")
	TSoftClassPtr<AActor> ObstacleTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Obstacle_Checkpoint.BPP_Tile_Obstacle_Checkpoint_C")));

	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> OpenGroundTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_OpenGround.BPP_Tile_None_OpenGround_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> RuinsTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_Ruins.BPP_Tile_None_Ruins_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureMeadowTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Meadow_V2.BP_Tile_Nature_Meadow_V2_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureForestSparseTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestSparse.BP_Tile_Nature_ForestSparse_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureForestDenseTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestDense.BP_Tile_Nature_ForestDense_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureRockyTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Rocky.BP_Tile_Nature_Rocky_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureScrubTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Scrub.BP_Tile_Nature_Scrub_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureAmbushTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ambush.BP_Tile_Nature_Ambush_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureServiceCampTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ServiceCamp.BP_Tile_Nature_ServiceCamp_C")));
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureDitchTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ditch.BP_Tile_Nature_Ditch_C")));

	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneYardTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Yard.BPP_Tile_WarZone_Yard_C")));
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneWarehouseTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Warehouse.BPP_Tile_WarZone_Warehouse_C")));
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneIndustrialOpenTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_IndustrialOpen.BP_Tile_WarZoneV2_IndustrialOpen_C")));
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneContainerLaneTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_ContainerLane.BP_Tile_WarZoneV2_ContainerLane_C")));
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneFactoryYardTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_FactoryYard.BP_Tile_WarZoneV2_FactoryYard_C")));
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneUtilityYardTile = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_UtilityYard.BP_Tile_WarZoneV2_UtilityYard_C")));

	// ---------- 큰 건물 (건물 자리 담당이 고른 자리에 공사 담당이 불러온다) ----------
	// 워존 한가운데 공장 단지(3×5) 레벨.
	// WarZone 중심: 창고 건물 두 동과 그 사이 드럼통 마당. Factory 팩 데모의 서쪽 구역에서
	// 잘라 왔다(잘라낸 창 중심 (400,-2000), 반폭 3000 — 모든 가장자리가 빈 마당을 지나간다.
	// 데모에서 그 안쪽은 z=100 평지라 로컬 0 으로 내렸다).
	// 코드로 만들던 IndustrialRaid3x3 을 대신한다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> WarZoneCoreLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_WarZoneCore_3x3.LD_Facility_WarZoneCore_3x3")));
	// 공장 단지 BP. 있으면 레벨보다 이걸 먼저 쓴다(벽 하나하나를 에디터에서 끌어 옮길 수 있음).
	// 손으로 만든 시설 블루프린트. 안의 벽·소품 하나하나가 따로 된 StaticMeshComponent 라서
	// 디자이너가 에디터 화면에서 하나를 골라 끌거나 크기를 바꿀 수 있다
	// - AProceduralFacilityActor 가 만드는 HISM 인스턴스로는 불가능한 일이다.
	// 여기 항목이 없는 모양 세트는 아직 손으로 안 만든 것이라 코드 빌더로 돌아간다.
	// 그래서 시설을 하나씩 채워 나갈 수 있다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftClassPtr<AActor> WarZoneCoreBlueprint = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/Blueprints/BP_Facility_IndustrialRaid_3x3.BP_Facility_IndustrialRaid_3x3_C")));
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> WarehouseLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_Warehouse_2x2.LD_Facility_Warehouse_2x2")));
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> YardLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_Yard_2x2.LD_Facility_Yard_2x2")));
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> CheckpointLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_Checkpoint_1x2.LD_Facility_Checkpoint_1x2")));
	// Downtown West 데모 환경에서 가져온 카페·상점 구역.
	// 포장된 보도와 연석은 지우지 않았다: 깎은 지형과 달리 이것들은 공통 땅 위에
	// 몇 cm 얹힌 얇은 면이고, 도시 블록이 맨흙 위에 서 있는 쪽이 이음새보다 더 어색하다.
	// 레벨을 들어 올려 포장면이 공통 높이보다 약 10 cm 위에 오게 했다
	// - 런타임 도로판이 깊이 버퍼 겹침(깜빡임)을 피하려고 띄우는 간격과 같다.
	// 6x6: 팩의 데모 도시에서 골목부터 골목까지 잘라 온, 양쪽에 건물이 있는 거리 한 구간.
	// 예전에는 붙어 있는 건물 줄을 가로질러 3x3 로 잘랐는데, 그러면 늘 어떤 건물의
	// 뒷면이나 옆면이 뚫린 채 남았다. 이 팩은 건물 블록을 끊김 없는 줄로 만들어 두어서,
	// 깨끗하게 자를 수 있는 곳은 데모의 x=-10100 과 x=-1000 에 있는 실제 골목뿐이다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> DowntownLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_DowntownBlock_6x6.LD_Facility_DowntownBlock_6x6")));
	// Factory Pack 데모 맵에서 가져온, 서로 이어진 공장 건물 네 동. 안쪽 - 선반, 지붕 트러스,
	// 천창 - 까지 다 있다. 시골 디오라마와 달리 이 레벨에는 자체 땅이 없다:
	// 바닥판을 지워서 공통 타일 지형이 그대로 지나가게 했다.
	// 그래야 시설이 맵 위에 올려놓은 디오라마처럼 보이지 않는다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> FactoryLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_FactoryHall_2x2.LD_Facility_FactoryHall_2x2")));
	// 호숫가 마을(오두막·부두·보트).
	// Modular Rural Cabin 데모에서 다듬어 온 손작업 호숫가 마을:
	// 오두막, 캠핑카, 부두, 물, 노 젓는 보트 두 척이 깎은 땅 위에 있다.
	// 다른 시설 레벨과 달리 이것만 자체 지형을 가지므로,
	// 그 아래 공통 평판은 깔지 않아야 한다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> RuralHideoutLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Facilities/LD_Facility_RuralDiorama_2x2.LD_Facility_RuralDiorama_2x2")));

	// 시설 안 지점 표(행 구조 FFacilityPointRow, MapPointPlanner.h). 창고 2층 상자 자리 같은 좌표를 여기서 고친다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UDataTable> FacilityPointTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Data/DT_FacilityPoints.DT_FacilityPoints")));

	// ---------- 아이템 (아이템 담당) ----------
	// 상자 자리에 무엇이 얼마나 자주 나오나(행 구조 FLootSpawnRow, MapItemSpawner.h).
	UPROPERTY(EditAnywhere, Category = "아이템")
	TSoftObjectPtr<UDataTable> LootSpawnTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Data/DT_LootSpawn.DT_LootSpawn")));
	// 바닥 아이템 BP(부모 AWorldItemActor). 비면 C++ 기본 클래스(대신 모양 없음)를 쓴다.
	UPROPERTY(EditAnywhere, Category = "아이템")
	TSoftClassPtr<AWorldItemActor> WorldItemClass = TSoftClassPtr<AWorldItemActor>(FSoftObjectPath(TEXT("/Game/PG/Blueprint/Item/BP_WorldItem.BP_WorldItem_C")));
	// 아이템 상자 BP(부모 ALootCrateActor). 상자 메시는 BP 에서 고른다. 비면 C++ 기본 클래스(메시 없음 — 안 보임)를 쓴다.
	UPROPERTY(EditAnywhere, Category = "아이템")
	TSoftClassPtr<ALootCrateActor> LootCrateClass = TSoftClassPtr<ALootCrateActor>(FSoftObjectPath(TEXT("/Game/PG/Blueprint/Item/BP_LootCrate.BP_LootCrate_C")));

	// ---------- 바닥 머티리얼 (바닥 담당) ----------
	// 들판 땅. 언덕·호숫가 비탈·타일 바닥판도 이 색으로 맞춘다(이음매가 안 보이게).
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> NatureGroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified")));
	// 들판과 워존 사이 경계 흙.
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> TransitionGroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_Transition.MI_RuntimeGround_Transition")));
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> WarZoneGroundMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_WarZone.MI_RuntimeGround_WarZone")));
	// 도로 아스팔트. 언덕 위 건물 경사로에도 쓴다.
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> RoadMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean.MI_RuntimeRoad_AsphaltClean")));
	// 호수 물. 호숫가 마을 안의 물과 같은 머티리얼이라 한 호수처럼 보인다.
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> LakeWaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Materials/Instances/Water_Lake.Water_Lake")));
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> LakeBedMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Materials/Instances/Diorama_Ground.Diorama_Ground")));

	// ---------- 산·돌·나무 (바닥 담당) ----------
	// 맵 바깥을 둘러싼 산(충돌 없음, 보기만).
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> MountainMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Downtown_West/Assets/background_mountain/SM_background_mountains.SM_background_mountains")));
	// 언덕 위에 뿌리는 돌·나무(부딪힘)·덤불(통과).
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> TerrainRockMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_medium_a_low.SM_rock_medium_a_low")));
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> TerrainTreeMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/PCGBiomeSample/Meshes/PCG_Pine_01.PCG_Pine_01")));
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> TerrainBushMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2")));
	// 호숫가 얕은 물에 놓는 돌(부딪힘)·갈대(통과).
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> ShoreRockMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/River_Stone_2.River_Stone_2")));
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> ShoreReedMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Cat_Tail.Cat_Tail")));

	// ---------- 풀 (바닥 담당의 풀 뿌리기 + 공사 담당의 덤불 색) ----------
	// 들판·덤불 칸에 PCG 로 뿌리는 풀(2종 이상 섞어야 도장 찍은 것처럼 안 보인다).
	UPROPERTY(EditAnywhere, Category = "풀")
	TArray<TSoftObjectPtr<UStaticMesh>> GrassMeshes = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarA.SM_vlkhcbxia_VarA"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarC.SM_vlkhcbxia_VarC"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarF.SM_vlkhcbxia_VarF")))
	};
	UPROPERTY(EditAnywhere, Category = "풀")
	TArray<TSoftObjectPtr<UStaticMesh>> ShrubMeshes = {
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Shrubs_1.Shrubs_1"))),
		TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2")))
	};
	// 타일 안 덤불 잎을 바꿔 칠할 어두운 머티리얼(원래 잎이 형광색으로 너무 튀어서).
	UPROPERTY(EditAnywhere, Category = "풀")
	TSoftObjectPtr<UMaterialInterface> HeroShrubLeafMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeHeroShrub_Dark.MI_RuntimeHeroShrub_Dark")));
};
