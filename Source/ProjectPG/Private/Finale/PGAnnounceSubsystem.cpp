#include "Finale/PGAnnounceSubsystem.h"
#include "Common/PGSoundRouter.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Flow/PGFlowSettings.h"
#include "Objects/PGObjectTypes.h"
#include "UI/PGOverlayWidgets.h"

UPGAnnounceSubsystem* UPGAnnounceSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	return World ? World->GetSubsystem<UPGAnnounceSubsystem>() : nullptr;
}

bool UPGAnnounceSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPGAnnounceSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPGAnnounceSubsystem, STATGROUP_Tickables);
}

void UPGAnnounceSubsystem::Announce(const TArray<FText>& Lines)
{
	// 같은 문구가 지금 떠 있거나(Queue[0]) 기다리는 중이면 또 넣지 않는다.
	// 왜(9/28 사용자 PIE: "변신 중 글자 왜 계속 나와? 3번 나온 것 같은데"): 여고생이 변신하는 2초 동안 F 를 세 번 누르면
	//   "변신 중" 이 세 번 줄을 서서 하나씩 차례로 떴다 — 이미 차를 타고 날고 있을 때까지 계속 보였다.
	for (const FText& Line : Lines)
		if (!Line.IsEmpty() && !Queue.ContainsByPredicate([&Line](const FText& Waiting) { return Waiting.EqualTo(Line); }))
			Queue.Add(Line);
	UE_LOG(LogPGObjects, Display, TEXT("PGAnnounce: %d line(s) queued (%d waiting)"), Lines.Num(), Queue.Num());
	if (Lines.Num() > 0)
		PGSound::PlayLocal(this, FName(TEXT("UI_Announce")), nullptr, FVector::ZeroVector); // 내 화면 안내 — 나만
}

void UPGAnnounceSubsystem::SetCountdown(const FText& Line)
{
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (Line.IsEmpty())
	{
		if (CountdownSlate.IsValid() && Viewport)
			Viewport->RemoveViewportWidgetContent(CountdownSlate.ToSharedRef());
		CountdownSlate.Reset();
		CountdownWidget = nullptr;
		return;
	}
	if (!Viewport)
		return;
	if (!CountdownWidget)
	{
		// 화면 가운데보다 조금 아래(조준점을 가리지 않게), 안내 줄보다 크게.
		CountdownWidget = CreateLine(UPGFlowSettings::Get().CountdownLineClass, UPGCountdownLineWidget::StaticClass());
		if (!CountdownWidget)
			return;
		CountdownSlate = CountdownWidget->TakeWidget();
		Viewport->AddViewportWidgetContent(CountdownSlate.ToSharedRef(), 51);
	}
	CountdownWidget->SetLine(Line);
}

UPGScreenLineWidget* UPGAnnounceSubsystem::CreateLine(const TSoftClassPtr<UUserWidget>& Designed, UClass* Fallback)
{
	// 설정(ProjectPG Flow > Screens)에 WBP 가 있으면 그것, 없으면 코드 기본 모양(예전 Slate 와 같은 자리·크기·색).
	UPGScreenLineWidget* Created = CreateWidget<UPGScreenLineWidget>(GetWorld(), UPGFlowSettings::ResolveWidgetClass(Designed, Fallback));
	if (Created)
		UE_LOG(LogPGObjects, Display, TEXT("PGAnnounce: widget %s"), *Created->GetClass()->GetName());
	return Created;
}

void UPGAnnounceSubsystem::EnsureWidget()
{
	if (LineWidget)
		return;
	UWorld* World = GetWorld();
	UGameViewportClient* Viewport = World ? World->GetGameViewport() : nullptr;
	if (!Viewport)
		return; // 화면이 없다(전용 서버·헤드리스 테스트)
	// 화면 위쪽 1/4 쯤, 가로 가운데. 조준점(가운데)과 계기판(아래)을 가리지 않는 자리.
	LineWidget = CreateLine(UPGFlowSettings::Get().AnnounceLineClass, UPGAnnounceLineWidget::StaticClass());
	if (!LineWidget)
		return;
	LineSlate = LineWidget->TakeWidget();
	Viewport->AddViewportWidgetContent(LineSlate.ToSharedRef(), 50);
}

void UPGAnnounceSubsystem::RemoveWidget()
{
	if (!LineSlate.IsValid())
		return;
	if (UWorld* World = GetWorld())
		if (UGameViewportClient* Viewport = World->GetGameViewport())
			Viewport->RemoveViewportWidgetContent(LineSlate.ToSharedRef());
	LineSlate.Reset();
	LineWidget = nullptr;
}

void UPGAnnounceSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Queue.IsEmpty())
		return;
	UWorld* World = GetWorld();
	if (!World)
		return;
	EnsureWidget();
	const double Now = World->GetTimeSeconds();
	if (CurrentStart < 0.0)
	{
		CurrentStart = Now;
		if (LineWidget)
		{
			LineWidget->SetLine(Queue[0]);
			LineWidget->SetLineOpacity(0.0f); // 새 줄은 완전히 투명하게 시작
		}
		UE_LOG(LogPGObjects, Display, TEXT("PGAnnounce: showing \"%s\""), *Queue[0].ToString());
	}
	const float Age = static_cast<float>(Now - CurrentStart);
	float Alpha = 0.0f;
	if (Age < FadeInSeconds)
		Alpha = Age / FMath::Max(FadeInSeconds, 0.01f);
	else if (Age < FadeInSeconds + HoldSeconds)
		Alpha = 1.0f;
	else if (Age < FadeInSeconds + HoldSeconds + FadeOutSeconds)
		Alpha = 1.0f - (Age - FadeInSeconds - HoldSeconds) / FMath::Max(FadeOutSeconds, 0.01f);
	// 글자 색 대신 위젯 전체 투명도로 흐리게 한다. 예전에는 글자 색만 투명하게 해서 그림자(검정 85%)가 첫 프레임부터 진하게 떠
	// "갑자기 튀어나온다" 로 보였다(사용자 9/22). 전체 투명도는 글자와 그림자를 같이 흐린다.
	// 부드러운 곡선(SmoothStep): 직선으로 올리면 시작과 끝이 딱 끊겨 보인다.
	const float Smooth = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(Alpha, 0.0f, 1.0f));
	if (LineWidget)
		LineWidget->SetLineOpacity(Smooth);
	if (Age >= FadeInSeconds + HoldSeconds + FadeOutSeconds + GapSeconds)
	{
		Queue.RemoveAt(0);
		CurrentStart = -1.0;
		if (Queue.IsEmpty())
			RemoveWidget(); // 다 보여 주면 화면에서 뗀다 — 빈 위젯이 남아 있을 이유가 없다
	}
}

void UPGAnnounceSubsystem::Deinitialize()
{
	RemoveWidget();
	SetCountdown(FText::GetEmpty());
	Queue.Reset();
	Super::Deinitialize();
}
