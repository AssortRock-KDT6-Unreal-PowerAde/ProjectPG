#include "Monster/MonsterAIController.h"
#include "Common/PGSoundRouter.h"

#include "Monster/AI/PGMonsterAIStats.h"
#include "Monster/AI/StateTree/PGMonsterStateTreeComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "StateTree.h"

#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/Pawn.h"
#include "Monster/PGMonsterCharacter.h"
#include "Robot/PGRobotCharacter.h"
#include "Navigation/PathFollowingComponent.h"
#include "Objects/PGObjectTypes.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"

AMonsterAIController::AMonsterAIController()
{
	PrimaryActorTick.bCanEverTick = true;

	AIPerceptionComponent = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// StateTree 방식의 판단 부품. 스스로 시작하지 않는다 — 방식이 정해지면(ResolveMode) 컨트롤러가 켠다.
	StateTreeComponent = CreateDefaultSubobject<UPGMonsterStateTreeComponent>(TEXT("MonsterStateTree"));
	StateTreeComponent->SetStartLogicAutomatically(false);
	MonsterStateTree = TSoftObjectPtr<UStateTree>(FSoftObjectPath(TEXT("/Game/PG/AI/ST_PGMonster.ST_PGMonster")));

	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	// 40m 였을 때는 넓은 맵에서 차로 지나가면 거의 반응이 없었다.
	SightConfig->SightRadius = 6000.0f;
	SightConfig->LoseSightRadius = 7000.0f;
	SightConfig->PeripheralVisionAngleDegrees = 80.0f;
	SightConfig->SetMaxAge(5.0f);
	// 팀 구분(Team ID)을 아직 안 쓰므로 전부 감지하고, 누구를 쫓을지는 IsValidTarget 이 고른다.
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	HearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	HearingConfig->HearingRange = 4500.0f;
	HearingConfig->SetMaxAge(5.0f);
	HearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	HearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	HearingConfig->DetectionByAffiliation.bDetectFriendlies = true;

	AIPerceptionComponent->ConfigureSense(*SightConfig);
	AIPerceptionComponent->ConfigureSense(*HearingConfig);
	AIPerceptionComponent->SetDominantSense(SightConfig->GetSenseImplementation());
}

void AMonsterAIController::BeginPlay()
{
	Super::BeginPlay();
	if (IsValid(AIPerceptionComponent))
		AIPerceptionComponent->OnTargetPerceptionUpdated.AddDynamic(this, &AMonsterAIController::HandlePerceptionUpdated);
}

void AMonsterAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
	// 8배짜리 보스 로봇은 60m 만 보면 자기 발밑 주변도 좁다. 로봇만 시야를 몸 크기에 비례해 키운다(기본 2.5배 기준).
	// 몬스터(5배 크리처)는 키우지 않는다: 키웠더니 시야가 120m 가 돼서 플레이어가 스폰되자마자 멀리서 달려왔다.
	const bool bRobot = IsValid(InPawn) && InPawn->IsA<APGRobotCharacter>();
	const float Scale = bRobot ? FMath::Max(1.0f, InPawn->GetActorScale3D().X / 2.5f) : 1.0f;
	const APGMonsterCharacter* PossessedMonster = Cast<APGMonsterCharacter>(InPawn);
	// 세력별 시야(B 크리처 35m)가 있으면 그걸 쓴다.
	const float Sight = (IsValid(PossessedMonster) && PossessedMonster->SightRadiusOverride > 0.0f) ? PossessedMonster->SightRadiusOverride : 6000.0f * Scale;
	if (IsValid(SightConfig) && IsValid(AIPerceptionComponent))
	{
		SightConfig->SightRadius = Sight;
		SightConfig->LoseSightRadius = Sight + 1000.0f;
		AIPerceptionComponent->ConfigureSense(*SightConfig);
		AIPerceptionComponent->RequestStimuliListenerUpdate();
	}
}

