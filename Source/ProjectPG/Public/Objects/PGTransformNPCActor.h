// 변신 여고생 NPC: 연료통을 건네면 그 자리에서 스포츠카로 변신한다. (2026-09-20)
//
// 흐름(사용자 결정 9/20): 스타터 지역에 연료통과 이 NPC 를 같이 둔다 → 연료통을 F 로 주우면 인벤토리에 들어간다
//   → 이 NPC 에게 F = 연료통 하나를 건네고(ConsumeItem) → 변신 연출 → 같은 자리에 탈 수 있는 스포츠카(APGVehiclePawn)가 생긴다.
// "건네기"는 인벤토리 약속(IPGItemReceiver: HasItem/ConsumeItem)만 쓴다. 인벤토리가 팀 것이든 검증 캐릭터 것이든 그대로 돈다.
// 겉모습(여고생 스켈레탈 메시·대기/변신 애니메이션)은 모델링 세션(D:\AI\transform)이 만든 것을 소프트 경로로 끼운다.
// 아직 없으면 스포츠카 메시를 세워 두고(자리 표시) 연출 없이 바꾼다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "PGTransformNPCActor.generated.h"

class UAnimSequence;
class UCapsuleComponent;
class UParticleSystem;
class UStaticMesh;
class UMaterialInterface;
class USkeletalMesh;
class USkeletalMeshComponent;

