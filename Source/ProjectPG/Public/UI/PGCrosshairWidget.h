// 화면 가운데 조준점 + 분홍 가발 광선 고리. (2026-09-23)
//
// 왜 하나인가: 총(UPGWeaponComponent)과 가발 광선(UPGWigBeamComponent)은 둘 다 "화면 가운데(카메라가 보는 곳)" 를 겨눈다.
//   그래서 가운데 점 하나로 둘 다 맞는다. 가발이 있으면 점 둘레에 고리를 그린다 — 쏜 뒤 다시 쏠 수 있을 때까지 차오른다.
// 무엇이 WBP 인가: 점 모양·크기·색, 고리 자리(WigRing 크기·위치)와 색·굵기는 WBP_PGCrosshair(설정 ProjectPG Flow > Screens > Crosshair Class).
//   코드는 "가발이 있나 / 얼마나 찼나" 만 읽어 고리의 몫을 그린다(원호는 UMG 기본 위젯에 없어서 NativePaint 로 그린다).
// 언제 숨기나: 카메라가 내 캐릭터가 아닐 때(차·로봇·전함 조종석) — 거기엔 각자 조준선이 따로 있다.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PGCrosshairWidget.generated.h"

class APlayerController;

UCLASS()
class PROJECTPG_API UPGCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 설정의 WBP(없으면 이 C++ 클래스 — 가운데 점과 고리를 코드로 그린다)로 만든다.
	static UPGCrosshairWidget* Create(APlayerController* Owner);

	// 가운데 점(모양은 WBP). 없으면 코드가 작은 점을 그린다.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> Dot;

	// 가발 고리가 그려질 자리. 이 위젯의 가운데에, 크기(짧은 변)의 절반을 반지름으로 그린다. 없으면 화면 가운데 RingRadius.
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UWidget> WigRing;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Crosshair")
	FLinearColor RingReadyColor = FLinearColor(1.0f, 0.35f, 0.72f, 0.95f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Crosshair")
	FLinearColor RingChargingColor = FLinearColor(1.0f, 0.35f, 0.72f, 0.55f);

	// 고리 전체(아직 안 찬 부분) 바탕.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Crosshair")
	FLinearColor RingTrackColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Crosshair", meta = (ClampMin = "2.0"))
	float RingRadius = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Crosshair", meta = (ClampMin = "0.5"))
	float RingThickness = 2.5f;

	// WBP 가 따로 꾸밀 수 있게(예: 다 차면 반짝임). 가발을 얻거나 잃을 때, 다시 쏠 수 있게 될 때 부른다.
	UFUNCTION(BlueprintImplementableEvent, Category = "PG|Crosshair")
	void OnWigStateChanged(bool bHasWig, bool bBeamReady);

	UFUNCTION(BlueprintCallable, Category = "PG|Crosshair")
	bool IsWigWorn() const { return bWigWorn; }

	// 0 = 방금 쐈다, 1 = 다시 쏠 수 있다.
	UFUNCTION(BlueprintCallable, Category = "PG|Crosshair")
	float GetWigReadyFraction() const { return ReadyFraction; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	bool bWigWorn = false;
	bool bWigReady = true;
	float ReadyFraction = 1.0f;
	bool bShown = true;

	// 매 프레임 부품을 찾지 않게 기억해 둔다. 조종하는 폰이 바뀌면(차에 탔다 내림) 다시 찾는다.
	TWeakObjectPtr<APawn> CachedPawn;
	TWeakObjectPtr<class UPGWigBeamComponent> CachedBeam;
	// 가발을 썼나는 0.25초마다만 묻는다(남은 시간).
	float WornCheckTimer = 0.0f;
};
