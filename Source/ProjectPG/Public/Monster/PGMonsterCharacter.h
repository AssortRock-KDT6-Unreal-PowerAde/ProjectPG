// 임시 몬스터. 팀 저장소에 몬스터 클래스가 아직 없어서(2026-09-16 전 브랜치 확인) 여기서 만든다.
//
// 하는 일은 "맞으면 체력이 깎이고, 0이 되면 시체가 되어 루팅된다" 까지다.
// 시체 루팅은 새로 만들지 않고 UPGLootableComponent(OBJ-053 ~ 056)를 붙여 쓴다.
// 애니메이션은 애님 블루프린트 없이 PlayAnimation 으로 돌린다. 팩에 ABP가 없고, 에디터에서
// 만들지 않아도 코드만으로 서 있기·달리기·공격·죽기가 보이게 하려는 것.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "PGMonsterCharacter.generated.h"

class UAnimSequence;
class UNavigationInvokerComponent;
class UPGLootableComponent;
class USkeletalMesh;

// 몬스터 세력 (기획 v0.4 3.3.5). 다른 세력끼리는 서로 싸운다. None 은 세력 없음(콘솔 스폰·보스) - 플레이어만 공격.
//  A: 수 많고 약함, 짧은 리스폰   B: 중간, 긴 리스폰   C: 적고 강함, 리스폰 없음
UENUM(BlueprintType)
enum class EPGMonsterFaction : uint8
{
	None,
	A,
	B,
	C,
};

// 메시 + 애니 한 벌. 콘솔에서 종류를 바꿔 스폰할 때 쓴다.
USTRUCT(BlueprintType)
struct FPGMonsterVisuals
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<USkeletalMesh> Mesh;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Idle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Run;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Attack;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Hit;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Die;
	// 프리셋별 손맛. 0 이면 클래스 기본값. 램페이지처럼 팔이 긴 놈은 사거리가 길어야 헛손질을 안 한다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float AttackRange = 0.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float HealthScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float DamageScale = 1.0f;
	// 공격 모션이 여러 개면 여기에. 비어 있으면 Attack 하나만 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TArray<TSoftObjectPtr<UAnimSequence>> AttackVariants;
	// 잠복(변기·화분) 상태의 대기 모션과, 깨어날 때 한 번 트는 변신 모션. 둘 다 있으면 잠복 가능.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> DormantIdle;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UAnimSequence> Wake;
	// 캡슐 중심이 원점이라 메시는 발밑까지 내린다. 팩마다 기준 방향이 달라 회전도 같이 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector MeshOffset = FVector(0.0f, 0.0f, -88.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FRotator MeshRotation = FRotator(0.0f, -90.0f, 0.0f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CapsuleRadius = 42.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) float CapsuleHalfHeight = 88.0f;
};

// [멀티 임시수정 2026-09-28 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md "몬스터 공격·맞는 동작")
// 무엇: 서버가 튼 한 번짜리 동작(공격·맞음·벽 부수기 휘두르기) 한 줄. 번호(Seq)는 같은 동작을 다시 틀어도 값이 바뀌어 클라에 다시 오게.
// 왜: PlayOnce 가 서버에서만 불려, 전용 서버 판에서는 클라 화면의 몬스터가 휘두르지 않고 피해만 줬다.
USTRUCT()
struct FPGMonsterOnceCue
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UAnimSequence> Anim;
	UPROPERTY()
	uint8 Seq = 0;
};
// [멀티 임시수정] 끝

