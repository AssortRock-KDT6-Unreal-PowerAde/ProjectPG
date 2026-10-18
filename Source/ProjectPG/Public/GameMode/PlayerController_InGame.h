#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Common/GameData.h"
#include "PlayerController_InGame.generated.h"

class UInventoryComponent;
class ACustomCharacter;

UCLASS()
class PROJECTPG_API APlayerController_InGame : public APlayerController
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetupInputComponent() override;
	void ToggleInventory();
	void OnRotateKey();
	void OpenCharacterWidgetInGame();
	void InteractPressed();

	UFUNCTION(Server, Reliable)
	void Server_InitializeInventory(const FInventorySnapshot& Snapshot);

	UFUNCTION(Client, Reliable)
	void Client_InitialInventoryResult(bool bSuccess, const FString& Reason);

	UFUNCTION(Server, Reliable)
	void Server_MoveInventoryItem(UInventoryComponent* Source, UInventoryComponent* Target, FGuid ItemGuid, FGuid SourceGuid, FGuid TargetGuid, FIntPoint Position, bool bRotated);

	UFUNCTION(Client, Reliable)
	void Client_InventoryRequestResult(bool bSuccess);

	// Server->Client RPC to forward inventory/websocket messages received by dedicated server
	UFUNCTION(Client, Reliable)
	void Client_ReceiveInventoryJson(const FString& MessageType, const FString& PayloadJson);

	// 죽은 플레이어의 PC 에서 실행: 마우스를 풀고 로비 맵(게임 시작 맵)으로 혼자 나간다.
	// 서버는 이 사람 연결이 끊긴 것으로 보고 정리하고, 다른 사람 게임은 그대로 이어진다.
	UFUNCTION(Client, Reliable)
	void Client_ReturnToLobby();

protected:
	// 서버: 캐릭터를 잡을 때 "죽으면 알려 줘"를 걸고, 놓을 때 푼다.
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;
	// 서버: 플레이어가 나갈 때 캐릭터를 지우는데, 죽은 캐릭터면 시체로 남긴다(남은 사람 눈에 보이게).
	virtual void PawnLeavingGame() override;

private:
	FTimerHandle InitialInventoryTimer;
	bool bInitialInventorySubmitted = false;
	bool bInitialInventoryRequested = false;

	UPROPERTY(EditDefaultsOnly, Category = "Inventory")
	float InventoryInteractionDistance = 400.f;

	// 죽고 나서 로비로 나가기까지 기다리는 초(죽는 동작을 볼 시간).
	// 게임모드가 이 C++ 클래스를 바로 쓰므로 DefaultGame.ini [/Script/ProjectPG.PlayerController_InGame] 에서 바꾼다.
	UPROPERTY(EditDefaultsOnly, Config, Category = "Death")
	float SecondsBeforeReturnToLobby = 5.f;

	// 로비로 가라고 보낸 뒤 이만큼 지나도 연결이 남아 있으면 서버가 강제로 내보낸다(조용히 남아 있지 않게).
	UPROPERTY(EditDefaultsOnly, Config, Category = "Death")
	float SecondsBeforeForceKick = 15.f;

	FTimerHandle ReturnToLobbyTimer;
	FTimerHandle ForceKickTimer;

	UFUNCTION()
	void HandlePawnDied(ACustomCharacter* DeadCharacter);
	void SendDeadPlayerToLobby();
	void ForceKickIfStillHere();

	void TryInitializeInventory();
	void SubmitInitialInventory(const FInventorySnapshot& Snapshot);
	bool CanAccessInventory(const UInventoryComponent* Inventory) const;

	UFUNCTION()
	void HandleInitialInventory(const FInventoryMapWrapper& Inventory);
};
