// 몬스터(적 생물)에게 묻고 시키는 창구 — 탈것·비행 장비가 몬스터 클래스를 직접 모르게 한다.
//
// 왜 만들었나(2026-09-26 SOLID — 의존 역전): 차·탱크·비행 장비 4파일이 팀원 클래스 APGMonsterCharacter 로 직접 Cast 해서
//   체력·죽음·크기를 읽었다. 몬스터는 팀원 담당이라, 몬스터 클래스 이름이나 부모가 바뀌면(예: 팀원이 새 Monster 클래스를 만들면)
//   우리 탈것 4파일이 같이 깨진다. 이제 몬스터를 아는 곳은 PGCreatureAccess.cpp 한 파일뿐이다 — 바뀌면 그 파일만 고친다(어댑터).
// 팀원 코드는 고치지 않았다. 여기 함수들은 몬스터가 원래 가진 공개 함수를 그대로 부른다.

#pragma once

#include "CoreMinimal.h"

class AActor;

namespace PGCreature
{
	// 이 액터가 몬스터(적 생물)인가. 지금은 APGMonsterCharacter 와 그 자식(보스 로봇 포함).
	PROJECTPG_API bool IsCreature(const AActor* Actor);
	// 몬스터이고 살아 있나.
	PROJECTPG_API bool IsAliveCreature(const AActor* Actor);
	PROJECTPG_API float GetMaxHealth(const AActor* Actor);
	PROJECTPG_API float GetHealth(const AActor* Actor);
	// 몸 반지름(cm). 작은 몹만 날리는 기준(1.2~1.5m)에 쓴다.
	PROJECTPG_API float GetBodyRadius(const AActor* Actor);
	// 날려 보내기(캐릭터 발사). 몬스터가 아니면 아무것도 안 한다.
	PROJECTPG_API void Launch(AActor* Actor, const FVector& Velocity);
}
