// 캐릭터·인벤토리 창에 기획서 검색·종류 고르기를 더한 자식(기획서 1.2.1).

#pragma once

#include "CoreMinimal.h"
#include "UI/InventoryWindow.h"
#include "Common/GameDefine.h"
#include "PlanInventoryWindow.generated.h"

class UComboBoxString;
class UEditableTextBox;
class UItemWidget;

// 종류 고르기 목록 한 줄: 보이는 글자 + 거를 아이템 종류.
// 글자와 종류를 짝으로 둔다 — 목록 순서를 아이템 종류(EItemType) 순서에 기대면, 종류가 하나 끼어들 때 조용히 어긋난다.
USTRUCT(BlueprintType)
struct FPlanFilterOption
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Search")
	FText Label;

	// 켜면 모든 종류를 보여 준다("전체"). 끄면 아래 Type 만.
	UPROPERTY(EditAnywhere, Category = "Search")
	bool bAllTypes = false;

	UPROPERTY(EditAnywhere, Category = "Search", meta = (EditCondition = "!bAllTypes"))
	EItemType Type = EItemType::ETC;
};

// 캐릭터·인벤토리 창 자식.
// 게임에서: 창고 위에 "아이템 이름 검색" 칸과 종류 고르기(전체·무기·방어구·소비·퀘스트·가방·기타)가 있다.
//   글자를 치거나 종류를 고르면 맞지 않는 아이템이 흐려진다(아이템은 그 자리에 그대로 — 옮기거나 지우지 않는다).
// 왜 자식인가: 형님 창(UInventoryWindow)의 인벤토리 연결·서버 동기화는 그대로 두고, 보이는 투명도만 바꾸려고.
//   격자(형님 UInventoryGridWidget)는 인벤토리가 바뀌면 아이템 칸을 새로 그려 투명도가 1 로 돌아간다.
//   그래서 창이 들고 있는 인벤토리(내 것·열어 둔 상자)의 "바뀜" 알림을 듣고, 다음 프레임에 한 번만 다시 맞춘다.
// 칸 이름(WBP_CharacterWidget): SearchBox, FilterCombo. 없으면 그 기능만 빠진다.
UCLASS()
class PROJECTPG_API UPlanInventoryWindow : public UInventoryWindow
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UEditableTextBox> SearchBox;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UComboBoxString> FilterCombo;

	// 종류 고르기 목록(글자와 종류 짝). WBP_CharacterWidget 에서 고친다.
	UPROPERTY(EditAnywhere, Category = "Search")
	TArray<FPlanFilterOption> FilterOptions = {
		{ INVTEXT("전체"), true, EItemType::ETC },
		{ INVTEXT("무기"), false, EItemType::Weapon }, { INVTEXT("방어구"), false, EItemType::Armor },
		{ INVTEXT("소비"), false, EItemType::Consumable }, { INVTEXT("퀘스트"), false, EItemType::Quest },
		{ INVTEXT("가방"), false, EItemType::Bag }, { INVTEXT("기타"), false, EItemType::ETC } };

	// 검색에 안 맞는 아이템을 얼마나 흐리게(0 = 안 보임, 1 = 그대로).
	UPROPERTY(EditAnywhere, Category = "Search", meta = (ClampMin = "0", ClampMax = "1"))
	float FilteredOutOpacity = 0.2f;

private:
	UFUNCTION() void HandleSearchChanged(const FText& Text);
	UFUNCTION() void HandleFilterChanged(FString SelectedItem, ESelectInfo::Type SelectionType);

	// 인벤토리 "바뀜" 알림 → 다음 프레임에 다시 맞추라고 표시만 한다(격자가 먼저 새로 그리게).
	UFUNCTION() void HandleInventoryUpdated();
	// 창이 들고 있는 인벤토리가 바뀌었으면(상자를 열었거나 닫음) 알림을 다시 건다.
	void WatchInventories();

	// 창 안(가방·창고·주머니 격자 포함)의 아이템 칸을 모두 찾아 투명도를 맞춘다.
	void ApplyFilter();
	bool Matches(const UItemWidget* Item) const;
	bool IsFilterActive() const;

	FString SearchText;
	int32 FilterIndex = 0; // FilterOptions 의 몇 번째
	bool bFilterWasActive = false;
	bool bReapplyPending = false;
	TWeakObjectPtr<UInventoryComponent> WatchedPlayerInventory;
	TWeakObjectPtr<UInventoryComponent> WatchedContainerInventory;
};
