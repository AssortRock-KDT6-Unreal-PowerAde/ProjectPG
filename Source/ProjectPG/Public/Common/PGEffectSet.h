// 여러 곳이 같이 쓰는 이펙트·재질(데이터 에셋 DA_PGEffects). (2026-09-23 블루프린트 분리)
//
// 왜 데이터 에셋인가: 미사일(UPGMissileSubsystem)·잔해(UPGDebrisSubsystem)는 액터가 아니라 서브시스템이라 블루프린트 자식을 만들 수 없다.
//   그래서 에디터에서 여는 데이터 에셋 하나에 모았다. 어느 에셋을 쓰나: 설정(ProjectPG Visuals > Effect Set).
//   비어 있으면 이 클래스 기본값(원래 코드에 적혀 있던 에셋)을 쓴다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/EngineTypes.h"
#include "PGEffectSet.generated.h"

class UMaterialInterface;
class UParticleSystem;

UCLASS(BlueprintType)
class PROJECTPG_API UPGEffectSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPGEffectSet();

	// 지금 쓰는 이펙트 묶음: 설정에 에셋이 있으면 그것, 없으면 기본값. 게임 중에만 부른다(에셋을 읽는다).
	static const UPGEffectSet* GetActive();

	// 큰 잔해 조각이 넘어질 때 한 번 터지는 흙먼지.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Debris")
	TSoftObjectPtr<UParticleSystem> DebrisDustEffect;

	// 미사일 키트 폴더(SM_ShipMissile·SM_ShipMissileTrail·SM_MissileBlast 를 이름으로 찾는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Missile", meta = (ContentDir, LongPackageName))
	FDirectoryPath MissileKitFolder;

	// 미사일 전용 메시가 없을 때 기본 도형으로 짓는 몸통·링·배기 불빛 재질.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Missile")
	TSoftObjectPtr<UMaterialInterface> MissileBodyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Missile")
	TSoftObjectPtr<UMaterialInterface> MissileDarkMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Missile")
	TSoftObjectPtr<UMaterialInterface> MissileGlowMaterial;
};
