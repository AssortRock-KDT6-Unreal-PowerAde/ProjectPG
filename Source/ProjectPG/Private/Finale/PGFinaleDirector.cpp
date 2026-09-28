#include "Finale/PGFinaleDirector.h"
#include "Monster/PGMonsterLookSet.h"
#include "Common/PGPlayerMessageComponent.h"
#include "Combat/PGCombatSettings.h"
#include "Common/PGSoundRouter.h"

#include "Common/PGVisualSettings.h"
#include "LevelDesign/PGMapInfo.h"
#include "Engine/World.h"
#include "Common/PGPhysicsUtil.h"
#include "Engine/OverlapResult.h"
#include "Finale/PGBattleshipActor.h"
#include "Finale/PGDragonBoss.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectTypes.h"

APGFinaleDirector::APGFinaleDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	bAlwaysRelevant = true; // 맵 어디에 있든 모든 클라이언트가 진행 상황을 받아야 한다
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("RootScene")));
}

void APGFinaleDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGFinaleDirector, State);
	DOREPLIFETIME(APGFinaleDirector, Ship);
	DOREPLIFETIME(APGFinaleDirector, FocusLocation);
	DOREPLIFETIME(APGFinaleDirector, Dragon);
}

APGFinaleDirector* APGFinaleDirector::Get(const UWorld* World, bool bCreate)
{
	if (!IsValid(World))
		return nullptr;
	if (AActor* Found = UGameplayStatics::GetActorOfClass(World, APGFinaleDirector::StaticClass()))
		return Cast<APGFinaleDirector>(Found);
	if (!bCreate)
		return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	return const_cast<UWorld*>(World)->SpawnActor<APGFinaleDirector>(APGFinaleDirector::StaticClass(), FTransform::Identity, Params);
}

void APGFinaleDirector::NotifyBossDefeated(AActor* Boss)
{
	if (!IsValid(Boss) || !Boss->HasAuthority())
		return;
	const UWorld* World = Boss->GetWorld();
	if (!IsValid(World) || World->IsNetMode(NM_Client))
		return;
	const UPGCombatSettings* Settings = GetDefault<UPGCombatSettings>();
	if (Settings && !Settings->bEnableFinale)
		return;
	APGFinaleDirector* Director = Get(World, true);
	if (!Director || Director->State != EPGFinaleState::Idle)
		return; // 보스가 여러 번 죽어도(리스폰) 피날레는 한 번만
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: boss %s defeated, finale starts"), *Boss->GetName());
	Director->StartFinale(Boss->GetActorLocation());
}

void APGFinaleDirector::Prewarm(const UWorld* World, const FVector& BossLocation)
{
	if (!IsValid(World) || World->IsNetMode(NM_Client))
		return;
	const UPGCombatSettings* Settings = GetDefault<UPGCombatSettings>();
	if (Settings && !Settings->bEnableFinale)
		return;
	if (APGFinaleDirector* Director = Get(World, true))
		Director->PrepareShip(BossLocation);
}

void APGFinaleDirector::StartFinale(const FVector& Focus)
{
	if (State != EPGFinaleState::Idle)
		return;
	FocusLocation = Focus;
	EnterState(EPGFinaleState::Warning);
}

// ---- 상태 표 (2026-09-26 OCP — 상태 패턴) ----
// 전에는 "들어갈 때 할 일" 이 EnterState 의 switch 에, "매 틱 할 일" 이 Tick 의 if 사슬에 흩어져 있어서
// 상태 하나를 늘리면 두 곳을 찾아 고쳐야 했다. 이제 상태마다 Enter*/Tick* 함수 한 쌍이고, 연결은 이 표 한 곳이다.
// 함수 본문은 옮기기만 했다(내용 그대로).
const APGFinaleDirector::FStateHandlers* APGFinaleDirector::FindStateHandlers(EPGFinaleState InState)
{
	static const TMap<EPGFinaleState, FStateHandlers> Table = {
		{ EPGFinaleState::Warning, { &APGFinaleDirector::EnterWarningState, &APGFinaleDirector::TickWarningState } },
		{ EPGFinaleState::Arrival, { &APGFinaleDirector::EnterArrivalState, &APGFinaleDirector::TickArrivalState } },
		{ EPGFinaleState::Hover, { &APGFinaleDirector::EnterHoverState, &APGFinaleDirector::TickHoverState } },
		{ EPGFinaleState::Launch, { &APGFinaleDirector::EnterLaunchState, &APGFinaleDirector::TickLaunchState } },
		{ EPGFinaleState::Dragon, { nullptr, &APGFinaleDirector::TickDragonState } },
		{ EPGFinaleState::Victory, { &APGFinaleDirector::EnterVictoryState, nullptr } },
	};
	return Table.Find(InState);
}

void APGFinaleDirector::EnterState(EPGFinaleState NewState)
{
	State = NewState;
	StateTimer = 0.0f;
	// 단계마다 소리 신호(Finale_Warning, Finale_Arrival … — 경보·음악 전환 자리). 서버 → 모두.
	PGSound::PlayAll(this, FName(*(TEXT("Finale_") + StaticEnum<EPGFinaleState>()->GetNameStringByValue(static_cast<int64>(NewState)))), nullptr, GetActorLocation());
	OnRep_State();
	if (const FStateHandlers* Handlers = FindStateHandlers(NewState); Handlers && Handlers->Enter)
		(this->*Handlers->Enter)();
}

void APGFinaleDirector::EnterWarningState()
{
	// 보스가 쓰러지면 화면에 두 줄을 차례로 띄운다(9/22 사용자 문구 그대로). 순서·시간은 코드(PGAnnounceSubsystem)가 맡는다.
	// 멀티(9/27): 접속한 모두의 화면에(서버 화면에 띄우면 전용 서버에는 화면이 없어 아무도 못 봤다).
	UPGPlayerMessageComponent::AnnounceToAll(this, {
		NSLOCTEXT("Finale", "ShipIncoming", "전함이 곧 도착합니다."),
		NSLOCTEXT("Finale", "PrepareForBeast", "다가올 괴수를 맞이할 준비하세요.") });
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: Warning (%.0fs) — ship approaching"), WarningSeconds);
}

