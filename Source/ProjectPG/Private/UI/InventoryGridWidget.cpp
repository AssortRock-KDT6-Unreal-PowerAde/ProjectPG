// Fill out your copyright notice in the Description page of Project Settings.

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/InventoryGridWidget.h"
#include "UI/ItemWidget.h"
#include "UI/SlotWidget.h"
#include "UI/ItemDragDropOperation.h"

#include "Core/ItemSubSystem.h"

#include "Components/UniformGridSlot.h"
#include "Components/InventoryComponent.h"
#include "Components/UniformGridPanel.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/SizeBox.h"
#include "GameFramework/PlayerState.h"

void UInventoryGridWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 이미 바인딩되어 있다면 중복 실행 방지
	if (IsValid(TargetInventoryComp)) return;

	// 위젯의 주체(Owning Player)로부터 PlayerState -> InventoryComponent를 찾아 스스로 바인딩
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (APlayerState* PS = PC->PlayerState)
		{
			if (UInventoryComponent* InvComp = PS->FindComponentByClass<UInventoryComponent>())
			{
				BindInventoryComponent(InvComp);
			}
		}
	}
}

void UInventoryGridWidget::CreateBackGroundGrid(int32 Columns, int32 Rows)
{

	if (!BackGroundGrid || !SlotWidgetClass || !InventorySizeBox)
	{
		UE_LOG(LogTemp, Error, TEXT("[UInventoryGridWidget] Required UI components or SlotWidgetClass are NULL!"));
		return;
	}

	BackGroundGrid->ClearChildren();
	BackGroundGrid->SetSlotPadding(FMargin(-1.0f));
	for (int32 r = 0; r < Rows; ++r)
	{
		for (int32 c = 0; c < Columns; ++c)
		{
			USlotWidget* SlotWidget = CreateWidget<USlotWidget>(this, SlotWidgetClass);

			// UniformGridPanel의 (r, c) 위치에 추가
			UUniformGridSlot* GridSlot = BackGroundGrid->AddChildToUniformGrid(SlotWidget, r, c);
			GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);

		}
	}

	// 전체 SizeBox 크기 고정 (TileSize가 64일 때 10열 15행 = 640x960)
	InventorySizeBox->SetWidthOverride(Columns * TileSize);
	InventorySizeBox->SetHeightOverride(Rows * TileSize);

	
}

void UInventoryGridWidget::BindInventoryComponent(UInventoryComponent* InComp)
{
	TargetInventoryComp = InComp;
	if (TargetInventoryComp)
	{
		// 1. 컴포넌트로부터 인벤토리 가로/세로 칸 수를 가져와 배경 그리드 생성
		int32 GridColumns = TargetInventoryComp->GetColumns(); // 컴포넌트에 구현된 가로 칸 수 getter
		int32 GridRows = TargetInventoryComp->GetRows();       // 컴포넌트에 구현된 세로 칸 수 getter

		CreateBackGroundGrid(GridColumns, GridRows);

		// 2. 델리게이트 바인딩 및 아이템 배치
		TargetInventoryComp->OnInventoryUpdated.AddDynamic(this, &UInventoryGridWidget::RefreshGridUI);
		RefreshGridUI();
	}
}

void UInventoryGridWidget::RefreshGridUI()
{
	if (!TargetInventoryComp || !ItemWidgetClass) return;

	// 기존 렌더링된 아이템 UI를 모두 지움
	ItemCanvas->ClearChildren();

	const TArray<FItemInstance>& Items = TargetInventoryComp->GetItems();

	for (const FItemInstance& Item : Items)
	{
		const FItemTableRow* Data = TargetInventoryComp->GetItemData(Item.ItemID);
		if (!Data) continue;

		// 아이템 위젯 스폰
		UItemWidget* NewItemWidget = CreateWidget<UItemWidget>(this, ItemWidgetClass);
		NewItemWidget->InitWidget(Item, *Data, TileSize);

		// CanvasPanel에 자식으로 등록
		UCanvasPanelSlot* CanvasSlot = ItemCanvas->AddChildToCanvas(NewItemWidget);
		CanvasSlot->SetAutoSize(true);

		// (X * TileSize, Y * TileSize) 위치에 절대 좌표 배치
		FVector2D DrawPos = FVector2D(Item.Position.X * TileSize, Item.Position.Y * TileSize);
		CanvasSlot->SetPosition(DrawPos);
	}
}

