#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "AuthSubSystem.generated.h"

// Auth 전용 델리게이트 분배
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnLoginStatusChanged, bool, bIsLoggedIn, bool, bInventoryLoaded, const FString&, Message);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCreateIDStatusChanged, bool, bSuccess, const FString&, UserId, const FString&, Message);

UCLASS()
class PROJECTPG_API UAuthSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UAuthSubSystem* Get(UWorld* World);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void HandleAuthMessage(const FString& MessageType, TSharedPtr<FJsonObject> PayloadObject);

	UFUNCTION(BlueprintCallable, Category = "Auth")
	void RequestLogin(const FString& UserID);

	UFUNCTION(BlueprintCallable, Category = "Auth")
	void RequestCreateID(const FString& UserId);

	// Auth 관련 델리게이트 배치
	UPROPERTY(BlueprintAssignable, Category = "Auth|Events")
	FOnLoginStatusChanged OnLoginStatusChanged;

	UPROPERTY(BlueprintAssignable, Category = "Auth|Events")
	FOnCreateIDStatusChanged OnCreateIDStatusChanged;
};