void APGFinaleDirector::EnterArrivalState()
{
	SpawnShip();
}

void APGFinaleDirector::EnterHoverState()
{
	// 정박하면 승강 발판을 땅까지 내린다. 날으는 차가 없어도 걸어서 탈 수 있어야 한다.
	if (IsValid(Ship))
		Ship->DeployElevator(GroundZ);
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: Hover — ship parked, deck %.0fm up, elevator coming down"),
		IsValid(Ship) ? (Ship->GetDeckWorldZ() - GroundZ) * 0.01f : 0.0f);
	// 시험용(-PGFinaleAutoLaunch): 정박하자마자 이륙시켜 드래곤 단계까지 화면 없이 확인한다(9/23 블루프린트 분리 검증).
	//   콘솔 PG.Finale.Launch 를 시작 명령과 같이 보내면 아직 정박 전이라 무시되기 때문이다.
	if (FParse::Param(FCommandLine::Get(), TEXT("PGFinaleAutoLaunch")))
	{
		// 조종석에 앉을 사람이 없으니 드래곤 보험(기본 꺼짐)을 5초로 켠다.
		DragonFallbackSeconds = FMath::Max(DragonFallbackSeconds, 5.0f);
		FTimerHandle AutoLaunch;
		GetWorldTimerManager().SetTimer(AutoLaunch, this, &APGFinaleDirector::ForceLaunch, 1.0f, false);
	}
}

void APGFinaleDirector::EnterLaunchState()
{
	// 이륙: 정박 높이보다 훨씬 위로. 올라가는 길에 배 밑 타일·소품이 날아간다.
	if (IsValid(Ship))
	{
		Ship->RetractElevator(); // 매달고 올라가면 갑판에서 "뭔가 떨어져 나가는" 것으로 보인다

		// 이륙하면서 선미 문을 닫는다 — 여기서 명시적으로 부르는 이유(9/21):
		//
		// 원래는 "떠 있고 안에 사람이 2초" 라는 자동 판정 하나뿐이었는데, 탑승 판정(IsAnyoneAboard)이
		// 훨씬 넓어서 문턱 부근에 서기만 해도 이륙이 먼저 시작됐다. 그러면 이륙 중이라는 이유로
		// 닫힘 카운트가 매 프레임 0 으로 리셋돼 문이 영영 안 닫혔다. 닫히는 판과 안 닫히는 판이
		// 실행마다 갈려서, 로그를 안 보면 "고쳤다" 고 착각하기 제일 쉬운 종류였다.
		//
		// 사용자 요구도 "마지막에 닫히면서 함선이 밀폐" 다. 닫을지 말지를 배가 스스로 판단하게 두지
		// 말고, 연출이 "지금 닫아" 라고 시키는 것이 의도에 맞다.
		//
		// 이미 닫혀 있으면 아무 일도 안 하므로 자동 닫힘과 겹쳐도 안전하다. 애니메이션은 시작만
		// 하고 보간은 Cruise 로 바뀐 뒤에도 계속 돈다(Motion 가드는 여는 쪽에만 있다).
		Ship->CloseHangar();
		// 곧장 위로만 오르지 않고 **뱃머리 반대쪽(= 뒤쪽) 끝으로 물러나면서** 오른다.
		//
		// 왜: 드래곤은 뱃머리 정면 끝에서 솟는다. 배가 맵 한가운데에 떠 있으면 둘 사이가 300m 남짓인데
		//   필요한 간격은 494m 다 — 드래곤이 태어나는 순간부터 배와 겹친다(9/20 PIE: gap 342 / need 605).
		//   맵(600m)이 좁아서 생기는 문제라, 둘을 같은 축의 양 끝에 세워 간격을 두 배로 번다.
		// 0.9 인 이유: 그 밖은 산줄기다. 산 위에서는 땅 높이를 제대로 못 잰다(9/20 사고).
		FVector Centre = FVector::ZeroVector;
		float MapRadiusCm = 0.0f;
		const FVector2D Axis = ResolveFinaleAxis(Centre, MapRadiusCm);
		// 9/21 사용자: "굳이 떠오르게 하지 마 ... 조종석에 앉아야 움직이게, 너무 높이 오르지 않게".
		//   자동 상승(LaunchClimb, 300m)과 끝으로 물러나기는 끈다. 배는 정박 높이에 그대로 있고, 움직임은 조종석의 W/S/A/D 뿐이다.
		//   LaunchClimb 를 0 보다 크게 주면 예전처럼 오른다.
		if (LaunchClimb > 0.0f)
		{
			const FVector Up(
				Centre.X + Axis.X * MapRadiusCm * 0.9f,
				Centre.Y + Axis.Y * MapRadiusCm * 0.9f,
				Ship->GetActorLocation().Z + LaunchClimb);
			Ship->CruiseTo(Up);
		}
	}
	// 드래곤은 여기서 나온다(Tick 이 DragonDelaySeconds 를 세고 부른다. 기본 0 = 이륙하는 그 프레임).
	//
	// 타이밍을 두 번 옮겼다. 정리해 둔다:
	//   ① 처음엔 여기(이륙 시작)였다.
	//   ② "시야가 조종을 시작하면 텀을 두고" 라는 말을 듣고 배가 다 올라간 뒤로 미뤘다. 그런데
	//      그 조건으로 쓴 HasArrived() 는 정박(Hover)할 때 이미 참이라, 승강기를 타고 올라오는
	//      중에 나와 버렸다.
	//   ③ 사용자 재확인(9/20): "조종석 다룰 때가 아니라 그냥 전함에 엘베 올라가자마자 등장해."
	//      그래서 여기로 되돌렸다. Launch 는 갑판에 BoardedSeconds(3초) 서 있어야 시작하므로
	//      "엘베로 올라온 직후" 가 맞고, 올라오는 **중에는** 절대 안 나온다.
	// 여기서 나와야 하는 또 다른 이유: 배는 이 순간부터 반대쪽 끝으로 물러난다. 더 미루면
	//   드래곤이 솟는 동안 배가 아직 가까이 있어 날개가 선체에 겹친다.
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: Launch — climbing, tearing up the ground below"));
}