bool AMonsterAIController::IsValidTarget(const AActor* Actor) const
{
	// 이름을 Pawn 으로 두면 AController::Pawn 멤버를 가려서 경고(C4458)가 오류로 잡힌다.
	const APawn* Candidate = Cast<const APawn>(Actor);
	if (!IsValid(Candidate) || Candidate == GetPawn() || Candidate->IsHidden())
		return false;
	// 영역이 있는 몬스터: 돌아가는 중이면 누구도 표적이 아니고, 영역 밖에 있는 대상은 처음부터 쫓지 않는다.
	// (지역변수 이름을 Owner 로 두면 AActor::Owner 를 가려 C4458 경고=오류. Pawn 과 같은 함정.)
	if (const APGMonsterCharacter* LeashedSelf = Cast<APGMonsterCharacter>(GetPawn()); IsValid(LeashedSelf) && LeashedSelf->LeashRadius > 0.0f)
	{
		if (bReturningHome || FVector::Dist2D(Candidate->GetActorLocation(), LeashedSelf->HomeLocation) > LeashedSelf->LeashRadius)
			return false;
	}
	// 플레이어가 조종하는 것은 무엇이든 표적(캐릭터든, 타고 있는 차·로봇이든).
	// 단 DefaultPawn(날아다니는 기본 카메라 폰)은 아니다. PIE 시작 순간 에디터 카메라 자리에 잠깐 생겼다가 검증 캐릭터로 바뀌는데,
	// 그 자리가 워존이면 보스가 이걸 보고 깨어나 몬스터를 쫓아다니며 벽을 다 날렸다(플레이어는 멀리서 착장 테스트 중이었다).
	if (Candidate->IsA<ADefaultPawn>())
		return false;
	if (Candidate->IsPlayerControlled())
		return true;
	// 다른 세력 몬스터는 표적(세력 충돌 시 서로 전투). 같은 세력·세력 없음·시체는 아니다.
	if (const APGMonsterCharacter* Other = Cast<APGMonsterCharacter>(Candidate))
	{
		if (Other->IsDead())
			return false;
		const APGMonsterCharacter* Self = Cast<APGMonsterCharacter>(GetPawn());
		if (!IsValid(Self))
			return false;
		// 잠복(변기·화분)은 세력 싸움에 끼지 않는다. 양쪽 다:
		//  - 내가 잠복 중이면 몬스터에는 반응하지 않는다(플레이어만 나를 깨운다).
		//  - 상대가 잠복 중이면 그건 그냥 변기다. 때리지 않는다.
		// 이게 없으니 플레이어가 도착하기도 전에 지나가던 다른 세력이 보스를 깨웠고, 보스가 걔들을 쫓아다니며 중앙 건물을 다 부숴 놨다.
		if (Self->IsDormant() || Other->IsDormant())
			return false;
		// 거인(보스·탑승 로봇·크리처)에게 발밑 크기(반지름 40% 이하)의 몬스터는 표적이 아니다. 공격 모션으로 슬라임을 때리는 게 어색했다(9/20).
		// 걸어가다 발에 걸리면 KickSmallMonster 가 밟아 죽이거나 걷어찬다 — 거인에게 작은 몹은 "밟히는" 존재다.
		if (Self->IsGiant() && Other->GetCapsuleComponent()->GetScaledCapsuleRadius() <= Self->GetCapsuleComponent()->GetScaledCapsuleRadius() * 0.4f)
			return false;
		return Self->Faction != EPGMonsterFaction::None && Other->Faction != EPGMonsterFaction::None && Other->Faction != Self->Faction;
	}
	// 그 외(맵 검증용 걷기 캐릭터, 빈 탈것 등)는 표적이 아니다. 플레이어가 안 왔는데 보스가 검증 캐릭터를 패고 있었다.
	return false;
}

