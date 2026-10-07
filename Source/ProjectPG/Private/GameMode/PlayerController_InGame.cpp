#include "GameMode/PlayerController_InGame.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/InventoryWindow.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameMode/CustomPlayerState.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Server/WebSocketSubSystem.h"
#include "Core/TableSubSystem.h"
#include "Common/TableData.h"
#include "TimerManager.h"
#include <Actor/InteractActor.h>
// For forwarding inventory messages
#include "Server/InventorySubSystem.h"
#include "Json.h"

void APlayerController_InGame::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log, TEXT("APlayerController_InGame::BeginPlay Class=%s NetMode=%d IsLocal=%d"), *GetClass()->GetName(), GetWorld() ? (int32)GetWorld()->GetNetMode() : -1, IsLocalController() ? 1 : 0);

	// 💡 GameMode_InGame의 로컬 전환 정책(SetForceLocalMoves/SetUseWebSocket)은 서버측 InventorySubSystem에만 적용된다.
	// 실제 UI 드래그/장착 로직은 클라이언트의 UInventorySubSystem을 사용하므로, 클라이언트측에서도
	// InGame 레벨에 들어오면 즉시 로컬전용 모드로 전환해야 장착/이동이 WebSocket/DB로 새는 것을 방지한다.
	if (IsLocalController())
	{
		if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
		{
			InvSub->SetUseWebSocket(false);
			InvSub->SetForceLocalMoves(true);
			UE_LOG(LogTemp, Log, TEXT("APlayerController_InGame::BeginPlay - Client InventorySubSystem forced to local-only mode."));
			InvSub->OnInventoryReceived.AddDynamic(this, &APlayerController_InGame::HandleInitialInventory);
		}
		GetWorldTimerManager().SetTimer(InitialInventoryTimer, this, &APlayerController_InGame::TryInitializeInventory, 0.2f, true);
		TryInitializeInventory();
	}
}

void APlayerController_InGame::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(InitialInventoryTimer);
	if (UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld()))
	{
		Subsystem->OnInventoryReceived.RemoveDynamic(this, &APlayerController_InGame::HandleInitialInventory);
	}
	Super::EndPlay(EndPlayReason);
}

void APlayerController_InGame::TryInitializeInventory()
{
	if (!IsLocalController() || !PlayerState || bInitialInventorySubmitted) return;
	UInventoryComponent* Inventory = PlayerState->FindComponentByClass<UInventoryComponent>();
	if (!Inventory) return;
	if (Inventory->HasInitialInventory())
	{
		GetWorldTimerManager().ClearTimer(InitialInventoryTimer);
		return;
	}
	UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld());
	if (!Subsystem) return;
	if (const FInventorySnapshot* Saved = Subsystem->GetTravelInventory())
	{
		SubmitInitialInventory(*Saved);
		return;
	}
	if (Subsystem->bHasCachedInventory)
	{
		HandleInitialInventory(Subsystem->CachedInventory);
		return;
	}
	UWebSocketSubSystem* WebSocket = UWebSocketSubSystem::Get(this);
	if (!bInitialInventoryRequested && WebSocket && WebSocket->IsConnected())
	{
		bInitialInventoryRequested = true;
		Subsystem->SetUseWebSocket(true);
		Subsystem->RequestGetInventory();
	}
}

