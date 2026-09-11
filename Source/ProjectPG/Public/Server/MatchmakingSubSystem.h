#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Dom/JsonObject.h"
#include "MatchmakingSubSystem.generated.h"

// Matchmaking 전용 델리게이트 분배
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMatchStatusChanged, const FString&, StatusType, const FString&, Message);

UCLASS()
class PROJECTPG_API UMatchmakingSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UMatchmakingSubSystem* Get(UWorld* World);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	void HandleMatchMessage(const FString& MessageType, TSharedPtr<FJsonObject> PayloadObject);

	UFUNCTION(BlueprintCallable, Category = "Matchmaking")
	void RequestGameStart();

	UFUNCTION(BlueprintCallable, Category = "Matchmaking")
	void RequestCancleMatch();

	// Matchmaking 관련 델리게이트 배치
	UPROPERTY(BlueprintAssignable, Category = "Matchmaking|Events")
	FOnMatchStatusChanged OnMatchStatusChanged;
};