bool AMonsterAIController::TickLeash(APGMonsterCharacter* Monster, float Now)
{
	if (Monster->LeashRadius <= 0.0f || Monster->IsDormant())
		return false;
	const float FromHome = FVector::Dist2D(Monster->GetActorLocation(), Monster->HomeLocation);
	if (!bReturningHome)
	{
		if (FromHome <= Monster->LeashRadius)
			return false;
		// 영역을 크게 벗어났다: 표적을 버리고 집으로.
		UE_LOG(LogPGObjects, Display, TEXT("%s: left its area (%.0fm from home > %.0fm), returning home, dropped target %s"),
			*GetNameSafe(Monster), FromHome / 100.0f, Monster->LeashRadius / 100.0f, *GetNameSafe(Target.Get()));
		bReturningHome = true;
		PGMonsterAIStats::CountReturnHome();
		Target = nullptr;
		ClearFocus(EAIFocusPriority::Gameplay);
		LastReturnMoveTime = -1000.0f;
	}
	// 영역 반경의 40% 안까지 들어오면 다시 싸운다. 경계 바로 안쪽에서 풀어 주면 다시 끌려 나가 왔다 갔다 한다.
	if (FromHome <= Monster->LeashRadius * 0.4f)
	{
		bReturningHome = false;
		StopMovement();
		UE_LOG(LogPGObjects, Display, TEXT("%s: back home"), *GetNameSafe(Monster));
		return false;
	}
	if (!Monster->IsBusy() && Now - LastReturnMoveTime >= 1.0f)
	{
		LastReturnMoveTime = Now;
		// 길이 없으면(내비 밖) 집 쪽으로 그냥 걷는다. 추격과 같은 보험.
		PGMonsterAIStats::CountMoveRequest();
		if (MoveToLocation(Monster->HomeLocation, Monster->LeashRadius * 0.2f) == EPathFollowingRequestResult::Failed)
			Monster->AddMovementInput((Monster->HomeLocation - Monster->GetActorLocation()).GetSafeNormal2D());
	}
	return true;
}

AActor* AMonsterAIController::FindPerceivedTarget(bool* bOutTargetStillPerceived) const
{
	if (bOutTargetStillPerceived)
		*bOutTargetStillPerceived = false;
	const APawn* Self = GetPawn();
	if (!IsValid(AIPerceptionComponent) || !IsValid(Self))
		return nullptr;
	TArray<AActor*> Perceived;
	AIPerceptionComponent->GetCurrentlyPerceivedActors(nullptr, Perceived); // nullptr = 시각·청각 전부
	AActor* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	for (AActor* Actor : Perceived)
	{
		if (bOutTargetStillPerceived && Actor == Target.Get())
			*bOutTargetStillPerceived = true;
		if (!IsValidTarget(Actor))
			continue;
		// 가까운 순이 아니라 점수 순(플레이어 우선, TargetScore).
		const float Score = TargetScore(Actor);
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Actor;
		}
	}
	return Best;
}

AActor* AMonsterAIController::FindCloseTarget() const
{
	const APGMonsterCharacter* Self = Cast<APGMonsterCharacter>(GetPawn());
	UWorld* World = GetWorld();
	if (!IsValid(Self) || !World || CloseSenseRadius <= 0.0f)
		return nullptr;
	// 몸 가장자리 기준: 5배 크리처는 캡슐 반지름만 몇 m 라, 중심에서 6m 로 재면 몸 안쪽이다.
	const float Radius = Self->GetCapsuleComponent()->GetScaledCapsuleRadius() + CloseSenseRadius;
	AActor* Best = nullptr;
	float BestDistSq = Radius * Radius;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APawn* PlayerPawn = It->IsValid() ? (*It)->GetPawn() : nullptr;
		if (!IsValid(PlayerPawn) || !IsValidTarget(PlayerPawn))
			continue;
		const float DistSq = FVector::DistSquared2D(PlayerPawn->GetActorLocation(), Self->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = PlayerPawn;
		}
	}
	return Best;
}

