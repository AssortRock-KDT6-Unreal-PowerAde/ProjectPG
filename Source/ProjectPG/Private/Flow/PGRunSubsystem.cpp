#include "Flow/PGRunSubsystem.h"
#include "Flow/PGLoadingScreenSubsystem.h"
#include "TimerManager.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Actors/LevelDesignValidationCharacter.h"
#include "Components/InventoryComponent.h"      // 팀 인벤토리(읽기만). PlayerState 에 붙어 있다
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Flow/PGFlowSettings.h"
#include "Flow/PGStashSaveGame.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameModes/GameModePG.h"               // 팀 맵 생성 게임모드. "이 게임모드가 돌면 판이다" 만 본다
#include "GameplayAbilities/CharacterAttributeSet.h" // 팀 체력 속성(GAS). 0 이 되는 것을 듣는다
#include "GameplayEffectTypes.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Robot/PGRobotCharacter.h"
#include "TimerManager.h"
#include "Common/PGPlayerMessageComponent.h"
#include "Server/MatchmakingSubSystem.h"   // 팀 매칭(부르기만)
#include "Server/WebSocketSubSystem.h"     // 팀 로비 연결(로그인 여부만 읽는다)

// 파일 고유 접두어를 붙인 이름 공간. 유니티 빌드에서 다른 cpp 의 익명 namespace 상수와 이름이 겹쳐 빌드가 깨진 적이 있다(263437b).
namespace PGRunSubsystemLocal
{
	// 한 틱에 이보다 멀리 움직였으면 순간이동(스폰 자리로 옮김·탈것 갈아타기)으로 보고 거리에 안 넣는다. 50m.
	constexpr float TeleportThresholdCm = 5000.0f;

	// 흐름 레벨이 설정에 없을 때 쓰는 엔진 빈 맵. 오브젝트 스모크 테스트가 같은 맵을 쓴다.
	const TCHAR* EntryMapPackage = TEXT("/Engine/Maps/Entry");

	const TCHAR* ScreenName(EPGFlowScreen Screen)
	{
		switch (Screen)
		{
		case EPGFlowScreen::Title: return TEXT("Title");
		case EPGFlowScreen::Lobby: return TEXT("Lobby");
		case EPGFlowScreen::Game: return TEXT("Game");
		case EPGFlowScreen::Scoreboard: return TEXT("Scoreboard");
		}
		return TEXT("?");
	}

	const TCHAR* ResultName(EPGRunResult Result)
	{
		switch (Result)
		{
		case EPGRunResult::None: return TEXT("None");
		case EPGRunResult::InProgress: return TEXT("InProgress");
		case EPGRunResult::Extracted: return TEXT("Extracted");
		case EPGRunResult::Died: return TEXT("Died");
		case EPGRunResult::TimedOut: return TEXT("TimedOut");
		case EPGRunResult::Aborted: return TEXT("Aborted");
		}
		return TEXT("?");
	}

	// 흐름 레벨이 없을 때 Entry 맵에 얹을 게임모드 경로.
	const TCHAR* FallbackGameModePath(EPGFlowScreen Screen)
	{
		switch (Screen)
		{
		case EPGFlowScreen::Title: return TEXT("/Script/ProjectPG.PGTitleGameMode");
		case EPGFlowScreen::Lobby: return TEXT("/Script/ProjectPG.PGLobbyGameMode");
		case EPGFlowScreen::Scoreboard: return TEXT("/Script/ProjectPG.PGScoreboardGameMode");
		case EPGFlowScreen::Game: return TEXT("/Script/ProjectPG.GameModePG");
		}
		return TEXT("");
	}

	void AddStack(TArray<FPGItemStack>& Items, FName ItemId, int32 Count)
	{
		if (ItemId.IsNone() || Count <= 0)
			return;
		for (FPGItemStack& Stack : Items)
		{
			if (Stack.ItemId == ItemId)
			{
				Stack.Count += Count;
				return;
			}
		}
		FPGItemStack& NewStack = Items.AddDefaulted_GetRef();
		NewStack.ItemId = ItemId;
		NewStack.Count = Count;
	}

	FString DescribeItems(const TArray<FPGItemStack>& Items)
	{
		FString Out;
		for (const FPGItemStack& Stack : Items)
			Out += FString::Printf(TEXT("%s%s x%d"), Out.IsEmpty() ? TEXT("") : TEXT(", "), *Stack.ItemId.ToString(), Stack.Count);
		return Out.IsEmpty() ? TEXT("(none)") : Out;
	}
}

UPGRunSubsystem* UPGRunSubsystem::Get(const UObject* WorldContext)
{
	if (!IsValid(WorldContext))
		return nullptr;
	const UWorld* World = WorldContext->GetWorld();
	const UGameInstance* GameInstance = IsValid(World) ? World->GetGameInstance() : nullptr;
	return IsValid(GameInstance) ? GameInstance->GetSubsystem<UPGRunSubsystem>() : nullptr;
}

void UPGRunSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadStash();
	// 서브시스템은 Tick 이 없다. 코어 티커에 붙여 매 프레임 돈다(PGPerfSweep 과 같은 방식). 판이 없을 때는 거의 공짜다.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UPGRunSubsystem::TickRun), 0.0f);
	UE_LOG(LogTemp, Display, TEXT("PGFlow: run subsystem ready — stash %d kinds, %d runs recorded (%d extracted / %d died)"),
		Stash.Num(), Stats.TotalRuns, Stats.Extractions, Stats.Deaths);
}

void UPGRunSubsystem::Deinitialize()
{
	if (IsRunActive())
		AbortRun(TEXT("game instance shutting down"));
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	UnbindDeathWatch();
	Super::Deinitialize();
}

// ---- 흐름 전환 ----

void UPGRunSubsystem::GoToTitle() { OpenFlowLevel(EPGFlowScreen::Title); }
void UPGRunSubsystem::GoToLobby() { OpenFlowLevel(EPGFlowScreen::Lobby); }
void UPGRunSubsystem::GoToScoreboard() { OpenFlowLevel(EPGFlowScreen::Scoreboard); }

