#include "Common/PGPhysicsUtil.h"

#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Components/CapsuleComponent.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "LevelDesign/PGMapInfo.h"
#include "Interaction/Interactable.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Common/PGDebrisSubsystem.h"
#include "EngineUtils.h"
#include "Objects/PGFloorItemActor.h"
#include "Actors/ItemContainerActor.h"
#include "Robot/PGRobotCharacter.h"
#include "Common/PGKnockRelay.h"
#include "Engine/OverlapResult.h"

namespace
{
	// 이름에 이 단어가 들어가면 안 날린다. 덤불·풀은 날려도 이상하고, 가로등·기둥은 레벨 구조물.
	const TCHAR* const SkipWords[] = { TEXT("Shrub"), TEXT("Bush"), TEXT("Foliage"), TEXT("Grass"), TEXT("Reed"), TEXT("Cat_Tail"), TEXT("Pole"), TEXT("Lamp"), TEXT("Street") };
	// 거대 몸(LowHeight 를 주는 쪽 = 보스·탑승 로봇·크리처)에게는 풀·덤불만 건너뛴다. 가로등·기둥·조명도 부순다.
	// 안 그러면 지붕을 부순 뒤 공중에 남은 천장 조명·기둥이 17m 로봇 앞을 벽처럼 막았다(9/19).
	const TCHAR* const GiantSkipWords[] = { TEXT("Shrub"), TEXT("Bush"), TEXT("Foliage"), TEXT("Grass"), TEXT("Reed"), TEXT("Cat_Tail") };
	// 이름에 이 단어가 들어가면 무겁게(질량 2배, 튀는 속도 60%). 돌은 굴러가되 소품처럼 붕 뜨진 않게. 맵에 돌이 많아 달리는 맛은 살린다.
	const TCHAR* const HeavyWords[] = { TEXT("rock"), TEXT("stone"), TEXT("boulder"), TEXT("concrete") };
	// 나무: 바운드 상자(10m 높이)를 그대로 물리로 쓰면 차를 덮어 튕겨 올린다. 줄기 굵기의 얇은 기둥으로 만들고 차·폰과는 안 부딪히게 해서
	// "꺾여 쓰러지는" 것만 보여준다. 이름 또는 가늘고 높은 형태(높이가 폭의 2배 넘고 2.5m 이상)로 판별.
	const TCHAR* const TreeWords[] = { TEXT("Pine"), TEXT("Tree"), TEXT("oak"), TEXT("narrowleaf"), TEXT("birch"), TEXT("spruce") };

	// ---- 잔해 예산 ----
	// 8배 로봇이 공장 안을 걸으면 한 프레임에 선반·드럼통 수십 개가 걸린다. 하나마다 액터 스폰 + HISM 인스턴스 제거(트리 재구성) +
	// 물리 몸 + 움직이는 그림자(VSM 페이지 무효화)라 프레임이 뚝 떨어졌다. 그래서 세 가지로 묶는다.
	//  PerFrame : 한 프레임에 새로 만드는 수. 넘치면 그 소품은 그대로 두고 다음 프레임 스윕이 다시 잡는다(눈에는 연달아 무너지는 것으로 보인다).
	//  MaxAlive : 동시에 살아 있는 잔해 수. 넘치면 가장 오래된 것부터 치운다.
	//  LifeSeconds : 잔해 수명.
	TAutoConsoleVariable<int32> CVarKnockPerFrame(TEXT("PG.Knock.PerFrame"), 4, TEXT("Max knock proxies spawned per frame."));
	// 120 이었다. 공장 하나 부수면 금방 120 개가 차서 Game 81ms 까지 갔다. 굳히기(FreezeSeconds)와 잔해끼리 무충돌을 넣고 40 으로.
	TAutoConsoleVariable<int32> CVarKnockMaxAlive(TEXT("PG.Knock.MaxAlive"), 40, TEXT("Max knock proxies alive at once; oldest are destroyed first."));
	TAutoConsoleVariable<float> CVarKnockLifeSeconds(TEXT("PG.Knock.LifeSeconds"), 25.0f, TEXT("Knock proxy lifespan in seconds."));
	//  FreezeSeconds : 이만큼 지나면 물리를 끄고 그 자리에 굳힌다(0 이면 안 굳힘).
	// 이보다 큰 조각(바운드 반대각선 cm)은 물리로 날리지 않고 "그 자리에서 기울며 내려앉는" 연출만 한다(물리 없음).
	// 20m 벽·지붕 한 장을 물리 상자로 만들면 바닥·옆 벽 속에 파묻힌 채 태어나 겹침 풀기 계산이 폭발했다(공장 부수는 순간 Game 90ms).
	// 800 → 300: stat namedevents 로 찍어 보니 공장 부술 때 게임 스레드가 기다린 6초 중 대부분이 Chaos Collisions::NarrowPhase 였다.
	// 큰 상자가 공장 벽·바닥의 복잡한 충돌(삼각형 수천 개)과 맞닿아 상자-삼각형 비교가 폭발한다. 벽·지붕 판은 무너지기로, 통·팔레트만 물리로.
	TAutoConsoleVariable<float> CVarKnockCollapseRadius(TEXT("PG.Knock.CollapseRadius"), 300.0f, TEXT("Pieces larger than this (cm) collapse in place without physics (0 = always physics)."));
	// 물리 상자를 메시 바운드보다 이만큼 작게. 바운드 그대로면 이웃 조각·바닥과 늘 살짝 겹친 채 태어난다.
	TAutoConsoleVariable<float> CVarKnockBoxShrink(TEXT("PG.Knock.BoxShrink"), 0.85f, TEXT("Physics box extent multiplier relative to mesh bounds."));
	TAutoConsoleVariable<float> CVarKnockFreezeSeconds(TEXT("PG.Knock.FreezeSeconds"), 4.0f, TEXT("Seconds after which a knock proxy stops simulating and stays in place (0 = never)."));
	// 이보다 작은 잔해(가장 긴 변의 절반, cm)는 그림자를 안 드리운다. 움직이는 작은 그림자 수십 개가 VSM 을 계속 다시 그리게 한다.
	// 1 이면 예전 방식(부술 때마다 액터 생성 + 전부 Chaos 물리 + 큰 조각은 기울며 가라앉기). 새 방식(UPGDebrisSubsystem)과 비교 측정용.
	TAutoConsoleVariable<int32> CVarDebrisLegacy(TEXT("PG.Debris.Legacy"), 0, TEXT("1 = old per-knock actor spawning with Chaos physics (for A/B measurement)."));
	TAutoConsoleVariable<float> CVarKnockShadowMinExtent(TEXT("PG.Knock.ShadowMinExtent"), 120.0f, TEXT("Knock proxies smaller than this half-extent cast no shadow."));

	uint64 BudgetFrame = 0;
	int32 SpawnedThisFrame = 0;
	TArray<TWeakObjectPtr<AActor>> LiveProxies; // 만든 순서 = 오래된 순서

	bool TakeSpawnBudget()
	{
		if (BudgetFrame != GFrameCounter)
		{
			BudgetFrame = GFrameCounter;
			SpawnedThisFrame = 0;
		}
		if (SpawnedThisFrame >= FMath::Max(1, CVarKnockPerFrame.GetValueOnGameThread()))
			return false;
		++SpawnedThisFrame;
		return true;
	}

