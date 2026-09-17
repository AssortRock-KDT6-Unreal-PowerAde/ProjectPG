// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Controller/LobbyUIFlowController.h"
#include "Server/WebSocketSubSystem.h"
#include "Server/AuthSubSystem.h"         // 인증 전담 서브시스템 추가
#include "Core/UIManagerSubSystem.h"
#include "Common/GameData.h"
#include <Kismet/GameplayStatics.h>
#include <Server/InventorySubSystem.h>

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
	// 아이디 생성 요청은 AuthSubSystem을 통해 처리
	UAuthSubSystem* AuthSub = GetGameInstance()->GetSubsystem<UAuthSubSystem>();
	if (nullptr == AuthSub) return;

	AuthSub->OnCreateIDStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);
	AuthSub->OnCreateIDStatusChanged.AddDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);

	AuthSub->RequestCreateID(UserID);
}

void ULobbyUIFlowController::CancleIDCreateWindow()
{
	UAuthSubSystem* AuthSub = GetGameInstance()->GetSubsystem<UAuthSubSystem>();
	if (nullptr == AuthSub) return;
	AuthSub->OnCreateIDStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);
}

void ULobbyUIFlowController::SuccedCreateIDpopup() // 아이디 생성 후 다음 동작
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;
	UIsubSystem->CloseUI(EUIType::CreateUser);
	UIsubSystem->OpenUI(EUIType::Login);
	UIsubSystem->OnPopupClosed.RemoveDynamic(this, &ULobbyUIFlowController::SuccedCreateIDpopup);
}

void ULobbyUIFlowController::RequestLogin(FString UserID)
{
	// 로그인 요청은 AuthSubSystem을 통해 처리
	UAuthSubSystem* AuthSub = GetGameInstance()->GetSubsystem<UAuthSubSystem>();
	if (nullptr == AuthSub) return;

	AuthSub->OnLoginStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::SucceedLogin);
	AuthSub->OnLoginStatusChanged.AddDynamic(this, &ULobbyUIFlowController::SucceedLogin);

	AuthSub->RequestLogin(UserID);
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
			UIsubSystem->OnPopupClosed.AddDynamic(this, &ULobbyUIFlowController::SuccedCreateIDpopup);
			if (UIsubSystem->OnMessagePopupEvent.IsBound())
			{
				UIsubSystem->OpenMessageBox(TEXT("아이디 생성 성공"), 0);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("아이디 생성 실패: %s"), *Message);
		if (IsValid(UIsubSystem))
		{
			UIsubSystem->OpenMessageBox(FString::Printf(TEXT("아이디 생성 실패 : %s"), *Message), 0);
		}
	}

	UAuthSubSystem* AuthSub = GetGameInstance()->GetSubsystem<UAuthSubSystem>();
	if (nullptr == AuthSub) return;
	AuthSub->OnCreateIDStatusChanged.RemoveDynamic(this, &ULobbyUIFlowController::HandleCreateIDStatus);
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
			UIsubSystem->OpenMessageBox(FString::Printf(TEXT("로그인 실패 : %s"), *Messsage), 0);
		}
	}
}

void ULobbyUIFlowController::DataLoadPopup()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()); // 인벤토리 전담 서브시스템 호출

	if (false == IsValid(UIsubSystem)) return;
	if (false == IsValid(InvSub)) return;

	UIsubSystem->CloseAllUI();
	UIsubSystem->OpenMessageBox(TEXT("데이터 로딩중..."), 0);
	UIsubSystem->OnPopupClosed.RemoveDynamic(this, &ULobbyUIFlowController::ShowLobby);
	UIsubSystem->OnPopupClosed.AddDynamic(this, &ULobbyUIFlowController::ShowLobby);

	// 서버로 인벤토리(데이터) 요청
	InvSub->RequestGetInventory();
}

void ULobbyUIFlowController::ShowLobby()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;

	UIsubSystem->OpenUI(EUIType::Lobby);
}