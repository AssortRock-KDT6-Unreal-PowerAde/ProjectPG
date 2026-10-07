#include "Common/GameData.h"
#include "Components/InventoryComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventorySnapshotSerializationTest, "ProjectPG.Inventory.SnapshotSerialization", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInventorySnapshotSerializationTest::RunTest(const FString& Parameters)
{
	FInventorySnapshot Original;
	Original.bInitialized = true;
	Original.PocketGuid = FGuid::NewGuid();
	Original.StashGuid = FGuid::NewGuid();
	FInventoryContainerSnapshot& Container = Original.Containers.AddDefaulted_GetRef();
	Container.Guid = FGuid::NewGuid();
	Container.Size = FIntPoint(4, 5);
	Container.EquipSlot = EEquipSlot::BackPack;
	FItemInstance& Item = Container.Items.AddDefaulted_GetRef();
	Item.GUID = FGuid::NewGuid();
	Item.inventory_guid = Item.GUID;
	Item.parent_inventory_guid = Container.Guid;
	Item.ItemID = TEXT("SerializationTestBag");
	Item.bIsRotated = true;
	Item.bEquip = true;
	Item.type = EItemType::Bag;
	Item.Position = FIntPoint(2, 3);
	Item.StackCount = 7;
	Item.Durability = 42.f;

	TArray<uint8> Bytes;
	FMemoryWriter MemoryWriter(Bytes);
	FObjectAndNameAsStringProxyArchive Writer(MemoryWriter, false);
	FInventorySnapshot::StaticStruct()->SerializeItem(Writer, &Original, nullptr);

	FInventorySnapshot Restored;
	FMemoryReader MemoryReader(Bytes);
	FObjectAndNameAsStringProxyArchive Reader(MemoryReader, false);
	FInventorySnapshot::StaticStruct()->SerializeItem(Reader, &Restored, nullptr);

	TestTrue(TEXT("Initialized flag"), Restored.bInitialized);
	TestEqual(TEXT("Pocket GUID"), Restored.PocketGuid, Original.PocketGuid);
	TestEqual(TEXT("Stash GUID"), Restored.StashGuid, Original.StashGuid);
	if (!TestEqual(TEXT("Container count"), Restored.Containers.Num(), 1)) return false;
	const FInventoryContainerSnapshot& Copy = Restored.Containers[0];
	TestEqual(TEXT("Container GUID"), Copy.Guid, Container.Guid);
	TestEqual(TEXT("Container size"), Copy.Size, Container.Size);
	TestTrue(TEXT("Equipment slot"), Copy.EquipSlot == EEquipSlot::BackPack);
	if (!TestEqual(TEXT("Item count"), Copy.Items.Num(), 1)) return false;
	const FItemInstance& CopiedItem = Copy.Items[0];
	TestEqual(TEXT("Item GUID"), CopiedItem.GUID, Item.GUID);
	TestEqual(TEXT("Inventory GUID"), CopiedItem.inventory_guid, Item.inventory_guid);
	TestEqual(TEXT("Parent GUID"), CopiedItem.parent_inventory_guid, Container.Guid);
	TestEqual(TEXT("Item ID"), CopiedItem.ItemID, Item.ItemID);
	TestEqual(TEXT("Position"), CopiedItem.Position, Item.Position);
	TestEqual(TEXT("Stack count"), CopiedItem.StackCount, Item.StackCount);
	TestEqual(TEXT("Durability"), CopiedItem.Durability, Item.Durability);
	TestTrue(TEXT("Rotation"), CopiedItem.bIsRotated);
	TestTrue(TEXT("Equipped"), CopiedItem.bEquip);
	TestTrue(TEXT("Item type"), CopiedItem.type == EItemType::Bag);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryInitialSnapshotTest, "ProjectPG.Inventory.InitialSnapshotValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInventoryInitialSnapshotTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Inventory owner"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>(Owner);
	Owner->AddInstanceComponent(Inventory);
	Inventory->RegisterComponent();

	FInventorySnapshot Valid;
	Valid.bInitialized = true;
	Valid.PocketGuid = FGuid::NewGuid();
	Valid.StashGuid = FGuid::NewGuid();
	FInventoryContainerSnapshot Pocket;
	Pocket.Guid = Valid.PocketGuid;
	Pocket.Size = FIntPoint(4, 4);
	Valid.Containers.Add(Pocket);
	FInventoryContainerSnapshot Stash;
	Stash.Guid = Valid.StashGuid;
	Stash.Size = FIntPoint(10, 10);
	Valid.Containers.Add(Stash);

	FInventorySnapshot Invalid = Valid;
	Invalid.Containers[0].Size = FIntPoint(0, 4);
	TestFalse(TEXT("Reject zero-sized container"), Inventory->InitializeFromSnapshot(Invalid));
	Invalid = Valid;
	Invalid.Containers[0].Size = FIntPoint(MAX_int32, MAX_int32);
	TestFalse(TEXT("Reject excessive grid allocation"), Inventory->InitializeFromSnapshot(Invalid));
	Invalid = Valid;
	Invalid.Containers.Add(Pocket);
	TestFalse(TEXT("Reject duplicate container GUID"), Inventory->InitializeFromSnapshot(Invalid));
	Invalid = Valid;
	Invalid.PocketGuid = FGuid::NewGuid();
	TestFalse(TEXT("Reject missing root container"), Inventory->InitializeFromSnapshot(Invalid));
	TestFalse(TEXT("Invalid snapshots do not initialize state"), Inventory->HasInitialInventory());
	TestTrue(TEXT("Accept valid first snapshot"), Inventory->InitializeFromSnapshot(Valid));
	TestTrue(TEXT("Initialized state"), Inventory->HasInitialInventory());
	FInventorySnapshot Replacement = Valid;
	Replacement.Containers[0].Size = FIntPoint(5, 5);
	TestFalse(TEXT("Reject second initialization"), Inventory->InitializeFromSnapshot(Replacement));
	TestEqual(TEXT("Original inventory is retained"), Inventory->GetInventorySizeByGuid(Valid.PocketGuid), Pocket.Size);
	TestEqual(TEXT("Empty containers are retained"), Inventory->MakeSnapshot().Containers.Num(), 2);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLobbyInventoryRefreshTest, "ProjectPG.Inventory.LobbyRepeatedRefresh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLobbyInventoryRefreshTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	AActor* Owner = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Inventory owner"), Owner))
	{
		World->DestroyWorld(false);
		return false;
	}
	UInventoryComponent* Inventory = NewObject<UInventoryComponent>(Owner);
	Owner->AddInstanceComponent(Inventory);
	Inventory->RegisterComponent();
	TestFalse(TEXT("Standalone without InGame uses WebSocket data"), Inventory->IsServerManaged());

	FInventoryMapWrapper Response;
	Response.PocketGuid = FGuid::NewGuid();
	Response.StashGuid = FGuid::NewGuid();
	Response.InventorySizeMap.Add(Response.PocketGuid, FIntPoint(4, 4));
	Response.InventorySizeMap.Add(Response.StashGuid, FIntPoint(10, 10));
	FItemInstance Item;
	Item.GUID = FGuid::NewGuid();
	Item.parent_inventory_guid = Response.PocketGuid;
	Item.StackCount = 1;
	Response.InventoryMap.FindOrAdd(Response.PocketGuid).Items.Add(Item);
	Inventory->HandleInventoryReceived(Response);
	TestEqual(TEXT("First response applied"), Inventory->GetItems(Response.PocketGuid).Num(), 1);

	Response.InventoryMap.FindChecked(Response.PocketGuid).Items[0].StackCount = 5;
	Response.InventorySizeMap[Response.PocketGuid] = FIntPoint(5, 5);
	Inventory->HandleInventoryReceived(Response);
	TestEqual(TEXT("Subsequent response updates size"), Inventory->GetInventorySizeByGuid(Response.PocketGuid), FIntPoint(5, 5));
	if (TestEqual(TEXT("Subsequent response retains one item"), Inventory->GetItems(Response.PocketGuid).Num(), 1))
	{
		TestEqual(TEXT("Subsequent response updates quantity"), Inventory->GetItems(Response.PocketGuid)[0].StackCount, 5);
	}

	Response.InventoryMap.Empty();
	Inventory->HandleInventoryReceived(Response);
	TestEqual(TEXT("Empty response removes previous items"), Inventory->GetItems(Response.PocketGuid).Num(), 0);
	World->DestroyWorld(false);
	return true;
}

#endif
