#include "EquipmentTestInventory.h"
#include "Components/EquipComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Misc/AutomationTest.h"

void UEquipmentTestInventory::ObserveEquipmentChanged()
{
	++EquipmentChangeCount;
	const UEquipComponent* Equipment = GetOwner()->FindComponentByClass<UEquipComponent>();
	const FItemInstance* Equipped = Equipment ? Equipment->GetEquipment(EEquipSlot::HelMet) : nullptr;
	LastEquippedGuid = Equipped ? Equipped->GUID : FGuid();
	if (Equipped)
	{
		const FItemInstance* Stored = FindItemByGuid(Equipped->GUID);
		bObservedInconsistentState |= !Stored || !Stored->bEquip || Stored->parent_inventory_guid != Equipped->parent_inventory_guid;
	}
}

#if WITH_DEV_AUTOMATION_TESTS
namespace
{
	struct FEquipmentFixture
	{
		UWorld* World = nullptr;
		UEquipmentTestInventory* Inventory = nullptr;
		UEquipComponent* Equipment = nullptr;
		FGuid Pocket = FGuid::NewGuid();
		FGuid Stash = FGuid::NewGuid();
		FGuid HelmetSlot = FGuid::NewGuid();
		FItemInstance Helmet;

		FEquipmentFixture()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!World) return;
			APlayerState* Owner = World->SpawnActor<APlayerState>();
			if (!Owner) return;
			Inventory = NewObject<UEquipmentTestInventory>(Owner);
			Owner->AddInstanceComponent(Inventory);
			Inventory->RegisterComponent();
			Equipment = NewObject<UEquipComponent>(Owner);
			Owner->AddInstanceComponent(Equipment);
			Equipment->RegisterComponent();
			Equipment->OnEquipmentChanged.AddDynamic(Inventory, &UEquipmentTestInventory::ObserveEquipmentChanged);

