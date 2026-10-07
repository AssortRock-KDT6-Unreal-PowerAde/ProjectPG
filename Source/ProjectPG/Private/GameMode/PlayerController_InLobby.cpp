// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/PlayerController_InLobby.h"

#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>
#include "Server/InventorySubSystem.h"
#include "Components/InventoryComponent.h"
#include "GameFramework/PlayerState.h"

void APlayerController_InLobby::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		if (UInventorySubSystem* Inventory = UInventorySubSystem::Get(GetWorld()))
		{
			Inventory->ClearTravelInventory();
			Inventory->SetUseWebSocket(true);
			Inventory->SetForceLocalMoves(false);
		}
	}

}

void APlayerController_InLobby::PreClientTravel(const FString& PendingURL, ETravelType TravelType, bool bIsSeamlessTravel)
{
	if (IsLocalController())
	{
		if (UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld()))
		{
			const UInventoryComponent* Inventory = PlayerState ? PlayerState->FindComponentByClass<UInventoryComponent>() : nullptr;
			if (!Subsystem->CaptureTravelInventory(Inventory))
			{
				UE_LOG(LogTemp, Warning, TEXT("[InventoryTravel] Lobby inventory is not initialized; InGame will use the initial WebSocket data."));
			}
		}
	}
	Super::PreClientTravel(PendingURL, TravelType, bIsSeamlessTravel);
}

void APlayerController_InLobby::ToggleInventory()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubSystem* UIMgr = GI->GetSubsystem<UUIManagerSubSystem>())
		{
			// PlayerController는 InventoryWidget의 존재를 몰라도 됨!
			// 열거형(EUIType)만 넘겨서 UIManager에게 처리를 위임함.
			UIMgr->ToggleUI(EUIType::Inventory);
		}
	}
}

void APlayerController_InLobby::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &APlayerController_InLobby::OnRotateKey);
	}
}

void APlayerController_InLobby::OnRotateKey()
{
	if (UItemDragDropOperation* DragOp = Cast<UItemDragDropOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent()))
	{
		DragOp->RotateItem();
	}
}
