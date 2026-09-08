#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "EquipmentWidget.generated.h"

class UEquipSlot;
class UEquipComponent;
class UInventoryComponent;
class UImage;
UCLASS()
class PROJECTPG_API UEquipmentWidget : public UUserWidget
{
	GENERATED_BODY()


private:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UImage> CharacterView;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> WeaponSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> ConsumalSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> HealPackSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> HelmetSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> ClothSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> PantsSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> ShoesSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> SubWeaponSlot;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<UEquipSlot> BackPackSlot;

	/** 슬롯 타입별 위젯 매핑 테이블 */
	UPROPERTY()
	TMap<EEquipSlot, TObjectPtr<UEquipSlot>> SlotWidgetMap;

	UPROPERTY()
	TObjectPtr<UEquipComponent> EquipComponent;

	UPROPERTY()
	TObjectPtr<UInventoryComponent> InventoryComponent;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** 장착 상태 변경 시 UI 전체를 갱신하는 콜백 함수 */
	UFUNCTION()	void UpdateEquipmentUI();


public:
	void InitWidget(UEquipComponent* InEquipComp, UInventoryComponent* InInvenComp);
	void HandleBackpackContainerUpdate();
};