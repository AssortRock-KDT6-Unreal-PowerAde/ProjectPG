// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "AI/CustomEnemyAIController.h"
#include "AI/EnemyAILog.h"
#include "AI/EnemyAIConfig.h"
#include "AI/EnemyAITags.h"
#include "AI/EnemyCoverFinderComponent.h"
#include "AI/EnemyNoiseTags.h"
#include "AI/EnemyVoiceComponent.h"
#include "Characters/CustomEnemyCharacter.h"
#include "Equipments/FirearmEquipment.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "Components/StateTreeAIComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Damage.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Damage.h"
#include "Perception/AISense_Hearing.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "StateTree.h"
#include "TimerManager.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogEnemyAI);

ACustomEnemyAIController::ACustomEnemyAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 매 프레임 함수는 싸우는 동안만 켠다(SetCombatStance). 판단은 상태 트리, 감지는 Perception 이 따로 돈다.
	// 왜 켜야 하나: 엔진은 "목표 쪽으로 시선(ControlRotation) 돌리기(SetFocus)" 를 이 컨트롤러의 매 프레임 함수 안에서만 한다.
	//     꺼 두었더니 시선이 처음 방향 그대로라 몬스터가 딴 데를 보고 그쪽 허공에 쐈다(2026-10-19 사용자 시험).
	// 부담: 목표가 있는 몬스터만, 시선 계산 한 번.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 눈: 감지 거리는 몬스터 표(EnemyTable)를 읽은 뒤 StartAI 에서 다시 맞춘다.
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;  // 플레이어는 팀 번호가 없어 "중립"으로 잡힌다.
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	SightConfig->SetMaxAge(0.f);
	// 맞으면 쏜 사람을 안다(뒤에서 맞아도 돌아봄).
	DamageConfig = CreateDefaultSubobject<UAISenseConfig_Damage>(TEXT("DamageConfig"));
	// 귀: 총소리와 동료 외침을 듣는다. 듣는 거리는 StartAI 에서 AIConfig.HearingRange 로 맞춘다.
	// 소리를 낸 "사람"으로는 그 소리가 가리키는 플레이어가 들어온다(EnemyNoiseTags.h 참고).
	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	UAIPerceptionComponent* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
	Perception->ConfigureSense(*SightConfig);
	Perception->ConfigureSense(*DamageConfig);
	Perception->ConfigureSense(*HearingConfig);
	Perception->SetDominantSense(UAISense_Sight::StaticClass());
	SetPerceptionComponent(*Perception);

	CoverFinder = CreateDefaultSubobject<UEnemyCoverFinderComponent>(TEXT("CoverFinder"));
}

ACustomEnemyCharacter* ACustomEnemyAIController::GetEnemy() const
{
	return Cast<ACustomEnemyCharacter>(GetPawn());
}

void ACustomEnemyAIController::BeginPlay()
{
	Super::BeginPlay();
	StartAI();
}

void ACustomEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	StartAI();
}