USlotWidget* UInventoryGridWidget::GetSlotWidgetAt(int32 TileX, int32 TileY)
{
	if (!BackGroundGrid || !TargetInventoryComp) return nullptr;

	int32 Columns = TargetInventoryComp->GetColumns();
	int32 Rows = TargetInventoryComp->GetRows();

	if (TileX < 0 || TileX >= Columns || TileY < 0 || TileY >= Rows)
	{
		return nullptr;
	}

	// GetChildrenCount 및 GetChildAt으로 순회
	const int32 ChildCount = BackGroundGrid->GetChildrenCount();
	for (int32 i = 0; i < ChildCount; ++i)
	{
		UWidget* Child = BackGroundGrid->GetChildAt(i);
		if (!Child) continue;

		if (UUniformGridSlot* GridSlot = Cast<UUniformGridSlot>(Child->Slot))
		{
			if (GridSlot->GetColumn() == TileX && GridSlot->GetRow() == TileY)
			{
				return Cast<USlotWidget>(Child);
			}
		}
	}

	return nullptr;
}
bool UInventoryGridWidget::CanPlaceItemAt(const FItemInstance& ItemToPlace, FIntPoint TargetTile, FIntPoint GridSize)
{

	if (nullptr == TargetInventoryComp) return false;

	float Columns = TargetInventoryComp->GetColumns();
	float Rows = TargetInventoryComp->GetRows();

	// 1. 인벤토리 경계(Bounds) 검사
	if (TargetTile.X < 0 || TargetTile.Y < 0) return false;
	if (TargetTile.X + GridSize.X > Columns) return false; // 열(가로) 초과
	if (TargetTile.Y + GridSize.Y > Rows) return false;    // 행(세로) 초과


	// 2. 해당 영역의 타일 충돌 검사
	for (int32 x = 0; x < GridSize.X; ++x)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			int32 CheckX = TargetTile.X + x;
			int32 CheckY = TargetTile.Y + y;

			// 이미 아이템이 차지하고 있는 슬롯인지 확인
			if (FItemInstance* ExistingItem = GetItemAtCell(CheckX, CheckY))
			{
				// 💡 중요한 예외 처리: 들고 있는 아이템 본인의 기존 위치라면 겹쳐도 배치 가능
				if (ExistingItem->GUID == ItemToPlace.GUID)
				{
					continue;
				}

				// 다른 아이템이 이미 배치되어 있다면 배치 불가
				return false;
			}
		}
	}

	// 모든 검사를 통과하면 배치 가능
	return true;
}


FItemInstance* UInventoryGridWidget::GetItemAtCell(int32 TileX, int32 TileY)
{
	if (nullptr == TargetInventoryComp) return nullptr;

	float Columns = TargetInventoryComp->GetColumns();
	float Rows = TargetInventoryComp->GetRows();

	UItemSubSystem* subSystem = UItemSubSystem::Get(GetWorld());
	if (nullptr == subSystem) return nullptr;


	// 1. 좌표 범위 검사
	if (TileX < 0 || TileX >= Columns || TileY < 0 || TileY >= Rows)
	{
		return nullptr;
	}
	TArray<FItemInstance> items = TargetInventoryComp->GetItems();


	// 2. 인벤토리에 배치된 아이템 목록 순회
	for (FItemInstance& Item : items)
	{
		// 아이템의 데이터 테이블(FItemTableRow) 데이터 가져오기

		const FItemTableRow* ItemData = subSystem->GetItem(Item.ItemID);
		if (!ItemData) continue;

		// 회전 상태가 반영된 현재 격자 크기 (FIntPoint: X=가로칸, Y=세로칸)
		FIntPoint GridSize = Item.GetCurrentGridSize(ItemData);

		// 아이템의 시작 위치 (Position.X, Position.Y)
		int32 StartX = Item.Position.X;
		int32 StartY = Item.Position.Y;
		int32 EndX = StartX + GridSize.X;
		int32 EndY = StartY + GridSize.Y;

		// 💡 검사할 (TileX, TileY) 좌표가 아이템이 점유한 영역 [StartX ~ EndX), [StartY ~ EndY) 내에 있는지 확인
		if (TileX >= StartX && TileX < EndX && TileY >= StartY && TileY < EndY)
		{
			return &Item; 
		}
	}

	return nullptr;
}
void UInventoryGridWidget::ClearSlotHighlights()
{
	for (USlotWidget* SlotWidget : HighlightedSlots)
	{
		if (SlotWidget)
		{
			SlotWidget->SetHighlightState(EBorderHighlightState::None);
		}
	}
	HighlightedSlots.Empty();
}



//////////////////////////////////

FReply UInventoryGridWidget::NativeOnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::R)
	{
		// 마우스에 붙어 있는 DragDropOp 가져오기
		if (UItemDragDropOperation* DragOp = Cast<UItemDragDropOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent()))
		{
			// 회전 상태 토글 및 Drag Visual UI 회전 적용
			DragOp->bCurrentRotated = !DragOp->bCurrentRotated;

			if (UItemWidget* DragVisual = Cast<UItemWidget>(DragOp->DefaultDragVisual))
			{
				DragVisual->ItemInstance.bIsRotated = DragOp->bCurrentRotated;
				// Visual UI 갱신 로직 실행
			}
			return FReply::Handled();
		}
	}
	return Super::NativeOnKeyDown(MyGeometry, InKeyEvent);
}


