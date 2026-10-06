// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/LobbyWidget.h"
#include "UI/InventoryWindow.h"
#include "UI/InventoryGridWidget.h"
#include "Core/UIManagerSubSystem.h"
#include "Components/Button.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"

#include "GameMode/CustomPlayerState.h"
#include "Server/SessionSubSystem.h"
#include "UI/Controller/LobbyUIFlowController.h"
#include "Kismet/KismetSystemLibrary.h"

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

	// 기획서: 캐릭터 화면은 메인 메뉴를 대신해 뜨고, "뒤로가기" 로 메인 메뉴에 돌아온다 → 메뉴를 닫고 연다.
	UISubsystem->CloseUI(EUIType::Lobby);
	// 카메라를 캐릭터 정면으로 — 장비 칸 사이에 지금 캐릭터가 보이게(파란 그림 대신 실제 맵·캐릭터).
	if (ULobbyUIFlowController* Flow = ULobbyUIFlowController::Get(this))
	{
		Flow->FocusCamera(TEXT("LobbyCamera_Character"));
	}
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

// 게임 시작: 매칭 화면을 띄우고 매칭 담당에게 맡긴다(같은 네트워크의 방에 들어가거나, 없으면 내가 방장 = 리슨 서버).
// (예전: 웹 서버 매칭 요청) 10/4 팀 합의로 리슨 서버.
void ULobbyWidget::OnClickedGameStartButton()
{
	UUIManagerSubSystem* UISubsystem = UUIManagerSubSystem::Get(GetWorld());
	if (!IsValid(UISubsystem)) return;
	UISubsystem->OpenUI(EUIType::Matching);

	if (USessionSubSystem* Session = USessionSubSystem::Get(this))
	{
		Session->StartMatching();
	}
}

void ULobbyWidget::OnClickedOptionButton()
{
	if (UUIManagerSubSystem* UISubsystem = UUIManagerSubSystem::Get(GetWorld()))
	{
		UISubsystem->OpenUI(EUIType::Option);
	}
}

// 종료: 게임을 끈다.
void ULobbyWidget::OnClickedExitButton()
{
	UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
}