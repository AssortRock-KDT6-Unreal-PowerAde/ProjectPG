#pragma once

#include "Components/EquipComponent.h"
#include "Common/TableData.h"
#include "EquipmentVisualTestComponent.generated.h"

UCLASS(Transient, NotBlueprintable)
class AEquipmentVisualTestActor : public AEquipActor
{
	GENERATED_BODY()
public:
	void ReceiveMesh(UStaticMesh* Mesh)
	{
		WorldMesh = Mesh;
		OnRep_EquipMesh();
	}
	void ReceiveCharacter(ACharacter* Character)
	{
		OwnerCharacter = Character;
		OnRep_OwnerCharacter();
	}
	void ReceiveSocket(FName Socket)
	{
		EquipSocketName = Socket;
		OnRep_SocketName();
	}
};

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