void APlayerController_InGame::HandleInitialInventory(const FInventoryMapWrapper& Inventory)
{
	if (!IsLocalController() || !PlayerState || bInitialInventorySubmitted) return;
	UInventoryComponent* Component = PlayerState->FindComponentByClass<UInventoryComponent>();
	UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld());
	if (!Component || !Subsystem || Component->HasInitialInventory()) return;
	if (const FInventorySnapshot* Saved = Subsystem->GetTravelInventory())
	{
		SubmitInitialInventory(*Saved);
		return;
	}

	FInventorySnapshot Snapshot;
	Snapshot.bInitialized = true;
	Snapshot.PocketGuid = Inventory.PocketGuid;
	Snapshot.StashGuid = Inventory.StashGuid;
	auto FindContainer = [&Snapshot](const FGuid& Guid) -> FInventoryContainerSnapshot&
	{
		for (FInventoryContainerSnapshot& Container : Snapshot.Containers)
		{
			if (Container.Guid == Guid) return Container;
		}
		FInventoryContainerSnapshot& Container = Snapshot.Containers.AddDefaulted_GetRef();
		Container.Guid = Guid;
		return Container;
	};
	for (const auto& Pair : Inventory.InventorySizeMap) FindContainer(Pair.Key).Size = Pair.Value;
	for (const auto& Pair : Inventory.InventoryMap) FindContainer(Pair.Key).Items = Pair.Value.Items;
	const FGuid SlotGuids[] = { Inventory.MainWeapon, Inventory.SubWeapon, Inventory.HelMet, Inventory.Cloth, Inventory.Pants, Inventory.Shose, Inventory.BackPack, Inventory.Accuracy1, Inventory.Accuracy2 };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(SlotGuids); ++Index)
	{
		if (!SlotGuids[Index].IsValid()) continue;
		FInventoryContainerSnapshot& Container = FindContainer(SlotGuids[Index]);
		Container.EquipSlot = static_cast<EEquipSlot>(Index);
		Container.Size = FIntPoint(1, 1);
		if (Subsystem->bHasCachedEquip)
		{
			if (const FItemArrayWrapper* Equipment = Subsystem->CachedEquip.InventoryMap.Find(Container.Guid)) Container.Items = Equipment->Items;
		}
	}
	TArray<FItemInstance> AllItems;
	for (const FInventoryContainerSnapshot& Container : Snapshot.Containers) AllItems.Append(Container.Items);
	if (UTableSubSystem* Tables = UTableSubSystem::Get(GetWorld()))
	{
		for (const FItemInstance& Item : AllItems)
		{
			const FItemTableRow* Data = Component->GetItemData(Item.ItemID);
			if (!Data || Data->ItemType != EItemType::Bag) continue;
			if (const FItemBackpackTable* Bag = Tables->FindTableRow<FItemBackpackTable>(TEXT("BackpackTable"), Item.ItemID))
			{
				FindContainer(Item.GUID).Size = Bag->SlotSize;
			}
		}
	}
	SubmitInitialInventory(Snapshot);
}

void APlayerController_InGame::SubmitInitialInventory(const FInventorySnapshot& Snapshot)
{
	UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld());
	if (!IsLocalController() || !PlayerState || !Subsystem || bInitialInventorySubmitted) return;
	const FInventorySnapshot SnapshotToSend = Snapshot;
	bInitialInventorySubmitted = true;
	GetWorldTimerManager().ClearTimer(InitialInventoryTimer);
	Subsystem->SetUseWebSocket(false);
	Subsystem->SetForceLocalMoves(true);
	Subsystem->OnInventoryReceived.RemoveDynamic(this, &APlayerController_InGame::HandleInitialInventory);
	int32 ItemCount = 0;
	for (const FInventoryContainerSnapshot& Container : SnapshotToSend.Containers) ItemCount += Container.Items.Num();
	UE_LOG(LogTemp, Log, TEXT("[InventoryTravel] Submitting initial state: Source=%s Containers=%d Items=%d"), Subsystem->GetTravelInventory() ? TEXT("LobbySnapshot") : TEXT("WebSocketCache"), SnapshotToSend.Containers.Num(), ItemCount);
	Server_InitializeInventory(SnapshotToSend);
}

