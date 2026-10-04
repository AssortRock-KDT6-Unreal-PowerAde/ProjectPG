#include "Server/SessionSubSystem.h"

#include "OnlineSubsystem.h"
#include "OnlineSubsystemUtils.h"
#include "OnlineSessionSettings.h"
#include "Online/OnlineSessionNames.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

namespace PGSession
{
	// 우리 게임 방만 고르기 위한 표시(같은 네트워크의 다른 언리얼 게임 방은 건너뛴다).
	const FName GameKey(TEXT("PGGAME"));
	const FString GameValue(TEXT("ProjectPG"));
}

USessionSubSystem* USessionSubSystem::Get(const UObject* WorldContext)
{
	const UGameInstance* GameInstance = UGameplayStatics::GetGameInstance(WorldContext);
	return GameInstance ? GameInstance->GetSubsystem<USessionSubSystem>() : nullptr;
}

IOnlineSessionPtr USessionSubSystem::GetSessionInterface() const
{
	return Online::GetSessionInterface(GetWorld());
}

float USessionSubSystem::GetProgress() const
{
	switch (State)
	{
	case EMatchingState::Searching:
		return FMath::Clamp(static_cast<float>((FPlatformTime::Seconds() - SearchStartSeconds) / FMath::Max(0.1f, SearchSeconds)), 0.0f, 1.0f);
	case EMatchingState::Joining:
	case EMatchingState::Hosting:
		return 1.0f;
	default:
		return 0.0f;
	}
}

void USessionSubSystem::SetState(EMatchingState NewState, const FText& Message)
{
	State = NewState;
	UE_LOG(LogTemp, Display, TEXT("[Session] state=%s %s"),
		*StaticEnum<EMatchingState>()->GetNameStringByValue(static_cast<int64>(NewState)), *Message.ToString());
	OnMatchingStateChanged.Broadcast(NewState, Message);
}

// 게임 시작.
// ① 지난 판 방이 남아 있으면 먼저 지운다(로비로 돌아왔다가 다시 시작하는 경우).
// ② 같은 네트워크의 방을 SearchSeconds 초 동안 찾는다.
void USessionSubSystem::StartMatching()
{
	if (State == EMatchingState::Searching || State == EMatchingState::Joining || State == EMatchingState::Hosting)
		return;
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid())
	{
		SetState(EMatchingState::Failed, NSLOCTEXT("PG", "NoOnline", "온라인 기능을 쓸 수 없습니다"));
		return;
	}

	SearchStartSeconds = FPlatformTime::Seconds();
	SetState(EMatchingState::Searching, NSLOCTEXT("PG", "Searching", "매칭중..."));
	if (Sessions->GetNamedSession(NAME_GameSession))
	{
		bSearchAfterDestroy = true;
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &USessionSubSystem::HandleDestroyComplete));
		Sessions->DestroySession(NAME_GameSession);
		return;
	}
	BeginSearch();
}

void USessionSubSystem::HandleDestroyComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	if (bSearchAfterDestroy && State == EMatchingState::Searching)
	{
		bSearchAfterDestroy = false;
		BeginSearch();
	}
}

void USessionSubSystem::BeginSearch()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	Search = MakeShared<FOnlineSessionSearch>();
	Search->bIsLanQuery = true;
	Search->MaxSearchResults = 20;
	Search->TimeoutInSeconds = SearchSeconds;
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &USessionSubSystem::HandleFindComplete));
	if (!Sessions->FindSessions(0, Search.ToSharedRef()))
		HandleFindComplete(false);
}

// 찾기 끝: 우리 게임 방 중 자리가 남은 첫 방에 들어간다. 없으면 내가 방을 연다.
void USessionSubSystem::HandleFindComplete(bool bWasSuccessful)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	if (State != EMatchingState::Searching)
		return; // 그사이 취소됨

	const FOnlineSessionSearchResult* Found = nullptr;
	if (bWasSuccessful && Search.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : Search->SearchResults)
		{
			FString Value;
			const bool bOurs = Result.Session.SessionSettings.Get(PGSession::GameKey, Value) && Value == PGSession::GameValue;
			if (bOurs && Result.Session.NumOpenPublicConnections > 0)
			{
				Found = &Result;
				break;
			}
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[Session] search done ok=%d results=%d joinable=%s"),
		bWasSuccessful ? 1 : 0, Search.IsValid() ? Search->SearchResults.Num() : 0, Found ? TEXT("yes") : TEXT("no"));

	if (!Found)
	{
		BeginHost();
		return;
	}
	SetState(EMatchingState::Joining, NSLOCTEXT("PG", "Joining", "방에 들어가는 중..."));
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &USessionSubSystem::HandleJoinComplete));
	if (!Sessions->JoinSession(0, NAME_GameSession, *Found))
		HandleJoinComplete(NAME_GameSession, EOnJoinSessionCompleteResult::UnknownError);
}

// 방 들어가기 끝: 방장 주소로 이동한다(리슨 서버에 클라로 접속).
void USessionSubSystem::HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result)
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid())
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);

	FString Address;
	APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (Result != EOnJoinSessionCompleteResult::Success || !Sessions.IsValid()
		|| !Sessions->GetResolvedConnectString(SessionName, Address) || !PlayerController)
	{
		SetState(EMatchingState::Failed, NSLOCTEXT("PG", "JoinFailed", "방에 들어가지 못했습니다"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[Session] joining %s"), *Address);
	PlayerController->ClientTravel(Address, TRAVEL_Absolute);
}

// 방 열기: LAN 에 알리는 방을 만들고, 다 만들어지면 게임 맵을 리슨 서버로 연다.
void USessionSubSystem::BeginHost()
{
	const IOnlineSessionPtr Sessions = GetSessionInterface();
	SetState(EMatchingState::Hosting, NSLOCTEXT("PG", "Hosting", "방을 여는 중..."));

	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = true;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = true;
	Settings.bUsesPresence = false;
	Settings.NumPublicConnections = MaxPlayers;
	Settings.Set(PGSession::GameKey, PGSession::GameValue, EOnlineDataAdvertisementType::ViaOnlineService);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &USessionSubSystem::HandleCreateComplete));
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
		HandleCreateComplete(NAME_GameSession, false);
}

void USessionSubSystem::HandleCreateComplete(FName SessionName, bool bWasSuccessful)
{
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	if (!bWasSuccessful)
	{
		SetState(EMatchingState::Failed, NSLOCTEXT("PG", "HostFailed", "방을 열지 못했습니다"));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("[Session] hosting listen server map=%s"), *GameMapPath);
	UGameplayStatics::OpenLevel(GetWorld(), FName(*GameMapPath), true, TEXT("listen"));
}

// 취소: 찾는 중일 때만. 찾기를 멈추고 처음 상태로.
void USessionSubSystem::CancelMatching()
{
	if (State != EMatchingState::Searching)
		return;
	if (const IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		Sessions->CancelFindSessions();
	}
	bSearchAfterDestroy = false;
	SetState(EMatchingState::Idle, NSLOCTEXT("PG", "Cancelled", "매칭을 취소했습니다"));
}
