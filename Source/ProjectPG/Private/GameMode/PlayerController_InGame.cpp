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
// 죽은 플레이어를 로비로 보낼 때 씀(파일 위 흐름: Source/ProjectPG/Private/GameMode/README.md)
#include "Characters/CustomCharacter.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/GameSession.h"
#include "GameMapsSettings.h"
#include "Misc/PackageName.h"
#include "GameMode/PGRaidStatsSubsystem.h"
#include "UI/PGDeathStatsWidget.h"

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
	
	FInputModeGameOnly InputMode;
	InputMode.SetConsumeCaptureMouseDown(true);
	SetInputMode(InputMode);

	// GetGameViewport()가 유효한지 안전 검사 후 진행
	if (GetWorld() && GetWorld()->GetGameViewport())
	{
		FViewport* Viewport = GetWorld()->GetGameViewport()->Viewport;
		if (Viewport)
		{
			Viewport->LockMouseToViewport(true); // 마우스를 화면에 완전히 가둡니다.
			Viewport->CaptureMouse(true);       // 마우스 입력을 엔진이 캡처합니다.
		}
	}
}

void APlayerController_InGame::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GetWorldTimerManager().ClearTimer(InitialInventoryTimer);
	GetWorldTimerManager().ClearTimer(DeathStatsTimer);
	GetWorldTimerManager().ClearTimer(ForceKickTimer);
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
			// I 키를 한 번 더 누르면 닫는다(열려 있을 때 = 화면에 붙어 있을 때). 마우스·입력 모드는 CloseUI 가 되돌린다.
			if (UUserWidget* OpenWidget = UIMgr->GetUI(EUIType::Character); OpenWidget && OpenWidget->IsInViewport())
			{
				UIMgr->CloseUI(EUIType::Character);
				return;
			}
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

// ── 죽으면 통계표 → [확인] → 그 사람만 로비로 ──────────────────────────────
// 흐름 그림: Source/ProjectPG/Private/GameMode/README.md

void APlayerController_InGame::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// 서버에서만 불린다. 플레이어 캐릭터가 죽으면 HandlePawnDied 가 불리게 걸고, 이번 판 기록을 시작한다.
	if (ACustomCharacter* MyCharacter = Cast<ACustomCharacter>(InPawn))
	{
		MyCharacter->OnDied.AddUniqueDynamic(this, &APlayerController_InGame::HandlePawnDied);
		if (UPGRaidStatsSubsystem* RaidStats = UPGRaidStatsSubsystem::Get(this)) RaidStats->StartLife(this);
	}
}

void APlayerController_InGame::OnUnPossess()
{
	if (ACustomCharacter* MyCharacter = Cast<ACustomCharacter>(GetPawn()))
	{
		MyCharacter->OnDied.RemoveDynamic(this, &APlayerController_InGame::HandlePawnDied);
	}
	Super::OnUnPossess();
}

void APlayerController_InGame::PawnLeavingGame()
{
	// 죽어서 로비로 나간 사람의 캐릭터는 지우지 않고 손만 뗀다 → 시체가 그 자리에 남는다.
	// 살아 있는데 나간 사람(창 닫음 등)은 원래대로 캐릭터를 지운다.
	const ACustomCharacter* MyCharacter = Cast<ACustomCharacter>(GetPawn());
	if (MyCharacter && MyCharacter->IsDead())
	{
		UnPossess();
		return;
	}
	Super::PawnLeavingGame();
}

void APlayerController_InGame::HandlePawnDied(ACustomCharacter* DeadCharacter)
{
	// 서버: 생존 시간을 멈추고, 죽는 동작을 볼 시간만큼 기다렸다가 통계표를 보낸다. 이미 기다리는 중이면 다시 걸지 않는다.
	if (!HasAuthority() || GetWorldTimerManager().IsTimerActive(DeathStatsTimer)) return;
	if (UPGRaidStatsSubsystem* RaidStats = UPGRaidStatsSubsystem::Get(this)) RaidStats->MarkDied(this);
	UE_LOG(LogTemp, Log, TEXT("[DeathToLobby] %s died. Showing stats in %.1fs."), *GetNameSafe(this), SecondsBeforeDeathStats);
	GetWorldTimerManager().SetTimer(DeathStatsTimer, this, &APlayerController_InGame::SendDeathStats, FMath::Max(SecondsBeforeDeathStats, 0.1f), false);
}

