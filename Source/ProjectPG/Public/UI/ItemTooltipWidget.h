// 아이템 설명 창(기획서 1.2.1: 마우스를 1초 올려 두면 이름·설명·능력치).

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Common/GameData.h"
#include "ItemTooltipWidget.generated.h"

class UTextBlock;
struct FItemTableRow;

// 아이템 설명 창.
// 게임에서: 인벤토리의 아이템 위에 마우스를 1초 올려 두면 옆에 뜬다(이름, 설명, 종류·장비 칸, 크기·개수). 마우스를 떼면 사라진다.
// 누가 띄우나: 아이템 칸(UItemWidget)이 1초 타이머 뒤에 ShowFor 를 부르고, 마우스가 나가면 Hide.
// 화면 관리는 형님 UI 관리자(UIManagerSubSystem, EUIType::ItemTooltip)에게 맡긴다 — 창은 하나만 만들어 두고 내용만 바꾼다.
// C++ 은 내용 채우기만. 모양(판·글꼴·색)은 WBP_ItemTooltip. 글자 칸 이름이 같으면 붙는다.
UCLASS()
class PROJECTPG_API UItemTooltipWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 이 아이템 설명을 마우스 옆에 띄운다.
	static void ShowFor(const UObject* WorldContext, const FItemInstance& Item, const FItemTableRow& Data);
	static void Hide(const UObject* WorldContext);

	// 칸 채우기. 표에 설명이 비어 있으면 종류로 대신 쓴다.
	void SetItem(const FItemInstance& Item, const FItemTableRow& Data);

protected:
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> NameText;
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> DescriptionText;
	// 왼쪽 "머리 방어" 자리(기획서) — 지금은 종류·장비 칸.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatNameText;
	// 오른쪽 "+10" 자리 — 지금은 크기·개수.
	UPROPERTY(meta = (BindWidgetOptional)) TObjectPtr<UTextBlock> StatValueText;
};
