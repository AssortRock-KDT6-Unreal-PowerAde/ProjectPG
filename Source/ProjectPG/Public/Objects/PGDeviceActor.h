// 작동 장치 원형 (노션 "작동 장치 원형", OBJ-089, OBJ-096 ~ OBJ-107).
//
// 스위치·레버·버튼·발전기·차단기·퓨즈 박스·키패드·카드 리더기·문 제어 패널·경보 장치·
// 시간 제한 스위치·압력판이 전부 이 클래스다.
//  - Mode:           토글 / 누르는 동안만 / 작동 후 일정 시간 / 밟으면 작동
//  - RequiredItemId: 퓨즈·키카드·연료처럼 넣어야 작동하는 장치 (소모 여부 선택)
//  - LinkedTargets:  신호를 받을 액터들 (문, 다른 장치, 경보 표시). IPGDeviceSignalTarget 구현체.
//  - 키패드의 "코드 입력"은 UI 담당이 코드를 검증한 뒤 SetOn(true)를 부르는 방식으로 연결한다.
//
// 노션 기준 이 대분류는 대부분 "검토 필요 / P3"라서 카탈로그에서 bIncluded=false로 두고,
// 팀 승인 후 켠다. 코드 원형만 미리 준비해 둔 것이다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "Objects/PGDeviceSignalInterface.h"
#include "PGDeviceActor.generated.h"

class UBoxComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FPGDeviceStateSignature, APGDeviceActor*, Device, bool, bOn, APawn*, Pawn);

UCLASS()
class PROJECTPG_API APGDeviceActor : public APGInteractableActorBase, public IPGDeviceSignalTarget
{
	GENERATED_BODY()

public:
	APGDeviceActor();

	virtual void BeginPlay() override;
	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;

	UFUNCTION(BlueprintCallable, Category = "PG|Device")
	bool IsOn() const { return bIsOn; }

	// 서버 전용. 상태를 바꾸고 연결 대상에 신호를 보낸다.
	UFUNCTION(BlueprintCallable, Category = "PG|Device")
	void SetOn(bool bNewOn, APawn* InstigatorPawn = nullptr);

	UFUNCTION(BlueprintCallable, Category = "PG|Device")
	void SetMode(EPGDeviceMode NewMode) { Mode = NewMode; }

	UFUNCTION(BlueprintCallable, Category = "PG|Device")
	void AddLinkedTarget(AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "PG|Device")
	void SetRequiredItem(FName ItemId, bool bConsume);

	// 다른 장치의 신호를 받아 중계할 수 있다 (차단기 → 발전기 → 문).
	virtual void OnDeviceSignal_Implementation(bool bOn, AActor* Source) override;

	UPROPERTY(BlueprintAssignable, Category = "PG|Device")
	FPGDeviceStateSignature OnDeviceStateChanged;

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_IsOn();

	UFUNCTION()
	void OnPlateBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void OnPlateEndOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

	void BroadcastSignal(APawn* InstigatorPawn);

protected:
	// 압력판 전용 감지 영역. 다른 모드에서는 비활성.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Device")
	TObjectPtr<UBoxComponent> PlateTrigger;

	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Device")
	EPGDeviceMode Mode = EPGDeviceMode::Toggle;

	// Timed 모드에서 켜져 있는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Device", meta = (ClampMin = "0.0"))
	float TimedSeconds = 10.0f;

	// 작동에 필요한 아이템 (퓨즈, 키카드, 연료). 비어 있으면 필요 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Device")
	FName RequiredItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Device")
	bool bConsumeRequiredItem = true;

	// 한 번 아이템을 넣으면 그 뒤로는 아이템 없이 작동한다 (퓨즈 박스, 발전기 연료).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Device")
	bool bRequiredItemOnce = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Device")
	TArray<TObjectPtr<AActor>> LinkedTargets;

	UPROPERTY(ReplicatedUsing = OnRep_IsOn, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Device")
	bool bIsOn = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|Device")
	bool bRequiredItemSatisfied = false;

	FTimerHandle TimedHandle;
	int32 PlateOccupants = 0;
};
