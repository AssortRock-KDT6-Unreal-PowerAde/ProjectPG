// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/PlayerController_InLobby.h"

#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>

void APlayerController_InLobby::BeginPlay()
{
	Super::BeginPlay();

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