	void TrackProxy(AActor* Proxy)
	{
		LiveProxies.RemoveAll([](const TWeakObjectPtr<AActor>& P) { return !P.IsValid(); }); // 수명이 다했거나 이전 PIE 세션 것
		LiveProxies.Add(Proxy);
		const int32 MaxAlive = FMath::Max(8, CVarKnockMaxAlive.GetValueOnGameThread());
		while (LiveProxies.Num() > MaxAlive)
		{
			if (AActor* Oldest = LiveProxies[0].Get())
				Oldest->Destroy();
			LiveProxies.RemoveAt(0);
		}
	}

	// Delay 초 뒤에 굳힌다. 단 그때 아직 움직이고 있으면(날아가는 중·벽에 걸쳐 미끄러지는 중) 1초 뒤에 다시 본다.
	// 시간만 보고 굳혔더니 드럼통이 벽 옆 공중에서 멈춰 떠 있었다(9/19). 수명(LifeSeconds)이 다하면 어차피 사라진다.
	void ArmFreeze(UWorld* World, UBoxComponent* Box, float Delay = -1.0f)
	{
		const float FreezeSeconds = Delay > 0.0f ? Delay : CVarKnockFreezeSeconds.GetValueOnGameThread();
		if (FreezeSeconds <= 0.0f || !IsValid(World) || !IsValid(Box))
			return;
		FTimerHandle FreezeTimer;
		TWeakObjectPtr<UBoxComponent> WeakBox(Box);
		World->GetTimerManager().SetTimer(FreezeTimer, [WeakBox]()
		{
			UBoxComponent* B = WeakBox.Get();
			if (!IsValid(B) || !B->IsSimulatingPhysics())
				return;
			if (B->GetPhysicsLinearVelocity().SizeSquared() > FMath::Square(30.0f) || B->GetPhysicsAngularVelocityInDegrees().SizeSquared() > FMath::Square(45.0f))
			{
				ArmFreeze(B->GetWorld(), B, 1.0f);
				return;
			}
			B->SetSimulatePhysics(false);
		}, FreezeSeconds, false);
	}

	// 큰 조각 무너뜨리기: 물리 없이, 보이는 메시만 1.2초 동안 밀린 쪽으로 기울며 땅속으로 내려앉고 사라진다.
	// 물리 상자·충돌·길찾기 갱신이 전부 없어서 20m 벽 한 장도 비용이 거의 0 이다. 멀리서 보면 "무너져 내린" 것으로 보인다.
	AActor* SpawnCollapseProxy(UWorld* World, UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_SpawnCollapse);
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Proxy = World->SpawnActor<AActor>(AActor::StaticClass(), WorldTransform, Params);
		if (!IsValid(Proxy))
			return nullptr;
		UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Proxy, TEXT("CollapseVisual"));
		Visual->SetMobility(EComponentMobility::Movable);
		Visual->SetStaticMesh(Mesh);
		Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Visual->SetCanEverAffectNavigation(false);
		Visual->SetAffectDistanceFieldLighting(false);
		Visual->SetAffectDynamicIndirectLighting(false);
		Proxy->SetRootComponent(Visual);
		Visual->RegisterComponent();
		Proxy->SetActorTransform(WorldTransform);

		// 3단계: 쓰러짐(1.2초) → 바닥에 누운 채 머묾 → 오래 지나면 천천히 치움.
		// 왜: 처음엔 1.2초 만에 통째로 가라앉았고, 그다음엔 "기울며 30% 가라앉기"로 바꿨는데 둘 다 땅에 박혀 보였다(9/19 PIE 두 번).
		// 원점(메시 가운데·바닥 등 제각각)을 축으로 돌리면 아래 모서리가 땅을 파고든다. 그래서 "밀린 쪽 바닥 모서리"를 경첩으로 삼아
		// 실제 물건이 넘어지듯 90도 눕힌다 — 땅 위에 그대로 눕는다. 물리 없이 보이기만 하는 건 그대로라 비용은 같다.
		const float FallTime = 1.2f;
		const float RestTime = 20.0f;
		const float SinkTime = 3.0f;
		const FVector PushDir = Impulse.GetSafeNormal2D();
		// 기울 축: 밀린 방향으로 윗부분이 넘어가게(Up x Dir).
		const FVector TiltAxis = FVector::CrossProduct(FVector::UpVector, PushDir).GetSafeNormal();
		const FBox WorldBox = Mesh->GetBoundingBox().TransformBy(WorldTransform);
		const FVector BoxCenter = WorldBox.GetCenter();
		const FVector BoxExtent = WorldBox.GetExtent();
		// 밀린 방향으로 잰 반폭 = 경첩 모서리까지 거리. 누우면 이만큼의 두 배가 새 "높이"가 된다(마지막에 그만큼 내려 치운다).
		const float HalfDepth = FMath::Abs(BoxExtent.X * PushDir.X) + FMath::Abs(BoxExtent.Y * PushDir.Y);
		const FVector Hinge(BoxCenter.X + PushDir.X * HalfDepth, BoxCenter.Y + PushDir.Y * HalfDepth, WorldBox.Min.Z);
		const float LyingHeight = FMath::Max(HalfDepth * 2.0f, 10.0f);
		const FTransform Start = WorldTransform;
		const double StartTime = World->GetTimeSeconds();
		TWeakObjectPtr<AActor> WeakProxy(Proxy);
		TWeakObjectPtr<UWorld> WeakWorld(World);
		// 핸들을 공유 포인터로 들고 있어야 잔해가 사라진 뒤 타이머가 스스로 멈출 수 있다(예전엔 빈 타이머가 초당 30번 영원히 돌았다).
		TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		FTimerDelegate Step;
		Step.BindLambda([WeakProxy, WeakWorld, Handle, Start, StartTime, FallTime, RestTime, SinkTime, TiltAxis, Hinge, LyingHeight]()
		{
			AActor* P = WeakProxy.Get();
			if (!IsValid(P))
			{
				if (UWorld* W = WeakWorld.Get())
					W->GetTimerManager().ClearTimer(*Handle);
				return;
			}
			const float Elapsed = static_cast<float>(P->GetWorld()->GetTimeSeconds() - StartTime);
			if (Elapsed > FallTime && Elapsed < FallTime + RestTime)
				return; // 누워 있는 동안은 움직일 게 없다
			const float Fall = FMath::Clamp(Elapsed / FallTime, 0.0f, 1.0f);
			const float FallEase = Fall * Fall; // 처음엔 천천히, 점점 빠르게(넘어가는 느낌)
			const float Sink = FMath::Clamp((Elapsed - FallTime - RestTime) / SinkTime, 0.0f, 1.0f);
			const FQuat Tilt = TiltAxis.IsNearlyZero() ? FQuat::Identity : FQuat(TiltAxis, FMath::DegreesToRadians(90.0f * FallEase));
			// 경첩(바닥 모서리)을 중심으로 돌린다: 위치도 경첩 기준으로 같이 돈다.
			FTransform Now = Start;
			Now.SetRotation(Tilt * Start.GetRotation());
			Now.SetLocation(Hinge + Tilt.RotateVector(Start.GetLocation() - Hinge) - FVector(0.0f, 0.0f, LyingHeight * 1.1f * Sink));
			P->SetActorTransform(Now);
		});
		World->GetTimerManager().SetTimer(*Handle, Step, 1.0f / 30.0f, true);
		Proxy->SetLifeSpan(FallTime + RestTime + SinkTime + 0.1f); // 액터가 사라지면 약한 참조가 비어 타이머 몸통은 아무것도 안 한다
		TrackProxy(Proxy);
		return Proxy;
	}

	// 받침(선반·탁자·상자)이 부서져 사라지면 그 위에 놓였던 바닥 아이템을 아래 바닥으로 내린다.
	// 왜: 차·로봇이 선반만 치워서, 선반 위 아이템이 풀밭 공중에 줄지어 떠 있었다(9/19 PIE).
	// 아이템 수는 맵 전체에 수백 개라 액터 반복으로 위치만 본다(겹침 질의는 아이템 충돌 설정에 따라 못 찾을 수 있다).
	void DropSupportedItems(UWorld* World, const FBox& Support, const UPrimitiveComponent* Removed, const FVector& Impulse = FVector::ZeroVector)
	{
		if (!IsValid(World) || !Support.IsValid)
			return;
		TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_DropItems);
		const FBox Zone(FVector(Support.Min.X - 20.0f, Support.Min.Y - 20.0f, Support.Max.Z - 40.0f),
			FVector(Support.Max.X + 20.0f, Support.Max.Y + 20.0f, Support.Max.Z + 120.0f));
		// 상자: 얹혀 있던 조각과 같이 날아간다(9/23). 전에는 바닥 아이템만 챙겨서 건물을 날려도 상자는 허공에 떠 있었다.
		//   조각이 받은 힘의 절반 + 살짝 위로. 받침이 나중에 사라진 경우(Impulse 가 거의 0)는 그냥 떨어진다.
		for (TActorIterator<AItemContainerActor> It(World); It; ++It)
		{
			AItemContainerActor* Container = *It;
			if (IsValid(Container) && !Container->IsKnockedLoose() && Zone.IsInsideOrOn(Container->GetActorLocation()))
				Container->KnockLoose(Impulse * 0.5f + FVector(0.0f, 0.0f, Impulse.Size() > 100.0f ? 300.0f : 0.0f));
		}
		for (TActorIterator<APGFloorItemActor> It(World); It; ++It)
		{
			APGFloorItemActor* Item = *It;
			if (!IsValid(Item) || !Zone.IsInsideOrOn(Item->GetActorLocation()))
				continue;
			FVector Origin, Extent;
			Item->GetActorBounds(true, Origin, Extent);
			FCollisionObjectQueryParams Objects;
			Objects.AddObjectTypesToQuery(ECC_WorldStatic);
			Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDropItem), false, Item);
			if (Removed)
				Params.AddIgnoredComponent(Removed);
			TArray<FHitResult> Hits;
			World->LineTraceMultiByObjectType(Hits, Origin, Origin - FVector(0.0f, 0.0f, 3000.0f), Objects, Params);
			for (const FHitResult& Hit : Hits)
			{
				const UPrimitiveComponent* Floor = Hit.GetComponent();
				// 길찾기용 보이지 않는 바닥판(탈것 무시)·폰은 바닥으로 치지 않는다.
				if (!IsValid(Floor) || Floor->GetCollisionResponseToChannel(ECC_Vehicle) != ECR_Block || Cast<APawn>(Hit.GetActor()))
					continue;
				const float Drop = Hit.ImpactPoint.Z - (Origin.Z - Extent.Z);
				if (Drop < -5.0f)
					Item->SetActorLocation(Item->GetActorLocation() + FVector(0.0f, 0.0f, Drop));
				break;
			}
		}
	}

	bool NameHasAny(const FString& Name, const TCHAR* const* Words, int32 Count)
	{
		for (int32 I = 0; I < Count; ++I)
			if (Name.Contains(Words[I]))
				return true;
		return false;
	}
}

