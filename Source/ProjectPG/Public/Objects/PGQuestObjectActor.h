// 퀘스트 오브젝트 원형 (노션 "퀘스트 오브젝트 원형", OBJ-086 ~ OBJ-095).
//
//  - 조사 대상 물체(OBJ-088):   Mode = Investigate, InteractSeconds 동안 F 유지
//  - 작동 장치(OBJ-089):        Mode = Operate (켜기/끄기 이벤트)
//  - 아이템 납품함(OBJ-090):    Mode = DeliverItem, RequiredItemId × RequiredCount 원자적 전달
//  - NPC 전달 지점(OBJ-091):    DeliverItem을 NPC 액터 옆에 배치
//  - 위치 방문 Trigger(OBJ-092): APGQuestTriggerVolume (플레이어별 1회)
//  - 퀘스트 아이템(OBJ-086):    바닥 아이템 원형 + QuestTag
//  - 특정 문/차량 이용(OBJ-093/094): 문·탈출구의 델리게이트 + QuestTag
//  - 목표 표시기(OBJ-095):       UI 담당 (여기서는 위치·태그만 제공)
//
// 이 클래스는 조건을 검사하고 OnQuestEvent(QuestTag, Source, Pawn)만 보낸다.
// 퀘스트 진행도·보상은 퀘스트 담당이 구독해서 처리한다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "GameFramework/Actor.h"
#include "PGQuestObjectActor.generated.h"

class UBoxComponent;

UCLASS()
class PROJECTPG_API APGQuestObjectActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	APGQuestObjectActor();

	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;

	UFUNCTION(BlueprintCallable, Category = "PG|Quest")
	void Configure(EPGQuestObjectMode InMode, FName InQuestTag, FName InRequiredItemId = NAME_None, int32 InRequiredCount = 1, bool bInOncePerPlayer = true);

	UFUNCTION(BlueprintCallable, Category = "PG|Quest")
	bool HasCompleted(const APawn* Pawn) const;

	UFUNCTION(BlueprintCallable, Category = "PG|Quest")
	int32 GetCompletionCount() const { return CompletedPlayerKeys.Num(); }

	UPROPERTY(BlueprintAssignable, Category = "PG|Quest")
	FPGQuestEventSignature OnQuestEvent;

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Quest")
	EPGQuestObjectMode Mode = EPGQuestObjectMode::Investigate;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Quest")
	FName RequiredItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Quest", meta = (ClampMin = "1"))
	int32 RequiredCount = 1;

	// 플레이어마다 한 번만 완료되는지. 납품함은 보통 true.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Quest")
	bool bOncePerPlayer = true;

	// Operate 모드의 현재 상태.
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Replicated, Category = "PG|Quest")
	bool bOperated = false;

	// 서버에서만 유지. 완료한 플레이어 키.
	TSet<FName> CompletedPlayerKeys;
};

// 위치 방문 트리거 (OBJ-092). 영역에 들어오면 플레이어별 한 번 OnQuestEvent를 보낸다.
UCLASS()
class PROJECTPG_API APGQuestTriggerVolume : public AActor
{
	GENERATED_BODY()

public:
	APGQuestTriggerVolume();

	virtual void BeginPlay() override;

	UFUNCTION(BlueprintCallable, Category = "PG|Quest")
	void SetQuestTag(FName InTag) { QuestTag = InTag; }

	UFUNCTION(BlueprintCallable, Category = "PG|Quest")
	FName GetQuestTag() const { return QuestTag; }

	// 서버 전용. Overlap이 부르지만 테스트에서 직접 불러도 된다. 처음 들어온 플레이어면 true.
	UFUNCTION(BlueprintCallable, Category = "PG|Quest")
	bool NotifyPawnEntered(APawn* Pawn);

	UPROPERTY(BlueprintAssignable, Category = "PG|Quest")
	FPGQuestEventSignature OnQuestEvent;

protected:
	UFUNCTION()
	void OnVolumeBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Quest")
	TObjectPtr<UBoxComponent> Volume;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Quest")
	FName QuestTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Quest")
	bool bOncePerPlayer = true;

	TSet<FName> EnteredPlayerKeys;
};
