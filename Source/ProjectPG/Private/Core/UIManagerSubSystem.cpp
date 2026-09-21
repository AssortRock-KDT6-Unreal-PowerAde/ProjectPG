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

	// 이미 생성된 위젯이 ActiveWidgets에 존재하는데 뷰포트에 없으면 강제로 보여줍니다.
	if (TargetWidget)
	{
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
			// 뷰포트로 복원할 때 가시성도 확실히 설정
			TargetWidget->SetVisibility(ESlateVisibility::Visible);
		}

		UpdateInputMode();
		return TargetWidget;
	}

	// 없으면 새로 생성
	TSubclassOf<UUserWidget>* TargetClass = UIClassMap.Find(UIType);

	if (TargetClass && *TargetClass)
	{
		TargetWidget = CreateWidget<UUserWidget>(PC, *TargetClass);

			if (TargetWidget)
			{
				ActiveWidgets.Add(UIType, TargetWidget);

				// 생성 직후 뷰포트에 추가 및 가시성 설정
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
				TargetWidget->SetVisibility(ESlateVisibility::Visible);
			}
	}

	if (!TargetWidget)
	{
		return nullptr;
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

		// 메시지 팝업에서는 UIOnly로 전환하면 포커스 불가 위젯에 대한 시도가 발생해
		// 입력이 차단될 수 있으므로 GameAndUI로 설정합니다. (게임 입력도 허용)
		FInputModeGameAndUI InputMode;
		InputMode.SetHideCursorDuringCapture(false);

		// 포커스 시도는 제거하여 Non-Focusable 위젯 경고를 방지
		// 필요시 팝업 위젯이 포커스를 지원하면 이후에 명시적으로 포커스 설정 가능

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