AActor* PGPhysicsUtil::SpawnKnockProxy(UWorld* World, UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse, float MassKg, AActor* Instigator, const UPrimitiveComponent* Source, bool bFallOnly)
{
	// 부수기 비용을 Unreal Insights 에서 이름으로 보이게(공장 부술 때 프레임 드랍 원인 찾기). 평소 비용은 없다.
	TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_SpawnProxy);
	if (!IsValid(World) || !IsValid(Mesh))
		return nullptr;

	const FBox LocalBox = Mesh->GetBoundingBox();
	const FVector Scale = WorldTransform.GetScale3D().GetAbs();
	const FVector LocalCenter = LocalBox.GetCenter();
	FVector Extent = LocalBox.GetExtent() * Scale;
	if (Extent.GetMax() < 5.0f)
		return nullptr;
	const bool bTree = IsTreeMesh(Mesh, WorldTransform.GetScale3D());
	// 나무는 제대로 속도를 내고 박아야 꺾인다(속도 변화 400cm/s 미만이면 그대로 서 있음). 살살 밀어서 숲이 넘어가면 이상하다.
	if (bTree && Impulse.Size2D() < 400.0f)
		return nullptr;
	if (bTree)
	{
		// 줄기만: 폭의 15%(20~60cm), 높이는 그대로.
		const float Trunk = FMath::Clamp(FMath::Max(Extent.X, Extent.Y) * 0.15f, 20.0f, 60.0f);
		Extent.X = Trunk;
		Extent.Y = Trunk;
	}

	if (!TakeSpawnBudget())
		return nullptr; // 이번 프레임 몫을 다 썼다. 원본은 그대로 남고 다음 프레임에 다시 잡힌다.
	++DebrisMadeCount;

	const float CollapseRadius = CVarKnockCollapseRadius.GetValueOnGameThread();
	// 새 방식: 미리 만들어 둔 잔해 묶음에서 하나 꺼내 쓴다(PGDebrisSubsystem.h 머리말). 큰 판·나무는 넘어지기, 나머지는 날아가기.
	if (CVarDebrisLegacy.GetValueOnGameThread() == 0)
	{
		if (UPGDebrisSubsystem* Debris = UPGDebrisSubsystem::Get(World))
		{
			const bool bFall = bFallOnly; // 받침 무너짐: 넘어지지 않고 그대로 떨어진다(가벼운 흉내)
			// 넘어뜨리기(Tip)는 "서 있는 것"에만 쓴다. 눕혀져 있는 넓은 판을 90도 돌리면 오히려 세로로 서 버린다
			// (9/20 PIE: 컨테이너·벽판이 넘어가다 비스듬히 선 채로 굳었다). 키가 폭보다 크지 않으면 그냥 날린다.
			const bool bStanding = bTree || Extent.Z > FMath::Max(Extent.X, Extent.Y) * 0.8f;
			const bool bTip = !bFall && bStanding && (bTree || (CollapseRadius > 0.0f && Extent.Size() > CollapseRadius));
			const bool bHeavyPiece = NameHasAny(Mesh->GetName(), HeavyWords, UE_ARRAY_COUNT(HeavyWords));
			return Debris->Launch(Mesh, WorldTransform, Impulse, bTip ? EPGDebrisMotion::Tip : EPGDebrisMotion::Fly, MassKg, bHeavyPiece, Source, Instigator, bFall);
		}
	}
	if (!bTree && CollapseRadius > 0.0f && Extent.Size() > CollapseRadius)
		return SpawnCollapseProxy(World, Mesh, WorldTransform, Impulse);

	// 상자는 바운드 중심에 두고, 메시는 그만큼 반대로 밀어 원래 자리에 보이게 한다.
	FTransform ProxyTransform(WorldTransform.GetRotation(), WorldTransform.TransformPosition(LocalCenter));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Proxy = World->SpawnActor<AActor>(AActor::StaticClass(), ProxyTransform, Params);
	if (!IsValid(Proxy))
		return nullptr;

	const bool bHeavy = NameHasAny(Mesh->GetName(), HeavyWords, UE_ARRAY_COUNT(HeavyWords));

	UBoxComponent* Box = NewObject<UBoxComponent>(Proxy, TEXT("KnockBox"));
	Box->SetBoxExtent(bTree ? Extent : Extent * FMath::Clamp(CVarKnockBoxShrink.GetValueOnGameThread(), 0.3f, 1.0f));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_PhysicsBody);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	// 처음부터 차와 부딪힌다. 그래야 천천히 밀 때 차가 잔해를 실제로 밀고 간다(전에는 잠시 무시해서 통과했다).
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore); // 총알·F 트레이스에 잔해가 걸리지 않게
	// 잔해는 캐릭터(플레이어·몬스터·크리처)를 막지 않는다. 날아가는 상자가 캡슐을 파고들면 캐릭터 이동이 "겹침 해소"로
	// 캡슐을 밀어내는데, 상자가 빠를수록 그 거리가 커서 몬스터가 잔해와 같이 날아갔다. 차는 물리 몸끼리라 계속 밀고 간다.
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// 잔해는 길찾기 지도(내비메시)에 넣지 않는다. 박스 컴포넌트는 엔진 기본값이 "길찾기에 영향 줌"이고 이 프로젝트는
	// RuntimeGeneration=Dynamic 이라, 날아가는 상자가 움직일 때마다 주변 내비 타일을 다시 구웠다. 공장이 무너질 때 프레임이 뚝 떨어진 원인.
	Box->SetCanEverAffectNavigation(false);
	// 잔해끼리는 부딪히지 않는다. 잔해 상자는 원래 메시의 바운드 크기라 이웃 벽 판자·지붕 조각끼리 겹친 채로 태어나는데,
	// 서로 부딪히게 두면 물리가 겹침을 풀려고 계산이 폭발했다(PG.Knock.MaxAlive 120→10 으로 줄이자 Game 81ms→13ms. 지붕 부술 때 순간 60ms).
	// 땅·건물·차와는 그대로 부딪힌다.
	Box->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Ignore);
	if (bTree)
	{
		// 쓰러지는 나무는 차도 안 막는다(막으면 차가 튕겨 오른다). 땅·건물에만 걸려 넘어진다.
		Box->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	}
	// 무게는 크기에 비례(대략 부피). 돌 60kg, 벽 판자 400kg, 큰 블록 1톤 — 1.3톤 차가 밀면 실제처럼 밀린다.
	const float Radius = Extent.Size();
	const float SizeMass = 20.0f + Radius * Radius * 0.004f;
	Box->SetMassOverrideInKg(NAME_None, FMath::Max(MassKg, SizeMass) * (bHeavy ? 2.0f : 1.0f) * (bTree ? 3.0f : 1.0f), true);
	Box->SetLinearDamping(0.3f);
	Box->SetAngularDamping(0.5f);
	// 잔해는 발판이 아니다. 보스가 날아가는 벽 판자 위에 "서서"(floor=Actor_96) 같이 하늘로 올라간 로그가 있다.
	// 발 높이 5.7m 짜리 보스는 날아오르는 상자도 계단처럼 밟는다. 밟을 수 없게 하면 캐릭터는 그 위에서 미끄러져 내린다.
	Box->CanCharacterStepUpOn = ECB_No;
	Proxy->SetRootComponent(Box);
	Box->RegisterComponent();
	// 루트 없이 스폰된 액터는 스폰 위치를 잃는다. 루트를 붙인 뒤 위치를 다시 넣어야 원점(0,0,0)이 아니라 소품 자리에 생긴다.
	Box->SetWorldLocationAndRotation(ProxyTransform.GetLocation(), ProxyTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);

	UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Proxy, TEXT("KnockVisual"));
	// 런타임에 만든 스태틱 메시 컴포넌트는 기본이 Static 이라 Movable 상자에 안 붙는다(붙이기 실패 → 원점으로 가서 안 보임).
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetStaticMesh(Mesh);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetCastShadow(Extent.GetMax() >= CVarKnockShadowMinExtent.GetValueOnGameThread());
	// 잔해는 Lumen(간접광·거리장)에서 뺀다. Insights 로 재 보니 공장이 무너지는 동안 GPU 가 거의 100% 였고, 부술 때만 늘어난 일이
	// UpdateDistanceFieldObjectBuffers 였다. 날아가는 메시 하나하나가 매 프레임 거리장·Lumen 장면을 다시 갱신하게 만든다.
	// 몇 초 날아가는 잔해의 간접광 차이는 눈에 안 띈다. 직접광·그림자는 그대로라 모양은 똑같이 보인다.
	Visual->SetAffectDistanceFieldLighting(false);
	Visual->SetAffectDynamicIndirectLighting(false);
	Visual->SetupAttachment(Box);
	Visual->SetRelativeLocation(-LocalCenter * Scale);
	Visual->SetRelativeScale3D(WorldTransform.GetScale3D());
	Visual->RegisterComponent();

	// 밀친 쪽이 캐릭터(로봇)면 서로 안 부딪힌다. 8배 로봇은 캡슐이 벽 판자 몇 개를 통째로 덮을 만큼 커서, 대리 상자가
	// 캡슐 안에 생기면 캐릭터 이동이 매 프레임 "겹침 해소"로 몸을 밀어 올린다(보스·탑승 로봇이 순간이동하듯 튀다 하늘로 간 원인).
	// 차는 물리 몸이라 상자를 실제로 밀고 가야 하므로 그대로 둔다.
	if (ACharacter* Character = Cast<ACharacter>(Instigator))
	{
		Box->IgnoreActorWhenMoving(Character, true);
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
			Capsule->IgnoreComponentWhenMoving(Box, true);
	}

	Box->SetSimulatePhysics(true);
	if (bTree)
	{
		// 꺾임: 밀린 방향으로 윗부분이 넘어가게 회전을 주고, 옆으로는 살짝만 민다. 각속도 = Up x Dir 이면 꼭대기가 Dir 쪽으로 간다.
		const FVector Dir = Impulse.GetSafeNormal2D();
		Box->AddImpulse(Dir * 150.0f, NAME_None, true);
		Box->AddAngularImpulseInDegrees(FVector::CrossProduct(FVector::UpVector, Dir) * 160.0f, NAME_None, true);
	}
	else
	{
		// 작은 것은 더 빠르게, 큰 것은 묵직하게(반지름 2m 기준으로 0.6~1.8배).
		const float VelocityScale = FMath::Clamp(200.0f / FMath::Max(Radius, 1.0f), 0.6f, 1.8f) * (bHeavy ? 0.6f : 1.0f);
		Box->AddImpulse(Impulse * VelocityScale, NAME_None, true);
		// 구르는 맛: 임의 축으로 회전 속도(도/초)
		Box->AddAngularImpulseInDegrees(FMath::VRand() * FMath::FRandRange(180.0f, 540.0f), NAME_None, true);
	}
	Proxy->SetLifeSpan(FMath::Max(1.0f, CVarKnockLifeSeconds.GetValueOnGameThread())); // 90초였다. 잔해가 쌓일수록 물리·그림자 비용이 계속 남는다
	// 날아가서 떨어질 시간만 물리로 돌리고, 그 뒤엔 그 자리에 굳힌다. 굳은 잔해는 보이기만 하고 물리 계산이 없다.
	// (차가 밀고 가는 맛은 날아가는 몇 초 동안만 남는다. 굳은 뒤에는 벽처럼 막는다.)
	ArmFreeze(World, Box);
	TrackProxy(Proxy);
	// 로그도 프레임마다 수십 줄이면 비용이다. 평소엔 Verbose.
	UE_LOG(LogTemp, Verbose, TEXT("KnockProxy %s mesh=%s at %s extent=%s"), *Proxy->GetName(), *Mesh->GetName(),
		*Box->GetComponentLocation().ToCompactString(), *Extent.ToCompactString());
	return Proxy;
}

