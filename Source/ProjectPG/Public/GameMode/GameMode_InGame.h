#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Common/GameData.h"
#include "GameMode_InGame.generated.h"

class UInventorySubSystem;

UCLASS()
class PROJECTPG_API AGameMode_InGame : public AGameModeBase
{
	GENERATED_BODY()

public:
	AGameMode_InGame();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

protected:
	// Callback when inventory data received first time from server
	UFUNCTION()
	void OnInventoryReceivedOnce(const struct FInventoryMapWrapper& InventoryMap);

private:
	// Flag to ensure we only switch to local mode once
	bool bHasInitializedInventory = false;

	// WebSocket이 실제로 연결될 때까지 폴링한 뒤 인벤토리를 요청한다.
	FTimerHandle InventoryRequestRetryHandle;
	void TryRequestInventoryWhenConnected();
};
