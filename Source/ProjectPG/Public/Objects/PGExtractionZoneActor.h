// 탈출구 원형 (노션 "탈출구 원형", OBJ-057 ~ OBJ-068, 차량 출발·탈출 지점 OBJ-077).
//
// 일반 탈출 영역 / 대기 시간형 / 아이템 요구 / 플레이어별 / 제한시간 / 1회 사용 /
// 열쇠 탈출문 / 헬기·선박·자동차 탈출구가 전부 이 클래스다. 값만 다르다.
//
//  - 대기 시간형:   RequiredSeconds > 0
//  - 아이템 요구:   RequiredItemId 지정 (bConsumeRequiredItem으로 소모 여부)
//  - 플레이어별:    AllowedPlayerKeys에 사용 가능한 플레이어 키를 넣는다 (비면 전원)
//  - 제한시간:      ActiveFromSeconds / ActiveUntilSeconds (세션 시작 기준, -1이면 무시)
//  - 1회 사용:      MaxUses = 1
//  - 열쇠 탈출문:   문 원형(잠금)을 앞에 두고, 이 액터는 문 뒤에 배치
//  - 헬기·선박·차량: Trigger = Interact, RequiredItemId = 연료 등
//
// 탈출 성공 판정 자체는 게임플레이 담당이 OnExtractionCompleted를 구독해서 처리한다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "PGExtractionZoneActor.generated.h"

class UBoxComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGExtractionPawnSignature, APGExtractionZoneActor*, Zone, APawn*, Pawn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPGExtractionProgressSignature, APGExtractionZoneActor*, Zone, APawn*, Pawn, float, Progress01);