void APlayerController_InGame::Server_InitializeInventory_Implementation(const FInventorySnapshot& Snapshot)
{
	UInventoryComponent* Inventory = PlayerState ? PlayerState->FindComponentByClass<UInventoryComponent>() : nullptr;
	FString Reason;
	bool bSuccess = false;
	if (Inventory)
	{
		bSuccess = Inventory->HasInitialInventory() || Inventory->InitializeFromSnapshot(Snapshot, &Reason);
	}
	else
	{
		Reason = FString::Printf(TEXT("PlayerState has no InventoryComponent: %s"), *GetNameSafe(PlayerState));
	}
	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[InventoryTravel] Server initialized inventory for %s: Containers=%d"), *GetName(), Snapshot.Containers.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryTravel] Server rejected initial state for %s: %s"), *GetName(), *Reason);
	}
	Client_InitialInventoryResult(bSuccess, Reason);
}

void APlayerController_InGame::Client_InitialInventoryResult_Implementation(bool bSuccess, const FString& Reason)
{
	if (bSuccess)
	{
		if (UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld())) Subsystem->ClearTravelInventory();
		UE_LOG(LogTemp, Log, TEXT("[InventoryTravel] Server accepted lobby state; inventory is now server-managed."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[InventoryTravel] Initial inventory rejected: %s. Lobby snapshot retained; check server build and item tables."), *Reason);
	}
}

bool APlayerController_InGame::CanAccessInventory(const UInventoryComponent* Inventory) const
{
	if (!IsValid(Inventory) || Inventory->GetWorld() != GetWorld() || !Inventory->HasInitialInventory()) return false;
	if (PlayerState && Inventory == PlayerState->FindComponentByClass<UInventoryComponent>()) return true;
	const AInteractActor* Container = Cast<AInteractActor>(Inventory->GetOwner());
	if (!Container || Container->InventoryComp != Inventory || !GetPawn()) return false;
	const FVector Location = Container->GetComponentsBoundingBox().GetClosestPointTo(GetPawn()->GetActorLocation());
	if (FVector::DistSquared(Location, GetPawn()->GetActorLocation()) > FMath::Square(InventoryInteractionDistance)) return false;
	FVector Eyes;
	FRotator Rotation;
	GetPawn()->GetActorEyesViewPoint(Eyes, Rotation);
	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetPawn());
	return !GetWorld()->LineTraceSingleByChannel(Hit, Eyes, Location, ECC_Visibility, Params) || Hit.GetActor() == Container;
}

void APlayerController_InGame::Server_MoveInventoryItem_Implementation(UInventoryComponent* Source, UInventoryComponent* Target, FGuid ItemGuid, FGuid SourceGuid, FGuid TargetGuid, FIntPoint Position, bool bRotated)
{
	UInventoryComponent* PlayerInventory = PlayerState ? PlayerState->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!PlayerInventory || !PlayerInventory->HasInitialInventory() || !CanAccessInventory(Source) || !CanAccessInventory(Target))
	{
		Client_InventoryRequestResult(false);
		return;
	}
	const FItemInstance* Item = Source->FindItemByGuid(ItemGuid);
	if (!Item || Item->parent_inventory_guid != SourceGuid)
	{
		Client_InventoryRequestResult(false);
		return;
	}
	Client_InventoryRequestResult(Target->TransferItemFrom(Source, ItemGuid, TargetGuid, Position, bRotated));
}

void APlayerController_InGame::Client_InventoryRequestResult_Implementation(bool bSuccess)
{
	if (!bSuccess) UE_LOG(LogTemp, Warning, TEXT("Inventory request rejected by server; authoritative inventory retained."));
}

void APlayerController_InGame::Client_ReceiveInventoryJson_Implementation(const FString& MessageType, const FString& PayloadJson)
{
	UE_LOG(LogTemp, Log, TEXT("Client_ReceiveInventoryJson received Type=%s PayloadLen=%d"), *MessageType, PayloadJson.Len());

	// Forward to local subsystems (InventorySubSystem) for processing on client
	if (GetWorld())
	{
		if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(PayloadJson);
			if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
			{
				InvSub->HandleInventoryMessage(MessageType, Obj);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Client_ReceiveInventoryJson: Failed to parse payload JSON"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Client_ReceiveInventoryJson: InventorySubSystem not found"));
		}
	}
}

