#include "UI/Plan/PlanInventoryWindow.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Common/TableData.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
#include "Components/InventoryComponent.h"
#include "UI/ItemWidget.h"

void UPlanInventoryWindow::NativeConstruct()
{
	Super::NativeConstruct();
	if (SearchBox)
	{
		SearchBox->OnTextChanged.RemoveDynamic(this, &UPlanInventoryWindow::HandleSearchChanged);
		SearchBox->OnTextChanged.AddDynamic(this, &UPlanInventoryWindow::HandleSearchChanged);
	}
	if (FilterCombo)
	{
		FilterCombo->OnSelectionChanged.RemoveDynamic(this, &UPlanInventoryWindow::HandleFilterChanged);
		FilterCombo->ClearOptions();
		for (const FPlanFilterOption& Option : FilterOptions)
			FilterCombo->AddOption(Option.Label.ToString());
		FilterCombo->SetSelectedIndex(FMath::Clamp(FilterIndex, 0, FilterOptions.Num() - 1));
		FilterCombo->OnSelectionChanged.AddDynamic(this, &UPlanInventoryWindow::HandleFilterChanged);
	}
}

void UPlanInventoryWindow::HandleSearchChanged(const FText& Text)
{
	SearchText = Text.ToString().TrimStartAndEnd();
	ApplyFilter();
}

void UPlanInventoryWindow::HandleFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	FilterIndex = FilterCombo ? FMath::Max(0, FilterCombo->GetSelectedIndex()) : 0;
	ApplyFilter();
}

bool UPlanInventoryWindow::IsFilterActive() const
{
	return !SearchText.IsEmpty() || (FilterOptions.IsValidIndex(FilterIndex) && !FilterOptions[FilterIndex].bAllTypes);
}

void UPlanInventoryWindow::NativeDestruct()
{
	for (TWeakObjectPtr<UInventoryComponent>* Watched : { &WatchedPlayerInventory, &WatchedContainerInventory })
	{
		if (UInventoryComponent* Inventory = Watched->Get())
			Inventory->OnInventoryUpdated.RemoveDynamic(this, &UPlanInventoryWindow::HandleInventoryUpdated);
		Watched->Reset();
	}
	Super::NativeDestruct();
}

void UPlanInventoryWindow::HandleInventoryUpdated()
{
	bReapplyPending = true;
}

void UPlanInventoryWindow::WatchInventories()
{
	auto Rewatch = [this](TWeakObjectPtr<UInventoryComponent>& Watched, UInventoryComponent* Current)
	{
		if (Watched.Get() == Current)
			return;
		if (UInventoryComponent* Old = Watched.Get())
			Old->OnInventoryUpdated.RemoveDynamic(this, &UPlanInventoryWindow::HandleInventoryUpdated);
		Watched = Current;
		if (Current)
			Current->OnInventoryUpdated.AddUniqueDynamic(this, &UPlanInventoryWindow::HandleInventoryUpdated);
		bReapplyPending = true; // 새 상자가 열리면 그 칸들도 한 번 맞춘다
	};
	Rewatch(WatchedPlayerInventory, InvenComp);
	Rewatch(WatchedContainerInventory, MainInventoryComp);
}

// 매 프레임 하는 일은 포인터 비교와 표시 확인뿐. 실제로 창을 훑는 건 인벤토리가 바뀐 다음 프레임 한 번.
void UPlanInventoryWindow::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	WatchInventories();
	const bool bActive = IsFilterActive();
	// 끄는 중인 아이템은 형님 코드가 반투명으로 둔다 — 그동안은 건드리지 않는다.
	if (!bReapplyPending || (!bActive && !bFilterWasActive) || UWidgetBlueprintLibrary::IsDragDropping())
		return;
	bReapplyPending = false;
	ApplyFilter();
}

bool UPlanInventoryWindow::Matches(const UItemWidget* Item) const
{
	const FItemTableRow* Data = Item->GetCachedItemData();
	if (!Data)
		return true;
	if (FilterOptions.IsValidIndex(FilterIndex) && !FilterOptions[FilterIndex].bAllTypes
		&& Data->ItemType != FilterOptions[FilterIndex].Type)
		return false;
	return SearchText.IsEmpty() || Data->DisPlayName.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
}

void UPlanInventoryWindow::ApplyFilter()
{
	const bool bActive = IsFilterActive();
	// 위젯 나무를 따라 내려가며 아이템 칸을 찾는다. 격자·장비 칸은 각각 자기 나무를 가진 위젯이라 그 안으로도 들어간다.
	TFunction<void(UWidgetTree*)> Visit = [&](UWidgetTree* Tree)
	{
		if (!Tree)
			return;
		Tree->ForEachWidget([&](UWidget* Widget)
		{
			if (UItemWidget* Item = Cast<UItemWidget>(Widget))
				Item->SetRenderOpacity(!bActive || Matches(Item) ? 1.0f : FilteredOutOpacity);
			else if (UUserWidget* Child = Cast<UUserWidget>(Widget))
				Visit(Child->WidgetTree);
		});
	};
	Visit(WidgetTree);
	bFilterWasActive = bActive;
}
