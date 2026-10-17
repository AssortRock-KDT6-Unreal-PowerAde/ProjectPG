#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "EnvironmentQuery/EnvQueryContext.h"
#include "EnvQueryContext_EnemyTarget.generated.h"

// EQS 에게 "이 몬스터가 지금 노리는 사람"을 알려 준다.
// 엄폐 찾기 EQS(EQS_FindCover)가 "그 사람 눈에서 가려지는 점"을 고를 때 쓴다.
// 목표가 없으면 빈 값 → EQS 는 후보를 못 내고 엄폐 찾기가 실패한다.
UCLASS(meta=(DisplayName="Enemy Target"))
class PROJECTPG_API UEnvQueryContext_EnemyTarget : public UEnvQueryContext
{
	GENERATED_BODY()
public:
	virtual void ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const override;
};
