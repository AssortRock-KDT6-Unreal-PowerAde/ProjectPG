// 피날레 드래곤. 전함이 이륙하면 맵 외곽 산에서 솟아올라 공중전을 건다. (2026-09-20)
//
// 에셋: Fab "Dragon for Boss Monster : PBR" 의 SoulEater(613 × 1029 × 578 cm). 네 마리 중 유일하게
//   TakeOff / FlyIdle / FlyForward / FlyFastForward / FlyAttack / Land / Scream / Die 가 다 있어서 공중전이 된다.
//   배율 4 = 날개폭 40m. 전함(410m)과 나란히 놓았을 때 "큰 짐승"으로 읽히는 크기.
//
// 왜 몬스터 클래스(APGMonsterCharacter)를 안 쓰나: 그쪽은 땅 위 길찾기와 캡슐 이동이 전제다.
//   이 녀석은 하늘에서만 살고 길찾기를 쓰지 않는다 — 전함·미사일과 같은 "코드로 움직이는 액터"가 맞다.
//   피해만 UE 기본 TakeDamage 로 받아서 전함 주포·미사일이 그대로 통한다.
//
// 싸움 흐름(기획서 13절, 9/21 개정):
//   Rising(산에서 솟기) → Orbit(전함 둘레를 돈다) → 공격 하나 → Orbit → ...
//     공격은 Breath(공중에 뜬 채 입에서 불을 뿜는다) 와 Pass(배 옆을 스치며 할퀸다) 를 번갈아 한다.
//     표적은 전함 > 밑에 보이는 몬스터 > 플레이어 순서다(ChooseTarget).
//   공격을 몇 번 하면 Descend(내려앉기) → Landed(땅에서 포효·지면 공격 = 약점, 피해 2배) → TakeOff(이륙) → Orbit
//   체력 50% 가 되면 차례와 상관없이 한 번 내려앉는다. 0 이 되면 Dying(추락 → 땅에 닿으면 죽는 동작)
//
// 9/21 사용자: "드래곤이 앉는 모션을 공중에서 시전한다" — 원인과 고친 곳은 .cpp 의 Descend 주석 참고.
// 9/21 사용자: "쓰러트렸는데 너무 맥없이 떨어진다. 산 밑으로 꺼진다" — 죽는 순서(맞음 → 비틀거리며 추락 → 땅에 처박힘 → 누움)는
//   .cpp 의 TickDying 위 주석 참고. 산에는 충돌이 없어서 땅 찾기가 산을 뚫고 지나가던 것이 "꺼짐"의 원인이다.
// 9/21 사용자: "전함 체력이 0 이 되면 추락하려나? 그러면 드래곤도 지면에 따라와서 그때부터는 지면에서 총 맞고 싸우는 거지."
//   → 지상전(GroundFight). 전함 체력 0 = 추락 중(전함 쪽 계약, 새 함수 없이 GetHealth() 만 본다)을 매 틱 확인해,
//   전함 근처 맵 안쪽 땅으로 내려와 그 뒤로는 **다시 높이 날지 않고** 가장 가까운 플레이어(차에 탔으면 그 차)를 걸어서 쫓는다.
//   총·차 피해는 땅에서는 세게(GroundGunDamageMultiplier), 공중에서는 약하게(AirGunDamageMultiplier) 들어간다.
//   .cpp 의 "지상전" 절 주석 참고.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraShakeBase.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "Common/PGNetPoseSmoother.h"
#include "PGDragonBoss.generated.h"

class APGBattleshipActor;
class UAnimSequence;
class UCapsuleComponent;
class UParticleSystem;
class UParticleSystemComponent;
class USkeletalMeshComponent;
class USkeletalMesh;
class UStaticMesh;
class UMaterialInterface;

// 새 상태는 **뒤에만** 붙인다. 로그에 숫자로 찍히던 옛 값(0~4)이 그대로 같은 뜻을 갖게.
UENUM(BlueprintType)
enum class EPGDragonState : uint8
{
	Rising   UMETA(DisplayName = "등장"),
	Orbit    UMETA(DisplayName = "선회"),
	Pass     UMETA(DisplayName = "돌진"),
	Landed   UMETA(DisplayName = "착지(약점)"),
	Dying    UMETA(DisplayName = "추락"),
	Breath   UMETA(DisplayName = "브레스"),
	Descend  UMETA(DisplayName = "내려앉기"),
	TakeOff  UMETA(DisplayName = "이륙"),
	// 전함이 추락한 뒤의 지상전. 땅에 선 채로 플레이어·차를 쫓아 지면 공격을 되풀이한다. 여기서는 이륙(TakeOff)이
	// "짧게 뛰어 자리 옮기기" 로만 쓰이고, 선회(Orbit)로는 다시 돌아가지 않는다.
	GroundFight UMETA(DisplayName = "지상전"),
};

