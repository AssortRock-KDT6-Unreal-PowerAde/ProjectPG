#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "IWebSocket.h"
#include "Templates/SharedPointer.h"
#include "Dom/JsonObject.h"
#include "WebSocketSubSystem.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(	FOnWebSocketMessageReceived,	const FString&,	TSharedPtr<FJsonObject>);
UCLASS()
class PROJECTPG_API UWebSocketSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UWebSocketSubSystem* Get(const UObject* worldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	void ConnectToLobbyServer();

	bool SendPayload(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);
	void SendJsonMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);

	UFUNCTION(BlueprintCallable, Category = "Lobby WebSocket")
	FString GetCurrentUserID() const { return CurrentUserId; }
	void SetCurrentUserID(const FString& NewId) { CurrentUserId = NewId; }

	// 2. 일반 C++ 델리게이트이므로 UPROPERTY를 제거합니다. (바인딩은 .cpp나 다른 클래스에서 AddUObject로 수행)
	FOnWebSocketMessageReceived OnWebSocketMessageReceived;
private:
	void OnConnected();
	void OnConnectionError(const FString& Error);
	void OnClosed(int32 StatusCode, const FString& Reason, bool bWasClean);
	void OnMessageReceived(const FString& MessageString);
	void HandleParsedMessage(const FString& Type, TSharedPtr<FJsonObject> PayloadObject);

	bool bHasAttemptConnection = false;
	TSharedPtr<IWebSocket> WebSocket;
	FString CurrentUserId = TEXT("");
};