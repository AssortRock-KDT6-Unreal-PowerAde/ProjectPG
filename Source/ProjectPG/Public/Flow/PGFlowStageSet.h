// 타이틀·로비·결과 화면 뒤 3D 무대에 쓰는 에셋 목록(데이터 에셋 DA_PGFlowStage). (2026-09-23 블루프린트 분리)
//
// 왜 데이터 에셋인가: 무대(PGFlowStage)는 액터가 아니라 게임모드가 시작할 때 조각을 놓는 코드라 블루프린트 자식을 만들 수 없다.
//   캐릭터·나무·차·불 같은 "무엇을 놓나" 만 이 에셋으로 뺐다. "어디에 몇 개, 얼마나 크게" 는 여전히 코드(PGFlowStage.cpp)가 정한다.
// 어느 에셋을 쓰나: 설정(ProjectPG Visuals > Flow Stage). 비어 있으면 이 클래스 기본값(원래 코드에 적혀 있던 에셋)을 쓴다.
// 칸이 비었거나 에셋이 디스크에 없으면 그 조각만 빼고 짓는다(원래와 같다 — 팩이 없는 PC 에서도 화면은 뜬다).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PGFlowStageSet.generated.h"

class UAnimSequence;
class UMaterialInterface;
class UParticleSystem;
class USkeletalMesh;
class UStaticMesh;

UCLASS(BlueprintType)
class PROJECTPG_API UPGFlowStageSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPGFlowStageSet();

	// 지금 쓰는 목록: 설정에 에셋이 있으면 그것, 없으면 기본값. 무대를 지을 때(게임 중)만 부른다.
	static const UPGFlowStageSet* GetActive();

	// 가운데 서 있는 캐릭터와 대기 동작(애님 블루프린트 없이 한 동작만 되풀이).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Hero")
	TSoftObjectPtr<USkeletalMesh> HeroMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Hero")
	TSoftObjectPtr<UAnimSequence> HeroIdle;

	// 바닥 평면(800배로 늘린다)과 그 재질.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Ground")
	TSoftObjectPtr<UStaticMesh> GroundMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Ground")
	TSoftObjectPtr<UMaterialInterface> GroundMaterial;

	// 흩뿌리는 풀 덤불(차례로 돌려 쓴다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Ground")
	TArray<TSoftObjectPtr<UStaticMesh>> GrassPatches;

	// 캐릭터 뒤 숲의 소나무(차례로 돌려 쓴다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Nature")
	TArray<TSoftObjectPtr<UStaticMesh>> PineTrees;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Nature")
	TSoftObjectPtr<UStaticMesh> Bush;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Nature")
	TSoftObjectPtr<UStaticMesh> BushTree;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Nature")
	TSoftObjectPtr<UStaticMesh> Rock;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Nature")
	TSoftObjectPtr<UStaticMesh> Stones;

	// 로비 왼쪽에 세워 둔 차.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Props")
	TSoftObjectPtr<UStaticMesh> ParkedCar;

	// 타이틀에서 불타는 폐차.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Props")
	TSoftObjectPtr<UStaticMesh> BurningWreck;

	// 타이틀 불꽃·불티(캐스케이드 파티클).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Fire")
	TSoftObjectPtr<UParticleSystem> FireEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Fire")
	TSoftObjectPtr<UParticleSystem> EmbersEffect;

	// 연기·큰 불(나이아가라 시스템). 우리 모듈은 나이아가라에 의존하지 않아 형식을 UObject 로 두고 고르는 창만 나이아가라로 좁힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Fire", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	TSoftObjectPtr<UObject> SmokeEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Stage|Fire", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	TSoftObjectPtr<UObject> BigFireEffect;
};