void APGFinaleDirector::EnterVictoryState()
{
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: Victory — the dragon is down"));
}

bool APGFinaleDirector::IsAnyoneAboard() const
{
	if (!IsValid(Ship))
		return false;
	// 갑판 높이 근처에, 배 길이 안에 사람이 있으면 탄 것으로 본다.
	//
	// **높이는 월드에서 재고, 앞뒤·좌우만 배 로컬 상자로 본다.** 단위를 섞지 않으려는 것이다.
	//   전에는 높이도 로컬(`Local.Z`)로 재서 월드 기준 값(`GetDeckWorldZ()` 와의 차)과 비교했다.
	//   배는 배율이 10 이라 두 값의 단위가 다르고, "갑판 −4m ~ +40m" 로 적은 창이 실제로는 훨씬 넓게
	//   열렸다. 그 바람에 **승강 발판에 매달려 갑판보다 20m 아래에 있는 사람이 "탔다" 로 잡혔고**,
	//   그대로 이륙해 버렸다(9/20 PIE: `player below deck by 20.0 m, aboard=1, ship 281 m up` —
	//   사용자: "조종석에 앉기도 전에 엘베 타고 올라오기만 하면 드래곤이 바로 나타난다").
	// 아래쪽 창을 1m 로 좁힌 이유: 발판을 타고 올라오는 중에는 갑판보다 확실히 아래에 있다.
	//   발판이 갑판 높이에 닿은 뒤부터가 "거의 내려선 것" 이고, 거기서부터 BoardedSeconds 를 센다.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		const APawn* Rider = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Rider))
			continue;
		const FBox& Bounds = Ship->GetHullLocalBounds();
		if (!Bounds.IsValid)
			continue;
		// 앞뒤·좌우: 배가 돌아 있어도 맞아야 하므로 로컬 상자로 본다.
		// 선체 전체 상자로 보면 배 옆 허공을 날아도 이륙해 버린다(9/20 조사).
		const FVector Local = Ship->GetActorTransform().InverseTransformPosition(Rider->GetActorLocation());
		// 뒤쪽 한계는 선체 끝이 아니라 **문 안쪽 15m** 다(9/21).
		//   선체 끝부터 세면 문 뒤에 깔린 문턱판 위에 서기만 해도 3초 뒤 이륙하고, 이륙하면서 문턱판이
		//   그 사람 발밑에서 배 안으로 밀려 들어간다. 배의 "안에 탔나" 판정(HangarInsideLocal)도 문 안쪽
		//   15m 부터 세므로 둘을 맞춘다 — 두 판정의 범위가 어긋난 것이 오늘 문 닫힘 사고의 근본이었다.
		const float EntranceLocalX = Ship->GetActorTransform().InverseTransformPosition(Ship->GetHangarEntranceWorld()).X;
		const float RearLimitX = FMath::Max(Bounds.Min.X, EntranceLocalX + 1500.0f);
		if (Local.X <= RearLimitX || Local.X >= Bounds.Max.X
			|| FMath::Abs(Local.Y) >= Bounds.GetSize().Y * 0.28f)
			continue;
		// 높이: 월드에서만 잰다. 갑판보다 1m 아래 ~ 40m 위.
		const float DeckWorldZ = Ship->GetDeckWorldZ();
		const float RiderZ = Rider->GetActorLocation().Z;
		if (RiderZ > DeckWorldZ - 100.0f && RiderZ < DeckWorldZ + 4000.0f)
			return true;
	}
	return false;
}

void APGFinaleDirector::ForceLaunch()
{
	if (State == EPGFinaleState::Hover || State == EPGFinaleState::Arrival)
		EnterState(EPGFinaleState::Launch);
}

void APGFinaleDirector::KnockGroundUnderShip()
{
	if (!IsValid(Ship))
		return;
	// 배 밑을 훑어 소품·타일 조각을 날린다. 한 번에 몇 개씩만 — 410m 짜리 밑을 한꺼번에 털면 프레임이 죽는다.
	const FVector Under(Ship->GetActorLocation().X, Ship->GetActorLocation().Y, GroundZ + 200.0f);
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFinaleLaunchKnock), false, Ship);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Under, FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(9000.0f), Params);
	int32 Budget = 4;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Budget <= 0)
			break;
		UPrimitiveComponent* Component = Overlap.GetComponent();
		AActor* PropOwner = Component->GetOwner();
		// 배 본체뿐 아니라 "배에 붙어 있는 것"(껍데기 부품 31개)도 빼야 한다.
		if (!IsValid(Component) || !IsValid(PropOwner) || PropOwner == Ship || PropOwner->IsAttachedTo(Ship) || PropOwner->GetOwner() == Ship)
			continue;
		FHitResult Hit;
		// 인스턴스 번호를 꼭 채워야 한다. 안 채우면 0 번으로 읽혀서, 맵 어디에 있든 그 묶음의 0번 인스턴스가
		// 뜯겨 날아간다(9/20 조사: 이륙할 때 맵 곳곳의 돌·풀이 무작위로 튀어 오르던 원인).
		Hit.Item = Overlap.ItemIndex;
		Hit.ImpactPoint = Component->GetComponentLocation();
		Hit.Location = Hit.ImpactPoint;
		Hit.Component = Component;
		Hit.HitObjectHandle = FActorInstanceHandle(Component->GetOwner());
		const FVector Away = (Hit.ImpactPoint - Under).GetSafeNormal2D() * 900.0f + FVector(0.0f, 0.0f, 1200.0f);
		if (PGPhysicsUtil::TryKnockProp(Component, Hit, Away, Ship, 900.0f, 200.0f))
			--Budget;
	}
}

