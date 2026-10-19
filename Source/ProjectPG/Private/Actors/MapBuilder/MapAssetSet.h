#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Actor/InteractActor.h"
#include "MapAssetSet.generated.h"

class UDataTable;
class UMaterialInterface;
class UStaticMesh;

// 맵에 쓰이는 "보이는 것" 목록 (데이터 에셋).
// 게임에서: 숲 타일이 어떤 BP 인지, 땅이 어떤 색 머티리얼인지, 바깥 산·풀이 어떤 메시인지, 창고가 어떤 레벨인지.
// 왜 따로 뺐나: 예전엔 이 경로 77줄이 C++ 에 글자로 박혀 있어서, 숲 모양 하나 바꾸려 해도 코드를 고치고 빌드해야 했다.
//               이제 에디터에서 DA_MapAssets 를 열어 드롭다운으로 고르면 된다(코드·빌드 필요 없음).
// 경로는 C++ 기본값으로 두지 않고 DA_MapAssets 칸에 저장한다. 왜: 칸 값이 C++ 기본값과 같으면 언리얼이 파일에
// 적지 않아서, 패키징(쿠킹)이 그 연결을 못 보고 타일·시설·상자를 패키지에서 빼 버렸다(데디 서버 상자 0개).
// 칸에 저장해 두면 DA_MapAssets 가 쿠킹될 때 여기 적힌 에셋도 따라 담긴다.
// 여기 없는 것(엔진 큐브, 언덕 메시, 호숫가 메시)은 코드의 숫자(높이·모양)와 짝이라 C++ 에 남겼다.
UCLASS(BlueprintType)
class UMapAssetSet : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	// ---------- 타일 BP (공사 담당이 칸마다 세운다) ----------
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadCornerTile;
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadStraightTile;
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadTJunctionTile;
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadCrossTile;
	UPROPERTY(EditAnywhere, Category = "타일|도로")
	TSoftClassPtr<AActor> RoadDeadEndTile;

	// 시작 대기소(벽 친 칸, 입구 하나), 출구 검문소, 길 막는 장애물.
	UPROPERTY(EditAnywhere, Category = "타일|시작·출구")
	TSoftClassPtr<AActor> SpawnTile;
	UPROPERTY(EditAnywhere, Category = "타일|시작·출구")
	TSoftClassPtr<AActor> ExitTile;
	UPROPERTY(EditAnywhere, Category = "타일|시작·출구")
	TSoftClassPtr<AActor> ObstacleTile;

	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> OpenGroundTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> RuinsTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureMeadowTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureForestSparseTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureForestDenseTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureRockyTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureScrubTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureAmbushTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureServiceCampTile;
	UPROPERTY(EditAnywhere, Category = "타일|들판")
	TSoftClassPtr<AActor> NatureDitchTile;

	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneYardTile;
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneWarehouseTile;
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneIndustrialOpenTile;
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneContainerLaneTile;
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneFactoryYardTile;
	UPROPERTY(EditAnywhere, Category = "타일|워존")
	TSoftClassPtr<AActor> WarZoneUtilityYardTile;

	// ---------- 큰 건물 (건물 자리 담당이 고른 자리에 공사 담당이 불러온다) ----------
	// 워존 한가운데 공장 단지(3×5) 레벨.
	// WarZone 중심: 창고 건물 두 동과 그 사이 드럼통 마당. Factory 팩 데모의 서쪽 구역에서
	// 잘라 왔다(잘라낸 창 중심 (400,-2000), 반폭 3000 — 모든 가장자리가 빈 마당을 지나간다.
	// 데모에서 그 안쪽은 z=100 평지라 로컬 0 으로 내렸다).
	// 코드로 만들던 IndustrialRaid3x3 을 대신한다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> WarZoneCoreLevel;
	// 공장 단지 BP. 있으면 레벨보다 이걸 먼저 쓴다(벽 하나하나를 에디터에서 끌어 옮길 수 있음).
	// 손으로 만든 시설 블루프린트. 안의 벽·소품 하나하나가 따로 된 StaticMeshComponent 라서
	// 디자이너가 에디터 화면에서 하나를 골라 끌거나 크기를 바꿀 수 있다
	// - AProceduralFacilityActor 가 만드는 HISM 인스턴스로는 불가능한 일이다.
	// 여기 항목이 없는 모양 세트는 아직 손으로 안 만든 것이라 코드 빌더로 돌아간다.
	// 그래서 시설을 하나씩 채워 나갈 수 있다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftClassPtr<AActor> WarZoneCoreBlueprint;
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> WarehouseLevel;
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> YardLevel;
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> CheckpointLevel;
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
	TSoftObjectPtr<UWorld> DowntownLevel;
	// Factory Pack 데모 맵에서 가져온, 서로 이어진 공장 건물 네 동. 안쪽 - 선반, 지붕 트러스,
	// 천창 - 까지 다 있다. 시골 디오라마와 달리 이 레벨에는 자체 땅이 없다:
	// 바닥판을 지워서 공통 타일 지형이 그대로 지나가게 했다.
	// 그래야 시설이 맵 위에 올려놓은 디오라마처럼 보이지 않는다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> FactoryLevel;
	// 호숫가 마을(오두막·부두·보트).
	// Modular Rural Cabin 데모에서 다듬어 온 손작업 호숫가 마을:
	// 오두막, 캠핑카, 부두, 물, 노 젓는 보트 두 척이 깎은 땅 위에 있다.
	// 다른 시설 레벨과 달리 이것만 자체 지형을 가지므로,
	// 그 아래 공통 평판은 깔지 않아야 한다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UWorld> RuralHideoutLevel;

	// 시설 안 지점 표(행 구조 FFacilityPointRow, MapPointPlanner.h). 창고 2층 상자 자리 같은 좌표를 여기서 고친다.
	UPROPERTY(EditAnywhere, Category = "시설")
	TSoftObjectPtr<UDataTable> FacilityPointTable;

	// ---------- 상자 (상자 담당) ----------
	// 상자 자리(Loot 지점)에 놓을 상자 BP 목록. 자리마다 이 중 하나를 고른다(같은 시드면 같은 자리에 같은 상자).
	// 부모가 AInteractActor(에디터에서는 InteractActor)인 BP 면 무엇이든 넣을 수 있다 — 탄약 상자, 의료 상자 등.
	// 같은 BP 를 두 번 넣으면 그 상자가 두 배 자주 나온다.
	UPROPERTY(EditAnywhere, Category = "상자")
	TArray<TSoftClassPtr<AInteractActor>> LootContainerClasses;

	// 타일·시설 BP 에 찍어 둔 상자 자리(LootSlot) 중 판마다 몇 개에 상자를 놓을지. 타일(묶음)마다 이 범위에서 고른다.
	// 기본 최소 0, 최대 1 → 타일마다 "없음" 아니면 1개(판마다 400개 안팎). 자리 3개 중 어디에 나올지는 판마다 바뀐다.
	// 왜 1개까지만: 타일마다 자리를 3~4개 찍어 두어서, 최대를 자리 수까지 두면 판마다 1500개가 넘었다.
	UPROPERTY(EditAnywhere, Category = "상자", meta = (ClampMin = "0"))
	int32 MinActiveSlotsPerGroup = 0;
	// 0 = 그 타일의 자리 수까지.
	UPROPERTY(EditAnywhere, Category = "상자", meta = (ClampMin = "0"))
	int32 MaxActiveSlotsPerGroup = 1;
	// 상자 자리를 아직 안 찍은 타일에 예전 방식(코드가 정한 Loot 지점)으로 상자를 놓을지.
	// 켜 두면: 자리를 찍은 타일은 자리대로, 안 찍은 타일은 예전대로. 모든 타일에 자리를 찍었으면 끈다.
	UPROPERTY(EditAnywhere, Category = "상자")
	bool bUseLegacyLootPoints = true;

	// ---------- 바닥 머티리얼 (바닥 담당) ----------
	// 들판 땅. 언덕·호숫가 비탈·타일 바닥판도 이 색으로 맞춘다(이음매가 안 보이게).
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> NatureGroundMaterial;
	// 들판과 워존 사이 경계 흙.
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> TransitionGroundMaterial;
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> WarZoneGroundMaterial;
	// 도로 아스팔트. 언덕 위 건물 경사로에도 쓴다.
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> RoadMaterial;
	// 호수 물. 호숫가 마을 안의 물과 같은 머티리얼이라 한 호수처럼 보인다.
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> LakeWaterMaterial;
	UPROPERTY(EditAnywhere, Category = "바닥")
	TSoftObjectPtr<UMaterialInterface> LakeBedMaterial;

	// ---------- 산·돌·나무 (바닥 담당) ----------
	// 맵 바깥을 둘러싼 산(충돌 없음, 보기만).
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> MountainMesh;
	// 언덕 위에 뿌리는 돌·나무(부딪힘)·덤불(통과).
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> TerrainRockMesh;
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> TerrainTreeMesh;
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> TerrainBushMesh;
	// 호숫가 얕은 물에 놓는 돌(부딪힘)·갈대(통과).
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> ShoreRockMesh;
	UPROPERTY(EditAnywhere, Category = "산·돌·나무")
	TSoftObjectPtr<UStaticMesh> ShoreReedMesh;

	// ---------- 풀 (바닥 담당의 풀 뿌리기 + 공사 담당의 덤불 색) ----------
	// 들판·덤불 칸에 PCG 로 뿌리는 풀(2종 이상 섞어야 도장 찍은 것처럼 안 보인다).
	UPROPERTY(EditAnywhere, Category = "풀")
	TArray<TSoftObjectPtr<UStaticMesh>> GrassMeshes;
	UPROPERTY(EditAnywhere, Category = "풀")
	TArray<TSoftObjectPtr<UStaticMesh>> ShrubMeshes;
	// 타일 안 덤불 잎을 바꿔 칠할 어두운 머티리얼(원래 잎이 형광색으로 너무 튀어서).
	UPROPERTY(EditAnywhere, Category = "풀")
	TSoftObjectPtr<UMaterialInterface> HeroShrubLeafMaterial;
};
