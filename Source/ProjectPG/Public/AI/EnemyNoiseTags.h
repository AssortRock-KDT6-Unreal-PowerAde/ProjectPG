#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "UObject/NameTypes.h"

// 몬스터 귀(청각 감지)에 들어가는 소리 종류 이름. 소리를 내는 쪽과 듣는 쪽이 같은 이름을 써야 해서 한곳에 둔다.
// 소리를 낼 때는 "소리 난 자리"와 "그 소리가 가리키는 사람(플레이어)"을 같이 보낸다.
namespace EnemyNoiseTags
{
	// 총을 쐈을 때. 소리 난 자리 = 총구, 가리키는 사람 = 쏜 사람.
	inline const FName Gunshot(TEXT("Gunshot"));
	// 몬스터가 플레이어를 보고 소리 질렀을 때. 소리 난 자리 = 외친 몬스터, 가리키는 사람 = 발견된 플레이어.
	inline const FName EnemyCallout(TEXT("EnemyCallout"));
}
