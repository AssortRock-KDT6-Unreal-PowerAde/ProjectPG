// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/CustomPlayerController.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>

void ACustomPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (IsLocalController())
	{
		// 0.01초 뒤에 UI 설정 시작 (GameInstance Init 및 Subsystem 등록 완료 대기)
		FTimerHandle TimerHandle;
		GetWorldTimerManager().SetTimer(TimerHandle, [this]()
			{
				ULobbyUIFlowController* FlowController = ULobbyUIFlowController::Get(this);
				if (FlowController)
				{
					FlowController->BeginSetting();
				}
			}, 0.01f, false);
	}
}
void ACustomPlayerController::ToggleInventory()
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


void ACustomPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ACustomPlayerController::ToggleInventory);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACustomPlayerController::OnRotateKey);
	}
}


void ACustomPlayerController::OnRotateKey()
{
	// 현재 마우스에 들려있는 DragDropOp 가져오기
	if (UItemDragDropOperation* DragOp = Cast<UItemDragDropOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent()))
	{
		DragOp->RotateItem();
	}
}