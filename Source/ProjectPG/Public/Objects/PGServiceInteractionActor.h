// 서비스 상호작용 원형 (노션 "서비스 상호작용 원형", OBJ-078 ~ OBJ-085).
//
// 상점 NPC / 상점 창구 / 택배 NPC / 택배 접수 창구가 이 클래스다.
// "상점 사용 위치"(OBJ-080/083)는 별도 오브젝트가 아니라 이 액터의 UsePoint 컴포넌트다:
// 플레이어가 UsePoint에서 MaxUseDistance 안, MaxUseAngle 안에 서 있어야 상호작용된다.
//
// 이 액터는 "거래 화면을 열어 달라"는 요청(OnServiceRequested)만 보낸다.
// 가격·재고·배송비·우편 생성은 상점/우편 담당의 책임이다.
// 제조(Craft)는 노션에서 제외였다가 9/20 사용자가 부스 4종(상점·택배·제조·교환소)으로 재승인했다.
// 거래소 부스(APGBoothActor)가 이 액터를 창구 앞에 세우고 ConfigureUseArea 로 서는 자리·겨누는 자리를 맞춘다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "PGServiceInteractionActor.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPGServiceSignature, APGServiceInteractionActor*, Service, APawn*, Pawn, EPGServiceKind, Kind);

UCLASS()
class PROJECTPG_API APGServiceInteractionActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	APGServiceInteractionActor();

	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;

	UFUNCTION(BlueprintCallable, Category = "PG|Service")
	EPGServiceKind GetServiceKind() const { return ServiceKind; }

	UFUNCTION(BlueprintCallable, Category = "PG|Service")
	void SetServiceKind(EPGServiceKind InKind) { ServiceKind = InKind; }

	// 거리·각도 판정. 사용 위치 컴포넌트 기준.
	UFUNCTION(BlueprintCallable, Category = "PG|Service")
	bool IsPawnAtUsePoint(const APawn* Pawn) const;

	// 서버 전용. UI 담당이 화면을 닫을 때 부른다. 사용 중 잠금이 풀린다.
	UFUNCTION(BlueprintCallable, Category = "PG|Service")
	void EndService(APawn* Pawn);

	// 멀티(9/27): 사용자가 창구를 떠나면(또는 사라지면) 잠금을 푼다. 아직 창구 화면이 없어 EndService 를 부르는 곳이 없었고,
	//   처음 F 를 누른 사람이 판 끝까지 창구를 잠갔다(다른 사람은 "사용 중"). 1초마다 본다.
	void CheckServiceUserStillHere();
	FTimerHandle ServiceUserCheckTimer;

	// 부스용: 손님이 서는 자리(UseWorld)와 F 로 겨누는 창구 자리(TargetWorld)를 월드 좌표로 맞춘다.
	void ConfigureUseArea(const FVector& UseWorld, const FVector& TargetWorld);

	UPROPERTY(BlueprintAssignable, Category = "PG|Service")
	FPGServiceSignature OnServiceRequested;

	UPROPERTY(BlueprintAssignable, Category = "PG|Service")
	FPGServiceSignature OnServiceEnded;

	// 교환소 조건을 사람이 읽는 한 줄로("드래곤 비늘 1개 → 분홍 단발 가발"). 부스 안내(APGBoothActor)가 쓴다(9/23).
	FText GetExchangeSummary() const;

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 멀티(9/27): 부스가 정한 사용 자리(서버)를 클라이언트에도. 컴포넌트 위치는 복제되지 않아 클라에선 창구가 1m 높고 1.5m 비껴 있었다.
	UPROPERTY(ReplicatedUsing = OnRep_UseArea)
	FVector ReplicatedUseWorld = FVector::ZeroVector;
	UPROPERTY(ReplicatedUsing = OnRep_UseArea)
	FVector ReplicatedTargetWorld = FVector::ZeroVector;
	UPROPERTY(Replicated)
	bool bUseAreaSet = false;
	UFUNCTION()
	void OnRep_UseArea();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Service")
	TObjectPtr<USceneComponent> UsePoint;

	// F 로 겨눌 수 있는 보이지 않는 상자(Visibility 만 막음). 메시 없는 창구(부스)도 겨눌 수 있게. 사람·차는 통과한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Service")
	TObjectPtr<class UBoxComponent> InteractBox;

	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다. 교환·제조·택배 창이 클라에서 모두 "거래하기" 로 뜨던 것.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Service")
	EPGServiceKind ServiceKind = EPGServiceKind::Shop;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Service", meta = (ClampMin = "0.0"))
	float MaxUseDistance = 250.0f;

	// 교환소 조건(ServiceKind == Exchange 일 때만): Cost 를 Count 개 내면 Reward 하나를 준다. 화면 없이 F 한 번에 바로 바꾼다.
	// 9/20 에는 드래곤 전리품이 없어 고철 3개 → 분홍 가발로 시험했다. 9/23: 드래곤에게 가장 큰 피해를 준 사람이 받는
	//   드래곤 비늘(Dragon_Scale, APGDragonBoss::AwardTopDamageDealer) 1개 → 분홍 가발.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Service|Exchange")
	FName ExchangeCostItemId = TEXT("Dragon_Scale");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Service|Exchange", meta = (ClampMin = "1"))
	int32 ExchangeCostCount = 1;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Service|Exchange")
	FName ExchangeRewardItemId = TEXT("Wig_Pink");

	// UsePoint의 정면(X+)과 플레이어 방향 사이 허용 각도. 180이면 각도 무시.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Service", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float MaxUseAngleDeg = 180.0f;
};