// 죽는 동안의 단계. 상태(Dying)는 하나로 두고 안에서만 나눈다 — 디렉터는 IsDead() 하나만 보면 되고,
// 로그의 상태 숫자도 안 바뀐다.
enum class EPGDragonDeathStep : uint8
{
	Struck,   // 마지막 한 방을 맞은 직후. 몸이 크게 흔들리고 뒤로 밀린다(아직 안 떨어진다)
	Falling,  // 날갯짓이 무너져 비틀거리며 추락. 맵 안쪽 땅을 향해 밀려간다
	Crashed,  // 땅에 처박혔다. 흙먼지·폭발 구·소품 날리기, 죽는 동작
	Resting,  // 누워 있다. CorpseSeconds 가 0 이면 그대로 남는다
};

// 멀티(9/27): "지금 이 동작을 틀어라" 한 줄. 서버가 PlayAnim 에서 채워 복제하고 클라이언트가 같은 동작을 튼다.
// 왜: 드래곤 동작은 서버의 상태 기계(EnterState·Tick)에서만 틀어서, 클라이언트 화면의 드래곤은 기본 자세(T포즈)로 싸웠다.
//   번호(Seq)를 두는 이유: 같은 동작을 다시 틀어도(반복 공격) 값이 바뀌어야 클라이언트에 다시 온다.
USTRUCT()
struct FPGDragonAnimCue
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UAnimSequence> Anim;
	UPROPERTY()
	bool bLoop = false;
	UPROPERTY()
	uint8 Seq = 0;
};

// 멀티(9/27): 불뿜기 한 줄(켜짐·입 자리·방향·길이). 서버가 SetBreath 에서 채워 복제하고 모두가 같은 불길을 그린다.
// 왜: 불길(원뿔 + 횃불 불꽃)은 서버의 공격 코드에서만 켜서, 전용 서버 판에서는 아무도 브레스를 못 봤다.
USTRUCT()
struct FPGDragonBreathCue
{
	GENERATED_BODY()
	UPROPERTY()
	bool bOn = false;
	UPROPERTY()
	FVector_NetQuantize From = FVector::ZeroVector;
	UPROPERTY()
	FVector_NetQuantizeNormal Dir = FVector::ForwardVector;
	UPROPERTY()
	float Length = 0.0f;
};

// 땅에 처박힐 때의 카메라 흔들림.
//
// 왜 직접 만드나: 엔진의 흔들림 틀(UWaveOscillatorCameraShakePattern 등)은 GameplayCameras 플러그인 모듈에 있는데,
//   이 프로젝트 Build.cs 에 그 모듈이 없다. Build.cs 는 담당 파일이 아니라 못 건드린다.
//   흔들림 "틀(pattern)" 은 엔진 모듈(UCameraShakePattern)에 있어서, 그것만 상속해 삼각함수로 직접 흔든다.
// 흔들림 클래스가 있어야 UGameplayStatics::PlayWorldCameraShake 를 부를 수 있다(거리에 따라 세기가 줄어든다).
UCLASS()
class UPGDragonCrashShakePattern : public UCameraShakePattern
{
	GENERATED_BODY()

public:
	// 전체 길이(초). 처음 0.3초는 가장 세고, 나머지는 서서히 잦아든다.
	float TotalSeconds = 1.8f;

private:
	virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo) const override;
	virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params) override;
	virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params, FCameraShakePatternUpdateResult& OutResult) override;
	virtual bool IsFinishedImpl() const override;

	float Elapsed = 0.0f;
};

UCLASS()
class UPGDragonCrashShake : public UCameraShakeBase
{
	GENERATED_BODY()

public:
	UPGDragonCrashShake(const FObjectInitializer& ObjectInitializer);
};

UCLASS()
class PROJECTPG_API APGDragonBoss : public AActor
{
	GENERATED_BODY()

public:
	APGDragonBoss();

	// 산 속(Ground 아래)에서 시작해 솟아오른다. Ship 을 표적으로 삼는다.
	void BeginRise(const FVector& GroundSpot, APGBattleshipActor* InShip);

	EPGDragonState GetState() const { return State; }
	// 전함이 추락해 지상전으로 넘어갔나(한 번 넘어가면 죽을 때까지 그대로).
	bool IsInGroundFight() const { return bGroundFight; }
	// 액터 원점에서 보이는 몸이 가로로 가장 멀리 뻗은 거리(cm). 에셋을 재서 채운다.
	float GetHalfWingCm() const { return HalfWingCm; }

	// 솟아오를 때(피치 35도) 보이는 몸 한가운데가 액터 원점에서 수평으로 얼마나 떨어져 있나(월드 XY, cm).
	// 이 모델은 피벗이 발밑이라 몸 한가운데가 원점에서 수십 m 떨어져 있다 — 등장 구덩이를 몸에 맞추는 데 쓴다(9/23).
	FVector GetRiseBodyCentreOffset() const;