void UPGRunSubsystem::StartGame()
{
	// 멀티(9/28): 출격은 서버로 간다. 예전에는 로비 버튼이 늘 이 컴퓨터에서 혼자 하는 판(게임 레벨)을 열었다 —
	//   게임은 전용 서버 멀티인데 로비에서 서버로 가는 길이 없었다(멀티 점검 A1). 순서:
	//   1) 실행 인자 -PGServer=주소:포트 가 있으면 그 서버로 바로 접속(형님 서버 없이 시험·시연할 때).
	//   2) 로그인했으면(형님 로비 연결에 사용자 ID 가 있으면) 형님 매칭에 "게임 시작" 을 청한다. 서버가 뜨면 형님 코드가
	//      JOIN_SERVER 를 받아 접속시킨다(UMatchmakingSubSystem::HandleMatchMessage). 기다리는 동안 로딩 화면에 매칭 상태.
	//   3) 둘 다 아니면 예전처럼 이 컴퓨터에서 혼자 하는 판(에디터 시험·로그인 없는 실행).
	UWorld* World = GetWorld();
	UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(World);
	FString DirectServer;
	if (FParse::Value(FCommandLine::Get(), TEXT("PGServer="), DirectServer) && !DirectServer.IsEmpty() && IsValid(World))
	{
		UE_LOG(LogTemp, Display, TEXT("PGFlow: 출격 — connecting to the server %s (-PGServer)"), *DirectServer);
		if (Loading)
			Loading->ShowLoading(NSLOCTEXT("PGFlow", "ConnectingServer", "서버에 접속 중…"));
		World->GetTimerManager().SetTimer(TravelTimer, FTimerDelegate::CreateWeakLambda(this, [this, DirectServer]()
		{
			if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
				PC->ClientTravel(DirectServer, ETravelType::TRAVEL_Absolute);
			else
				UGameplayStatics::OpenLevel(GetWorld(), FName(*DirectServer));
		}), 0.15f, false);
		return;
	}
	const UWebSocketSubSystem* Lobby = UWebSocketSubSystem::Get(World);
	if (UMatchmakingSubSystem* Match = UMatchmakingSubSystem::Get(World); Match && Lobby && !Lobby->GetCurrentUserID().IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("PGFlow: 출격 — asking the matchmaker for a server (user %s)"), *Lobby->GetCurrentUserID());
		if (!bMatchStatusBound)
		{
			Match->OnMatchStatusChanged.AddDynamic(this, &UPGRunSubsystem::HandleMatchStatus);
			bMatchStatusBound = true;
		}
		if (Loading)
			Loading->ShowLoading(NSLOCTEXT("PGFlow", "Matching", "매칭 중…"));
		Match->RequestGameStart();
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("PGFlow: 출격 — opening the game level"));
	// 먼저 화면을 검게 가리고, 0.15초 뒤에 레벨을 연다. 바로 열면 레벨 교체가 다음 틱 맨 앞에서 일어나 검은 화면이 한 번도
	// 그려지지 않은 채 로비 화면이 멈춰 보였다. 가림막은 시작 지점에 플레이어가 선 뒤에 걷힌다(WarZoneFootprintPreview).
	// 9/22 사용자: "출격 누르면 마네킹부터 나오고 맵 가운데가 잠깐 보이다 넘어간다 — 검은 화면 + 필드 진입 중… 로딩 표시가 낫다".
	if (Loading && IsValid(World))
	{
		Loading->ShowLoading(NSLOCTEXT("PGFlow", "EnteringField", "필드에 진입 중…"));
		World->GetTimerManager().SetTimer(TravelTimer, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			OpenFlowLevel(EPGFlowScreen::Game);
		}), 0.15f, false);
		return;
	}
	OpenFlowLevel(EPGFlowScreen::Game);
}

// 매칭 상태를 로딩 화면 글자로. 취소되면 로딩을 걷어 로비로 돌려준다(접속은 형님 코드가 JOIN_SERVER 에서 한다).
void UPGRunSubsystem::HandleMatchStatus(const FString& StatusType, const FString& Message)
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: match status %s — %s"), *StatusType, *Message);
	UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(GetWorld());
	if (!Loading)
		return;
	if (StatusType.Equals(TEXT("Match_CANCELLED"), ESearchCase::IgnoreCase) || StatusType.Equals(TEXT("CancelMatch"), ESearchCase::IgnoreCase))
		Loading->HideLoading(TEXT("match cancelled"));
	else
		Loading->ShowLoading(FText::FromString(Message));
}

bool UPGRunSubsystem::ResolveFlowLevel(EPGFlowScreen Screen, FString& OutPackage, FString& OutOptions) const
{
	const UPGFlowSettings& Settings = UPGFlowSettings::Get();
	const TSoftObjectPtr<UWorld>* Level = nullptr;
	switch (Screen)
	{
	case EPGFlowScreen::Title: Level = &Settings.TitleLevel; break;
	case EPGFlowScreen::Lobby: Level = &Settings.LobbyLevel; break;
	case EPGFlowScreen::Scoreboard: Level = &Settings.ScoreboardLevel; break;
	case EPGFlowScreen::Game: Level = &Settings.GameLevel; break;
	}

	OutOptions.Reset();
	const FString Package = Level ? Level->ToSoftObjectPath().GetLongPackageName() : FString();
	// 설정된 레벨이 실제로 있을 때만 그 레벨. 경로만 적어 두고 아직 안 만든 경우가 흔해서 확인한다.
	if (!Package.IsEmpty() && FPackageName::DoesPackageExist(Package))
	{
		OutPackage = Package;
		return true;
	}
	if (Screen == EPGFlowScreen::Game)
	{
		// 게임 맵은 대신할 것이 없다. 빈 맵에 맵 생성 게임모드를 얹어도 타일 레벨이 없어 아무것도 안 나온다.
		UE_LOG(LogTemp, Error, TEXT("PGFlow: game level '%s' not found — set ProjectPG Flow > GameLevel"), *Package);
		OutPackage.Reset();
		return false;
	}
	OutPackage = PGRunSubsystemLocal::EntryMapPackage;
	OutOptions = FString::Printf(TEXT("game=%s"), PGRunSubsystemLocal::FallbackGameModePath(Screen));
	return false;
}

FString UPGRunSubsystem::DescribeFlowLevel(EPGFlowScreen Screen) const
{
	FString Package, Options;
	const bool bAuthored = ResolveFlowLevel(Screen, Package, Options);
	return FString::Printf(TEXT("%s -> %s%s%s%s"), PGRunSubsystemLocal::ScreenName(Screen), *Package,
		Options.IsEmpty() ? TEXT("") : TEXT("?"), *Options, bAuthored ? TEXT("") : TEXT(" (fallback: engine Entry map)"));
}

void UPGRunSubsystem::OpenFlowLevel(EPGFlowScreen Screen)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return;
	// 게임 맵을 떠나는데 판이 아직 열려 있으면 중단으로 기록한다(스코어보드로 가는 길이면 이미 끝나 있다).
	if (IsRunActive() && Screen != EPGFlowScreen::Game)
		AbortRun(TEXT("leaving the game level"));
	if (IsRunActive() && Screen == EPGFlowScreen::Game)
		AbortRun(TEXT("restarting the game level"));

	FString Package, Options;
	ResolveFlowLevel(Screen, Package, Options);
	if (Package.IsEmpty())
		return;
	World->GetTimerManager().ClearTimer(TravelTimer);
	UE_LOG(LogTemp, Display, TEXT("PGFlow: -> %s  (%s%s%s)"), PGRunSubsystemLocal::ScreenName(Screen), *Package,
		Options.IsEmpty() ? TEXT("") : TEXT("?"), *Options);
	// 서버가 생기면 여기만 ServerTravel 로 바꾼다(클라이언트는 서버를 따라간다).
	UGameplayStatics::OpenLevel(World, FName(*Package), true, Options);
}

