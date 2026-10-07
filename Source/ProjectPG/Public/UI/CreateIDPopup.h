// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CreateIDPopup.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UCreateIDPopup : public UUserWidget
{
	GENERATED_BODY()
private:

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UOverlay> CreateIDWindow;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> CreateIDBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> CanacleBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UEditableText> CreateEditText;
public:
	virtual void NativeConstruct() override;
	UFUNCTION(BlueprintCallable)
	void OnClickedCreateID();
	UFUNCTION(BlueprintCallable)
	void OnClickedCancle();
};
