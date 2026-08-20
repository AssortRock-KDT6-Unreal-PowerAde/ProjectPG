// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/LoginPopup.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/EditableText.h"
#include "Server/WebSocketSubSystem.h"
#include "Core/UIManagerSubSystem.h"
#include <UI/Controller/LobbyUIFlowController.h>

void ULoginPopup::NativeConstruct()
{
	Super::NativeConstruct();
	if (LoginBtn) {
		LoginBtn->OnClicked.RemoveDynamic(this, &ULoginPopup::OnClickedLogin);
		LoginBtn->OnClicked.AddDynamic(this, &ULoginPopup::OnClickedLogin);
	}
	if (CreateIdViewBtn)
	{
		CreateIdViewBtn->OnClicked.RemoveDynamic(this, &ULoginPopup::OnClickedCreateIDView);
		CreateIdViewBtn->OnClicked.AddDynamic(this, &ULoginPopup::OnClickedCreateIDView);
	}
}

void ULoginPopup::OnClickedLogin()
{
	UE_LOG(LogTemp, Warning, TEXT("로그인 버튼 클릭"));	
	FString UserID = LoginEditText->GetText().ToString().TrimStartAndEnd();
	if (UserID.IsEmpty())
	{
		UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
		if (IsValid(UIsubSystem))
		{
			UIsubSystem->OpenUI(EUIType::MessagePopup);
			UIsubSystem->OnMessagePopupEvent.Broadcast(TEXT("UserID를 입력해주세요"), 0);
			return;
		}
	}
	ULobbyUIFlowController* flowController = ULobbyUIFlowController::Get(GetWorld());
	if (IsValid(flowController))
	{
		flowController->RequestLogin(UserID);
	}
}

void ULoginPopup::OnClickedCreateIDView()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (IsValid(UIsubSystem))
	{
		UIsubSystem->CloseUI(EUIType::Login);
		UIsubSystem->OpenUI(EUIType::CreateUser);

	}

}