// ---- 판 시작·진행 ----

bool UPGRunSubsystem::TickRun(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || !World->HasBegunPlay())
		return true;

	if (!IsRunActive())
	{
		// 판 시작 감지: 맵 생성 게임모드(팀 AGameModePG)가 돌기 시작했으면 판이다. 팀 게임모드를 고치지 않으려고 여기서 본다.
		// 방금 판이 끝난 월드는 제외: 끝난 뒤 스코어보드로 넘어가기까지 게임모드가 그대로라 또 판이 열렸다.
		if (Cast<AGameModePG>(World->GetAuthGameMode()) && EndedRunWorld.Get() != World)
			StartRun(World);
		return true;
	}

	if (RunWorld.Get() != World)
	{
		// 판을 끝내지 않고 다른 월드로 옮겨졌다(콘솔 open, PIE 재시작). 기록은 남기지 않는다.
		AbortRun(TEXT("world changed"));
		return true;
	}

	if (IsNetRun())
	{
		TickNetRun(DeltaSeconds);
		return true;
	}

	APlayerController* PC = GetRunPlayerController();
	APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
	TrackPawn(Pawn, DeltaSeconds);

	// 출격 때 창고를 들고 나가기(설정). 폰이 생긴 첫 틱에 한 번만.
	if (!bStashGivenThisRun && IsValid(Pawn) && UPGFlowSettings::Get().bCarryStashIntoRun && Pawn->Implements<UPGItemReceiver>())
	{
		bStashGivenThisRun = true;
		const int32 Given = GiveStashToPawn(Pawn);
		UE_LOG(LogTemp, Display, TEXT("PGFlow: carried %d stash item(s) into the run"), Given);
	}

	const float Limit = UPGFlowSettings::Get().RunTimeLimitSeconds;
	if (Limit > 0.0f && GetRunElapsedSeconds() >= Limit)
		FinishRun(EPGRunResult::TimedOut, NSLOCTEXT("PGFlow", "TimeUp", "제한 시간 종료"));
	return true;
}

void UPGRunSubsystem::StartRun(UWorld* World)
{
	CurrentRun = FPGRunRecord();
	CurrentRun.Result = EPGRunResult::InProgress;
	CurrentRun.StartedAt = FDateTime::Now();
	if (const AGameModePG* GameMode = World->GetAuthGameMode<AGameModePG>())
		CurrentRun.MapSeed = GameMode->GetMapGenerationSeed();
	RunWorld = World;
	RunStartWorldSeconds = World->GetTimeSeconds();
	bStashGivenThisRun = false;
	TrackedPawn = nullptr;
	UnbindDeathWatch();
	for (TPair<TWeakObjectPtr<APlayerController>, FPlayerRun>& Pair : PlayerRuns)
		UnwatchPlayer(Pair.Value);
	PlayerRuns.Reset();
	UE_LOG(LogTemp, Display, TEXT("PGFlow: run started on %s (seed %lld, time limit %.0fs)"),
		*World->GetMapName(), CurrentRun.MapSeed, UPGFlowSettings::Get().RunTimeLimitSeconds);
	OnRunStarted.Broadcast();
}

void UPGRunSubsystem::StartRunManually()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return;
	if (IsRunActive())
		AbortRun(TEXT("manual restart"));
	StartRun(World);
}

float UPGRunSubsystem::GetRunElapsedSeconds() const
{
	const UWorld* World = RunWorld.Get();
	return (IsRunActive() && IsValid(World)) ? static_cast<float>(World->GetTimeSeconds() - RunStartWorldSeconds) : 0.0f;
}

void UPGRunSubsystem::AbortRun(const TCHAR* Why)
{
	if (!IsRunActive())
		return;
	// 시간은 결과를 바꾸기 "전에" 잰다. GetRunElapsedSeconds 는 판이 진행 중(InProgress)일 때만 값을 주기 때문이다 — FinishRun 참고.
	const float Elapsed = GetRunElapsedSeconds();
	CurrentRun.Result = EPGRunResult::Aborted;
	CurrentRun.DurationSeconds = Elapsed;
	UE_LOG(LogTemp, Warning, TEXT("PGFlow: run aborted (%s) after %.0fs — not recorded"), Why, CurrentRun.DurationSeconds);
	UnbindDeathWatch();
	TrackedPawn = nullptr;
	EndedRunWorld = RunWorld;
	RunWorld = nullptr;
}

void UPGRunSubsystem::FinishRun(EPGRunResult Result, const FText& How)
{
	if (!IsRunActive())
		return;
	if (Result == EPGRunResult::None || Result == EPGRunResult::InProgress)
		return;
	if (IsNetRun())
	{
		// 멀티: 판 전체 결과(시간 초과·콘솔)는 아직 안 끝난 사람 모두에게 그 결과로 끝낸다. 서버는 맵을 바꾸지 않는다.
		TArray<TWeakObjectPtr<APlayerController>> Open;
		for (const TPair<TWeakObjectPtr<APlayerController>, FPlayerRun>& Pair : PlayerRuns)
			if (!Pair.Value.Record.IsFinished())
				Open.Add(Pair.Key);
		for (const TWeakObjectPtr<APlayerController>& PC : Open)
			FinishPlayerRun(PC.Get(), Result, How);
		CurrentRun.Result = Result;
		CurrentRun.HowItEnded = How;
		EndedRunWorld = RunWorld;
		RunWorld = nullptr;
		UE_LOG(LogTemp, Display, TEXT("PGFlow: net run closed — %s for %d player(s) still in the raid (server stays on the map)"),
			PGRunSubsystemLocal::ResultName(Result), Open.Num());
		return;
	}

	// [버그 수정 9/22] 결과 화면에 "걸린 시간 0분 00초" 가 나왔다. 원인: 예전에는 Result 를 Extracted 로 먼저 바꾼 다음
	//   GetRunElapsedSeconds() 를 불렀는데, 그 함수는 IsRunActive()(= Result 가 InProgress)일 때만 시간을 주고 아니면 0 을 돌려준다.
	//   즉 판을 "끝났다" 로 표시한 순간 시계가 0 을 말했다. 로그의 "Extracted ... in 0s" 가 그 흔적이다.
	//   그래서 결과를 바꾸기 전에 먼저 잰다.
	const float Elapsed = GetRunElapsedSeconds();
	CurrentRun.Result = Result;
	CurrentRun.HowItEnded = How;
	CurrentRun.DurationSeconds = Elapsed;
	CollectPlayerItems(CurrentRun.ItemsAtEnd);
	UnbindDeathWatch();

	UE_LOG(LogTemp, Display, TEXT("PGFlow: run finished — %s (%s) in %.0fs: kills %d (+%d boss), dragon %s, moved %.0fm, shots %d, items [%s]"),
		PGRunSubsystemLocal::ResultName(Result), *How.ToString(), CurrentRun.DurationSeconds,
		CurrentRun.MonsterKills, CurrentRun.BossKills, CurrentRun.bDragonKilled ? TEXT("killed") : TEXT("no"),
		CurrentRun.DistanceCm / 100.0f, CurrentRun.ShotsFired, *PGRunSubsystemLocal::DescribeItems(CurrentRun.ItemsAtEnd));

	LastRun = CurrentRun;
	if (Result != EPGRunResult::Aborted)
		ApplyRunToStashAndStats(LastRun);
	OnRunFinished.Broadcast(LastRun);

	UWorld* World = RunWorld.Get();
	EndedRunWorld = RunWorld;
	RunWorld = nullptr;
	TrackedPawn = nullptr;

	const UPGFlowSettings& Settings = UPGFlowSettings::Get();
	if (Settings.bAutoTravelAfterRun && IsValid(World))
	{
		// 탈출·죽는 연출을 보고 나서 넘어간다. 람다는 UObject 에 묶어(WeakLambda) 서브시스템이 먼저 죽으면 안 돈다.
		World->GetTimerManager().SetTimer(TravelTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]() { GoToScoreboard(); }),
			FMath::Max(0.01f, Settings.ResultScreenDelaySeconds), false);
		UE_LOG(LogTemp, Display, TEXT("PGFlow: scoreboard in %.1fs"), Settings.ResultScreenDelaySeconds);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("PGFlow: bAutoTravelAfterRun is off — staying on the level (PG.Flow.Scoreboard to look at the result)"));
	}
}

