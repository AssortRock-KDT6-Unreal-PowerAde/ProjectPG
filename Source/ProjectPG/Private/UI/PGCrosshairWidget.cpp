#include "UI/PGCrosshairWidget.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Flow/PGFlowSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Objects/PGWigBeamComponent.h"
#include "Rendering/DrawElements.h"
#include "Robot/PGRobotCharacter.h"

UPGCrosshairWidget* UPGCrosshairWidget::Create(APlayerController* Owner)
{
	if (!Owner)
		return nullptr;
	// 이미 있으면 그걸 쓴다. 캐릭터가 새로 생기면(시작 지점에 다시 세우기 등) 또 만들어 두 개가 겹쳐 떠 있었다(9/23 로그 "created" 두 번).
	TArray<UUserWidget*> Existing;
	UWidgetBlueprintLibrary::GetAllWidgetsOfClass(Owner, Existing, UPGCrosshairWidget::StaticClass(), false);
	for (UUserWidget* Found : Existing)
		if (Found && Found->GetOwningPlayer() == Owner)
			return Cast<UPGCrosshairWidget>(Found);
	UPGCrosshairWidget* Widget = CreateWidget<UPGCrosshairWidget>(Owner,
		UPGFlowSettings::ResolveWidgetClass(UPGFlowSettings::Get().CrosshairClass, UPGCrosshairWidget::StaticClass()));
	UE_LOG(LogTemp, Display, TEXT("PGCrosshair: created (%s)"), Widget ? *Widget->GetClass()->GetName() : TEXT("null"));
	return Widget;
}

void UPGCrosshairWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 마우스를 가로채지 않는다(조준점 위에서 클릭해도 게임으로 간다).
	SetVisibility(ESlateVisibility::HitTestInvisible);
	// 고리는 매 프레임 차오르므로 늘 다시 그린다. Slate 는 바뀐 게 없는 위젯의 그림을 재사용해서,
	// 처음(가발 없을 때) 그린 그림이 그대로 남아 고리가 영영 안 보였다(9/23 시험). 작은 위젯이라 비용은 거의 없다.
	ForceVolatile(true);
}

void UPGCrosshairWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	const APlayerController* PC = GetOwningPlayer();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	// 걸어 다니는 내 캐릭터를 카메라가 볼 때만 보인다. 차·로봇에 탔거나(조종 대상이 바뀐다) 전함 조종석에 앉으면(카메라가 배로 간다) 숨긴다.
	const bool bOnFoot = IsValid(Pawn) && Pawn->IsA<ACharacter>() && !Pawn->IsA<APGRobotCharacter>() && PC->GetViewTarget() == Pawn;
	if (bOnFoot != bShown)
	{
		bShown = bOnFoot;
		// 접기(Collapsed)를 쓰면 Tick 이 멈춰 다시 못 켠다 — 투명도로 숨긴다.
		SetRenderOpacity(bShown ? 1.0f : 0.0f);
	}
	if (CachedPawn.Get() != Pawn)
	{
		CachedPawn = Pawn;
		CachedBeam = IsValid(Pawn) ? Pawn->FindComponentByClass<UPGWigBeamComponent>() : nullptr;
		WornCheckTimer = 0.0f; // 폰이 바뀌면 바로 다시 묻는다
	}
	const UPGWigBeamComponent* Beam = CachedBeam.Get();
	// "가발을 썼나" 는 가방을 전부 뒤져야 안다(HasItem). 매 프레임 물을 일이 아니다 — 0.25초면 쓰자마자 뜬 것처럼 보인다.
	//   차오르는 정도(GetReadyFraction)는 뺄셈 하나라 매 프레임 읽는다(고리가 부드럽게 차야 한다).
	bool bNowWorn = bWigWorn;
	WornCheckTimer -= InDeltaTime;
	if (WornCheckTimer <= 0.0f)
	{
		WornCheckTimer = 0.25f;
		bNowWorn = Beam && Beam->IsWigWorn();
	}
	ReadyFraction = Beam ? Beam->GetReadyFraction() : 1.0f;
	const bool bNowReady = ReadyFraction >= 1.0f;
	if (bNowWorn != bWigWorn || bNowReady != bWigReady)
	{
		UE_LOG(LogTemp, Display, TEXT("PGCrosshair: wig %s, beam %s (%.0f%%)"), bNowWorn ? TEXT("worn") : TEXT("none"),
			bNowReady ? TEXT("ready") : TEXT("charging"), ReadyFraction * 100.0f);
		bWigWorn = bNowWorn;
		bWigReady = bNowReady;
		OnWigStateChanged(bWigWorn, bWigReady);
	}
	// WigRing 은 숨기지 않는다. 숨긴(Hidden) 위젯은 자리 계산이 안 돼 고리가 화면 왼쪽 위(0,0)에 그려졌다(9/23 시험).
	// 가발이 없으면 NativePaint 가 고리를 안 그리므로 빈 상자일 뿐이다.
}