void AMonsterAIController::NotifyDamagedBy(AActor* Attacker)
{
	if (!IsValidTarget(Attacker))
		return;
	// 플레이어를 쫓는 중에 다른 세력 몬스터에게 맞았다고 플레이어를 버리지 않는다(반대로 플레이어에게 맞으면 바로 돌아본다).
	// 단, 바로 옆(사거리 1.5배 안)에서 때리는 몬스터는 받아친다. 그것까지 무시했더니 보스가 크리처에게 맞기만 하고 서 있었다.
	// 받아친 몬스터가 죽거나 멀어지면 표적 점수(TargetScore, 플레이어 우선)로 다시 플레이어에게 돌아간다.
	if (const APawn* AttackerPawn = Cast<APawn>(Attacker); IsValid(AttackerPawn) && !AttackerPawn->IsPlayerControlled())
	{
		const APGMonsterCharacter* Self = Cast<APGMonsterCharacter>(GetPawn());
		const bool bInMelee = IsValid(Self) && APGMonsterCharacter::DistanceToTarget(Self, AttackerPawn) <= Self->GetAttackRange() * 1.5f;
		if (const APawn* CurrentPawn = Cast<APawn>(Target.Get()); !bInMelee && IsValidTarget(CurrentPawn) && CurrentPawn->IsPlayerControlled())
			return;
		// 받아치는 동안(4초)은 표적을 바꾸지 않는다. 안 그러면 한 대 치러 돌아섰다가 바로 플레이어 쪽으로 돌아서기를 반복했다.
		if (bInMelee)
			RetaliateUntil = GetWorld()->GetTimeSeconds() + 4.0f;
	}
	if (Target.Get() != Attacker)
	{
		UE_LOG(LogPGObjects, Display, TEXT("%s: hit by %s, targeting it"), *GetNameSafe(GetPawn()), *GetNameSafe(Attacker));
		PGMonsterAIStats::CountTargetAcquired();
	}
	Target = Attacker;
	LastSensedTime = GetWorld()->GetTimeSeconds();
}

float AMonsterAIController::TargetScore(const AActor* Candidate) const
{
	// 낮을수록 좋은 표적. 거리 그대로에 몬스터는 ×3: 같은 거리면 언제나 플레이어가 먼저다.
	// 보스가 30m 앞의 플레이어를 두고 10m 옆을 지나가던 다른 세력 슬라임을 쫓아 엉뚱한 데로 걸어갔다.
	const APawn* Self = GetPawn();
	const APawn* CandidatePawn = Cast<const APawn>(Candidate);
	if (!IsValid(Self) || !IsValid(CandidatePawn))
		return TNumericLimits<float>::Max();
	const float Distance = FVector::Dist(CandidatePawn->GetActorLocation(), Self->GetActorLocation());
	return CandidatePawn->IsPlayerControlled() ? Distance : Distance * 3.0f;
}

bool AMonsterAIController::ShouldSwitchTarget(const AActor* Candidate) const
{
	const AActor* Current = Target.Get();
	if (!IsValidTarget(Current))
		return true;
	if (GetWorld()->GetTimeSeconds() < RetaliateUntil)
		return false;
	if (Candidate == Current)
		return false;
	// 지금 표적보다 30% 넘게 좋아야 바꾼다. 비슷한 거리의 플레이어 둘 사이에서 매 순간 갈아타며 제자리에서 두리번거리지 않게(멀티).
	return TargetScore(Candidate) < TargetScore(Current) * 0.7f;
}

void AMonsterAIController::HandlePerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	if (!IsValidTarget(Actor) || !Stimulus.WasSuccessfullySensed())
		return;
	// 예전에는 무엇이든 새로 감지되면 바로 표적을 바꿨다. 그래서 보스가 지나가던 다른 세력 몬스터에게 끌려갔다.
	if (Actor == Target.Get())
	{
		LastSensedTime = GetWorld()->GetTimeSeconds();
		return;
	}
	if (!ShouldSwitchTarget(Actor))
		return;
	UE_LOG(LogPGObjects, Verbose, TEXT("%s: target %s -> %s"), *GetNameSafe(GetPawn()), *GetNameSafe(Target.Get()), *GetNameSafe(Actor));
	PGMonsterAIStats::CountTargetAcquired();
	Target = Actor;
	LastSensedTime = GetWorld()->GetTimeSeconds();
}

