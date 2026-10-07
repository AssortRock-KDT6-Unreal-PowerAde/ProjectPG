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
class UEquipComponent;
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
	TObjectPtr<UButton> BackBtn;


	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEquipmentWidget> EquipmentWidget;

	UPROPERTY()
	TObjectPtr<UInventoryComponent> InvenComp;

	UPROPERTY()
	TObjectPtr<UEquipComponent> EquipComp;

	// When showing an InteractActor's main inventory, this holds that actor's InventoryComponent
	UPROPERTY()
	TObjectPtr<UInventoryComponent> MainInventoryComp;
	bool bShowMainInventory;
	// When true, the main overlay is explicitly bound to an interact target's inventory component
	// and should not fall back to the player's InvenComp.
	bool bBoundToInteractTarget = false;

	// Optional: when set, Force the main overlay to display this specific container GUID
	FGuid MainInventoryGUID;
public:

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UOverlay> BackPackInvenOverlay;
protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	// Timer handle for deferred refresh when container registration is delayed
	FTimerHandle DeferredRefreshTimer;

public:
	// InvenComponent: player-owned inventory (stash/pocket/backpack)
	// EquipComponent: player's equip component
	// InMainInventory: optional inventory to display in the Main area (e.g., InteractActor). If nullptr, Main shows player stash.
	void InitWidget(UInventoryComponent* InvenComponent, UEquipComponent* EquipComponent, UInventoryComponent* InMainInventory = nullptr);

	UFUNCTION(BlueprintCallable)
	void SetShowMainInventory(bool bShow) { bShowMainInventory = bShow; }

	// Alternative init: bind only actor inventory (for InteractActor)
	UFUNCTION(BlueprintCallable)
	void InitWidgetForActor(UInventoryComponent* ActorInventory);

	// Explicit init variants
	UFUNCTION(BlueprintCallable)
	void InitForPlayer(UInventoryComponent* PlayerInv, UEquipComponent* PlayerEquip, bool bShowMain = true);

	UFUNCTION(BlueprintCallable)
	void InitForContainer(UInventoryComponent* ContainerInv);
	// Overload: specify which container GUID of the provided InventoryComponent should be shown in Main area
	void InitForContainer(UInventoryComponent* ContainerInv, const FGuid& PreferredGuid);

	UFUNCTION(BlueprintCallable)
	void InitForHuman(UInventoryComponent* HumanInv, UEquipComponent* HumanEquip);

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
	UFUNCTION() void RefreshAllGrids();

private:
	// Centralized initializer to avoid duplicate delegate bindings between Init variants.
	void ApplyInit(UInventoryComponent* PlayerInv, UEquipComponent* PlayerEquip, UInventoryComponent* MainInv, bool bBindToInteractTargetFlag);
	UFUNCTION()	void OnInventoryDataReceived(const FInventoryMapWrapper& InventoryMapWrapper);



};