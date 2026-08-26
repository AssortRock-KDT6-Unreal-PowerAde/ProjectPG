// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LobbyWidget.h"
#include "Components/Button.h"
#include "UI/InventoryWindow.h"
#include "UI/InventoryGridWidget.h"
#include "Core/UIManagerSubSystem.h"
#include "Components/InventoryComponent.h"
#include "Server/WebSocketSubSystem.h"
#include "GameMode/CustomPlayerState.h"

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();

	if (CharacterBtn)
	{
		CharacterBtn->OnClicked.RemoveDynamic(this, &ULobbyWidget::OnClickedCharacterButton);
		CharacterBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnClickedCharacterButton);
	}
	if (GameStartBtn)
	{
		GameStartBtn->OnClicked.RemoveDynamic(this, &ULobbyWidget::OnClickedGameStartButton);
		GameStartBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnClickedGameStartButton);
	}
	if (OptionBtn)
	{
		OptionBtn->OnClicked.RemoveDynamic(this, &ULobbyWidget::OnClickedOptionButton);
		OptionBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnClickedOptionButton);
	}
	if (ExitBtn)
	{
		ExitBtn->OnClicked.RemoveDynamic(this, &ULobbyWidget::OnClickedExitButton);
		ExitBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnClickedExitButton);
	}
}

void ULobbyWidget::OnClickedCharacterButton()
{
	UUIManagerSubSystem* UISubsystem = UUIManagerSubSystem::Get(GetWorld());
	if (!IsValid(UISubsystem)) return;

	UUserWidget* CharacterWidget = UISubsystem->OpenUI(EUIType::Character);
	UInventoryWindow* Window = Cast<UInventoryWindow>(CharacterWidget);

	if (Window)
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC)
		{
			if (ACustomPlayerState* MyPS = PC->GetPlayerState<ACustomPlayerState>())
			{
				UInventoryComponent* InvenComp = MyPS->GetComponentByClass<UInventoryComponent>();
				UEquipComponent* EquipComp = MyPS->GetComponentByClass<UEquipComponent>();

				// 1. 컴포넌트 초기화
				Window->InitWidget(InvenComp, EquipComp);

				// 2. Main / Pocket 인벤토리 UI 생성 호출
				TSubclassOf<UUserWidget> InvenClass = UISubsystem->GetUIClass(EUIType::Inventory);
				if (InvenClass)
				{
					Window->SetupMainInventoryWidget(InvenClass);
					Window->SetupPocketInventoryWidget(InvenClass); // 포켓 UI도 필요한 경우 연달아 호출 가능
					Window->SetupBackPackInventoryWidget(InvenClass);

				}
			}
		}

		// 3. 서버에 인벤토리 데이터 요청 및 상태 Update
		Window->UpdateState();
	}
}

void ULobbyWidget::OnClickedGameStartButton() {}
void ULobbyWidget::OnClickedOptionButton() {}
void ULobbyWidget::OnClickedExitButton() {}