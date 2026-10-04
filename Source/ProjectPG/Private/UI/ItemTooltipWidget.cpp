#include "UI/ItemTooltipWidget.h"

#include "Common/TableData.h"
#include "Components/TextBlock.h"
#include "Core/UIManagerSubSystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"

namespace PGTooltip
{
	FString TypeName(EItemType Type)
	{
		switch (Type)
		{
		case EItemType::Weapon:     return TEXT("무기");
		case EItemType::Armor:      return TEXT("방어구");
		case EItemType::Consumable: return TEXT("소비");
		case EItemType::Quest:      return TEXT("퀘스트");
		case EItemType::Bag:        return TEXT("가방");
		default:                    return TEXT("기타");
		}
	}

	FString SlotName(EEquipSlot Slot)
	{
		switch (Slot)
		{
		case EEquipSlot::MainWeapon: return TEXT("주무기");
		case EEquipSlot::SubWeapon:  return TEXT("보조 무기");
		case EEquipSlot::HelMet:     return TEXT("머리");
		case EEquipSlot::Cloth:      return TEXT("상의");
		case EEquipSlot::Pants:      return TEXT("하의");
		case EEquipSlot::Shose:      return TEXT("신발");
		case EEquipSlot::BackPack:   return TEXT("가방");
		case EEquipSlot::Accuracy1:  return TEXT("보조 칸 1");
		case EEquipSlot::Accuracy2:  return TEXT("보조 칸 2");
		default:                     return TEXT("");
		}
	}
}

void UItemTooltipWidget::SetItem(const FItemInstance& Item, const FItemTableRow& Data)
{
	if (NameText)
		NameText->SetText(FText::FromName(Data.DisPlayName));

	const FString Type = PGTooltip::TypeName(Data.ItemType);
	const FString SlotLabel = PGTooltip::SlotName(Data.EquipSlotType);
	if (DescriptionText)
		DescriptionText->SetText(FText::FromString(!Data.Description.IsEmpty()
			? Data.Description
			: (SlotLabel.IsEmpty() ? FString::Printf(TEXT("%s 아이템."), *Type)
			                  : FString::Printf(TEXT("%s · %s 칸에 장착한다."), *Type, *SlotLabel))));

	if (StatNameText)
		StatNameText->SetText(FText::FromString(SlotLabel.IsEmpty() ? Type : FString::Printf(TEXT("%s · %s"), *Type, *SlotLabel)));

	if (StatValueText)
	{
		const FIntPoint Size = Item.GetCurrentGridSize(&Data);
		StatValueText->SetText(FText::FromString(Data.MaxStack > 1
			? FString::Printf(TEXT("%d×%d  ·  %d / %d"), Size.X, Size.Y, FMath::Max(1, Item.StackCount), Data.MaxStack)
			: FString::Printf(TEXT("%d×%d"), Size.X, Size.Y)));
	}
}

// 설명 창 하나를 UI 관리자에게서 받아 내용을 바꾸고 마우스 오른쪽 아래에 둔다.
void UItemTooltipWidget::ShowFor(const UObject* WorldContext, const FItemInstance& Item, const FItemTableRow& Data)
{
	UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(WorldContext);
	UUserWidget* Opened = UI ? UI->OpenUI(EUIType::ItemTooltip) : nullptr;
	UItemTooltipWidget* Tooltip = Cast<UItemTooltipWidget>(Opened);
	if (!Tooltip)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemTooltip] not shown: ui=%d opened=%s class=%s"), UI ? 1 : 0,
			Opened ? *Opened->GetClass()->GetName() : TEXT("null"),
			UI && UI->GetUIClass(EUIType::ItemTooltip) ? *UI->GetUIClass(EUIType::ItemTooltip)->GetName() : TEXT("not registered"));
		return;
	}
	Tooltip->SetItem(Item, Data);
	// 마우스를 막지 않게(설명 창 위로 마우스가 가도 아래 아이템이 계속 "올려 둠" 상태).
	Tooltip->SetVisibility(ESlateVisibility::HitTestInvisible);
	// 마우스 오른쪽 아래. 화면 가장자리에서는 창이 밖으로 나가지 않게 안쪽으로 당긴다.
	Tooltip->ForceLayoutPrepass();
	const FVector2D Mouse = UWidgetLayoutLibrary::GetMousePositionOnViewport(Tooltip);
	const FVector2D Viewport = UWidgetLayoutLibrary::GetViewportSize(Tooltip) / FMath::Max(0.01f, UWidgetLayoutLibrary::GetViewportScale(Tooltip));
	const FVector2D Desired = Tooltip->GetDesiredSize();
	const FVector2D Wanted = Mouse + FVector2D(18.0f, 18.0f);
	const FVector2D Clamped(
		FMath::Clamp(Wanted.X, 0.0f, FMath::Max(0.0f, Viewport.X - Desired.X)),
		FMath::Clamp(Wanted.Y, 0.0f, FMath::Max(0.0f, Viewport.Y - Desired.Y)));
	Tooltip->SetPositionInViewport(Clamped, false);
	UE_LOG(LogTemp, Display, TEXT("[ItemTooltip] %s at (%.0f,%.0f) in_viewport=%d desired=(%.0f,%.0f)"), *Data.DisPlayName.ToString(),
		Clamped.X, Clamped.Y, Tooltip->IsInViewport() ? 1 : 0, Desired.X, Desired.Y);
}

void UItemTooltipWidget::Hide(const UObject* WorldContext)
{
	if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(WorldContext))
		if (UI->GetUI(EUIType::ItemTooltip))
			UI->CloseUI(EUIType::ItemTooltip);
}