	// 공중에서 기준점(액터 원점)이 내려갈 수 있는 가장 낮은 높이(cm) = 맵 바닥 + 발까지 거리 + MinFlyClearanceCm.
	float MinAirborneZ() const;

	// 등장할 때 뚫는 구덩이 반지름 한도 = 맵 반지름 × 이 비율. 몸(날개 끝까지)보다 크게는 안 뚫는다.
	// 9/23 블루프린트 분리: 크게 하면 몸에 더 잘 맞지만 맵이 더 많이 사라진다(0.35 전에는 한 방에 맵이 통째로 사라진 적이 있다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Rise", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float CollapseMapRadiusFraction = 0.35f;

	// 공중(선회·돌진·브레스·이륙 뒤)에서 발이 맵 바닥 위 이만큼(cm) 아래로는 못 내려간다.
	// [9/23 수정] 전에는 하한이 없어서, 배가 땅 가까이 내려앉거나 배를 피하다가 몸이 땅·산 밑으로 들어가 날았다(사용자 스크린샷).
	//   3500 = 35m: 선회 중 머리를 숙이거나 이륙 자세에서 꼬리가 내려가도 땅 위로 보이는 여유.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Flight", meta = (ClampMin = "0.0"))
	float MinFlyClearanceCm = 3500.0f;

	// 드래곤에게 피해를 가장 많이 준 플레이어가 자동으로 받는 전리품(아이템 ID). 교환소에서 분홍 단발 가발로 바꾼다(9/23).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Reward")
	FName TrophyItemId = TEXT("Dragon_Scale");
	// 이 배 한가운데에서 이만큼은 떨어져 있어야 보이는 몸이 선체를 파고들지 않는다(cm).
	// 디렉터도 태어날 자리를 고를 때 같은 식을 써야 해서 공개해 둔다 — 식이 두 벌이 되면 한쪽만 고치게 된다.
	float KeepDistanceCm(const APGBattleshipActor* Against) const;
	float GetHealth() const { return Health; }
	bool IsDead() const { return State == EPGDragonState::Dying; }

