#include "Tests/EquipmentInventoryMockGameMode.h"

#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Components/InputComponent.h"
#include "Core/TableSubSystem.h"
#include "Core/UIManagerSubSystem.h"
#include "GameFramework/PlayerState.h"
#include "InputCoreTypes.h"
#include "Server/InventorySubSystem.h"
#include "TimerManager.h"
#include "UI/InventoryWindow.h"
#include "UObject/ConstructorHelpers.h"

AEquipmentInventoryMockController::AEquipmentInventoryMockController() = default;

void AEquipmentInventoryMockController::BeginPlay()
{
	// Deliberately skip InGame's WebSocket/travel-snapshot initializer only in this
	// test subclass. Its input setup and Server_MoveInventoryItem remain inherited.
	APlayerController::BeginPlay();
	if (IsLocalController())
	{
		if (UInventorySubSystem* Inventory = UInventorySubSystem::Get(GetWorld()))
		{
			Inventory->SetUseWebSocket(false);
			Inventory->SetForceLocalMoves(true);
		}
		UE_LOG(LogTemp, Display, TEXT("[EquipmentInventoryMock] Press I to open the existing inventory UI."));
	}
}

void AEquipmentInventoryMockController::SetupInputComponent()
{
	APlayerController::SetupInputComponent();
	if (!InputComponent) return;
	InputComponent->BindKey(EKeys::I, IE_Pressed, this, &AEquipmentInventoryMockController::ToggleMockInventory);
	InputComponent->BindKey(EKeys::R, IE_Pressed, this, &APlayerController_InGame::OnRotateKey);
}

void AEquipmentInventoryMockController::ToggleMockInventory()
{
	if (!IsLocalController() || !PlayerState) return;
	UInventoryComponent* Inventory = PlayerState->FindComponentByClass<UInventoryComponent>();
	UEquipComponent* Equipment = PlayerState->FindComponentByClass<UEquipComponent>();
	if (!Inventory || !Inventory->HasInitialInventory())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EquipmentInventoryMock] Inventory has not arrived yet."));
		return;
	}
	if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(this))
	{
		if (UInventoryWindow* Window = Cast<UInventoryWindow>(UI->ToggleUI(EUIType::Character)))
		{
			Window->InitForPlayer(Inventory, Equipment, true);
			// InitForPlayer refreshes existing grids, but does not create the stash
			// grid when this Character widget starts with an empty Main overlay.
			if (TSubclassOf<UUserWidget> GridClass = UI->GetUIClass(EUIType::Inventory))
				Window->SetupMainInventoryWidget(GridClass);
			else
				UE_LOG(LogTemp, Error, TEXT("[EquipmentInventoryMock] Inventory grid UI class is not registered."));
			UE_LOG(LogTemp, Display, TEXT("[EquipmentInventoryMock] UI bound: Pocket=%d Stash=%d"),
				Inventory->GetItems(Inventory->GetPocketInventoryID()).Num(),
				Inventory->GetItems(Inventory->GetStashInventoryID()).Num());
		}
	}
}

AEquipmentInventoryMockGameMode::AEquipmentInventoryMockGameMode()
{
	PlayerControllerClass = AEquipmentInventoryMockController::StaticClass();
	static ConstructorHelpers::FClassFinder<APawn> Character(TEXT("/Game/PG/Blueprint/Characters/CBP_CustomPlayerCharacter_Modular"));
	if (Character.Succeeded()) DefaultPawnClass = Character.Class;
}

void AEquipmentInventoryMockGameMode::BeginPlay()
{
	Super::BeginPlay();
	GetWorldTimerManager().SetTimer(InitializationTimer, this,
		&AEquipmentInventoryMockGameMode::InitializePendingPlayers, 0.25f, true);
	InitializePendingPlayers();
}

void AEquipmentInventoryMockGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(InitializationTimer);
	Super::EndPlay(Reason);
}

void AEquipmentInventoryMockGameMode::InitializePendingPlayers()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* Controller = It->Get();
		if (IsValid(Controller) && !InitializedPlayers.Contains(Controller)
			&& InitializePlayerInventory(Controller)) InitializedPlayers.Add(Controller);
	}
	// Report startup problems once; keep polling to support late-joining PIE players.
	if (++InitializationAttempts == 40)
	{
		UE_LOG(LogTemp, Display, TEXT("[EquipmentInventoryMock] Initialized %d player(s). Missing players require valid ItemTable/EquipTable and CustomPlayerState."), InitializedPlayers.Num());
	}
}