UCLASS()
class PROJECTPG_API APGTransformNPCActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	APGTransformNPCActor();

	// 차에서 돌아올 때: 변신 애니를 거꾸로 돌려 "차가 도로 여고생이 되는" 그림을 만든다.
	void SetPlayRevertIntro(bool bValue) { bPlayRevertIntro = bValue; }

	// 이미 연료를 받은 상태로 세운다(차에서 돌아온 여고생). 이 뒤로는 F 한 번이면 바로 변신한다.
	void SetAlreadyFueled(bool bValue) { bAlreadyFueled = bValue; }
	bool IsAlreadyFueled() const { return bAlreadyFueled; }

	virtual void BeginPlay() override;

	// 변신하려면 필요한 아이템과 개수
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	FName RequiredItemId = TEXT("Fuel");

	// 변신 뒤에 생기는 탈것. 기본은 APGVehiclePawn(기본 메시 = 빨간 SportsCar).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSubclassOf<APawn> VehicleClass;

	// 변신 연출 길이(초). 변신 애니메이션이 있으면 그 길이를 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform", meta = (ClampMin = "0.1"))
	float TransformSeconds = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	// 애니메이션이 뼈를 키워 놓은 경우까지 잡는다(메시는 멀쩡한데 재생하면 커지는 경우).
	// 여고생을 지키는 울타리. 차·탱크·몬스터·잔해는 막고, 플레이어만 지나다닌다.
	// 밟혀 사라지면 시작 동선이 끊기는데, 그렇다고 밟히지 않는 척하면 어색하다 — 눈에 안 보이는 울타리로 막는다(사용자 9/20).
	void SetupBarrier();
	// 연료통이 차에 치여 사라지면 변신을 할 수가 없다. 주기적으로 보고 없으면 다시 놓는다.
	void EnsureFuelNearby();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TObjectPtr<class UCapsuleComponent> Barrier;
	FTimerHandle FuelCheckTimer;
	FTimerHandle DrinkTimer;
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> BarrierProps;
	// 툰 외곽선: 같은 메시를 살짝 부풀린 껍데기에 검은 언릿 재질을 씌워 뒷면만 그린다.
	// VRoid 모델은 툰 셰이더용이라 외곽선이 없으면 사실적인 배경 위에서 흐릿하게 뜬다(사용자 9/20).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TObjectPtr<USkeletalMeshComponent> BodyOutline;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<UMaterialInterface> OutlineMaterial;
	// 외곽선을 쓸지. 재질 자체는 제대로 동작하지만(Tools/make_girl_outline.py, 연결 8개 확인),
	// 얼굴 세부 메시(입·속눈썹·눈)까지 껍데기가 부풀어 입가에 검은 자국이 생긴다(9/20 PIE).
	// 제대로 하려면 슬롯별로 두께를 0 으로 빼야 한다 — 그때까지는 꺼 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	bool bUseOutline = false;
	// 연료를 이미 받았나. 연료통은 한 번만 먹인다(사용자 결정 9/20) — 차가 여고생으로 돌아올 때 켜진다.
	// 멀티(9/27): 복제한다 — 차에서 돌아온 여고생은 서버가 세우는데, 클라이언트도 "이미 받았다" 를 알아야 안내 문구가 맞다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Transform")
	bool bAlreadyFueled = false;

	// 눈에 보이는 방어막 반구(막는 일은 Barrier 캡슐이 한다).
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Shield;

	// 마시는 동작이 "끝났을 때" 불린다. 여기서 변신으로 넘어간다.
	// 이름이 BeginDrink 이던 시절 "마시기 시작"으로 잘못 읽혀 연료통을 여기서 붙였고,
	// 그래서 통이 0초만 손에 있었다(9/20). 이름으로 자리를 헷갈리지 않게 Finish 로 바꿨다.
	void FinishDrink();
	// 마시는 동안 두 손에 쥘 연료통. 없으면 허공에 대고 마시는 것으로 보인다.
	void AttachFuelCan();
	void DetachFuelCan();

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> HeldCan;
	FTimerHandle FuelCanTimer;

	// 통이 손에 생기는 시점(마시기 21프레임 = 0.7초, "받는 순간"). 그 자세를 재서 쥐는 위치를 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform", meta = (ClampMin = "0.0"))
	float FuelCanReceiveSeconds = 0.7f;
	// 두 손 가운데에서 통을 얼마나 더 내려 쥐게 할지(cm). 0 이면 통 무게중심이 정확히 두 손 가운데다.
	// 8cm 내리면 마시는 순간 주둥이가 입에서 10cm → 6cm 로 붙는다(9/20 헤드리스 계산). 눈으로 맞출 자리.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	float FuelCanDropCm = 8.0f;
	// 거꾸로 변신 연출을 시작하고, 끝나면 평소 대기 자세로 돌아간다.
	void BeginRevertIntro();
	void EndRevertIntro();
	void VerifyPlayedHeight();
	FTimerHandle HeightCheckTimer;

	// 여고생 키(cm). 메시 단위가 어긋나 들어오면 이 키로 맞춘다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform", meta = (ClampMin = "50.0"))
	float TargetHeightCm = 161.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<UAnimSequence> TransformAnim;
	// 연료통을 건네받아 마시는 동작(약 3초). 이게 끝나고 변신이 시작된다 — 건네자마자 변신하면 너무 급작스럽다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<UAnimSequence> DrinkAnim;

	// 변신 둘째 절반: 스포츠카를 25 조각으로 나눈 스켈레탈 메시가 튀어나와 조립된다. 쉬는 자세 = 완성된 차(원점·방향·크기가 진짜 차와 같다).
	// 여고생 애니와 같은 자리에서 동시에 틀고, 마지막 프레임에 진짜 탈것으로 바꾼다(모델링 세션 README "변신 재생 방법").
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<USkeletalMesh> PanelsMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<UAnimSequence> PanelsAnim;

	// 변신해서 나온 차의 차체 색(남색 + 빨간 리본 줄). 팩 원본 재질은 그대로 두고 이 차에만 덮어쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TSoftObjectPtr<UMaterialInterface> VehicleBodyMaterial;

	// 탈것 메시에서 차체 재질 슬롯 이름(SK_SportsCar 는 M_Exterior).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	FName VehicleBodySlot = TEXT("M_Exterior");

	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGTransformNPC 에서 바꾼다. 기본값 = 원래 코드에 적혀 있던 에셋) ----
	// 여고생 모델이 없을 때 대신 세워 두는 자리 표시(변신 결과인 스포츠카).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Transform|Visual")
	TSoftObjectPtr<USkeletalMesh> PlaceholderMesh;

	// 방어막 반구 모양과 은은한 빛 재질(가산 재질 — 어두울수록 투명). 밝기는 ShieldBrightness.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Transform|Visual")
	TSoftObjectPtr<UStaticMesh> ShieldMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Transform|Visual")
	TSoftObjectPtr<UMaterialInterface> ShieldMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Transform|Visual")
	float ShieldBrightness = 0.18f;

	// 마시는 동안 두 손에 쥐는 연료통.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Transform|Visual")
	TSoftObjectPtr<UStaticMesh> HeldCanMesh;

	// 변신 애니메이션이 없을 때 대신 터뜨리는 먼지 한 번.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Transform|Visual")
	TSoftObjectPtr<UParticleSystem> NoAnimDustFx;

	// 여고생을 세울 클래스. 설정(ProjectPG Visuals > Transform NPC Class)에 블루프린트가 있으면 그것, 없으면 이 C++ 클래스.
	// 세우는 곳(시작 지점·차에서 돌아옴·인트로·콘솔)이 모두 이걸 쓴다 — 한 곳만 블루프린트를 쓰면 모습이 곳마다 달라진다.
	static UClass* GetSpawnClass();

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;

	void FinishTransform();

	// ---- 멀티(9/27): 연출은 모든 컴퓨터에서, 판정·차 만들기는 서버에서 ----
	// 전에는 마시기·변신 연출(애니·연료통·차 조각·방어막 치우기)이 서버에서만 돌아, 클라이언트 화면에서는 여고생이
	//   가만히 서 있다가 갑자기 사라지고 차만 생겼다(9/27 PIE: "변신 중" 만 뜨고 변신이 안 보였다).
	// 서버가 판정한 뒤 이 두 함수로 "지금 이 연출을 틀어라" 를 모두에게 보낸다(서버 자신도 같이 튼다).
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayDrink();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPlayTransform();
	// 변신 연출 길이 — 서버 타이머(FinishTransform)와 연출이 같은 값을 쓰게 한 곳에서 잰다.
	float GetTransformDuration() const;
	// 울타리를 플레이어만 지나가게: 새로 생긴 캐릭터(늦게 들어온 사람·다시 태어난 사람)까지 1초마다 챙긴다.
	void RefreshBarrierIgnores();
	FTimerHandle BarrierIgnoreTimer;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// F 트레이스(Visibility)와 몸 충돌을 받는 캡슐. 스켈레탈 메시는 보이기만 한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TObjectPtr<UCapsuleComponent> Capsule;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TObjectPtr<USkeletalMeshComponent> Body;

	// 조립되는 차 조각들. 변신할 때만 보인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	TObjectPtr<USkeletalMeshComponent> Panels;

	bool bTransforming = false;
	bool bDrinking = false; // 마시는 동작 재생 중(끝나면 변신)
	// 생기자마자 변신 애니를 거꾸로 한 번 돌린다. 복제한다(9/27 멀티) — 서버가 세우기 전에 켜 두므로 클라이언트 BeginPlay 에서도 보인다.
	UPROPERTY(Replicated)
	bool bPlayRevertIntro = false;
	FTimerHandle RevertIntroTimer;
};
