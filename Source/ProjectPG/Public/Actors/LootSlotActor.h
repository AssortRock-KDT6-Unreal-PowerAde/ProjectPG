#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootSlotActor.generated.h"

class ULootSlotComponent;

// 레벨에 끌어다 놓는 상자 자리 표시.
// 시설 레벨(LD_Facility_*)처럼 BP 가 아니라 레벨로 된 곳에 쓴다. BP 안에서는 ULootSlotComponent 를 바로 단다.
// 같은 레벨에 놓인 표시들(같은 SlotGroup)이 한 묶음이 되어, 판마다 그중 몇 개를 고른다.
// 복제하지 않는다: 서버가 읽고 그 자리에 상자를 스폰할 뿐, 표시 자체는 각자 레벨에 있으면 된다.
UCLASS(meta = (DisplayName = "Loot Slot"))
class PROJECTPG_API ALootSlotActor : public AActor
{
	GENERATED_BODY()

public:
	ALootSlotActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "상자 자리")
	TObjectPtr<ULootSlotComponent> Slot;
};