bool AEquipmentInventoryMockGameMode::InitializePlayerInventory(APlayerController* Controller)
{
	APlayerState* State = Controller->PlayerState;
	UInventoryComponent* Inventory = State ? State->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inventory) return false;
	if (Inventory->HasInitialInventory())
	{
		UE_LOG(LogTemp, Warning, TEXT("[EquipmentInventoryMock] %s already has inventory; preserving it without overwriting."), *Controller->GetName());
		return true;
	}
	const UTableSubSystem* Tables = UTableSubSystem::Get(this);
	UDataTable* Items = Tables ? Tables->FindTable(TEXT("ItemTable")).Get() : nullptr;
	UDataTable* Equipment = Tables ? Tables->FindTable(TEXT("EquipTable")).Get() : nullptr;
	if (!Items || !Equipment) return false;

	FInventorySnapshot Snapshot;
	Snapshot.bInitialized = true;
	Snapshot.PocketGuid = FGuid::NewGuid();
	Snapshot.StashGuid = FGuid::NewGuid();
	FInventoryContainerSnapshot Pocket;
	Pocket.Guid = Snapshot.PocketGuid;
	Pocket.Size = FIntPoint(FMath::Clamp(PocketSize.X, 1, 64), FMath::Clamp(PocketSize.Y, 1, 64));
	FInventoryContainerSnapshot Stash;
	Stash.Guid = Snapshot.StashGuid;
	Stash.Size = FIntPoint(FMath::Clamp(StashSize.X, 1, 64), FMath::Clamp(StashSize.Y, 1, 64));
	TSet<FIntPoint> Occupied;
	TArray<FName> IDs = TestItemIDs.IsEmpty() ? Equipment->GetRowNames() : TestItemIDs;
	IDs.Sort(FNameLexicalLess());
	TSet<FName> AddedIDs;
	for (FName ID : IDs)
	{
		const FItemTableRow* Item = Items->FindRow<FItemTableRow>(ID, TEXT("Equipment mock"), false);
		const FEquipTableRow* Equip = Equipment->FindRow<FEquipTableRow>(ID, TEXT("Equipment mock"), false);
		if (AddedIDs.Contains(ID) || !Item || !Equip || !Equip->EquipActorClass
			|| Item->EquipSlotType == EEquipSlot::MAX || Equip->EquipType != Item->EquipSlotType
			|| Item->GridSize.X <= 0 || Item->GridSize.Y <= 0) continue;
		FIntPoint Position(-1, -1);
		for (int32 Y = 0; Y <= Stash.Size.Y - Item->GridSize.Y && Position.X < 0; ++Y)
			for (int32 X = 0; X <= Stash.Size.X - Item->GridSize.X; ++X)
			{
				bool bFree = true;
				for (int32 DY = 0; DY < Item->GridSize.Y && bFree; ++DY)
					for (int32 DX = 0; DX < Item->GridSize.X; ++DX)
						if (Occupied.Contains(FIntPoint(X + DX, Y + DY))) { bFree = false; break; }
				if (bFree) { Position = FIntPoint(X, Y); break; }
			}
		if (Position.X < 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("[EquipmentInventoryMock] Stash full; skipped %s."), *ID.ToString());
			continue;
		}
		FItemInstance Instance;
		Instance.GUID = FGuid::NewGuid();
		Instance.inventory_guid = Instance.GUID;
		Instance.ItemID = ID;
		Instance.parent_inventory_guid = Stash.Guid;
		Instance.Position = Position;
		Instance.StackCount = 1;
		Instance.type = Item->ItemType;
		Instance.Owner = State;
		Stash.Items.Add(Instance);
		AddedIDs.Add(ID);
		for (int32 DY = 0; DY < Item->GridSize.Y; ++DY)
			for (int32 DX = 0; DX < Item->GridSize.X; ++DX) Occupied.Add(Position + FIntPoint(DX, DY));
		if (Item->ItemType == EItemType::Bag)
		{
			const FItemBackpackTable* Bag = Tables->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), ID);
			if (Bag && Bag->SlotSize.X > 0 && Bag->SlotSize.Y > 0)
			{
				FInventoryContainerSnapshot Container;
				Container.Guid = Instance.GUID;
				Container.Size = Bag->SlotSize;
				Snapshot.Containers.Add(Container);
			}
		}
	}
	if (AddedIDs.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("[EquipmentInventoryMock] No compatible equipment rows; check table row IDs, slot types and actor classes."));
		return true;
	}
	Snapshot.Containers.Add(Pocket);
	Snapshot.Containers.Add(Stash);
	FString Error;
	if (!Inventory->InitializeFromSnapshot(Snapshot, &Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[EquipmentInventoryMock] Snapshot rejected: %s"), *Error);
		return true;
	}
	State->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[EquipmentInventoryMock] %s: %d equipment items seeded in stash; equip slots initially empty."), *Controller->GetName(), AddedIDs.Num());
	return true;
}
