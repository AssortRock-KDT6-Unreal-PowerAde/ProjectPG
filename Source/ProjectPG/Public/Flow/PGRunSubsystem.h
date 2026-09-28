// 한 판 기록 + 게임 흐름(타이틀 → 로비 → 게임 → 스코어보드 → 로비) 담당. (2026-09-22)
//
// 왜 GameInstanceSubsystem 인가: 레벨을 넘어도 살아남아야 한다. 게임 맵에서 모은 기록(처치·거리·아이템)을
//   스코어보드 레벨에서 보여 주고, 창고는 로비에서 읽는다. 액터·월드 서브시스템은 레벨과 함께 죽는다.
//
// 기존 코드와의 접점은 "알림" 한 줄씩이다(전부 static 이라 판이 없을 때는 아무 일도 안 한다):
//   - 탈출: APGExtractionZoneActor::CompletePawn → NotifyExtraction
//   - 몬스터·보스 처치: APGMonsterCharacter::Die → NotifyMonsterKilled (보스 로봇은 여기서 가려낸다)
//   - 드래곤 격추: APGDragonBoss::TakeDamage(체력 0) → NotifyDragonKilled
//   - 사격: 플레이어가 실제로 쏘는 곳 전부 → NotifyShotFired
//           (가발 광선 UPGWigBeamComponent::FireAuthoritative, 날으는 차 빔 UPGFlightKitComponent::FireBeam,
//            탱크 포 APGTankPawn::FireMainGun, 전함 주포·미사일 APGBattleshipActor::FireCannon/FireMissile, 검증 폰 총 UPGWeaponComponent::PerformAttack)
//   - 사망: 팀 캐릭터에는 아직 죽는 코드가 없다. 여기서는 (1) 체력 속성(GAS Health)이 0 이 되는 것, (2) 폰이 Destroy 되는 것을
//           구독하고, (3) NotifyPlayerDied 를 열어 둔다 — 캐릭터 담당이 죽는 코드를 만들면 그 안에서 한 줄 부르면 된다.
//   판 시작은 알림 없이 스스로 안다: 매 틱 "지금 월드의 게임모드가 AGameModePG(맵 생성 게임모드)인가" 를 본다.
//   팀 게임모드를 고치지 않으려는 것.
//
// 흐름 전환은 전부 UGameplayStatics::OpenLevel 이다(단일 플레이어·PIE 기준). 서버가 생기면 ServerTravel 로 바꾸는 자리는
//   OpenFlowLevel 하나다.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Flow/PGRunTypes.h"
#include "PGRunSubsystem.generated.h"

class APGExtractionZoneActor;
class APlayerController;
class UAbilitySystemComponent;
class UPGStashSaveGame;
struct FOnAttributeChangeData;

// 흐름의 화면. 로그와 게임모드가 같은 이름을 쓴다.
UENUM(BlueprintType)
enum class EPGFlowScreen : uint8
{
	Title      UMETA(DisplayName = "타이틀"),
	Lobby      UMETA(DisplayName = "로비"),
	Game       UMETA(DisplayName = "게임"),
	Scoreboard UMETA(DisplayName = "스코어보드"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPGRunStartedSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPGRunFinishedSignature, const FPGRunRecord&, Record);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPGStashChangedSignature);

