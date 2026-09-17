// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/CreateIDPopup.h"
#include "Components/Button.h"
#include "Components/Overlay.h"
#include "Components/EditableText.h"
#include "Server/WebSocketSubSystem.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/Controller/LobbyUIFlowController.h"
void UCreateIDPopup::NativeConstruct()
{
	Super::NativeConstruct();
	if (CreateIDBtn) {
		CreateIDBtn->OnClicked.RemoveDynamic(this, &UCreateIDPopup::OnClickedCreateID);
		CreateIDBtn->OnClicked.AddDynamic(this, &UCreateIDPopup::OnClickedCreateID);
	}
	if (CanacleBtn) {
		CanacleBtn->OnClicked.RemoveDynamic(this, &UCreateIDPopup::OnClickedCancle);
		CanacleBtn->OnClicked.AddDynamic(this, &UCreateIDPopup::OnClickedCancle);
	}
}

void UCreateIDPopup::OnClickedCreateID()
{
	FString UserID = CreateEditText->GetText().ToString().TrimStartAndEnd();
	if (UserID.IsEmpty())
	{
		
		UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
		if (IsValid(UIsubSystem)) 
		{
			UIsubSystem->OpenUI(EUIType::MessagePopup);
			UIsubSystem->OnMessagePopupEvent.Broadcast(TEXT("UserID를 입력해주세요"), 0);
		}
	}
	ULobbyUIFlowController* flowController = ULobbyUIFlowController::Get(GetWorld());
	flowController->RequestIDCreate(UserID);
}

void UCreateIDPopup::OnClickedCancle()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (IsValid(UIsubSystem))
	{
		UIsubSystem->CloseUI(EUIType::CreateUser);
		UIsubSystem->OpenUI(EUIType::Login);

	}
	ULobbyUIFlowController* flowController = ULobbyUIFlowController::Get(GetWorld());
	flowController->CancleIDCreateWindow();
	
}

