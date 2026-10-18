#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "Engine/DataAsset.h"
#include "EnemyAIConfig.generated.h"

class UStateTree;
class UEnvQuery;

// 몬스터 AI 가 "어떻게 움직일지" 정하는 숫자와 에셋을 한곳에 모은 데이터 에셋.
// 왜: 예전엔 5초·4초·60·170 같은 숫자가 코드 곳곳에 박혀 있어 기획이 조절할 수 없었다.
// 몬스터 종류마다 다른 값(감지 거리·사거리·체력 등)은 여전히 EnemyTable 에 있고,
// 여기엔 종류와 상관없는 행동 습관만 둔다.
// 쓰는 곳: 몬스터 BP(CBP_CustomEnemyCharacter)의 AIConfig 칸 → AI 컨트롤러가 빙의할 때 읽는다.
UCLASS(BlueprintType)
class PROJECTPG_API UEnemyAIConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	// 몬스터가 실행할 상태 트리(순찰 → 전투 → 수색). PGBuildEnemyAI 명령(-run=PGBuildEnemyAI)으로 만든다.
	UPROPERTY(EditDefaultsOnly, Category="Assets")
	TObjectPtr<UStateTree> StateTree;

	// 엄폐 자리 후보를 고르는 EQS. 비어 있으면 엄폐 없이 서서 싸운다(경고 로그 한 번).
	UPROPERTY(EditDefaultsOnly, Category="Assets")
	TObjectPtr<UEnvQuery> CoverQuery;

	// 정면에서 좌우로 몇 도까지 보이는지(한쪽 각도).
	UPROPERTY(EditDefaultsOnly, Category="Sight", meta=(ClampMin="10", ClampMax="180"))
	float SightHalfAngleDegrees = 75.f;

	// 한 번 본 목표는 감지 거리의 이 배수까지 멀어져야 시야에서 놓친다(바로 놓치면 깜빡거림).
	UPROPERTY(EditDefaultsOnly, Category="Sight", meta=(ClampMin="1"))
	float LoseSightRangeScale = 1.5f;

	// 목표가 안 보이게 된 뒤 이 초가 지나도 다시 안 보이면 포기하고 마지막 위치를 수색한다.
	UPROPERTY(EditDefaultsOnly, Category="Sight", meta=(ClampMin="0"))
	float LoseTargetDelay = 5.f;

	// 엄폐 자리를 한 번 찾은 뒤 다음 찾기까지 기다리는 초. 너무 짧으면 계속 자리만 옮긴다.
	UPROPERTY(EditDefaultsOnly, Category="Cover", meta=(ClampMin="0"))
	float CoverSearchCooldown = 4.f;

	// EQS 가 이 초 안에 답을 안 주면 실패로 보고 Error 로그를 남긴다.
	UPROPERTY(EditDefaultsOnly, Category="Cover", meta=(ClampMin="0.1"))
	float CoverQueryTimeout = 2.f;

	// EQS 가 준 후보 중 앞에서부터 몇 개까지 "옆으로 나가서 쏠 자리"를 확인할지.
	UPROPERTY(EditDefaultsOnly, Category="Cover", meta=(ClampMin="1"))
	int32 MaxCoverCandidates = 8;

	// 엄폐 자리에서 옆으로 몇 cm 나가서 쏠지.
	UPROPERTY(EditDefaultsOnly, Category="Cover", meta=(ClampMin="0"))
	float PeekSideDistance = 170.f;

	// 이동할 때 목적지에서 이만큼 가까우면 도착으로 본다(cm).
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0"))
	float MoveAcceptanceRadius = 60.f;

	// 이동 시간 제한 = 예상 걸리는 시간(길 길이 ÷ 걷는 속도) × MoveTimeoutScale + MoveTimeoutSlack.
	// 이걸 넘으면 막힌 것으로 보고 Error 로그 후 실패 처리.
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="1"))
	float MoveTimeoutScale = 1.5f;
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0.5"))
	float MoveTimeoutSlack = 5.f;

	// 목표에게 다가갈 때 사거리의 이 비율까지 들어간다(1 이면 사거리 끝에서 멈춤).
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0.1", ClampMax="1"))
	float ApproachRangeScale = 0.6f;

	// 순찰 지점에 닿은 뒤 서 있는 시간(초, 이 사이에서 무작위).
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0"))
	FVector2D PatrolWaitRange = FVector2D(1.f, 3.f);

	// 놓친 목표의 마지막 위치에 도착한 뒤 둘러보는 시간(초).
	UPROPERTY(EditDefaultsOnly, Category="Movement", meta=(ClampMin="0"))
	float SearchWaitTime = 3.f;

	// 몸이 목표 쪽으로 이 각도 안까지 돌아와야 쏜다(옆을 보며 쏘는 것 막기).
	UPROPERTY(EditDefaultsOnly, Category="Fire", meta=(ClampMin="1", ClampMax="180"))
	float AimToleranceDegrees = 60.f;

	// 쏘는 동안 조준 각도·탄약·시야를 다시 확인하는 간격(초).
	UPROPERTY(EditDefaultsOnly, Category="Fire", meta=(ClampMin="0.05"))
	float FireCheckInterval = 0.2f;

	// ── 귀(청각 감지) ──
	// 총소리·동료 외침을 이 거리(cm) 안에서 듣는다. 벽 너머도 들린다.
	UPROPERTY(EditDefaultsOnly, Category="Hearing", meta=(ClampMin="0"))
	float HearingRange = 4000.f;

	// 플레이어를 보고 외치면 이 거리(cm) 안의 다른 몬스터가 그 플레이어를 노리고 온다.
	// HearingRange 보다 크면 HearingRange 까지만 퍼진다(듣는 쪽 귀가 거기까지라서).
	UPROPERTY(EditDefaultsOnly, Category="Hearing", meta=(ClampMin="0"))
	float CalloutRange = 2500.f;

	// ── 목소리 (몬스터 머리에서 나는 소리) ──
	// 각 목록에서 무작위로 하나를 튼다. 같은 말만 반복되지 않게 3개 이상 넣는 걸 권함.
	UPROPERTY(EditDefaultsOnly, Category="Voice")
	TArray<TObjectPtr<class USoundBase>> TargetSpottedSounds;
	UPROPERTY(EditDefaultsOnly, Category="Voice")
	TArray<TObjectPtr<class USoundBase>> HurtSounds;
	UPROPERTY(EditDefaultsOnly, Category="Voice")
	TArray<TObjectPtr<class USoundBase>> DeathSounds;

	// 거리에 따라 작아지는 정도. 비우면 소리가 맵 어디서나 같은 크기로 들리니 꼭 넣는다.
	UPROPERTY(EditDefaultsOnly, Category="Voice")
	TObjectPtr<class USoundAttenuation> VoiceAttenuation;

	// 같은 몬스터가 다시 외치기까지 기다리는 초. 숨었다 나올 때마다 외치지 않게.
	UPROPERTY(EditDefaultsOnly, Category="Voice", meta=(ClampMin="0"))
	float TargetSpottedCooldown = 8.f;

	// 맞는 소리 사이 최소 초. 연사에 맞을 때 "악악악" 이 되지 않게.
	UPROPERTY(EditDefaultsOnly, Category="Voice", meta=(ClampMin="0"))
	float HurtCooldown = 0.8f;

	// 목소리 높낮이를 이 범위에서 무작위로 바꾼다(1 = 그대로). 같은 파일도 조금씩 다르게 들림.
	UPROPERTY(EditDefaultsOnly, Category="Voice", meta=(ClampMin="0.5", ClampMax="2"))
	FVector2D VoicePitchRange = FVector2D(0.94f, 1.06f);
};
