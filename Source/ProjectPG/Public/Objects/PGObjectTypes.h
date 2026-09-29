// ProjectPG 오브젝트 원형(archetype) 공통 타입.
//
// 노션 "ProjectPG 오브젝트 관리 목록"의 152개 항목은 여기 정의한 원형 몇 개와
// 데이터(카탈로그 행)의 조합으로 표현한다. 항목마다 클래스를 새로 만들지 않는다.
// 예: 군용 상자 / 탄약 상자 / 금고는 전부 AItemContainerActor 하나이고,
//     메시·LootTable·잠금·열리는 시간만 카탈로그 행 값으로 달라진다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "PGObjectTypes.generated.h"

class UStaticMesh;
class AActor;

// 재사용 코드 원형. 노션의 "재사용 코드 원형" 열과 1:1로 대응한다.
UENUM(BlueprintType)
enum class EPGObjectArchetype : uint8
{
	None,
	Container      UMETA(DisplayName = "아이템 상자 원형"),
	Door           UMETA(DisplayName = "문 원형"),
	FloorItem      UMETA(DisplayName = "바닥 아이템 원형"),
	Extraction     UMETA(DisplayName = "탈출구 원형"),
	Corpse         UMETA(DisplayName = "시체 루팅 (컴포넌트 부착)"),
	Service        UMETA(DisplayName = "서비스 상호작용 원형"),
	Device         UMETA(DisplayName = "작동 장치 원형"),
	Destructible   UMETA(DisplayName = "파괴물 원형"),
	QuestObject    UMETA(DisplayName = "퀘스트 오브젝트 원형"),
	SpawnSocket    UMETA(DisplayName = "생성 지점 원형"),
	Environment    UMETA(DisplayName = "코드 원형 없음 (환경 요소)")
};

// 생성 지점(소켓)의 종류. 노션 OBJ-116 ~ OBJ-133 "생성·배치" 대분류와 대응한다.
// 타일 제작 시 레벨에 소켓을 놓아 두면, 서버가 시드에 맞춰 종류에 맞는 오브젝트를 골라 생성한다.
UENUM(BlueprintType)
enum class EPGSpawnSocketKind : uint8
{
	Generic        UMETA(DisplayName = "범용 오브젝트 소켓 (OBJ-133)"),
	Item           UMETA(DisplayName = "일반 아이템 (OBJ-116)"),
	Weapon         UMETA(DisplayName = "무기 (OBJ-117)"),
	Ammo           UMETA(DisplayName = "탄약 (OBJ-118)"),
	Consumable     UMETA(DisplayName = "소비품 (OBJ-119)"),
	Key            UMETA(DisplayName = "열쇠 (OBJ-120)"),
	Fuel           UMETA(DisplayName = "연료 (OBJ-121)"),
	QuestItem      UMETA(DisplayName = "퀘스트 아이템 (OBJ-122)"),
	Container      UMETA(DisplayName = "상자 (OBJ-123)"),
	Safe           UMETA(DisplayName = "금고 (OBJ-124)"),
	Shop           UMETA(DisplayName = "상점 (OBJ-125)"),
	Courier        UMETA(DisplayName = "택배 NPC (OBJ-126)"),
	Extraction     UMETA(DisplayName = "탈출구 (OBJ-127)"),
	Vehicle        UMETA(DisplayName = "차량 (OBJ-128)"),
	PlayerStart    UMETA(DisplayName = "플레이어 시작 (OBJ-129)"),
	MonsterA       UMETA(DisplayName = "세력 A 몬스터 (OBJ-130)"),
	MonsterB       UMETA(DisplayName = "세력 B 몬스터 (OBJ-131)"),
	MonsterC       UMETA(DisplayName = "세력 C 몬스터 (OBJ-132)")
};

// 노션 "우선순위" 열.
UENUM(BlueprintType)
enum class EPGObjectPriority : uint8
{
	P0_Common   UMETA(DisplayName = "P0 공통 기반"),
	P1_MVP      UMETA(DisplayName = "P1 MVP 필수"),
	P2_Main     UMETA(DisplayName = "P2 본편"),
	P3_Extend   UMETA(DisplayName = "P3 확장"),
	Hold        UMETA(DisplayName = "보류")
};

// 문 원형의 열림 연출 종류. 외여닫이/양문/미닫이/셔터/해치가 전부 같은 클래스다.
UENUM(BlueprintType)
enum class EPGDoorMotion : uint8
{
	SwingSingle  UMETA(DisplayName = "외여닫이 (회전 1장)"),
	SwingDouble  UMETA(DisplayName = "양문형 (회전 2장)"),
	Slide        UMETA(DisplayName = "미닫이 (옆으로 이동)"),
	Vertical     UMETA(DisplayName = "셔터·차고문 (위로 이동)"),
	Hatch        UMETA(DisplayName = "맨홀·해치 (위로 회전)")
};

// 서비스 상호작용 원형이 여는 화면 종류. 가격·재고·배송비는 각 담당 시스템의 책임이다.
UENUM(BlueprintType)
enum class EPGServiceKind : uint8
{
	Shop     UMETA(DisplayName = "상점"),
	Courier  UMETA(DisplayName = "택배"),
	Craft    UMETA(DisplayName = "제조"),   // 9/20 사용자 재승인(부스 4종 구현)
	Exchange UMETA(DisplayName = "교환소")  // 드래곤 전리품 → 특수 장비(가발 등) 교환
};

