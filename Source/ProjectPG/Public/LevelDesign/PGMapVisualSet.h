// 맵을 지을 때 코드가 게임 중에 읽던 에셋 목록(데이터 에셋 DA_PGMapVisuals). (2026-09-23 블루프린트 분리)
//
// 무엇이 여기 있나: 맵 짓기(AWarZoneFootprintPreview)·전술 타일(ATacticalTileActor)이 게임 중에 경로로 직접 읽던 것 —
//   무너짐 흙먼지·구덩이/가장자리 재질, 풀밭 장식(PCG) 풀·관목, 호숫가 배, 비탈 재질, 묶은 타일 바닥·관목 잎 재질, 타일 풀·바위.
// 무엇이 여기 없나(이미 에디터에서 바꿀 수 있거나, 바꾸면 안 되는 것):
//   - 타일·시설 모양: 타일 블루프린트(BP_Tile_*)·묶은 타일(BPP_Tile_*)·시설 레벨(LD_Facility_*)에 들어 있다. 코드는 "어느 것을 쓸지" 만 고른다.
//   - 바닥·도로·호수 재질, 산·지형 장식 메시: 맵 짓기 액터의 부품(컴포넌트) 기본값이라 레벨에 놓인 액터의 부품 칸에서 바꾼다.
//   - 지형 굴곡(SM_Terrain_*)·호숫가(SM_Shore_*) 메시: 코드의 높이 공식·생성기(PG.BuildShoreMeshes)와 숫자가 맞아야 해서 코드에 둔다.
// 어느 에셋을 쓰나: 설정(ProjectPG Visuals > Map Visual Set). 비어 있으면 이 클래스 기본값(원래 코드에 적혀 있던 에셋).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PGMapVisualSet.generated.h"

class UMaterialInterface;
class UParticleSystem;
class UStaticMesh;

UCLASS(BlueprintType)
class PROJECTPG_API UPGMapVisualSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPGMapVisualSet();

	// 지금 쓰는 목록: 설정에 에셋이 있으면 그것, 없으면 기본값.
	static const UPGMapVisualSet* GetActive();

	// ---- 무너짐(피날레) ----
	// 땅이 꺼질 때 흙먼지(잔해·드래곤과 같은 것).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Collapse")
	TSoftObjectPtr<UParticleSystem> CollapseDust;

	// 꺼진 구덩이 바닥과 맵 가장자리 치마에 까는 재질(배경 산과 같은 바위 재질).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Collapse")
	TSoftObjectPtr<UMaterialInterface> RockEdgeMaterial;

	// ---- 풀밭 장식(PCG) ----
	// 풀(무게 3)과 관목(무게 1)을 섞어 뿌린다. 관목은 두 종류 이상이어야 풀밭이 도장 찍은 것처럼 안 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Dressing")
	TArray<TSoftObjectPtr<UStaticMesh>> DressingGrass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Dressing")
	TArray<TSoftObjectPtr<UStaticMesh>> DressingShrubs;

	// ---- 호수·비탈 ----
	// 호숫가에 띄우는 배(타고 나가는 탈출구).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Lake")
	TSoftObjectPtr<UStaticMesh> LakeBoat;

	// 높은 땅으로 오르는 비탈 재질(도로와 같은 아스팔트).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Terrain")
	TSoftObjectPtr<UMaterialInterface> RampMaterial;

	// ---- 묶은 타일(BPP_Tile_*) 손질 ----
	// 묶은 타일 바닥을 게임 맵 자연 바닥과 같은 재질로 맞춘다. 전술 타일 바닥도 같은 재질.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Tiles")
	TSoftObjectPtr<UMaterialInterface> UnifiedGroundMaterial;

	// 무료 관목 팩의 형광색·크게 흔들리는 잎 재질을 갈아 끼우는 어둡고 거의 안 흔들리는 잎 재질(줄기 재질은 그대로).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Tiles")
	TSoftObjectPtr<UMaterialInterface> HeroShrubLeafMaterial;

	// ---- 전술 타일(ATacticalTileActor) ----
	// 예전 블루프린트 자식이 저장해 둔 옛 메시를 게임 중에 이 메시로 되돌려 놓는다(보이는 게 확인된 풀 세 가지·실제 크기 바위 두 가지).
	// 풀 메시는 묶은 타일 손질이 경로("/RuntimeOptimized/SM_GrassPatch_")로 풀인지 알아보므로, 바꾸면 그 판정도 같이 봐야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|TacticalTile")
	TSoftObjectPtr<UStaticMesh> TileGrassA;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|TacticalTile")
	TSoftObjectPtr<UStaticMesh> TileGrassB;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|TacticalTile")
	TSoftObjectPtr<UStaticMesh> TileGrassLong;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|TacticalTile")
	TSoftObjectPtr<UStaticMesh> TileRockLarge;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|TacticalTile")
	TSoftObjectPtr<UStaticMesh> TileRockMedium;

	// 시설 레벨마다 그림자를 드리우게 남겨 둘 점·스포트 조명 수. 나머지는 레벨이 뜰 때 그림자를 끈다.
	// 9/28: 중앙 건물 조명 52개가 부서질 때마다 VSM 을 다시 그려 프레임이 떨어졌다(WarZoneFootprintPreview.cpp TrimFacilityLightShadows).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Lighting", meta = (ClampMin = "0"))
	int32 FacilityShadowLights = 0;

	// 타일 풀 묶음(SM_GrassPatch_*_Runtime, Grass_Patch)이 흐려지기 시작하는 거리·다 사라지는 거리(cm).
	// 9/28 4060 측정 때 코드에서 옮겼다(값은 그대로 18m~60m). 풀은 10만 개 넘는 마스크 재질 카드(나나이트 아님)라, 느린 PC 에서 줄일 첫 손잡이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Performance", meta = (ClampMin = "0"))
	float RuntimeGrassFadeStartCm = 1800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Performance", meta = (ClampMin = "0"))
	float RuntimeGrassCullEndCm = 6000.0f;

	// 상호작용 오브젝트(바닥 아이템·상자·소품) 메시가 보이는 최대 거리(cm). 메시 크기(바운드 반지름)로 나눈다. 0 = 제한 없음.
	// 9/28 4060 측정: 바닥 탄약 914개·권총 216개·무기 수백 개가 각각 따로(나나이트 아님) 맵 끝에서도 그려져 그리기 준비(CPU)를 먹었다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Performance", meta = (ClampMin = "0"))
	float SmallObjectDrawDistanceCm = 6000.0f;   // 반지름 60cm 미만(탄약·총·약·연료통)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Map|Performance", meta = (ClampMin = "0"))
	float MediumObjectDrawDistanceCm = 12000.0f; // 반지름 1.5m 미만(상자·드럼통·가방)
};