void ACustomEnemyAIController::StartAI()
{
	ACustomEnemyCharacter* Enemy = GetEnemy();
	if (bAIStarted || !HasAuthority() || !Enemy || !HasActorBegunPlay() || !Enemy->HasActorBegunPlay()) return;
	bAIStarted = true;

	// 표나 설정이 비면 반쯤 움직이게 두지 않고 멈춘 채 이유를 남긴다.
	if (!Enemy->HasEnemyData())
	{
		UE_LOG(LogEnemyAI, Error, TEXT("[EnemyAI] %s: EnemyTable 행 '%s' 이 없어 AI 를 켜지 않음"), *GetNameSafe(Enemy), *Enemy->GetCharacterID().ToString());
		return;
	}
	Config = Enemy->GetAIConfig();
	if (!Config || !Config->StateTree)
	{
		UE_LOG(LogEnemyAI, Error, TEXT("[EnemyAI] %s: 몬스터 BP 의 AIConfig(또는 그 안의 StateTree)가 비어 AI 를 켜지 않음. PGBuildEnemyAI 명령을 돌렸는지 확인"), *GetNameSafe(Enemy));
		return;
	}

	PatrolOrigin = Enemy->GetActorLocation();
	const FEnemyTableRow& Data = Enemy->GetEnemyData();
	SightConfig->PeripheralVisionAngleDegrees = Config->SightHalfAngleDegrees;
	// 눈을 뒤로 물린 자리에서 각도를 잰다 → 몸 둘레 CloseAwarenessDistance 안은 옆·뒤도 보인다(EnemyAIConfig.h 설명).
	// 엔진이 감지 거리에도 이 값을 더하므로 그만큼 빼서 표의 감지 거리를 그대로 지킨다.
	SightConfig->PointOfViewBackwardOffset = Config->CloseAwarenessDistance;
	SightConfig->NearClippingRadius = 0.f;
	SightConfig->SightRadius = FMath::Max(100.f, Data.DetectionRange - Config->CloseAwarenessDistance);
	SightConfig->LoseSightRadius = FMath::Max(SightConfig->SightRadius, Data.DetectionRange * Config->LoseSightRangeScale - Config->CloseAwarenessDistance);
	UAIPerceptionComponent* Perception = GetPerceptionComponent();
	Perception->ConfigureSense(*SightConfig);
	HearingConfig->HearingRange = Config->HearingRange;
	Perception->ConfigureSense(*HearingConfig);
	Perception->RequestStimuliListenerUpdate();
	Perception->OnTargetPerceptionUpdated.AddUniqueDynamic(this, &ThisClass::HandlePerceptionUpdated);

	if (UCharacterAttributeSet* Attributes = Enemy->GetCharacterAttributeSet())
		Attributes->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleOwnHealthChanged);

	// 상태 트리 컴포넌트는 에셋을 넣은 뒤에 붙인다.
	// 왜: 생성자에서 미리 붙이면 에셋이 빈 채로 초기화되어 엔진이 "State Tree asset is not set" Error 를 낸다.
	if (!StateTreeComponent)
	{
		StateTreeComponent = NewObject<UStateTreeAIComponent>(this, TEXT("StateTree"));
		StateTreeComponent->SetStartLogicAutomatically(false);
		StateTreeComponent->SetStateTree(Config->StateTree);
		StateTreeComponent->RegisterComponent();
	}
	StateTreeComponent->StartLogic();
	UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: AI 시작 (감지 %.0f, 사거리 %.0f)"), *GetNameSafe(Enemy), Data.DetectionRange, Data.AttackRange);
}

void ACustomEnemyAIController::OnUnPossess()
{
	ShutdownAI(TEXT("빙의 해제"));
	bAIStarted = false;
	Super::OnUnPossess();
}

void ACustomEnemyAIController::EndPlay(const EEndPlayReason::Type Reason)
{
	ShutdownAI(TEXT("EndPlay"));
	Super::EndPlay(Reason);
}

void ACustomEnemyAIController::ShutdownAI(const TCHAR* Reason)
{
	if (StateTreeComponent) StateTreeComponent->StopLogic(Reason);
	CancelMove();
	CancelCoverSearch();
	StopFire();
	if (UAIPerceptionComponent* Perception = GetPerceptionComponent())
		Perception->OnTargetPerceptionUpdated.RemoveDynamic(this, &ThisClass::HandlePerceptionUpdated);
	if (ACustomEnemyCharacter* Enemy = GetEnemy())
		if (UCharacterAttributeSet* Attributes = Enemy->GetCharacterAttributeSet())
			Attributes->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleOwnHealthChanged);
	if (Target.IsValid())
	{
		// 목표 쪽에 걸어 둔 알림만 떼고, 상태 트리는 이미 멈췄으니 알림은 안 보낸다.
		if (UCharacterAttributeSet* Attributes = Target->GetCharacterAttributeSet())
			Attributes->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleTargetHealthChanged);
		Target->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleTargetDestroyed);
	}
	Target.Reset();
	bTargetVisible = false;
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(LoseTargetTimer);
	if (GetPawn()) SetCombatStance(false);
}

// ───────────── 목표 정하기 / 놓치기 ─────────────

bool ACustomEnemyAIController::IsValidTarget(const AActor* Actor) const
{
	// 몬스터끼리는 서로 노리지 않는다. 플레이어 클래스 이름 대신 공용 부모(ACustomCharacter)만 본다.
	const ACustomCharacter* Candidate = Cast<ACustomCharacter>(Actor);
	return Candidate && !Candidate->IsA<ACustomEnemyCharacter>() && !Candidate->IsDead() && Candidate->CanBeHit();
}

void ACustomEnemyAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!IsValidTarget(Actor)) return;
	ACustomCharacter* Candidate = CastChecked<ACustomCharacter>(Actor);
	const bool bSight = Stimulus.Type == UAISense::GetSenseID<UAISense_Sight>();
	const bool bHeard = Stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>();
	const bool bCallout = bHeard && Stimulus.Tag == EnemyNoiseTags::EnemyCallout;
	const bool bDamaged = Stimulus.Type == UAISense::GetSenseID<UAISense_Damage>();
	// 소리는 들린 순간 한 번만 쓴다. 귀에서 "잊힘" 알림이 와도 할 일이 없다(포기 시계는 눈이 맡음).
	if (bHeard && !Stimulus.WasSuccessfullySensed()) return;

	// 새 목표 정하기:
	// - 목표가 없을 때 → 본 사람이든 쏜 사람이든, 총소리·동료 외침이 가리키는 사람이든 목표로.
	// - 목표가 있지만 지금 안 보일 때 → 눈에 보이는 다른 사람으로 바꿈.
	if (Candidate != Target.Get())
	{
		// 맞았을 때도 지금 목표가 안 보이면 쏜 사람으로 바꾼다(맞으면 바로 반격, 2026-10-19 사용자 요청).
		const bool bTake = !Target.IsValid() || ((bSight || bDamaged) && Stimulus.WasSuccessfullySensed() && !bTargetVisible);
		if (!bTake || !Stimulus.WasSuccessfullySensed()) return;
		if (bHeard)
			UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: %s 듣고 %s 쪽으로 감"), *GetNameSafe(GetPawn()), bCallout ? TEXT("동료 외침") : TEXT("총소리"), *GetNameSafe(Candidate));
		SetTarget(Candidate);
	}

	// 마지막으로 안 위치:
	// - 동료 외침 → 소리 난 자리는 외친 동료라서, 동료가 본 플레이어 위치를 쓴다.
	// - 그 밖(눈·총소리·피격) → 감각이 알려 준 자리.
	LastKnownTargetLocation = bCallout ? Candidate->GetActorLocation() : Stimulus.StimulusLocation;
	// 맞은 순간: 눈 감각이 다음에 확인해 줄 때까지 기다리지 않고, 쏜 사람까지 시야 선이 뚫려 있으면 바로 "보임" 으로 친다.
	// 왜: 맞고도 한참 멍하니 있다가 반응해 멍청해 보였다. 벽 뒤라 안 뚫리면 아래처럼 마지막 위치로 찾아간다.
	const bool bSeenNow = (bSight && Stimulus.WasSuccessfullySensed()) || (bDamaged && Stimulus.WasSuccessfullySensed() && LineOfSightTo(Candidate));
	if (bSeenNow)
	{
		const bool bWasVisible = bTargetVisible;
		const bool bTargetEverSeenBefore = bTargetEverSeen;
		bTargetVisible = true;
		bTargetEverSeen = true;
		GetWorldTimerManager().ClearTimer(LoseTargetTimer);
		if (!bWasVisible)
		{
			// 보자마자 쏘지 않고 조준 시간을 준다: 처음 본 목표는 ReactionTime, 다시 본 목표는 ReacquireReactionTime.
			// 그사이 외침이 들리고 몬스터가 몸을 돌리는 게 보인다.
			const bool bFirstSight = !bTargetEverSeenBefore;
			FireAllowedTime = GetWorld()->GetTimeSeconds() + (bFirstSight ? Config->ReactionTime : Config->ReacquireReactionTime);
			SendTreeEvent(EnemyAITags::TargetSeen);
			ShoutTargetSpotted();
		}
		return;
	}

	// 시야에서 빠졌거나(sight 실패) 맞기만 하고 못 봤을 때 → 정해진 시간 뒤 포기.
	// 그 사이 다시 보이면 위에서 타이머를 지운다.
	if (bSight) bTargetVisible = false;
	if (!bTargetVisible && !GetWorldTimerManager().IsTimerActive(LoseTargetTimer))
	{
		// 포기까지 기다리는 시간:
		// - 한 번이라도 눈으로 본 목표 → LoseTargetDelay (숨으면 금방 수색으로 넘어감)
		// - 소리로만 안 목표 → HeardTargetDelay (소리 난 쪽까지 걸어갈 시간)
		const float Delay = bTargetEverSeen ? Config->LoseTargetDelay : Config->HeardTargetDelay;
		if (Delay <= 0.f) ForgetTarget(TEXT("시야에서 사라짐"));
		else GetWorldTimerManager().SetTimer(LoseTargetTimer, this, &ThisClass::HandleLoseTargetTimeout, Delay, false);
	}
}