void UPGRunSubsystem::ApplyRunToStashAndStats(const FPGRunRecord& Record)
{
	Stats.TotalRuns += 1;
	Stats.TotalMonsterKills += Record.MonsterKills;
	Stats.TotalBossKills += Record.BossKills;
	Stats.DragonKills += Record.bDragonKilled ? 1 : 0;
	Stats.TotalDistanceCm += Record.DistanceCm;
	Stats.TotalPlaySeconds += Record.DurationSeconds;
	switch (Record.Result)
	{
	case EPGRunResult::Extracted:
		Stats.Extractions += 1;
		// 탈출: 들고 나온 것이 창고로 들어간다. 기획서 "판이 끝나면 장비·통계를 동기화, 창고는 판 밖에서만".
		for (const FPGItemStack& Stack : Record.ItemsAtEnd)
			PGRunSubsystemLocal::AddStack(Stash, Stack.ItemId, Stack.Count);
		break;
	case EPGRunResult::Died:
		Stats.Deaths += 1; // 사망: 들고 있던 것은 잃는다(기록에는 남아 스코어보드가 "잃은 아이템" 으로 보여 준다)
		break;
	case EPGRunResult::TimedOut:
		Stats.TimeOuts += 1;
		break;
	default:
		break;
	}

	RunHistory.Insert(Record, 0);
	const int32 MaxHistory = FMath::Max(1, UPGFlowSettings::Get().MaxRunHistory);
	if (RunHistory.Num() > MaxHistory)
		RunHistory.SetNum(MaxHistory);

	SaveStash();
	OnStashChanged.Broadcast();
}

// ---- 플레이어 폰 추적(거리·사망) ----

APlayerController* UPGRunSubsystem::GetRunPlayerController() const
{
	const UWorld* World = RunWorld.Get();
	return IsValid(World) ? World->GetFirstPlayerController() : nullptr;
}

bool UPGRunSubsystem::IsRunPlayerPawn(const APawn* Pawn) const
{
	if (!IsRunActive() || !IsValid(Pawn) || Pawn->GetWorld() != RunWorld.Get())
		return false;
	// 플레이어가 조종하는 폰(탈것에 탔으면 탈것)만. 스모크 테스트 폰·AI 폰은 컨트롤러가 없거나 AI 라 걸러진다.
	const AController* Controller = Pawn->GetController();
	if (IsValid(Controller) && Controller->IsPlayerController())
		return true;
	// 탈것에 탄 동안의 캐릭터는 컨트롤러가 떨어져 있다. 그래도 이 판의 플레이어 폰으로 본다(탈출구는 캐릭터에 F 를 누른다).
	return TrackedPawn.Get() == Pawn;
}

void UPGRunSubsystem::TrackPawn(APawn* Pawn, float DeltaSeconds)
{
	if (TrackedPawn.Get() != Pawn)
	{
		// 폰이 바뀌었다(처음 생김·탈것 탑승·하차). 거리는 새 자리부터 다시 재고, 사망 감시를 새 폰에 건다.
		TrackedPawn = Pawn;
		if (IsValid(Pawn))
			LastPawnLocation = Pawn->GetActorLocation();
		BindDeathWatch(Pawn);
		return;
	}
	if (!IsValid(Pawn))
		return;
	const FVector Now = Pawn->GetActorLocation();
	const float Step = FVector::Dist2D(Now, LastPawnLocation);
	LastPawnLocation = Now;
	if (Step < PGRunSubsystemLocal::TeleportThresholdCm)
		CurrentRun.DistanceCm += Step;
}

void UPGRunSubsystem::BindDeathWatch(APawn* Pawn)
{
	UnbindDeathWatch();
	if (!IsValid(Pawn))
		return;

	// (1) GAS 체력 속성이 0 이 되면 사망. 팀 캐릭터(ACustomPlayerCharacter)는 UCharacterAttributeSet::Health 를 가진다.
	//     지금은 아무 코드도 이 속성을 깎지 않는다(2026-09-22 확인) — 캐릭터 담당이 피해를 Health 에 넣기 시작하면 그대로 붙는다.
	if (const IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* Asc = AbilityOwner->GetAbilitySystemComponent())
		{
			WatchedAsc = Asc;
			HealthWatchHandle = Asc->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetHealthAttribute())
				.AddUObject(this, &UPGRunSubsystem::HandleHealthChanged);
		}
	}
	// (2) 폰이 Destroy 되면 사망. 레벨 이동 때는 Destroyed() 가 아니라 EndPlay 라 여기 안 걸린다.
	Pawn->OnDestroyed.AddUniqueDynamic(this, &UPGRunSubsystem::HandleTrackedPawnDestroyed);
}

void UPGRunSubsystem::UnbindDeathWatch()
{
	if (UAbilitySystemComponent* Asc = WatchedAsc.Get())
		Asc->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetHealthAttribute()).Remove(HealthWatchHandle);
	WatchedAsc = nullptr;
	HealthWatchHandle.Reset();
	if (APawn* Pawn = TrackedPawn.Get())
		Pawn->OnDestroyed.RemoveDynamic(this, &UPGRunSubsystem::HandleTrackedPawnDestroyed);
}

void UPGRunSubsystem::HandleHealthChanged(const FOnAttributeChangeData& Data)
{
	if (Data.NewValue <= 0.0f && Data.OldValue > 0.0f)
		NotifyPlayerDied(TrackedPawn.Get(), nullptr);
}

