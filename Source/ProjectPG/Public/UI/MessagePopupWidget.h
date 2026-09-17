// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MessagePopupWidget.generated.h"

UENUM(BlueprintType)
enum class EMessageBoxType : uint8
{
	None = 0,
	OneButton = 1,
	ServerClose = 2,
	ServerWating = 3,
	Timer
};
UCLASS()
class PROJECTPG_API UMessagePopupWidget : public UUserWidget
{
	GENERATED_BODY()

private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UTextBlock> MessageText;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> OkButton;

	FTimerHandle DestroyTimerHandle;
	EMessageBoxType MessageType = EMessageBoxType::None;

	float LifeTime = 3.0f;
public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable)
	void SetMessageText(const FString& Message,int32 num =0);
	
	UFUNCTION(BlueprintCallable)
	void SetMessageWebsocket(const FString& StatusType, const FString& Message);
	
	UFUNCTION(BlueprintCallable)
	void SetMessageType(EMessageBoxType type) { MessageType = type; }

	
	UFUNCTION(BlueprintCallable)
	void OnClickeOkbutton();
	
	UFUNCTION(BlueprintCallable)
	void OnLifetimeExpired();



	void SetExipireTimer(float Time) { LifeTime = Time; }


};
