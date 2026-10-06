// 게임 시작(매칭) 담당 — 리슨 서버.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "SessionSubSystem.generated.h"

class FOnlineSessionSearch;

// 매칭이 지금 어디까지 왔나.
UENUM(BlueprintType)
enum class EMatchingState : uint8
{
	Idle,        // 안 하는 중
	Searching,   // 같은 네트워크의 방을 찾는 중
	Joining,     // 찾은 방에 들어가는 중
	Hosting,     // 방이 없어서 내가 방을 만드는 중(리슨 서버)
	Cancelled,   // 사용자가 취소함
	JoinFailed,  // 찾은 방에 못 들어감
	HostFailed,  // 방을 못 만듦
	Unavailable  // 온라인 기능이 없음(설정 문제)
};

// 상태만 알린다. 화면에 어떤 글자를 띄울지는 매칭 화면(WBP_Matching 의 StateTexts)이 정한다.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchingStateChanged, EMatchingState, State);

// 매칭 담당 (리슨 서버).
// 게임에서: 로비의 "게임 시작" → 같은 네트워크(LAN)에 열린 방이 있으면 들어가고, 몇 초 안에 못 찾으면
//           내가 방장이 되어(리슨 서버) 맵을 연다. 다른 사람이 게임 시작을 누르면 내 방을 찾아 들어온다.
// 왜 리슨 서버: 10/4 팀 합의. 전용 서버·웹 서버 없이 한 사람이 방장(서버 겸 플레이어)이 된다.
// 화면은 안 그린다 — 매칭 화면(UMatchingWidget)이 이 담당의 상태·진행률을 읽어 보여 준다.
// 방 찾기·만들기는 언리얼 온라인 서브시스템(Null = LAN)의 세션 기능을 쓴다.
UCLASS(Config = Game)
class PROJECTPG_API USessionSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static USessionSubSystem* Get(const UObject* WorldContext);

	// 게임 시작 버튼. 이미 매칭 중이면 무시.
	UFUNCTION(BlueprintCallable, Category = "Matching")
	void StartMatching();

	// 매칭 화면의 취소 버튼. 방 찾는 중에만 멈출 수 있다(들어가거나 방을 여는 중이면 이미 늦음).
	UFUNCTION(BlueprintCallable, Category = "Matching")
	void CancelMatching();

	UFUNCTION(BlueprintPure, Category = "Matching")
	EMatchingState GetState() const { return State; }

	// 0~1. 방 찾는 시간이 얼마나 지났나(매칭 화면 진행 막대). 들어가거나 방을 여는 중이면 1.
	UFUNCTION(BlueprintPure, Category = "Matching")
	float GetProgress() const;

	UPROPERTY(BlueprintAssignable, Category = "Matching")
	FOnMatchingStateChanged OnMatchingStateChanged;

private:
	IOnlineSessionPtr GetSessionInterface() const;
	void SetState(EMatchingState NewState);

	void BeginSearch();
	void HandleFindComplete(bool bWasSuccessful);
	void HandleJoinComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void BeginHost();
	void HandleCreateComplete(FName SessionName, bool bWasSuccessful);
	void HandleDestroyComplete(FName SessionName, bool bWasSuccessful);

	// 게임 맵(리슨 서버로 연다). DefaultGame.ini [/Script/ProjectPG.SessionSubSystem] 에서 바꾼다.
	UPROPERTY(Config)
	FString GameMapPath = TEXT("/Game/PG/LevelDesign/Tests/LD_MetaballGenerationTest");

	// 방 하나에 들어올 수 있는 사람 수(방장 포함).
	UPROPERTY(Config)
	int32 MaxPlayers = 4;

	// 방을 이만큼(초) 찾아보고 없으면 내가 방을 연다.
	UPROPERTY(Config)
	float SearchSeconds = 4.0f;

	EMatchingState State = EMatchingState::Idle;
	double SearchStartSeconds = 0.0;
	// 지난 판 방이 남아 있어 지우는 중이면, 다 지운 뒤 할 일(찾기).
	bool bSearchAfterDestroy = false;

	TSharedPtr<FOnlineSessionSearch> Search;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle CreateHandle;
	FDelegateHandle DestroyHandle;
};
