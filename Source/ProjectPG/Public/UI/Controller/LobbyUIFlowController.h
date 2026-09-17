// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LobbyUIFlowController.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ULobbyUIFlowController : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static ULobbyUIFlowController* Get(const UObject* worldContext);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	UFUNCTION()
	void BeginSetting();//해당 로비씬으로 왔을시 최초 호출
	
	UFUNCTION()
	void RequestIDCreate(FString UserID);
	UFUNCTION()
	void CancleIDCreateWindow();
	UFUNCTION()
	void SuccedCreateIDpopup();
	UFUNCTION()
	void RequestLogin(FString UserID);

	UFUNCTION()
	void HandleCreateIDStatus(bool bSuccess, const FString& UserID, const FString& Message);
	UFUNCTION()
	void SucceedLogin(bool bIsLogedIn, bool bDataLoaded, const FString& Messsage);
	UFUNCTION()
	void DataLoadPopup();

	UFUNCTION()
	void ShowLobby();

};