// 코드 방식 ↔ StateTree 방식. 새로 생기는 몬스터부터 적용된다(이미 도는 몬스터는 처음 정한 방식 그대로).
static TAutoConsoleVariable<int32> CVarPGMonsterAIMode(
	TEXT("PG.MonsterAI.Mode"), 1,
	TEXT("Monster AI decision layer: 0 = classic (if-chain in Tick), 1 = StateTree (ST_PGMonster). Command line: -PGMonsterAI=Classic|StateTree."));

void AMonsterAIController::ResolveMode()
{
	if (bModeResolved)
		return;
	bModeResolved = true;
	bool bWantStateTree = CVarPGMonsterAIMode.GetValueOnGameThread() != 0;
	FString CommandLineMode;
	if (FParse::Value(FCommandLine::Get(), TEXT("PGMonsterAI="), CommandLineMode))
		bWantStateTree = !CommandLineMode.Equals(TEXT("Classic"), ESearchCase::IgnoreCase);
	UStateTree* Tree = bWantStateTree ? MonsterStateTree.LoadSynchronous() : nullptr;
	bUseStateTree = IsValid(Tree) && IsValid(StateTreeComponent);
	static bool bLogged = false; // 몬스터마다 찍으면 넘친다 — 판 전체에서 한 번
	if (bWantStateTree && !bUseStateTree)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGMonsterAI: StateTree asset %s missing — using classic (run PG.BuildMonsterStateTree in the editor)"), *MonsterStateTree.ToString());
	}
	else if (!bLogged)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGMonsterAI: decision layer = %s"), bUseStateTree ? TEXT("StateTree") : TEXT("Classic"));
	}
	bLogged = true;
	PGMonsterAIStats::SetModeName(bUseStateTree ? TEXT("StateTree") : TEXT("Classic"));
	if (bUseStateTree)
	{
		StateTreeComponent->SetStateTree(Tree);
		// 나무는 컨트롤러가 감각(표적 고르기)을 마친 뒤에 돈다 — 같은 프레임의 새 표적을 보고 판단하게.
		StateTreeComponent->AddTickPrerequisiteActor(this);
		StateTreeComponent->SetComponentTickEnabled(true);
	}
	else if (IsValid(StateTreeComponent))
	{
		StateTreeComponent->SetComponentTickEnabled(false); // 코드 방식이면 부품은 아무것도 안 한다
	}
}

void AMonsterAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 여기서부터 끝까지가 "몬스터가 판단하는 데 든 시간"(측정, PGMonsterAIStats). 부모 Tick 은 두 방식이 같아서 뺀다.
	PGMonsterAIStats::FThinkScope ThinkScope;
	ResolveMode();

	APGMonsterCharacter* Monster = Cast<APGMonsterCharacter>(GetPawn());
	if (!IsValid(Monster) || Monster->IsDead())
	{
		StopMovement();
		if (bUseStateTree && StateTreeComponent->IsRunning())
			StateTreeComponent->StopLogic(TEXT("monster dead"));
		return;
	}
	// 표적이 없거나 잠복 중이면 0.25초에 한 번만 생각한다. 표적이 생기면 매 프레임.
	// StateTree 부품도 같은 간격으로 돈다 — "한가할 땐 덜 생각한다" 는 두 방식 공통 규칙이지 판단 틀의 차이가 아니다.
	const float Interval = (!Target.IsValid() || Monster->IsDormant()) ? 0.25f : 0.0f;
	SetActorTickInterval(Interval);
	const float Now = GetWorld()->GetTimeSeconds();

	if (!bUseStateTree)
	{
		TickClassic(Monster, Now);
		return;
	}
	if (!StateTreeComponent->IsRunning())
		StateTreeComponent->StartLogic();
	// StateTree 방식: 여기서는 감각만. 무엇을 할지는 나무(ST_PGMonster)가 고른다.
	// 나무의 틱 간격은 나무가 칸마다 정한다(한가한 칸 0.25초, 싸우는 칸 매 프레임 — PGMonsterStateTreeBuilder).
	// 집으로 돌아가는 동안은 감각도 쉰다(코드 방식과 같다 — 그동안은 누구도 표적이 아니다).
	if (!bReturningHome && UpdateTargetFromSenses(Monster, Now) && !Monster->IsDormant()
		&& StateTreeComponent->GetComponentTickInterval() > 0.0f)
	{
		// 쉬는 칸(0.25초 간격)에 있는데 표적이 생겼다: 나무를 다음 프레임에 바로 돌게 깨운다.
		// 안 깨우면 최대 0.25초 늦게 반응한다 — 코드 방식은 표적을 찾은 그 틱에 바로 움직인다.
		StateTreeComponent->SetComponentTickIntervalAndCooldown(0.0f);
	}
}