// 작동 장치 원형의 동작 방식.
UENUM(BlueprintType)
enum class EPGDeviceMode : uint8
{
	Toggle         UMETA(DisplayName = "토글 (스위치·레버·차단기)"),
	Momentary      UMETA(DisplayName = "누르는 동안만 (버튼)"),
	Timed          UMETA(DisplayName = "작동 후 일정 시간 (시간 제한 스위치)"),
	PressurePlate  UMETA(DisplayName = "밟으면 작동 (압력판)")
};

// 파괴물 원형이 어떤 피해로 부서지는지.
UENUM(BlueprintType)
enum class EPGDestroyCondition : uint8
{
	AnyDamage      UMETA(DisplayName = "아무 피해"),
	ExplosiveOnly  UMETA(DisplayName = "폭발 피해만"),
	ToolOnly       UMETA(DisplayName = "도구 아이템으로 상호작용")
};

// 퀘스트 오브젝트 원형의 동작 방식.
UENUM(BlueprintType)
enum class EPGQuestObjectMode : uint8
{
	Investigate  UMETA(DisplayName = "조사 (일정 시간 F 유지)"),
	DeliverItem  UMETA(DisplayName = "아이템 납품"),
	Operate      UMETA(DisplayName = "작동 (켜기/끄기)")
};

// 탈출구 원형의 시작 조건.
UENUM(BlueprintType)
enum class EPGExtractionTrigger : uint8
{
	Overlap   UMETA(DisplayName = "영역에 서 있으면 진행"),
	Interact  UMETA(DisplayName = "F로 시작 (헬기·선박·차량)")
};

// 루팅 테이블 한 줄. ItemId는 팀 인벤토리의 FItemTableRow.ItemID와 같은 이름을 쓴다.
USTRUCT(BlueprintType)
struct FPGLootEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ItemId;

	// 가중치. 같은 테이블 안에서 상대값으로만 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	int32 MinCount = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	int32 MaxCount = 1;
};

// 루팅 테이블. DataTable 행이거나, 코드에서 등록한 런타임 테이블이다.
USTRUCT(BlueprintType)
struct FPGLootTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FPGLootEntry> Entries;

	// 몇 번 뽑을지. 상자는 보통 2~4, 시체는 1~2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0"))
	int32 RollCount = 1;

	// 같은 아이템이 두 번 뽑혀도 되는지.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bAllowDuplicates = true;
};

// 루팅 결과 한 묶음.
USTRUCT(BlueprintType)
struct FPGItemStack
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;
};

// 오브젝트 카탈로그 한 행 = 노션 목록의 한 줄(OBJ-xxx).
// 원형 클래스 하나에 이 행을 적용(ApplyCatalogRow)하면 그 오브젝트가 된다.
USTRUCT(BlueprintType)
struct FPGObjectCatalogRow : public FTableRowBase
{
	GENERATED_BODY()

	// 노션 오브젝트 ID (예: OBJ-001). 로그와 서버 Manifest에서 이 이름을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ObjectId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPGObjectArchetype Archetype = EPGObjectArchetype::None;

	// 비워 두면 원형의 기본 클래스를 쓴다. BP 자식으로 연출만 바꾼 경우 여기 지정한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftClassPtr<AActor> ActorClass;

	// 겉모습. 상자 종류를 바꾸는 것은 이 메시를 바꾸는 것이다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UStaticMesh> Mesh;

	// 문이 열리는 방식. 회전·미닫이·셔터·해치가 데이터로 갈린다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPGDoorMotion Motion = EPGDoorMotion::SwingSingle;
	
	// 뚜껑, 두 번째 문짝처럼 움직이는 보조 메시.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSoftObjectPtr<UStaticMesh> SecondaryMesh;

	// 상자·시체가 쓰는 루팅 테이블 이름. 바닥 아이템은 ItemId 하나만 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName LootTableId;

	// 바닥 아이템 원형 전용: 어떤 아이템이 몇 개인지.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "1"))
	int32 ItemCount = 1;

	// 잠금. 잠긴 상자(OBJ-009), 잠긴 문(OBJ-017), 잠긴 펜스(OBJ-020)가 이 두 값으로 갈린다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bLocked = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (EditCondition = "bLocked"))
	FName RequiredKeyId;

	// 0이면 즉시, 0보다 크면 그 시간 동안 F를 유지해야 한다. 금고(OBJ-010)는 여기가 길다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float InteractSeconds = 0.0f;

	// 어떤 소켓에서 생성될 수 있는지.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPGSpawnSocketKind SocketKind = EPGSpawnSocketKind::Generic;

	// 소켓 Tier(1~3)와 비교한다. 0이면 Tier를 따지지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0", ClampMax = "3"))
	uint8 Tier = 0;

	// 같은 소켓 후보 사이의 선택 가중치.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (ClampMin = "0.0"))
	float SpawnWeight = 1.0f;

	// 한 맵에 하나만 생성한다 (예: 퀘스트 상자). "중복 방지" 요구사항.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUniquePerMap = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EPGObjectPriority Priority = EPGObjectPriority::P2_Main;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bMVP = false;

	// 노션 "포함 여부". 꺼진 행은 생성 후보에서 빠지지만 데이터는 남긴다(삭제 금지 규칙).
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bIncluded = true;

	// 퀘스트 담당이 구독하는 태그. 문·차량 이용 목표(OBJ-093/094)도 이 태그로 잡는다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName QuestTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Notes;
	

};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGObjectInteractedSignature, AActor*, Object, APawn*, Interactor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPGQuestEventSignature, FName, QuestTag, AActor*, Source, APawn*, Pawn);

DECLARE_LOG_CATEGORY_EXTERN(LogPGObjects, Log, All);
