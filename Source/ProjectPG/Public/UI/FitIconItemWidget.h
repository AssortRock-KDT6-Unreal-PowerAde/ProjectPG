#pragma once

#include "CoreMinimal.h"
#include "UI/ItemWidget.h"
#include "FitIconItemWidget.generated.h"

class UScaleBox;
class UItemTooltipWidget;

// 아이템 칸 위젯(UItemWidget)의 자식: 아이콘을 칸에 늘려 붙이지 않고 그림 비율대로 맞춘다.
// 왜 자식인가: 부모(인벤토리 담당 코드)는 그대로 두고, 아이콘 보이는 방식만 덧붙이려고.
//   부모의 InitWidget 이 끝에 RefreshWidget 을 부르므로, 그걸 이어받아(override) 아이콘 배치만 더 한다.
// 쓰는 법: WBP_FitIconItemWidget(이 클래스가 부모, ItemIcon 을 IconCanvas > IconScale 로 감싼 WBP)를
//   WBP_InventoryGrid 의 Item Widget Class 로 고른다.
// 10/8 덧붙임(기획서 화면):
//   - 마우스를 1초 올려 두면 아이템 설명 창(WBP_ItemTooltip)을 띄운다. 부모에 없는 동작이라 여기서 더한다.
UCLASS()
class PROJECTPG_API UFitIconItemWidget : public UItemWidget
{
	GENERATED_BODY()

public:
	virtual void RefreshWidget() override;

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeDestruct() override;

	// 설명 창을 지금 바로 띄운다(1초 기다리지 않음). 자동 시험 도구도 이걸 부른다.
	void ShowTooltip();
	void HideTooltip();

protected:
	// 아이콘과 칸 테두리 사이 여백(픽셀). WBP 에서 고친다.
	UPROPERTY(EditAnywhere, Category = "Icon", meta = (ClampMin = "0"))
	float IconPadding = 3.0f;

	// ItemIcon 을 감싼 ScaleBox(맞춰 줄이기). 캔버스 위에 있어서 돌린 아이템은 이걸 90° 돌린다.
	// WBP 에 없으면 아무것도 안 한다(부모 동작 그대로).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> IconScale;

	// 마우스를 올리고 설명 창이 뜨기까지(초). 기획서 1초.
	UPROPERTY(EditAnywhere, Category = "Tooltip", meta = (ClampMin = "0"))
	float TooltipDelaySeconds = 1.0f;

	// 띄울 설명 창 WBP(WBP_ItemTooltip). 비어 있으면 설명 창이 안 뜬다.
	UPROPERTY(EditAnywhere, Category = "Tooltip")
	TSubclassOf<UItemTooltipWidget> TooltipClass;

private:
	FTimerHandle TooltipTimer;
};
