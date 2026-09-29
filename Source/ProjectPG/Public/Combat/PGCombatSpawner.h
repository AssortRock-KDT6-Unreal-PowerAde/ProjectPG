// 맵이 생성될 때 몬스터·보스 로봇·탈것 로봇·차량을 자동으로 놓는다.
//
// 상자를 Loot 포인트에 까는 것과 같은 방식이다. 맵 생성기가 만들어 준 레벨 디자인 포인트 중
//  - AISpawn 포인트 → 몬스터. 플레이어 Spawn 에서 가장 먼 AISpawn 하나는 보스, 중간 거리 하나는 탈것 로봇
//  - Spawn 포인트 옆 → 차량 (초반에 바로 탈 수 있게)
// 전부 맵 시드로 정하므로 같은 시드 = 같은 배치. 개수·종류·몬스터 클래스는 프로젝트 설정(ProjectPG Objects > Combat).
#pragma once

#include "CoreMinimal.h"

struct FLevelDesignPoint;

class APGMonsterCharacter;
class APGRobotCharacter;
enum class EPGMonsterFaction : uint8;
namespace PGCombatSpawner
{
	// 서버 전용. 스폰한 액터 수를 돌려준다.
	int32 SpawnFromPoints(UWorld* World, const TArray<FLevelDesignPoint>& Points, int64 Seed);

	// 몬스터 한 마리. 바닥을 다시 찾아 발을 붙이고, 프리셋 외형과 세력별 체력·공격·루팅·몸집을 적용한다. 리스폰도 이걸 쓴다.
	APGMonsterCharacter* SpawnMonster(UWorld* World, UClass* MonsterClass, const FVector& Location, float Yaw, FName Preset, EPGMonsterFaction Faction);

	// 로봇 한 대(보스 또는 탈것). 바닥을 다시 찾아 발을 붙인다. 리스폰도 이걸 쓴다.
	APGRobotCharacter* SpawnRobot(UWorld* World, const FVector& Location, float Yaw, bool bBoss, float Scale, EPGMonsterFaction Faction);
}
