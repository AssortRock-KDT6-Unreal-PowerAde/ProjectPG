#include "Monster/AI/StateTree/PGMonsterStateTreeComponent.h"

#include "Monster/AI/PGMonsterAIStats.h"

void UPGMonsterStateTreeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	// 횟수는 세지 않는다(컨트롤러 Tick 이 이미 "생각" 한 번으로 셌다). 시간만 더하고, 나무가 몇 번 돌았는지는 따로 센다.
	PGMonsterAIStats::FThinkScope ThinkScope(/*bCountThink*/ false);
	PGMonsterAIStats::CountTreeTick();
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

void UPGMonsterStateTreeComponent::ValidateStateTreeReference()
{
	if (!StateTreeRef.IsValid())
		return;
	Super::ValidateStateTreeReference();
}
