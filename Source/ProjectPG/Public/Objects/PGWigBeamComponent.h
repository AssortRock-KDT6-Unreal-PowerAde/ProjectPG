// 분홍 단발 가발 광선. 가발(Wig_Pink)을 가지고 있으면 머리에 씌우고, 마우스 오른쪽으로 이마에서 강한 광선을 쏜다. (2026-09-20)
//
// 흐름(사용자 결정 9/20): 교환소에서 가발을 받는다 → 인벤토리에 가발이 있으면 머리에 보인다 → 오른쪽 클릭 = 광선
//   (피해 500, 재사용 10초, 맞은 자리 둘레의 소품·건물 조각도 부서진다). 왼쪽 클릭은 나중에 총 공격 자리로 남겨 둔다.
// "가지고 있으면 쓴다"로 한 이유: 팀 캐릭터의 장비창(EquipComponent)은 아직 장비 표(EquipTable) 줄이 없어 가발을 끼울 수 없다.
//   장비창이 생기면 IsWigWorn 만 "장착 칸에 있나"로 바꾸면 된다.
// 광선 출발점은 가발 메시의 이마 소켓(Forehead), 방향은 카메라가 보는 쪽(모델링 세션 권장 — 소켓 방향은 고개에 따라 흔들린다).
// 서버가 판정하고(피해·부수기), 모두의 화면에 광선 메시를 잠깐 보여 준다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "PGWigBeamComponent.generated.h"

class UMaterialInterface;
class UParticleSystem;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGWigBeamComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGWigBeamComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 입력(오른쪽 클릭)에서 부른다. 클라이언트면 서버로 넘긴다.
	UFUNCTION(BlueprintCallable, Category = "PG|WigBeam")
	void RequestFire();

	UFUNCTION(BlueprintCallable, Category = "PG|WigBeam")
	bool IsWigWorn() const;

	// 다시 쏠 수 있을 때까지 얼마나 찼나(0 = 방금 쐈다, 1 = 쏠 수 있다). 조준점 고리(UPGCrosshairWidget)가 읽는다(9/23).
	UFUNCTION(BlueprintCallable, Category = "PG|WigBeam")
	float GetReadyFraction() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	FName WigItemId = TEXT("Wig_Pink");

	// 맞은 것 하나하나에 주는 피해. 9/23: 500 → 5000 — 교환소에서 드래곤 비늘로만 얻는 보상이라 몬스터는 한 방에 쓰러진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	float Damage = 5000.0f;

	// 광선 굵기(반지름, cm). 이 굵기로 선을 따라 훑어 맞은 몸을 전부 뚫는다(9/23).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam", meta = (ClampMin = "1.0"))
	float PierceRadiusCm = 80.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	float CooldownSeconds = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	float Range = 30000.0f;

	// 맞은 자리 둘레에서 소품·건물 조각을 날리는 반지름(cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	float BlastRadius = 400.0f;

	// 광선이 화면에 남는 시간(초)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	float BeamVisibleSeconds = 0.6f;

protected:
	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize AimStart, FVector_NetQuantizeNormal AimDirection);

	// HitPoints: 광선이 뚫은 몸들의 자리 — 그 자리마다 터지는 효과를 낸다(9/23).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBeam(FVector_NetQuantize From, FVector_NetQuantize To, const TArray<FVector_NetQuantize>& HitPoints);

	// 커지면서 사라지는 불덩이 하나(연료통 폭발과 같은 방식). 화면마다(멀티캐스트) 만든다.
	void SpawnBlastBall(const FVector& Where, float RadiusCm) const;

	void FireAuthoritative(const FVector& AimStart, const FVector& AimDirection);
	void RefreshWornVisual();
	FVector GetBeamOrigin() const;
	class USkeletalMeshComponent* GetBodyMesh() const;

	// 머리에 씌운 가발(보이기만 함)
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> WornWig;

	// 광선 메시: 피벗에서 +X 로 100cm → X 배율 = 거리 / 100
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BeamMesh;

	float LastFireTime = -1000.0f;
	float BeamHideTime = 0.0f;
	FTimerHandle WornCheckTimer;
	// 멀티(9/27): 가발을 가졌는지는 서버만 안다(가방이 서버에만 있다). 서버가 0.5초마다 보고 복제 — 클라는 이 값으로
	//   머리에 씌우고 발사 버튼을 받는다. 전에는 클라가 자기 빈 가방을 봐서 누구의 가발도 안 보였고 쏠 수도 없었다.
	UPROPERTY(Replicated)
	bool bWigWornReplicated = false;

public:
	// 빔 모양(피벗에서 +X 로 100cm, X 배율 = 거리/100). 9/23 블루프린트 분리 — 캐릭터 블루프린트의 WigBeam 부품에서 바꾼다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual")
	TSoftObjectPtr<UStaticMesh> BeamMeshAsset;

	// 발사 버튼(9/23 사용자: "왼쪽 클릭으로"). 컨트롤러가 왼쪽·오른쪽 클릭을 모두 넘기고, 이 버튼일 때만 쏜다 —
	// 캐릭터 블루프린트의 WigBeam 부품에서 바꾼다. 9/20 에는 왼쪽을 총 자리로 비워 두느라 오른쪽이었다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam")
	FKey FireKey = EKeys::LeftMouseButton;

	// 누른 버튼이 FireKey 면 쏜다(컨트롤러 입력이 부른다).
	UFUNCTION(BlueprintCallable, Category = "PG|WigBeam")
	void RequestFireWithKey(FKey PressedKey);

	// ---- 맞을 때 터지는 효과(9/23 사용자: "빔 맞으면 걍 터지는 모션, 때릴 때마다") ----
	// 광선이 멈춘 자리(벽·땅): 흙먼지 + 커지는 불덩이. 뚫고 지나간 몸마다: 작은 폭발 + 작은 불덩이.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual")
	TSoftObjectPtr<UParticleSystem> ImpactEffect;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual")
	TSoftObjectPtr<UParticleSystem> BodyHitEffect;

	// 불덩이 모양(반지름 1m 구)과 재질. 비우면 불덩이는 안 나온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual")
	TSoftObjectPtr<UStaticMesh> BlastBallMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual")
	TSoftObjectPtr<UMaterialInterface> BlastBallMaterial;

	// 불덩이가 커지는 최대 반지름(cm): 멈춘 자리 / 맞은 몸.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual", meta = (ClampMin = "0.0"))
	float ImpactBlastRadiusCm = 600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|WigBeam|Visual", meta = (ClampMin = "0.0"))
	float BodyBlastRadiusCm = 300.0f;
};