void ACustomEnemyAIController::ShoutTargetSpotted()
{
	ACustomEnemyCharacter* Enemy = GetEnemy();
	// 쉬는 시간(AIConfig.TargetSpottedCooldown) 안이면 외치지도, 동료를 부르지도 않는다(숨바꼭질마다 외치지 않게).
	if (!Enemy || !Target.IsValid() || !Enemy->GetVoice()->TryPlayLine(EEnemyVoiceLine::TargetSpotted)) return;
	// 외친 자리에서 CalloutRange 안의 동료 귀에 "이 플레이어 봤다" 를 보낸다.
	// 동료가 외침을 듣고 목표를 정해도, 그 동료는 자기 눈으로 볼 때까지 다시 외치지 않는다(외침이 끝없이 번지지 않게).
	UAISense_Hearing::ReportNoiseEvent(GetWorld(), Enemy->GetActorLocation(), 1.f, Target.Get(), Config->CalloutRange, EnemyNoiseTags::EnemyCallout);
	UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: %s 발견, 외침 (반경 %.0f)"), *GetNameSafe(Enemy), *GetNameSafe(Target.Get()), Config->CalloutRange);
}

void ACustomEnemyAIController::SetTarget(ACustomCharacter* NewTarget)
{
	if (Target.Get() == NewTarget) return;
	if (Target.IsValid())
	{
		if (UCharacterAttributeSet* Attributes = Target->GetCharacterAttributeSet())
			Attributes->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleTargetHealthChanged);
		Target->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleTargetDestroyed);
	}
	const bool bHadTarget = Target.IsValid();
	Target = NewTarget;
	bTargetVisible = false;
	bTargetEverSeen = false;
	GetWorldTimerManager().ClearTimer(LoseTargetTimer);  // 전 목표를 포기하려던 시계는 새 목표와 상관없음
	LastKnownTargetLocation = NewTarget->GetActorLocation();
	// 목표가 죽거나 사라지는 순간을 그쪽 알림으로 받는다(매번 살아 있는지 확인하지 않으려고).
	if (UCharacterAttributeSet* Attributes = NewTarget->GetCharacterAttributeSet())
		Attributes->OnHealthChanged.AddUniqueDynamic(this, &ThisClass::HandleTargetHealthChanged);
	NewTarget->OnDestroyed.AddUniqueDynamic(this, &ThisClass::HandleTargetDestroyed);
	SetFocus(NewTarget);
	SetCombatStance(true);
	UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: 목표 %s %s"), *GetNameSafe(GetPawn()), bHadTarget ? TEXT("바꿈 →") : TEXT("정함 →"), *GetNameSafe(NewTarget));
	SendTreeEvent(EnemyAITags::TargetFound);
}

void ACustomEnemyAIController::ForgetTarget(const TCHAR* Reason)
{
	if (!Target.IsValid() && !bTargetVisible) return;
	if (Target.IsValid())
	{
		LastKnownTargetLocation = bTargetVisible ? Target->GetActorLocation() : LastKnownTargetLocation;
		if (UCharacterAttributeSet* Attributes = Target->GetCharacterAttributeSet())
			Attributes->OnHealthChanged.RemoveDynamic(this, &ThisClass::HandleTargetHealthChanged);
		Target->OnDestroyed.RemoveDynamic(this, &ThisClass::HandleTargetDestroyed);
	}
	UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: 목표 %s 포기 (%s)"), *GetNameSafe(GetPawn()), *GetNameSafe(Target.Get()), Reason);
	Target.Reset();
	bTargetVisible = false;
	GetWorldTimerManager().ClearTimer(LoseTargetTimer);
	StopFire();
	ClearFocus(EAIFocusPriority::Gameplay);
	SetCombatStance(false);
	SendTreeEvent(EnemyAITags::TargetLost);
}

