// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()
private:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> CharacterBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> GameStartBtn;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> OptionBtn;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UButton> ExitBtn;
public:
	virtual void NativeConstruct() override;

public:
	UFUNCTION()
	void OnClickedCharacterButton();

	UFUNCTION()
	void OnClickedGameStartButton();

	UFUNCTION()
	void OnClickedOptionButton();

	UFUNCTION()
	void OnClickedExitButton();
};
