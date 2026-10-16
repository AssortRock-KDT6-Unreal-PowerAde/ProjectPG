#pragma once

#include "CoreMinimal.h"
#include "Components/BoxComponent.h"
#include "LootSlotComponent.generated.h"

class AInteractActor;

// 상자 자리 표시 (부품).
// 게임에서: 판이 시작되면 서버가 타일마다 이 표시들을 모아 그중 몇 개를 고르고, 고른 자리에 상자를 스폰한다.
//           예: 타일에 표시 3개 → 어떤 판은 없음, 어떤 판은 그중 1개(개수 범위는 DA_MapAssets 의 "상자").
// 쓰는 법: 타일·시설 BP 를 열고 "추가" → LootSlot. 노란 상자 선을 상자가 놓일 바닥 위에 올려 둔다(선 아래면 = 상자 바닥).
//           화살표(빨간 X축) 쪽이 상자 앞면이다. 게임에서는 안 보이고 부딪히지도 않는다.
// 왜 상자를 BP 에 직접 넣지 않나: 타일은 서버와 클라가 각자 짓는다. 타일 안에 상자를 넣으면 서로 모르는 상자가 두 개 생긴다.
//           표시만 넣어 두고 서버가 스폰하면, 위치는 사람이 정한 그대로고 상자는 클라에 복제된다.
// 레벨(시설 레벨)에 놓을 때는 ALootSlotActor 를 끌어다 놓는다.
UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent, DisplayName = "LootSlot"))
class PROJECTPG_API ULootSlotComponent : public UBoxComponent
{
	GENERATED_BODY()

public:
	ULootSlotComponent();

	// 이 자리에만 나올 상자 BP. 비우면 DA_MapAssets 의 상자 목록에서 고른다.
	// 예: 캠프 타일 자리에는 의료 상자만.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "상자 자리")
	TArray<TSoftClassPtr<AInteractActor>> ContainerClasses;

	// 같은 타일 안에서 따로 고를 묶음 이름. 비우면 그 타일의 자리가 전부 한 묶음.
	// 예: 1층 자리는 "Ground", 2층 자리는 "Upper" → 층마다 따로 몇 개씩 고른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "상자 자리")
	FName SlotGroup = NAME_None;
};
