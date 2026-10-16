#include "UI/CharacterVitalsWidget.h"
#include "Characters/CustomCharacter.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "Blueprint/WidgetTree.h"
#include "Components/VerticalBox.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/Image.h"
#include "Equipments/FirearmEquipment.h"
#include "GameplayAbilities/WeaponAttributeSet.h"

void UCharacterVitalsWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree->RootWidget)
	{
		auto* Box = WidgetTree->ConstructWidget<UVerticalBox>();
		auto* Row = WidgetTree->ConstructWidget<UHorizontalBox>();
		auto* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>();
		WidgetTree->RootWidget = Canvas;
		auto* VitalsSlot = Canvas->AddChildToCanvas(Row);
		VitalsSlot->SetAnchors(FAnchors(0.f, 1.f));
		VitalsSlot->SetAlignment(FVector2D(0.f, 1.f));
		VitalsSlot->SetPosition(FVector2D(24.f, -24.f));
		VitalsSlot->SetSize(FVector2D(480.f, 100.f));
		// 폰트 대신 도형을 사용해 조준 중심과 선 두께를 일정하게 유지합니다.
		auto AddCrosshairRect = [&](FVector2D Position, FVector2D Size, FLinearColor Color)
		{
			auto* Image = WidgetTree->ConstructWidget<UImage>();
			FSlateBrush Brush;
			Brush.DrawAs = ESlateBrushDrawType::Image;
			Brush.TintColor = FSlateColor(Color);
			Image->SetBrush(Brush);
			auto* ImageSlot = Canvas->AddChildToCanvas(Image);
			ImageSlot->SetAnchors(FAnchors(0.5f, 0.5f));
			ImageSlot->SetAlignment(FVector2D(0.5f, 0.5f));
			ImageSlot->SetPosition(Position);
			ImageSlot->SetSize(Size);
		};
		auto AddCrosshairMark = [&](FVector2D Position, FVector2D Size)
		{
			AddCrosshairRect(Position, Size + FVector2D(2.f, 2.f), FLinearColor(0.f, 0.f, 0.f, 0.65f));
			AddCrosshairRect(Position, Size, FLinearColor(0.95f, 0.95f, 0.95f));
		};
		AddCrosshairMark(FVector2D::ZeroVector, FVector2D(2.f, 2.f));
		AddCrosshairMark(FVector2D(-8.f, 0.f), FVector2D(6.f, 1.f));
		AddCrosshairMark(FVector2D(8.f, 0.f), FVector2D(6.f, 1.f));
		AddCrosshairMark(FVector2D(0.f, -8.f), FVector2D(1.f, 6.f));
		AddCrosshairMark(FVector2D(0.f, 8.f), FVector2D(1.f, 6.f));
		auto* VitalsSize = WidgetTree->ConstructWidget<USizeBox>();
		VitalsSize->SetWidthOverride(260.f);
		VitalsSize->SetContent(Box);
		Row->AddChildToHorizontalBox(VitalsSize);
		AmmoText = WidgetTree->ConstructWidget<UTextBlock>();
		auto* AmmoSlot = Row->AddChildToHorizontalBox(AmmoText);
		AmmoSlot->SetPadding(FMargin(24.f, 0.f, 0.f, 0.f));
		AmmoSlot->SetVerticalAlignment(VAlign_Bottom);
		HealthText = WidgetTree->ConstructWidget<UTextBlock>();
		HealthBar = WidgetTree->ConstructWidget<UProgressBar>();
		StaminaText = WidgetTree->ConstructWidget<UTextBlock>();
		StaminaBar = WidgetTree->ConstructWidget<UProgressBar>();
		HealthBar->SetFillColorAndOpacity(FLinearColor(0.85f, 0.12f, 0.12f));
		StaminaBar->SetFillColorAndOpacity(FLinearColor(0.15f, 0.8f, 0.25f));
		Box->AddChildToVerticalBox(HealthText);
		Box->AddChildToVerticalBox(HealthBar);
		Box->AddChildToVerticalBox(StaminaText);
		Box->AddChildToVerticalBox(StaminaBar);
	}
}

void UCharacterVitalsWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (Attributes)
	{
		Attributes->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::UpdateHealth);
		Attributes->OnStaminaChanged.AddUniqueDynamic(this, &ThisClass::UpdateStamina);
	}
	UpdateHealth(Attributes ? Attributes->GetHealth() : 0.f, Attributes ? Attributes->GetMaxHealth() : 0.f);
	UpdateStamina(Attributes ? Attributes->GetStamina() : 0.f, Attributes ? Attributes->GetMaxStamina() : 0.f);
	RefreshAmmoSubscription();
}

void UCharacterVitalsWidget::Unsubscribe()
{
	if (ObservedCharacter.IsValid()) ObservedCharacter->OnAmmoChanged.RemoveDynamic(this, &ThisClass::RefreshAmmoSubscription);
	if (WeaponAttributes) WeaponAttributes->OnLoadedAmmoChanged.RemoveDynamic(this, &ThisClass::UpdateAmmo);
	WeaponAttributes = nullptr;
	ObservedCharacter.Reset();
	if (Attributes)
	{
		Attributes->OnHealthChanged.RemoveDynamic(this, &ThisClass::UpdateHealth);
		Attributes->OnStaminaChanged.RemoveDynamic(this, &ThisClass::UpdateStamina);
	}
	Attributes = nullptr;
}

void UCharacterVitalsWidget::ObserveCharacter(ACustomCharacter* Character)
{
	Unsubscribe();
	ObservedCharacter = Character;
	if (IsValid(Character)) Character->OnAmmoChanged.AddUniqueDynamic(this, &ThisClass::RefreshAmmoSubscription);
	Attributes = IsValid(Character) ? Character->GetCharacterAttributeSet() : nullptr;
	if (Attributes)
	{
		Attributes->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::UpdateHealth);
		Attributes->OnStaminaChanged.AddUniqueDynamic(this, &ThisClass::UpdateStamina);
	}
	if (HealthBar && StaminaBar)
	{
		UpdateHealth(Attributes ? Attributes->GetHealth() : 0.f, Attributes ? Attributes->GetMaxHealth() : 0.f);
		UpdateStamina(Attributes ? Attributes->GetStamina() : 0.f, Attributes ? Attributes->GetMaxStamina() : 0.f);
	}
	RefreshAmmoSubscription();
}

void UCharacterVitalsWidget::RefreshAmmoSubscription()
{
	if (WeaponAttributes) WeaponAttributes->OnLoadedAmmoChanged.RemoveDynamic(this, &ThisClass::UpdateAmmo);
	const auto* Weapon = ObservedCharacter.IsValid() ? ObservedCharacter->GetActiveFirearm() : nullptr;
	WeaponAttributes = IsValid(Weapon) ? Weapon->GetWeaponAttributeSet() : nullptr;
	if (WeaponAttributes) WeaponAttributes->OnLoadedAmmoChanged.AddUniqueDynamic(this, &ThisClass::UpdateAmmo);
	UpdateAmmo();
}

void UCharacterVitalsWidget::UpdateAmmo()
{
	if (!AmmoText) return;
	const auto* Character = ObservedCharacter.Get();
	const auto* Weapon = Character ? Character->GetActiveFirearm() : nullptr;
	AmmoText->SetText(FText::FromString(IsValid(Weapon)
		? FString::Printf(TEXT("AMMO  %d / %d"), Weapon->GetLoadedAmmo(), Character->GetReserveAmmo(Weapon->GetWeaponType()))
		: TEXT("AMMO  -- / --")));
}

void UCharacterVitalsWidget::NativeDestruct()
{
	Unsubscribe();
	Super::NativeDestruct();
}

void UCharacterVitalsWidget::UpdateHealth(float Current, float Maximum)
{
	HealthBar->SetPercent(Maximum > 0.f ? FMath::Clamp(Current / Maximum, 0.f, 1.f) : 0.f);
	HealthText->SetText(FText::FromString(Maximum > 0.f ? FString::Printf(TEXT("HP  %.0f / %.0f"), Current, Maximum) : TEXT("HP  -- / --")));
}

void UCharacterVitalsWidget::UpdateStamina(float Current, float Maximum)
{
	StaminaBar->SetPercent(Maximum > 0.f ? FMath::Clamp(Current / Maximum, 0.f, 1.f) : 0.f);
	StaminaText->SetText(FText::FromString(Maximum > 0.f ? FString::Printf(TEXT("ST  %.0f / %.0f"), Current, Maximum) : TEXT("ST  -- / --")));
}