void APGFinaleDirector::SpawnDragon()
{
	if (IsValid(Dragon) || !IsValid(Ship))
		return;
	// 언제·어떤 상황에서 불렸는지 한 줄로 남긴다.
	//
	// 왜: "드래곤이 너무 일찍 나온다" 를 두 번 고쳤는데 두 번 다 빗나갔다. 짐작으로 시점을 옮기는 대신,
	//   불린 순간의 상태·경과·배 높이·사람 위치를 찍어 두면 다음 판에서 어디가 어긋났는지 바로 갈린다.
	// 사람 위치는 갑판 기준 상대 높이로 찍는다 — 승강기를 타고 올라오는 중(갑판보다 아래)인지,
	//   갑판에 내려선 뒤인지가 이 숫자 하나로 구분된다.
	{
		float DeckRelZM = 0.0f;
		bool bFoundPawn = false;
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			const APawn* Rider = IsValid(PC) ? PC->GetPawn() : nullptr;
			if (!IsValid(Rider))
				continue;
			DeckRelZM = (Rider->GetActorLocation().Z - Ship->GetDeckWorldZ()) * 0.01f;
			bFoundPawn = true;
			break;
		}
		UE_LOG(LogPGObjects, Display,
			TEXT("PGFinale: SpawnDragon — state %d, %.1fs into it, delay %.1fs, ship %.0f m up, ")
			TEXT("player %s deck by %.1f m, aboard=%d"),
			static_cast<int32>(State), StateTimer, DragonDelaySeconds,
			(Ship->GetActorLocation().Z - GroundZ) * 0.01f,
			bFoundPawn ? (DeckRelZM >= 0.0f ? TEXT("above") : TEXT("below")) : TEXT("(none)"),
			FMath::Abs(DeckRelZM), IsAnyoneAboard() ? 1 : 0);
	}
	// 배의 뱃머리 정면 끝에서 땅을 뚫고 올라와 배를 향해 날아온다(조종석 화면에 잡히게 — ResolveFinaleAxis 참고).
	//
	// 기획서 13절은 "맵 외곽 산에서" 였지만 산 위는 못 쓴다 — 산에는 부술 타일이 없어 "지면을 부수며 등장"이
	// 안 보이고, 땅 높이를 재는 선 검사가 산 꼭대기를 찍어 사고가 난다. 타일이 깔린 가장 바깥으로 바꿨다.
	//
	// 축은 배를 세울 때와 같은 것을 쓴다(ResolveFinaleAxis). 배는 +축(뒤쪽) 끝, 드래곤은 -축(뱃머리 정면) 끝 —
	//   이렇게 해야 좁은 맵(600m)에서 낼 수 있는 최대 간격(맵 반지름 × 1.8)이 나오고, 등 뒤에서 솟지 않는다.
	// 0.6배(270m)로는 배까지 414m 밖에 안 떨어져 태어나는 순간부터 경계가 겹쳤고(9/20 조사),
	//   0.85 로 올린 뒤에는 필요 간격을 못 채운 루프가 산줄기 위까지 밀어 버려 땅 높이를 산 꼭대기로
	//   재는 사고가 났다(9/20 PIE: 붕괴가 294m 공중에서 터져 맵이 통째로 사라졌다).
	//   그래서 "모자라면 민다"를 버리고 **처음부터 낼 수 있는 최대치(0.9)** 로 세운다. 산 고리에는 안 닿는다.
	FVector Centre = FVector::ZeroVector;
	float MapRadius = 0.0f;
	const FVector2D Outward = ResolveFinaleAxis(Centre, MapRadius);
	const FVector Ship2D(Ship->GetActorLocation().X, Ship->GetActorLocation().Y, 0.0f);
	FVector Spot(Centre.X - Outward.X * MapRadius * 0.9f, Centre.Y - Outward.Y * MapRadius * 0.9f, GroundZ);
	// 드래곤을 먼저 만든다. 태어날 자리와 무너뜨릴 넓이를 정하려면 **몸 크기를 알아야** 하는데,
	// 그 크기는 드래곤이 BeginPlay 에서 에셋을 재야 나온다. 숫자를 여기 박아 두면(전에는 1029×40 이
	// 박혀 있었다 — 지금 쓰지도 않는 SoulEater 의 치수다) 모델을 바꾸는 순간 전부 거짓말이 된다.
	// 이 시점의 위치는 잠깐이다 — 바로 아래 BeginRise 가 땅속으로 옮긴다.
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 설정(ProjectPG Visuals > Dragon Boss Class)에 블루프린트가 있으면 그것으로(9/23 블루프린트 분리).
	// 맵 가장자리를 따라 옆으로 서서 솟는다(9/23). 이 모델은 피벗이 발밑이고 몸 한가운데가 꼬리 쪽으로 100m 쯤 떨어져 있다.
	//   예전(요 0 고정)에는 맵 서쪽 끝에서 솟으면 꼬리 쪽 몸이 맵 밖(산 위)에 걸려 구덩이와 몸이 어긋났다.
	//   가장자리와 나란히 서면 몸이 맵 안쪽 가장자리를 따라 놓이고, 배 조종석에서는 옆모습이 통째로 보인다.
	const float RiseYaw = FMath::RadiansToDegrees(FMath::Atan2(Outward.Y, Outward.X)) + 90.0f;
	Dragon = GetWorld()->SpawnActor<APGDragonBoss>(UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().DragonBossClass, APGDragonBoss::StaticClass()),
		FTransform(FRotator(0.0f, RiseYaw, 0.0f), Spot), SpawnParams);
	if (!IsValid(Dragon))
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGFinale: dragon spawn failed"));
		return;
	}

	// 필요한 간격. 이 자리가 낼 수 있는 최대치인데도 모자라면 로그로 남긴다 —
	// 그때는 배율이나 맵 크기를 건드려야 하고, 코드가 몰래 때울 수 있는 문제가 아니다.
	const float NeedCm = Dragon->KeepDistanceCm(Ship);

	// 솟는 자리의 땅 높이를 그 자리에서 다시 잰다. 전에는 배 밑에서 잰 GroundZ 를 그대로 써서,
	// 지형이 다르면 묻힌 깊이가 어긋났다.
	FVector DragonGround = Spot;
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDragonGround), false, this);
		Params.AddIgnoredActor(Dragon); // 이제 드래곤이 먼저 태어나 있다. 제 몸을 땅으로 착각하면 안 된다
		const FVector From(Spot.X, Spot.Y, GroundZ + 30000.0f);
		if (GetWorld()->LineTraceSingleByChannel(Hit, From, From - FVector(0.0f, 0.0f, 100000.0f), ECC_Visibility, Params))
		{
			// 잰 높이가 터무니없으면 안 쓴다.
			//
			// 이 값은 붕괴의 중심 높이가 되고, "무너지는 땅에서 얼마나 위인가" 를 재는 기준이 된다.
			// 한 번 틀리면 맵 전체가 지워진다 — 실제로 산 꼭대기를 찍어 294m 공중에서 터뜨린 적이 있다.
			// 지면 기준보다 100m 넘게 높으면 땅이 아니라 산·구조물을 찍은 것으로 보고 기준 높이를 쓴다.
			if (FMath::Abs(Hit.ImpactPoint.Z - GroundZ) < 10000.0f)
			{
				DragonGround.Z = Hit.ImpactPoint.Z;
			}
			else
			{
				UE_LOG(LogPGObjects, Warning,
					TEXT("PGFinale: ground trace at the rise spot hit %s at %.0f m (ground is %.0f m) — using ground"),
					*GetNameSafe(Hit.GetActor()), Hit.ImpactPoint.Z * 0.01f, GroundZ * 0.01f);
				DragonGround.Z = GroundZ;
			}
		}
	}
	// 솟기 직전에 그 자리 타일을 무너뜨린다.
	//
	// 왜 여기서 하나: 드래곤의 넉백(TryKnockProp)으로는 지면 타일이 원리상 안 부서진다 — 지면 HISM 에는
	//   PGTerrain 태그가 붙어 있어 거부되고, 20m 타일은 넉백의 크기 한도(12m)도 넘는다. 그래서 "지면을 부수며
	//   등장"이 한 번도 보이지 않았다(9/20 조사). 전용 붕괴 경로로 바꾼다.
	// 덤으로 메모리가 준다(사용자 9/20 제안): 꺼진 타일·소품·시설을 아예 지우므로 공중전 동안 지상 부하가 빠진다.
	//   어차피 이 시점부터 아무도 지상에 없다.
	if (IPGMapInfo* Preview = UPGMapInfoSubsystem::FindMap(this))
	{
		// 몸이 빠져나올 만큼은 비우되, 맵보다 크게는 못 뚫는다.
		//
		// 드래곤이 474 × 392m 라 뻗은 거리 × 1.2 = 354m 가 나왔는데, 맵은 600 × 600m 라 **지름 708m**,
		//   맵보다 큰 구덩이였다. 한 방에 맵이 통째로 사라졌다(9/20 PIE, 사용자: "어우 맵들 다 부숴버리네").
		// 구덩이가 몸보다 작아도 된다 — 뚫고 나오는 그림이라 오히려 자연스럽다. 맵 반지름의 35% 로 묶는다.
		// 비율은 BP_PGDragonBoss 의 Collapse Map Radius Fraction(기본 0.35)에서 읽는다(9/23 블루프린트 분리).
		const float WantCm = Dragon->GetHalfWingCm() * 1.2f;
		const float CollapseCm = FMath::Min(WantCm, MapRadius * Dragon->CollapseMapRadiusFraction);
		if (CollapseCm < WantCm)
		{
			UE_LOG(LogPGObjects, Display, TEXT("PGFinale: collapse radius capped %.0f m -> %.0f m (map radius %.0f m x %.2f)"),
				WantCm * 0.01f, CollapseCm * 0.01f, MapRadius * 0.01f, Dragon->CollapseMapRadiusFraction);
		}
		// 구덩이 중심 = 솟아오를 때 보이는 몸 한가운데. 이 모델은 피벗이 발밑이라 몸 한가운데가 원점에서 수십 m 떨어져 있어,
		//   원점에 뚫으면 구덩이가 몸 한쪽으로 치우쳐 보였다(9/23 사용자: "부서지는 지면과 등장 위치가 안 맞는다").
		//   드래곤은 원점(DragonGround)에서 그대로 솟는다 — 배와의 간격 계산은 원점 기준이라 건드리지 않는다.
		const FVector BodyOffset = Dragon->GetRiseBodyCentreOffset();
		FVector CollapseCentre = DragonGround + BodyOffset;
		// 그래도 구덩이 절반 이상은 맵 안(타일 위)에 오게 붙잡는다 — 밖으로 나가면 부술 타일이 없어 구덩이가 안 생긴다
		//   (9/23 시험: 몸 쪽으로 102m 옮기자 맵 밖이라 "pit 0 planes").
		const FVector2D FromCentre(CollapseCentre.X - Centre.X, CollapseCentre.Y - Centre.Y);
		const float Allowed = FMath::Max(MapRadius - CollapseCm * 0.5f, 0.0f);
		if (FromCentre.Size() > Allowed)
		{
			const FVector2D Clamped = FromCentre.GetSafeNormal() * Allowed;
			CollapseCentre.X = Centre.X + Clamped.X;
			CollapseCentre.Y = Centre.Y + Clamped.Y;
		}
		UE_LOG(LogPGObjects, Display, TEXT("PGFinale: collapse centred on the body — body %.0f m from the dragon origin, pit %.0f m from it (rise yaw %.0f)"),
			BodyOffset.Size2D() * 0.01f, FVector::Dist2D(CollapseCentre, DragonGround) * 0.01f, RiseYaw);
		Preview->CollapseRegion(CollapseCentre, CollapseCm, 3.0f);
	}
	Dragon->BeginRise(DragonGround, Ship);
	// 간격이 맞는지 PIE 에서 바로 볼 수 있게 찍는다. gap 이 need 보다 작으면 맵이 좁아 더 밀 자리가 없었던 것이다.
	// 붕괴에 넘긴 자리를 그대로 찍는다. 전에는 Spot(높이가 기준값 그대로)을 찍어서, 붕괴 로그의 높이와
	// 이 줄의 높이가 달라 보이는 바람에 어디서 터진 것인지 헷갈렸다.
	//
	// 간격은 두 개를 찍는다. 이 순간 배는 아직 반대쪽 끝으로 **가는 중**이라(이륙과 동시에 드래곤이 난다)
	// 지금 재면 작게 나온다. 다 도착했을 때의 간격(맵 반지름 × 1.8)이 실제로 지켜야 할 값이다.
	const float GapNowCm = FVector2D::Distance(FVector2D(DragonGround.X, DragonGround.Y), FVector2D(Ship2D.X, Ship2D.Y));
	const float GapWhenPlacedCm = MapRadius * 1.8f;
	UE_LOG(LogPGObjects, Display,
		TEXT("PGFinale: Dragon — rising at %s (gap now %.0f m, gap when the ship arrives %.0f m, need %.0f m)"),
		*DragonGround.ToCompactString(), GapNowCm * 0.01f, GapWhenPlacedCm * 0.01f, NeedCm * 0.01f);
	if (GapWhenPlacedCm < NeedCm)
	{
		// 여기까지 왔으면 코드가 낼 수 있는 최대 간격인데도 모자란 것이다. 몰래 때우지 않고 알린다 —
		// 고칠 곳은 DragonScale 이나 맵 크기지 이 함수가 아니다.
		UE_LOG(LogPGObjects, Warning,
			TEXT("PGFinale: the map is too small — %.0f m apart at best but the dragon needs %.0f m. ")
			TEXT("Lower DragonScale (now %.0f) or widen the map."),
			GapWhenPlacedCm * 0.01f, NeedCm * 0.01f, Dragon->DragonScale);
	}
}

