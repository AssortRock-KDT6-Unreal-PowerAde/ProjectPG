// 문 원형 (노션 "문 원형", OBJ-012 ~ OBJ-024, 탈출용 문 OBJ-021/022/063/064).
//
// 외여닫이·양문·미닫이·셔터·차고문·펜스 게이트·해치가 전부 이 클래스다.
// 차이는 Motion(연출 방식)과 메시, 잠금 값뿐이다.
// 탈출용 문은 "문 + 탈출구 원형(APGExtractionZoneActor)을 옆에 배치"로 만든다.
//
// 장치 연동: 문 제어 패널·스위치가 IPGDeviceSignalTarget::OnDeviceSignal로 열고 닫는다.
// 퀘스트 연동: OnDoorUsed 델리게이트 + QuestTag로 "특정 문 이용 목표(OBJ-093)"를 잡는다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "Objects/PGDeviceSignalInterface.h"
#include "PGDoorActor.generated.h"

class UPGLockComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPGDoorUsedSignature, APGDoorActor*, Door, APawn*, Pawn, bool, bOpened);

UCLASS()
class PROJECTPG_API APGDoorActor : public APGInteractableActorBase, public IPGDeviceSignalTarget
{
	GENERATED_BODY()

public:
	APGDoorActor();

	virtual void Tick(float DeltaTime) override;
	// 레벨·블루프린트에 저장된 문은 태그 목록을 따로 저장해 생성자 태그가 빠질 수 있다 — 생길 때마다 "통째로 부서짐" 태그를 다시 단다.
	virtual void PostInitializeComponents() override;
	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;
	// 멀티(9/27): 표 줄 겉모습을 서버·클라 모두에서 입힌다(APGInteractableActorBase::ApplyCatalogLook).
	virtual void ApplyCatalogLook() override;

	// 서버 전용. 잠금은 무시하고 연다(장치·퀘스트용). 플레이어 상호작용은 Interact를 쓴다.
	UFUNCTION(BlueprintCallable, Category = "PG|Door")
	void SetOpen(bool bNewOpen, APawn* InstigatorPawn = nullptr);

	UFUNCTION(BlueprintCallable, Category = "PG|Door")
	bool IsOpen() const { return bIsOpen; }

	UFUNCTION(BlueprintCallable, Category = "PG|Door")
	UPGLockComponent* GetLock() const { return LockComponent; }

	UFUNCTION(BlueprintCallable, Category = "PG|Door")
	void SetMotion(EPGDoorMotion NewMotion) { Motion = NewMotion; }

	// IPGDeviceSignalTarget
	virtual void OnDeviceSignal_Implementation(bool bOn, AActor* Source) override;

	UPROPERTY(BlueprintAssignable, Category = "PG|Door")
	FPGDoorUsedSignature OnDoorUsed;

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_IsOpen();

	// 닫을 때 문짝 자리에 폰이 서 있으면 닫지 않는다 ("막힘 확인").
	bool IsBlockedForClosing() const;

	void ApplyLeafTransform(float Alpha);

protected:
	// 문짝 A는 회전축(PivotA)에 붙어 있어 힌지 기준으로 돈다. 미닫이·셔터는 Pivot 자체를 이동한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Door")
	TObjectPtr<USceneComponent> PivotA;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Door")
	TObjectPtr<UStaticMeshComponent> LeafA;

	// 양문형에서만 쓴다. 메시가 없으면 무시된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Door")
	TObjectPtr<USceneComponent> PivotB;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Door")
	TObjectPtr<UStaticMeshComponent> LeafB;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Door")
	TObjectPtr<UPGLockComponent> LockComponent;

	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다. 미닫이·셔터가 클라에서 회전문으로 열리던 것.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Door")
	EPGDoorMotion Motion = EPGDoorMotion::SwingSingle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Door")
	float SwingAngle = 95.0f;

	// 미닫이 이동 거리(cm). 보통 문짝 폭.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Door")
	float SlideDistance = 100.0f;

	// 셔터·차고문 상승 거리(cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Door")
	float VerticalDistance = 220.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Door", meta = (ClampMin = "0.0"))
	float AnimSeconds = 0.7f;

	// 열린 뒤 자동으로 닫히기까지(초). 0이면 자동 닫힘 없음. 차고문·셔터는 길게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Door", meta = (ClampMin = "0.0"))
	float AutoCloseSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Door")
	bool bCheckBlockedBeforeClose = true;

	UPROPERTY(ReplicatedUsing = OnRep_IsOpen, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Door")
	bool bIsOpen = false;

	float AnimElapsed = 0.0f;
	float AnimFrom = 0.0f;
	float AnimTo = 0.0f;
	FTimerHandle AutoCloseTimer;
};
