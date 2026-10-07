#include "EquipmentTestInventory.h"
#include "Components/EquipComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameMode/GameMode_InGame.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSharedEquipmentTransferTest, "ProjectPG.Inventory.Equipment.SharedTransfer", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSharedEquipmentTransferTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	if (!TestNotNull(TEXT("Test world"), World)) return false;
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	AGameStateBase* State = World->SpawnActor<AGameStateBase>();
	if (!TestNotNull(TEXT("Game state"), State)) return false;
	State->GameModeClass = AGameMode_InGame::StaticClass();
	World->SetGameState(State);

	auto MakeInventory = [World](bool bPlayer)
	{
		AActor* Owner = bPlayer ? World->SpawnActor<APlayerState>() : World->SpawnActor<AActor>();
		if (!Owner) return static_cast<UEquipmentTestInventory*>(nullptr);
		UEquipmentTestInventory* Inventory = NewObject<UEquipmentTestInventory>(Owner);
		Owner->AddInstanceComponent(Inventory);
		Inventory->RegisterComponent();
		FItemTableRow Data;
		Data.GridSize = FIntPoint(2, 3);
		Data.ItemType = EItemType::Armor;
		Data.EquipSlotType = EEquipSlot::HelMet;
		Inventory->TestItems.Add(TEXT("TestHelmet"), Data);
		if (bPlayer)
		{
			UEquipComponent* Equipment = NewObject<UEquipComponent>(Owner);
			Owner->AddInstanceComponent(Equipment);
			Equipment->RegisterComponent();
			Equipment->OnEquipmentChanged.AddDynamic(Inventory, &UEquipmentTestInventory::ObserveEquipmentChanged);
		}
		return Inventory;
	};
	auto EmptySnapshot = []()
	{
		FInventorySnapshot Snapshot;
		Snapshot.bInitialized = true;
		Snapshot.PocketGuid = FGuid::NewGuid();
		Snapshot.StashGuid = FGuid::NewGuid();
		for (const FGuid& Guid : { Snapshot.PocketGuid, Snapshot.StashGuid })
		{
			FInventoryContainerSnapshot& Container = Snapshot.Containers.AddDefaulted_GetRef();
			Container.Guid = Guid;
			Container.Size = FIntPoint(4, 4);
		}
		return Snapshot;
	};
	UEquipmentTestInventory* A = MakeInventory(true);
	UEquipmentTestInventory* B = MakeInventory(true);
	UEquipmentTestInventory* Shared = MakeInventory(false);
	if (!TestNotNull(TEXT("A inventory"), A) || !TestNotNull(TEXT("B inventory"), B) || !TestNotNull(TEXT("Shared inventory"), Shared)) return false;
	TestTrue(TEXT("Uses server-managed transfer path"), B->IsServerManaged());

	FInventorySnapshot InitialA = EmptySnapshot();
	const FInventorySnapshot InitialB = EmptySnapshot();
	const FInventorySnapshot InitialShared = EmptySnapshot();
	const FGuid ASlot = FGuid::NewGuid();
	FInventoryContainerSnapshot& Slot = InitialA.Containers.AddDefaulted_GetRef();
	Slot.Guid = ASlot;
	Slot.Size = FIntPoint(1, 1);
	Slot.EquipSlot = EEquipSlot::HelMet;
	FItemInstance Helmet;
	Helmet.GUID = FGuid::NewGuid();
	Helmet.ItemID = TEXT("TestHelmet");
	Helmet.StackCount = 1;
	Helmet.parent_inventory_guid = ASlot;
	Helmet.bEquip = true;
	Slot.Items.Add(Helmet);
	if (!TestTrue(TEXT("Initialize A equipped"), A->InitializeFromSnapshot(InitialA)) ||
		!TestTrue(TEXT("Initialize B without equipment slots"), B->InitializeFromSnapshot(InitialB)) ||
		!TestTrue(TEXT("Initialize shared container"), Shared->InitializeFromSnapshot(InitialShared))) return false;
	A->GetOwner()->DispatchBeginPlay();
	B->GetOwner()->DispatchBeginPlay();
	Shared->GetOwner()->DispatchBeginPlay();
	UEquipComponent* EquipA = A->GetOwner()->FindComponentByClass<UEquipComponent>();
	UEquipComponent* EquipB = B->GetOwner()->FindComponentByClass<UEquipComponent>();
	TestEqual(TEXT("Preserve A slot GUID"), A->GetEquipSlotIDs().FindRef(EEquipSlot::HelMet), ASlot);
	TestEqual(TEXT("Generate all B slots"), B->GetEquipSlotIDs().Num(), static_cast<int32>(EEquipSlot::MAX));
	TestEqual(TEXT("Shared inventory has no equipment slots"), Shared->GetEquipSlotIDs().Num(), 0);
	const FGuid BSlot = B->GetEquipSlotIDs().FindRef(EEquipSlot::HelMet);
	TestTrue(TEXT("B owns a distinct valid slot"), BSlot.IsValid() && BSlot != ASlot);
	const FInventorySnapshot BState = B->MakeSnapshot();
	TestTrue(TEXT("Generated slot is included in replication snapshot"), BState.Containers.ContainsByPredicate([BSlot](const FInventoryContainerSnapshot& Container) { return Container.Guid == BSlot && Container.EquipSlot == EEquipSlot::HelMet && Container.Items.IsEmpty(); }));
	TestFalse(TEXT("Reinitialization cannot replace B slots"), B->InitializeFromSnapshot(InitialB));
	TestEqual(TEXT("B slot remains stable"), B->GetEquipSlotIDs().FindRef(EEquipSlot::HelMet), BSlot);

	if (!TestTrue(TEXT("A unequips into shared storage"), EquipA->UnEquipTo(EEquipSlot::HelMet, Shared, InitialShared.PocketGuid, FIntPoint::ZeroValue, false))) return false;
	TestNull(TEXT("A equipment cleared"), EquipA->GetEquipment(EEquipSlot::HelMet));
	TestNull(TEXT("A no longer owns item"), A->FindItemByGuid(Helmet.GUID));
	if (!TestTrue(TEXT("B takes item into pocket"), B->TransferItemFrom(Shared, Helmet.GUID, InitialB.PocketGuid, FIntPoint::ZeroValue, false))) return false;
	const FItemInstance* Taken = B->FindItemByGuid(Helmet.GUID);
	if (!TestNotNull(TEXT("Item now belongs to B inventory"), Taken)) return false;
	TestTrue(TEXT("Owner is B and equipped flag cleared"), Taken->Owner == B->GetOwner() && !Taken->bEquip);
	if (!TestTrue(TEXT("B equips item from own pocket"), EquipB->Equip(*Taken))) return false;
	TestEqual(TEXT("B equipment UI receives item"), B->LastEquippedGuid, Helmet.GUID);
	TestEqual(TEXT("B equipment slot contains item"), B->GetItems(BSlot).Num(), 1);
	TestNull(TEXT("Shared storage no longer has item"), Shared->FindItemByGuid(Helmet.GUID));
	if (!TestTrue(TEXT("B returns equipped item to storage"), EquipB->UnEquipTo(EEquipSlot::HelMet, Shared, InitialShared.PocketGuid, FIntPoint::ZeroValue, false))) return false;
	TestTrue(TEXT("B equips directly from storage"), EquipB->Equip(Helmet, Shared));
	TestEqual(TEXT("Direct equip updates B UI"), B->LastEquippedGuid, Helmet.GUID);
	TestFalse(TEXT("B UI never observes stale equipment"), B->bObservedInconsistentState);
	int32 Copies = 0;
	for (const UEquipmentTestInventory* Inventory : { A, B, Shared })
	{
		for (const auto& Pair : Inventory->GetItemsMap())
		{
			for (const FItemInstance& Item : Pair.Value.Items) Copies += Item.GUID == Helmet.GUID ? 1 : 0;
		}
	}
	TestEqual(TEXT("Exactly one item across A, B and storage"), Copies, 1);
	return true;
}

#endif
