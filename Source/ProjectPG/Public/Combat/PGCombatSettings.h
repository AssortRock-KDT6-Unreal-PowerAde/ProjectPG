// 프로젝트 설정 > Game > ProjectPG Combat — 몬스터·세력·보스·탈것 자동 생성과 피날레 스위치.
//
// 왜 따로 뺐나(2026-09-26 SOLID — 한 책임): 원래 "ProjectPG Objects"(UPGObjectSettings) 안에 있었는데,
//   그 설정의 절반 이상이 오브젝트가 아니라 전투였다(Combat 14 + Combat|Faction 13 + Finale 1). 이름과 내용이 달라
//   찾기 어려웠고, 오브젝트 스포너를 고칠 때 전투 값까지 한 클래스로 딸려 왔다.
//   DefaultGame.ini 의 값은 [/Script/ProjectPG.PGCombatSettings] 칸으로 같이 옮겼다(값은 그대로).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Monster/PGMonsterCharacter.h"
#include "PGCombatSettings.generated.h"

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ProjectPG Combat"))
class PROJECTPG_API UPGCombatSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// ---- 전투 자동 생성 (몬스터·보스·탈것). 맵 생성 후 AISpawn/Spawn 포인트에 시드로 배치한다. ----
	UPROPERTY(Config, EditAnywhere, Category = "Combat")
	bool bAutoSpawnCombat = true;

	// 비워 두면 APGMonsterCharacter. 팀 몬스터 클래스가 나오면 여기만 바꾼다(APGMonsterCharacter 파생이어야 프리셋 적용).
	UPROPERTY(Config, EditAnywhere, Category = "Combat")
	TSubclassOf<class APGMonsterCharacter> MonsterClass;

	// 워존 밖(둘레·숲) AISpawn 에 놓을 몬스터 수.
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	int32 MonsterCount = 8;

	// 워존 안(ScavPatrol 포인트) 에 놓을 몬스터 수. 파밍의 중심이라 더 빽빽하게.
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	int32 WarZoneMonsterCount = 20;

	// APGMonsterCharacter::GetPresetVisuals 이름들. 세력 프리셋이 비어 있을 때의 기본 목록.
	UPROPERTY(Config, EditAnywhere, Category = "Combat")
	TArray<FName> MonsterPresets = { FName(TEXT("Slime")), FName(TEXT("Cactus")), FName(TEXT("Beholder")), FName(TEXT("ChestMonster")) };

	// ---- 세력 (기획 v0.4 3.3.5). 워존 몬스터 중 C 마리 → 세력 C, B 마리 → 세력 B, 나머지와 워존 밖 전부 → 세력 A. ----
	// 워존을 중심 기준 각도로 3등분해 구역마다 한 세력. A 구역 = 작은 몬스터 떼, B 구역 = 크리처(램페이지) 2~3, C 구역 = 보스 로봇.
	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0"))
	int32 FactionBCount = 3;

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0"))
	int32 FactionCCount = 0;

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction")
	TArray<FName> FactionAPresets = { FName(TEXT("Slime")), FName(TEXT("Cactus")), FName(TEXT("Beholder")), FName(TEXT("ChestMonster")) };

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction")
	TArray<FName> FactionBPresets = { FName(TEXT("Rampage")) };

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction")
	TArray<FName> FactionCPresets;

	// 보스 로봇이 속할 세력. C 면 A·B 몬스터와 서로 싸운다(세력 충돌). None 이면 플레이어만 상대.
	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction")
	EPGMonsterFaction BossFaction = EPGMonsterFaction::C;

	// 죽은 뒤 다시 채우기까지의 시간(초). 0 이면 리스폰 없음. 자리 주변 RespawnPlayerClearRadius 안에 플레이어가 있으면 기다린다.
	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0"))
	float FactionARespawnSeconds = 60.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0"))
	float FactionBRespawnSeconds = 180.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0"))
	float RespawnPlayerClearRadius = 3000.0f;

	// 세력별 몸집 배율. B 크리처(램페이지)는 보스 로봇만큼은 아니어도 사람을 내려다보게. 발 높이·사거리·속도도 같이 커진다(스포너 ApplyFactionTuning).
	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0.5", ClampMax = "12.0"))
	float FactionBScale = 5.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0.5", ClampMax = "12.0"))
	float FactionCScale = 1.3f;

	// B 크리처 시야(cm). 다른 몬스터는 60m. 크리처는 크고 강해서 넓게 보면 플레이어가 스폰되자마자 달려온다.
	// 35m 였을 때는 몸집(키 15m)에 비해 너무 좁아서 코앞까지 가야 반응했다. 50m 로 넓힌다(여전히 일반 몬스터 60m 보다는 좁다).
	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "500.0"))
	float FactionBSightRadius = 5000.0f;

	// B 크리처 영역 반경(cm). 스폰 자리에서 이만큼 끌려 나가면 쫓기를 그만두고 돌아간다. 이 밖의 대상은 처음부터 쫓지 않는다. 0 이면 끔.
	// 돌아가는 동안은 아무도 상대하지 않고, 영역 반경의 40% 안으로 들어오면 다시 싸운다(경계에서 쫓다 말다 반복하지 않게).
	// 60m 였을 때는 조금만 도망가도 돌아가 버려서 "쫓아오는 범위가 좁다"는 느낌이었다. 90m 로 넓힌다.
	UPROPERTY(Config, EditAnywhere, Category = "Combat|Faction", meta = (ClampMin = "0.0"))
	float FactionBLeashRadius = 9000.0f;

	// 보스·탈것 로봇이 죽은 뒤 다시 나오기까지(초). 0 이면 없음. 원래 자리가 아니라 비어 있는 AI 포인트 중 플레이어에서 먼 곳에 랜덤.
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	float BossRespawnSeconds = 300.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	float RideableRobotRespawnSeconds = 120.0f;

	UPROPERTY(Config, EditAnywhere, Category = "Combat")
	bool bSpawnBoss = true;

	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "1.0", ClampMax = "12.0"))
	float BossScale = 2.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	int32 RideableRobotCount = 1;

	// 탈것 로봇 크기 배율. 카메라 거리도 같이 커진다.
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "1.0", ClampMax = "12.0"))
	float RideableRobotScale = 1.0f;

	// 시작 지역 하나당 차 대수(9/27 멀티: 전체 대수가 아니라 지역마다. 지역 4곳이면 4배).
	UPROPERTY(Config, EditAnywhere, Category = "Combat", meta = (ClampMin = "0"))
	int32 VehicleCount = 2;

	// APGVehiclePawn::GetPresetMeshPath 이름들.
	UPROPERTY(Config, EditAnywhere, Category = "Combat")
	TArray<FName> VehiclePresets = { FName(TEXT("SportsCar")), FName(TEXT("Pickup")), FName(TEXT("SUV")), FName(TEXT("Hatchback")) };

	// 탱크(APGTankPawn): 시작 지역마다 한 대, 그 지역 첫 자리에서 맵 바깥쪽 벽 너머에 숨겨 둔다(멀티 9/27: 예전엔 첫 지역에만).
	UPROPERTY(Config, EditAnywhere, Category = "Combat")
	bool bSpawnHiddenTank = true;

	// 피날레(보스 처치 → 우주전함 등장). 끄면 APGFinaleDirector::NotifyBossDefeated 가 그냥 돌아간다 —
	// 피날레 코드가 다른 시스템을 건드리지 않게 막는 유일한 스위치다(기획서 §5).
	UPROPERTY(Config, EditAnywhere, Category = "Finale")
	bool bEnableFinale = true;
};