UCLASS()
class PROJECTPG_API APGExtractionZoneActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	APGExtractionZoneActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;
	// 멀티(9/27): 표 줄 겉모습을 서버·클라 모두에서 입힌다(APGInteractableActorBase::ApplyCatalogLook).
	virtual void ApplyCatalogLook() override;

	// 지금 이 폰이 이 탈출구를 쓸 수 있는가 (활성 시간, 플레이어별 허용, 남은 횟수, 필요 아이템).
	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	bool IsAvailableFor(const APawn* Pawn, FText& OutReason) const;

	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	bool IsActiveNow() const;

	// 서버 전용. 폰의 진행을 DeltaSeconds만큼 올린다. Tick이 부르지만 테스트에서도 직접 부른다.
	// 완료되면 true.
	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	bool AdvancePawn(APawn* Pawn, float DeltaSeconds);

	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	float GetProgress(const APawn* Pawn) const;

	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	int32 GetUsesRemaining() const { return MaxUses > 0 ? MaxUses - UsesConsumed : TNumericLimits<int32>::Max(); }

	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	void SetAllowedPlayerKeys(const TArray<FName>& Keys) { AllowedPlayerKeys = Keys; }

	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	void Configure(EPGExtractionTrigger InTrigger, float InRequiredSeconds, FName InRequiredItemId, bool bInConsume, int32 InMaxUses);

	// 영역 상자 크기·위치(액터 기준). 탈출 세트(펜스 뒤 넓은 영역 등)가 스폰 뒤에 맞춘다.
	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	void SetZoneBox(const FVector& LocalCenter, const FVector& Extent);
	// 탈출 방법 이름(스코어보드·안내문에 쓰인다). 카탈로그 행 없이 코드로 붙이는 탈출구(호수 배)가 쓴다.
	void SetExitDisplayName(const FText& InName) { DisplayName = InName; }
	// 스폰 직후(BeginPlay 전)에 켠다. 켜면 겹침 이벤트 대신 0.1초마다 상자 안을 직접 묻는다 — 차로 지나가는 EXIT 문·호숫가 배.
	void SetPollOccupants(bool bInPoll) { bPollOccupants = bInPoll; }
	// "가만히 버티기" 탈출: 시작하면 화면에 5·4·3·2·1 을 띄우고, 그동안 1m 넘게 움직이면 취소한다(9/22 사용자 결정).
	// 검문소 출구·세워 둔 헬기·차·호숫가 배가 모두 이 방식이다. 초는 RequiredSeconds 를 쓴다.
	void SetHoldStill(bool bInHoldStill) { bHoldStill = bInHoldStill; }

	// 가방(아이템)을 가진 폰. 사람이면 그 사람, 차·로봇이면 타고 있는 사람.
	static APawn* ResolveCarrier(const APawn* Pawn);

	// 폰이 영역에 들어옴/나감. Overlap 이벤트가 부르지만 테스트에서 직접 불러도 된다.
	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	void NotifyPawnEntered(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "PG|Extraction")
	void NotifyPawnLeft(APawn* Pawn);

	UPROPERTY(BlueprintAssignable, Category = "PG|Extraction")
	FPGExtractionPawnSignature OnExtractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "PG|Extraction")
	FPGExtractionPawnSignature OnExtractionCancelled;

	UPROPERTY(BlueprintAssignable, Category = "PG|Extraction")
	FPGExtractionPawnSignature OnExtractionCompleted;

	UPROPERTY(BlueprintAssignable, Category = "PG|Extraction")
	FPGExtractionProgressSignature OnExtractionProgress;

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnZoneBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnZoneEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void StartPawn(APawn* Pawn);
	void CompletePawn(APawn* Pawn);
	void CancelPawn(APawn* Pawn);
	float GetSessionSeconds() const;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Extraction")
	TObjectPtr<UBoxComponent> Zone;

	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Extraction")
	EPGExtractionTrigger Trigger = EPGExtractionTrigger::Overlap;

	// 영역 안에 이만큼 서 있어야 탈출. 0이면 즉시.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Extraction", meta = (ClampMin = "0.0"))
	float RequiredSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Extraction")
	FName RequiredItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Extraction")
	bool bConsumeRequiredItem = true;

	// 비어 있으면 모든 플레이어. 아니면 IPGItemReceiver::GetPlayerKey가 여기 있어야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Extraction")
	TArray<FName> AllowedPlayerKeys;

	// 세션 시작 후 몇 초부터 / 몇 초까지 활성인지. -1이면 제한 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Extraction")
	float ActiveFromSeconds = -1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Extraction")
	float ActiveUntilSeconds = -1.0f;

	// 0이면 무제한. 1이면 한 번 출발하면 닫힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Extraction", meta = (ClampMin = "0"))
	int32 MaxUses = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Extraction")
	int32 UsesConsumed = 0;

	// 서버에서만 유지되는 진행 상태. 진행률은 UI가 OnExtractionProgress로 받는다.
	TMap<TWeakObjectPtr<APawn>, float> Progress;
	TSet<TWeakObjectPtr<APawn>> PawnsInside;
	double SessionStartSeconds = 0.0;

	void PollOccupants();
	bool bPollOccupants = false;
	FTimerHandle PollTimer;
	TSet<TWeakObjectPtr<APawn>> PolledInside; // PollOccupants 가 넣은 폰(그 검사만 내보낸다)
	// 멀티(9/27): 안내 상태는 사람(폰)마다 따로. 예전에는 하나라서 두 사람이 동시에 서 있으면 숫자·거절 안내가 서로 지웠다.
	// 안내는 그 사람 화면으로 보낸다(UPGPlayerMessageComponent) — 서버에서 그리면 전용 서버에는 화면이 없어 아무도 못 봤다.
	TMap<TWeakObjectPtr<APawn>, double> LastRefusalAnnounceAt;

	bool bHoldStill = false;
	TMap<TWeakObjectPtr<APawn>, FVector> HoldStartSpot; // 카운트다운을 시작한 자리. 여기서 1m 넘게 벗어나면 취소
	TMap<TWeakObjectPtr<APawn>, int32> LastShownSecond; // 화면 숫자를 초가 바뀔 때만 고친다
	void ShowCountdown(APawn* Pawn, float Remaining);
	void ClearCountdown(APawn* Pawn, const FText& Why);
	// "움직여서 취소" 문구를 멈추는 순간 지우려고 0.1초마다 본다.
	void TickCancelNotice();
	TSet<TWeakObjectPtr<APawn>> CancelNoticePawns;
	FTimerHandle CancelNoticeTimer;
};