APGMonsterCharacter* AMonsterAIController::GetMonster() const
{
	return Cast<APGMonsterCharacter>(GetPawn());
}

AActor* AMonsterAIController::GetLiveTarget() const
{
	AActor* TargetActor = Target.Get();
	if (!IsValidTarget(TargetActor))
		return nullptr;
	if (const APGMonsterCharacter* TargetMonster = Cast<APGMonsterCharacter>(TargetActor); TargetMonster && TargetMonster->IsDead())
		return nullptr;
	return TargetActor;
}

bool AMonsterAIController::ShouldReturnHome() const
{
	const APGMonsterCharacter* Monster = GetMonster();
	if (!IsValid(Monster) || Monster->LeashRadius <= 0.0f || Monster->IsDormant())
		return false;
	return bReturningHome || FVector::Dist2D(Monster->GetActorLocation(), Monster->HomeLocation) > Monster->LeashRadius;
}

bool AMonsterAIController::TickReturnHome()
{
	APGMonsterCharacter* Monster = GetMonster();
	return IsValid(Monster) && TickLeash(Monster, GetWorld()->GetTimeSeconds());
}

AActor* AMonsterAIController::UpdateTargetFromSenses(APGMonsterCharacter* Monster, float Now)
{
	AActor* TargetActor = Target.Get();
	// 감각 목록을 직접 훑는다(이벤트만으로는 놓치는 경우가 있다 — FindPerceivedTarget 주석).
	//  - 표적이 없거나 무효가 됐으면(플레이어가 차에 타서 캐릭터가 숨겨짐 등) 바로 다음 표적으로 갈아탄다. 잠복으로 돌아갔다 다시 깨는 지연이 없어진다.
	//  - 표적이 계속 보이고 있으면 LastSensedTime 을 갱신한다. 안 그러면 15초 내내 보고 있어도 "감지 끊김"으로 포기했다.
	const bool bTargetInvalid = !IsValidTarget(TargetActor);
	if (bTargetInvalid || Now - LastPerceptionPollTime >= 0.5f)
	{
		LastPerceptionPollTime = Now;
		bool bStillPerceived = false;
		AActor* Nearest = FindPerceivedTarget(&bStillPerceived);
		// 바로 옆의 플레이어는 시야 각도 밖(등 뒤·옆)이어도 알아챈다.
		AActor* Close = FindCloseTarget();
		if (!IsValid(Nearest))
			Nearest = Close;
		if (bStillPerceived || (IsValid(Close) && Close == TargetActor))
			LastSensedTime = Now;
		// 표적이 없어졌거나, 지금 표적보다 확실히 나은 표적(다른 세력 몬스터를 쫓던 중에 보인 플레이어 등)이 있으면 갈아탄다.
		if (IsValid(Nearest) && Nearest != TargetActor && (bTargetInvalid || ShouldSwitchTarget(Nearest)))
		{
			UE_LOG(LogPGObjects, Display, TEXT("%s: target %s -> %s (polled)"), *GetNameSafe(GetPawn()), *GetNameSafe(TargetActor), *GetNameSafe(Nearest));
			Target = Nearest;
			TargetActor = Nearest;
			PGMonsterAIStats::CountTargetAcquired();
			LastSensedTime = Now;
		}
	}

	// 감각이 끊겨도 AggroKeepRadius 안이면 놓지 않는다(보스). 그 밖이면 LoseTargetSeconds 뒤 포기.
	const bool bSensedRecently = (Now - LastSensedTime) <= LoseTargetSeconds;
	const bool bKeepByRadius = IsValid(TargetActor) && Monster->AggroKeepRadius > 0.0f
		&& FVector::Dist(Monster->GetActorLocation(), TargetActor->GetActorLocation()) <= Monster->AggroKeepRadius;
	const bool bLost = !IsValidTarget(TargetActor) || (!bSensedRecently && !bKeepByRadius);
	if (bLost)
	{
		if (TargetActor)
		{
			UE_LOG(LogPGObjects, Display, TEXT("%s: lost target %s"), *GetNameSafe(GetPawn()), *GetNameSafe(TargetActor));
			PGMonsterAIStats::CountTargetLost();
			Target = nullptr;
			StopMovement();
			ClearFocus(EAIFocusPriority::Gameplay);
			Monster->Sleep(); // 잠복형이면 다시 변기·화분 자세로
		}
		return nullptr;
	}
	if (const APGMonsterCharacter* TargetMonster = Cast<APGMonsterCharacter>(TargetActor))
	{
		if (TargetMonster->IsDead())
		{
			Target = nullptr;
			StopMovement();
			return nullptr;
		}
	}
	return TargetActor;
}