	virtual void BeginPlay() override;
	// 멀티(9/28): 드래곤 몸·동작·불 효과를 뒤에서 미리 읽어 둔다(모든 컴퓨터). 전함이 도착할 때 디렉터가 부른다.
	//   드래곤이 나오는 순간 이것들을 한꺼번에 디스크에서 읽어 프레임이 떨어졌다(9/28 사용자 PIE). 전함의 "미리 만들기" 와 같은 생각.
	static void PreloadAssets(TSubclassOf<APGDragonBoss> DragonClass);
	virtual void Tick(float DeltaSeconds) override;
	// 멀티(9/28): 클라이언트는 서버 위치로 순간이동하지 않고 부드럽게 따라간다(PGNetPoseSmoother.h). 사용자 PIE: "우주선 움직임이 버벅거린다" — 드래곤도 같은 방식으로 움직인다.
	virtual void PostNetReceiveLocationAndRotation() override;
	FPGNetPoseSmoother ProxySmoother;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// 배율 25 = 몸 296 × 날개폭 245 × 높이 196m. 전함이 437m 라 "옆에 뜨면 함선만 한 짐승"으로 읽힌다.
	//
	// 왜 40 이 아닌가(사용자는 "우주선만큼 커야 한다"고 했다): 40 이면 474 × 392m 로 배보다 크지만,
	//   그만큼 배에서 멀리 떨어뜨려야 날개가 선체를 안 파고든다 — 필요한 간격이 605m 인데 맵이 600m 라
	//   자리가 안 나온다(9/20 PIE: gap 342 / need 605, 태어나는 순간부터 겹쳤다).
	//   25 면 필요 간격이 494m 로 줄고, 배와 드래곤을 맵 양 끝으로 벌려 504~540m 를 만들 수 있다.
	//   안전거리의 여유(60m)를 깎아 40 을 유지하는 길도 있었지만, 그 여유는 "배를 뚫는다"는 지적 때문에
	//   넣은 것이라 크기를 지키려고 버그를 되살리는 거래가 된다. 더 키우고 싶으면 다음 후보는 30 이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	float DragonScale = 25.0f;

	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGDragonBoss 에서 바꾼다. 기본값 = 원래 코드에 적혀 있던 에셋) ----
	// 몸 모델. 크기·입 뼈·맞는 캡슐은 이 모델을 재서 정하므로 다른 드래곤으로 바꿔도 알아서 맞는다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual")
	TSoftObjectPtr<USkeletalMesh> DragonMesh;

	// 애니메이션이 들어 있는 폴더. 동작은 이름(FlyForwardAnim 등)으로 찾고, 없으면 비슷한 이름으로 대신한다(LoadDragonAnim).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual", meta = (ContentDir, LongPackageName))
	FDirectoryPath DragonAnimFolder;

	// 브레스 불꽃과 불이 닿은 자리의 먼지.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual")
	TSoftObjectPtr<UParticleSystem> BreathFireEffect;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual")
	TSoftObjectPtr<UParticleSystem> BreathImpactEffect;

	// 브레스 불기둥(원뿔)에 입히는 재질.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual")
	TSoftObjectPtr<UMaterialInterface> BreathFlameMaterial;

	// 땅에 처박힐 때의 폭발 구(반지름 1m 구 기준으로 키운다)와 재질.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual")
	TSoftObjectPtr<UStaticMesh> CrashBlastMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Dragon|Visual")
	TSoftObjectPtr<UMaterialInterface> CrashBlastMaterial;
	// 체력. 전함 무기에 맞춰 "잘 맞히면 2분 남짓, 못 맞히면 3분 남짓" 이 되게 잡았다.
	//
	// 계산(PGBattleshipActor 의 기본값): 주포 1초마다 직격 900 + 폭발 최대 1400,
	//   미사일 2.5초마다 두 발 × (직격 1500 + 폭발 최대 2000). 전부 맞으면 초당 약 5100.
	//   실제로는 3발 중 1발 남짓 맞는다고 보면 초당 1500~1700 → 200000 이면 약 2분.
	//   내려앉은 동안(약점)은 두 배로 들어가니 그 틈을 잘 노리면 더 빨라진다.
	// 죽을 때 "몇 초 동안 싸웠고 초당 얼마를 받았는지" 로그를 찍으니, 그 숫자를 보고 여기만 고치면 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Health", meta = (ClampMin = "1.0"))
	float MaxHealth = 200000.0f;
	// 선회 반지름·속도. 전함이 410m 라 넉넉히 돈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	float OrbitRadius = 60000.0f; // 600m. 몸이 커진 만큼 넉넉히 돈다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	float FlySpeed = 4500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	float ChargeSpeed = 9000.0f;
	// 선회하다가 몇 초마다 공격하나.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Attack", meta = (ClampMin = "1.0"))
	float AttackIntervalSeconds = 6.0f;
	// 돌진해서 전함에 주는 피해와, 공격 예고 시간(예고가 없으면 피할 수가 없다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Attack")
	float PassDamage = 1200.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Attack")
	float TellSeconds = 1.0f;

	// ── 브레스(공중에서 불 뿜기) ──
	// 입에서 이만큼까지 닿는다(cm). 선회 반지름이 600m 이고 배 반길이가 218m 라, 배 옆구리까지는 약 380m 다.
	//   입이 몸 앞쪽에 있어 그보다 조금 짧아도 닿는다. 450m 면 선회 자리에서 그대로 뿜어도 배에 닿는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath")
	float BreathRangeCm = 45000.0f;
	// 불길이 퍼지는 각도(중심선에서 한쪽으로, 도). 끝에서의 폭 = 사거리 × tan(각도). 12도면 끝에서 폭 약 190m.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath", meta = (ClampMin = "1.0", ClampMax = "45.0"))
	float BreathHalfAngleDeg = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath", meta = (ClampMin = "0.2"))
	float BreathSeconds = 2.5f;
	// 초당 피해. 표적마다 따로 둔다 — 전함(체력 20000)과 몬스터(수백)와 사람(100 남짓)은 단위가 다르다.
	//   전함: 한 번 뿜으면 1000. 공격 세 번에 한 번꼴로 내려앉으니 대략 45초에 3000 → 전함이 버티는 시간은 약 5분.
	//         드래곤(약 2분)보다 넉넉히 길어서 "제대로 쏘면 이긴다" 가 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath")
	float BreathShipDamagePerSecond = 400.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath")
	float BreathMonsterDamagePerSecond = 150.0f;
	// 사람은 약하게. 전함을 향해 뿜으면 갑판의 플레이어도 불길 안에 든다 — 스치기만 해도 죽으면 억울하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath")
	float BreathPlayerDamagePerSecond = 15.0f;
	// 불길 판정을 몇 초마다 하나. 매 프레임 하면 4060 에서 아깝다 — 0.2초면 눈으로는 이어져 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath", meta = (ClampMin = "0.05"))
	float BreathTraceInterval = 0.2f;
	// 불꽃 이펙트 크기. 원본은 횃불 불꽃이라 작다 — 화면을 보고 맞춘다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Breath")
	float BreathFxScale = 40.0f;

	// ── 표적 고르기 ──
	// 전함이 없거나(부서졌거나) 너무 멀면, 드래곤 밑(수평 거리) 이 반경 안에 보이는 몬스터를 노린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Target")
	float MonsterSeekRadiusCm = 15000.0f;
	// 전함이 이보다 멀면(플레이어가 몰고 도망가면) "지금은 못 닿는다" 로 보고 다음 순위로 넘어간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Target")
	float ShipEngageRangeCm = 120000.0f;
	// 몬스터도 없으면 이 반경 안의 플레이어.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Target")
	float PlayerSeekRadiusCm = 60000.0f;

	// ── 내려앉기·지면 공격 ──
	// 공중 공격을 이만큼 하면 한 번 내려앉는다. 0 이면 체력 50% 때 한 번만.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground", meta = (ClampMin = "0"))
	int32 AttacksBeforeLanding = 3;
	// 땅에 머무는 시간 = 약점 시간. 이 동안 포효 → 지면 공격을 되풀이하고, 끝나면 이륙한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground")
	float WeakPointSeconds = 10.0f;
	// 착지해서 포효하는 동안(약점) 받는 피해 배수.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground")
	float WeakPointMultiplier = 2.0f;
	// 지면 공격 한 번의 피해(중심)와 반경. 몸이 300m 짜리라 반경도 크게 — 90m.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground")
	float GroundAttackDamage = 400.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground")
	float GroundAttackRadiusCm = 9000.0f;
	// 착지 동작을 발이 땅에서 몸 높이의 몇 배쯤 떠 있을 때 시작하나.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground", meta = (ClampMin = "0.0"))
	float LandingStartHeightRatio = 0.25f;
	// 착지 동작의 몇 % 지점에서 발이 땅에 닿나. 그 순간에 맞춰 내려오는 속도를 정한다(동작을 보고 맞춘다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Ground", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float LandingTouchdownFraction = 0.6f;

	// ── 지상전(전함 추락 뒤) ──
	// 총·차 같은 지상 무기(피해 원인이 전함이 아닌 것)가 **땅에 선 드래곤**에게 주는 피해 배율.
	//
	// 왜 6 인가: 소총 AR70 은 한 발 35, 0.15초마다(PGWeaponComponent 의 RegisterDefaultWeapons) → 초당 약 233.
	//   드래곤 체력 200000 을 혼자 소총으로 깎으면 배율 1 에서 약 860초(14분), 3 이면 약 290초(5분) — 너무 길다.
	//   6 이면 초당 1400 → 약 143초(2분 20초). 전함전이 "잘 맞히면 2분 남짓" 이라 그와 비슷한 길이가 된다.
	//   둘이 쏘면 70초, 넷이면 36초. 게다가 지면 공격(400, 반경 90m)은 사람을 한 방에 죽이니 계속 쏘지는 못한다 —
	//   실제로는 이보다 길게 걸린다. 지상전에 들어올 때쯤이면 주포에 몇 번 맞아 체력이 깎여 있기도 하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|GroundFight", meta = (ClampMin = "0.0"))
	float GroundGunDamageMultiplier = 6.0f;
	// 같은 지상 무기가 **날고 있는 드래곤**에게 주는 배율. 맞기는 하지만 아프지 않다(9/21 사용자: "공중에서는 안 아프게").
	//   0.3 이면 소총 초당 약 70 → 혼자서는 48분. "긁힌다" 정도이고, 전함 주포(5발에 격추)가 여전히 답이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|GroundFight", meta = (ClampMin = "0.0"))
	float AirGunDamageMultiplier = 0.3f;
	// 땅에서 걷는 속도(cm/s). 사람이 뛰는 속도(약 6m/s)보다는 빠르고 차(약 30m/s)와 비슷하게 — 차를 타면 따돌릴 수는 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|GroundFight", meta = (ClampMin = "0.0"))
	float GroundWalkSpeed = 2800.0f;
	// 상대가 이보다 멀면(수평 cm) 걷지 않고 짧게 뛰어올라 자리를 옮긴다(TakeOff → Descend → 다시 지상전). 350m.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|GroundFight", meta = (ClampMin = "0.0"))
	float GroundHopDistanceCm = 35000.0f;
	// 뛰어 옮기기 사이의 최소 간격(초). 자주 뛰면 "지상전" 이 아니라 다시 공중전이 된다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|GroundFight", meta = (ClampMin = "0.0"))
	float GroundHopCooldownSeconds = 15.0f;
	// 뛰어 옮길 때 몸 높이의 몇 배까지 뜨나. 0.5 면 약 115m — 착지 동작을 틀 높이(0.25배)보다 조금 위.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|GroundFight", meta = (ClampMin = "0.1"))
	float GroundHopHeightRatio = 0.5f;

	// ── 죽음 ──
	// 마지막 한 방을 맞고 나서 떨어지기 시작할 때까지(초). 이 동안 몸이 흔들리고 뒤로 밀린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "0.0"))
	float DeathStrikeSeconds = 1.3f;

	// 죽는 동안 트는 동작 세 가지(동작 폴더 안의 이름). 9/23 블루프린트 분리 — BP_PGDragonBoss 에서 바꾼다.
	//   맞는 순간(공중, 위 DeathStrikeSeconds 동안) → 떨어지는 중(공중, 되풀이) → 땅에 닿은 뒤(한 번).
	// [9/23 수정] 맞는 순간 기본값이 예전엔 GetHitAnim 이었다. 그건 "땅에서 맞는" 동작이라 끝에 발을 딛고 선 자세로 멈춰,
	//   하늘에서 죽는데 한 번 착지하는 것처럼 보였다(사용자 지적). 공중 동작(FlyIdleAnim, 제자리 날갯짓)으로 바꿨다.
	//   맞는 느낌(몸 젖힘·떨림·밀림)은 동작이 아니라 코드(TickDying)가 몸을 흔들어 만들므로 그대로 남는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PG|Dragon|Death")
	FString DeathStruckAnimName = TEXT("FlyIdleAnim");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PG|Dragon|Death")
	FString DeathFallAnimName = TEXT("FlyGlideAnim");

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PG|Dragon|Death")
	FString DeathCrashAnimName = TEXT("DieAnim");
	// 추락 가속(cm/s²)과 최고 낙하 속도(cm/s). 중력(980)보다 세게 잡았다 — 300m 짜리 몸이 진짜 중력으로 떨어지면
	//   화면에서는 오히려 느릿해 보인다(크기 때문에 눈이 속도를 작게 읽는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "100.0"))
	float DeathFallAccel = 1400.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "100.0"))
	float DeathFallMaxSpeed = 9000.0f;
	// 떨어지며 옆으로 밀려가는 속도(cm/s)의 아래·위 한계. 맵 밖(산 위)에서 죽으면 땅에 닿기 전에 맵 안으로
	//   들어와야 하므로, 필요한 속도를 "거리 ÷ 떨어지는 데 걸리는 시간" 으로 계산하고 이 사이로 자른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "0.0"))
	float DeathDriftMinSpeed = 2500.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "0.0"))
	float DeathDriftMaxSpeed = 15000.0f;
	// 땅에 처박힌 뒤 주변 소품을 몇 초 동안 계속 날리나(한 번에 다 날리면 프레임이 죽는다 — KnockAround 주석).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "0.0"))
	float CrashKnockSeconds = 2.0f;
	// 죽는 동작이 끝나고 몇 초 뒤에 사라지나. 0 = 사라지지 않고 그대로 남는다(쓰러진 보스가 곧 승리의 증거다).
	//   사라질 때도 땅속으로 가라앉히지 않는다 — 그냥 없어진다(9/21 사용자: "산 밑으로 꺼지는" 것이 싫다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Dragon|Death", meta = (ClampMin = "0.0"))
	float CorpseSeconds = 0.0f;