int32 UPGCrosshairWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 Layer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	if (!bShown)
		return Layer;
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	// WBP 에 점이 없으면(C++ 기본) 가운데에 작은 십자를 그린다.
	if (!IsValid(Dot))
	{
		const FVector2D C = Size * 0.5f;
		const TArray<FVector2D> H = { C + FVector2D(-7.0f, 0.0f), C + FVector2D(7.0f, 0.0f) };
		const TArray<FVector2D> V = { C + FVector2D(0.0f, -7.0f), C + FVector2D(0.0f, 7.0f) };
		FSlateDrawElement::MakeLines(OutDrawElements, ++Layer, AllottedGeometry.ToPaintGeometry(), H, ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.85f), true, 2.0f);
		FSlateDrawElement::MakeLines(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(), V, ESlateDrawEffect::None, FLinearColor(1, 1, 1, 0.85f), true, 2.0f);
	}
	if (!bWigWorn)
		return Layer;
	// 고리 자리: WigRing 위젯의 가운데·크기(WBP 가 정한다). 없으면 화면 가운데 RingRadius.
	FVector2D Centre = Size * 0.5f;
	float Radius = RingRadius;
	// 자리는 "그리는 좌표(paint space)" 로 읽어야 한다. GetCachedGeometry(틱 좌표)로 읽었더니 창 위치만큼 어긋나
	// 고리가 화면 오른쪽 바깥(2582, 1045 — 화면은 1922x1081)에 그려져 안 보였다(9/23 진단 로그).
	if (IsValid(WigRing) && WigRing->GetCachedWidget().IsValid() && WigRing->GetPaintSpaceGeometry().GetLocalSize().GetMin() > 2.0f)
	{
		const FGeometry& RingGeometry = WigRing->GetPaintSpaceGeometry();
		Centre = AllottedGeometry.AbsoluteToLocal(RingGeometry.GetAbsolutePositionAtCoordinates(FVector2D(0.5f, 0.5f)));
		const FVector2D RingSize = RingGeometry.GetLocalSize();
		if (RingSize.GetMin() > 2.0f)
			Radius = RingSize.GetMin() * 0.5f;
	}
	constexpr int32 Segments = 48;
	auto Arc = [&](float From01, float To01, const FLinearColor& Color, int32 DrawLayer)
	{
		TArray<FVector2D> Points;
		const int32 Count = FMath::Max(2, FMath::CeilToInt(Segments * (To01 - From01)) + 1);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// 12시에서 시작해 시계 방향으로.
			const float T = FMath::Lerp(From01, To01, static_cast<float>(Index) / (Count - 1));
			const float Angle = T * 2.0f * PI - PI * 0.5f;
			Points.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}
		FSlateDrawElement::MakeLines(OutDrawElements, DrawLayer, AllottedGeometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Color, true, RingThickness);
	};
	Arc(0.0f, 1.0f, RingTrackColor, ++Layer);
	if (ReadyFraction > 0.001f)
		Arc(0.0f, FMath::Clamp(ReadyFraction, 0.0f, 1.0f), bWigReady ? RingReadyColor : RingChargingColor, ++Layer);
	return Layer;
}
