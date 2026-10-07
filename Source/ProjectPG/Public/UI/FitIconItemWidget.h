#pragma once

#include "CoreMinimal.h"
#include "UI/ItemWidget.h"
#include "FitIconItemWidget.generated.h"

class UScaleBox;

// 아이템 칸 위젯(UItemWidget)의 자식: 아이콘을 칸에 늘려 붙이지 않고 그림 비율대로 맞춘다.
// 왜 자식인가: 부모(인벤토리 담당 코드)는 그대로 두고, 아이콘 보이는 방식만 덧붙이려고.
//   부모의 InitWidget 이 끝에 RefreshWidget 을 부르므로, 그걸 이어받아(override) 아이콘 배치만 더 한다.
// 쓰는 법: WBP_FitIconItemWidget(이 클래스가 부모, ItemIcon 을 IconCanvas > IconScale 로 감싼 WBP)를
//   WBP_InventoryGrid 의 Item Widget Class 로 고른다.
UCLASS()
class PROJECTPG_API UFitIconItemWidget : public UItemWidget
{
	GENERATED_BODY()

public:
	virtual void RefreshWidget() override;

protected:
	// 아이콘과 칸 테두리 사이 여백(픽셀). WBP 에서 고친다.
	UPROPERTY(EditAnywhere, Category = "Icon", meta = (ClampMin = "0"))
	float IconPadding = 3.0f;

	// ItemIcon 을 감싼 ScaleBox(맞춰 줄이기). 캔버스 위에 있어서 돌린 아이템은 이걸 90° 돌린다.
	// WBP 에 없으면 아무것도 안 한다(부모 동작 그대로).
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UScaleBox> IconScale;
};
