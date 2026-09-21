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

	// 실제 소켓 연결 여부. Connect()는 비동기이므로 InitGame 등 이른 시점에서는
	// false일 수 있다. 연결 완료 후 요청을 보내려면 OnSocketConnected를 사용할 것.
	UFUNCTION(BlueprintCallable, Category = "WebSocket_Lobby")
	bool IsConnected() const { return WebSocket.IsValid() && WebSocket->IsConnected(); }

	// 소켓이 실제로 연결되었을 때 브로드캐스트. 연결 이전에 요청을 보내야 하는 코드는
	// 이 델리게이트에 바인딩한 뒤 연결 완료 시 요청을 보낸다.
	DECLARE_MULTICAST_DELEGATE(FOnSocketConnected);
	FOnSocketConnected OnSocketConnected;

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