void UPGRunSubsystem::HandleTrackedPawnDestroyed(AActor* DestroyedActor)
{
	const UWorld* World = IsValid(DestroyedActor) ? DestroyedActor->GetWorld() : nullptr;
	if (!IsValid(World) || World->bIsTearingDown)
		return;
	if (IsNetRun())
	{
		// 멀티: 누구의 폰이었나. 그 사람이 나가는 중(접속 끊김)이면 사망이 아니다 — 기록 없이 뺀다.
		for (auto It = PlayerRuns.CreateIterator(); It; ++It)
		{
			if (It->Value.Tracked.Get() != DestroyedActor || It->Value.Record.IsFinished())
				continue;
			APlayerController* PC = It->Key.Get();
			if (!IsValid(PC) || PC->IsActorBeingDestroyed() || PC->GetPawn() == nullptr && !PC->GetPlayerState<APlayerState>())
			{
				UnwatchPlayer(It->Value);
				It.RemoveCurrent();
				return;
			}
			FinishPlayerRun(PC, EPGRunResult::Died, NSLOCTEXT("PGFlow", "DiedUnknown", "사망"));
			return;
		}
		return;
	}
	NotifyPlayerDied(Cast<APawn>(DestroyedActor), nullptr);
}

// ---- 아이템 ----

void UPGRunSubsystem::CollectPlayerItems(TArray<FPGItemStack>& OutItems) const
{
	OutItems.Reset();
	const APlayerController* PC = GetRunPlayerController();
	if (!IsValid(PC))
		return;

	// 1) 팀 인벤토리: PlayerState 에 붙은 UInventoryComponent 의 모든 가방(주머니·배낭)을 돈다. StackCount 0 은 1개로 센다
	//    (ACustomPlayerCharacter::HasItem 과 같은 규칙).
	if (const APlayerState* State = PC->GetPlayerState<APlayerState>())
	{
		if (const UInventoryComponent* Inventory = State->FindComponentByClass<UInventoryComponent>())
		{
			for (const TPair<FGuid, FItemArrayWrapper>& Bag : Inventory->GetItemsMap())
				for (const FItemInstance& Item : Bag.Value.Items)
					PGRunSubsystemLocal::AddStack(OutItems, Item.ItemID, FMath::Max(1, Item.StackCount));
			if (OutItems.Num() > 0)
				return;
		}
	}
	// 2) 검증용 폰(팀 캐릭터가 없을 때 PIE 가 주는 폰)의 이름→개수 맵.
	const APawn* Pawn = TrackedPawn.IsValid() ? static_cast<const APawn*>(TrackedPawn.Get()) : static_cast<const APawn*>(PC->GetPawn());
	if (const ALevelDesignValidationCharacter* Validation = Cast<ALevelDesignValidationCharacter>(Pawn))
		for (const TPair<FName, int32>& Entry : Validation->GetDebugInventory())
			PGRunSubsystemLocal::AddStack(OutItems, Entry.Key, Entry.Value);
}

void UPGRunSubsystem::AddToStash(FName ItemId, int32 Count)
{
	if (ItemId.IsNone() || Count <= 0)
		return;
	PGRunSubsystemLocal::AddStack(Stash, ItemId, Count);
	SaveStash();
	OnStashChanged.Broadcast();
}

bool UPGRunSubsystem::RemoveFromStash(FName ItemId, int32 Count)
{
	for (int32 Index = 0; Index < Stash.Num(); ++Index)
	{
		if (Stash[Index].ItemId != ItemId || Stash[Index].Count < Count)
			continue;
		Stash[Index].Count -= Count;
		if (Stash[Index].Count <= 0)
			Stash.RemoveAt(Index);
		SaveStash();
		OnStashChanged.Broadcast();
		return true;
	}
	return false;
}

void UPGRunSubsystem::ClearStash()
{
	Stash.Reset();
	SaveStash();
	OnStashChanged.Broadcast();
	UE_LOG(LogTemp, Display, TEXT("PGFlow: stash cleared"));
}

int32 UPGRunSubsystem::GiveStashToPawn(APawn* Pawn)
{
	if (!IsValid(Pawn))
		return 0;
	int32 Given = 0;
	// 뒤에서부터: 다 들어간 줄은 지운다. 가방이 차서 못 들어간 것은 창고에 남는다.
	for (int32 Index = Stash.Num() - 1; Index >= 0; --Index)
	{
		const FPGItemStack Stack = Stash[Index];
		if (UPGItemReceiverLibrary::GiveItem(Pawn, Stack.ItemId, Stack.Count))
		{
			Stash.RemoveAt(Index);
			Given += Stack.Count;
		}
	}
	if (Given > 0)
	{
		SaveStash();
		OnStashChanged.Broadcast();
	}
	return Given;
}

// ---- 저장 (서버가 생기면 이 두 함수만 바꾼다) ----

void UPGRunSubsystem::LoadStash()
{
	const FString Slot = UPGFlowSettings::Get().StashSlotName;
	if (!UGameplayStatics::DoesSaveGameExist(Slot, 0))
		return;
	const UPGStashSaveGame* Save = Cast<UPGStashSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	if (!IsValid(Save))
	{
		UE_LOG(LogTemp, Warning, TEXT("PGFlow: stash save '%s' could not be read — starting empty"), *Slot);
		return;
	}
	Stash = Save->Stash;
	Stats = Save->Stats;
	RunHistory = Save->RunHistory;
	if (RunHistory.Num() > 0)
		LastRun = RunHistory[0];
}

bool UPGRunSubsystem::SaveStash()
{
	UPGStashSaveGame* Save = Cast<UPGStashSaveGame>(UGameplayStatics::CreateSaveGameObject(UPGStashSaveGame::StaticClass()));
	if (!IsValid(Save))
		return false;
	Save->Stash = Stash;
	Save->Stats = Stats;
	Save->RunHistory = RunHistory;
	const FString Slot = UPGFlowSettings::Get().StashSlotName;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, Slot, 0);
	UE_LOG(LogTemp, Display, TEXT("PGFlow: stash saved=%s slot=%s (%d kinds: %s)"),
		bSaved ? TEXT("true") : TEXT("false"), *Slot, Stash.Num(), *PGRunSubsystemLocal::DescribeItems(Stash));
	return bSaved;
}

// ---- 멀티: 사람마다 따로(2026-09-27) ----

bool UPGRunSubsystem::IsNetRun() const
{
	const UWorld* World = RunWorld.Get();
	return IsRunActive() && IsValid(World) && World->GetNetMode() != NM_Standalone && World->GetNetMode() != NM_Client;
}

