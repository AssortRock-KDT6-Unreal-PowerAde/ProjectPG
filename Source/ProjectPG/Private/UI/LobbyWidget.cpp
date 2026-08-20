// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/LobbyWidget.h"
#include "Components/Button.h"
#include "UI/InventoryGridWidget.h"
#include "UI/InventoryWindow.h"

#include "Core/UIManagerSubSystem.h"
#include <GameMode/CustomPlayerState.h>

void ULobbyWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (CharacterBtn)
	{
		CharacterBtn->OnClicked.RemoveDynamic(this, &ULobbyWidget::OnClickedCharacterButton);
		CharacterBtn->OnClicked.AddDynamic(this, &ULobbyWidget::OnClickedCharacterButton);
	}
	if (GameStartBtn) {
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
	UUIManagerSubSystem* subsystem = UUIManagerSubSystem::Get(GetWorld());
	if (!IsValid(subsystem)) return;

	UUserWidget * widget = subsystem->OpenUI(EUIType::Character);
	UInventoryWindow* window = Cast<UInventoryWindow>(widget);
	if (widget)
	{	
		UUserWidget* mainInven = subsystem->OpenUI(EUIType::Inventory);
		if (mainInven)
		{
			UInventoryGridWidget* InvenWidget = Cast<UInventoryGridWidget>(mainInven);
			if (nullptr == InvenWidget ) return;

		}

		if (window) {
			APlayerController* PC = GetOwningPlayer();
			if (PC)
			{
				// 2. 컨트롤러에서 템플릿 Cast 방식(UE5 권장)으로 PlayerState를 가져옵니다.
				ACustomPlayerState* MyPlayerState = PC->GetPlayerState<ACustomPlayerState>();
				if (nullptr == MyPlayerState) return;

				window->SetChildMainInvenOverlay(mainInven);
				window->UpdateState();
			}
		}
	}
}

void ULobbyWidget::OnClickedGameStartButton()
{
}

void ULobbyWidget::OnClickedOptionButton()
{
}

void ULobbyWidget::OnClickedExitButton()
{
}
