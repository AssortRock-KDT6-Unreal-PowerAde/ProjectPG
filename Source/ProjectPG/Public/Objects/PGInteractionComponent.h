// 상호작용 컴포넌트 (폰에 붙인다). 노션 기준 "상호작용 담당"의 영역이지만,
// 오브젝트를 실제로 눌러 볼 수 있어야 하므로 최소 구현을 오브젝트 쪽에서 제공한다.
// 팀의 상호작용 담당이 자기 구현을 가져오면 이 컴포넌트는 그대로 버려도 된다.
// 오브젝트는 IInteractable / UPGLootableComponent 로만 말하므로 영향이 없다.
//
// 흐름:
//  1. 소유 폰(로컬)에서 일정 간격으로 시선 방향 스윕 → 가장 가까운 대상과 안내 문구 갱신
//  2. F 누름 → BeginInteract. 대상의 InteractSeconds가 0이면 즉시 서버 RPC,
//     아니면 유지 시간을 재다가 채워지면 서버 RPC. 떼면 취소.
//  3. 서버 RPC(ServerInteract)가 거리·유효성을 다시 검사한 뒤 대상의 Interact를 부른다.
//     상태 변경은 전부 서버에서 일어난다.
//
// 겹친 대상 (기획 v0.4 3.3.7): 상자 위에 총이 놓여 있거나 아이템이 뭉쳐 있으면 시선 중앙 하나만으로는 원하는 걸 못 고른다.
//  시선이 닿은 지점 둘레(CandidateRadius)의 상호작용 대상을 전부 후보로 모으고, 마우스 휠(CycleTarget)로 고른다.
//  고르는 건 전부 로컬이다. 서버 RPC 는 원래부터 "어느 대상인지"를 인자로 받으므로 네트워크 쪽은 바뀌는 게 없다.
//  UI 는 OnCandidatesChanged 로 목록과 선택 번호를 받아 그리면 된다(후보 1개면 목록을 안 그려도 됨).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PGInteractionComponent.generated.h"

class UPGLootableComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGInteractionTargetChanged, AActor*, Target, const FText&, Prompt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPGInteractionHoldProgress, float, Progress01);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGInteractionCandidatesChanged, const TArray<AActor*>&, Candidates, int32, SelectedIndex);

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 입력 담당이 F 눌림/뗌에 연결한다.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	void BeginInteract();
	// 같은 누름이 두 군데(팀 입력 표 + 컨트롤러 예비 바인딩)에서 들어와도 한 번만 처리한다.
	double LastBeginInteractTime = -100.0;

	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	void EndInteract();

	// 대상 갱신을 지금 강제로 한다 (테스트·UI용).
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	AActor* RefreshTarget();

	// 시점을 직접 주는 버전. RefreshTarget 이 폰의 카메라(없으면 눈높이)로 이걸 부른다. 테스트·AI 용으로도 쓴다.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	AActor* RefreshTargetFromView(const FVector& Start, const FVector& Direction);

	// 입력 담당이 마우스 휠에 연결한다. +1 다음 / -1 이전. 후보가 1개 이하면 아무 일도 없다.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	void CycleTarget(int32 Direction);

	// 지금 고를 수 있는 대상들(0번 = 시선이 직접 닿은 것, 나머지는 시선에 가까운 순)과 선택 번호.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	TArray<AActor*> GetCandidates() const;

	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	// 목록 UI 의 한 줄. 범위를 벗어나면 빈 글자.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	FText GetCandidatePrompt(int32 Index) const;

	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	AActor* GetCurrentTarget() const { return CurrentTarget; }

	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	FText GetCurrentPrompt() const { return CurrentPrompt; }

	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	bool IsHolding() const { return bHolding; }

	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	float GetHoldProgress() const { return HoldRequired > 0.0f ? FMath::Clamp(HoldElapsed / HoldRequired, 0.0f, 1.0f) : 0.0f; }

	// 서버 전용 직접 호출 (AI·테스트). 클라이언트 경로는 BeginInteract → ServerInteract.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	bool InteractWith(AActor* Target);

	// 지금 보고 있는 상자·시체의 Index 번째 칸 하나만 집는다. 클라이언트에서 불러도 서버로 간다.
	UFUNCTION(BlueprintCallable, Category = "PG|Interaction")
	void TakeItemFromTarget(int32 Index);

	UPROPERTY(BlueprintAssignable, Category = "PG|Interaction")
	FPGInteractionTargetChanged OnTargetChanged;

	UPROPERTY(BlueprintAssignable, Category = "PG|Interaction")
	FPGInteractionHoldProgress OnHoldProgress;

	// 후보 목록이나 선택이 바뀔 때.
	UPROPERTY(BlueprintAssignable, Category = "PG|Interaction")
	FPGInteractionCandidatesChanged OnCandidatesChanged;

