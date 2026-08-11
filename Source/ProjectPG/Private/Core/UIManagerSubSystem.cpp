// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/UIManagerSubSystem.h"
#include "Blueprint/UserWidget.h"

void UUIManagerSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); // 7번째 줄
}

void UUIManagerSubSystem::Deinitialize()
{
	Super::Deinitialize();
}

UUserWidget* UUIManagerSubSystem::ToggleUI(EUIType UIType)
{
	if (UUserWidget** FoundWidget = ActiveWidgets.Find(UIType))
	{
		if (*FoundWidget && (*FoundWidget)->IsInViewport())
		{
			CloseUI(UIType);
			return nullptr;
		}
	}

	return OpenUI(UIType);
}

 UUserWidget* UUIManagerSubSystem::OpenUI(EUIType UIType)
{

	if (UIType == EUIType::None) return nullptr;

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return nullptr;

	// 1. 이미 스폰된 위젯이 있는지 확인
	UUserWidget** FoundWidget = ActiveWidgets.Find(UIType);
	UUserWidget* TargetWidget = FoundWidget ? *FoundWidget : nullptr;

	// 2. 스폰된 위젯이 없다면 UIClassMap에서 블루프린트 클래스를 찾아 스폰
	if (!TargetWidget)
	{
		TSubclassOf<UUserWidget>* TargetClass = UIClassMap.Find(UIType);
		if (TargetClass && *TargetClass)
		{
			TargetWidget = CreateWidget<UUserWidget>(PC, *TargetClass);
			if (TargetWidget)
			{
				ActiveWidgets.Add(UIType, TargetWidget);
				UE_LOG(LogTemp, Warning, TEXT("Add ActiveWidgets  "));

			}
		}
	}

	// 3. Viewport에 출력
	if (TargetWidget && !TargetWidget->IsInViewport())
	{
		TargetWidget->AddToViewport();
		UE_LOG(LogTemp, Warning, TEXT("Inventory Open"));
		UpdateInputMode();
	}

	return TargetWidget;
}

void UUIManagerSubSystem::CloseUI(EUIType UIType)
{
	if (UUserWidget** FoundWidget = ActiveWidgets.Find(UIType))
	{
		if (*FoundWidget && (*FoundWidget)->IsInViewport())
		{
			(*FoundWidget)->RemoveFromParent();
			UpdateInputMode();
		}
	}
}

UUserWidget* UUIManagerSubSystem::GetUI(EUIType UIType) const
{
	if (false == ActiveWidgets.Contains(UIType)  ) return nullptr;

	return ActiveWidgets[UIType];
}

void UUIManagerSubSystem::CloseAllUI()
{
	for (auto& Pair : ActiveWidgets)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			Pair.Value->RemoveFromParent();
		}
	}
	UpdateInputMode();
}

void UUIManagerSubSystem::RegisterUIClass(EUIType UIType, TSubclassOf<UUserWidget> WidgetClass)
{
	if (UIType != EUIType::None && WidgetClass)
	{
		UIClassMap.Add(UIType, WidgetClass);
	}

}

void UUIManagerSubSystem::UpdateInputMode()
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	// 현재 Viewport에 떠 있는 Managed UI가 하나라도 있는지 체크
	bool bHasActiveUI = false;
	for (const auto& Pair : ActiveWidgets)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			bHasActiveUI = true;
			break;
		}
	}

	if (bHasActiveUI)
	{
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
	else
	{
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}