void ACustomEnemyAIController::HandleLoseTargetTimeout()
{
	ForgetTarget(TEXT("오래 안 보임"));
}

void ACustomEnemyAIController::HandleTargetHealthChanged(float Current, float Maximum)
{
	if (Current <= 0.f) ForgetTarget(TEXT("목표 죽음"));
}

void ACustomEnemyAIController::HandleTargetDestroyed(AActor* DestroyedActor)
{
	if (DestroyedActor == Target.Get()) ForgetTarget(TEXT("목표 사라짐"));
}

void ACustomEnemyAIController::HandleOwnHealthChanged(float Current, float Maximum)
{
	if (Current > 0.f) return;
	UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: 죽어서 AI 멈춤"), *GetNameSafe(GetPawn()));
	ShutdownAI(TEXT("죽음"));
}

void ACustomEnemyAIController::SetCombatStance(bool bInCombat)
{
	// 싸우는 동안만 매 프레임 시선을 목표 쪽으로 맞춘다(생성자 주석). 순찰 중엔 가는 쪽을 보므로 필요 없다.
	SetActorTickEnabled(bInCombat);
	ACustomEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy) return;
	// 싸울 땐 이동 방향과 상관없이 목표를 보고(옆걸음), 아닐 땐 가는 쪽을 본다.
	Enemy->GetCharacterMovement()->bOrientRotationToMovement = !bInCombat;
	Enemy->GetCharacterMovement()->bUseControllerDesiredRotation = bInCombat;
	Enemy->SetAimTracking(bInCombat);
	if (!bInCombat) Enemy->UnCrouch();
}

void ACustomEnemyAIController::SendTreeEvent(const FGameplayTag& Tag)
{
	if (StateTreeComponent && StateTreeComponent->GetStateTreeRunStatus() == EStateTreeRunStatus::Running)
		StateTreeComponent->SendStateTreeEvent(Tag);
}

// ───────────── 상태 트리 조건이 읽는 값 ─────────────

bool ACustomEnemyAIController::CanShootTarget() const
{
	const ACustomEnemyCharacter* Enemy = GetEnemy();
	return Enemy && IsTargetVisible()
		&& FVector::DistSquared(Enemy->GetActorLocation(), Target->GetActorLocation()) <= FMath::Square(Enemy->GetEnemyData().AttackRange);
}

bool ACustomEnemyAIController::IsCoverSearchReady() const
{
	const ACustomEnemyCharacter* Enemy = GetEnemy();
	return Config && Config->CoverQuery && Enemy && Enemy->GetEnemyData().CoverSearchRadius > 0.f
		&& CanShootTarget() && CoverFinder->IsCooldownOver();
}

bool ACustomEnemyAIController::PickPatrolPoint(FVector& OutPoint) const
{
	const ACustomEnemyCharacter* Enemy = GetEnemy();
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Enemy || !Nav || Enemy->GetEnemyData().PatrolRadius <= 0.f) return false;
	FNavLocation Point;
	if (!Nav->GetRandomReachablePointInRadius(PatrolOrigin, Enemy->GetEnemyData().PatrolRadius, Point)) return false;
	OutPoint = Point.Location;
	return true;
}

// ───────────── 이동 (시간 제한 있음) ─────────────

ACustomEnemyAIController::EMoveStart ACustomEnemyAIController::MoveWithTimeout(const FVector& Goal, const TCHAR* What, TFunction<void(bool)> OnDone)
{
	FAIMoveRequest Request(Goal);
	Request.SetAcceptanceRadius(Config->MoveAcceptanceRadius);
	Request.SetAllowPartialPath(false);
	return StartMove(Request, What, MoveTemp(OnDone));
}

