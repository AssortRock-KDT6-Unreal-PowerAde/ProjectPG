// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Chaos/ChaosEngineInterface.h"
#include "PGAudioSubsystem.generated.h"

class UPGSoundSet;
class UAudioComponent;
class ACharacter;
struct FPGSoundCue;
class FPGUISoundInput;

// 화면이 있는 쪽(클라·혼자 하기)에서만 소리를 맡는 곳. 데디 서버에서는 아무것도 안 함.
// - 게임이 시작되면: 소리 표를 읽고, 캐릭터가 생길 때마다 발소리·몽타주 소리 부품을 붙이고, UI 버튼 소리를 켬.
// - 소리는 복제하지 않는다. 이미 모든 클라에 복제되는 몽타주·이동을 보고 각 클라가 자기 귀 위치 기준으로 튼다.
UCLASS()
class PROJECTPG_API UPGAudioSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGAudioSubsystem* Get(const UObject* WorldContext);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	const UPGSoundSet* GetSoundSet() const { return SoundSet; }

	// 소리 하나를 그 자리에 틈. 녹음 후보에서 하나 골라 메타사운드 입력으로 넘기고 음높이를 살짝 흔든다.
	UAudioComponent* PlayCue(const FPGSoundCue& Cue, const FVector& Location, float VolumeScale = 1.f);

	// 이름으로 찾아 틈(UI.Click 등). 표에 없으면 아무것도 안 함.
	UAudioComponent* PlayCueByName(FName CueName, const FVector& Location, float VolumeScale = 1.f);

	// 맞은 자리 재질. 순서: 물리 재질 → 캐릭터면 Flesh → 지형이면 표의 LandscapeSurface → 이름 규칙(SurfaceKeywords) → Default.
	EPhysicalSurface SurfaceOf(const FHitResult& Hit);

	// 위로 선 몇 개를 쏴서 대부분 막히면 지붕 아래(실내)로 봄.
	bool IsIndoors(const FVector& Location) const;

private:
	void HandleActorSpawned(AActor* Actor);
	void AttachCharacterSounds(ACharacter* Character);

	UPROPERTY(Transient)
	TObjectPtr<UPGSoundSet> SoundSet;

	UPROPERTY(Transient)
	TSubclassOf<class UPGCharacterSoundComponent> CharacterSoundClass;

	FDelegateHandle ActorSpawnedHandle;
	TSharedPtr<FPGUISoundInput> UIInput;

	// 같은 녹음이 연달아 나오지 않게 소리마다 바로 전에 고른 번호를 기억.
	TMap<const void*, int32> LastPicked;
	int32 PickIndex(const void* Key, int32 Count);

	// 이름 규칙으로 짐작한 재질을 물체(머티리얼·메시)마다 기억 → 같은 물체는 글자 비교를 다시 안 함.
	TMap<TWeakObjectPtr<const UObject>, TEnumAsByte<EPhysicalSurface>> GuessedSurfaces;
	EPhysicalSurface GuessSurfaceByName(const FHitResult& Hit);
};
