// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PGAudioSettings.generated.h"

class UPGSoundSet;

// 프로젝트 설정 → Game → PG Audio. 어떤 소리 표(DA_PGSounds)를 쓸지 고르는 칸.
// 값은 Config/DefaultGame.ini 에 저장되고, 표 자체는 AssetManager 목록(PGSoundSet, 항상 쿠킹)으로 패키지에 들어간다.
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="PG Audio"))
class PROJECTPG_API UPGAudioSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftObjectPtr<UPGSoundSet> SoundSet;

	// 캐릭터마다 붙일 소리 부품. BP 자식(BP_PGCharacterSound)을 넣으면 BP 이벤트로 동작을 바꿀 수 있음. 비우면 C++ 기본.
	UPROPERTY(Config, EditAnywhere, Category="Audio")
	TSoftClassPtr<class UPGCharacterSoundComponent> CharacterSoundClass;

	// 실내 검사: 소리 난 자리에서 위로 이 거리(cm) 안에 지붕이 있으면 실내로 봄.
	UPROPERTY(Config, EditAnywhere, Category="Audio", meta=(ClampMin=100))
	float IndoorCeilingDistance = 2000.f;

	virtual FName GetCategoryName() const override { return TEXT("Game"); }
};
