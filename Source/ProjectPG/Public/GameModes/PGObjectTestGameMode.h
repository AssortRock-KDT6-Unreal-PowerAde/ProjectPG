// 오브젝트 스모크 테스트 전용 게임 모드. 맵 생성을 하지 않는다.
// 명령줄에 -PGObjectSmokeTest 가 있으면 StartPlay 직후 테스트를 돌리고 프로세스를 끝낸다.
//   UnrealEditor-Cmd.exe <uproject> "/Engine/Maps/Entry?game=/Script/ProjectPG.PGObjectTestGameMode" -game -nullrhi -unattended -PGObjectSmokeTest
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "PGObjectTestGameMode.generated.h"

UCLASS()
class PROJECTPG_API APGObjectTestGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	APGObjectTestGameMode();
	virtual void StartPlay() override;
};
