// 피날레 우주전함. 보스를 쓰러뜨리면 산 너머에서 넘어와 맵 위에 낮게 떠서 플레이어를 기다린다. (2026-09-20)
//
// 겉모습: Minerva(KitBash3D) 화물선 블루프린트(부품 31개)를 10배로 키워 붙인다. 배율 1 일 때 길이 44m 라서 10배 = 436m,
//   맵 타일 20m 기준 20칸이 넘는다 — "하늘을 덮는다"는 느낌은 이 크기라야 난다(사용자 결정 9/20).
//
// 왜 블루프린트를 그대로 붙였나: 부품 31개의 상대 위치가 이미 그 블루프린트 안에 있다. 부품을 하나씩 코드로 조립하면
//   좌표를 전부 다시 재야 하는데, 콘솔 명령(PG.SpawnBattleship)으로 이미 "그대로 띄우면 제대로 조립된다"를 확인했다.
//
// 왜 껍데기는 충돌을 다 끄나 — 프레임 방어:
//  1) 10배로 키운 복잡 충돌(삼각형 수십만)이 움직이면 Chaos 가 매 프레임 그 전부를 다시 넣었다 뺐다 한다(잔해 때 겪은 NarrowPhase 폭발과 같은 자리).
//  2) 그림자도 끈다. 436m 짜리가 동적 그림자를 드리우면 그림자 맵을 통째로 다시 그린다(RTX 4060 8GB 목표).
//  타고 다닐 바닥(격납고·복도·함교)은 2단계에서 "사람 크기 Minerva 모듈 + 단순 상자 충돌"로 따로 깐다. 껍데기는 눈으로만 보는 것.
//
// 움직임은 물리가 아니라 탱크(APGTankPawn::TickDrive)와 같은 코드 이동이다: 목표점으로 일정 속도로 가고, 도는 만큼 기울인다.
//   물리로 띄우면 436m 짜리 질량체가 바람에 흔들리듯 떨린다.
//
// 좌표 규칙 — 다른 세션도 이 배의 좌표를 쓴다(2026-09-20 점검 결과):
//  - 이 액터 자체의 배율은 1 이다. 10배는 껍데기(Hull) "자식 액터"에만 걸려 있고 InteriorRoot 이하는 배율 1 이다.
//    그래서 로컬 1 단위 = 월드 1 cm 이고, 로컬과 월드는 위치·회전으로만 갈린다. 배율 때문에 10배 어긋날 일은 없다.
//  - HullLocalBounds 는 부품을 "이 액터 기준"으로 다시 재서 모은 값이라(MeasureHull) 이미 10배가 반영된 cm 다.
//  - 월드가 필요하면 GetActorTransform().TransformPosition(로컬), 반대는 InverseTransformPosition 을 쓴다
//    (TickCatchFallers 가 그 예다 — 폰의 월드 위치를 배 기준으로 바꿔 놓고 로컬끼리 비교한다).
//  - 예외 하나: GetDeckWorldZ() 는 GetActorLocation().Z + DeckLocalZ 다. 배 중심선 한 점의 높이이고 배가 기울면
//    그만큼 어긋난다 — 정박 중 0cm, 조종 중 최대 뱅크(8도) 43cm, 이륙 중 피치(6도) 24cm. 갑판은 335m 라
//    피치 6도면 뱃머리와 선미의 실제 높이 차가 22m 다. "갑판 어느 지점의 높이"가 필요하면 이 함수 말고
//    그 지점을 TransformPosition 으로 직접 옮길 것.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "Common/PGNetPoseSmoother.h"
#include "PGBattleshipActor.generated.h"

class UBoxComponent;
class UCameraComponent;
class UCanvas;
class ULocalLightComponent;
class UPointLightComponent;
class URectLightComponent;
class UMaterialInstanceDynamic;
class UParticleSystem;
class UParticleSystemComponent;
class USceneCaptureComponent2D;
class UTextRenderComponent;
class UTextureRenderTarget2D;

UENUM(BlueprintType)
enum class EPGShipMotion : uint8
{
	Parked,  // 제자리(등장 전 또는 정박)
	Cruise,  // 목표점으로 이동
	Hover,   // 목표점 도착, 제자리에서 미세하게 위아래로 흔들림
};

