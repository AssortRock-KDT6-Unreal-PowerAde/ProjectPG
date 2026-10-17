#pragma once

// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "Commandlets/Commandlet.h"
#include "PGBuildEnemyAICommandlet.generated.h"

// 몬스터 AI 에셋 4개를 코드로 다시 만드는 에디터 전용 명령.
//   UnrealEditor-Cmd.exe ProjectPG.uproject -run=PGBuildEnemyAI -unattended -nosplash
// 만드는 것: ST_Enemy(상태 트리), EQS_FindCover(엄폐 EQS), DA_EnemyAI_Rifle(AIConfig),
//            그리고 CBP_CustomEnemyCharacter 의 AIConfig 칸에 DA 를 넣는다.
// 왜 코드로: 상태 트리 구조가 README 그림과 항상 같게, 그리고 누가 다시 만들어도 똑같이 나오게.
// 처음 한 번 만든 뒤로는 에디터에서 고친 ST·EQS 가 기준이다. 다시 돌려도 이미 있는 에셋은 안 건드린다.
// 처음 모양으로 되돌리고 싶을 때만 -Force 를 붙인다(에디터에서 고친 내용이 사라짐). DA 숫자는 어느 경우에도 안 덮어씀.
UCLASS()
class UPGBuildEnemyAICommandlet : public UCommandlet
{
	GENERATED_BODY()
public:
	virtual int32 Main(const FString& Params) override;
};