void APlayerController_InGame::InteractPressed()
{
	// simple trace to find nearby interact actors and call Interact
	FVector Loc;
	FRotator Rot;
	GetPlayerViewPoint(Loc, Rot);

	FVector Start = Loc;
	FVector End = Start + Rot.Vector() * 300.f;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(GetPawn());

	if (GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		if (IInteractable* IA = Cast<IInteractable>(Hit.GetActor()))
		{
			IA->Interact_Implementation(this);
		}
	}
}

void APlayerController_InGame::ToggleInventory()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubSystem* UIMgr = GI->GetSubsystem<UUIManagerSubSystem>())
		{
			UIMgr->ToggleUI(EUIType::Inventory);
		}
	}
	
}

// 새 기능: I 키로 캐릭터 UI 열기 (InGame 레벨에서 로비에서 받은 캐릭터 데이터만 표시)
void APlayerController_InGame::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::I, IE_Pressed, this, &APlayerController_InGame::OpenCharacterWidgetInGame);
		InputComponent->BindKey(EKeys::E, IE_Pressed, this, &APlayerController_InGame::InteractPressed);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &APlayerController_InGame::OnRotateKey);

		UE_LOG(LogTemp, Log, TEXT("APlayerController_InGame::SetupInputComponent bound keys. NetMode=%d IsLocal=%d"), GetWorld() ? (int32)GetWorld()->GetNetMode() : -1, IsLocalController() ? 1 : 0);
	}
}

void APlayerController_InGame::OpenCharacterWidgetInGame()
{
	UE_LOG(LogTemp, Log, TEXT("APlayerController_InGame::OpenCharacterWidgetInGame called. NetMode=%d IsLocal=%d"), GetWorld() ? (int32)GetWorld()->GetNetMode() : -1, IsLocalController() ? 1 : 0);

	if (!IsLocalController())
	{
		UE_LOG(LogTemp, Warning, TEXT("OpenCharacterWidgetInGame: called on non-local controller, ignoring."));
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubSystem* UIMgr = GI->GetSubsystem<UUIManagerSubSystem>())
		{
			UUserWidget* CharacterWidget = UIMgr->OpenUI(EUIType::Character);
			UE_LOG(LogTemp, Log, TEXT("OpenCharacterWidgetInGame: OpenUI returned widget=%s"), CharacterWidget ? TEXT("valid") : TEXT("null"));
			if (UInventoryWindow* Win = Cast<UInventoryWindow>(CharacterWidget))
			{
				// 로비에서 로드된 플레이어 데이터(Inventory/Equip)를 사용하여 위젯 초기화
				if (ACustomPlayerState* MyPS = GetPlayerState<ACustomPlayerState>())
				{
					UInventoryComponent* InvenComp = MyPS->GetComponentByClass<UInventoryComponent>();
					UEquipComponent* EquipComp = MyPS->GetComponentByClass<UEquipComponent>();
				// InGame에서는 Main 영역을 기본으로 표시하지 않도록 설정
					// InGame: initialize as player but do not show main inventory until Interact
					Win->InitForPlayer(InvenComp, EquipComp, false);
					UE_LOG(LogTemp, Log, TEXT("OpenCharacterWidgetInGame: InventoryWindow initialized (InvenComp=%s EquipComp=%s)"), InvenComp ? TEXT("ok") : TEXT("null"), EquipComp ? TEXT("ok") : TEXT("null"));
					// InGame에서는 Main inventory grid는 자동으로 열지 않음
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("OpenCharacterWidgetInGame: PlayerState is null or not ACustomPlayerState."));
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("OpenCharacterWidgetInGame: UUIManagerSubSystem not found on GameInstance."));
		}
	}
}

// (SetupInputComponent already implemented above with I and R bindings)

void APlayerController_InGame::OnRotateKey()
{
	if (UItemDragDropOperation* DragOp = Cast<UItemDragDropOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent()))
	{
		DragOp->RotateItem();
	}
}
