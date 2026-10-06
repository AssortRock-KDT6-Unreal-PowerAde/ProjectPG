// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Controller/LobbyUIFlowController.h"
#include "Core/UIManagerSubSystem.h"
#include "Server/InventorySubSystem.h"
#include <Kismet/GameplayStatics.h>
#include "Camera/CameraActor.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"

ULobbyUIFlowController* ULobbyUIFlowController::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;

	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;

	return inst->GetSubsystem<ULobbyUIFlowController>();
}

void ULobbyUIFlowController::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void ULobbyUIFlowController::Deinitialize()
{
	Super::Deinitialize();
}

// 로비에 들어왔을 때: 시작 짐을 넣고(처음 한 번만) 로비 메뉴를 띄운다.
void ULobbyUIFlowController::BeginSetting()
{
	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvSub->LoadStarterInventory(GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr);
	}
	SpawnLobbyCharacter();
	ShowLobby();
}

void ULobbyUIFlowController::SpawnLobbyCharacter()
{
	UWorld* World = GetWorld();
	if (!World || LobbyCharacter.IsValid())
		return;
	AActor* Spot = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CharacterSpotTag))
		{
			Spot = *It;
			break;
		}
	}
	const UClass* GameModeClass = CharacterSourceGameMode.TryLoadClass<AGameModeBase>();
	const AGameModeBase* GameModeDefaults = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
	UClass* PawnClass = GameModeDefaults ? GameModeDefaults->DefaultPawnClass.Get() : nullptr;
	if (!Spot || !PawnClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] no lobby character: spot=%d pawn=%s"), Spot ? 1 : 0, PawnClass ? *PawnClass->GetName() : TEXT("none"));
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Pawn = World->SpawnActor<APawn>(PawnClass, Spot->GetActorTransform(), Params);
	if (!Pawn)
		return;
	// 조종하는 사람은 없지만 AI 조종기를 붙여야 이동 부품이 돌아 바닥에 내려서고 서 있는 동작이 나온다(레벨에 놓았을 때와 같게).
	Pawn->SpawnDefaultController();
	LobbyCharacter = Pawn;
	UE_LOG(LogTemp, Display, TEXT("[Lobby] lobby character %s at %s"), *PawnClass->GetName(), *Spot->GetActorLocation().ToCompactString());
}

void ULobbyUIFlowController::FocusCamera(FName CameraTag, float BlendSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
		return;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CameraTag))
		{
			PC->SetViewTargetWithBlend(*It, BlendSeconds, VTBlend_EaseInOut, 2.0f);
			return;
		}
	}
}

void ULobbyUIFlowController::ShowLobby()
{
	UUIManagerSubSystem* UIsubSystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(UIsubSystem)) return;

	UIsubSystem->CloseAllUI();
	UIsubSystem->OpenUI(EUIType::Lobby);
}
