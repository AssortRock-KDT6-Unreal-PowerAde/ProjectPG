#include "UI/MatchingWidget.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Core/UIManagerSubSystem.h"
#include "TimerManager.h"

void UMatchingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (USessionSubSystem* Session = USessionSubSystem::Get(this))
	{
		Session->OnMatchingStateChanged.RemoveDynamic(this, &UMatchingWidget::HandleStateChanged);
		Session->OnMatchingStateChanged.AddDynamic(this, &UMatchingWidget::HandleStateChanged);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.RemoveDynamic(this, &UMatchingWidget::HandleCancelClicked);
		CancelButton->OnClicked.AddDynamic(this, &UMatchingWidget::HandleCancelClicked);
		CancelButton->SetIsEnabled(true);
	}
	if (StatusText)
		StatusText->SetText(StateTexts.FindRef(EMatchingState::Searching));
	if (MatchProgress)
		MatchProgress->SetPercent(0.0f);
}

void UMatchingWidget::NativeDestruct()
{
	if (USessionSubSystem* Session = USessionSubSystem::Get(this))
		Session->OnMatchingStateChanged.RemoveDynamic(this, &UMatchingWidget::HandleStateChanged);
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(CloseTimer);
	Super::NativeDestruct();
}

// 진행 막대는 매 프레임 매칭 담당에게 물어본다(찾는 시간이 흐르는 만큼 찬다).
void UMatchingWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (MatchProgress)
		if (const USessionSubSystem* Session = USessionSubSystem::Get(this))
			MatchProgress->SetPercent(Session->GetProgress());
}

void UMatchingWidget::HandleStateChanged(EMatchingState State)
{
	if (StatusText)
		StatusText->SetText(StateTexts.FindRef(State));
	// 들어가거나 방을 여는 중에는 취소할 수 없다(이미 이동이 시작됨).
	if (CancelButton)
		CancelButton->SetIsEnabled(State == EMatchingState::Searching);
	if (State != EMatchingState::Searching && State != EMatchingState::Joining && State != EMatchingState::Hosting)
		CloseSoon();
}

void UMatchingWidget::HandleCancelClicked()
{
	if (USessionSubSystem* Session = USessionSubSystem::Get(this))
		Session->CancelMatching();
}

void UMatchingWidget::CloseSoon()
{
	if (UWorld* World = GetWorld())
	{
		TWeakObjectPtr<UMatchingWidget> WeakThis(this);
		World->GetTimerManager().SetTimer(CloseTimer, FTimerDelegate::CreateLambda([WeakThis]()
		{
			if (!WeakThis.IsValid())
				return;
			if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(WeakThis.Get()))
				UI->CloseUI(EUIType::Matching);
		}), FMath::Max(0.01f, CloseDelaySeconds), false);
	}
}