void APlayerController_InGame::SendDeathStats()
{
	UPGRaidStatsSubsystem* RaidStats = UPGRaidStatsSubsystem::Get(this);
	const FPGRaidStats Stats = RaidStats ? RaidStats->FinishLife(this) : FPGRaidStats();
	UE_LOG(LogTemp, Log, TEXT("[DeathToLobby] Sending stats to %s: time=%.0fs kills=%d shots=%d hits=%d dealt=%.0f taken=%.0f by=%s"),
		*GetNameSafe(this), Stats.SurvivalSeconds, Stats.Kills, Stats.ShotsFired, Stats.Hits, Stats.DamageDealt, Stats.DamageTaken, *Stats.KilledBy);
	Client_ShowDeathStats(Stats);
	// 확인을 끝내 안 누르면(자리 비움, 클라 멈춤) 일정 시간 뒤 서버가 내보낸다. 조용히 남아 있지 않게.
	GetWorldTimerManager().SetTimer(ForceKickTimer, this, &APlayerController_InGame::ForceKickIfStillHere, FMath::Max(SecondsBeforeForceKick, 1.f), false);
}

void APlayerController_InGame::ForceKickIfStillHere()
{
	UE_LOG(LogTemp, Error, TEXT("[DeathToLobby] %s did not leave %.1fs after the death stats were sent. Kicking from server."), *GetNameSafe(this), SecondsBeforeForceKick);
	AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr;
	if (GameMode && GameMode->GameSession)
	{
		// 쫓겨난 클라는 엔진이 알아서 게임 시작 맵(로비)으로 돌려보낸다.
		GameMode->GameSession->KickPlayer(this, FText::FromString(TEXT("Returned to lobby after death")));
	}
}

void APlayerController_InGame::ReleaseMouseForUI()
{
	SetShowMouseCursor(true);
	if (UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr)
	{
		if (FViewport* Viewport = ViewportClient->Viewport)
		{
			Viewport->LockMouseToViewport(false);
			Viewport->CaptureMouse(false);
		}
	}
}

void APlayerController_InGame::Client_ShowDeathStats_Implementation(const FPGRaidStats& Stats)
{
	ReleaseMouseForUI();
	if (DeathStatsWidget) return;  // 이미 떠 있음(같은 RPC 가 두 번 와도 창 하나)
	// 열려 있던 가방·캐릭터 창은 닫는다. 통계표 위에 겹쳐 [확인]을 가리지 않게.
	if (UUIManagerSubSystem* UIManager = UUIManagerSubSystem::Get(this)) UIManager->CloseAllUI();

	UClass* WidgetClass = DeathStatsWidgetClass.LoadSynchronous();
	DeathStatsWidget = WidgetClass ? CreateWidget<UPGDeathStatsWidget>(this, WidgetClass) : nullptr;
	if (!DeathStatsWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("[DeathToLobby] 통계표 WBP 를 못 만듦(DefaultGame.ini DeathStatsWidgetClass='%s'). 바로 로비로 감 - 패키지면 쿠킹 목록 확인"),
			*DeathStatsWidgetClass.ToString());
		ReturnToLobbyNow();
		return;
	}
	DeathStatsWidget->AddToViewport(2000);  // HUD·가방 창보다 위
	DeathStatsWidget->ShowStats(Stats);
	FInputModeUIOnly InputMode;
	InputMode.SetWidgetToFocus(DeathStatsWidget->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(InputMode);
	UE_LOG(LogTemp, Log, TEXT("[DeathToLobby] Death stats shown"));
}

void APlayerController_InGame::ReturnToLobbyNow()
{
	if (!IsLocalController()) return;
	ReleaseMouseForUI();
	SetInputMode(FInputModeUIOnly());
	// 로비 = 게임 시작 맵(DefaultEngine.ini GameDefaultMap). 맵 경로를 여기 따로 적지 않는다.
	const FString LobbyMap = FPackageName::ObjectPathToPackageName(UGameMapsSettings::GetGameDefaultMap());
	UE_LOG(LogTemp, Log, TEXT("[DeathToLobby] Leaving match, travelling to %s"), *LobbyMap);
	ClientTravel(LobbyMap, TRAVEL_Absolute);
}