void APGFinaleDirector::BeginPlay()
{
	Super::BeginPlay();
	// 멀티(9/28): 드래곤 에셋을 판 시작(로딩 화면이 떠 있는 동안)에 모든 컴퓨터에서 미리 읽는다. 디렉터는 판 시작에 서버가 만들고
	//   (Prewarm) 항상 복제되므로 클라이언트에도 로딩 중에 생긴다. 드래곤이 나오는 순간 읽으면 프레임이 떨어졌고(9/28 사용자 PIE),
	//   전함 도착 때 읽으면 그때 0.5초 멈칫했다(큰 몸을 메모리에 올리는 마무리가 게임을 잠깐 붙잡는다) — 아무도 안 보는 로딩 때가 낫다.
	if (!GetWorld() || !GetWorld()->IsGameWorld())
		return;
	// 몬스터 모양(메시·애니)도 같은 때 미리 읽는다(9/28 4060 측정: 몬스터가 처음 보이는 순간 4.2초 멈춤).
	UPGMonsterLookSet::PreloadAll();
	const UPGCombatSettings* Settings = GetDefault<UPGCombatSettings>();
	if (!Settings || Settings->bEnableFinale)
		APGDragonBoss::PreloadAssets(UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().DragonBossClass, APGDragonBoss::StaticClass()));
}

