// 타이틀 인트로(구출·매복·전함과 드래곤)에 쓰는 에셋 목록(데이터 에셋 DA_PGTitleIntro). (2026-09-23 블루프린트 분리)
//
// 왜 데이터 에셋인가: 인트로는 카메라·등장 시각이 코드에 한 줄씩 짜인 짧은 영상이라, 블루프린트 자식으로 쪼개면 흐름을 따라가기 어렵다.
//   그래서 "무엇이 나오나"(동작·총·차·배·드래곤·효과)만 이 에셋으로 뺐다. "언제 어디서 어떻게 움직이나" 는 코드 그대로다.
//   주인공 캐릭터와 바닥은 무대 에셋(DA_PGFlowStage)을 같이 쓴다 — 인트로가 무대 주인공을 빌려 쓰기 때문이다.
//   몬스터 모양은 몬스터 모양표(DA_PGMonsterLooks), 여고생 차 색·부스터는 BP_PGTransformNPC / BP_PGFlightKit 에서 온다.
// 어느 에셋을 쓰나: 설정(ProjectPG Visuals > Title Intro Set). 비어 있으면 이 클래스 기본값(원래 코드에 적혀 있던 에셋).
// 칸이 비었거나 에셋이 없으면 그 조각만 빠진다(원래와 같다).
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PGTitleIntroSet.generated.h"

class UAnimSequence;
class UParticleSystem;
class USkeletalMesh;
class UStaticMesh;

UCLASS(BlueprintType)
class PROJECTPG_API UPGTitleIntroSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPGTitleIntroSet();

	// 지금 쓰는 목록: 설정에 에셋이 있으면 그것, 없으면 기본값. 인트로를 틀 때(게임 중)만 부른다.
	static const UPGTitleIntroSet* GetActive();

	// ---- 여러 인트로가 같이 쓰는 것 ----
	// 광선(피벗에서 +X 로 100cm — 게임의 가발 광선·날으는 차 빔과 같은 메시).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Shared")
	TSoftObjectPtr<UStaticMesh> BeamMesh;

	// 착지·폭발 흙먼지, 총알 맞은 작은 효과.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Shared")
	TSoftObjectPtr<UParticleSystem> DustEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Shared")
	TSoftObjectPtr<UParticleSystem> HitEffect;

	// ---- 구출·매복 ----
	// 소총 동작이 없을 때 주인공이 쓰는 대체 동작(무대 주인공과 같은 팩).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UAnimSequence> HeroIdle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UAnimSequence> HeroRun;

	// 소총 동작을 트는 보이지 않는 마네킹과 그 동작들(주인공이 이 포즈를 따라 한다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<USkeletalMesh> RifleMannequin;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UAnimSequence> RifleJog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UAnimSequence> RifleIdle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UAnimSequence> RifleFire;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UAnimSequence> RifleReload;

	// 손에 드는 총과 총구 불꽃(게임의 소총 AK 와 같은 것).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UStaticMesh> RifleMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<UParticleSystem> MuzzleFlash;

	// 여고생이 되는 차(게임 스포츠카).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|Rescue")
	TSoftObjectPtr<USkeletalMesh> CarMesh;

	// ---- 전함과 드래곤 ----
	// 하늘을 지나가는 배(게임 전함과 같은 팩 화물선 블루프린트).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|ShipDragon")
	TSoftClassPtr<AActor> ShipClass;

	// 쫓아오는 드래곤과 날갯짓 동작(게임 드래곤 보스와 같은 것).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|ShipDragon")
	TSoftObjectPtr<USkeletalMesh> DragonMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Intro|ShipDragon")
	TSoftObjectPtr<UAnimSequence> DragonFly;
};
