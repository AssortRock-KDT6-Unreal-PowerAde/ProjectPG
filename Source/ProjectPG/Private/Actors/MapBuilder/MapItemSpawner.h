#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/DataTable.h"
#include "Actors/MapBuilder.h"
#include "MapItemSpawner.generated.h"

class AWorldItemActor;

// 상자 자리에 나오는 아이템 한 줄 (데이터 테이블 DT_LootSpawn 의 한 줄).
// 게임에서: "붕대는 흔하게(무게 5), 1~3개씩, 아무 자리에서나" / "에픽 소총은 드물게(무게 1), 좋은 자리(3)에서만".
// 왜 표로 뺐나: 무엇이 얼마나 자주 나오는지는 기획·밸런스 값이라 코드 없이 바꿀 수 있어야 해서.
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

// 아이템 담당.
// 게임에서: 판이 시작되고 맵이 다 지어지면, 상자 자리(Loot 지점)마다 바닥에 아이템을 놓는다.
//           좋은 자리(등급 3)일수록 더 많이(등급 = 개수) 그리고 더 좋은 것이 나올 수 있다.
// 같은 시드 = 같은 자리에 같은 아이템. 자리마다 정해진 씨앗(PointSeed)으로만 주사위를 굴린다.
// 서버만 한다(아이템은 서버가 정하는 것). 지금 기획은 혼자 하는 판이라 서버 = 내 화면.
UCLASS(Transient)
class UMapItemSpawner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 상자 자리마다 아이템 놓기. 맵 Tick 이 매번 부르지만, 지점 끼임 정리가 끝난 뒤 딱 한 번만 일한다.
	// 왜 그때: 끼임 정리 전에는 자리가 벽 속일 수 있고, 시설 레벨(창고 바닥)이 아직 안 보여서 바닥을 못 찾는다.
	void SpawnLootOnce();

private:
	bool bSpawned = false;

	// 이번 판에 놓은 아이템. 바닥 찾기 선이 이미 놓은 아이템 위에 걸리지 않게 빼는 데도 쓴다.
	UPROPERTY()
	TArray<TObjectPtr<AWorldItemActor>> SpawnedItems;

	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
};
