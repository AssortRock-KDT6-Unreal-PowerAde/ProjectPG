#pragma once

#include "Components/InventoryComponent.h"
#include "Common/TableData.h"
#include "EquipmentTestInventory.generated.h"

UCLASS(Transient, NotBlueprintable)
class UEquipmentTestInventory : public UInventoryComponent
{
	GENERATED_BODY()

public:
	TMap<FName, FItemTableRow> TestItems;
	int32 EquipmentChangeCount = 0;
	FGuid LastEquippedGuid;
	bool bObservedInconsistentState = false;

	virtual const FItemTableRow* GetItemData(FName ItemID) const override
	{
		return TestItems.Find(ItemID);
	}

	UFUNCTION()
	void ObserveEquipmentChanged();
};
