// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "UIManagerSubSystem.generated.h"

/**
 * 
 */
UENUM(BlueprintType)
enum class EUIType : uint8
{
	None, Login, CreateUser,
	Character, Inventory, EquipMent, Quest,
};
UCLASS()
class PROJECTPG_API UUIManagerSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

private:
	// 생성된 위젯들을 관리하는 맵
	UPROPERTY()
	TMap<EUIType, UUserWidget*> ActiveWidgets;

	UPROPERTY()
	TMap<EUIType, TSubclassOf<UUserWidget>> UIClassMap;
public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "UIManager")
	class UUserWidget* ToggleUI(EUIType UIType);

	UFUNCTION(BlueprintCallable, Category = "UIManager")
	class UUserWidget* OpenUI(EUIType UIType);

	UFUNCTION(BlueprintCallable, Category = "UI Manager")
	void CloseUI(EUIType UIType);

	// 생성된 UI 가져오기
	UFUNCTION(BlueprintPure, Category = "UI Manager")
	UUserWidget* GetUI(EUIType UIType) const;

	// 모든 UI 닫기
	UFUNCTION(BlueprintCallable, Category = "UI Manager")
	void CloseAllUI();

	UFUNCTION(BlueprintCallable, Category = "UI Manager")
	void RegisterUIClass(EUIType UIType, TSubclassOf<UUserWidget> WidgetClass);

private:
	void UpdateInputMode();
};
