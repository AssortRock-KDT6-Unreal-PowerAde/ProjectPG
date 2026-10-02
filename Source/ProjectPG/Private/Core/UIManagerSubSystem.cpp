// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/UIManagerSubSystem.h"
#include "Blueprint/UserWidget.h"
#include <Kismet/GameplayStatics.h>

void UUIManagerSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection); // 7번째 줄
}

void UUIManagerSubSystem::Deinitialize()
{
	Super::Deinitialize();
}
UUIManagerSubSystem* UUIManagerSubSystem::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;

	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;


	return inst->GetSubsystem<UUIManagerSubSystem>();
}
TSubclassOf<UUserWidget> UUIManagerSubSystem::GetUIClass(EUIType UIType) const
{
	if (const TSubclassOf<UUserWidget>* FoundClass = UIClassMap.Find(UIType))
	{
		return *FoundClass;
	}
	return nullptr;
}
UUserWidget* UUIManagerSubSystem::OpenDynamicUI(
	EUIType UIType,
	FGuid guid)
{
	if (UIType == EUIType::None || !guid.IsValid())
	{
		return nullptr;
	}

	// 이미 열려있는 가방이면 기존 위젯 반환
	if (UUserWidget** Found = DynamicActiveWidgets.Find(guid))
	{
		if (*Found && (*Found)->IsInViewport())
		{
			return *Found;
		}
	}

	APlayerController* PC =
		GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	if (!PC)
	{
		return nullptr;
	}

	TSubclassOf<UUserWidget>* TargetClass =
		UIClassMap.Find(UIType);

	if (TargetClass && *TargetClass)
	{
		UUserWidget* NewWidget =
			CreateWidget<UUserWidget>(PC, *TargetClass);

		if (NewWidget)
		{
			DynamicActiveWidgets.Add(guid, NewWidget);

			// =====================================================
			// Dynamic UI는 InventoryWindow보다 위
			// =====================================================
			int32 ZOrder = 200;

			NewWidget->AddToViewport(ZOrder);

			UpdateInputMode();

			return NewWidget;
		}
	}

	return nullptr;
}
void UUIManagerSubSystem::CloseDynamicUI(FGuid guid)
{
	if (!guid.IsValid()) return;

	// 1. 해당 컨텍스트로 관리되던 위젯이 있는지 검색
	if (UUserWidget** FoundWidget = DynamicActiveWidgets.Find(guid))
	{
		if (*FoundWidget)
		{
			if ((*FoundWidget)->IsInViewport())
			{
				(*FoundWidget)->RemoveFromParent();
			}
		}

		// 2. 관리 맵에서 제거 (필요에 따라 인스턴스를 날리거나 유지할 수 있습니다)
		DynamicActiveWidgets.Remove(guid);
	}

	// 3. 입력 모드 갱신 (다른 창들이 여전히 떠 있는지 확인하기 위함)
	UpdateInputMode();
}
void UUIManagerSubSystem::OpenMessageBox(FString message, int boxType)
{
	OpenUI(EUIType::MessagePopup);
	OnMessagePopupEvent.Broadcast(message, boxType);

}
void UUIManagerSubSystem::CloseItemContext()
{
	if (UUserWidget** FoundWidget =
		ActiveWidgets.Find(EUIType::ItemContext))
	{
		if (*FoundWidget)
		{
			UUserWidget* ContextWidget = *FoundWidget;

			ContextWidget->SetVisibility(
				ESlateVisibility::Collapsed
			);

			if (ContextWidget->IsInViewport())
			{
				ContextWidget->RemoveFromParent();
			}
		}
	}

	UpdateInputMode();
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
	if (UIType == EUIType::None)
	{
		return nullptr;
	}

	APlayerController* PC =
		GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;

	if (!PC)
	{
		return nullptr;
	}

	UUserWidget** FoundWidget = ActiveWidgets.Find(UIType);
	UUserWidget* TargetWidget = FoundWidget ? *FoundWidget : nullptr;

	if (!TargetWidget)
	{
		TSubclassOf<UUserWidget>* TargetClass = UIClassMap.Find(UIType);

		if (TargetClass && *TargetClass)
		{
			TargetWidget = CreateWidget<UUserWidget>(PC, *TargetClass);

			if (TargetWidget)
			{
				ActiveWidgets.Add(UIType, TargetWidget);
			}
		}
	}

	if (!TargetWidget)
	{
		return nullptr;
	}

	if (!TargetWidget->IsInViewport())
	{
		int32 ZOrder = 100;

		switch (UIType)
		{
		case EUIType::Inventory:
			ZOrder = 100;
			break;

		case EUIType::ItemContext:
			ZOrder = 300;
			break;

		case EUIType::MessagePopup:
			ZOrder = 1000;
			break;

		default:
			ZOrder = 100;
			break;
		}

		TargetWidget->AddToViewport(ZOrder);
	}

	UpdateInputMode();

	return TargetWidget;
}