protected:
	// 책임별 협력 객체를 만든다(BeginPlay 맨 앞). 드래곤 액터는 이들을 순서대로 부르기만 한다.
	void CreateCollaborators();
	// ---- 협력 객체: 드래곤 죽음 (PGDragonDeath.h) ----
	friend class UPGDragonDeath;
	UPROPERTY(Transient)
	TObjectPtr<class UPGDragonDeath> Death;
	// ---- 협력 객체: 드래곤 지상전 (PGDragonGroundCombat.h) ----
	friend class UPGDragonGroundCombat;
	UPROPERTY(Transient)
	TObjectPtr<class UPGDragonGroundCombat> GroundCombat;
	// ---- 협력 객체: 드래곤 공중전 (PGDragonAirCombat.h) ----
	friend class UPGDragonAirCombat;
	UPROPERTY(Transient)
	TObjectPtr<class UPGDragonAirCombat> AirCombat;
	void EnterState(EPGDragonState NewState);
	// 동작을 튼다. 튼 동작의 길이(초)를 돌려준다 — 못 틀었으면 0.
	float PlayAnim(const TCHAR* Name, bool bLoop);
	UPROPERTY(ReplicatedUsing = OnRep_AnimCue)
	FPGDragonAnimCue AnimCue;
	UFUNCTION()
	void OnRep_AnimCue();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// ---- 멀티(9/27): 보이는 것만 모든 컴퓨터에 ----
	// 불뿜기 켜기/끄기. 서버에서 부르면 복제되고, 이 컴퓨터에 화면이 있으면 바로 그린다.
	void SetBreath(bool bOn, const FVector& From = FVector::ZeroVector, const FVector& Dir = FVector::ForwardVector, float Length = 0.0f);
	UPROPERTY(ReplicatedUsing = OnRep_BreathCue)
	FPGDragonBreathCue BreathCue;
	UFUNCTION()
	void OnRep_BreathCue();
	bool bBreathShownLocal = false; // 클라이언트 로그용: 이 화면의 불길이 켜져 있나
	// 흙먼지 한 번(브레스 탄착·착지·지면 공격·땅에서 쓰러짐). 서버가 부르면 모든 화면에 띄운다.
	void PlayImpactFx(const FVector& At, float Scale);
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastImpactFx(FVector_NetQuantize At, float Scale);
	// 하늘에서 처박힘: Wave 0 = 폭발 구 + 흔들림 + 먼지, 1·2 = 뒤이어 번지는 먼지. Feet.Z 가 땅 높이다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastCrashFx(FVector_NetQuantize Feet, uint8 Wave);
	// 지금 공중에 떠 있나. 공중이면 지상 동작을 안 튼다(.cpp 의 PlayAnim 주석 참고).
	bool IsAirborne() const;
	// 안전거리 안으로 들어와 있으면 껍데기 밖으로 되돌린다. 날고 있는 동안 **매 틱** 부른다.
	void KeepClearOfShip();
	// 보이는 몸(메시 상자)이 선체 상자(여유 포함) 안으로 들어가면 가장 짧은 쪽으로 밀어낸다. KeepClearOfShip 다음에 부른다(.cpp 설명).
	void KeepBodyOutOfHull();
	// Where 둘레(반경 Reach)의 타일·소품을 날린다(땅을 뚫고 올라오는 그림, 지면 공격, 추락).
	void KnockAround(const FVector& Where, float Reach);
	// 타일 범위 안쪽으로 자른 자리. Inset 만큼 가장자리에서 더 안으로.
	FVector2D ClampToMap(const FVector2D& Want, float InsetCm) const;
	// 타일이 깔린 범위(월드 XY). 산·둘레 바닥판은 충돌이 없어 이 밖에서는 땅 찾기가 실패한다.
	bool ResolveMapBox(FBox2D& OutBox) const;
	void FaceAndMove(const FVector& Target, float Speed, float DeltaSeconds);
	// 그 자리에서 몸만 돌린다(수평으로).
	void FaceToward(const FVector& Target, float DeltaSeconds);

	// 액터 원점에서 발바닥까지 내려가는 거리(cm). 메시가 원점보다 이만큼 아래 붙어 있다.
	float FeetOffsetCm() const;
	static const TCHAR* StateName(EPGDragonState InState);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	TObjectPtr<USkeletalMeshComponent> Mesh;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	TObjectPtr<UCapsuleComponent> Hull; // 맞는 몸(아랫도리). 액터 원점이 여기 있다
	// 몸통·머리를 덮는 두 번째 맞는 몸. 왜 둘인지는 .cpp 의 BeginPlay 주석 참고.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Dragon")
	TObjectPtr<UCapsuleComponent> Torso;

	UPROPERTY(Transient)
	TObjectPtr<APGBattleshipActor> Ship;

	// 불꽃(횃불 불꽃을 크게 늘려 입에서 앞으로 몇 개 늘어놓는다)과, 불이 땅·배에 닿은 자리의 먼지.
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> FireFxAsset;
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> ImpactFxAsset;

	EPGDragonState State = EPGDragonState::Rising;
	bool bFloorClampLogged = false; // 최저 높이로 들어 올린 일을 로그에 한 번만 남긴다
	// 플레이어별로 드래곤에게 준 피해(서버). 죽을 때 AwardTopDamageDealer 가 가장 큰 사람을 고른다.
	TMap<TWeakObjectPtr<AController>, float> DamageByController;
	void AwardTopDamageDealer();
	float Health = 0.0f;
	float StateTimer = 0.0f;
	bool bUsedWeakPoint = false;
	FVector PassTarget = FVector::ZeroVector;
	bool bPassDamaged = false;
	double LastKnockTime = -100.0;
	double LastClampLogTime = -100.0; // 안전망이 걸렸다는 로그를 1초에 한 번만 찍으려고
	// 선체 상자 피하기(KeepBodyOutOfHull). 상자는 배 기준 좌표라 배가 돌아도 맞고, 부품이 늦게 붙을 수 있어 가끔 다시 잰다.
	FBox HullAvoidBox = FBox(ForceInit);
	double HullBoxMeasuredAt = -100.0;
	bool bHullAvoidActive = false; // 지금 밀어내는 중인가 — "한 번 붙을 때 한 번만" 로그를 찍으려고
	double LastHealthLogTime = -100.0;
	double FightStartTime = -1.0;     // 처음 선회에 들어간 시각. 죽을 때 "몇 초 싸웠나" 를 찍는다
	// BeginPlay 에서 에셋을 직접 재서 채운다(모델을 바꿔도 안전거리가 알아서 맞도록).
	float BodyHeightCm = 23120.0f;  // 재기 전의 임시값(SoulEater 기준)
	// 액터 원점에서 메시가 가로로 가장 멀리 뻗은 거리. "반날개폭"이라 부르지만 실제로는
	// 원점에서 가장 먼 모서리까지의 거리다 — 피벗이 몸 한가운데가 아니어서 둘이 다르다(.cpp 참고).
	float HalfWingCm = 20580.0f;
	// 이번 돌진에서 배 중심까지 실제로 얼마나 가까워졌나(cm). 안전거리와 대조해 로그로 찍는다.
	float PassClosest = 0.0f;
	bool bPassLeft = false;         // 돌진할 때 배의 왼쪽/오른쪽 번갈기
	bool bGliding = false;          // 선회 중 활공/날갯짓 번갈기
	float LastGlideSwap = 0.0f;
	// 지금 "나는 동작(반복)"을 틀고 있나. 한 번짜리 동작(이륙·공격)이 끝나면 여기로 되돌린다.
	bool bFlapping = false;
	int32 AttacksSinceLanding = 0;
	bool bWarnedNoMesh = false;
	// 한 번짜리 공격 동작을 이번 상태에서 이미 틀었나, 그 길이는 얼마인가.
	// 전에는 "0.1초 창 안에서 한 번" 으로 골랐는데, 그 순간 프레임이 튀면 동작이 통째로 빠졌다.
	bool bAttackSwung = false;
	float AttackAnimSeconds = 0.0f;

	// 브레스
	TWeakObjectPtr<AActor> AttackTarget;
	TArray<TWeakObjectPtr<AActor>> BreathCandidates; // 브레스 시작 때 한 번 모은 몬스터·플레이어
	FBox ShipLocalBox = FBox(ForceInit);             // 브레스 시작 때 한 번 잰 전함 크기(전함 기준 좌표)
	FName MouthBone = NAME_None;
	bool bBreathing = false;
	int32 BreathTickCount = 0;
	int32 BreathShipHits = 0;
	int32 BreathMonsterHits = 0;
	int32 BreathPlayerHits = 0;

	// 내려앉기·땅·이륙·추락
	float GroundZ = 0.0f;
	bool bLandingAnimStarted = false;
	float DescendRate = 0.0f;       // 착지 동작 중 내려오는 속도(cm/s)
	float LandingTailSeconds = 0.0f; // 발이 닿은 뒤 착지 동작이 남은 시간
	float NextLandedAction = 0.0f;
	float PendingGroundHit = -1.0f; // 지면 공격 피해가 들어갈 시각(StateTimer 기준). 음수 = 없음
	int32 LandedStep = 0;
	bool bGroundFlame = false;
	bool bLifted = false;           // 이륙 동작에서 발이 땅을 떠났나
	FVector TakeOffTo = FVector::ZeroVector;
	bool bOnGround = false;         // 추락해서 땅에 닿았나
	float FallSpeed = 0.0f;

	// 지상전
	bool bHadShip = false;          // BeginRise 로 전함을 받은 적이 있나. 전함이 지워져도(GC) "전함이 있었다" 를 기억한다
	bool bGroundFight = false;      // 전함 추락 → 지상전. 한 번 켜지면 안 꺼진다
	bool bDescendToSpot = false;    // 내려앉기(Descend)가 지금 자리가 아니라 DescendSpot 으로 날아가서 앉나
	FVector2D DescendSpot = FVector2D::ZeroVector;
	bool bHopping = false;          // 지상전의 "뛰어 옮기기" 중(TakeOff → Descend). 이륙이 선회로 안 간다
	FVector2D HopSpot = FVector2D::ZeroVector;
	double LastHopWorldTime = -100.0;
	bool bWalking = false;          // 걷는 동작을 틀어 놓았나
	bool bIdling = false;           // 상대가 없어 서 있는 동작을 틀어 놓았나
	double LastGunLogTime = -100.0; // "gun hit x6.0" 로그를 0.5초에 한 번만

	// 죽음(TickDying 위 주석 참고)
	EPGDragonDeathStep DeathStep = EPGDragonDeathStep::Struck;
	FVector LastHitFrom = FVector::ZeroVector;   // 마지막 한 방이 날아온 자리. 그 반대쪽으로 밀린다
	FVector DeathDrift = FVector::ZeroVector;    // 떨어지며 옆으로 밀려가는 속도(cm/s, 수평)
	FVector DeathJitter = FVector::ZeroVector;   // 맞은 직후 몸 떨림(메시 상대 위치에 더한 값)
	float LastGroundProbe = -10.0f;              // 떨어지는 동안 땅 높이를 다시 잰 시각(StateTimer)
	bool bDeathOutsideMap = false;               // 땅을 못 찾아 기준 높이(20)를 쓰고 있나 — 로그를 한 번만
	int32 CrashDustWave = 0;
};