void AMonsterAIController::WakeIfClose(APGMonsterCharacter* Monster, float Distance)
{
	// 잠복 중이면 가까이 올 때까지 기다렸다가 깨어난다. 깨어나는 모션 동안은 IsBusy 로 멈춘다.
	if (Distance <= Monster->GetWakeRange())
	{
		Monster->Wake();
		PGMonsterAIStats::CountWake();
	}
}

bool AMonsterAIController::TryAttackOrSmash(APGMonsterCharacter* Monster, AActor* TargetActor, float Distance)
{
	if (Distance > Monster->GetAttackRange())
		return false;
	if (Monster->HasClearLineTo(TargetActor))
	{
		StopMovement();
		SetFocus(TargetActor);
		if (Monster->TryAttack(TargetActor))
		{
			PGMonsterAIStats::CountAttack();
			PGSound::PlayAll(this, FName(TEXT("Monster_Attack")), Monster, Monster->GetActorLocation()); // 서버에서 도는 판단 — 모두에게
		}
		return true;
	}
	// 사거리 안인데 벽이 막고 있다: 큰 몸은 그 벽을 부순다(공격 모션). 작은 몸은 아래 이동으로 돌아서 다가간다.
	// 없을 때는 사거리 안이라는 이유로 벽 너머에서 헛손질만 하며 서 있었다.
	SetFocus(TargetActor);
	return Monster->SmashObstacleToward(TargetActor);
}

