#include "GameMode/GameMode_InGame.h"
#include "Server/InventorySubSystem.h"
#include "GameMode/PlayerController_InGame.h"
#include "GameMode/CustomPlayerState.h"
AGameMode_InGame::AGameMode_InGame()
{
	// Use the InGame player controller class by default
	PlayerControllerClass = APlayerController_InGame::StaticClass();
	PlayerStateClass = ACustomPlayerState::StaticClass();
}

void AGameMode_InGame::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	UE_LOG(LogTemp, Warning, TEXT("GameMode Chagne : InGame"));


	// 최초 데이터는 각 로컬 플레이어가 전달하고, 인게임 변경은 데디서버가 처리한다.
	if (UInventorySubSystem* InvSub = UInventorySubSystem::Get(GetWorld()))
	{
		InvSub->SetUseWebSocket(false);
		InvSub->SetForceLocalMoves(true);
	}
}
