#include "GameMode/PlayerController_InGame.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/InventoryWindow.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameMode/CustomPlayerState.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Components/EquipComponent.h"
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
		}
	}
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
			IA->Interact_Implementation(Hit.GetActor());
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
