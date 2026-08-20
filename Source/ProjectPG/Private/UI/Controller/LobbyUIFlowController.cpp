// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Controller/LobbyUIFlowController.h"
#include "Server/WebSocketSubSystem.h"
#include "Core/UIManagerSubSystem.h"
#include "Common/GameData.h"
#include <Kismet/GameplayStatics.h>

ULobbyUIFlowController* ULobbyUIFlowController::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;

	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;


	return inst->GetSubsystem<ULobbyUIFlowController>();
}

void ULobbyUIFlowController::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	
}

void ULobbyUIFlowController::Deinitialize()
{
	Super::Deinitialize();
}

void ULobbyUIFlowController::BeginSetting()
{
	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return;

	if (!subSystem->GetCurrentUserID().IsEmpty())
		RequestLogin(subSystem->GetCurrentUserID());
	else
	{
		UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
		if (nullptr == UIsubSystem) return;
		UIsubSystem->OpenUI(EUIType::LoginWindow);
		UIsubSystem->OpenUI(EUIType::Login);
	}


}

void ULobbyUIFlowController::RequestIDCreate(FString UserID)
{
	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return;

	subSystem->OnCreateIDStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);
	subSystem->OnCreateIDStatusChanged.AddDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);

	subSystem->RequestCreateID(UserID);
}
void ULobbyUIFlowController::CancleIDCreateWindow()
{
	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return;
	subSystem->OnCreateIDStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);

}
void ULobbyUIFlowController::SuccedCreateIDpopup()//아이디 생성후 다음동작
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;
	UIsubSystem->CloseUI(EUIType::CreateUser);
	UIsubSystem->OpenUI(EUIType::Login);
	UIsubSystem->OnPopupClosed.RemoveDynamic(this, &ULobbyUIFlowController::SuccedCreateIDpopup);
}
void ULobbyUIFlowController::RequestLogin(FString UserID)
{
	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return;
	subSystem->OnLoginStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::SucceedLogin);
	subSystem->OnLoginStatusChanged.AddDynamic(this, &ULobbyUIFlowController::SucceedLogin);
	subSystem->RequestLogin(UserID);
}
void ULobbyUIFlowController::HandleCreateIDStatus(bool bSuccess, const FString& UserID, const FString& Message)
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("아이디 생성 성공: %s"), *UserID);
		if (IsValid(UIsubSystem))
		{
			UIsubSystem->OnPopupClosed.RemoveDynamic(this, &ULobbyUIFlowController::SuccedCreateIDpopup);
			UIsubSystem->OnPopupClosed.AddDynamic(this,&ULobbyUIFlowController::SuccedCreateIDpopup);
			UIsubSystem->OpenUI(EUIType::MessagePopup);
			if (UIsubSystem->OnMessagePopupEvent.IsBound())
			{
				UIsubSystem->OnMessagePopupEvent.Broadcast(TEXT("아이디 생성 성공"), 0);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("아이디 생성 실패: %s"), *Message);
		if (IsValid(UIsubSystem))
		{
			UIsubSystem->OpenUI(EUIType::MessagePopup);
			UIsubSystem->OnMessagePopupEvent.Broadcast(FString::Printf(TEXT("아이디 생성 실패 : %s"), *Message), 0);
		}
	}

	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return;
	subSystem->OnCreateIDStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);
}

void ULobbyUIFlowController::SucceedLogin(bool bIsLogedIn, bool bDataLoaded, const FString& Messsage)
{
	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	UE_LOG(LogTemp, Warning, TEXT("로그인 상태 %s"), *Messsage);
	if (nullptr == subSystem) return;
	if (subSystem->GetCurrentUserID().IsEmpty())
	{
		UIsubSystem->OpenUI(EUIType::LoginWindow);
		UIsubSystem->OpenUI(EUIType::Login);
	}
	else 
	{
		if (false == IsValid(UIsubSystem)) return;
		if (bIsLogedIn && bDataLoaded)
		{
			DataLoadPopup();
		}
		else
		{
			UIsubSystem->ToggleUI(EUIType::MessagePopup);
			UIsubSystem->OnMessagePopupEvent.Broadcast(FString::Printf(TEXT("아이디 생성 실패 : %s"), *Messsage), 0);

		}
	}
}

void ULobbyUIFlowController::DataLoadPopup()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	UWebSocketSubSystem* subSystem = UWebSocketSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;
	if (false == IsValid(subSystem)) return;

	UIsubSystem->CloseAllUI();
	UIsubSystem->OpenUI(EUIType::MessagePopup);
	UIsubSystem->OnMessagePopupEvent.Broadcast(TEXT("데이터 로딩중..."), 0);
	UIsubSystem->OnPopupClosed.AddDynamic(this, &ULobbyUIFlowController::ShowLobby);
	subSystem->RequestGetInventory();
}

void ULobbyUIFlowController::ShowLobby()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;

	UIsubSystem->OpenUI(EUIType::Lobby);

}

