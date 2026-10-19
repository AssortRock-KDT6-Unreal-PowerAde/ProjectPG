#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "NativeGameplayTags.h"

// AI 컨트롤러가 상태 트리에 보내는 알림 이름.
// 상태 트리는 이 알림을 받았을 때만 순찰·전투·수색 사이를 옮긴다(매 순간 확인하지 않음).
namespace EnemyAITags
{
	// 목표를 새로 정했을 때(눈에 띔 또는 맞음).
	PROJECTPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TargetFound);
	// 놓친 목표가 다시 보였을 때. 다가가던 중이면 바로 쏠 수 있는지 다시 고른다.
	PROJECTPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TargetSeen);
	// 목표를 포기했을 때(오래 안 보임·죽음·사라짐).
	PROJECTPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(TargetLost);
}