UCLASS()
class PROJECTPG_API APGMonsterCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	APGMonsterCharacter();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	bool IsDead() const { return bDead; }

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	float GetMaxHealth() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	float GetAttackRange() const { return AttackRange; }

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	bool IsAttacking() const { return bAttacking; }

	// 공격 중이거나 한 번짜리 모션(피격·변신)이 끝나기 전. AI는 이때 이동 명령을 안 낸다.
	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	bool IsBusy() const;

	// 잠복: 플레이어가 WakeRange 안에 올 때까지 DormantIdle 로 서 있다가 Wake 모션 후 움직인다.
	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	bool IsDormant() const { return bDormant; }

	// 표적까지의 거리. 사람·몬스터(캐릭터)는 지금처럼 중심 거리, 차·탱크처럼 큰 몸은 몸 표면까지.
	// 탱크(길이 약 8m)를 중심 거리로 재면 몬스터가 차체에 막혀 영영 사거리(1.5m 안팎)에 못 들어가 비비기만 했다.
	static float DistanceToTarget(const AActor* From, const AActor* Target);

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	float GetWakeRange() const { return WakeRange; }

	// 서버 전용. 잠복을 풀고 변신 모션을 튼다. 모션이 끝날 때까지 IsBusy.
	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	void Wake();

	// 서버 전용. 표적을 놓치면 다시 잠복 자세로 돌아간다(잠복으로 시작한 몬스터만).
	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	void Sleep();

	// 서버 전용. 사거리·간격이 맞으면 공격 애니를 틀고, 잠시 뒤 실제 피해를 준다.
	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	bool TryAttack(AActor* Target);

	// 서버 전용. 표적이 막힌 곳(공장 안 등)에 있어 길이 없을 때, 표적 쪽 장애물을 공격 모션으로 부순다.
	// 몸집 3배 이상(CanKnockProps)만. 부쉈거나 휘둘렀으면 true.
	bool SmashObstacleToward(const AActor* Target);

	// 나와 표적 사이를 건물·벽(정적·동적 월드 물체)이 막고 있지 않은지. 몬스터·잔해는 무시.
	// 없을 때는 건물 안의 플레이어를 보스가 벽 너머로 때렸다(팔이 벽을 뚫고 보이고, 건물은 멀쩡).
	bool HasClearLineTo(const AActor* Target) const;

	// 길찾기 없이 직선으로 쫓아도 되는 거대 몸인지(8배 보스급: 부수기 배율 3 이상). 가는 길의 건물을 다 부술 수 있으니 돌아갈 이유가 없다.
	bool CanSmashThrough() const { return CanKnockProps() && GetKnockRatio() >= 3.0f; }
	// 소품을 밀쳐내는 거인인가(보스·탑승 로봇·큰 크리처). AI 가 "발밑 크기 몹은 표적 아님"을 가를 때 쓴다.
	bool IsGiant() const { return CanKnockProps(); }

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	UPGLootableComponent* GetLootable() const { return Lootable; }

	UFUNCTION(BlueprintCallable, Category = "PG|Monster")
	void SetVisuals(const FPGMonsterVisuals& InVisuals);

	// 콘솔 스폰용 프리셋. 이름은 팩 폴더와 같다(Slime, Cactus, Beholder, ChestMonster).
	static bool GetPresetVisuals(FName Preset, FPGMonsterVisuals& OutVisuals);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Monster")
	EPGMonsterFaction Faction = EPGMonsterFaction::None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float MaxHealth = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float AttackDamage = 15.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float AttackRange = 200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float AttackInterval = 1.5f;

	// 공격 애니 시작 후 실제 타격까지의 시간. 휘두르는 순간에 맞게 보이도록.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float AttackHitDelay = 0.4f;

	// 시체 루팅 테이블. 세력별로 LT_CorpseA/B/C.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	FName LootTableId = TEXT("LT_CorpseA");

	// 다 털린 시체를 치우기까지의 시간.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float CorpseLifetimeAfterEmpty = 5.0f;

	// 아무도 루팅하지 않아도 이 시간이 지나면 시체를 치운다. 0 이면 안 치운다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float CorpseLifetime = 60.0f;

	// 스폰 직후 잠복 상태로 시작할지. Visuals 에 DormantIdle 이 있어야 의미가 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	bool bStartDormant = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float WakeRange = 1200.0f;

	// 0 이면 감각(시야·청각)이 끊긴 뒤 LoseTargetSeconds 만에 포기. 0 보다 크면 그 거리 안의 표적은 안 보여도 계속 쫓는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float AggroKeepRadius = 0.0f;

	// 영역(목줄). 0 이면 없음. HomeLocation 에서 이만큼 벗어나면 AI 가 쫓기를 그만두고 돌아간다. HomeLocation 은 스폰 자리(BeginPlay).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float LeashRadius = 0.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Monster")
	FVector HomeLocation = FVector::ZeroVector;

	// 0 이면 AI 컨트롤러 기본 시야. 세력별로 다르게 줄 때 스포너가 넣는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	float SightRadiusOverride = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Monster")
	FPGMonsterVisuals Visuals;

