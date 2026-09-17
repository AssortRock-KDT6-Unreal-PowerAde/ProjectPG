// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/SlotWidget.h"
#include "Components/Border.h"
#include "Components/Image.h"

void USlotWidget::SetSlotState(bool bIsHovered, bool bCanPlace)
{
	if (!SlotBorder) return;

	if (bIsHovered)
	{
		// 마우스가 올라왔을 때: 배치 가능하면 초록색, 불가능하면 빨간색 반투명
		FLinearColor Color = bCanPlace ? FLinearColor(0.f, 1.f, 0.f, 0.3f) : FLinearColor(1.f, 0.f, 0.f, 0.3f);
		SlotBorder->SetBrushColor(Color);
	}
	else
	{
		// 기본 상태: 어두운 기본 배경색
		SlotBorder->SetBrushColor(FLinearColor(0.05f, 0.05f, 0.05f, 0.8f));
	}
}

void USlotWidget::SetHighlightState(EBorderHighlightState NewState)
{
    if (!BackGround) return;

    FLinearColor TargetColor = DefaultSlotColor; // 기본값은 슬롯의 원래 색상

    switch (NewState)
    {
    case EBorderHighlightState::None:
        TargetColor = DefaultSlotColor; // 원래 슬롯 색상으로 복구
        break;
    case EBorderHighlightState::Valid:
        TargetColor = FLinearColor(0.0f, 1.0f, 0.0f, 0.4f); // 초록색 하이라이트
        break;
    case EBorderHighlightState::Invalid:
        TargetColor = FLinearColor(1.0f, 0.0f, 0.0f, 0.4f); // 빨간색 하이라이트
        break;
    }

    BackGround->SetBrushTintColor(FSlateColor(TargetColor));
}