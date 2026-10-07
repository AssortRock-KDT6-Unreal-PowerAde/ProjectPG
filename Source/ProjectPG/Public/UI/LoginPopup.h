// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LoginPopup.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ULoginPopup : public UUserWidget
{
	GENERATED_BODY()
private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UOverlay> LoginWindow;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> LoginBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> CreateIdViewBtn;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UEditableText> LoginEditText;
public:
	virtual void NativeConstruct() override;
	UFUNCTION(BlueprintCallable)
	void OnClickedLogin();
	UFUNCTION(BlueprintCallable)
	void OnClickedCreateIDView();

};
