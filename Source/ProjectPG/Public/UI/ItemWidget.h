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

	UPROPERTY(BlueprintReadOnly)
	FItemInstance ItemInstance;

	// 💡 출처 인벤토리 GUID 저장 변수
	UPROPERTY(BlueprintReadOnly)
	FGuid OwnerInventoryGUID;

protected:
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class USizeBox> RootSizeBox;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UImage> ItemIcon;
	UPROPERTY(meta = (BindWidget)) TObjectPtr<class UTextBlock> TextStackCount;

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

	void RefreshWidget();
	const FItemTableRow* GetCachedItemData() const { return &CachedItemData; }
};