void APGFinaleDirector::OnRep_State()
{
	// 클라이언트 쪽 연출(경고음·화면 흔들림)은 아직 없다. 지금은 로그만 — UI 담당과 붙일 자리.
	UE_LOG(LogPGObjects, Verbose, TEXT("PGFinale: state = %d"), static_cast<int32>(State));
}

void APGFinaleDirector::ResolveMapExtent(const FVector& Focus, FVector& OutCentre, float& OutMountainRadius) const
{
	OutCentre = Focus;
	OutMountainRadius = 80000.0f; // 못 찾았을 때: 맵 반지름 300m + 산 240m 쯤으로 본다
	const IPGMapInfo* Design = UPGMapInfoSubsystem::FindMap(this);
	if (!Design)
		return;
	// 맵 크기는 "만들어진 지점들"에서 거꾸로 잰다. 타일 배치 배열은 private 이고, 지점은 맵 전체에 골고루 퍼져 있다.
	const TArray<FLevelDesignPoint>& Points = Design->GetLevelDesignPoints();
	if (Points.IsEmpty())
		return;
	FBox2D Box(ForceInit);
	for (const FLevelDesignPoint& Point : Points)
		Box += FVector2D(Point.WorldLocation.X, Point.WorldLocation.Y);
	const FVector2D Centre2D = Box.GetCenter();
	OutCentre = FVector(Centre2D.X, Centre2D.Y, Focus.Z);
	const FVector2D Extent2D = Box.GetExtent();
	// 산줄기 안쪽 고리는 맵 반지름 + 240m 에 있다(AWarZoneFootprintPreview::BuildBorderMountains).
	OutMountainRadius = FMath::Max(Extent2D.X, Extent2D.Y) + 24000.0f;
}

// 공중전의 축. 배는 이 방향 끝, 드래곤은 반대쪽 끝에 선다.
//
// 왜 한 함수로 빼나: 배를 세우는 곳(Launch)과 드래곤을 세우는 곳(SpawnDragon)이 **같은 축**을 써야
//   둘이 양 끝으로 벌어진다. 각자 계산하면 배가 아직 안 움직인 상태에서 축이 달라져 간격이 안 나온다.
//
// 왜 "배 뒤쪽" 인가: 드래곤이 배 **뱃머리 앞**에서 솟아야 조종하는 사람 화면에 잡힌다.
//   배는 뱃머리 방향을 도착할 때(FlyInFrom) 한 번 정하고, 그 뒤로 CruiseTo 는 **방향을 안 바꾼다** —
//   자리만 옮긴다. 그래서 "맵 중심에서 배 쪽" 같은 위치 기준으로 축을 잡으면, 배가 어느 쪽을 보고
//   도착했느냐에 따라 드래곤이 등 뒤에서 솟는다. 축을 배의 **뒤쪽 방향**에 걸면 배는 뒤로 물러나 한쪽 끝에,
//   드래곤은 뱃머리 정면 반대쪽 끝에 서서, 간격(맵 반지름 × 1.8)도 시야도 둘 다 지켜진다.
FVector2D APGFinaleDirector::ResolveFinaleAxis(FVector& OutCentre, float& OutMapRadiusCm) const
{
	float MountainRadius = 0.0f;
	ResolveMapExtent(FocusLocation, OutCentre, MountainRadius);
	OutMapRadiusCm = FMath::Max(MountainRadius - 24000.0f, 10000.0f);
	FVector2D Axis(0.0f, 0.0f);
	if (IsValid(Ship))
	{
		const FVector Back = -Ship->GetActorForwardVector();
		Axis = FVector2D(Back.X, Back.Y);
	}
	if (Axis.IsNearlyZero())
		Axis = FVector2D(1.0f, 0.0f);
	Axis.Normalize();
	return Axis;
}

