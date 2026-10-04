// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Controller/LobbyUIFlowController.h"
#include "Core/UIManagerSubSystem.h"
#include "Server/InventorySubSystem.h"
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

// 로비에 들어왔을 때: 시작 짐을 넣고(처음 한 번만) 로비 메뉴를 띄운다.
void ULobbyUIFlowController::BeginSetting()
{
	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvSub->LoadStarterInventory(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
	}
	ShowLobby();
}

void ULobbyUIFlowController::ShowLobby()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;

	UIsubSystem->CloseAllUI();
	UIsubSystem->OpenUI(EUIType::Lobby);
}