bool PGPhysicsUtil::IsTreeMesh(const UStaticMesh* Mesh, const FVector& Scale)
{
	if (!IsValid(Mesh))
		return false;
	if (NameHasAny(Mesh->GetName(), TreeWords, UE_ARRAY_COUNT(TreeWords)))
		return true;
	const FVector E = Mesh->GetBounds().BoxExtent * Scale.GetAbs();
	return E.Z > 250.0f && E.Z > 2.0f * FMath::Max(E.X, E.Y);
}

bool PGPhysicsUtil::IsKnockableMesh(const UStaticMesh* Mesh, const FVector& Scale)
{
	if (!IsValid(Mesh))
		return false;
	return !NameHasAny(Mesh->GetName(), SkipWords, UE_ARRAY_COUNT(SkipWords));
}

namespace PGPhysicsUtil
{
namespace
{
	// ---- 소품 부수기: 종류별 처리기 (2026-09-28 TryKnockProp 에서 떼어 냄 — 동작 그대로) ----
	// 왜: TryKnockProp 한 함수가 "이미 잔해 / 묶음 인스턴스 / 통째로 부서지는 문 / 상자 / 레벨 메시" 를 차례로 if 로 갈라 190줄이었다.
	//   부술 수 있는 종류가 늘 때마다 그 함수를 고쳐야 했다(개방-폐쇄 원칙 위반). 이제 종류마다 처리기 하나이고, TryKnockProp 은 공통 검사 뒤
	//   아래 표(KnockHandlers)를 순서대로 물어본다. 새 종류는 처리기 하나를 쓰고 표에 한 줄 넣으면 된다.
	// 처리기의 답: NotMine = 내 종류 아님(다음 처리기로), Refused = 내 종류지만 이번엔 안 부숨(끝), Knocked = 부숨(끝).
	enum class EPGKnockOutcome : uint8 { NotMine, Refused, Knocked };