class UMaterialInterface;
class UStaticMesh;
UCLASS()
class PROJECTPG_API APGBattleshipActor : public AActor
{
	GENERATED_BODY()

public:
	APGBattleshipActor();

	// 산 너머(Start)에 놓고 Hover 로 날아오게 한다. 도착하면 Motion 이 Hover 가 된다.
	void FlyInFrom(const FVector& Start, const FVector& Hover);

	// 이미 떠 있는 배를 다른 자리로 보낸다(2단계 이륙에서 쓴다).
	void CruiseTo(const FVector& Target);

	// 배 전체(껍데기 = 붙여 둔 자식 액터 포함)를 숨기거나 보이게 한다.
	// AActor::SetActorHiddenInGame 은 이 액터의 컴포넌트만 숨긴다 — 붙여 둔 "액터"는 그대로 보인다.
	// 그래서 미리 만들어 둔 전함의 몸통이 판 시작부터 하늘에 보였다(9/20 PIE).
	void SetShipHidden(bool bHide);

	// ---- 멀티(9/27): 조종석 ----
	// 서버에서만: 그 사람이 앉기/일어서기를 청했다(UPGHelmControlComponent 가 넘긴다). 서버가 거리·상태를 보고 정한다.
	void ServerSeatRequest(APlayerController* PC, bool bSit);
	// 서버에서만: 조종하는 사람 컴퓨터가 보낸 조종 입력(W/S/A/D·오르기·주포·조준점).
	void ServerHelmInput(APlayerController* PC, const struct FPGHelmInput& Input);
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- 멀티(9/27): 보이는 것만 모든 컴퓨터에 ----
	// 주포 빔·탄착, 추락 중 폭발·불, 땅에 닿을 때 먼지·흔들림은 서버에서만 그려져 전용 서버 판에서는 아무도 못 봤다.
	// 피해·판정은 그대로 서버가 하고, 그림만 이 방송으로 각자 그린다(전용 서버는 화면이 없어 건너뛴다).
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCannonFx(uint8 MuzzleIndex, FVector_NetQuantize Impact, bool bBig);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastWreckPop(FVector_NetQuantize Local, bool bWithFire);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastWreckTouchdown(FVector_NetQuantize Feet);

	// 정박한 뒤 디렉터가 부른다. 원래는 승강 발판을 땅까지 내렸지만 승강기는 없앴다(9/21 사용자 결정).
	// 지금은 갑판에 차를 한 대 놓는 일만 한다 — 디렉터가 "이제 탈 시간" 이라고 알려 주는 자리가 여기뿐이다.
	// 이름을 그대로 두는 이유: PGFinaleDirector 가 부르는 함수라 지우면 그쪽이 깨진다. 디렉터 담당 세션이
	// 호출을 정리하면 그때 같이 없앤다.
	void DeployElevator(float GroundWorldZ);
	// 이륙 전에 디렉터가 부른다. 승강기가 없어져 지금은 하는 일이 없다(위와 같은 이유로 이름만 남긴다).
	void RetractElevator();

	// 뒷문은 없다(9/21 사용자 결정: 팩의 원래 뒷문·경사로가 입구다 — 우리가 덧댄 미닫이 문짝·막는 상자·발광 테두리·메움벽을 걷어냈다.
	// 근거는 BuildInterior 의 "no custom hangar door" 로그 주석). PGFinaleDirector 가 이륙 때 아직 이 함수를 부르므로
	// 빈 껍데기로 남긴다 — 그쪽 호출을 지우면 같이 지운다.
	void CloseHangar() {}

	// 누가 조종석에 앉아 있나. 드래곤 디렉터가 "조종을 시작한 뒤 몇 초 있다가 등장" 조건에 쓴다(9/20 사용자:
	// 조종석에 앉아 창밖을 보고 있어야 그 장면을 본다). 앉은 폰이 사라졌으면(죽음·리스폰) 앉은 것으로 치지 않는다.
	bool IsPilotSeated() const { return bSeated && IsValid(SeatedPawn); }
	APawn* GetSeatedPawn() const { return bSeated ? SeatedPawn.Get() : nullptr; }

