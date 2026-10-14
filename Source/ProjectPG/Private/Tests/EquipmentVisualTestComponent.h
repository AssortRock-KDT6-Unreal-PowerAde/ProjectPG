#pragma once

#include "Components/EquipComponent.h"
#include "Common/TableData.h"
#include "EquipmentVisualTestComponent.generated.h"

UCLASS(Transient, NotBlueprintable)
class UEquipmentVisualTestComponent : public UEquipComponent
{
	GENERATED_BODY()
public:
	TMap<FName, FEquipTableRow> TestEquipment;
protected:
	virtual const FEquipTableRow* GetEquipData(FName ItemID) const override
	{
		return TestEquipment.Find(ItemID);
	}
};
