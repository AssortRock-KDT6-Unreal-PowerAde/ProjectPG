// 캐릭터·인벤토리 창에 기획서 검색·종류 고르기를 더한 자식(기획서 1.2.1).

#pragma once

#include "CoreMinimal.h"
#include "UI/InventoryWindow.h"
#include "PlanInventoryWindow.generated.h"

class UComboBoxString;
class UEditableTextBox;
class UItemWidget;

// 캐릭터·인벤토리 창 자식.
// 게임에서: 창고 위에 "아이템 이름 검색" 칸과 종류 고르기(전체·무기·방어구·소비·퀘스트·가방·기타)가 있다.
//   글자를 치거나 종류를 고르면 맞지 않는 아이템이 흐려진다(아이템은 그 자리에 그대로 — 옮기거나 지우지 않는다).
// 왜 자식인가: 형님 창(UInventoryWindow)의 인벤토리 연결·서버 동기화는 그대로 두고, 보이는 투명도만 바꾸려고.
//   격자(형님 UInventoryGridWidget)가 아이템 칸을 새로 그려도 0.2초마다 다시 맞춘다.
// 칸 이름(WBP_CharacterWidget): SearchBox, FilterCombo. 없으면 그 기능만 빠진다.
UCLASS()
class PROJECTPG_API UPlanInventoryWindow : public UInventoryWindow
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> SearchBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> FilterCombo;

	// 종류 고르기 목록 글자. 순서: 전체 → 무기·방어구·소비·퀘스트·가방·기타(아이템 종류 순서와 같게).
	UPROPERTY(EditAnywhere, Category = "Search")
	TArray<FText> FilterLabels = { INVTEXT("전체"), INVTEXT("무기"), INVTEXT("방어구"), INVTEXT("소비"),
		INVTEXT("퀘스트"), INVTEXT("가방"), INVTEXT("기타") };

	// 검색에 안 맞는 아이템을 얼마나 흐리게(0 = 안 보임, 1 = 그대로).
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0", ClampMax = "1"))
	float FilteredOutOpacity = 0.2f;

private:
	UFUNCTION() void HandleSearchChanged(const FText& Text);
	UFUNCTION() void HandleFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// 창 안(가방·창고·주머니 격자 포함)의 아이템 칸을 모두 찾아 투명도를 맞춘다.
	void ApplyFilter();
	bool Matches(const UItemWidget* Item) const;

	FString SearchText;
	int32 FilterIndex = 0; // 0 = 전체, 1.. = EItemType + 1
	bool bFilterWasActive = false;
	float ReapplyTimer = 0.0f;
};