UCLASS()
class PROJECTPG_API UPGRunSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPGRunSubsystem* Get(const UObject* WorldContext);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// ---- 흐름 전환 (UI 버튼이 부른다) ----
	UFUNCTION(BlueprintCallable, Category = "PG|Flow")
	void GoToTitle();

	UFUNCTION(BlueprintCallable, Category = "PG|Flow")
	void GoToLobby();

	// "출격". 게임 맵을 연다. 판 기록은 게임 맵의 게임모드가 시작될 때 자동으로 열린다.
	UFUNCTION(BlueprintCallable, Category = "PG|Flow")
	void StartGame();

	UFUNCTION(BlueprintCallable, Category = "PG|Flow")
	void GoToScoreboard();

	// 흐름 레벨의 실제 경로(설정에 없으면 Entry 맵 + 게임모드 옵션). 로그·디버그용.
	UFUNCTION(BlueprintCallable, Category = "PG|Flow")
	FString DescribeFlowLevel(EPGFlowScreen Screen) const;

	// ---- 판 기록 ----
	UFUNCTION(BlueprintCallable, Category = "PG|Run")
	bool IsRunActive() const { return CurrentRun.Result == EPGRunResult::InProgress; }

	UFUNCTION(BlueprintCallable, Category = "PG|Run")
	const FPGRunRecord& GetCurrentRun() const { return CurrentRun; }

	// 마지막으로 끝난 판(스코어보드·로비가 읽는다). 없으면 Result == None.
	UFUNCTION(BlueprintCallable, Category = "PG|Run")
	const FPGRunRecord& GetLastRun() const { return LastRun; }

	UFUNCTION(BlueprintCallable, Category = "PG|Run")
	float GetRunElapsedSeconds() const;

	// 판을 끝낸다. 결과를 잠그고 창고·통계를 갱신·저장한 뒤, 설정에 따라 스코어보드로 넘어간다.
	// 두 번 불리면 두 번째는 무시한다(탈출 직후 죽는 등).
	UFUNCTION(BlueprintCallable, Category = "PG|Run")
	void FinishRun(EPGRunResult Result, const FText& How);

	// 콘솔·테스트용. 게임 맵이 아니어도 판을 연다.
	UFUNCTION(BlueprintCallable, Category = "PG|Run")
	void StartRunManually();

	// ---- 창고(판 밖 아이템)·통계 ----
	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	const TArray<FPGItemStack>& GetStashItems() const { return Stash; }

	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	const FPGPlayerStats& GetStats() const { return Stats; }

	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	const TArray<FPGRunRecord>& GetRunHistory() const { return RunHistory; }

	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	void AddToStash(FName ItemId, int32 Count);

	// 창고에서 뺀다. 모자라면 아무것도 안 빼고 false.
	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	bool RemoveFromStash(FName ItemId, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	void ClearStash();

	// 창고 전부를 폰(IPGItemReceiver)에 넣는다. 들어간 것만 창고에서 뺀다(가방이 차면 남는다).
	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	int32 GiveStashToPawn(APawn* Pawn);

	// 지금 디스크에 쓴다. 판 끝·창고 변경 때 자동으로 부르므로 보통은 필요 없다.
	UFUNCTION(BlueprintCallable, Category = "PG|Stash")
	bool SaveStash();

	// 타이틀에서 팀 로그인 창을 이미 띄워 봤나(게임 인스턴스가 살아 있는 동안). 두 번째 타이틀부터는 로그인 창 대신 "시작" 버튼을 보인다.
	//   팀 UIManager 는 만든 위젯을 맵에 들고 있다가 재사용하는데, 레벨을 넘은 뒤에는 그 위젯의 주인 컨트롤러가 이미 사라져 있다.
	bool HasShownTitleLogin() const { return bTitleLoginShown; }
	void MarkTitleLoginShown() { bTitleLoginShown = true; }

	// ---- 기존 코드에서 부르는 알림. 판이 없거나 플레이어가 아니면 아무 일도 안 한다. ----
	// 멀티(9/27): 서버가 보낸 "내 판 결과" 를 이 컴퓨터에 적용한다 — 창고·통계 저장, 결과 신호, 잠시 뒤 결과 화면.
	//   UPGPlayerMessageComponent::ClientRunFinished 가 부른다(듣기 서버 방장은 바로).
	void ReceiveRunResultFromServer(const FPGRunRecord& Record);

	static void NotifyExtraction(APawn* Pawn, const FText& How);
	static void NotifyPlayerDied(APawn* Pawn, AActor* Killer);
	static void NotifyMonsterKilled(AActor* Victim, AController* Killer);
	static void NotifyDragonKilled(AActor* Dragon, AController* Killer);
	// Shooter 는 쏜 폰(캐릭터·탱크·날으는 차) 또는 지시한 컨트롤러(전함 주포처럼 폰이 아닌 것이 쏠 때). Count 는 한 번에 나간 발 수.
	static void NotifyShotFired(AActor* Shooter, int32 Count = 1);
	static void NotifyItemPickedUp(APawn* Pawn, FName ItemId, int32 Count);

	// ---- UI 담당이 듣는 델리게이트 ----
	UPROPERTY(BlueprintAssignable, Category = "PG|Run")
	FPGRunStartedSignature OnRunStarted;

	UPROPERTY(BlueprintAssignable, Category = "PG|Run")
	FPGRunFinishedSignature OnRunFinished;

	UPROPERTY(BlueprintAssignable, Category = "PG|Stash")
	FPGStashChangedSignature OnStashChanged;

protected:
	// 매 프레임: 판 시작 감지, 이동 거리, 제한 시간, 플레이어 폰 바뀜(탈것 탑승) 추적.
	bool TickRun(float DeltaSeconds);
	void StartRun(UWorld* World);
	void AbortRun(const TCHAR* Why);
	void TrackPawn(APawn* Pawn, float DeltaSeconds);
	void BindDeathWatch(APawn* Pawn);
	void UnbindDeathWatch();
	void HandleHealthChanged(const FOnAttributeChangeData& Data);

	UFUNCTION()
	void HandleTrackedPawnDestroyed(AActor* DestroyedActor);

	// 멀티(9/28): 출격 — 매칭 상태(형님 UMatchmakingSubSystem 의 알림)를 로딩 화면 글자로 보여 준다.
	UFUNCTION()
	void HandleMatchStatus(const FString& StatusType, const FString& Message);
	bool bMatchStatusBound = false;

	// 지금 이 판의 플레이어 폰인가(스모크 테스트 폰·AI 폰은 아니다).
	bool IsRunPlayerPawn(const APawn* Pawn) const;
	APlayerController* GetRunPlayerController() const;
	// 플레이어가 지금 가진 아이템(ItemId·개수). 팀 인벤토리(PlayerState 의 UInventoryComponent) → 검증 폰 순으로 읽는다.
	void CollectPlayerItems(TArray<FPGItemStack>& OutItems) const;
	void ApplyRunToStashAndStats(const FPGRunRecord& Record);

	void OpenFlowLevel(EPGFlowScreen Screen);
	// 레벨 경로와 옵션. 설정 레벨이 없으면 Entry 맵 + ?game= 게임모드.
	bool ResolveFlowLevel(EPGFlowScreen Screen, FString& OutPackage, FString& OutOptions) const;

	void LoadStash();

	FPGRunRecord CurrentRun;
	FPGRunRecord LastRun;
	TArray<FPGItemStack> Stash;
	FPGPlayerStats Stats;
	TArray<FPGRunRecord> RunHistory;

	// 판이 열린 월드. 다른 월드로 바뀌면(레벨 이동) 판이 끝나지 않았어도 중단으로 친다.
	TWeakObjectPtr<UWorld> RunWorld;
	// 판이 끝난(또는 중단된) 월드. 같은 월드에서 곧바로 새 판을 열지 않으려고 기억한다 — 탈출 뒤 스코어보드로 넘어가기 전 3초 동안
	//   게임모드가 아직 AGameModePG 라서, 이게 없으면 "판 시작" 이 또 찍히고 떠날 때 "중단" 으로 버려졌다(9/22 로그).
	TWeakObjectPtr<UWorld> EndedRunWorld;
	double RunStartWorldSeconds = 0.0;
	bool bTitleLoginShown = false;
	bool bStashGivenThisRun = false;

	// 이동 거리·사망 감시용.
	TWeakObjectPtr<APawn> TrackedPawn;
	FVector LastPawnLocation = FVector::ZeroVector;
	TWeakObjectPtr<UAbilitySystemComponent> WatchedAsc;
	FDelegateHandle HealthWatchHandle;

	FTSTicker::FDelegateHandle TickHandle;
	FTimerHandle TravelTimer;

	// ---- 멀티(9/27): 사람마다 따로 ----
	// 왜: 이 서브시스템은 혼자 하는 판 기준이었다 — 기록은 "첫 번째 플레이어" 한 사람 것이고, 누가 탈출하든 판 전체가 끝나며,
	//   끝나면 서버가 맵을 바꿔(OpenLevel) 접속한 모두가 튕겼다. 창고도 서버 디스크에 저장됐다(멀티 점검 A1).
	// 멀티에서는: 서버가 사람마다 기록을 들고, 한 사람이 끝나면 그 사람 기록만 그 사람 컴퓨터로 보낸다(SendRunResult).
	//   서버는 맵을 바꾸지 않는다 — 다른 사람은 계속 싸운다. 혼자 하는 판(NM_Standalone)은 예전 그대로다.
	struct FPlayerRun
	{
		FPGRunRecord Record;
		TWeakObjectPtr<APawn> Tracked;
		FVector LastLocation = FVector::ZeroVector;
		TWeakObjectPtr<UAbilitySystemComponent> Asc;
		FDelegateHandle HealthHandle;
	};
	TMap<TWeakObjectPtr<APlayerController>, FPlayerRun> PlayerRuns;
	bool IsNetRun() const;
	// 폰(또는 그 사람이 탄 탈것)의 주인. 없으면 nullptr.
	APlayerController* ResolveRunPlayer(const APawn* Pawn) const;
	FPlayerRun* FindOpenPlayerRun(APlayerController* PC);
	void TickNetRun(float DeltaSeconds);
	void WatchPlayerPawn(APlayerController* PC, FPlayerRun& Run, APawn* Pawn);
	void UnwatchPlayer(FPlayerRun& Run);
	void HandleNetHealthChanged(const FOnAttributeChangeData& Data, TWeakObjectPtr<APlayerController> PC);
	void FinishPlayerRun(APlayerController* PC, EPGRunResult Result, const FText& How);
	void CollectItemsFor(const APlayerController* PC, TArray<FPGItemStack>& OutItems) const;
};