ACustomEnemyAIController::EMoveStart ACustomEnemyAIController::MoveToTargetWithTimeout(TFunction<void(bool)> OnDone)
{
	const ACustomEnemyCharacter* Enemy = GetEnemy();
	if (!Target.IsValid() || !Enemy) return EMoveStart::Failed;
	FAIMoveRequest Request(Target.Get());
	// 멈추는 거리:
	// - 보이는 목표 → 사거리 × ApproachRangeScale, 단 ApproachMaxStopDistance 를 넘지 않음(사거리가 커도 실제로 다가오게)
	// - 안 보이는 목표 → ApproachBlindStopDistance 까지 바짝 가서 직접 찾음
	const float StopDistance = bTargetVisible
		? FMath::Min(Enemy->GetEnemyData().AttackRange * Config->ApproachRangeScale, Config->ApproachMaxStopDistance)
		: Config->ApproachBlindStopDistance;
	Request.SetAcceptanceRadius(StopDistance);
	Request.SetAllowPartialPath(true);  // 목표가 길 밖(지붕 등)에 있어도 갈 수 있는 데까지는 간다.
	return StartMove(Request, TEXT("목표에게 접근"), MoveTemp(OnDone));
}

ACustomEnemyAIController::EMoveStart ACustomEnemyAIController::StartMove(const FAIMoveRequest& Request, const TCHAR* What, TFunction<void(bool)> OnDone)
{
	// 전 이동의 끝 알림이 새 이동에 섞이지 않게 먼저 비운다.
	PendingMoveDone = nullptr;
	GetWorldTimerManager().ClearTimer(MoveTimeoutTimer);
	const FPathFollowingRequestResult Result = MoveTo(Request);
	if (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal) return EMoveStart::AlreadyThere;
	if (Result.Code != EPathFollowingRequestResult::RequestSuccessful)
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[EnemyAI] %s: %s 를 시작 못 함(길 없음)"), *GetNameSafe(GetPawn()), What);
		return EMoveStart::Failed;
	}
	PendingMoveId = Result.MoveId;
	PendingMoveWhat = What;
	PendingMoveDone = MoveTemp(OnDone);
	PendingMoveLimit = GetMoveTimeLimit();
	GetWorldTimerManager().SetTimer(MoveTimeoutTimer, this, &ThisClass::HandleMoveTimeout, PendingMoveLimit, false);
	return EMoveStart::Started;
}

float ACustomEnemyAIController::GetMoveTimeLimit() const
{
	// 시간 제한 = 예상 걸리는 시간(길 길이 ÷ 걷는 속도) × 배수 + 여유.
	// 왜: 먼 순찰 지점은 원래 오래 걸려서, 고정 초로 두면 멀쩡한 이동도 "막힘"으로 끊겼다.
	const UPathFollowingComponent* Follow = GetPathFollowingComponent();
	const FNavPathSharedPtr Path = Follow ? Follow->GetPath() : nullptr;
	const ACustomEnemyCharacter* Enemy = GetEnemy();
	const float Speed = Enemy ? FMath::Max(1.f, Enemy->GetCharacterMovement()->GetMaxSpeed()) : 1.f;
	const float Expected = Path.IsValid() ? Path->GetLength() / Speed : 0.f;
	return Expected * Config->MoveTimeoutScale + Config->MoveTimeoutSlack;
}

void ACustomEnemyAIController::OnMoveCompleted(FAIRequestID RequestID, const FPathFollowingResult& Result)
{
	Super::OnMoveCompleted(RequestID, Result);
	if (!PendingMoveDone || RequestID != PendingMoveId) return;
	GetWorldTimerManager().ClearTimer(MoveTimeoutTimer);
	if (!Result.IsSuccess())
		UE_LOG(LogEnemyAI, Warning, TEXT("[EnemyAI] %s: %s 이동 실패 (%s)"), *GetNameSafe(GetPawn()), *PendingMoveWhat, *Result.ToString());
	TFunction<void(bool)> Done = MoveTemp(PendingMoveDone);
	PendingMoveDone = nullptr;
	Done(Result.IsSuccess());
}

void ACustomEnemyAIController::HandleMoveTimeout()
{
	if (!PendingMoveDone) return;
	UE_LOG(LogEnemyAI, Error, TEXT("[EnemyAI] %s: %s 가 %.1f초 안에 안 끝나 멈춤(끼었거나 막힘)"), *GetNameSafe(GetPawn()), *PendingMoveWhat, PendingMoveLimit);
	TFunction<void(bool)> Done = MoveTemp(PendingMoveDone);
	PendingMoveDone = nullptr;
	StopMovement();
	Done(false);
}