void AMonsterAIController::TickChase(APGMonsterCharacter* Monster, AActor* TargetActor, float Distance)
{
	const float Now = GetWorld()->GetTimeSeconds();
	// 막힘 감지: 경로는 있는데 1초에 60cm 도 못 갔으면 몸이 걸린 것. 2초간 목표를 향해 그냥 민다.
	if (Now - StuckCheckTime >= 1.0f)
	{
		const float Moved = FVector::Dist2D(Monster->GetActorLocation(), StuckCheckLocation);
		if (StuckCheckTime > 0.0f && Moved < 60.0f)
		{
			DirectChaseUntil = Now + 2.0f;
			UE_LOG(LogPGObjects, Display, TEXT("%s: stuck (moved %.0fcm), pushing straight"), *GetNameSafe(GetPawn()), Moved);
		}
		StuckCheckLocation = Monster->GetActorLocation();
		StuckCheckTime = Now;
	}

	// 거대 보스(8배)는 길찾기를 쓰지 않고 표적을 향해 곧장 걷는다. 길찾기(내비메시)는 건물을 "못 지나가는 벽"으로 보고
	// 돌아가는 길을 주는데, 보스는 가는 길의 건물을 전부 부술 수 있어서 빙 돌아가는 모습이 "헤매는 멍청이"로 보였다.
	// A* 의 "부수는 비용"을 따로 매기는 대신, 이 몸집에서는 직선 비용이 언제나 최소라고 보고 직선으로 간다.
	if (Monster->CanSmashThrough())
	{
		bDirectChase = true;
	}
	else if (Now - LastRepathTime >= RepathInterval)
	{
		LastRepathTime = Now;
		// 부분 경로는 받지 않는다. 표적까지 이어진 길이 없으면(지붕 위에 스폰됐는데 내려가는 내비가 없음 등) 예전에는
		// "갈 수 있는 데까지" 가서 지붕 끝에 서 있었다. 실패로 받아서 직진 추격으로 넘기면 난간 없는 지붕에서는 그냥 뛰어내린다.
		FAIMoveRequest Request(TargetActor);
		Request.SetAcceptanceRadius(Monster->GetAttackRange() * 0.6f);
		Request.SetAllowPartialPath(false);
		// 도착 판정에 두 몸의 반지름을 더하지 않는다. 더하면 5배 크리처(캡슐 반지름 4.5m)는 사거리보다 먼 곳에서 "도착"해 버렸다.
		// 사거리는 몸 중심끼리 3D 로 재서(키 차이 6m 넘게 포함) 그 자리에서는 공격도 이동도 안 하고 서 있다가,
		// 플레이어가 조금 움직이면 다시 걷고 또 서는 "다가오다 자꾸 멈춤"이 됐다.
		Request.SetReachTestIncludesAgentRadius(false);
		Request.SetReachTestIncludesGoalRadius(false);
		const FPathFollowingRequestResult Result = MoveTo(Request);
		PGMonsterAIStats::CountMoveRequest();
		// 길이 없거나(Failed), 길찾기는 "이미 도착"이라는데 아직 사거리 밖이면 표적 쪽으로 그냥 민다.
		bDirectChase = (Result.Code == EPathFollowingRequestResult::Failed)
			|| (Result.Code == EPathFollowingRequestResult::AlreadyAtGoal && Distance > Monster->GetAttackRange());
	}
	// 길이 없거나 몸이 걸렸는데 표적이 가까우면(공장 안에 숨은 플레이어 등) 큰 몸은 가로막은 벽을 부순다.
	// 5배 크리처는 길찾기로는 문으로 가는 길이 있어도 몸이 문보다 커서 문 앞에서 멈춰 있기만 했다.
	// 몸집 3배 미만(일반 몬스터)은 SmashObstacleToward 가 알아서 거절한다.
	// 거대 보스(CanSmashThrough)는 거리와 상관없이 몸이 막히면(1초에 60cm 도 못 감) 부순다 — 직선으로 오므로 부수는 것 말고는 길이 없다.
	if ((bDirectChase || Now < DirectChaseUntil)
		&& (Distance <= Monster->GetAttackRange() * 3.0f || (Monster->CanSmashThrough() && Now < DirectChaseUntil))
		&& Monster->SmashObstacleToward(TargetActor))
		return;
	if (bDirectChase || Now < DirectChaseUntil)
	{
		const FVector Direction = (TargetActor->GetActorLocation() - Monster->GetActorLocation()).GetSafeNormal2D();
		Monster->AddMovementInput(Direction);
	}
}
