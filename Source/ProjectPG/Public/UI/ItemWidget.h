#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "Common/TableData.h"
#include "ItemWidget.generated.h"

UCLASS()
class PROJECTPG_API UItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float TileSize = 64.0f;

	// 마우스를 올려 두고 설명 창이 뜨기까지(초). 기획서 1초. WBP_ItemWidget 에서 고친다.
	UPROPERTY(EditAnywhere, Category = "Tooltip", meta = (ClampMin = "0"))
	float TooltipDelaySeconds = 1.0f;

	// 아이콘과 칸 테두리 사이 여백(픽셀). WBP_ItemWidget 에서 고친다.
	UPROPERTY(EditAnywhere, Category = "Icon", meta = (ClampMin = "0"))
	float IconPadding = 3.0f;

	UPROPERTY(BlueprintReadOnly)
	FItemInstance ItemInstance;

	// 💡 출처 인벤토리 GUID 저장 변수
	UPROPERTY(BlueprintReadOnly)
	FGuid OwnerInventoryGUID;

protected:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class USizeBox> RootSizeBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UImage> ItemIcon;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UTextBlock> TextStackCount;
	// 아이콘 비율 지키기(10/7): ItemIcon 을 감싼 ScaleBox(맞춰 줄이기). 캔버스 위에 있어서 돌린 아이템은 이걸 90° 돌린다.
	// WBP 에 없으면 예전처럼 아이콘을 칸 크기로 늘려 붙인다.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<class UScaleBox> IconScale;

	FItemTableRow CachedItemData;

	UPROPERTY()
	TObjectPtr<class UItemContextWidget> _ContextWidget;

public:
	// 💡 InInvenGUID 매개변수 추가
	UFUNCTION(BlueprintCallable)
	void InitWidget(const FItemInstance InItem, const FItemTableRow& InData, const FGuid& InInvenGUID, float InTileSize = 64.0f);

	void SetContextWidget(class UItemContextWidget* widget);

	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual bool NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;

	// (10/4 기획서 1.2.1) 마우스를 1초 올려 두면 설명 창, 떼면 닫기.
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;
	virtual void NativeDestruct() override;

	void RefreshWidget();
	const FItemTableRow* GetCachedItemData() const { return &CachedItemData; }

private:
	FTimerHandle TooltipTimer;
	void ShowTooltip();
	void HideTooltip();
};