	struct FPGKnockContext
	{
		UPrimitiveComponent* Component = nullptr;
		const FHitResult& Hit;
		const FVector& Impulse;
		AActor* Instigator = nullptr;
		float MaxRadius = 0.0f;
		float MassKg = 0.0f;
		float LowHeight = 0.0f;
		float FeetZ = 0.0f;
		bool bFallOnly = false;
		bool bClient = false;

		// 크기 한도(설명은 TryKnockProp 머리 주석): 너무 커서 이 몸으로는 못 부수나.
		bool TooBig(const FBoxSphereBounds& Bounds) const
		{
			if (Bounds.SphereRadius <= MaxRadius)
				return false;
			const bool bLow = LowHeight > 0.0f && Bounds.BoxExtent.Z * 2.0f <= LowHeight;
			const bool bFloor = Bounds.Origin.Z + Bounds.BoxExtent.Z < FeetZ + 60.0f;
			return !(bLow && !bFloor);
		}
	};

	// 1) 이미 잔해·굳은 잔해·물리로 움직이는 것: 다시 밀기만.
	EPGKnockOutcome KnockExistingDebris(FPGKnockContext& Ctx)
	{
		UPrimitiveComponent* Component = Ctx.Component;
		const FVector& Impulse = Ctx.Impulse;
		AActor* Instigator = Ctx.Instigator;
		// 이미 잔해(미리 만든 묶음의 조각)면 다시 밀기만. 누워 있던 판자도 차·몬스터가 다시 치면 밀려난다.
		if (UPGDebrisSubsystem* Debris = UPGDebrisSubsystem::Get(Component->GetWorld()); Debris && Debris->IsDebris(Component))
			return Debris->Kick(Component, Impulse, Instigator) ? EPGKnockOutcome::Knocked : EPGKnockOutcome::Refused;

		// 이미 날아간 잔해면 한 번 더 밀기만. 단 차만: 로봇은 매 Tick 앞을 스윕하므로 발밑 잔해에 프레임마다 위쪽 속도(+1920cm/s)를
		// 더해 로켓처럼 쏘아 올렸다. 차는 접촉 물리로 밀리는 게 주고, 이건 보조라 그대로 둔다.
		// 굳어 있던 잔해(KnockBox, 물리 꺼짐)를 다시 박으면 깨워서 날린다. 안 그러면 굳은 판자가 벽처럼 차를 막았다.
		if (UBoxComponent* Frozen = Cast<UBoxComponent>(Component); IsValid(Frozen) && !Frozen->IsSimulatingPhysics() && Frozen->GetFName() == TEXT("KnockBox"))
		{
			Frozen->SetSimulatePhysics(true);
			Frozen->AddImpulse(Impulse, NAME_None, true);
			ArmFreeze(Frozen->GetWorld(), Frozen);
			return EPGKnockOutcome::Knocked;
		}
		if (Component->IsSimulatingPhysics())
		{
			if (Cast<ACharacter>(Instigator))
				return EPGKnockOutcome::Refused;
			Component->AddImpulse(Impulse, NAME_None, true);
			return EPGKnockOutcome::Knocked;
		}
		return EPGKnockOutcome::NotMine;
	}