bool UInventoryGridWidget::NativeOnDrop(const FGeometry& MyGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	ClearSlotHighlights();
	UItemDragDropOperation* ItemDragOp = Cast<UItemDragDropOperation>(InOperation);
	if (!ItemDragOp || !TargetInventoryComp || !BackGroundGrid) return false;



	// 1. 전체 위젯 대신 BackGroundGrid의 Geometry 사용
	FGeometry GridGeometry = BackGroundGrid->GetCachedGeometry();

	// 2. AbsoluteToLocal 변환 (DPI 스케일 영향 최소화)
	FVector2D LocalMousePos = GridGeometry.AbsoluteToLocal(InDragDropEvent.GetScreenSpacePosition());

	// 3. 마우스 클릭 오프셋 감안한 좌표 계산
	FVector2D AdjustedPos = LocalMousePos - ItemDragOp->DragOffset;

	int32 TargetTileX = FMath::FloorToInt(AdjustedPos.X / TileSize);
	int32 TargetTileY = FMath::FloorToInt(AdjustedPos.Y / TileSize);

	UE_LOG(LogTemp, Warning, TEXT("[Drop Check] LocalMouse: %s | TargetTile: (%d, %d)"),
		*AdjustedPos.ToString(), TargetTileX, TargetTileY);

	return TargetInventoryComp->MoveItem(
		ItemDragOp->DraggedItem.GUID,
		FIntPoint(TargetTileX, TargetTileY),
		ItemDragOp->bCurrentRotated
	);
}
bool UInventoryGridWidget::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

	if (!TargetInventoryComp || !BackGroundGrid) return false;

	UItemDragDropOperation* ItemDragOp = Cast<UItemDragDropOperation>(InOperation);
	if (!ItemDragOp) return false;

	UItemWidget* DragVisual = Cast<UItemWidget>(ItemDragOp->DefaultDragVisual);
	if (!DragVisual) return false;

	// 💡 1. 로직 시작 시 이전 하이라이트 영역만 정확히 원래 색으로 끄기
	ClearSlotHighlights();

	// 2. 마우스 및 아이템 타일 위치 계산
	FGeometry GridGeometry = BackGroundGrid->GetCachedGeometry();
	FVector2D LocalMousePos = GridGeometry.AbsoluteToLocal(InDragDropEvent.GetScreenSpacePosition());

	const FItemTableRow* ItemData = DragVisual->GetCachedItemData();
	FIntPoint GridSize = DragVisual->ItemInstance.GetCurrentGridSize(ItemData);
	FIntPoint TargetTile = MouseToTilePosition(LocalMousePos, ItemDragOp->DragOffset);

	int32 Columns = TargetInventoryComp->GetColumns();
	int32 Rows = TargetInventoryComp->GetRows();

	// 3. 현재 위치에 놓을 수 있는지 검사
	bool bCanPlace = CanPlaceItemAt(ItemDragOp->DraggedItem, TargetTile, GridSize);
	EBorderHighlightState HighlightState = bCanPlace ? EBorderHighlightState::Valid : EBorderHighlightState::Invalid;

	// 4. 새로 놓일 영역의 슬롯들만 색상 켜기 & 배열에 등록
	for (int32 x = 0; x < GridSize.X; ++x)
	{
		for (int32 y = 0; y < GridSize.Y; ++y)
		{
			int32 CheckX = TargetTile.X + x;
			int32 CheckY = TargetTile.Y + y;

			if (CheckX >= 0 && CheckX < Columns && CheckY >= 0 && CheckY < Rows)
			{
				if (USlotWidget* SlotWidget = GetSlotWidgetAt(CheckX, CheckY))
				{
					SlotWidget->SetHighlightState(HighlightState);
					HighlightedSlots.Add(SlotWidget); // 다음 프레임에 지울 대상으로 저장
				}
			}
		}
	}

	return true;
}
//
void UInventoryGridWidget::NativeOnDragLeave(const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	Super::NativeOnDragLeave(InDragDropEvent, InOperation);

	// 마우스가 인벤토리 영역을 벗어나면 하이라이트 해제
	ClearSlotHighlights();
}


FIntPoint UInventoryGridWidget::MouseToTilePosition(const FVector2D& LocalMousePos, const FVector2D& DragOffset)
{
	FVector2D TopLeftPos = LocalMousePos - DragOffset;
	int32 TileX = FMath::FloorToInt(TopLeftPos.X / TileSize);
	int32 TileY = FMath::FloorToInt(TopLeftPos.Y / TileSize);

	return FIntPoint(TileX, TileY);
}