void APGFinaleDirector::ComputeApproach(const FVector& Focus, FVector& OutStart, FVector& OutHover)
{
	FVector Centre = Focus;
	float MountainRadius = 0.0f;
	ResolveMapExtent(Focus, Centre, MountainRadius);


	// 정박 자리: 보스가 죽은 자리 바로 위가 아니라 그 방향의 맵 외곽. 길이 410m 짜리가 맵 한가운데에 서면
	// 판 전체를 덮어 버린다(9/20 사용자: "함선 좀 더 외곽에 있어도 될듯").
	FVector2D Outward(Focus.X - Centre.X, Focus.Y - Centre.Y);
	if (Outward.IsNearlyZero())
		Outward = FVector2D(1.0f, 0.0f);
	Outward.Normalize();
	// 산줄기 안쪽 고리는 맵 반지름 + 240m 였다(ResolveMapExtent). 거기서 산을 빼면 타일이 끝나는 자리다.
	const float MapRadius = FMath::Max(MountainRadius - 24000.0f, 10000.0f);
	// 여기서 쓰는 거리는 "배 한가운데"까지다. 배가 437m 라 가장자리에 세우면 절반이 맵 밖으로 나가고,
	// 뒤쪽 177m 에 달린 승강 발판은 타일 밖 산비탈에 떨어진다(9/20 PIE). 배 길이를 빼고 세운다.
	const float ShipLength = IsValid(Ship) ? Ship->GetShipLengthCm() : 44000.0f;
	const float HoverRadius = FMath::Clamp(MapRadius - ShipLength * 0.7f, MapRadius * 0.2f, MapRadius * 0.65f);
	OutHover = FVector(
		Centre.X + Outward.X * HoverRadius,
		Centre.Y + Outward.Y * HoverRadius,
		0.0f);
	// 땅 높이는 정박할 자리 밑에서 잰다(보스가 죽은 자리가 아니라). 승강 발판이 닿을 곳도 여기다.
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFinaleGround), false);
	const FVector Probe(OutHover.X, OutHover.Y, 30000.0f);
	GroundZ = GetWorld()->LineTraceSingleByChannel(Hit, Probe, Probe - FVector(0.0f, 0.0f, 80000.0f), ECC_Visibility, Params)
		? Hit.ImpactPoint.Z : 20.0f; // 20 = 이 프로젝트의 땅 높이 기준
	OutHover.Z = GroundZ + HoverAltitude;

	const float StartRadius = MountainRadius + ApproachMargin;
	OutStart = FVector(
		Centre.X + Outward.X * StartRadius,
		Centre.Y + Outward.Y * StartRadius,
		GroundZ + ApproachAltitude);
}

void APGFinaleDirector::PrepareShip(const FVector& Focus)
{
	if (IsValid(Ship))
		return;
	ComputeApproach(Focus, PreparedStart, PreparedHover);
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	// 설정(ProjectPG Visuals > Battleship Class)에 블루프린트가 있으면 그것으로(9/23 블루프린트 분리).
	Ship = GetWorld()->SpawnActor<APGBattleshipActor>(UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().BattleshipClass, APGBattleshipActor::StaticClass()), FTransform(PreparedStart), SpawnParams);
	if (!IsValid(Ship))
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGFinale: prewarm spawn failed"));
		return;
	}
	// 보이지 않게 두고 틱도 끈다. 조립(부품 붙이기·속 깔기)은 그대로 돌아서 비싼 일은 지금 다 끝난다.
	Ship->SetShipHidden(true);
	Ship->OnArrived.AddUObject(this, &APGFinaleDirector::HandleShipArrived);
	// 전함이 체력 0 으로 추락하면 한 줄 남긴다. 드래곤은 Ship->GetHealth() <= 0 을 보고 스스로 지상전으로 넘어가므로
	// 디렉터가 상태를 바꿀 일은 없다 — 로그에서 "드래곤이 왜 내려왔나" 를 바로 이을 표시만 둔다.
	Ship->OnWrecked.AddWeakLambda(this, [](APGBattleshipActor* Wrecked)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGFinale: ship wrecked (hp %.0f) — the dragon reads GetHealth() <= 0 and takes the fight to the ground"),
			IsValid(Wrecked) ? Wrecked->GetHealth() : 0.0f);
	});
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: prewarm — ship parked out of sight at %s (hover will be %s)"),
		*PreparedStart.ToCompactString(), *PreparedHover.ToCompactString());
}

void APGFinaleDirector::SpawnShip()
{
	if (!IsValid(Ship))
		PrepareShip(FocusLocation);
	if (!IsValid(Ship))
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGFinale: battleship spawn failed"));
		EnterState(EPGFinaleState::Idle);
		return;
	}
	// 미리 만들어 둔 배라면 정박 자리를 지금 기준(보스가 죽은 자리)으로 다시 잡는다.
	FVector Start, Hover;
	ComputeApproach(FocusLocation, Start, Hover);
	Ship->SetShipHidden(false);
	bShipRevealed = true;
	Ship->FlyInFrom(Start, Hover);
	UE_LOG(LogPGObjects, Display, TEXT("PGFinale: Arrival — ship from %s to %s"), *Start.ToCompactString(), *Hover.ToCompactString());
}

void APGFinaleDirector::HandleShipArrived(APGBattleshipActor* InShip)
{
	if (InShip == Ship && State == EPGFinaleState::Arrival)
		EnterState(EPGFinaleState::Hover);
}

void APGFinaleDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
		return;
	StateTimer += DeltaSeconds;
	// 지금 상태의 매 틱 처리(위 상태 표). 없으면 아무것도 안 한다.
	if (const FStateHandlers* Handlers = FindStateHandlers(State); Handlers && Handlers->Tick)
		(this->*Handlers->Tick)(DeltaSeconds);
}

