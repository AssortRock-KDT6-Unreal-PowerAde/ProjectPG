// 몬스터 클래스를 아는 유일한 곳(PGCreatureAccess.h 머리말). 팀원의 몬스터 클래스가 바뀌면 이 파일만 고친다.
#include "Combat/PGCreatureAccess.h"
#include "Monster/PGMonsterCharacter.h"

namespace PGCreature
{
	static const APGMonsterCharacter* AsMonster(const AActor* Actor)
	{
		return Cast<APGMonsterCharacter>(Actor);
	}

	bool IsCreature(const AActor* Actor)
	{
		return IsValid(AsMonster(Actor));
	}

	bool IsAliveCreature(const AActor* Actor)
	{
		const APGMonsterCharacter* Monster = AsMonster(Actor);
		return IsValid(Monster) && !Monster->IsDead();
	}

	float GetMaxHealth(const AActor* Actor)
	{
		const APGMonsterCharacter* Monster = AsMonster(Actor);
		return IsValid(Monster) ? Monster->MaxHealth : 0.0f;
	}

	float GetHealth(const AActor* Actor)
	{
		const APGMonsterCharacter* Monster = AsMonster(Actor);
		return IsValid(Monster) ? Monster->GetHealth() : 0.0f;
	}

	float GetBodyRadius(const AActor* Actor)
	{
		return IsValid(Actor) ? Actor->GetSimpleCollisionRadius() : 0.0f;
	}

	void Launch(AActor* Actor, const FVector& Velocity)
	{
		if (APGMonsterCharacter* Monster = Cast<APGMonsterCharacter>(Actor))
			Monster->LaunchCharacter(Velocity, true, true);
	}
}