protected:
	UFUNCTION(Server, Reliable)
	void ServerInteract(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerBeginUse(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerEndUse(AActor* Target);

	UFUNCTION(Server, Reliable)
	void ServerTakeItem(AActor* Target, int32 Index);

	// 멀티(9/27): 서버가 거절한 이유를 그 사람 화면에 띄운다(서버에서 Announce 하면 서버 화면에만 뜬다).
	UFUNCTION(Client, Reliable)
	void ClientInteractRefused(const FText& Reason);

	// 서버: 거절한 요청이 잡아 둔 "사용 중" 잠금을 푼다(안 풀면 다른 사람에게 판 끝까지 "사용 중").
	void ReleaseUseAfterReject(AActor* Target);

	// 시선 스윕이 닿은 지점 둘레의 상호작용 대상을 모은다. 0번 = 직접 닿은 것, 나머지는 시선(직선)에 가까운 순.
	void GatherCandidates(const FVector& Start, const FVector& Direction, TArray<AActor*>& OutCandidates) const;
	// 탈것에 타서 조종을 놓았을 때: 탄 탈것의 문구(IPGRideable)로 바꾸거나 비운다.
	void RefreshRiderPrompt(const APawn* Pawn);
	bool IsTargetInRange(const AActor* Target) const;
	static bool ResolveInteractable(const AActor* Target, const APawn* Pawn, FText& OutPrompt, float& OutHoldSeconds, bool& bOutCanInteract);

	// 시선 스윕 길이(cm)와 두께.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Interaction", meta = (ClampMin = "0.0"))
	float TraceDistance = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Interaction", meta = (ClampMin = "0.0"))
	float TraceRadius = 20.0f;

	// 시선이 닿은 지점에서 이 반경 안의 대상이 휠 후보가 된다. 0 이면 예전처럼 시선 중앙 하나만.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Interaction", meta = (ClampMin = "0.0"))
	float CandidateRadius = 160.0f;

	// 시선이 빗나가도 이 반경 안에 있으면 후보로 넣는다(내 몸 기준).
	// 9/20 PIE: 연료통을 정확히 쳐다봐야만 잡혀서 "여러 번 눌러야 되는 느낌"이었다.
	// 사용자 요구: "캐릭터가 겹칠 쯤이면 그냥 먹어지게".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Interaction")
	float BodyReachRadius = 220.0f;

	// 대상 갱신 간격(초). 매 프레임 스윕하면 낭비다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Interaction", meta = (ClampMin = "0.0"))
	float RefreshInterval = 0.1f;

	// 서버가 RPC를 받아들일 최대 거리. 트레이스보다 조금 넉넉하게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Interaction", meta = (ClampMin = "0.0"))
	float ServerAcceptDistance = 450.0f;

	UPROPERTY(VisibleInstanceOnly, Category = "PG|Interaction")
	TObjectPtr<AActor> CurrentTarget;

	UPROPERTY(VisibleInstanceOnly, Category = "PG|Interaction")
	FText CurrentPrompt;

	UPROPERTY(VisibleInstanceOnly, Category = "PG|Interaction")
	TArray<TObjectPtr<AActor>> Candidates;

	int32 SelectedIndex = INDEX_NONE;
	// 휠로 고른 대상. 후보 순서는 움직일 때마다 바뀌므로 번호가 아니라 액터로 기억한다. 후보에서 빠지면 잊는다.
	TWeakObjectPtr<AActor> PreferredTarget;

	float RefreshAccumulated = 0.0f;
	bool bHolding = false;
	float HoldElapsed = 0.0f;
	float HoldRequired = 0.0f;
	TWeakObjectPtr<AActor> HoldTarget;
};