	// 2) 타일 묶음(인스턴스 메시)의 인스턴스 하나.
	EPGKnockOutcome KnockInstance(FPGKnockContext& Ctx)
	{
		UPrimitiveComponent* Component = Ctx.Component;
		const FHitResult& Hit = Ctx.Hit;
		const FVector& Impulse = Ctx.Impulse;
		AActor* Instigator = Ctx.Instigator;
		const float MassKg = Ctx.MassKg;
		const float LowHeight = Ctx.LowHeight;
		const bool bFallOnly = Ctx.bFallOnly;
		const bool bClient = Ctx.bClient;
		auto TooBig = [&Ctx](const FBoxSphereBounds& Bounds) { return Ctx.TooBig(Bounds); };
		// 1) PCG/인스턴스 메시: 인스턴스 하나를 떼어내 대리 액터로
		if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			UStaticMesh* Mesh = Instanced->GetStaticMesh();
			if (!IsValid(Mesh) || Hit.Item < 0 || !Instanced->IsValidInstance(Hit.Item))
				return EPGKnockOutcome::Refused;
			FTransform InstanceTransform;
			if (!Instanced->GetInstanceTransform(Hit.Item, InstanceTransform, true))
				return EPGKnockOutcome::Refused;
			const float Radius = Mesh->GetBounds().SphereRadius * InstanceTransform.GetScale3D().GetAbsMax();
			if (!(LowHeight > 0.0f ? !NameHasAny(Mesh->GetName(), GiantSkipWords, UE_ARRAY_COUNT(GiantSkipWords)) : IsKnockableMesh(Mesh, InstanceTransform.GetScale3D())))
				return EPGKnockOutcome::Refused;
			if (TooBig(Mesh->GetBounds().TransformBy(InstanceTransform)) && !IsTreeMesh(Mesh, InstanceTransform.GetScale3D()))
				return EPGKnockOutcome::Refused;
			if (!SpawnKnockProxy(Instanced->GetWorld(), Mesh, InstanceTransform, Impulse, MassKg + Radius * 0.6f, Instigator, Instanced, bFallOnly))
				return EPGKnockOutcome::Refused;
			APGKnockRelay::Send(Instanced->GetWorld(), Mesh, InstanceTransform, Impulse, MassKg + Radius * 0.6f, bFallOnly); // 멀티(9/27): 클라도 지우고 날리게
			if (bClient)
				NotePredictedKnock(Instanced->GetWorld(), Mesh, InstanceTransform.GetLocation());
			// 같은 시각에 한 인스턴스는 제자리, 다른 하나는 멀리 떨어진 자리에 대리 액터가 생긴 기록이 있다(원인 미확인). 어느 컴포넌트·몇 번 인스턴스인지 남긴다.
			UE_LOG(LogTemp, Verbose, TEXT("KnockProxy from %s (%s) item=%d hit=%s"), *GetNameSafe(Instanced->GetOwner()), *Instanced->GetName(), Hit.Item, *Hit.ImpactPoint.ToCompactString());
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_RemoveInstance);
				Instanced->RemoveInstance(Hit.Item);
			}
			// 얹힌 아이템 떨구기·받침 무너짐은 서버만(아이템은 복제되고, 무너짐은 서버 알림으로 온다) — 멀티 9/28 먼저 부수기.
			if (!bClient)
				DropSupportedItems(Instanced->GetWorld(), Mesh->GetBounds().TransformBy(InstanceTransform).GetBox(), Instanced, Impulse);
			if (UPGDebrisSubsystem* Debris = bClient ? nullptr : UPGDebrisSubsystem::Get(Instanced->GetWorld()))
				Debris->QueueSupportCheck(Mesh->GetBounds().TransformBy(InstanceTransform).GetBox());
			return EPGKnockOutcome::Knocked;
		}
		return EPGKnockOutcome::NotMine;
	}

	// 3) 통째로 부서지는 액터(문짝 등, ShatterWholeTag).
	EPGKnockOutcome KnockShatterWholeActor(FPGKnockContext& Ctx)
	{
		UPrimitiveComponent* Component = Ctx.Component;
		const FVector& Impulse = Ctx.Impulse;
		AActor* Instigator = Ctx.Instigator;
		const float MassKg = Ctx.MassKg;
		const bool bFallOnly = Ctx.bFallOnly;
		const bool bClient = Ctx.bClient;
		// 2) 통째로 부서지는 액터(ShatterWholeTag — 문짝 등): 시설 로딩 때 벽 메시가 여닫는 문 액터로 바뀌어서 아래 스태틱 메시 액터 경로에 안 걸렸다.
		//    로봇·탱크·차가 박으면 문짝 메시(양문이면 둘 다)를 잔해로 날리고 문 액터는 없앤다. 잠긴 문도 부서진다(중장비로 뚫는 맛).
		UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component);
		// 문 클래스를 직접 알지 않고 태그로 판단한다(PGPhysicsUtil.h ShatterWholeTag 주석).
		AActor* Door = IsValid(StaticMesh) ? StaticMesh->GetOwner() : nullptr;
		if (IsValid(Door) && Door->ActorHasTag(ShatterWholeTag))
		{
			TInlineComponentArray<UStaticMeshComponent*> Leaves(Door);
			bool bSpawnedAny = false;
			for (UStaticMeshComponent* Leaf : Leaves)
			{
				if (!IsValid(Leaf) || !IsValid(Leaf->GetStaticMesh()) || !Leaf->IsVisible())
					continue;
				if (SpawnKnockProxy(Door->GetWorld(), Leaf->GetStaticMesh(), Leaf->GetComponentTransform(), Impulse, MassKg + Leaf->Bounds.SphereRadius * 0.6f, Instigator, Leaf, bFallOnly))
				{
					bSpawnedAny = true;
					APGKnockRelay::Send(Door->GetWorld(), Leaf->GetStaticMesh(), Leaf->GetComponentTransform(), Impulse, MassKg + Leaf->Bounds.SphereRadius * 0.6f, bFallOnly);
					if (bClient)
						NotePredictedKnock(Door->GetWorld(), Leaf->GetStaticMesh(), Leaf->GetComponentLocation());
				}
			}
			// 이번 프레임 잔해 예산이 없어 하나도 못 만들었으면 문은 그대로 두고 다음 스윕에서 다시 시도한다.
			if (!bSpawnedAny)
				return EPGKnockOutcome::Refused;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_DestroyDoor);
				if (!bClient || !Door->GetIsReplicated())
				{
					Door->Destroy();
				}
				else
				{
					// 복제되는 문은 서버가 지운다 — 그동안 이 화면에서는 숨기고 통과시킨다.
					Door->SetActorHiddenInGame(true);
					Door->SetActorEnableCollision(false);
				}
			}
			return EPGKnockOutcome::Knocked;
		}
		return EPGKnockOutcome::NotMine;
	}

	// 4) 상자: 부수지 않고 통째로 튕겨 보낸다.
	EPGKnockOutcome KnockContainer(FPGKnockContext& Ctx)
	{
		UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Ctx.Component);
		AActor* Owner = IsValid(StaticMesh) ? StaticMesh->GetOwner() : nullptr;
		if (!IsValid(Owner) || !IsValid(StaticMesh->GetStaticMesh()))
			return EPGKnockOutcome::NotMine; // 아래 레벨 메시 처리기가 같은 검사로 거절한다
		const FVector& Impulse = Ctx.Impulse;
		AActor* Instigator = Ctx.Instigator;
		// 상자는 부수지 않고 통째로 튕겨 보낸다(루팅은 떨어진 자리에서 그대로, 9/23). 사람이 걸어서 민 것(캐릭터)은 빼고 — 탈것·로봇만.
		if (AItemContainerActor* Container = Cast<AItemContainerActor>(Owner))
		{
			if (Cast<ACharacter>(Instigator) && !Instigator->IsA<APGRobotCharacter>())
				return EPGKnockOutcome::Refused;
			Container->KnockLoose(Impulse);
			return EPGKnockOutcome::Knocked;
		}
		return EPGKnockOutcome::NotMine;
	}

	// 5) 레벨에 놓인 스태틱 메시(액터째 또는 블루프린트 부품).
	EPGKnockOutcome KnockLevelMesh(FPGKnockContext& Ctx)
	{
		const FVector& Impulse = Ctx.Impulse;
		AActor* Instigator = Ctx.Instigator;
		const float MassKg = Ctx.MassKg;
		const float LowHeight = Ctx.LowHeight;
		const bool bFallOnly = Ctx.bFallOnly;
		const bool bClient = Ctx.bClient;
		auto TooBig = [&Ctx](const FBoxSphereBounds& Bounds) { return Ctx.TooBig(Bounds); };
		UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Ctx.Component);
		// 3) 레벨에 놓인 스태틱 메시: 원본을 없애고 대리 액터로.
		//    AStaticMeshActor 면 액터째 없앤다. 블루프린트 액터(공장 선반·드럼통 묶음 등)면 부딪힌 부품 컴포넌트만 숨기고 충돌을 끈다.
		//    예전에는 AStaticMeshActor 만 받아서, 공장 안 선반·통 묶음(블루프린트)이 벽처럼 서서 17m 로봇을 막았다(9/19).
		AActor* Owner = IsValid(StaticMesh) ? StaticMesh->GetOwner() : nullptr;
		if (!IsValid(Owner) || !IsValid(StaticMesh->GetStaticMesh()))
			return EPGKnockOutcome::Refused;
		const bool bWholeActor = Owner->IsA<AStaticMeshActor>();
		// 부품만 떼는 대상에서 빼는 것: 폰(캐릭터·차), 상호작용 오브젝트(상자·장치 — 루팅·퀘스트가 걸려 있다), 맵 생성기(지형·묶음은 위 HISM 경로),
		// 이미 대리 액터인 것(루트가 KnockBox·CollapseVisual).
		if (!bWholeActor)
		{
			if (Owner->IsA<APawn>() || Owner->Implements<UInteractable>() || Owner->Implements<UPGMapInfo>() /* 맵 자신(9/26: 클래스 대신 '맵 정보' 약속으로 판단) */)
				return EPGKnockOutcome::Refused;
			const FName RootName = Owner->GetRootComponent() ? Owner->GetRootComponent()->GetFName() : NAME_None;
			if (RootName == TEXT("KnockBox") || RootName == TEXT("CollapseVisual") || RootName == TEXT("DebrisBox"))
				return EPGKnockOutcome::Refused;
		}
		if (StaticMesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			return EPGKnockOutcome::Refused;
		if (!(LowHeight > 0.0f ? !NameHasAny(StaticMesh->GetStaticMesh()->GetName(), GiantSkipWords, UE_ARRAY_COUNT(GiantSkipWords)) : IsKnockableMesh(StaticMesh->GetStaticMesh(), StaticMesh->GetComponentScale())))
			return EPGKnockOutcome::Refused;
		if (TooBig(StaticMesh->Bounds) && !IsTreeMesh(StaticMesh->GetStaticMesh(), StaticMesh->GetComponentScale()))
			return EPGKnockOutcome::Refused;
		if (!SpawnKnockProxy(Owner->GetWorld(), StaticMesh->GetStaticMesh(), StaticMesh->GetComponentTransform(), Impulse, MassKg + StaticMesh->Bounds.SphereRadius * 0.6f, Instigator, StaticMesh, bFallOnly))
			return EPGKnockOutcome::Refused;
		APGKnockRelay::Send(Owner->GetWorld(), StaticMesh->GetStaticMesh(), StaticMesh->GetComponentTransform(), Impulse, MassKg + StaticMesh->Bounds.SphereRadius * 0.6f, bFallOnly);
		if (bClient)
			NotePredictedKnock(Owner->GetWorld(), StaticMesh->GetStaticMesh(), StaticMesh->GetComponentLocation());
		const FBox SupportBox = StaticMesh->Bounds.GetBox();
		UWorld* SupportWorld = Owner->GetWorld();
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_DestroySource);
			if (bWholeActor && !(bClient && Owner->GetIsReplicated()))
			{
				Owner->Destroy();
			}
			else
			{
				StaticMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				StaticMesh->SetVisibility(false);
			}
		}
		// 원본이 사라진 뒤라 바닥 찾기에 원본이 걸리지 않는다(액터째 없앴으면 곧 사라질 포인터라 넘기지 않는다).
		if (!bClient)
			DropSupportedItems(SupportWorld, SupportBox, bWholeActor ? nullptr : StaticMesh, Impulse);
		// 이 조각 위에 얹혀 있던 것(2층 바닥·지붕·위층 벽)이 받침을 잃었는지 다음 프레임들에 나눠 본다(서버만 — 클라는 알림으로 받는다).
		if (UPGDebrisSubsystem* Debris = bClient ? nullptr : UPGDebrisSubsystem::Get(SupportWorld))
			Debris->QueueSupportCheck(SupportBox);
		return EPGKnockOutcome::Knocked;
	}

	// 물어보는 순서가 곧 우선순위다(잔해를 먼저 봐야 잔해 상자를 레벨 메시로 착각하지 않는다).
	using FPGKnockHandler = EPGKnockOutcome (*)(FPGKnockContext&);
	const FPGKnockHandler KnockHandlers[] = { &KnockExistingDebris, &KnockInstance, &KnockShatterWholeActor, &KnockContainer, &KnockLevelMesh };
}
}