protected:
	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Dormant();

	UFUNCTION()
	void HandleLootEmptied(UPGLootableComponent* InLootable, APawn* Pawn);

	// 로봇이 덮어쓴다: 사람이 타고 있으면 먼저 내려 준다(빙의가 끊기면 플레이어가 몸 없이 남는다).
	virtual void Die(AController* Killer);

	// 큰 몸이 걸어가며 앞의 소품·벽 판자를 밀쳐낸다(차량과 같은 PGPhysicsUtil). 원래 로봇에만 있었는데
	// B 세력 크리처가 5배로 커지면서 같은 게 필요해져 몬스터로 올렸다. 조건과 세기만 로봇이 덮어쓴다.
	//  - CanKnockProps: 기본은 몸집 3배 이상. 로봇은 "보스이거나 사람이 탔을 때".
	//  - GetKnockRatio: 2.5배 몸을 1로 본 세기·크기 한도 배율. 5배 크리처 = 2 → 반지름 10m 까지 날린다.
	virtual bool CanKnockProps() const { return GetActorScale3D().X >= 3.0f; }
	virtual float GetKnockRatio() const { return GetActorScale3D().X / 2.5f; }
	void KnockAhead();
	// KnockAhead 의 본체. 방향·반경 배율을 받는다(걸으며 밀치기 = 속도 방향 ×1, 벽 부수기 = 표적 방향 ×SmashRadiusScale).
	int32 KnockToward(const FVector& Direction, float RadiusScale);
	float LastSmashTime = -1000.0f;
	// 큰 몸(보스·탑승 로봇·5배 크리처)이 발에 걸린 작은 몬스터를 옆으로 걷어찬다. 없을 때는 슬라임 하나에 17m 로봇이 막혀 서 있었다.
	// 내 캡슐 반지름의 40% 이하인 몬스터만. 처리했으면(쿨다운 중 포함) true.
	bool KickSmallMonster(AActor* OtherActor, const FVector& Direction);
	// 나보다 작은 탈것(차·탱크)을 밀쳐 날린다. 크기는 "내 키의 절반" 대 "탈것 경계 구 반지름"으로 비교 — 큰 쪽만 작은 쪽을 날린다.
	// 탈것이 아니거나 나보다 크면 false. 날렸거나(쿨다운 중 포함) 처리했으면 true.
	bool ShoveVehicle(AActor* OtherActor, const FVector& Direction);
	TMap<TWeakObjectPtr<AActor>, float> LastKickTime;
	void ApplyAttackHit();
	void EndAttack();
	void PlayLoop(const TSoftObjectPtr<UAnimSequence>& Anim);
	float PlayOnce(const TSoftObjectPtr<UAnimSequence>& Anim);
	// [멀티 임시수정 2026-09-28 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md "몬스터 공격·맞는 동작")
	// 서버: PlayOnce + 모두에게 같은 동작(OnceCue). 깨어남(OnRep_Dormant)·죽음(OnRep_Dead)은 원래 클라에서도 트니 쓰지 않는다.
	float PlayOnceForAll(const TSoftObjectPtr<UAnimSequence>& Anim);
	UPROPERTY(ReplicatedUsing = OnRep_OnceCue)
	FPGMonsterOnceCue OnceCue;
	UFUNCTION()
	void OnRep_OnceCue();
	// [멀티 임시수정] 끝

	UPROPERTY(ReplicatedUsing = OnRep_Dead, VisibleInstanceOnly, Category = "PG|Monster")
	bool bDead = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|Monster")
	float Health = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Dormant, VisibleInstanceOnly, Category = "PG|Monster")
	bool bDormant = false;

	UPROPERTY(VisibleAnywhere, Category = "PG|Monster")
	TObjectPtr<UPGLootableComponent> Lootable;

	// 이 맵은 인보커 주변에만 내비메시를 만든다. 몬스터도 자기 주변을 요청해야 걸어 다닌다.
	UPROPERTY(VisibleAnywhere, Category = "PG|Monster")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentLoop;

	TWeakObjectPtr<AActor> PendingAttackTarget;
	FTimerHandle AttackHitTimer;
	FTimerHandle AttackEndTimer;
	FTimerHandle CorpseTimer;
	float LastAttackTime = -1000.0f;
	float AnimLockUntil = -1.0f;
	bool bAttacking = false;
};