APlayerController* UPGRunSubsystem::ResolveRunPlayer(const APawn* Pawn) const
{
	if (APlayerController* PC = UPGPlayerMessageComponent::ResolvePlayer(Pawn))
		return PC;
	// 탈것에서 내린 뒤 막 없어진 경우 등: 그 폰을 따라가던 사람.
	for (const TPair<TWeakObjectPtr<APlayerController>, FPlayerRun>& Pair : PlayerRuns)
		if (Pair.Value.Tracked.Get() == Pawn)
			return Pair.Key.Get();
	return nullptr;
}

UPGRunSubsystem::FPlayerRun* UPGRunSubsystem::FindOpenPlayerRun(APlayerController* PC)
{
	if (!IsValid(PC))
		return nullptr;
	FPlayerRun* Run = PlayerRuns.Find(PC);
	return (Run && !Run->Record.IsFinished()) ? Run : nullptr;
}

void UPGRunSubsystem::TickNetRun(float DeltaSeconds)
{
	UWorld* World = RunWorld.Get();
	if (!IsValid(World))
		return;
	// 들어온 사람마다 기록을 연다(늦게 들어와도). 폰이 바뀌면(탈것 탑승·하차) 감시를 옮기고, 움직인 거리를 잰다.
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!IsValid(PC))
			continue;
		FPlayerRun* Run = PlayerRuns.Find(PC);
		if (!Run)
		{
			Run = &PlayerRuns.Add(PC);
			Run->Record.Result = EPGRunResult::InProgress;
			Run->Record.StartedAt = FDateTime::Now();
			Run->Record.MapSeed = CurrentRun.MapSeed;
			UE_LOG(LogTemp, Display, TEXT("PGFlow: net run — tracking %s"), *GetNameSafe(PC));
		}
		if (Run->Record.IsFinished())
			continue;
		APawn* Pawn = PC->GetPawn();
		if (Run->Tracked.Get() != Pawn)
		{
			WatchPlayerPawn(PC, *Run, Pawn);
			continue;
		}
		if (!IsValid(Pawn))
			continue;
		const FVector Now = Pawn->GetActorLocation();
		const float Step = FVector::Dist2D(Now, Run->LastLocation);
		Run->LastLocation = Now;
		if (Step < PGRunSubsystemLocal::TeleportThresholdCm)
			Run->Record.DistanceCm += Step;
	}
	// 창고 들고 나가기(bCarryStashIntoRun)는 멀티에서 하지 않는다 — 창고는 각자 컴퓨터(나중엔 형님 웹 서버)에 있다.
	const float Limit = UPGFlowSettings::Get().RunTimeLimitSeconds;
	if (Limit > 0.0f && GetRunElapsedSeconds() >= Limit)
		FinishRun(EPGRunResult::TimedOut, NSLOCTEXT("PGFlow", "TimeUp", "제한 시간 종료"));
}

void UPGRunSubsystem::WatchPlayerPawn(APlayerController* PC, FPlayerRun& Run, APawn* Pawn)
{
	UnwatchPlayer(Run);
	Run.Tracked = Pawn;
	if (!IsValid(Pawn))
		return;
	Run.LastLocation = Pawn->GetActorLocation();
	// 사망 감시: 혼자 하는 판(BindDeathWatch)과 같은 두 가지 — 체력 0, 폰 파괴.
	if (const IAbilitySystemInterface* AbilityOwner = Cast<IAbilitySystemInterface>(Pawn))
	{
		if (UAbilitySystemComponent* Asc = AbilityOwner->GetAbilitySystemComponent())
		{
			Run.Asc = Asc;
			Run.HealthHandle = Asc->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetHealthAttribute())
				.AddUObject(this, &UPGRunSubsystem::HandleNetHealthChanged, TWeakObjectPtr<APlayerController>(PC));
		}
	}
	Pawn->OnDestroyed.AddUniqueDynamic(this, &UPGRunSubsystem::HandleTrackedPawnDestroyed);
}

void UPGRunSubsystem::UnwatchPlayer(FPlayerRun& Run)
{
	if (UAbilitySystemComponent* Asc = Run.Asc.Get())
		Asc->GetGameplayAttributeValueChangeDelegate(UCharacterAttributeSet::GetHealthAttribute()).Remove(Run.HealthHandle);
	Run.Asc = nullptr;
	Run.HealthHandle.Reset();
	// 폰 파괴 신호는 여러 사람 폰에 같은 함수로 걸려 있다 — 이 폰 것만 푼다.
	if (APawn* Pawn = Run.Tracked.Get())
		Pawn->OnDestroyed.RemoveDynamic(this, &UPGRunSubsystem::HandleTrackedPawnDestroyed);
}

void UPGRunSubsystem::HandleNetHealthChanged(const FOnAttributeChangeData& Data, TWeakObjectPtr<APlayerController> PC)
{
	if (Data.NewValue <= 0.0f && Data.OldValue > 0.0f)
		FinishPlayerRun(PC.Get(), EPGRunResult::Died, NSLOCTEXT("PGFlow", "DiedUnknown", "사망"));
}

void UPGRunSubsystem::CollectItemsFor(const APlayerController* PC, TArray<FPGItemStack>& OutItems) const
{
	OutItems.Reset();
	// 서버의 그 사람 가방(팀 인벤토리는 서버에 있다 — 클라이언트로는 안 간다).
	const APlayerState* State = IsValid(PC) ? PC->GetPlayerState<APlayerState>() : nullptr;
	if (const UInventoryComponent* Inventory = State ? State->FindComponentByClass<UInventoryComponent>() : nullptr)
		for (const TPair<FGuid, FItemArrayWrapper>& Bag : Inventory->GetItemsMap())
			for (const FItemInstance& Item : Bag.Value.Items)
				PGRunSubsystemLocal::AddStack(OutItems, Item.ItemID, FMath::Max(1, Item.StackCount));
}

void UPGRunSubsystem::FinishPlayerRun(APlayerController* PC, EPGRunResult Result, const FText& How)
{
	FPlayerRun* Run = FindOpenPlayerRun(PC);
	if (!Run)
		return;
	FPGRunRecord& Record = Run->Record;
	Record.Result = Result;
	Record.HowItEnded = How;
	Record.DurationSeconds = static_cast<float>((FDateTime::Now() - Record.StartedAt).GetTotalSeconds());
	CollectItemsFor(PC, Record.ItemsAtEnd);
	UnwatchPlayer(*Run);
	UE_LOG(LogTemp, Display, TEXT("PGFlow: net run — %s finished %s (%s) in %.0fs: kills %d (+%d boss), moved %.0fm, shots %d, items [%s]"),
		*GetNameSafe(PC), PGRunSubsystemLocal::ResultName(Result), *How.ToString(), Record.DurationSeconds,
		Record.MonsterKills, Record.BossKills, Record.DistanceCm / 100.0f, Record.ShotsFired, *PGRunSubsystemLocal::DescribeItems(Record.ItemsAtEnd));
	// 그 사람 컴퓨터로 — 창고·통계는 거기 저장되고 결과 화면도 거기서 연다.
	UPGPlayerMessageComponent::SendRunResult(PC, Record);
}

