// 한 판(출격 → 탈출/사망) 기록에 쓰는 자료형. 흐름(타이틀 → 로비 → 게임 → 스코어보드 → 로비) 전체가 이 구조체를 주고받는다.
//
// 왜 따로 헤더인가: UI 담당이 스코어보드·로비 위젯을 만들 때 이 파일 하나만 보면 "무엇을 보여줄 수 있나" 를 알 수 있게.
// 서브시스템(PGRunSubsystem)은 이 값을 채우는 쪽이고, 위젯은 읽기만 한다.
//
// 나중에 마스터 서버로 옮길 때: FPGRunRecord 를 그대로 JSON 으로 보내면 된다. enum 은 이름(문자열)으로 보내야 한다 —
//   숫자로 보내면 enum 순서를 바꿀 때 옛 기록이 다 틀어진다(Docs/ProceduralLevelServerHandoff.md 의 "안정 이름" 규칙과 같다).
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGObjectTypes.h" // FPGItemStack (ItemId, Count) — 상자·시체 루팅과 같은 단위를 쓴다
#include "PGRunTypes.generated.h"

// 판이 어떻게 끝났나. 새 값은 **뒤에만** 붙인다(저장 파일에 숫자로 남는다).
UENUM(BlueprintType)
enum class EPGRunResult : uint8
{
	None       UMETA(DisplayName = "기록 없음"),
	InProgress UMETA(DisplayName = "진행 중"),
	Extracted  UMETA(DisplayName = "탈출"),
	Died       UMETA(DisplayName = "사망"),
	TimedOut   UMETA(DisplayName = "시간 초과"),
	Aborted    UMETA(DisplayName = "중단"),   // 판 도중에 레벨을 떠남(콘솔·PIE 종료). 통계에는 안 넣는다
};

// 한 판의 기록. 판이 시작될 때 만들어지고 끝날 때 잠긴다. 스코어보드가 이걸 그대로 보여 준다.
USTRUCT(BlueprintType)
struct PROJECTPG_API FPGRunRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	EPGRunResult Result = EPGRunResult::None;

	// 시작 시각(현지 시간)과 걸린 시간(초).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	FDateTime StartedAt;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	float DurationSeconds = 0.0f;

	// 어떤 맵(시드)이었나. 같은 시드면 같은 맵이라 "그 판" 을 다시 볼 수 있다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	int64 MapSeed = 0;

	// 탈출 방법: 탈출구 이름(체크포인트·헬기·차량 등). 사망이면 죽인 것의 이름, 시간 초과면 비어 있다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	FText HowItEnded;

	// 처치 수. 몬스터(A·B 세력·크리처)와 보스(로봇)를 따로 센다 — 스코어보드에서 보스 처치는 따로 크게 보여 줄 것.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	int32 MonsterKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	int32 BossKills = 0;

	// 피날레 드래곤을 떨어뜨렸나.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	bool bDragonKilled = false;

	// 걸어서·타고 움직인 거리(cm). 순간이동(스폰 위치로 옮김 등)은 안 센다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	float DistanceCm = 0.0f;

	// 플레이어가 쏜 발 수: 가발 광선·날으는 차 빔·탱크 포·전함 주포/미사일·총(PGWeaponComponent). 들이받기는 안 센다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	int32 ShotsFired = 0;

	// 판이 끝난 순간 가지고 있던 아이템(ItemId·개수). 탈출이면 창고로 들어가고, 사망이면 "잃은 아이템" 으로 보여 준다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	TArray<FPGItemStack> ItemsAtEnd;

	// 판 안에서 주운 횟수(줍기 이벤트 누적). 인벤토리를 못 읽는 폰이어도 이 값은 남는다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Run")
	int32 ItemsPickedUp = 0;

	bool IsFinished() const { return Result != EPGRunResult::None && Result != EPGRunResult::InProgress; }
};

// 여러 판을 합친 통계. 로비의 "통계" 화면(기획서 캐릭터 화면 > 통계)이 읽는다.
USTRUCT(BlueprintType)
struct PROJECTPG_API FPGPlayerStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 TotalRuns = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 Extractions = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 Deaths = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 TimeOuts = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 TotalMonsterKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 TotalBossKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	int32 DragonKills = 0;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	float TotalDistanceCm = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Stats")
	float TotalPlaySeconds = 0.0f;
};