bool PGPhysicsUtil::TryKnockProp(UPrimitiveComponent* Component, const FHitResult& Hit, const FVector& Impulse, AActor* Instigator, float MaxRadius, float MassKg, float LowHeight, bool bFallOnly)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_TryKnockProp);
	// 크기 한도: 반지름이 MaxRadius 이하이거나, (LowHeight 를 줬으면) 높이가 LowHeight 이하인 넓고 낮은 것.
	// 넓이만 봤을 때는 20m 짜리 판때기 지붕 건물을 8배 보스도 못 부숴서 매번 그 위로 올라가 걸었다.
	// 단, 이 예외는 "발밑 바닥"에는 쓰지 않는다: 윗면이 부딪힌 쪽 몸 아래 끝 + 60cm 보다 낮으면 밟고 선 바닥이다
	// (시설 바닥 슬래브·주차장 판처럼 지형 태그가 없는 바닥). 몸 아래 끝은 충돌 바운드로 잰다 — 탱크는 캡슐이 없어서
	// 액터 중심 기준으로 쟀더니 2m 아래 벽 판자가 전부 "바닥"이 돼 탱크가 밀지 못하고 끼었다(9/19).
	float FeetZ = -TNumericLimits<float>::Max();
	if (LowHeight > 0.0f && IsValid(Instigator))
	{
		FVector Origin, Extent;
		Instigator->GetActorBounds(true, Origin, Extent);
		FeetZ = Origin.Z - Extent.Z;
	}
	if (!IsValid(Component))
		return false;
	// 멀티(9/27): 부수기는 서버가 정하고 APGKnockRelay 로 모두에게 알린다.
	// 멀티(9/28): 단, 이 화면 사람이 직접 모는 것(차·로봇)이 부딪힌 소품은 먼저 부순다 — 기다리면 그동안 소품에 막혀 멈칫하고,
	//   서버 위치로 끌려가며 툭 튀었다. 먼저 부순 것은 적어 두고(NotePredictedKnock) 서버 알림이 오면 건너뛴다.
	//   남이 모는 것·몬스터·드래곤이 친 것은 여전히 서버 알림만 따른다.
	const bool bClient = Component->GetWorld() && Component->GetWorld()->GetNetMode() == NM_Client;
	if (bClient)
	{
		const APawn* Driver = Cast<APawn>(Instigator);
		if (!Driver || !Driver->IsLocallyControlled())
			return false;
	}
	if (Component->ComponentHasTag(TerrainTag))
		return false;
	// 보호 대상(상점·탈출구·퀘스트): 컴포넌트든 그 액터든 표시가 있으면 부수지 않는다.
	if (Component->ComponentHasTag(ProtectedTag) || (Component->GetOwner() && Component->GetOwner()->ActorHasTag(ProtectedTag)))
		return false;

	// 공통 검사를 지났다 — 종류별 처리기에 차례로 묻는다(위 KnockHandlers).
	FPGKnockContext Ctx{ Component, Hit, Impulse, Instigator, MaxRadius, MassKg, LowHeight, FeetZ, bFallOnly, bClient };
	for (const FPGKnockHandler Handler : KnockHandlers)
	{
		const EPGKnockOutcome Outcome = Handler(Ctx);
		if (Outcome != EPGKnockOutcome::NotMine)
			return Outcome == EPGKnockOutcome::Knocked;
	}
	return false;
}

