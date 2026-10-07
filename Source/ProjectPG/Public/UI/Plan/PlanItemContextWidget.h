// 우클릭 메뉴에 "돌리기"를 더한 자식 메뉴(기획서 1.2.1).

#pragma once

#include "CoreMinimal.h"
#include "UI/ItemContextWidget.h"
#include "Common/TableData.h"
#include "PlanItemContextWidget.generated.h"

class UButton;
class UInventoryComponent;

// 우클릭 메뉴 자식.
// 게임에서: 인벤토리 아이템을 우클릭하면 나오는 메뉴에 "돌리기" 버튼이 하나 더 있다. 누르면 아이템이 그 자리에서 90° 돈다
//   (가로로 긴 총이 세로로 서는 식). 자리가 모자라 돌릴 수 없으면 아무 일도 없이 메뉴만 닫힌다.
// 왜 자식인가: 형님 메뉴(UItemContextWidget)는 그대로 두고 버튼 하나만 더하려고. 형님 버튼들은 부모가 그대로 처리한다.
// 돌리는 길: 형님이 아이템을 끌어 놓을 때 쓰는 것과 같은 InventoryComponent::MoveItem(같은 칸, 돌림만 바꿈).
//   그래서 로비(웹 서버 저장)·게임 안(서버 동기화) 어디서든 형님 동기화 길을 그대로 탄다.
// "나누기"(겹친 아이템 쪼개기)는 형님 쪽에 쪼개기 요청(서버 저장)이 없어서 넣지 않았다.
UCLASS()
class PROJECTPG_API UPlanItemContextWidget : public UItemContextWidget
{
	GENERATED_BODY()

public:
	virtual void NativeConstruct() override;

	// 우클릭한 아이템 칸(UFitIconItemWidget)이 부른다: 이 아이템이 어느 인벤토리의 어느 칸에 있는지.
	void SetRotateTarget(UInventoryComponent* Inventory, const FGuid& ContainerGuid, const FItemInstance& Item, const FItemTableRow& Data);

protected:
	// WBP 에 RotateButton 이 없으면 돌리기만 빠진다.
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> RotateButton;

private:
	UFUNCTION()
	void OnRotateClicked();

	TWeakObjectPtr<UInventoryComponent> RotateInventory;
	FGuid RotateContainer;
	FItemInstance RotateItem;
};
