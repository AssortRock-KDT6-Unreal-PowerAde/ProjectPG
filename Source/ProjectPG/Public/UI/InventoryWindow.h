// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Server/WebSocketSubSystem.h" // FInventoryMapWrapper 정의 포함
#include "InventoryWindow.generated.h"

// 전방 선언 (Forward Declaration)
class UOverlay;
class UInventoryComponent;
class UInventoryGridWidget;
class UEquipmentWidget;
class UButton;
/**
 * 캐릭터 장비 및 인벤토리 창 통합 윈도우 UI
 */
UCLASS()
class PROJECTPG_API UInventoryWindow : public UUserWidget
{
	GENERATED_BODY()
protected:
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<class UCanvasPanel> MainCanvas;
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> MainInventoryOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> SubInventoryOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> EquipOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> BackPackInvenOverlay;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> BackBtn;


	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEquipmentWidget> EquipmentWidget;

	UPROPERTY()
	TObjectPtr<UInventoryComponent> InvenComp;

	UPROPERTY()
	TObjectPtr<class UEquipComponent> EquipComp;
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

public:
	void InitWidget(UInventoryComponent* InvenComponent, class UEquipComponent* EquipComponent);

	// 외부(LobbyWidget 등)에서 호출하는 인벤토리 초기 세팅용 함수
	void SetupMainInventoryWidget(TSubclassOf<UUserWidget> InvenClass);
	void SetupPocketInventoryWidget(TSubclassOf<UUserWidget> InvenClass);

	void SetupBackPackInventoryWidget(TSubclassOf<UUserWidget> InvenClass);

	void SetChildEquipOverlay(UUserWidget* childWidget);
	void SetChildSubInvenOverlay(UUserWidget* childWidget);
	void SetChildMainInvenOverlay(UUserWidget* ChildWidget);
	void SetChildBackpackInvenOverlay(UUserWidget* childWidget);
	void SetChildMainCanvas(UUserWidget* childWidget);

	void UpdateState();

	UFUNCTION()
	void OnClickedBackBtn();

private:
	UFUNCTION()	void OnInventoryDataReceived(const FInventoryMapWrapper InventoryMapWrapper);

	UFUNCTION() void RefreshAllGrids();


};