namespace
{
	struct FPGPredictedKnock
	{
		TWeakObjectPtr<UWorld> World;
		const UStaticMesh* Mesh = nullptr;
		FVector Pivot = FVector::ZeroVector;
		double At = 0.0;
	};
	TArray<FPGPredictedKnock>& PredictedKnocks()
	{
		static TArray<FPGPredictedKnock> List;
		return List;
	}
}

void PGPhysicsUtil::NotePredictedKnock(UWorld* World, const UStaticMesh* Mesh, const FVector& Pivot)
{
	if (!IsValid(World))
		return;
	TArray<FPGPredictedKnock>& List = PredictedKnocks();
	const double Now = World->GetTimeSeconds();
	// 10초 넘은 것(서버가 안 부순 것 — 규칙 차이·예산)은 잊는다.
	List.RemoveAll([Now](const FPGPredictedKnock& Entry) { return !Entry.World.IsValid() || Now - Entry.At > 10.0; });
	List.Add({ World, Mesh, Pivot, Now });
	++PredictedKnockCount;
	if (PredictedKnockCount <= 5)
		UE_LOG(LogTemp, Display, TEXT("KnockPredict: %d %s at %s"), PredictedKnockCount, *GetNameSafe(Mesh), *Pivot.ToCompactString());
}

// 멀티(9/27) 클라이언트: 서버가 날린 소품 하나를 이 화면에서 따라 한다.
// 찾는 법: 서버가 보낸 자리(원본 메시의 월드 트랜스폼) 둘레를 겹침 검사로 훑어, 같은 메시이고 원점이 10cm 안인 것을 고른다.
//   - 타일 묶음(인스턴스 메시): 그 인스턴스 하나만 지운다(겹침 결과의 ItemIndex 가 인스턴스 번호다).
//   - 레벨 메시·블루프린트 부품: 숨기고 충돌을 끈다(서버처럼 액터를 지우지 않는다 — 복제되는 액터일 수 있다).
// 그 뒤 서버와 같은 함수로 잔해를 날린다(보기용). 원본이 이미 없으면(서버가 지운 문 액터 등) 잔해만 날린다.
void PGPhysicsUtil::ApplyRemoteKnock(UWorld* World, UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse, float MassKg, bool bFallOnly)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PGKnock_ApplyRemote);
	if (!IsValid(World) || !IsValid(Mesh))
		return;
	// 이 화면에서 먼저 부순 것이면 건너뛴다(9/28, NotePredictedKnock).
	{
		TArray<FPGPredictedKnock>& List = PredictedKnocks();
		const FVector Pivot = WorldTransform.GetLocation();
		const int32 Found = List.IndexOfByPredicate([World, Mesh, &Pivot](const FPGPredictedKnock& Entry)
		{
			return Entry.World.Get() == World && Entry.Mesh == Mesh && FVector::DistSquared(Entry.Pivot, Pivot) < FMath::Square(20.0f);
		});
		if (Found != INDEX_NONE)
		{
			List.RemoveAtSwap(Found);
			++PredictedKnockConfirmed;
			if (PredictedKnockConfirmed <= 5 || PredictedKnockConfirmed % 20 == 0)
				UE_LOG(LogTemp, Display, TEXT("KnockRemote: server confirmed a knock this screen already did (%d of %d predicted)"), PredictedKnockConfirmed, PredictedKnockCount);
			return;
		}
	}
	const double FindStart = FPlatformTime::Seconds();
	const FBoxSphereBounds MeshBounds = Mesh->GetBounds();
	const FVector Centre = WorldTransform.TransformPosition(MeshBounds.Origin);
	const float Radius = FMath::Max(MeshBounds.SphereRadius * WorldTransform.GetScale3D().GetAbsMax(), 20.0f);
	const FVector Pivot = WorldTransform.GetLocation();
	constexpr float MatchCm = 10.0f;

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects(FCollisionObjectQueryParams::AllStaticObjects);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGKnockRemote), false);
	World->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, Objects, FCollisionShape::MakeSphere(Radius), Params);

	UPrimitiveComponent* Found = nullptr;
	int32 FoundItem = INDEX_NONE;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		UPrimitiveComponent* Component = Overlap.GetComponent();
		if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
		{
			if (Instanced->GetStaticMesh() != Mesh || !Instanced->IsValidInstance(Overlap.ItemIndex))
				continue;
			FTransform Instance;
			if (Instanced->GetInstanceTransform(Overlap.ItemIndex, Instance, true) && FVector::Dist(Instance.GetLocation(), Pivot) <= MatchCm)
			{
				Found = Instanced;
				FoundItem = Overlap.ItemIndex;
				break;
			}
		}
		else if (UStaticMeshComponent* StaticMesh = Cast<UStaticMeshComponent>(Component))
		{
			if (StaticMesh->GetStaticMesh() == Mesh && StaticMesh->IsVisible() && FVector::Dist(StaticMesh->GetComponentLocation(), Pivot) <= MatchCm)
			{
				Found = StaticMesh;
				break;
			}
		}
	}

	const double LaunchStart = FPlatformTime::Seconds();
	SpawnKnockProxy(World, Mesh, WorldTransform, Impulse, MassKg, nullptr, Found, bFallOnly);
	const double RemoveStart = FPlatformTime::Seconds();
	if (UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Found); IsValid(Instanced) && FoundItem != INDEX_NONE)
	{
		Instanced->RemoveInstance(FoundItem);
	}
	else if (IsValid(Found))
	{
		Found->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Found->SetVisibility(false);
	}
	const double RemoveEnd = FPlatformTime::Seconds();
	RemoteFindMs += (LaunchStart - FindStart) * 1000.0;
	RemoteLaunchMs += (RemoveStart - LaunchStart) * 1000.0;
	RemoteRemoveMs += (RemoveEnd - RemoveStart) * 1000.0;
	// 시험 확인용 개수. 처음 몇 개와 그 뒤 20개마다 한 줄(공장이 무너지면 수백 개라 다 찍으면 로그가 덮인다).
	++PGPhysicsUtil::RemoteKnockCount;
	if (IsValid(Found))
		++PGPhysicsUtil::RemoteKnockMatched;
	if (PGPhysicsUtil::RemoteKnockCount <= 5 || PGPhysicsUtil::RemoteKnockCount % 20 == 0)
		UE_LOG(LogTemp, Display, TEXT("KnockRemote: %d received from the server, %d matched on this screen (last %s -> %s)"),
			PGPhysicsUtil::RemoteKnockCount, PGPhysicsUtil::RemoteKnockMatched, *Mesh->GetName(), Found ? *Found->GetName() : TEXT("already gone"));
}

