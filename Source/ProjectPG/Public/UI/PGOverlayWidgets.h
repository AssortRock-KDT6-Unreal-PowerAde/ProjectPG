// 화면 맨 위에 겹쳐 뜨는 위젯 셋: 검은 로딩·시작 로고, 안내 한 줄, 카운트다운 한 줄. (2026-09-23 블루프린트 분리)
//
// 원래는 Slate(C++ 전용 UI)로 서브시스템이 직접 그렸다(UPGLoadingScreenSubsystem, UPGAnnounceSubsystem).
// Slate 는 WBP 의 부모가 될 수 없어서, 같은 모양의 UMG 부모 위젯으로 옮기고 WBP(WBP_PGLoadingScreen 등)가 모양을 맡게 했다.
// "언제 뜨고, 얼마나 흐려지고, 레벨이 바뀌면 다시 붙는가" 는 여전히 서브시스템이 한다 — 이 위젯들은 그리기만 한다.
// WBP 가 없으면 RebuildWidget 이 예전 Slate 와 같은 모양(엔진 기본 글꼴·같은 크기·같은 색·같은 자리)을 코드로 짓는다.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PGOverlayWidgets.generated.h"

class UBorder;
class UTextBlock;

// 검은 화면 두 가지: 시작 로고(가운데 PROJECT PG)와 필드 진입 로딩(오른쪽 아래 글자 + 도는 표시).
UCLASS()
class PROJECTPG_API UPGLoadingScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetLoadingText(const FText& Text);
	void ShowLoadingCorner(bool bShow);
	void ShowSplashLogo(bool bShow);
	void SetSplashOpacity(float Opacity);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;

	// ---- WBP 자리 (WBP_PGLoadingScreen) ----
	// 검은 바탕. 서브시스템은 위젯 전체 투명도로 페이드하므로 바탕 자체는 늘 불투명하게 둔다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> Backdrop;

	// 가운데 로고 묶음(처음엔 숨김, 로고 연출 때만 보인다).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SplashLogo;

	// 오른쪽 아래 "필드에 진입 중" + 도는 표시 묶음(처음엔 숨김).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LoadingCorner;

	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LoadingText;
};

// 화면에 한 줄 글자(안내·카운트다운 공용 부모). 세로 위치(TopFill)·크기·색만 자식이 다르다.
UCLASS(Abstract)
class PROJECTPG_API UPGScreenLineWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetLine(const FText& Text);
	// 글자(그림자 포함)만 흐리게 한다. 예전 Slate 도 글자 위젯에만 투명도를 걸었다.
	void SetLineOpacity(float Opacity);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;

	// 코드 기본 모양: 화면 위에서 TopFill 비율만큼 내려온 자리, 가로 가운데.
	float TopFill = 0.5f;
	int32 FontSize = 34;
	FLinearColor LineColor = FLinearColor::White;

	// ---- WBP 자리 ----
	UPROPERTY(BlueprintReadOnly, Category = "PG|Announce", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LineText;
};

// 안내 줄: 화면 위쪽 1/4 쯤, 흰색 34. 조준점(가운데)과 계기판(아래)을 가리지 않는 자리.
UCLASS()
class PROJECTPG_API UPGAnnounceLineWidget : public UPGScreenLineWidget
{
	GENERATED_BODY()

public:
	UPGAnnounceLineWidget(const FObjectInitializer& ObjectInitializer);
};

// 카운트다운 줄("탈출까지 3"): 가운데보다 조금 아래(조준점을 가리지 않게), 연두색 44.
UCLASS()
class PROJECTPG_API UPGCountdownLineWidget : public UPGScreenLineWidget
{
	GENERATED_BODY()

public:
	UPGCountdownLineWidget(const FObjectInitializer& ObjectInitializer);
};