void UUIManagerSubSystem::CloseUI(EUIType UIType)
{
	if (UIType == EUIType::Inventory)
	{
		CloseItemContext();
	}

	if (UUserWidget** FoundWidget =
		ActiveWidgets.Find(UIType))
	{
		if (*FoundWidget)
		{
			(*FoundWidget)->SetVisibility(
				ESlateVisibility::Collapsed
			);

			if ((*FoundWidget)->IsInViewport())
			{
				(*FoundWidget)->RemoveFromParent();
			}
		}
	}

	UpdateInputMode();
}
UUserWidget* UUIManagerSubSystem::GetUI(EUIType UIType) const
{
	if (false == ActiveWidgets.Contains(UIType)  ) return nullptr;

	return ActiveWidgets[UIType];
}

void UUIManagerSubSystem::CloseAllUI()
{
	// 고정 UI
	for (auto& Pair : ActiveWidgets)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			Pair.Value->RemoveFromParent();
		}
	}

	// 동적 UI
	for (auto& Pair : DynamicActiveWidgets)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			Pair.Value->RemoveFromParent();
		}
	}

	UpdateInputMode();
}

UUserWidget* UUIManagerSubSystem::GetDynamicUI(FGuid UIType) const
{
	if (false == DynamicActiveWidgets.Contains(UIType)) return nullptr;

	return DynamicActiveWidgets[UIType];
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
	APlayerController* PC =
		GetWorld()
		? GetWorld()->GetFirstPlayerController()
		: nullptr;

	if (!PC)
	{
		return;
	}

	bool bHasActiveUI = false;
	bool bHasMessageBox = false;

	// =====================================================
	// 1. 고정 UI 검사
	// =====================================================
	for (const auto& Pair : ActiveWidgets)
	{
		UUserWidget* Widget = Pair.Value;

		if (!Widget)
		{
			continue;
		}

		if (!Widget->IsInViewport())
		{
			continue;
		}

		// 실제로 Visible인 UI만 활성 UI로 취급
		if (Widget->GetVisibility() !=
			ESlateVisibility::Collapsed &&
			Widget->GetVisibility() !=
			ESlateVisibility::Hidden)
		{
			bHasActiveUI = true;
		}

		if (Pair.Key == EUIType::MessagePopup &&
			Widget->GetVisibility() ==
			ESlateVisibility::Visible)
		{
			bHasMessageBox = true;
		}
	}

	// =====================================================
	// 2. 동적 UI 검사
	// =====================================================
	for (const auto& Pair : DynamicActiveWidgets)
	{
		UUserWidget* Widget = Pair.Value;

		if (!Widget)
		{
			continue;
		}

		if (!Widget->IsInViewport())
		{
			continue;
		}

		if (Widget->GetVisibility() !=
			ESlateVisibility::Collapsed &&
			Widget->GetVisibility() !=
			ESlateVisibility::Hidden)
		{
			bHasActiveUI = true;
		}
	}

	// =====================================================
	// 3. MessageBox가 있는 경우
	// =====================================================
	if (bHasMessageBox)
	{
		PC->SetShowMouseCursor(true);

		FInputModeUIOnly InputMode;

		if (UUserWidget** MsgWidget =
			ActiveWidgets.Find(EUIType::MessagePopup))
		{
			if (MsgWidget && *MsgWidget)
			{
				InputMode.SetWidgetToFocus(
					(*MsgWidget)->TakeWidget()
				);
			}
		}

		PC->SetInputMode(InputMode);

		return;
	}

	// =====================================================
	// 4. 일반 UI가 있는 경우
	// =====================================================
	if (bHasActiveUI)
	{
		PC->SetShowMouseCursor(true);

		FInputModeGameAndUI InputMode;

		InputMode.SetHideCursorDuringCapture(false);

		PC->SetInputMode(InputMode);

		return;
	}

	// =====================================================
	// 5. UI가 하나도 없는 경우
	// =====================================================
	PC->SetShowMouseCursor(false);

	PC->SetInputMode(
		FInputModeGameOnly()
	);
}