	bool HasArrived() const { return Motion == EPGShipMotion::Hover; }
	EPGShipMotion GetMotion() const { return Motion; }

	// 갑판(사람이 서는 바닥)의 월드 높이. 날으는 차로 뒤쪽 입구로 들어갈 때 목표 높이로도 쓴다.
	float GetDeckWorldZ() const;
	// 뒤쪽(차가 날아 들어오는) 입구 한가운데. 안내 표시·자동 안착에 쓴다.
	FVector GetHangarEntranceWorld() const;
	// 함교(뱃머리 유리창 안쪽) 서는 자리. 1인칭으로 보면 창밖이 보인다.
	FVector GetBridgeWorld() const;



	// 전함 체력. 0 이 되면 불타며 추락한다(BeginWreck) — 드래곤은 이 값이 0 이하인 것을 보고 지상전으로 넘어간다.
	// 추락 뒤에는 어떤 피해가 와도 0 에 그대로 남는다(TakeDamage) — 드래곤 쪽 계약이 "GetHealth() <= 0" 이라 흔들리면 안 된다.
	float GetHealth() const { return Health; }
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	// 체력 0 뒤(떨어지는 중이거나 땅에 처박힌 뒤)인가. 드래곤 작업자는 GetHealth() 만 쓰므로 둘 다 유지한다.
	bool IsWrecked() const { return bWrecked; }
	// 땅에 닿아 멈췄나. 떨어지는 중이면 false.
	bool IsWreckGrounded() const { return bWreckGrounded; }

	// 조립이 끝나고(부품이 다 붙은 뒤) 잰 껍데기 크기. 조립 전에는 IsValid == false.
	const FBox& GetHullWorldBounds() const { return HullBounds; }
	float GetShipLengthCm() const { return HullBounds.IsValid ? FMath::Max(HullBounds.GetExtent().X, HullBounds.GetExtent().Y) * 2.0f : 0.0f; }

	// 부품까지 다 붙어 크기를 잰 뒤 한 번 알린다(디렉터가 이걸 받고 탑승 단계를 연다).
	DECLARE_MULTICAST_DELEGATE_OneParam(FPGShipSignature, APGBattleshipActor*);
	FPGShipSignature OnArrived;
	// 체력 0 으로 추락이 시작될 때 한 번(디렉터가 로그로 잇는다).
	FPGShipSignature OnWrecked;

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	// 멀티(9/28): 클라이언트는 서버 위치로 순간이동하지 않고 부드럽게 따라간다(PGNetPoseSmoother.h). 사용자 PIE: "우주선 움직임이 버벅거린다".
	virtual void PostNetReceiveLocationAndRotation() override;
	FPGNetPoseSmoother ProxySmoother;
	bool IsLocalWalkerAboard() const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	TSoftClassPtr<AActor> HullBlueprint;

	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGBattleship 에서 바꾼다. 기본값 = 원래 코드에 적혀 있던 에셋) ----
	// 함교(조종석)·갑판 판·경사로에 입히는 재질.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UMaterialInterface> BridgePanelMaterial;

	// 함교 위 표지등·안내선에 입히는 빛 재질.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UMaterialInterface> BridgeGlowMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UStaticMesh> BridgeConsoleMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UStaticMesh> BridgeCanopyMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UStaticMesh> BridgeSeatMesh;

	// 콘솔 화면 재질(Screen 텍스처 파라미터에 레이더 화면을 그린다).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UMaterialInterface> ShipScreenMaterial;

	// 주포 빔(피벗에서 +X 로 100cm, X 배율 = 거리/100 로 늘인다).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UStaticMesh> CannonBeamMesh;

	// 주포 탄착 폭발 공과 그 재질.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UStaticMesh> ImpactBlastMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UMaterialInterface> ImpactBlastMaterial;

	// 추락할 때 불·흙먼지(탄착 먼지로도 쓴다). 팩이 없는 PC 에서는 이펙트 없이 떨어지기만 한다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UParticleSystem> WreckFireEffect;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Finale|Visual")
	TSoftObjectPtr<UParticleSystem> WreckDustEffect;

	// 배율 5 = 길이 약 218m. 처음엔 10(436m)이었는데 9/21 사용자가 절반으로 줄였다 — 꼬리가 맵 끝을 넘고, 너무 커서 다루기 힘들었다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale", meta = (ClampMin = "0.5", ClampMax = "40.0"))
	float ShipScale = 5.0f;

	// 코드로 갑판(보이지 않는 바닥·벽·지붕, 바닥판, 차고, 복도, 화물, 뒤 선반)을 깔지.
	// 기본은 끔 — 바닥은 팩 블루프린트(BP_KB3D_MTM_VehicleCargoShip_A)에 사용자가 직접 깐다(9/21 사용자 결정).
	// 끄면 팩 원래 부품만 충돌을 끄고, 사용자가 블루프린트에 더한 부품은 제 충돌을 그대로 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	bool bBuildCodeDeck = false;

	// 이륙 때 팩 뒷문이 들어 올려져 닫히는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale", meta = (ClampMin = "0.1"))
	float RearDoorCloseSeconds = 3.0f;
	// 닫히며 도는 각도. 0 이면 자동(문의 기울기 + 90도 = 세로로 세움) — 눈으로 보고 덜 닫히거나 더 닫히면 여기 숫자를 넣는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float RearDoorCloseDegrees = 0.0f;

	// 순항 속도 cm/s. 산 너머(약 900m)에서 15초쯤 걸려 넘어온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float CruiseSpeed = 6000.0f;

	// 방향을 바꾸는 속도(초당 도). 배가 크니까 천천히 돈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float TurnRateDeg = 10.0f;

	// 도는 동안 기우는 최대 각(도).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float MaxBankDeg = 8.0f;

	// 배 앞쪽(+X) 기준 배 크기. 조립 뒤에만 뜻이 있다.
	const FBox& GetHullLocalBounds() const { return HullLocalBounds; }