void UPGRunSubsystem::ReceiveRunResultFromServer(const FPGRunRecord& Record)
{
	UE_LOG(LogTemp, Display, TEXT("PGFlow: my run result from the server — %s (%s), %.0fs, kills %d, items [%s]"),
		PGRunSubsystemLocal::ResultName(Record.Result), *Record.HowItEnded.ToString(), Record.DurationSeconds,
		Record.MonsterKills, *PGRunSubsystemLocal::DescribeItems(Record.ItemsAtEnd));
	LastRun = Record;
	if (Record.Result != EPGRunResult::Aborted)
		ApplyRunToStashAndStats(LastRun);
	OnRunFinished.Broadcast(LastRun);
	UWorld* World = GetWorld();
	// 듣기 서버 방장은 레벨을 옮기면 모두가 튕기므로 결과만 저장하고 머문다. 클라이언트는 결과 화면으로 간다(서버 연결을 끊고 떠난다).
	if (!IsValid(World) || World->GetNetMode() == NM_ListenServer)
		return;
	const UPGFlowSettings& Settings = UPGFlowSettings::Get();
	if (Settings.bAutoTravelAfterRun)
		World->GetTimerManager().SetTimer(TravelTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { GoToScoreboard(); }),
			FMath::Max(0.01f, Settings.ResultScreenDelaySeconds), false);
}

// ---- 기존 코드에서 오는 알림 ----

void UPGRunSubsystem::NotifyExtraction(APawn* Pawn, const FText& How)
{
	UPGRunSubsystem* Self = Get(Pawn);
	if (Self && Self->IsNetRun())
	{
		Self->FinishPlayerRun(Self->ResolveRunPlayer(Pawn), EPGRunResult::Extracted, How); // 멀티: 그 사람만
		return;
	}
	if (!Self || !Self->IsRunPlayerPawn(Pawn))
		return;
	UE_LOG(LogTemp, Display, TEXT("PGFlow: %s extracted via '%s'"), *GetNameSafe(Pawn), *How.ToString());
	Self->FinishRun(EPGRunResult::Extracted, How);
}

void UPGRunSubsystem::NotifyPlayerDied(APawn* Pawn, AActor* Killer)
{
	UPGRunSubsystem* Self = Get(Pawn);
	if (Self && Self->IsNetRun())
	{
		const FText NetHow = IsValid(Killer)
			? FText::Format(NSLOCTEXT("PGFlow", "KilledBy", "{0} 에게 사망"), FText::FromString(Killer->GetName()))
			: NSLOCTEXT("PGFlow", "DiedUnknown", "사망");
		Self->FinishPlayerRun(Self->ResolveRunPlayer(Pawn), EPGRunResult::Died, NetHow); // 멀티: 그 사람만
		return;
	}
	if (!Self || !Self->IsRunPlayerPawn(Pawn))
		return;
	const FText How = IsValid(Killer)
		? FText::Format(NSLOCTEXT("PGFlow", "KilledBy", "{0} 에게 사망"), FText::FromString(Killer->GetName()))
		: NSLOCTEXT("PGFlow", "DiedUnknown", "사망");
	UE_LOG(LogTemp, Display, TEXT("PGFlow: %s died (killer=%s)"), *GetNameSafe(Pawn), *GetNameSafe(Killer));
	Self->FinishRun(EPGRunResult::Died, How);
}

void UPGRunSubsystem::NotifyMonsterKilled(AActor* Victim, AController* Killer)
{
	UPGRunSubsystem* Self = Get(Victim);
	if (!Self || !Self->IsRunActive() || !IsValid(Victim) || Victim->GetWorld() != Self->RunWorld.Get())
		return;
	// 누가 죽였나: 플레이어 컨트롤러(총·차·탱크 모두 지시자가 플레이어 컨트롤러다)만 센다. 몬스터끼리 싸워 죽은 것은 안 센다.
	if (!IsValid(Killer) || !Killer->IsPlayerController())
		return;
	// 보스 = 탈것이 아닌 로봇(APGRobotCharacter::bRideable == false). 로봇 죽음도 APGMonsterCharacter::Die 를 거쳐 여기로 온다.
	const APGRobotCharacter* Robot = Cast<APGRobotCharacter>(Victim);
	const bool bBoss = IsValid(Robot) && !Robot->IsRideable();
	if (Self->IsNetRun())
	{
		// 멀티: 죽인 사람 기록에.
		if (FPlayerRun* Run = Self->FindOpenPlayerRun(Cast<APlayerController>(Killer)))
			(bBoss ? Run->Record.BossKills : Run->Record.MonsterKills) += 1;
		return;
	}
	if (bBoss)
		Self->CurrentRun.BossKills += 1;
	else
		Self->CurrentRun.MonsterKills += 1;
	UE_LOG(LogTemp, Display, TEXT("PGFlow: kill %s (%s) — monsters %d, bosses %d"),
		*Victim->GetName(), bBoss ? TEXT("boss") : TEXT("monster"), Self->CurrentRun.MonsterKills, Self->CurrentRun.BossKills);
}

void UPGRunSubsystem::NotifyDragonKilled(AActor* Dragon, AController* Killer)
{
	UPGRunSubsystem* Self = Get(Dragon);
	if (!Self || !Self->IsRunActive() || !IsValid(Dragon) || Dragon->GetWorld() != Self->RunWorld.Get())
		return;
	// 드래곤은 전함 주포(지시자 없음)로 잡는 것이 정석이라 누가 죽였는지는 안 따진다 — 이 판에 떨어뜨렸으면 플레이어의 것.
	Self->CurrentRun.bDragonKilled = true;
	for (TPair<TWeakObjectPtr<APlayerController>, FPlayerRun>& Pair : Self->PlayerRuns) // 멀티: 아직 판에 있는 모두의 것
		if (!Pair.Value.Record.IsFinished())
			Pair.Value.Record.bDragonKilled = true;
	UE_LOG(LogTemp, Display, TEXT("PGFlow: dragon %s shot down (killer=%s)"), *Dragon->GetName(), *GetNameSafe(Killer));
}