void ACustomEnemyAIController::CancelMove()
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(MoveTimeoutTimer);
	if (!PendingMoveDone) return;
	PendingMoveDone = nullptr;
	StopMovement();
}

// ───────────── 엄폐 찾기 (실제 일은 UEnemyCoverFinderComponent) ─────────────

bool ACustomEnemyAIController::FindCoverAsync(TFunction<void(bool)> OnDone)
{
	if (!Config->CoverQuery && !bWarnedNoCoverQuery)
	{
		UE_LOG(LogEnemyAI, Warning, TEXT("[EnemyAI] %s: AIConfig.CoverQuery 가 비어 엄폐 없이 싸움"), *GetNameSafe(GetPawn()));
		bWarnedNoCoverQuery = true;
	}
	return CoverFinder->StartSearch(GetEnemy(), Target.Get(), *Config, MoveTemp(OnDone));
}

void ACustomEnemyAIController::CancelCoverSearch()
{
	if (CoverFinder) CoverFinder->Cancel();
}

const FVector& ACustomEnemyAIController::GetCoverLocation() const
{
	return CoverFinder->GetCoverLocation();
}

const FVector& ACustomEnemyAIController::GetPeekLocation() const
{
	return CoverFinder->GetPeekLocation();
}

// ───────────── 사격 ─────────────

bool ACustomEnemyAIController::UpdateFire()
{
	ACustomEnemyCharacter* Enemy = GetEnemy();
	if (!Enemy || !IsTargetVisible())
	{
		StopFire();
		return false;
	}
	// 몸이 아직 목표 쪽으로 덜 돌았으면 방아쇠를 놓고 기다린다(옆을 보며 쏘지 않게).
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(Enemy->GetActorRotation().Yaw, Enemy->GetControlRotation().Yaw)) > Config->AimToleranceDegrees)
	{
		StopFire();
		return true;
	}
	AFirearmEquipment* Weapon = Enemy->GetActiveFirearm();
	if (!IsValid(Weapon))
	{
		StopFire();
		return true;
	}
	if (Weapon->GetLoadedAmmo() <= 0)
	{
		StopFire();
		Enemy->Reload();
		return true;
	}
	// 본 직후 조준 시간(FireAllowedTime) 전이면 방아쇠를 안 당기고 조준만 유지한다.
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < FireAllowedTime)
	{
		StopFire();
		return true;
	}
	// 연사 총은 방아쇠를 누르고 있으면 알아서 계속 쏜다 → 점사로 끊는다:
	// - 점사 중(BurstEndTime 전) → 계속 누름
	// - 점사 끝 → 떼고 BurstPauseRange 초 쉼 → 쉬는 시간이 지나면 다음 점사
	// 단발 총은 누를 때 한 발만 나가서, 확인할 때마다 눌렀다 뗀다.
	// (연사 여부는 캐릭터 쪽 규칙과 같게 맞춤: CustomCharacterCombat.cpp 의 HandleFireInput)
	if (Weapon->GetWeaponType() == EWeaponType::Rifle)
	{
		if (bFireHeld && Now >= BurstEndTime)
		{
			StopFire();
			NextBurstTime = Now + FMath::FRandRange(Config->BurstPauseRange.X, FMath::Max(Config->BurstPauseRange.X, Config->BurstPauseRange.Y));
			return true;
		}
		if (!bFireHeld && Now >= NextBurstTime)
		{
			Enemy->OnReq_SetFireInput(true);
			bFireHeld = true;
			BurstEndTime = Now + Config->BurstFireSeconds;
		}
	}
	else
	{
		Enemy->OnReq_SetFireInput(true);
		Enemy->OnReq_SetFireInput(false);
		bFireHeld = false;
	}
	return true;
}

void ACustomEnemyAIController::StopFire()
{
	if (!bFireHeld) return;
	bFireHeld = false;
	if (ACustomEnemyCharacter* Enemy = GetEnemy()) Enemy->OnReq_SetFireInput(false);
}
