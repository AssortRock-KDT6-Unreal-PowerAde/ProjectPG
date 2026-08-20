// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MessagePopupWidget.generated.h"

/**
 * 
 */
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

public:
	virtual void NativeConstruct() override;

	UFUNCTION(BlueprintCallable)
	void SetMessageText(const FString& Message,int32 num =0);
	UFUNCTION(BlueprintCallable)
	void OnClickeOkbutton();
	UFUNCTION(BlueprintCallable)
	void OnLifetimeExpired();
};
