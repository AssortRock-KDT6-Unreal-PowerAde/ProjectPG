#include "UI/Plan/PlanItemContextWidget.h"

#include "Components/Button.h"
#include "Components/InventoryComponent.h"
#include "Core/UIManagerSubSystem.h"

void UPlanItemContextWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (RotateButton)
	{
		RotateButton->OnClicked.RemoveDynamic(this, &UPlanItemContextWidget::OnRotateClicked);
		RotateButton->OnClicked.AddDynamic(this, &UPlanItemContextWidget::OnRotateClicked);
		// 어느 아이템인지 받기 전에는 숨긴다.
		RotateButton->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UPlanItemContextWidget::SetRotateTarget(UInventoryComponent* Inventory, const FGuid& ContainerGuid, const FItemInstance& Item, const FItemTableRow& Data)
{
	RotateInventory = Inventory;
	RotateContainer = ContainerGuid;
	RotateItem = Item;
	if (!RotateButton)
		return;
	// 정사각형 아이템은 돌려도 같으니 버튼을 숨긴다. 장비 칸 안의 아이템도 돌릴 일이 없다.
	const bool bSquare = Data.GridSize.X == Data.GridSize.Y;
	const bool bCanRotate = Inventory && ContainerGuid.IsValid() && !bSquare && !Inventory->IsEquipContainer(ContainerGuid);
	RotateButton->SetVisibility(bCanRotate ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}

void UPlanItemContextWidget::OnRotateClicked()
{
	UInventoryComponent* Inventory = RotateInventory.Get();
	bool bMoved = false;
	if (Inventory)
	{
		const bool bNewRotated = !RotateItem.bIsRotated;
		// 같은 칸에서 돌린 모양이 들어가는지 먼저 본다(자기 자신은 빼고). 들어가면 끌어 놓기와 같은 길로 옮긴다.
		if (Inventory->CanPlaceItemByGuid(RotateContainer, RotateItem.ItemID, RotateItem.Position, bNewRotated, RotateItem.GUID))
			bMoved = Inventory->MoveItem(RotateContainer, RotateItem.GUID, RotateItem.Position, bNewRotated);
	}
	UE_LOG(LogTemp, Display, TEXT("[PlanContext] rotate %s at (%d,%d) -> %s"), *RotateItem.ItemID.ToString(),
		RotateItem.Position.X, RotateItem.Position.Y, bMoved ? TEXT("ok") : TEXT("no room"));
	if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(GetWorld()))
		UI->CloseItemContext();
}
