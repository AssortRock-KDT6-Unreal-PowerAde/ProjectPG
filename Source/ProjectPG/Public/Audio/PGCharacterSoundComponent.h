// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Common/GameDefine.h"
#include "PGCharacterSoundComponent.generated.h"

class ACharacter;
class UAnimMontage;
class ACustomCharacter;

// 캐릭터(플레이어·몬스터) 하나의 몸에서 나는 소리. UPGAudioSubsystem 이 클라에서만 붙인다.
// 팀원 캐릭터 클래스는 고치지 않고, 엔진이 이미 주는 것(몽타주 시작 알림, 이동 상태)만 본다.
// - 발사 몽타주가 시작됨 → 몽타주 안 발사 표시(AnimNotify_Fire) 시각에 맞춰 총소리 + 각 클라가 직접 쏜 보이기용 선의 탄착음
// - 다른 몽타주가 시작됨 → 소리 표 MontageSounds 에 있으면 그 소리(장전·피격·죽음)
// - 0.1초마다 이동 확인 → 걸은 거리만큼 발소리, 공중에서 땅에 닿으면 착지 소리
// C++ 은 "언제·어디서·무슨 재질"만 알아내고, 실제로 트는 건 아래 On… 이벤트.
// BP 자식(BP_PGCharacterSound)에서 이벤트를 덮어쓰면 그쪽이 이김(프로젝트 설정 → PG Audio → CharacterSoundClass).
UCLASS(Blueprintable, ClassGroup=(Audio), meta=(BlueprintSpawnableComponent))
class PROJECTPG_API UPGCharacterSoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGCharacterSoundComponent();

	UFUNCTION(BlueprintPure, Category="PG|Audio")
	ACharacter* GetOwnerCharacter() const { return OwnerCharacter; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// 총구에서 한 발. 기본: 소리 표 Gunshots[무기 종류].
	UFUNCTION(BlueprintNativeEvent, Category="PG|Audio")
	void OnGunshot(FVector MuzzleLocation, EWeaponType WeaponType);

	// 이 클라가 직접 쏜 보이기용 선이 맞은 자리. 기본: 소리 표 BulletImpacts[재질].
	UFUNCTION(BlueprintNativeEvent, Category="PG|Audio")
	void OnBulletImpact(FVector ImpactLocation, EPhysicalSurface Surface);

	// 한 걸음 또는 착지. VolumeScale = 빠를수록 큼(0.3~1). 기본: Footsteps[재질] (+ 착지면 Landing).
	UFUNCTION(BlueprintNativeEvent, Category="PG|Audio")
	void OnFootstep(FVector FootLocation, EPhysicalSurface Surface, float VolumeScale, bool bLanding);

	// 소리 표 MontageSounds 에 걸린 몽타주가 시작됨(Delay 지난 뒤). 기본: Cues[CueName].
	UFUNCTION(BlueprintNativeEvent, Category="PG|Audio")
	void OnMontageSound(UAnimMontage* Montage, FName CueName);

private:
	// 몸 애니메이션이 아직 준비 안 됐을 수 있어서 0.5초마다 다시 시도, 10번(5초) 안 되면 Error 로그 남기고 포기.
	void TryBindAnimInstance();
	int32 BindTries = 0;
	FTimerHandle BindTimer;
	TWeakObjectPtr<UAnimInstance> BoundAnim;

	UFUNCTION()
	void HandleMontageStarted(UAnimMontage* Montage);

	// 발사 몽타주 안의 발사 표시 시각(초). 없으면 -1. 몽타주마다 한 번만 찾아 기억.
	float FindFireNotifyTime(UAnimMontage* Montage);
	UPROPERTY(Transient)
	TMap<TObjectPtr<UAnimMontage>, float> FireNotifyTimeCache;

	void FireNow();
	void TraceBulletImpacts(ACustomCharacter* Shooter, const FVector& Muzzle);

	// 발소리·착지: 이동은 끊임없이 바뀌는 값이라 알림이 없음 → 0.1초 타이머로 봄(매 프레임 X).
	// 부담: 캐릭터 하나에 1초 10번, 멈춰 있으면 속도만 보고 바로 끝남.
	void CheckMovement();
	FTimerHandle MoveTimer;
	float DistanceSinceStep = 0.f;
	bool bWasFalling = false;
	void StepNow(float VolumeScale, bool bLanding);

	UPROPERTY(Transient)
	TObjectPtr<ACharacter> OwnerCharacter;
};
