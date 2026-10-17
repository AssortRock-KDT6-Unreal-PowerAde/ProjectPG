// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Chaos/ChaosEngineInterface.h"
#include "Common/GameDefine.h"
#include "PGSoundSet.generated.h"

class USoundBase;
class USoundWave;
class USoundAttenuation;
class UAnimMontage;

// 소리 하나를 틀 때 필요한 것 묶음. 소리 모양(층·필터·메아리)은 Sound 에 넣은 메타사운드가 정하고,
// C++ 은 아래 녹음 목록에서 하나를 골라 메타사운드 입력으로 넘기기만 한다.
USTRUCT(BlueprintType)
struct FPGSoundCue
{
	GENERATED_BODY()

	// 틀 메타사운드(MS_OneShot, MS_Gunshot 등). 비어 있으면 아무 소리도 안 남.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	TObjectPtr<USoundBase> Sound;

	// 가까이서 들리는 녹음 후보. 매번 하나를 골라 메타사운드 "Wave" 입력으로 넘김(바로 전 것은 피함).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	TArray<TObjectPtr<USoundWave>> Waves;

	// 멀리서 들리는 녹음 후보(총소리만). 메타사운드 "FarWave" 입력 → 거리에 따라 Wave 와 섞임.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	TArray<TObjectPtr<USoundWave>> FarWaves;

	// 거리에 따라 줄어드는 정도. 비우면 메타사운드에 걸린 기본 감쇠를 씀.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	TObjectPtr<USoundAttenuation> Attenuation;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound", meta=(ClampMin=0))
	float Volume = 1.f;

	// 매번 음높이를 이만큼(비율) 흔들어 같은 소리 반복이 덜 티 나게. 0.05 = ±5%.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound", meta=(ClampMin=0, ClampMax=0.5))
	float PitchJitter = 0.05f;

	// UI·안내처럼 위치 없이 귀에 바로 들리는 소리.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	bool b2D = false;

	// 지붕 아래인지 검사해서 메타사운드 "Indoor" 입력(0/1)으로 넘길지(총소리·폭발처럼 메아리가 중요한 것만).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	bool bCheckIndoor = false;
};

// 몽타주가 시작될 때 틀 소리(장전·피격·죽음 등).
USTRUCT(BlueprintType)
struct FPGMontageSound
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	TObjectPtr<UAnimMontage> Montage;

	// 아래 Cues 표의 이름.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	FName Cue;

	// 몽타주 시작 뒤 몇 초 있다가 틀지(재생 속도 반영).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound", meta=(ClampMin=0))
	float Delay = 0.f;
};

// 물리 재질이 안 붙은 물체의 재질을 이름으로 짐작하는 규칙 한 줄. 예: "metal" → Metal.
USTRUCT(BlueprintType)
struct FPGSurfaceKeyword
{
	GENERATED_BODY()

	// 머티리얼·메시·액터 이름에 이 글자가 들어 있으면(대소문자 무시) 아래 재질로 봄.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	FString Keyword;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Sound")
	TEnumAsByte<EPhysicalSurface> Surface = SurfaceType_Default;
};

// 게임 소리 전부를 담는 표 한 장(DA_PGSounds). 어느 것을 쓸지는 프로젝트 설정 → PG Audio 에서 고른다.
UCLASS(BlueprintType)
class PROJECTPG_API UPGSoundSet : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 총 종류마다 발사 소리. 발사 몽타주가 시작되면 모든 클라가 각자 튼다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TMap<EWeaponType, FPGSoundCue> Gunshots;

	// 총알이 맞은 자리 재질마다 소리. 표에 없는 재질은 Default 칸을 씀.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Weapon")
	TMap<TEnumAsByte<EPhysicalSurface>, FPGSoundCue> BulletImpacts;

	// 밟은 바닥 재질마다 발소리. 표에 없는 재질은 Default 칸을 씀.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	TMap<TEnumAsByte<EPhysicalSurface>, FPGSoundCue> Footsteps;

	// 점프 뒤 땅에 닿을 때 소리(발소리와 겹쳐서 남).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement")
	FPGSoundCue Landing;

	// 맵 메시 대부분은 서드파티 팩이라 물리 재질이 없음 → 이름으로 재질을 짐작하는 규칙(위에서부터 처음 맞는 것).
	// 물리 재질이 붙은 곳은 그게 먼저. 캐릭터·몬스터는 Flesh, 지형(랜드스케이프)은 LandscapeSurface.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface")
	TArray<FPGSurfaceKeyword> SurfaceKeywords;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Surface")
	TEnumAsByte<EPhysicalSurface> LandscapeSurface = SurfaceType4;

	// 뛰는 속도(cm/s)일 때 발소리 사이 거리(cm). 걸으면 이보다 짧아짐.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin=30))
	float StrideAtRun = 170.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement", meta=(ClampMin=50))
	float RunSpeed = 500.f;

	// 장전·피격·죽음 등 몽타주 → 소리 이름.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Montage")
	TArray<FPGMontageSound> MontageSounds;

	// 이름으로 부르는 소리(UI.Click, UI.Hover, Reload.Rifle, 앞으로 생길 문·폭발·피날레 등).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Cues")
	TMap<FName, FPGSoundCue> Cues;

	// 재질 표에서 찾고, 없으면 Default 칸, 그것도 없으면 nullptr.
	static const FPGSoundCue* FindBySurface(const TMap<TEnumAsByte<EPhysicalSurface>, FPGSoundCue>& Table, EPhysicalSurface Surface);
};
