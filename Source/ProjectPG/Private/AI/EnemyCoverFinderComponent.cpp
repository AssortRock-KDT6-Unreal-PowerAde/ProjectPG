// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "AI/EnemyCoverFinderComponent.h"
#include "AI/EnemyAILog.h"
#include "AI/EnemyAIConfig.h"
#include "Characters/CustomEnemyCharacter.h"
#include "EnvironmentQuery/EnvQuery.h"
#include "EnvironmentQuery/EnvQueryManager.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "Engine/World.h"


namespace
{
	// EQS_FindCover 의 격자 크기 칸이 이 이름의 값을 읽는다. 바꾸면 PGBuildEnemyAI 명령도 같이 바꿀 것.
	const FName CoverSearchRadiusParam(TEXT("CoverSearchRadius"));
}

UEnemyCoverFinderComponent::UEnemyCoverFinderComponent()
{
	// 찾는 동안에도 매 프레임 할 일 없음(EQS 끝 알림 + 시간 제한 타이머로 끝남).
	PrimaryComponentTick.bCanEverTick = false;
}

bool UEnemyCoverFinderComponent::IsCooldownOver() const
{
	return GetWorld()->GetTimeSeconds() >= NextSearchTime;
}

bool UEnemyCoverFinderComponent::StartSearch(ACustomEnemyCharacter* InEnemy, ACustomCharacter* InTarget, const UEnemyAIConfig& InConfig, TFunction<void(bool)> OnDone)
{
	Cancel();
	NextSearchTime = GetWorld()->GetTimeSeconds() + InConfig.CoverSearchCooldown;
	if (!InEnemy || !InTarget || !InConfig.CoverQuery) return false;
	Enemy = InEnemy;
	Target = InTarget;
	Config = &InConfig;

	FEnvQueryRequest Request(InConfig.CoverQuery, InEnemy);
	Request.SetFloatParam(CoverSearchRadiusParam, InEnemy->GetEnemyData().CoverSearchRadius);
	QueryId = Request.Execute(EEnvQueryRunMode::AllMatching, FQueryFinishedSignature::CreateUObject(this, &ThisClass::HandleQueryDone));
	if (QueryId == INDEX_NONE)
	{
		UE_LOG(LogEnemyAI, Error, TEXT("[EnemyAI] %s: 엄폐 EQS 를 시작 못 함 (%s)"), *GetNameSafe(InEnemy), *GetNameSafe(InConfig.CoverQuery.Get()));
		return false;
	}
	PendingDone = MoveTemp(OnDone);
	GetWorld()->GetTimerManager().SetTimer(QueryTimeoutTimer, this, &ThisClass::HandleQueryTimeout, InConfig.CoverQueryTimeout, false);
	return true;
}

void UEnemyCoverFinderComponent::Cancel()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(QueryTimeoutTimer);
		if (QueryId != INDEX_NONE)
			if (UEnvQueryManager* Manager = UEnvQueryManager::GetCurrent(World)) Manager->AbortQuery(QueryId);
	}
	QueryId = INDEX_NONE;
	PendingDone = nullptr;
}

void UEnemyCoverFinderComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	Cancel();
	Super::EndPlay(Reason);
}

void UEnemyCoverFinderComponent::Finish(bool bFound)
{
	TFunction<void(bool)> Done = MoveTemp(PendingDone);
	Cancel();
	if (Done) Done(bFound);
}

void UEnemyCoverFinderComponent::HandleQueryDone(TSharedPtr<FEnvQueryResult> Result)
{
	if (!Result.IsValid() || Result->QueryID != QueryId || !PendingDone) return;
	QueryId = INDEX_NONE;  // 이미 끝난 EQS 라 취소할 필요 없음
	if (!Enemy.IsValid() || !Target.IsValid()) return Finish(false);

	const int32 Count = Result->IsSuccessful() ? FMath::Min(Result->Items.Num(), Config->MaxCoverCandidates) : 0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector Candidate = Result->GetItemAsLocation(Index);
		FVector Peek;
		if (!IsHiddenBehindStatic(Candidate) || !FindPeekFrom(Candidate, Peek)) continue;
		CoverLocation = Candidate;
		PeekLocation = Peek;
		return Finish(true);
	}
	UE_LOG(LogEnemyAI, Verbose, TEXT("[EnemyAI] %s: 쓸 만한 엄폐 자리 없음 (후보 %d)"), *GetNameSafe(Enemy.Get()), Count);
	Finish(false);
}

void UEnemyCoverFinderComponent::HandleQueryTimeout()
{
	UE_LOG(LogEnemyAI, Error, TEXT("[EnemyAI] %s: 엄폐 EQS 가 %.1f초 안에 답이 없어 취소"), *GetNameSafe(Enemy.Get()), Config ? Config->CoverQueryTimeout : 0.f);
	Finish(false);
}

bool UEnemyCoverFinderComponent::IsHiddenBehindStatic(const FVector& Cover) const
{
	// 움직이는 물건(문·차·다른 사람) 뒤는 엄폐로 치지 않는다. 고정된 벽·바위만.
	FVector TargetEye; FRotator Unused;
	Target->GetActorEyesViewPoint(TargetEye, Unused);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemyCover), false, Enemy.Get());
	Params.AddIgnoredActor(Target.Get());
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, TargetEye, Cover + FVector(0, 0, 50), ECC_Visibility, Params)) return false;
	return Hit.GetComponent() && Hit.GetComponent()->Mobility == EComponentMobility::Static;
}

bool UEnemyCoverFinderComponent::FindPeekFrom(const FVector& Cover, FVector& OutPeek) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav) return false;
	// 목표를 바라보는 방향의 왼쪽·오른쪽으로 한 걸음 나간 자리.
	const FVector Side = FVector::CrossProduct((Target->GetActorLocation() - Cover).GetSafeNormal2D(), FVector::UpVector);
	for (const float Sign : { -1.f, 1.f })
	{
		FNavLocation Peek;
		if (!Nav->ProjectPointToNavigation(Cover + Side * Sign * Config->PeekSideDistance, Peek, FVector(60, 60, 150))) continue;
		if (!CanSeeTargetFrom(Peek.Location + FVector(0, 0, Enemy->BaseEyeHeight))) continue;
		// 엄폐 자리에서 옆자리까지 바로 걸어갈 수 있는지(길 찾기 대신 네비 위 직선 검사 한 번, 가벼움).
		FVector HitLocation;
		if (UNavigationSystemV1::NavigationRaycast(GetWorld(), Cover, Peek.Location, HitLocation, nullptr, Enemy->GetController())) continue;
		OutPeek = Peek.Location;
		return true;
	}
	return false;
}

bool UEnemyCoverFinderComponent::CanSeeTargetFrom(const FVector& Eye) const
{
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EnemySight), false, Enemy.Get());
	TArray<AActor*> Attached;
	Enemy->GetAttachedActors(Attached, true, true);  // 내 총에 막히지 않게
	Params.AddIgnoredActors(Attached);
	FHitResult Hit;
	const bool bBlocked = GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target->GetActorLocation(), ECC_Visibility, Params);
	return !bBlocked || Hit.GetActor() == Target.Get();
}
