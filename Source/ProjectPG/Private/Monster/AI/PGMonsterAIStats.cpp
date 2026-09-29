#include "Monster/AI/PGMonsterAIStats.h"

#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Monster/MonsterAIController.h"
#include "Monster/PGMonsterCharacter.h"
#include "Objects/PGObjectTypes.h"

namespace PGMonsterAIStats
{
	// 게임 스레드에서만 부른다(컨트롤러·StateTree Tick) — 잠금이 필요 없다.
	static uint64 GThinkCycles = 0;
	static uint64 GMaxThinkCycles = 0; // 가장 오래 걸린 한 번 — 평균만 보면 가끔 튀는 순간(첫 길찾기 등)이 숨는다
	static int64 GThinks = 0;
	static int64 GMoveRequests = 0;
	static int64 GAttacks = 0;
	static int64 GAcquired = 0;
	static int64 GLost = 0;
	static int64 GWakes = 0;
	static int64 GReturns = 0;
	static int64 GTreeTicks = 0;
	static double GWindowStart = -1.0; // 게임 시간(초). 처음 재기 시작한 때
	static const TCHAR* GModeName = TEXT("Classic");

	FThinkScope::FThinkScope(bool bInCountThink)
		: StartCycles(FPlatformTime::Cycles64())
		, bCountThink(bInCountThink)
	{
	}

	FThinkScope::~FThinkScope()
	{
		const uint64 Spent = FPlatformTime::Cycles64() - StartCycles;
		GThinkCycles += Spent;
		GMaxThinkCycles = FMath::Max(GMaxThinkCycles, Spent);
		if (bCountThink)
			++GThinks;
	}

	void CountMoveRequest() { ++GMoveRequests; }
	void CountAttack() { ++GAttacks; }
	void CountTargetAcquired() { ++GAcquired; }
	void CountTargetLost() { ++GLost; }
	void CountWake() { ++GWakes; }
	void CountReturnHome() { ++GReturns; }
	void CountTreeTick() { ++GTreeTicks; }
	void SetModeName(const TCHAR* Name) { GModeName = Name; }

	static void Reset(UWorld* World)
	{
		GThinkCycles = 0;
		GMaxThinkCycles = 0;
		GThinks = GMoveRequests = GAttacks = GAcquired = GLost = GWakes = GReturns = GTreeTicks = 0;
		GWindowStart = World ? World->GetTimeSeconds() : 0.0;
		UE_LOG(LogPGObjects, Display, TEXT("PGMonsterAIStats: reset (mode=%s)"), GModeName);
	}

	static void Print(UWorld* World)
	{
		if (!World)
			return;
		if (GWindowStart < 0.0)
			GWindowStart = 0.0; // reset 없이 부르면 게임 시작부터
		const double Window = FMath::Max(0.001, World->GetTimeSeconds() - GWindowStart);
		// 몬스터 수: 살아 있는 것 / 그중 표적을 쫓는 것(=전투 중). 한가한 몬스터와 싸우는 몬스터는 비용이 크게 다르다.
		int32 Alive = 0;
		int32 Engaged = 0;
		for (TActorIterator<APGMonsterCharacter> It(World); It; ++It)
		{
			if (!IsValid(*It) || It->IsDead())
				continue;
			++Alive;
			if (const AMonsterAIController* Controller = Cast<AMonsterAIController>(It->GetController()); Controller && Controller->GetTarget())
				++Engaged;
		}
		const double ThinkMs = FPlatformTime::ToMilliseconds64(GThinkCycles);
		const double UsPerThink = GThinks > 0 ? ThinkMs * 1000.0 / GThinks : 0.0;
		const double MsPerSecond = ThinkMs / Window; // 1초(게임 시간)마다 AI 가 쓴 CPU — 60fps 한 프레임 예산은 16.7ms
		const double UsPerMonsterSecond = Alive > 0 ? MsPerSecond * 1000.0 / Alive : 0.0;
		UE_LOG(LogPGObjects, Display,
			TEXT("PGMonsterAIStats: mode=%s window=%.1fs monsters=%d engaged=%d | thinks=%lld (%.0f/s) cpu=%.2fms total, %.3fms/s, %.2fus/think (max %.1fus), %.1fus per monster-second")
			TEXT(" | tree_ticks=%lld (%.0f/s) | moveto=%lld (%.1f/s) attacks=%lld acquired=%lld lost=%lld wakes=%lld returns=%lld"),
			GModeName, Window, Alive, Engaged, GThinks, GThinks / Window, ThinkMs, MsPerSecond, UsPerThink, FPlatformTime::ToMilliseconds64(GMaxThinkCycles) * 1000.0, UsPerMonsterSecond,
			GTreeTicks, GTreeTicks / Window, GMoveRequests, GMoveRequests / Window, GAttacks, GAcquired, GLost, GWakes, GReturns);
	}

	static void StatsCommand(const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() > 0 && Args[0].Equals(TEXT("reset"), ESearchCase::IgnoreCase))
			Reset(World);
		else
			Print(World);
	}

	static FAutoConsoleCommandWithWorldAndArgs StatsCommandRegistration(
		TEXT("PG.MonsterAI.Stats"),
		TEXT("Monster AI cost counters. 'PG.MonsterAI.Stats reset' starts a new window, no arg prints one line (PGMonsterAIStats:)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&StatsCommand));
}
