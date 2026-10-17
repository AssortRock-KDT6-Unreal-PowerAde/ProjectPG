#include "AI/EnemyAITags.h"

namespace EnemyAITags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TargetFound, "AI.Event.TargetFound", "몬스터가 목표를 새로 정함");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TargetSeen, "AI.Event.TargetSeen", "놓친 목표가 다시 보임");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(TargetLost, "AI.Event.TargetLost", "몬스터가 목표를 포기함");
}
