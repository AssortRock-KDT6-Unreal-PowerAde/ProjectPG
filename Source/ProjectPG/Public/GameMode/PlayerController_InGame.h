#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Common/GameData.h"
#include "GameMode/PGRaidStatsSubsystem.h"
#include "PlayerController_InGame.generated.h"

class UInventoryComponent;
class ACustomCharacter;
class UPGDeathStatsWidget;

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

	// 죽은 플레이어의 PC 에서 실행: 마우스를 풀고 통계표(WBP_DeathStats)를 띄운다. [확인]을 누르면 ReturnToLobbyNow.
	// 통계표 WBP 를 못 찾으면 Error 로그를 남기고 곧바로 로비로 간다(죽은 화면에 갇히지 않게).
	UFUNCTION(Client, Reliable)
	void Client_ShowDeathStats(const FPGRaidStats& Stats);

	// 내 PC 에서만: 로비 맵(게임 시작 맵)으로 혼자 나간다. 통계표의 [확인] 버튼이 부른다.
	// 서버는 이 사람 연결이 끊긴 것으로 보고 정리하고, 다른 사람 게임은 그대로 이어진다.
	void ReturnToLobbyNow();

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

	// 죽고 나서 통계표가 뜨기까지 기다리는 초(죽는 동작을 볼 시간).
	// 게임모드가 이 C++ 클래스를 바로 쓰므로 DefaultGame.ini [/Script/ProjectPG.PlayerController_InGame] 에서 바꾼다.
	UPROPERTY(EditDefaultsOnly, Config, Category = "Death")
	float SecondsBeforeDeathStats = 3.f;

	// 통계표를 보낸 뒤 이만큼 지나도 연결이 남아 있으면(확인을 안 누르고 자리를 비움, 클라가 멈춤) 서버가 내보낸다.
	UPROPERTY(EditDefaultsOnly, Config, Category = "Death")
	float SecondsBeforeForceKick = 300.f;

	// 통계표 모양(WBP). DefaultGame.ini 에서 정하고, WBP 폴더는 쿠킹 목록(DirectoriesToAlwaysCook)에 넣어 패키지에 담는다.
	UPROPERTY(Config)
	TSoftClassPtr<UPGDeathStatsWidget> DeathStatsWidgetClass;

	UPROPERTY(Transient)
	TObjectPtr<UPGDeathStatsWidget> DeathStatsWidget;

	FTimerHandle DeathStatsTimer;
	FTimerHandle ForceKickTimer;

	UFUNCTION()
	void HandlePawnDied(ACustomCharacter* DeadCharacter);
	void SendDeathStats();
	void ForceKickIfStillHere();
	// 게임 중 가둬 둔 마우스를 풀고 커서를 보인다(통계표·로비 화면을 누를 수 있게).
	void ReleaseMouseForUI();

	void TryInitializeInventory();
	void SubmitInitialInventory(const FInventorySnapshot& Snapshot);
	bool CanAccessInventory(const UInventoryComponent* Inventory) const;

	UFUNCTION()
	void HandleInitialInventory(const FInventoryMapWrapper& Inventory);
};
