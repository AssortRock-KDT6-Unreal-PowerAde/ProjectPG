#include "UI/Plan/MatchingWidget.h"

#include "Components/Button.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

#include "Server/MatchmakingSubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include "TimerManager.h"

void UMatchingWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (UMatchmakingSubSystem* Match = UMatchmakingSubSystem::Get(GetWorld()))
	{
		Match->OnMatchStatusChanged.RemoveDynamic(this, &UMatchingWidget::HandleStatusChanged);
		Match->OnMatchStatusChanged.AddDynamic(this, &UMatchingWidget::HandleStatusChanged);
	}
	if (CancelButton)
	{
		CancelButton->OnClicked.RemoveDynamic(this, &UMatchingWidget::HandleCancelClicked);
		CancelButton->OnClicked.AddDynamic(this, &UMatchingWidget::HandleCancelClicked);
		CancelButton->SetIsEnabled(true);
	}
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(CloseTimer);

	const UWebSocketSubSystem* Socket = UWebSocketSubSystem::Get(GetWorld());
	SetStatus(Socket && Socket->IsConnected() ? TEXT("Start") : TEXT("NotConnected"));
}

void UMatchingWidget::NativeDestruct()
{
	if (UMatchmakingSubSystem* Match = UMatchmakingSubSystem::Get(GetWorld()))
		Match->OnMatchStatusChanged.RemoveDynamic(this, &UMatchingWidget::HandleStatusChanged);
	if (UWorld* World = GetWorld())
		World->GetTimerManager().ClearTimer(CloseTimer);
	Super::NativeDestruct();
}

// 글자와 막대를 바꾼다. 인원을 알면 막대를 그만큼 채우고, 모르면 왔다 갔다(기다리는 중 표시).
void UMatchingWidget::SetStatus(const FString& Key, int32 Current, int32 Target)
{
	const FText Text = StatusTexts.Contains(Key) ? StatusTexts[Key] : FText::FromString(Key);
	if (StatusText)
	{
		if (Current >= 0 && Target > 0)
		{
			FFormatNamedArguments Args;
			Args.Add(TEXT("Text"), Text);
			Args.Add(TEXT("Current"), Current);
			Args.Add(TEXT("Target"), Target);
			StatusText->SetText(FText::Format(CountFormat, Args));
		}
		else
		{
			StatusText->SetText(Text);
		}
	}
	if (MatchProgress)
	{
		const bool bKnown = Current >= 0 && Target > 0;
		MatchProgress->SetIsMarquee(!bKnown && (Key == TEXT("Start") || Key == TEXT("WAITING") || Key == TEXT("SERVER_STARTING")));
		MatchProgress->SetPercent(bKnown ? FMath::Clamp(static_cast<float>(Current) / Target, 0.0f, 1.0f)
			: (Key == TEXT("SERVER_STARTING") ? 1.0f : 0.0f));
	}
}

void UMatchingWidget::HandleStatusChanged(const FString& StatusType, const FString& Message)
{
	UE_LOG(LogTemp, Display, TEXT("[Matching] %s : %s"), *StatusType, *Message);
	int32 Current = -1, Target = -1;
	if (StatusType == TEXT("WAITING"))
	{
		// 형님 매칭이 "메시지 (지금/필요)" 모양으로 보낸다. 끝의 괄호에서 인원을 읽는다.
		int32 Open = INDEX_NONE;
		if (Message.FindLastChar(TEXT('('), Open))
		{
			FString Inside = Message.Mid(Open + 1);
			Inside.RemoveFromEnd(TEXT(")"));
			FString Left, Right;
			if (Inside.Split(TEXT("/"), &Left, &Right))
			{
				Left.TrimStartAndEndInline();
				Right.TrimStartAndEndInline();
				if (Left.IsNumeric() && Right.IsNumeric())
				{
					Current = FCString::Atoi(*Left);
					Target = FCString::Atoi(*Right);
				}
			}
		}
	}
	SetStatus(StatusType, Current, Target);

	// 서버를 여는 중이거나 취소 중에는 다시 취소할 수 없다.
	if (CancelButton)
		CancelButton->SetIsEnabled(StatusType == TEXT("WAITING"));
	if (StatusType == TEXT("Match_CANCELLED"))
		CloseAfter(CloseDelaySeconds);
}

void UMatchingWidget::HandleCancelClicked()
{
	const UWebSocketSubSystem* Socket = UWebSocketSubSystem::Get(GetWorld());
	if (!Socket || !Socket->IsConnected())
	{
		// 서버가 없으면 취소를 보낼 곳도 없다 → 바로 닫는다.
		RemoveFromParent();
		return;
	}
	if (UMatchmakingSubSystem* Match = UMatchmakingSubSystem::Get(GetWorld()))
		Match->RequestCancleMatch(); // 형님 코드가 "CancelMatch" 상태를 바로 알려 준다 → 글자가 바뀐다.
	if (CancelButton)
		CancelButton->SetIsEnabled(false);
	// 서버 답(Match_CANCELLED)이 안 오면 기다리다 닫는다.
	CloseAfter(CancelTimeoutSeconds);
}

void UMatchingWidget::CloseAfter(float Seconds)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	TWeakObjectPtr<UMatchingWidget> WeakThis(this);
	World->GetTimerManager().SetTimer(CloseTimer, FTimerDelegate::CreateLambda([WeakThis]()
	{
		if (WeakThis.IsValid())
			WeakThis->RemoveFromParent();
	}), FMath::Max(0.01f, Seconds), false);
}
