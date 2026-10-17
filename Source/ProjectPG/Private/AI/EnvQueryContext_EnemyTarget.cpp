#include "AI/EnvQueryContext_EnemyTarget.h"
#include "AI/CustomEnemyAIController.h"
#include "Characters/CustomCharacter.h"
#include "EnvironmentQuery/EnvQueryTypes.h"
#include "EnvironmentQuery/Items/EnvQueryItemType_Actor.h"
#include "GameFramework/Pawn.h"

void UEnvQueryContext_EnemyTarget::ProvideContext(FEnvQueryInstance& QueryInstance, FEnvQueryContextData& ContextData) const
{
	// 질문을 던진 쪽(Querier)은 몬스터 몸이다 → 그 컨트롤러의 목표를 넘긴다.
	const APawn* Pawn = Cast<APawn>(QueryInstance.Owner.Get());
	const ACustomEnemyAIController* AI = Pawn ? Cast<ACustomEnemyAIController>(Pawn->GetController()) : nullptr;
	if (AActor* Target = AI ? AI->GetTarget() : nullptr)
		UEnvQueryItemType_Actor::SetContextHelper(ContextData, Target);
}
