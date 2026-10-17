#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/DataTable.h"
#include "Actors/MapBuilder.h"
#include "MapItemSpawner.generated.h"

class AActor;
class AInteractActor;

// 상자 자리 하나를 적어 둔 것.
// 타일 액터는 맵이 메시를 HISM(같은 메시를 한 덩어리로 그리는 부품)으로 옮긴 뒤 지워 버린다.
// 그래서 지우기 전에 자리 표시(ULootSlotComponent)의 위치·설정을 여기에 옮겨 적어 둔다.
struct FLootSlotRecord
{
	// 이 값이 같은 자리끼리 한 묶음이다(타일 하나 + SlotGroup).
	FString GroupKey;
	FName SlotGroup;
	// 에디터에서 보이는 노란 상자 선의 아랫면 가운데 = 상자가 놓일 바닥 점.
	FVector Bottom = FVector::ZeroVector;
	float Yaw = 0.0f;
	TArray<TSoftClassPtr<AInteractActor>> ContainerClasses;
};

// 상자 자리에 나오는 아이템 한 줄 (데이터 테이블 DT_LootSpawn 의 한 줄).
// 게임에서: "붕대는 흔하게(무게 5), 1~3개씩, 아무 자리에서나" / "에픽 소총은 드물게(무게 1), 좋은 자리(3)에서만".
// 왜 표로 뺐나: 무엇이 얼마나 자주 나오는지는 기획·밸런스 값이라, 코드 없이 바꿀 수 있어야 해서.
// 지금은 맵이 상자 안을 채우지 않는다(형님 쪽 AInteractActor 가 채운다).
// DT_LootSpawn 에셋이 이 구조를 쓰고 있어서, 지우지 않고 남겨 둔다.
USTRUCT(BlueprintType)
struct FLootSpawnRow : public FTableRowBase
{
	GENERATED_BODY()

	// 아이템 표(ItemTable)의 번호. 예: 1001 = 권총.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName ItemID = NAME_None;

	// 뽑힐 무게. 다른 줄보다 2배 크면 2배 자주 나온다. 0 이면 안 나온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0"))
	int32 Weight = 1;

	// 이 등급 이상인 자리에서만 나온다(1 = 아무 데나, 3 = 창고 2층 같은 좋은 자리만).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1", ClampMax = "3"))
	int32 MinTier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 MinQuantity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1"))
	int32 MaxQuantity = 1;
};

// 상자 담당.
// 게임에서: 판이 시작되고 맵이 다 지어지면, 상자 자리(Loot 지점)마다 상자 하나를 놓는다.
//           상자는 DA_MapAssets 의 상자 BP 목록(부모 AInteractActor)에서 자리마다 하나 고른다.
//           안은 채우지 않는다 — 무엇이 들지는 형님 쪽(AInteractActor)이 정한다.
// 같은 시드 = 같은 자리에 같은 상자. 자리마다 정해진 씨앗(PointSeed)으로만 주사위를 굴린다.
// 서버만 놓는다. 접속한 사람은 서버가 놓은 상자를 자동으로 전달(복제)받는다.
// 타일처럼 각자 만들면, 서버 상자와 내 상자가 따로 생겨서 서로 모르는 상자가 두 개가 된다.
UCLASS(Transient)
class UMapItemSpawner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 상자 자리마다 상자 놓기. 맵 Tick 이 매번 부르지만, 지점 끼임 정리가 끝난 뒤 딱 한 번만 일한다.
	// 왜 그때 하나: 정리 전에는 자리가 벽 속일 수 있고, 시설 레벨(창고 바닥)이 아직 안 불러와져서 바닥을 못 찾는다.
	void SpawnLootOnce();

	// (테스트용) 에디터에서 플레이할 때만: 시작 자리에서 바라보는 쪽 2.5m 앞에 상자 하나를 놓는다.
	// 왜: 상자를 열어 보려고 매번 넓은 맵을 돌아다니지 않게. 패키징한 게임에서는 아무것도 안 한다.
	// 시작 구역 담당이 플레이어를 자리에 세운 직후 부른다(서버만).
	void SpawnTestContainerInFront(const FVector& SeatLocation, float Yaw);

	// 타일 담당이 타일 액터를 지우기 직전에 부른다: 그 타일의 상자 자리를 적어 둔다.
	void RecordLootSlots(AActor* Tile);
	// 타일을 다시 지을 때(맵 다시 만들기) 적어 둔 자리를 비운다.
	void ResetRecordedLootSlots();

private:
	// 액터에 달린 상자 자리를 기록으로 옮기고, 그 액터가 덮는 칸을 Cells 에 더한다.
	static void AppendSlotsOf(AActor* Owner, TArray<FLootSlotRecord>& OutSlots, TSet<FIntPoint>& OutCells);

	// 상자 하나를 바닥 점에 놓는다(서버만). 표 붙이기·무버블·바닥 맞추기까지.
	AInteractActor* SpawnContainer(UClass* ContainerClass, const FVector& FloorPoint, const FRotator& Rotation);
	// 상자 아래면을 바닥에 붙인다.
	static void PlaceOnFloor(AActor* Container, const FVector& FloorPoint);

	bool bSpawned = false;

	// DA_MapAssets 에서 읽은 상자 BP 중 실제로 찾은 것(SpawnLootOnce 가 채운다).
	UPROPERTY()
	TArray<TObjectPtr<UClass>> ContainerClasses;
	// 테스트 상자를 몇 개 놓았나(상자 종류를 돌아가며 고르는 데 쓴다).
	int32 TestContainerCount = 0;

	// 지워진 타일에서 옮겨 적은 상자 자리와 그 타일들이 덮는 칸.
	TArray<FLootSlotRecord> RecordedSlots;
	TSet<FIntPoint> RecordedSlotCells;

	// 이번 판에 놓은 상자. 바닥 찾기 선이 이미 놓은 상자 위에 걸리지 않게 빼는 데도 쓴다.
	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedContainers;

	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
};