void UPGRunSubsystem::NotifyShotFired(AActor* Shooter, int32 Count)
{
	// [버그 수정 9/22] 결과 화면에 "사격 0발" 이 나왔다. 원인: 예전에는 UPGWeaponComponent::PerformAttack 에서만 이 알림을 불렀는데,
	//   그 컴포넌트는 검증용 폰(ALevelDesignValidationCharacter)에만 붙어 있다. 실제 플레이어(팀 ACustomPlayerCharacter)에는 총 컴포넌트가
	//   없고(좌클릭 총은 아직 없음), 플레이어가 쏘는 것은 가발 광선·날으는 차 빔·탱크 포·전함 주포/미사일이었다 — 그 어디에서도 안 불렀다.
	//   그래서 그 발사 함수들(전부 우리 코드)에서 한 줄씩 부르게 하고, 폰이 아닌 전함은 지시한 컨트롤러를 넘기게 했다.
	UPGRunSubsystem* Self = Get(Shooter);
	if (!Self || !Self->IsRunActive() || Count <= 0)
		return;
	if (Self->IsNetRun())
	{
		// 멀티: 쏜 사람 기록에(컨트롤러가 왔으면 그 사람, 폰이면 그 폰의 주인).
		APlayerController* PC = Cast<APlayerController>(Shooter);
		if (!PC)
			PC = Self->ResolveRunPlayer(Cast<APawn>(Shooter));
		if (FPlayerRun* Run = Self->FindOpenPlayerRun(PC))
			Run->Record.ShotsFired += Count;
		return;
	}
	bool bFromPlayer = false;
	if (const AController* Controller = Cast<AController>(Shooter))
		bFromPlayer = Controller->IsPlayerController() && Controller->GetWorld() == Self->RunWorld.Get();
	else
		bFromPlayer = Self->IsRunPlayerPawn(Cast<APawn>(Shooter));
	if (!bFromPlayer)
		return;
	Self->CurrentRun.ShotsFired += Count;
}

void UPGRunSubsystem::NotifyItemPickedUp(APawn* Pawn, FName ItemId, int32 Count)
{
	UPGRunSubsystem* Self = Get(Pawn);
	if (Self && Self->IsNetRun())
	{
		if (FPlayerRun* Run = Self->FindOpenPlayerRun(Self->ResolveRunPlayer(Pawn)))
			Run->Record.ItemsPickedUp += FMath::Max(1, Count);
		return;
	}
	if (!Self || !Self->IsRunPlayerPawn(Pawn))
		return;
	Self->CurrentRun.ItemsPickedUp += FMath::Max(1, Count);
}

// ---- 콘솔 명령 (PG.Flow.*) ----

namespace PGRunSubsystemConsole
{
	static UPGRunSubsystem* FromWorld(UWorld* World)
	{
		UPGRunSubsystem* Self = UPGRunSubsystem::Get(World);
		if (!Self)
		{
			UE_LOG(LogTemp, Warning, TEXT("PGFlow: no run subsystem in this world"));
		}
		return Self;
	}

	static FAutoConsoleCommandWithWorld TitleCommand(TEXT("PG.Flow.Title"), TEXT("Open the title screen."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UPGRunSubsystem* S = FromWorld(World)) S->GoToTitle(); }));
	static FAutoConsoleCommandWithWorld LobbyCommand(TEXT("PG.Flow.Lobby"), TEXT("Open the lobby."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UPGRunSubsystem* S = FromWorld(World)) S->GoToLobby(); }));
	static FAutoConsoleCommandWithWorld GameCommand(TEXT("PG.Flow.Game"), TEXT("Deploy: open the game level."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UPGRunSubsystem* S = FromWorld(World)) S->StartGame(); }));
	static FAutoConsoleCommandWithWorld ScoreboardCommand(TEXT("PG.Flow.Scoreboard"), TEXT("Open the scoreboard."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UPGRunSubsystem* S = FromWorld(World)) S->GoToScoreboard(); }));

	// PG.Flow.Finish extract|die|timeout — 탈출구까지 안 가고 결과 화면을 본다.
	static FAutoConsoleCommandWithWorldAndArgs FinishCommand(TEXT("PG.Flow.Finish"),
		TEXT("Finish the current run: PG.Flow.Finish extract|die|timeout"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UPGRunSubsystem* Self = FromWorld(World);
			if (!Self)
				return;
			const FString Mode = Args.Num() > 0 ? Args[0].ToLower() : TEXT("extract");
			if (Mode.StartsWith(TEXT("die")))
				Self->FinishRun(EPGRunResult::Died, NSLOCTEXT("PGFlow", "ConsoleDie", "콘솔 사망"));
			else if (Mode.StartsWith(TEXT("time")))
				Self->FinishRun(EPGRunResult::TimedOut, NSLOCTEXT("PGFlow", "ConsoleTimeout", "콘솔 시간 초과"));
			else
				Self->FinishRun(EPGRunResult::Extracted, NSLOCTEXT("PGFlow", "ConsoleExtract", "콘솔 탈출"));
		}));

	static FAutoConsoleCommandWithWorld StartRunCommand(TEXT("PG.Flow.StartRun"), TEXT("Start a run record on the current level (for testing outside the game map)."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UPGRunSubsystem* S = FromWorld(World)) S->StartRunManually(); }));

	static FAutoConsoleCommandWithWorld StatusCommand(TEXT("PG.Flow.Status"), TEXT("Log the run, stash and flow level paths."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
		{
			UPGRunSubsystem* Self = FromWorld(World);
			if (!Self)
				return;
			const FPGRunRecord& Run = Self->IsRunActive() ? Self->GetCurrentRun() : Self->GetLastRun();
			UE_LOG(LogTemp, Display, TEXT("PGFlow: status — active=%s result=%s elapsed=%.0fs kills=%d/%d dragon=%s moved=%.0fm shots=%d"),
				Self->IsRunActive() ? TEXT("yes") : TEXT("no"), PGRunSubsystemLocal::ResultName(Run.Result), Self->GetRunElapsedSeconds(),
				Run.MonsterKills, Run.BossKills, Run.bDragonKilled ? TEXT("yes") : TEXT("no"), Run.DistanceCm / 100.0f, Run.ShotsFired);
			const FPGPlayerStats& Stats = Self->GetStats();
			UE_LOG(LogTemp, Display, TEXT("PGFlow: stats — runs %d, extracted %d, died %d, timed out %d, kills %d/%d, dragons %d"),
				Stats.TotalRuns, Stats.Extractions, Stats.Deaths, Stats.TimeOuts, Stats.TotalMonsterKills, Stats.TotalBossKills, Stats.DragonKills);
			UE_LOG(LogTemp, Display, TEXT("PGFlow: stash — %s"), *PGRunSubsystemLocal::DescribeItems(Self->GetStashItems()));
			for (const EPGFlowScreen Screen : { EPGFlowScreen::Title, EPGFlowScreen::Lobby, EPGFlowScreen::Game, EPGFlowScreen::Scoreboard })
				UE_LOG(LogTemp, Display, TEXT("PGFlow: level %s"), *Self->DescribeFlowLevel(Screen));
		}));

	static FAutoConsoleCommandWithWorldAndArgs StashAddCommand(TEXT("PG.Flow.StashAdd"), TEXT("PG.Flow.StashAdd <ItemId> [count] — put an item in the stash (test)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			UPGRunSubsystem* Self = FromWorld(World);
			if (!Self || Args.Num() < 1)
				return;
			Self->AddToStash(FName(*Args[0]), Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 1);
		}));

	static FAutoConsoleCommandWithWorld StashClearCommand(TEXT("PG.Flow.StashClear"), TEXT("Empty the stash."),
		FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if (UPGRunSubsystem* S = FromWorld(World)) S->ClearStash(); }));
}