protected:
	// 책임별 협력 객체를 만든다(BeginPlay 맨 앞). 전함 액터는 이들을 순서대로 부르기만 한다.
	void CreateCollaborators();
	// ---- 협력 객체: 전함 갑판·격납고 (PGShipDeck.h) ----
	friend class UPGShipDeck;
	UPROPERTY(Transient)
	TObjectPtr<class UPGShipDeck> Deck;
	// ---- 협력 객체: 전함 추락 (PGShipWreck.h) ----
	friend class UPGShipWreck;
	UPROPERTY(Transient)
	TObjectPtr<class UPGShipWreck> Wreck;
	// ---- 협력 객체: 전함 무기 (PGShipWeapons.h) ----
	friend class UPGShipWeapons;
	UPROPERTY(Transient)
	TObjectPtr<class UPGShipWeapons> Weapons;
	// ---- 협력 객체: 전함 조종 (PGShipHelm.h) ----
	friend class UPGShipHelm;
	UPROPERTY(Transient)
	TObjectPtr<class UPGShipHelm> Helm;
	// ---- 협력 객체: 전함 선체 조립·측정 (PGShipHullBuilder.h) ----
	friend class UPGShipHullBuilder;
	UPROPERTY(Transient)
	TObjectPtr<class UPGShipHullBuilder> HullBuilder;
	void TickCruise(float DeltaSeconds);
	// 배가 지금 움직이고 있나. 수직 상승(이륙)도 "움직이는 중"이다(.cpp 주석 참고).
	bool IsShipMoving() const;
	// 잠시 붙들어 둔 물리 몸(차) 하나. 사람은 UE 의 "움직이는 바닥"으로 따라오지만 물리 차량은 안 따라온다.
	struct FPGHeldPawn
	{
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<AActor> PreviousParent; // 배에 도킹(PGFlightKit)돼 있던 차면 놓을 때 다시 배에 붙여 준다
		bool bWasSimulating = false;
	};
	// 배 안에 있는 플레이어 폰의 상태(물리·붙임·닿은 상자)를 1초마다 찍는다 — "안에서 차가 못 다닌다" 진단용(.cpp 주석 참고).
	void TickPlayerInsideDiagnostics(float DeltaSeconds);
	float PlayerInsideLogTime = 0.0f;
	// 진단 로그 켜기/끄기. 격납고 끼임 원인이 잡혀(9/22) 기본은 끔 — 다시 볼 일이 생기면 BP 에서 켠다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Hangar")
	bool bLogPlayerInside = false;
	// 임시 조준점. 앉아 있는 동안만 화면 가운데에 십자를 그린다(.cpp 주석 참고).
	// 변신차(UPGFlightKitComponent::DrawCrosshair)와 같은 통로·같은 치수다 — 게임 안에서 조준점이 두 가지로 보이면 안 된다.
	void DrawCrosshair(UCanvas* Canvas, APlayerController* PC);


	// 떨어지는 가속(cm/s²)과 최고 낙하 속도(cm/s). 중력(980)보다 느리게 — 218m 짜리가 돌처럼 떨어지면 장난감으로 보인다.
	//   정박 높이(땅 위 80m)에서 약 5초, 최고 오르기(+200m)에서 약 10초 걸린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckFallAccel = 600.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckMaxFallSpeed = 5000.0f;
	// 떨어지는 동안 앞으로 미끄러지는 속도(cm/s). 제자리에서 수직으로 내려앉으면 "추락" 이 아니라 "착륙" 으로 읽힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckSlideSpeed = 1500.0f;
	// 기우는 각도(도). 뱃머리가 숙고 한쪽으로 기운다 — 어느 쪽인지는 무작위.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckPitchDeg = 8.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckRollDeg = 12.0f;
	// 체력 0 순간 선체 여러 곳에서 터지는 개수(이 사이 무작위) — 그 자리마다 불이 남는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck", meta = (ClampMin = "1", ClampMax = "12"))
	int32 WreckFireMin = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck", meta = (ClampMin = "1", ClampMax = "12"))
	int32 WreckFireMax = 6;
	// 불 이펙트 크기. 원본은 횃불 불꽃(드래곤 브레스와 같은 에셋)이라 작다 — 화면 보고 맞춘다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckFireScale = 30.0f;
	// 떨어지는 동안 몇 초마다 한 번씩 더 터지나(±30% 흔들린다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck", meta = (ClampMin = "0.5"))
	float WreckPopInterval = 2.0f;
	// 땅에 닿은 뒤 계속 타는 불 개수. 나머지는 끈다 — 파티클 대여섯 개가 판이 끝날 때까지 도는 것은 4060 에서 아깝다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck", meta = (ClampMin = "0", ClampMax = "12"))
	int32 WreckFiresAfterCrash = 3;
	// 땅에 닿을 때 선체 높이의 몇 배만큼 파묻히나. 0 이면 바닥이 땅에 딱 얹힌다 — 조금 묻혀야 "처박혔다" 로 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float WreckBuryRatio = 0.12f;
	// 땅에 닿은 뒤 둘레 소품을 몇 초 동안 날리나(한 번에 다 털면 프레임이 죽어서 0.3초마다 몇 개씩).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Wreck")
	float WreckKnockSeconds = 2.0f;

	// 멀티(9/27): 복제 — 클라이언트의 계기판("MAYDAY")·조종석(부서진 배엔 못 앉음)이 본다.
	UPROPERTY(Replicated)
	bool bWrecked = false;
	UPROPERTY(Replicated)
	bool bWreckGrounded = false;
	// 멀티(9/27): 배 전체 숨김. 껍데기(Hull)는 사람마다 따로 만든 로컬 액터라 서버가 숨김을 풀어도 클라에서는 숨은 채였다
	//   (클라에선 218m 짜리 투명한 배). 이 값을 복제해 각자 자기 껍데기를 숨기고 보인다.
	UPROPERTY(ReplicatedUsing = OnRep_ShipHidden)
	bool bShipHidden = false;
	UFUNCTION()
	void OnRep_ShipHidden();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	TObjectPtr<USceneComponent> RootScene;

	// 껍데기(Minerva 화물선 블루프린트). 눈으로만 보는 것 — 충돌·그림자 없음.
	UPROPERTY(Transient)
	TObjectPtr<AActor> Hull;

	// 사람 크기 속(갑판·복도·함교·격납고). 껍데기는 10배지만 이건 배율 1 — 그래야 사람이 사람만 하다.
	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> InteriorRoot;

	// 갑판 안을 돌아다닐 차 한 대. 배가 437m 라 걸어서는 함교까지 한참이다(9/20 사용자 요청).
	UPROPERTY()
	TObjectPtr<APawn> DeckVehicle;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Light")
	float DeckLightIntensity = 20000.0f;     // 칸델라. 25m 위에서 바닥 한가운데 32lux, 갑판 가장자리(53m 옆) 6lux — 자동 노출이 맞춘다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Light")
	float DeckLightRadius = 9000.0f;         // 90m. 갑판 폭(107m)을 한 등이 다 덮는다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Light")
	float DeckLightSpacing = 7000.0f;        // 앞뒤 70m 마다 하나. 반경 90m 라 어느 자리든 등 2~3개만 닿는다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Light")
	float DeckLightHeight = 2500.0f;         // 갑판에서 25m 위. 낮게 달면 등 바로 밑만 밝고 가장자리가 어둡다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Light")
	float SpotLightIntensity = 6000.0f;      // 함교·승강기 출입구의 작은 점광원(칸델라)

	// 한 번이라도 정박한 적이 있나. 정박한 뒤 다시 움직이기 시작하면 이륙이다 — 그때 뒤쪽 선반을 밀어 넣는다(TickHullRetract).
	// 멀티(9/27): 클라이언트도 뒷문 닫힘(TickHullRetract)을 같은 때 돌리도록 보낸다.
	UPROPERTY(Replicated)
	bool bHasArrivedOnce = false;


	TArray<TWeakObjectPtr<USceneComponent>> RearDoorParts;
	TArray<FTransform> RearDoorStartLocal;
	FVector RearDoorHinge = FVector::ZeroVector;
	FQuat RearDoorCloseRot = FQuat::Identity;
	// 본체 경계에서 이만큼 넘게 나가야 "튀어나왔다" 로 본다. 작게 잡으면 본체에 묻힌 것까지 지운다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Hangar")
	float HullOutsideMarginCm = 300.0f;

	// 착륙다리(팩 부품 Legs1/2/3 — 뱃머리 아래 흰 원판의 정체)는 조립 때 바로 숨긴다(MeasureHull).
	// 이 배는 땅에 내리지 않으므로 펴 둘 이유가 없다(9/21 사용자 결정). 접는 코드는 걷어냈다.

	// 널빤지 한 장이 다 들어가는 데 걸리는 시간(초). 에셋 쪽 실측 권장값.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Hangar")
	float ApronSlideSeconds = 1.6f;
	// 문턱판(DeckRearLip). 선반과 같이 밀려 들어간다 — 남겨 두면 닫힌 문 뒤에 6m 짜리 선반이 튀어나와 보인다.
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> DeckRearLipBox;
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> DeckRearLipPlate;

	// 멀티(9/27): 보낸다 — 안 보내면 클라이언트에서는 배가 늘 "서 있음" 이라 함교 화면 글자·뒷문·비행 장비 착함이 다 틀렸다.
	UPROPERTY(Replicated)
	EPGShipMotion Motion = EPGShipMotion::Parked;
	FVector TargetLocation = FVector::ZeroVector;
	FBox HullBounds = FBox(ForceInit);
	FBox HullLocalBounds = FBox(ForceInit);
	float Bank = 0.0f;
	double HoverStartTime = 0.0;
	float HoverBaseZ = 0.0f;
	bool bAssembled = false;
	// FlyInFrom 이 받은 목표는 "배 밑바닥이 있을 높이"다. 크기를 잰 뒤 배 중심 높이로 고쳐 잡는다.
	bool bTargetIsBelly = false;
	// 조립이 끝나기를 기다렸다가 출발한다(팩 부품을 Movable 로 바꾼 뒤라야 배를 따라온다).
	bool bPendingCruise = false;

	// 갑판(로컬 Z). 조립 때 정해진다.
	float DeckLocalZ = 0.0f;
	float HangarEntranceLocalX = 0.0f;
	float DeckFrontLocalX = 0.0f;   // 갑판 바닥이 끝나는 앞쪽 X(함교까지 이어져 있다)
	FVector BridgeLocal = FVector::ZeroVector;

	// 옆 화면(후방 카메라)은 화면을 한 번 더 그리는 일이라 비싸다. 가까이 있을 때만, 초당 이만큼만 찍는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float ScreenFps = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float ScreenActiveRange = 4000.0f;

	// 주포
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CannonBeams;
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> HelmZone;   // 여기 서 있어야 주포가 내 것이 된다

	// 장전 시간(초). 탱크(2초)보다 빠르지만 연사는 아니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon")
	float CannonInterval = 1.0f;
	// 주포 몇 발에 드래곤이 쓰러지나(9/21 사용자: "한 5 대 맞추면"). 맞으면 드래곤 최대 체력의 1/N 을 준다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon", meta = (ClampMin = "1"))
	int32 CannonHitsToKillDragon = 5;
	// 조종석 오른쪽 마우스로 오르는 속도(cm/s)와 정박 높이 위로 오를 수 있는 한도(cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmClimbSpeed = 1500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmMaxClimbCm = 20000.0f;
	// 오른쪽 마우스를 안 누르면 내려가는 속도(cm/s)와, 내려앉는 높이(배 밑면이 땅에서 몇 cm 위에서 멈추나).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmSinkSpeed = 600.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmLandClearanceCm = 300.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon")
	float CannonDirectDamage = 900.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon")
	float CannonBlastDamage = 1400.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon")
	float CannonBlastRadius = 1500.0f;
	// 사거리 600m. 하늘에서 땅까지 닿아야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon")
	float CannonRange = 150000.0f; // 9/21: 600m -> 1500m. 드래곤은 500m 밖에서 솟아 선회해 600m 로는 조준선이 늘 허공(hit=None at 600m)이었다
	// 전함 체력 — "3~4분 공중전" 이 되게 드래곤 피해량(PGDragonBoss.h)으로 역산한 값(9/21 사용자 요청).
	//   드래곤은 선회 6초마다 공격하고, 브레스(초당 400 × 2.5초 = 1000)와 돌진(스칠 때 1200)을 번갈아 세 번 한 뒤
	//   약 25초 동안 내려앉는다(그동안 배는 안 맞는다). 공격 하나가 예고·접근까지 대략 11초라 한 바퀴가 약 60초,
	//   그 안에 최대 3,200~3,400 → 전부 맞아도 초당 57 이 한계다. 12,000 이면 210초 ≈ 3.5분이고, 실제로는
	//   돌진이 못 붙거나(pass hit=0) 브레스가 선체를 비껴가 4분 쪽으로 늘어난다. 전의 20,000 은 이론상으로도 6분이라 길었다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Cannon")
	float MaxHealth = 12000.0f;

	// 미사일: 주포보다 느리게 쏘고 더 아프다. 한 번에 두 발씩(좌우 포구에서).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Missile")
	float MissileInterval = 2.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Missile")
	float MissileDirectDamage = 1500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Missile")
	float MissileBlastDamage = 2000.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Missile")
	float MissileBlastRadius = 2500.0f;
	// 조준 보정 각도(도). 화면 가운데에서 이 각도 안에 드래곤이 있으면 표적으로 잡는다 — 캡슐이 날개를 안 덮어서 필요하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Weapons", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float AimAssistDegrees = 6.0f;

	double LastBeamTime = -100.0;
	UPROPERTY(Replicated) // 멀티(9/27): 클라 계기판 "HULL %" 
	float Health = 0.0f;
	float LastTargetDistanceM = 0.0f;

	// 조종석에 앉아 있나 / 누가. 멀티(9/27): 복제 — 앉은 사람 컴퓨터가 이 값으로 카메라·걷기 막기·조준선을 켠다(OnRep_Seated).
	UPROPERTY(ReplicatedUsing = OnRep_Seated)
	bool bSeated = false;
	UPROPERTY(ReplicatedUsing = OnRep_Seated)
	TObjectPtr<APawn> SeatedPawn;
	UFUNCTION()
	void OnRep_Seated();

	// 자리와 각도는 전부 노출한다. PIE 에서 눈으로 보고 "더 뒤로/위로"를 정할 값이라, 빌드를 다시 돌리지 않고
	// 월드 아웃라이너에서 고칠 수 있어야 한다. 앉을 때마다 다시 적용되므로 일어섰다 앉으면 바로 반영된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float ViewBackRatio = 0.80f;   // 배 길이의 몇 배만큼 뒤에서 (0.55 -> 0.80: 선미가 세로 화면의 72% -> 46%)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float ViewUpRatio = 0.45f;     // 배 길이의 몇 배만큼 위에서
	// 마우스로 올려다보고 내려다볼 수 있는 한계. 드래곤은 대개 위에 있으니 위쪽을 넉넉히 연다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float ViewPitchMinDeg = -80.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float ViewPitchMaxDeg = 60.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float ViewFieldOfView = 85.0f; // 90 을 넘기면 437m 짜리가 가장자리에서 휘어 보인다
	// 앉았을 때 마우스로 시야를 도는 감도. 도보 캐릭터의 마우스는 제 스프링암만 돌리고 ControlRotation 은
	// 안 건드려서(Input/NA_Look_Mouse.cpp), 조종석에서는 마우스 움직임을 여기서 직접 받는다(.cpp 주석 참고).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float ViewMouseSensitivity = 1.0f;
	// PIE 에서 위아래가 거꾸로 느껴지면 이것만 켜면 된다. 빌드를 다시 돌릴 값이 아니다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	bool bViewInvertPitch = false;
	// 조준점 팔 하나 길이(화면 높이 대비). 차 쪽 조준선과 맞춘 값이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|View")
	float CrosshairScreenRatio = 0.012f;
	FDelegateHandle CrosshairHandle;
	TArray<FPGHeldPawn> DeckCargo; // 배가 순항하는 동안 붙들어 둔 차

	// 배가 이번 프레임에 실제로 움직인 속도(cm/s). 배는 물리가 아니라 SetActorLocation 으로 가므로 직접 재야 한다.
	// 붙들었던 차를 놓을 때 이 속도를 물려준다 — 안 그러면 놓는 순간 차만 제자리에 남아 뒤로 쏠린다.
	FVector LastShipLocation = FVector::ZeroVector;
	FVector ShipVelocity = FVector::ZeroVector;

	// 조종석 조종. 437m 짜리라 느리고 묵직해야 한다 — 목표값으로 천천히 보간한다(변신차 ArcadeSmooth 와 같은 요령).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	// 아래 세 값은 "묵직함"을 만들려다 과했다(9/21 사용자: "조작감이 엄청 별로네. 너무 심하게 무겁네").
	// 처음에는 18m/s · 한 바퀴 1분 · 보간 0.5 였다. 배가 크다는 느낌보다 "안 움직인다"가 먼저 왔다.
	// 눈으로 맞출 값이라 전부 노출돼 있다 — PIE 에서 직접 올리고 내려 볼 것.
	float HelmSpeed = 3000.0f;      // cm/s. 30m/s ≈ 108km/h
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmTurnRateDeg = 20.0f;  // 초당 20도 = 한 바퀴 18초. 드래곤을 눈으로 쫓아갈 수 있어야 한다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmSmooth = 1.5f;        // 목표까지 따라가는 속도. 작을수록 무겁다(0.5 는 키를 눌러도 2초쯤 뒤에 붙었다)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale|Helm")
	float HelmRangeCm = 40000.0f;   // 정박한 자리에서 400m 까지만. 맵 밖으로 몰고 나가지 못하게
	float HelmThrottle = 0.0f;      // -1 ~ 1
	float HelmYawRate = 0.0f;       // -1 ~ 1
	FVector HelmAnchor = FVector::ZeroVector; // 정박한 자리(조종 범위의 중심)
};