			FItemTableRow HelmetData;
			HelmetData.GridSize = FIntPoint(2, 3);
			HelmetData.ItemType = EItemType::Armor;
			HelmetData.EquipSlotType = EEquipSlot::HelMet;
			Inventory->TestItems.Add(TEXT("TestHelmet"), HelmetData);
			FItemTableRow BlockerData;
			BlockerData.GridSize = FIntPoint(4, 4);
			BlockerData.EquipSlotType = EEquipSlot::MAX;
			Inventory->TestItems.Add(TEXT("TestBlocker"), BlockerData);
			Inventory->SetPocketInventoryID(Pocket);
			Inventory->SetStashInventoryID(Stash);
			Inventory->RegisterContainer(Pocket, FIntPoint(4, 4));
			Inventory->RegisterContainer(Stash, FIntPoint(4, 4));
			Equipment->RegisterGuid(EEquipSlot::HelMet, HelmetSlot);
			Helmet.GUID = FGuid::NewGuid();
			Helmet.ItemID = TEXT("TestHelmet");
			Helmet.type = EItemType::Armor;
			Helmet.StackCount = 1;
			Helmet.parent_inventory_guid = Pocket;
		}

		~FEquipmentFixture()
		{
			if (World) World->DestroyWorld(false);
		}

		int32 CountItem(const FGuid& Guid) const
		{
			int32 Count = 0;
			for (const auto& Pair : Inventory->GetItemsMap())
			{
				for (const FItemInstance& Item : Pair.Value.Items) Count += Item.GUID == Guid ? 1 : 0;
			}
			return Count;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentReequipTest, "ProjectPG.Inventory.Equipment.Reequip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEquipmentReequipTest::RunTest(const FString& Parameters)
{
	FEquipmentFixture Fixture;
	if (!TestNotNull(TEXT("Equipment component"), Fixture.Equipment)) return false;
	UEquipmentTestInventory* Inventory = Fixture.Inventory;
	UEquipComponent* Equipment = Fixture.Equipment;
	TestTrue(TEXT("Seed inventory"), Inventory->AddItemAt(Fixture.Helmet, FIntPoint::ZeroValue));
	TestTrue(TEXT("Large item fits compatible equipment slot"), Inventory->CanPlaceItemByGuid(Fixture.HelmetSlot, Fixture.Helmet.ItemID, FIntPoint::ZeroValue, false, Fixture.Helmet.GUID));
	TestFalse(TEXT("Wrong equipment type is rejected"), Equipment->CanEquip(Fixture.Helmet, EEquipSlot::MainWeapon));
	TestFalse(TEXT("Equipment slot does not accept a grid position"), Inventory->CanPlaceItemByGuid(Fixture.HelmetSlot, Fixture.Helmet.ItemID, FIntPoint(1, 0), false, Fixture.Helmet.GUID));

	for (int32 Iteration = 0; Iteration < 3; ++Iteration)
	{
		TestTrue(TEXT("Equip large item"), Equipment->Equip(Fixture.Helmet));
		TestEqual(TEXT("One copy after equip"), Fixture.CountItem(Fixture.Helmet.GUID), 1);
		TestEqual(TEXT("Equipment UI notified of equipped item"), Inventory->LastEquippedGuid, Fixture.Helmet.GUID);
		TestEqual(TEXT("Original pocket empty"), Inventory->GetItems(Fixture.Pocket).Num(), 0);
		const FItemInstance* Equipped = Equipment->GetEquipment(EEquipSlot::HelMet);
		if (!TestNotNull(TEXT("Equipment state exists"), Equipped)) return false;
		TestTrue(TEXT("Equipped flag set"), Equipped->bEquip);
		TestEqual(TEXT("Equipment parent matches slot"), Equipped->parent_inventory_guid, Fixture.HelmetSlot);
		TestTrue(TEXT("Move equipment to inventory"), Equipment->UnEquipTo(EEquipSlot::HelMet, Inventory, Fixture.Pocket, FIntPoint(1, 1), false));
		TestNull(TEXT("Equipment state cleared"), Equipment->GetEquipment(EEquipSlot::HelMet));
		TestFalse(TEXT("UI notified of empty slot"), Inventory->LastEquippedGuid.IsValid());
		TestEqual(TEXT("One copy after unequip"), Fixture.CountItem(Fixture.Helmet.GUID), 1);
		const FItemInstance* Restored = Inventory->FindItemByGuid(Fixture.Helmet.GUID);
		if (!TestNotNull(TEXT("Restored item"), Restored)) return false;
		TestFalse(TEXT("Unequipped flag cleared"), Restored->bEquip);
		TestEqual(TEXT("Requested inventory position"), Restored->Position, FIntPoint(1, 1));
	}
	TestEqual(TEXT("Each successful transition updates equipment UI"), Inventory->EquipmentChangeCount, 6);
	TestFalse(TEXT("UI observes committed inventory state"), Inventory->bObservedInconsistentState);
	TestTrue(TEXT("Equip before automatic restore"), Equipment->Equip(Fixture.Helmet));
	TestTrue(TEXT("PlayerState-owned equipment restores to pocket"), Equipment->UnEquip(EEquipSlot::HelMet));
	TestEqual(TEXT("Automatic restore retains one copy"), Fixture.CountItem(Fixture.Helmet.GUID), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentFailurePreservesStateTest, "ProjectPG.Inventory.Equipment.FailurePreservesState", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEquipmentFailurePreservesStateTest::RunTest(const FString& Parameters)
{
	FEquipmentFixture Fixture;
	if (!TestNotNull(TEXT("Equipment component"), Fixture.Equipment)) return false;
	UEquipmentTestInventory* Inventory = Fixture.Inventory;
	UEquipComponent* Equipment = Fixture.Equipment;
	TestFalse(TEXT("Missing source item cannot equip"), Equipment->Equip(Fixture.Helmet));
	TestEqual(TEXT("Failure does not publish equipment UI state"), Inventory->EquipmentChangeCount, 0);
	TestTrue(TEXT("Seed inventory"), Inventory->AddItemAt(Fixture.Helmet, FIntPoint::ZeroValue));
	TestTrue(TEXT("Equip before failed restore"), Equipment->Equip(Fixture.Helmet));
	FItemInstance Blocker;
	Blocker.ItemID = TEXT("TestBlocker");
	Blocker.StackCount = 1;
	Blocker.GUID = FGuid::NewGuid();
	Blocker.parent_inventory_guid = Fixture.Pocket;
	TestTrue(TEXT("Fill pocket"), Inventory->AddItemAt(Blocker, FIntPoint::ZeroValue));
	Blocker.GUID = FGuid::NewGuid();
	Blocker.parent_inventory_guid = Fixture.Stash;
	TestTrue(TEXT("Fill stash"), Inventory->AddItemAt(Blocker, FIntPoint::ZeroValue));
	TestFalse(TEXT("Occupied drop target rejects unequip"), Equipment->UnEquipTo(EEquipSlot::HelMet, Inventory, Fixture.Pocket, FIntPoint::ZeroValue, false));
	TestFalse(TEXT("Full inventories reject automatic unequip"), Equipment->UnEquip(EEquipSlot::HelMet));
	TestNotNull(TEXT("Equipment remains equipped"), Equipment->GetEquipment(EEquipSlot::HelMet));
	TestEqual(TEXT("Equipment slot retains original"), Inventory->GetItems(Fixture.HelmetSlot).Num(), 1);
	TestEqual(TEXT("Failures do not clear equipment UI"), Inventory->EquipmentChangeCount, 1);
	TestEqual(TEXT("Failures retain exactly one item"), Fixture.CountItem(Fixture.Helmet.GUID), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FEquipmentSnapshotReequipTest, "ProjectPG.Inventory.Equipment.SnapshotReequip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FEquipmentSnapshotReequipTest::RunTest(const FString& Parameters)
{
	FEquipmentFixture Fixture;
	if (!TestNotNull(TEXT("Equipment component"), Fixture.Equipment)) return false;
	FInventoryMapWrapper EquippedResponse;
	EquippedResponse.HelMet = Fixture.HelmetSlot;
	FItemInstance EquippedItem = Fixture.Helmet;
	EquippedItem.parent_inventory_guid = Fixture.HelmetSlot;
	EquippedItem.bEquip = true;
	EquippedResponse.InventoryMap.FindOrAdd(Fixture.HelmetSlot).Items.Add(EquippedItem);
	UFunction* ReceiveEquipment = Fixture.Equipment->FindFunction(TEXT("SetServerEquipData"));
	if (!TestNotNull(TEXT("Equipment snapshot handler"), ReceiveEquipment)) return false;
	Fixture.Equipment->ProcessEvent(ReceiveEquipment, &EquippedResponse);

	FInventoryMapWrapper InventoryResponse;
	InventoryResponse.PocketGuid = Fixture.Pocket;
	InventoryResponse.StashGuid = Fixture.Stash;
	InventoryResponse.HelMet = Fixture.HelmetSlot;
	InventoryResponse.InventorySizeMap.Add(Fixture.Pocket, FIntPoint(4, 4));
	InventoryResponse.InventorySizeMap.Add(Fixture.Stash, FIntPoint(4, 4));
	Fixture.Inventory->HandleInventoryReceived(InventoryResponse);
	TestEqual(TEXT("Inventory response retains equipped source"), Fixture.Inventory->GetItems(Fixture.HelmetSlot).Num(), 1);
	TestTrue(TEXT("Received equipment can be removed"), Fixture.Equipment->UnEquipTo(EEquipSlot::HelMet, Fixture.Inventory, Fixture.Pocket, FIntPoint::ZeroValue, false));
	TestTrue(TEXT("Received equipment can be equipped again"), Fixture.Equipment->Equip(Fixture.Helmet));
	TestEqual(TEXT("Received equipment remains unique"), Fixture.CountItem(Fixture.Helmet.GUID), 1);
	TestEqual(TEXT("UI updated after re-equip"), Fixture.Inventory->LastEquippedGuid, Fixture.Helmet.GUID);
	return true;
}

#endif
