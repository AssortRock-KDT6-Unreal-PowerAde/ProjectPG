// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameDefine.h"

#include "SlotWidget.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API USlotWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UBorder> SlotBorder;
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<class UImage> BackGround;
public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "UI")
	FLinearColor DefaultSlotColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f); // 기본 슬롯 색상 (예: 어두운 회색, 불투명)


public:
public:
	UFUNCTION(BlueprintCallable)
	void SetSlotState(bool bIsHovered, bool bCanPlace);
	// 하이라이트 색상 적용 (EHighlightState: None, Valid, Invalid 등)
	void SetHighlightState(EBorderHighlightState InState);

private:

};
