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
UUserWidget* UUIManagerSubSystem::OpenDynamicUI(EUIType UIType, FGuid guid)
{
	if (UIType == EUIType::None || !guid.IsValid()) return nullptr;

	// 1. 이미 해당 컨텍스트(예: 특정 가방)에 대한 위젯이 열려있는지 확인
	if (UUserWidget** Found = DynamicActiveWidgets.Find(guid))
	{
		if (*Found && (*Found)->IsInViewport())
		{
			return *Found; // 이미 열려있다면 기존 것 반환
		}
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return nullptr;

	// 2. 매니저 내부에서 클래스 맵을 참조해 직접 생성 (외부에서 GetUIClass 할 필요 없음!)
	TSubclassOf<UUserWidget>* TargetClass = UIClassMap.Find(UIType);
	if (TargetClass && *TargetClass)
	{
		UUserWidget* NewWidget = CreateWidget<UUserWidget>(PC, *TargetClass);
		if (NewWidget)
		{
			DynamicActiveWidgets.Add(guid, NewWidget);
			NewWidget->AddToViewport();
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
		UE_LOG(LogTemp, Warning, TEXT("ClassMap %d"), UIClassMap.Num());

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
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	// 현재 Viewport에 떠 있는 Managed UI가 하나라도 있는지 체크 (고정형 + 동적형 모두 검사)
	bool bHasActiveUI = false;
	bool bHasMessageBox = false; //메시지 박스 현재 활동하는지 확인
	// 1. 고정형 UI 검사
	for (const auto& Pair : ActiveWidgets)
	{
		if (Pair.Value && Pair.Value->IsInViewport())
		{
			bHasActiveUI = true;
			break;
		}
	}

	// 2. [추가] 동적형 UI(가방, 상자 등) 검사
	if (!bHasActiveUI)
	{
		for (const auto& Pair : DynamicActiveWidgets)
		{
			if (Pair.Value && Pair.Value->IsInViewport())
			{
				bHasActiveUI = true;
				break;
			}
		}
	}

	if (bHasActiveUI)
	{
		PC->SetShowMouseCursor(true);

		if (bHasMessageBox)
		{
			FInputModeUIOnly InputMode;

			if (UUserWidget** MsgWidget = ActiveWidgets.Find(EUIType::MessagePopup))
			{
				if (MsgWidget && *MsgWidget)
				{
					InputMode.SetWidgetToFocus((*MsgWidget)->TakeWidget());
				}
			}
			PC->SetInputMode(InputMode);

			// [수정] 메시지 박스가 뜰 때 다른 위젯들은 눈에 그대로 보이되(Hit Test Invisible), 클릭만 투과되도록 설정
			for (auto& Pair : ActiveWidgets)
			{
				if (Pair.Key != EUIType::MessagePopup && Pair.Value)
				{
					Pair.Value->SetVisibility(ESlateVisibility::HitTestInvisible);
				}
			}
			for (auto& Pair : DynamicActiveWidgets)
			{
				if (Pair.Value)
				{
					Pair.Value->SetVisibility(ESlateVisibility::HitTestInvisible);
				}
			}
		}
		else
		{
			// 메시지 박스가 닫히면 다른 위젯들의 원래 상호작용성 복구 (다시 클릭 가능하게)
			for (auto& Pair : ActiveWidgets)
			{
				if (Pair.Value) Pair.Value->SetVisibility(ESlateVisibility::Visible);
			}
			for (auto& Pair : DynamicActiveWidgets)
			{
				if (Pair.Value) Pair.Value->SetVisibility(ESlateVisibility::Visible);
			}

			FInputModeGameAndUI InputMode;
			InputMode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(InputMode);
		}
	}
	else
	{
		// UI가 아예 없을 때 복구
		for (auto& Pair : ActiveWidgets) { if (Pair.Value) Pair.Value->SetVisibility(ESlateVisibility::Visible); }
		for (auto& Pair : DynamicActiveWidgets) { if (Pair.Value) Pair.Value->SetVisibility(ESlateVisibility::Visible); }

		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}