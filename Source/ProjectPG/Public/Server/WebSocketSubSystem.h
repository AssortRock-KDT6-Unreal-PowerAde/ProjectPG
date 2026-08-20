#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "Common/GameData.h" // FItemArrayWrapper 정의 포함 헤더

#include "WebSocketSubSystem.generated.h"

USTRUCT(BlueprintType)
struct FInventoryMapWrapper
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FGuid, FItemArrayWrapper> InventoryMap;
};

// 💡 USTRUCT 이름을 델리게이트 매개변수로 전달
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnLoginStatusChanged, bool, bIsLoggedIn, bool, bInventoryLoaded, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchStatusChanged, const FString&, StatusType, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCreateIDStatusChanged, bool, bSuccess, const FString&, UserId, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryReceived, FInventoryMapWrapper, ItemsWrapper);

UCLASS()
class PROJECTPG_API UWebSocketSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
private:
	bool bHasAttemptConnection = false;
	TSharedPtr<IWebSocket> WebSocket;
	FString CurrentUserId = TEXT("");

public:
	UPROPERTY(BlueprintAssignable, Category = "Lobby WebSocket|Events")
	FOnLoginStatusChanged OnLoginStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Lobby WebSocket|Events")
	FOnMatchStatusChanged OnMatchStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Lobby WebSocket|Events")
	FOnCreateIDStatusChanged OnCreateIDStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "WebSocket|Events")
	FOnInventoryReceived OnInventoryReceived;

public:
	static UWebSocketSubSystem* Get(const UObject* worldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void ConnectToLobbyServer();

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void RequestLogin(const FString& UserID);

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void RequestGameStart();

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void RequestCancleMatch();

	UFUNCTION(BlueprintCallable, Category = "Lobby WebSocket")
	void RequestCreateID(const FString& UserId);

	UFUNCTION(BlueprintCallable, Category = "Lobby WebSocket")
	void RequestGetInventory();

	UFUNCTION(BlueprintCallable, Category = "Lobby WebSocket")
	FString GetCurrentUserID() { return CurrentUserId; }

private:
	void OnConnected();
	void OnConnectionError(const FString& Error);
	void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnMessageReceived(const FString& MessageString);

	void HandleParsedMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);

	void SendJsonMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);
};