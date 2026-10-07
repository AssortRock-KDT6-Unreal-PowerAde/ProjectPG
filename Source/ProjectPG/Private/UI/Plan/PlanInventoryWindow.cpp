#include "UI/Plan/PlanInventoryWindow.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Common/TableData.h"
#include "Components/ComboBoxString.h"
#include "Components/EditableTextBox.h"
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
		for (const FText& Label : FilterLabels)
			FilterCombo->AddOption(Label.ToString());
		FilterCombo->SetSelectedIndex(FMath::Clamp(FilterIndex, 0, FilterLabels.Num() - 1));
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

// 격자가 아이템 칸을 다시 그리면(옮김·서버 응답) 투명도가 1 로 돌아가므로, 검색 중에는 자주 다시 맞춘다.
void UPlanInventoryWindow::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const bool bActive = !SearchText.IsEmpty() || FilterIndex > 0;
	// 끄는 중인 아이템은 형님 코드가 반투명으로 둔다 — 그동안은 건드리지 않는다.
	if ((!bActive && !bFilterWasActive) || UWidgetBlueprintLibrary::IsDragDropping())
		return;
	ReapplyTimer -= InDeltaTime;
	if (ReapplyTimer <= 0.0f)
	{
		ReapplyTimer = 0.2f;
		ApplyFilter();
	}
}

bool UPlanInventoryWindow::Matches(const UItemWidget* Item) const
{
	const FItemTableRow* Data = Item->GetCachedItemData();
	if (!Data)
		return true;
	if (FilterIndex > 0 && static_cast<int32>(Data->ItemType) != FilterIndex - 1)
		return false;
	return SearchText.IsEmpty() || Data->DisPlayName.ToString().Contains(SearchText, ESearchCase::IgnoreCase);
}

void UPlanInventoryWindow::ApplyFilter()
{
	const bool bActive = !SearchText.IsEmpty() || FilterIndex > 0;
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
