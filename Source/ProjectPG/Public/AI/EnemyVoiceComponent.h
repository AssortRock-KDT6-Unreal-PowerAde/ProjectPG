#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "Components/ActorComponent.h"
#include "EnemyVoiceComponent.generated.h"

class UAudioComponent;
class USoundBase;
class UEnemyAIConfig;

// 몬스터 목소리 종류.
UENUM()
enum class EEnemyVoiceLine : uint8
{
	TargetSpotted,  // 플레이어를 처음 봤을 때 외침("적 발견!")
	Hurt,           // 총에 맞았을 때("악!")
	Death,          // 죽을 때
};

// 몬스터 목소리를 내는 부품. 몬스터 몸(ACustomEnemyCharacter)에 붙어 있다.
// 언제 낼지는 서버가 정하고(AI 컨트롤러·피격 처리), 모든 클라이언트에 "이 목소리 틀어" 를 보낸다.
// 어떤 소리를 틀지는 몬스터 BP 의 AIConfig 데이터 에셋(Voice 칸)에 있다 → 소리 교체는 에디터에서만.
UCLASS(ClassGroup=AI)
class PROJECTPG_API UEnemyVoiceComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UEnemyVoiceComponent();

	// 서버에서만 부른다. 쉬는 시간이 안 끝났으면 아무것도 안 하고 false.
	// - 소리 목록이 비어 있어도 "낼 때가 됐다"면 true (외침에 딸린 동료 부르기는 소리 파일과 상관없이 돌게).
	// - 죽은 뒤엔 어떤 목소리도 다시 안 낸다.
	bool TryPlayLine(EEnemyVoiceLine Line);

private:
	// 서버 → 모든 클라이언트: 이 종류의 Index 번째 소리를 몬스터 머리에서 튼다.
	// Reliable 인 이유: 쉬는 시간 때문에 자주 안 가고, 죽는 소리가 빠지면 어색하다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayLine(EEnemyVoiceLine Line, uint8 Index);

	const UEnemyAIConfig* GetConfig() const;
	const TArray<TObjectPtr<USoundBase>>* GetSounds(const UEnemyAIConfig& Config, EEnemyVoiceLine Line) const;
	float GetCooldown(const UEnemyAIConfig& Config, EEnemyVoiceLine Line) const;

	// 종류별로 다음에 낼 수 있는 서버 시각.
	float NextAllowedTime[3] = { 0.f, 0.f, 0.f };
	bool bDeathPlayed = false;

	// 지금 나오는 목소리. 새 목소리가 나오면 끊는다(한 입에서 두 목소리가 겹치지 않게).
	TWeakObjectPtr<UAudioComponent> CurrentVoice;
};