void APGFinaleDirector::TickWarningState(float DeltaSeconds)
{
	// 등장 3초 전에 산 너머에서 먼저 보이게 한다. 처음 그릴 때 드는 비용(셰이더·재질 준비)을
	// 아무도 안 보는 동안 치러야 넘어오는 순간이 매끄럽다(9/20 사용자 피드백: "등장할 때 순간 렉").
	if (!bShipRevealed && IsValid(Ship) && StateTimer >= FMath::Max(0.0f, WarningSeconds - 3.0f))
	{
		Ship->SetShipHidden(false);
		bShipRevealed = true;
	}
	if (StateTimer >= WarningSeconds)
		EnterState(EPGFinaleState::Arrival);
}

void APGFinaleDirector::TickArrivalState(float DeltaSeconds)
{
	// 등장이 60초 넘게 안 끝나면(껍데기 팩이 없거나 목표가 이상하면) 그냥 정박으로 넘긴다.
	// 피날레가 도중에 멈춰 버리면 플레이어는 영문도 모르고 기다린다.
	if (!(StateTimer >= 60.0f))
		return;
	UE_LOG(LogPGObjects, Warning, TEXT("PGFinale: arrival timed out, parking the ship where it is"));
	if (IsValid(Ship))
		Ship->SetActorLocation(FVector(FocusLocation.X, FocusLocation.Y, GroundZ + HoverAltitude + 8000.0f));
	EnterState(EPGFinaleState::Hover);
}

void APGFinaleDirector::TickHoverState(float DeltaSeconds)
{
	// 조종석에 앉아야 출발한다(9/21 사용자: "조종석에 앉아야 움직이게").
	// 전에는 "갑판에 사람(또는 차)이 3초" 면 저절로 떠올랐다. 차를 몰고 들어오는 중에 배가 치솟아 차가 빠지고,
	// 시야가 확 바뀌었다 — "함선 안에 차가 있다고 인식되면 바로 날아오르는데 그 와중에 빠지네".
	AboardTimer = IsAnyoneAboard() ? AboardTimer + DeltaSeconds : 0.0f;
	if (IsValid(Ship) && Ship->IsPilotSeated())
		EnterState(EPGFinaleState::Launch);
}

void APGFinaleDirector::TickLaunchState(float DeltaSeconds)
{
	KnockGroundUnderShip();
	// 조종석에 앉아 있은 시간을 센다. 일어나면 0 으로 돌아간다.
	//
	// 9/21 에 기준을 바꿨다. 전에는 이륙 시작부터 셌는데, 이륙은 "갑판에 3초 서 있으면" 시작되고
	// 격납고 입구에서 의자까지가 409m 라 7초 안에 앉는 것이 불가능했다. 그래서 사용자 눈에는
	// 언제나 "그냥 바로 나온다" 였다(같은 증상을 세 번 고쳤는데 세 번 다 빗나간 이유).
	// IsPilotSeated() 는 9/20 에 바로 이 목적으로 만들어 놓고 아무 데서도 안 불리고 있었다.
	const bool bSeatedNow = IsValid(Ship) && Ship->IsPilotSeated();
	SeatedTimer = bSeatedNow ? SeatedTimer + DeltaSeconds : 0.0f;
	if (!IsValid(Dragon))
	{
		// 앉아서 밖을 보고 있으면 그때부터 DragonDelaySeconds 뒤에 솟는다.
		// 끝내 아무도 안 앉으면 보험으로 낸다 — 안 그러면 피날레가 통째로 멈춘다.
		const bool bSeatedLongEnough = SeatedTimer >= DragonDelaySeconds;
		const bool bWaitedTooLong = DragonFallbackSeconds > 0.0f && StateTimer >= DragonFallbackSeconds; // 0 = 보험 끔
		if (bSeatedLongEnough || bWaitedTooLong)
		{
			UE_LOG(LogPGObjects, Display,
				TEXT("PGFinale: dragon cue — %s (seated %.1fs, launch %.1fs, need seated %.1fs or launch %.1fs)"),
				bSeatedLongEnough ? TEXT("pilot seated") : TEXT("nobody sat down, falling back"),
				SeatedTimer, StateTimer, DragonDelaySeconds, DragonFallbackSeconds);
			SpawnDragon();
		}
	}
	// 다 올라가면 드래곤이 나온다.
	// 배가 목표 높이·자리에 닿으면 조종을 시작할 때다.
	// 못 닿아도 20초가 지나면 넘어간다 — 어딘가에 걸려도 피날레가 멈추면 안 된다.
	if (IsValid(Ship) && Ship->HasArrived())
		EnterState(EPGFinaleState::Dragon);
	else if (StateTimer > 20.0f)
		EnterState(EPGFinaleState::Dragon);
}

void APGFinaleDirector::TickDragonState(float DeltaSeconds)
{
	// 이륙 중에 이미 나왔어야 한다. 그래도 없으면 여기서 만든다.
	//
	// 여기서도 앉음을 기다리는 이유: 배가 목표에 일찍 닿으면 이륙 상태가 금방 끝나 버린다.
	// 그때 무조건 만들면 위에서 기준을 바꾼 것이 무의미해진다 — 사용자가 앉기도 전에 나온다.
	// 다만 여기가 마지막 관문이라 보험은 더 짧게 잡는다.
	if (!IsValid(Dragon))
	{
		const bool bSeatedNow = IsValid(Ship) && Ship->IsPilotSeated();
		SeatedTimer = bSeatedNow ? SeatedTimer + DeltaSeconds : 0.0f;
		if (SeatedTimer >= DragonDelaySeconds || (DragonFallbackSeconds > 0.0f && StateTimer >= DragonFallbackSeconds))
		{
			UE_LOG(LogPGObjects, Display,
				TEXT("PGFinale: dragon cue in Dragon state — %s (seated %.1fs, waited %.1fs)"),
				SeatedTimer >= DragonDelaySeconds ? TEXT("pilot seated") : TEXT("nobody sat down, falling back"),
				SeatedTimer, StateTimer);
			SpawnDragon();
		}
	}
	if (IsValid(Dragon) && Dragon->IsDead())
		EnterState(EPGFinaleState::Victory);
}
