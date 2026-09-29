#include "Common/PGDebrisSubsystem.h"

#include "Common/PGEffectSet.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Common/PGPhysicsUtil.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/Pawn.h"

namespace
{
	// 동시에 "진짜 물리"로 도는 잔해 수. 넘치면 새 잔해는 가벼운 흉내로 돈다.
	// 차로 소품 몇 개 칠 때는 전부 진짜 물리(자연스럽게 서로·땅·벽과 부딪힘), 로봇이 공장을 통째로 밀 때만 흉내로 넘어간다.
	// 흙먼지 간격(초). 9/28 중앙 건물 부수기 측정용으로 뺐다 — 크게 주면 먼지가 거의 안 난다.
	TAutoConsoleVariable<float> CVarDebrisDustInterval(TEXT("PG.Debris.DustInterval"), 0.12f, TEXT("Minimum seconds between debris dust bursts."));
	TAutoConsoleVariable<int32> CVarDebrisMaxPhysics(TEXT("PG.Debris.MaxPhysics"), 12, TEXT("Max debris pieces simulated by Chaos at once; the rest use lightweight motion."));
	// 진짜 물리 잔해끼리 부딪힐지. 예전엔 120 개가 겹친 채 태어나 겹침 풀기가 폭발해서 껐는데, 이제 동시에 MaxPhysics(12)개뿐이라 켠다.
	// 사용자 피드백(9/19): 한 번 부딪히고 서로 반응이 없으니 어색하다. 끊기면 0 으로.
	TAutoConsoleVariable<int32> CVarDebrisSelfCollide(TEXT("PG.Debris.SelfCollide"), 1, TEXT("1 = Chaos-simulated debris pieces collide with each other."));
	// 진짜 물리 잔해를 이만큼 지나면(또는 멈추면) 물리를 끄고 가벼운 쪽(누워 있기)으로 넘긴다. 다시 치면 깨어난다.
	TAutoConsoleVariable<float> CVarDebrisPhysicsSeconds(TEXT("PG.Debris.PhysicsSeconds"), 6.0f, TEXT("Max seconds a debris piece stays on Chaos before it is frozen into lightweight rest."));

	constexpr float Gravity = 980.0f;
	constexpr float HiddenZ = -100000.0f;
	constexpr int32 MaxTracesPerFrame = 64;
	// 바닥을 못 찾았다는 표시. 예전엔 5000cm 아래를 바닥으로 삼아 잔해가 땅속으로 꺼져 내려갔다.
	constexpr float NoGroundZ = -1.0e7f;

	// PGPhysicsUtil 쪽 콘솔 변수(PG.Knock.*)를 같이 쓴다. 한 곳에서만 정의하려고 이름으로 찾아 읽는다.
	// 이름 찾기는 처음 한 번만(문자열 해시 조회가 조각마다 매 프레임 돌지 않게). 부르는 쪽은 늘 같은 문자열 상수를 넘긴다.
	float KnockCVar(const TCHAR* Name, float Fallback)
	{
		// 문자열 내용이 아니라 상수의 주소로 찾는다(엔진이 TCHAR 배열 해시를 더는 권하지 않는다).
		static TMap<const void*, IConsoleVariable*> Found;
		IConsoleVariable*& Var = Found.FindOrAdd(static_cast<const void*>(Name));
		if (!Var)
			Var = IConsoleManager::Get().FindConsoleVariable(Name);
		return Var ? Var->GetFloat() : Fallback;
	}

	// 바닥·벽으로 치지 않는 것: 탈것을 막지 않는 충돌(길찾기용 보이지 않는 바닥판 NavigationFloor 등).
	// 이걸 바닥으로 잡으면 움푹 파인 지형 위에 잔해가 떠 있다(차가 떠서 달리던 것과 같은 원인).
	bool IsSolidForDebris(const UPrimitiveComponent* Component)
	{
		if (!IsValid(Component))
			return false;
		// 사람이나 차가 서는 것이면 바닥·벽이다. 단 "안 보이면서 차는 통과"시키는 판(길찾기 바닥판)은 뺀다.
		// 차 채널만 봤을 때는 차를 막지 않는 시설 바닥을 못 찾아 잔해가 5000cm 아래로 떨어졌다 = "땅속으로 꺼짐"(9/20 PIE).
		const bool bBlocksVehicle = Component->GetCollisionResponseToChannel(ECC_Vehicle) == ECR_Block;
		const bool bBlocksPawn = Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
		return bBlocksVehicle || (bBlocksPawn && Component->IsVisible());
	}

	// 가장 "눕기 좋은" 축: 지금 가장 위·아래를 향한 축인데, 그 면이 넓을수록 가산점. 좁은 끝면으로 서 있는 판자는 넓은 면으로 눕는다.
	FVector PickRestAxis(const FQuat& Rot, const FVector& Half, float& OutSign)
	{
		const FVector Axes[3] = { Rot.GetAxisX(), Rot.GetAxisY(), Rot.GetAxisZ() };
		const float FaceArea[3] = { Half.Y * Half.Z, Half.X * Half.Z, Half.X * Half.Y };
		int32 Best = 2;
		float BestScore = -1.0f;
		for (int32 I = 0; I < 3; ++I)
		{
			const float Score = FMath::Abs(Axes[I].Z) * FMath::Sqrt(FaceArea[I]);
			if (Score > BestScore)
			{
				BestScore = Score;
				Best = I;
			}
		}
		OutSign = Axes[Best].Z >= 0.0f ? 1.0f : -1.0f;
		return Axes[Best];
	}

	// 회전된 상자에서 중심부터 가장 낮은 모서리까지의 높이(꼭짓점 8개를 안 돌고 닫힌 식으로).
	float LowestReach(const FQuat& Rot, const FVector& Half)
	{
		return FMath::Abs(Rot.GetAxisX().Z) * Half.X + FMath::Abs(Rot.GetAxisY().Z) * Half.Y + FMath::Abs(Rot.GetAxisZ().Z) * Half.Z;
	}
}



UPGDebrisSubsystem* UPGDebrisSubsystem::Get(const UWorld* World)
{
	return IsValid(World) ? World->GetSubsystem<UPGDebrisSubsystem>() : nullptr;
}

bool UPGDebrisSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPGDebrisSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPGDebrisSubsystem, STATGROUP_Tickables);
}

void UPGDebrisSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	TRACE_CPUPROFILER_EVENT_SCOPE(PGDebris_Prewarm);

	// 미리 만들기: 판 시작은 검은 로딩 화면 뒤라 여기서 드는 비용은 안 보인다. 부술 때는 메시만 갈아 끼운다.
	const int32 PoolSize = FMath::Clamp(static_cast<int32>(KnockCVar(TEXT("PG.Knock.MaxAlive"), 40.0f)), 8, 128);
	Pieces.Reserve(PoolSize);
	for (int32 Index = 0; Index < PoolSize; ++Index)
	{
		FPiece Piece;
		if (CreatePooledActor(Piece))
			Pieces.Add(Piece);
	}

	// 먼지: 큰 조각이 넘어질 때 한 번. 팩(ParagonRampage)이 없는 PC 에서는 그냥 먼지 없이 돈다.
	// 먼지 에셋은 이펙트 묶음(DA_PGEffects, 없으면 원래 에셋)에서(9/23 블루프린트 분리).
	{
		const UPGEffectSet* Effects = UPGEffectSet::GetActive();
		DustEffect = (Effects && !Effects->DebrisDustEffect.IsNull()) ? Effects->DebrisDustEffect.LoadSynchronous() : nullptr;
	}
	if (DustEffect)
	{
		// 셰이더·파티클 자원을 로딩 중에 한 번 만들어 둔다(처음 부술 때만 끊기는 "첫 사용" 비용을 로딩으로 옮김).
		UGameplayStatics::SpawnEmitterAtLocation(&InWorld, DustEffect, FVector(0.0f, 0.0f, HiddenZ), FRotator::ZeroRotator,
			FVector(0.01f), true, EPSCPoolMethod::AutoRelease);
	}
	UE_LOG(LogTemp, Display, TEXT("PGDebris: pool=%d dust=%s"), Pieces.Num(), DustEffect ? TEXT("yes") : TEXT("no"));
}

void UPGDebrisSubsystem::Deinitialize()
{
	Pieces.Reset();
	PoolActors.Reset();
	Super::Deinitialize();
}

bool UPGDebrisSubsystem::CreatePooledActor(FPiece& Piece)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return false;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(0.0f, 0.0f, HiddenZ)), Params);
	if (!IsValid(Actor))
		return false;

	UBoxComponent* Box = NewObject<UBoxComponent>(Actor, TEXT("DebrisBox"));
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetGenerateOverlapEvents(false);
	// 잔해는 길찾기 지도에 안 넣는다(움직일 때마다 내비 타일을 다시 구웠던 원인). 발판도 아니다(보스가 잔해를 밟고 하늘로 갔다).
	Box->SetCanEverAffectNavigation(false);
	Box->CanCharacterStepUpOn = ECB_No;
	Box->SetLinearDamping(0.3f);
	Box->SetAngularDamping(0.5f);
	Actor->SetRootComponent(Box);
	Box->RegisterComponent();

	UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(Actor, TEXT("DebrisVisual"));
	Visual->SetMobility(EComponentMobility::Movable);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetCanEverAffectNavigation(false);
	// 잔해는 Lumen(간접광·거리장)에서 뺀다 — 날아가는 메시가 매 프레임 거리장을 다시 갱신하게 만들었다(GPU 측정).
	Visual->SetAffectDistanceFieldLighting(false);
	Visual->SetAffectDynamicIndirectLighting(false);
	Visual->SetupAttachment(Box);
	Visual->RegisterComponent();

	Actor->SetActorHiddenInGame(true);
	Actor->SetActorLocation(FVector(0.0f, 0.0f, HiddenZ));
	PoolActors.Add(Actor);
	Piece.Actor = Actor;
	Piece.Box = Box;
	Piece.Visual = Visual;
	return true;
}

int32 UPGDebrisSubsystem::FindPiece(const UPrimitiveComponent* Component) const
{
	if (!IsValid(Component) || Component->GetFName() != TEXT("DebrisBox"))
		return INDEX_NONE;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
		if (Pieces[Index].Box.Get() == Component)
			return Index;
	return INDEX_NONE;
}

bool UPGDebrisSubsystem::IsDebris(const UPrimitiveComponent* Component) const
{
	return FindPiece(Component) != INDEX_NONE;
}

int32 UPGDebrisSubsystem::CountSimulating() const
{
	int32 Count = 0;
	for (const FPiece& Piece : Pieces)
		if (Piece.bActive && Piece.Box.IsValid() && Piece.Box->IsSimulatingPhysics())
			++Count;
	return Count;
}

int32 UPGDebrisSubsystem::AcquirePiece()
{
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
		if (!Pieces[Index].bActive && Pieces[Index].Box.IsValid())
			return Index;

	// 다 쓰는 중이면: 화면에 안 보이는 것 중 가장 오래된 것 → 없으면 그냥 가장 오래된 것(눈앞에서 사라지는 건 최후의 수단).
	int32 Oldest = INDEX_NONE;
	int32 OldestHidden = INDEX_NONE;
	for (int32 Index = 0; Index < Pieces.Num(); ++Index)
	{
		const FPiece& Piece = Pieces[Index];
		if (!Piece.Box.IsValid())
			continue;
		if (Oldest == INDEX_NONE || Piece.StartTime < Pieces[Oldest].StartTime)
			Oldest = Index;
		const bool bSeen = Piece.Visual.IsValid() && Piece.Visual->WasRecentlyRendered(0.3f);
		if (!bSeen && (OldestHidden == INDEX_NONE || Piece.StartTime < Pieces[OldestHidden].StartTime))
			OldestHidden = Index;
	}
	const int32 Victim = OldestHidden != INDEX_NONE ? OldestHidden : Oldest;
	if (Victim != INDEX_NONE)
		Deactivate(Pieces[Victim]);
	return Victim;
}

void UPGDebrisSubsystem::Deactivate(FPiece& Piece)
{
	Piece.bActive = false;
	UBoxComponent* Box = Piece.Box.Get();
	if (IsValid(Box))
	{
		Box->SetSimulatePhysics(false);
		Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UStaticMeshComponent* Visual = Piece.Visual.Get())
		Visual->SetStaticMesh(nullptr); // 렌더 자원을 놓는다. 다음에 쓸 때 새 메시를 끼운다.
	if (AActor* Actor = Piece.Actor.Get())
	{
		Actor->SetActorHiddenInGame(true);
		Actor->SetActorLocation(FVector(0.0f, 0.0f, HiddenZ), false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UPGDebrisSubsystem::StartPhysics(FPiece& Piece, const FVector& Velocity, const FVector& AngularVelocity)
{
	UBoxComponent* Box = Piece.Box.Get();
	if (!IsValid(Box))
		return;
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_PhysicsBody);
	Box->SetCollisionResponseToAllChannels(ECR_Block);
	// 총알·F 트레이스·카메라에는 안 걸린다.
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 캐릭터를 막지 않는다: 날아가는 상자가 캡슐을 파고들면 "겹침 해소"로 몬스터가 잔해와 같이 날아갔다.
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	// 잔해끼리: 예전엔 120 개가 겹친 채 태어나 겹침 풀기 계산이 폭발했다(MaxAlive 120→10 에 Game 81→13ms).
	// 지금은 진짜 물리가 동시에 MaxPhysics 개뿐이라 서로 부딪히게 둔다(PG.Debris.SelfCollide 0 이면 예전처럼 무시).
	Box->SetCollisionResponseToChannel(ECC_PhysicsBody, CVarDebrisSelfCollide.GetValueOnGameThread() != 0 ? ECR_Block : ECR_Ignore);
	const float Radius = Piece.Half.Size();
	const float SizeMass = 20.0f + Radius * Radius * 0.004f; // 부피에 비례: 돌 60kg, 벽 판자 400kg
	Box->SetMassOverrideInKg(NAME_None, FMath::Max(Piece.MassKg, SizeMass) * (Piece.bHeavy ? 2.0f : 1.0f), true);
	ApplyTransform(Piece);
	Box->SetSimulatePhysics(true);
	Box->SetPhysicsLinearVelocity(Velocity);
	Box->SetPhysicsAngularVelocityInRadians(AngularVelocity);
	Piece.SettleTimer = 0.0f;
	Piece.bResting = false;
}

void UPGDebrisSubsystem::StopPhysics(FPiece& Piece)
{
	UBoxComponent* Box = Piece.Box.Get();
	if (!IsValid(Box))
		return;
	if (Box->IsSimulatingPhysics())
	{
		Piece.Pos = Box->GetComponentLocation();
		Piece.Rot = Box->GetComponentQuat();
		Box->SetSimulatePhysics(false);
	}
	// 가벼운 상태: 물리로는 아무것도 안 막고, 차·몬스터의 물체 종류 스윕(WorldDynamic)에만 잡힌다 → 다시 치면 Kick.
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldDynamic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Piece.GroundSampleXY = FVector2D(1.0e9f); // 물리로 움직였으니 다음에 움직일 때 바닥을 새로 찾는다
}

bool UPGDebrisSubsystem::TraceFirst(const FPiece& Piece, const FVector& From, const FVector& To, FHitResult& OutHit)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return false;
	++TracesThisFrame;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDebrisTrace), false);
	for (const TObjectPtr<AActor>& PoolActor : PoolActors)
		Params.AddIgnoredActor(PoolActor.Get()); // 잔해끼리는 바닥이 되지 않는다(아래 조각을 거두면 위 조각이 공중에 뜬다)
	if (const UPrimitiveComponent* Ignore = Piece.IgnoreForGround.Get())
		Params.AddIgnoredComponent(Ignore);
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, From, To, Objects, Params);
	for (const FHitResult& Hit : Hits)
	{
		if (!IsSolidForDebris(Hit.GetComponent()) || Hit.GetActor() == nullptr || Hit.GetActor()->IsA<APawn>())
			continue;
		OutHit = Hit;
		return true;
	}
	return false;
}

float UPGDebrisSubsystem::TraceGround(const FPiece& Piece, const FVector& From)
{
	FHitResult Hit;
	if (TraceFirst(Piece, From, From - FVector(0.0f, 0.0f, 50000.0f), Hit))
		return Hit.ImpactPoint.Z;
	return NoGroundZ; // 발밑에 아무것도 없다 → 부르는 쪽이 떨어뜨리지 않고 그 자리에서 사라지게 한다
}

AActor* UPGDebrisSubsystem::Launch(UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse, EPGDebrisMotion Motion,
	float MassKg, bool bHeavy, const UPrimitiveComponent* Source, AActor* Instigator, bool bLightweightOnly)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(PGDebris_Launch);
	UWorld* World = GetWorld();
	if (!IsValid(World) || !IsValid(Mesh))
		return nullptr;
	const int32 Index = AcquirePiece();
	if (Index == INDEX_NONE)
		return nullptr;
	FPiece& Piece = Pieces[Index];
	UBoxComponent* Box = Piece.Box.Get();
	UStaticMeshComponent* Visual = Piece.Visual.Get();
	AActor* Actor = Piece.Actor.Get();
	if (!IsValid(Box) || !IsValid(Visual) || !IsValid(Actor))
		return nullptr;

	const FBox LocalBox = Mesh->GetBoundingBox();
	const FVector Scale = WorldTransform.GetScale3D();
	const FVector LocalCenter = LocalBox.GetCenter();

	Piece.bActive = true;
	Piece.bResting = false;
	Piece.bTipping = false;
	Piece.bVanishing = false;
	Piece.bDusted = false;
	Piece.bHeavy = bHeavy;
	Piece.MassKg = MassKg;
	Piece.Half = (LocalBox.GetExtent() * Scale.GetAbs()).ComponentMax(FVector(3.0f));
	Piece.Pos = WorldTransform.TransformPosition(LocalCenter);
	Piece.Rot = WorldTransform.GetRotation();
	Piece.Vel = FVector::ZeroVector;
	Piece.AngVel = FVector::ZeroVector;
	Piece.StartTime = World->GetTimeSeconds();
	Piece.LastKickTime = Piece.StartTime;
	Piece.SettleTimer = 0.0f;
	Piece.IgnoreForGround = Source;
	Piece.VisualScale = Scale;
	Piece.VisualOffset = -LocalCenter * Scale;

	// 겉모습: 메시 + 원본의 재질(레벨에서 재질을 바꿔 놓은 소품이 기본 재질로 돌아가 보이지 않게).
	Visual->EmptyOverrideMaterials();
	Visual->SetStaticMesh(Mesh);
	if (const UMeshComponent* SourceMesh = Cast<UMeshComponent>(Source))
		for (int32 Slot = 0; Slot < Visual->GetNumMaterials(); ++Slot)
			Visual->SetMaterial(Slot, SourceMesh->GetMaterial(Slot));
	SetVanish(Piece, 1.0f);
	// 작은 잔해는 그림자를 안 드리운다: 움직이는 작은 그림자 수십 개가 VSM 을 계속 다시 그리게 한다.
	Visual->SetCastShadow(Piece.Half.GetMax() >= KnockCVar(TEXT("PG.Knock.ShadowMinExtent"), 120.0f));
	Box->SetBoxExtent(Piece.Half * FMath::Clamp(KnockCVar(TEXT("PG.Knock.BoxShrink"), 0.85f), 0.3f, 1.0f));
	Actor->SetActorHiddenInGame(false);

	Piece.GroundZ = TraceGround(Piece, Piece.Pos + FVector(0.0f, 0.0f, 10.0f));
	Piece.GroundSampleXY = FVector2D(Piece.Pos);
	const float Radius = Piece.Half.Size();

	if (Motion == EPGDebrisMotion::Tip)
	{
		// 넘어지기: 밀린 방향의 바닥 모서리가 경첩. 원점을 축으로 돌리면 아래 모서리가 땅을 파고들었다(9/19 PIE 두 번).
		StopPhysics(Piece);
		FVector PushDir = Impulse.GetSafeNormal2D();
		if (PushDir.IsNearlyZero())
			PushDir = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
		const FBox WorldBox = Mesh->GetBoundingBox().TransformBy(WorldTransform);
		const FVector Extent = WorldBox.GetExtent();
		const float HalfDepth = FMath::Abs(Extent.X * PushDir.X) + FMath::Abs(Extent.Y * PushDir.Y);
		const float Height = FMath::Max(Extent.Z * 2.0f, 100.0f);
		Piece.bTipping = true;
		Piece.TiltAxis = FVector::CrossProduct(FVector::UpVector, PushDir).GetSafeNormal(); // 윗부분이 PushDir 쪽으로
		Piece.Hinge = FVector(WorldBox.GetCenter().X + PushDir.X * HalfDepth, WorldBox.GetCenter().Y + PushDir.Y * HalfDepth, WorldBox.Min.Z);
		Piece.TipStartPos = Piece.Pos;
		Piece.TipStartRot = Piece.Rot;
		Piece.TipAngle = 0.0f;
		// 막대가 끝을 축으로 넘어가는 가속 = 1.5 g / 높이. 10m 나무는 느긋하게(약 2초), 3m 판은 빠르게.
		Piece.TipGravity = 1.5f * Gravity / Height;
		Piece.TipSpeed = FMath::Clamp(Impulse.Size2D() / Height * 0.5f, 0.3f, 2.0f);
		ApplyTransform(Piece);
		static int32 TipLogs = 0; // 진단(9/28 "클라에서 나무가 조금 밀렸다가 가라앉는다"): 처음 몇 개만
		if (++TipLogs <= 8)
			UE_LOG(LogTemp, Display, TEXT("Debris tip start (%s): %s height %.0fcm push %.0fcm/s speed %.2f rad/s hinge z %.0f ground z %.0f by %s"),
				World->GetNetMode() == NM_Client ? TEXT("client") : TEXT("server"), *Mesh->GetName(), Height, Impulse.Size2D(), Piece.TipSpeed,
				Piece.Hinge.Z, Piece.GroundZ, *GetNameSafe(Instigator));
		return Actor;
	}

	// 날아가기: 작은 것은 빠르게, 큰 것은 묵직하게(반지름 2m 기준 0.6~1.8배). 돌·콘크리트는 덜 튄다.
	const float VelocityScale = FMath::Clamp(200.0f / FMath::Max(Radius, 1.0f), 0.6f, 1.8f) * (bHeavy ? 0.6f : 1.0f);
	const FVector Velocity = Impulse * VelocityScale;
	// 받침 무너짐으로 떨어지는 조각은 거의 안 돈다(바닥 판이 팽이처럼 돌며 떨어지면 이상하다).
	const FVector Spin = FMath::VRand() * FMath::DegreesToRadians(FMath::FRandRange(180.0f, 540.0f)) * (bLightweightOnly ? 0.08f : 1.0f);
	if (!bLightweightOnly && CountSimulating() < FMath::Max(0, CVarDebrisMaxPhysics.GetValueOnGameThread()))
	{
		StartPhysics(Piece, Velocity, Spin);
	}
	else
	{
		StopPhysics(Piece);
		Piece.Vel = Velocity;
		Piece.AngVel = Spin;
		ApplyTransform(Piece);
	}
	if (Radius > 150.0f)
		PlayDust(Piece.Pos - FVector(0.0f, 0.0f, Piece.Half.Z), Radius);
	return Actor;
}

bool UPGDebrisSubsystem::Kick(const UPrimitiveComponent* Component, const FVector& Impulse, const AActor* Instigator)
{
	const int32 Index = FindPiece(Component);
	if (Index == INDEX_NONE)
		return false;
	FPiece& Piece = Pieces[Index];
	UWorld* World = GetWorld();
	UBoxComponent* Box = Piece.Box.Get();
	if (!Piece.bActive || !IsValid(World) || !IsValid(Box))
		return true;
	// 같은 조각을 매 프레임 치지 않는다: 로봇은 매 Tick 앞을 스윕해서, 발밑 잔해에 프레임마다 위쪽 속도를 더해 로켓처럼 쏘았다.
	const double Now = World->GetTimeSeconds();
	if (Now - Piece.LastKickTime < 0.25)
		return true;
	Piece.LastKickTime = Now;

	const float Radius = Piece.Half.Size();
	FVector Add = Impulse * 0.6f * FMath::Clamp(200.0f / FMath::Max(Radius, 1.0f), 0.4f, 1.5f) * (Piece.bHeavy ? 0.6f : 1.0f);
	if (Instigator && Instigator->IsA<ACharacter>())
		Add.Z = FMath::Min(Add.Z, 250.0f);

	if (Box->IsSimulatingPhysics())
	{
		Box->AddImpulse(Add, NAME_None, true);
		return true;
	}

	// 넘어지는 중인 조각(나무·큰 판)을 또 치면 넘어지기를 멈추지 않고 더 빨리 넘어가게만 한다.
	// 왜(9/28 사용자 PIE): 탱크는 넘어지는 나무를 계속 밀고 지나가며 매 0.25초 다시 친다. 전에는 여기서 넘어지기를 끊고
	//   "밀려 떨어지는 조각" 으로 바꿔서, 나무가 조금 기울다 말고 아래로 가라앉아 보였다(서버·클라 로그 모두 넘어지기 끝이 없었다).
	if (Piece.bTipping)
	{
		const float Height = FMath::Max(Piece.Half.Z * 2.0f, 100.0f);
		Piece.TipSpeed = FMath::Max(Piece.TipSpeed, FMath::Clamp(Impulse.Size2D() / Height * 0.5f, 0.3f, 2.0f));
		return true;
	}
	// 누워 있던 조각을 다시 친다: 작은 것은 여유가 있으면 진짜 물리로 깨우고, 아니면 가벼운 흉내로 밀려난다.
	Piece.bTipping = false;
	Piece.bResting = false;
	Piece.bVanishing = false;
	SetVanish(Piece, 1.0f);
	Piece.StartTime = Now; // 다시 친 잔해는 수명을 새로 센다
	const FVector Spin = FMath::VRand() * 2.0f;
	const bool bSmall = Radius <= KnockCVar(TEXT("PG.Knock.CollapseRadius"), 300.0f);
	if (bSmall && CountSimulating() < FMath::Max(0, CVarDebrisMaxPhysics.GetValueOnGameThread()))
	{
		StartPhysics(Piece, Add, Spin);
	}
	else
	{
		Piece.GroundSampleXY = FVector2D(1.0e9f);
		Piece.Vel = (Piece.Vel + Add).GetClampedToMaxSize(2500.0f);
		Piece.AngVel += bSmall ? Spin : Spin * 0.2f; // 큰 판은 거의 안 돌고 미끄러진다
	}
	return true;
}

void UPGDebrisSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (Pieces.IsEmpty() && PendingSupportChecks.IsEmpty())
		return;
	TRACE_CPUPROFILER_EVENT_SCOPE(PGDebris_Tick);
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return;
	TracesThisFrame = 0;
	ProcessSupportChecks();
	const float Dt = FMath::Min(DeltaTime, 1.0f / 20.0f); // 순간 끊긴 프레임에 조각이 벽을 뚫고 튀지 않게
	const double Now = World->GetTimeSeconds();
	const float Life = FMath::Max(1.0f, KnockCVar(TEXT("PG.Knock.LifeSeconds"), 25.0f));
	const float PhysicsSeconds = FMath::Max(1.0f, CVarDebrisPhysicsSeconds.GetValueOnGameThread());

	for (FPiece& Piece : Pieces)
	{
		if (!Piece.bActive)
			continue;
		UBoxComponent* Box = Piece.Box.Get();
		UStaticMeshComponent* Visual = Piece.Visual.Get();
		if (!IsValid(Box) || !IsValid(Visual))
		{
			Piece.bActive = false;
			continue;
		}
		const float Age = static_cast<float>(Now - Piece.StartTime);

		// 거두기: 수명이 다했으면 화면에 안 보일 때 바로. 계속 보이면 15초 더 기다렸다가 사라진다(가라앉히지 않는다).
		// 사라지기: 바로 없앤다. 예전엔 0.4초 동안 작아지다 사라졌는데, 사용자 판단(9/20)으로 "그냥 사라지는 게 낫다".
		if (Piece.bVanishing)
		{
			Deactivate(Piece);
			continue;
		}
		if (Age > Life && (Piece.bResting || Age > Life * 2.0f))
		{
			if (!Visual->WasRecentlyRendered(0.5f))
			{
				Deactivate(Piece);
				continue;
			}
			if (Age > Life + 15.0f)
				Piece.bVanishing = true;
		}

		if (Box->IsSimulatingPhysics())
		{
			// 진짜 물리: 멈췄거나 오래 돌았으면 그 자리에서 가벼운 "누워 있기"로 넘긴다(물리 비용이 계속 남지 않게).
			if (Box->GetComponentLocation().Z < HiddenZ * 0.2f)
			{
				Deactivate(Piece);
				continue;
			}
			const bool bStill = Box->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(30.0f)
				&& Box->GetPhysicsAngularVelocityInDegrees().SizeSquared() < FMath::Square(45.0f);
			Piece.SettleTimer = bStill ? Piece.SettleTimer + Dt : 0.0f;
			if (Piece.SettleTimer > 0.5f || Age > PhysicsSeconds)
			{
				StopPhysics(Piece);
				Piece.Vel = FVector::ZeroVector;
				Piece.AngVel = FVector::ZeroVector;
				// 굳힐 때 아직 공중이면(벽에 걸쳐 미끄러지던 중) 흉내로 이어서 떨어뜨린다. 공중에서 멈춰 떠 있던 문제(9/19).
				Piece.bResting = Piece.SettleTimer > 0.5f;
			}
			continue;
		}
		if (Piece.bResting)
			continue;
		if (Piece.bTipping)
			TickTip(Piece, Dt);
		else
			TickFly(Piece, Dt);
		if (Piece.bActive)
			ApplyTransform(Piece);
	}
}

void UPGDebrisSubsystem::TickTip(FPiece& Piece, float Dt)
{
	// 기울수록 빨라진다(중력 토크 ∝ sin 각도). 90도에 닿으면 한 번 작게 튀었다 눕는다.
	Piece.TipSpeed += Piece.TipGravity * FMath::Sin(FMath::Max(Piece.TipAngle, 0.05f)) * Dt;
	// 다 넘어가기 전에는 최소한의 속도를 준다. 안 그러면 중력 토크가 작은 조각이 비스듬히 선 채로 굳는다
	// (9/20 PIE: "넘어가다 뭔 세로로 서버리니"). bDusted 는 90도에 닿아야 켜지므로 그 전까지만 민다.
	if (!Piece.bDusted && Piece.TipSpeed < 0.4f)
		Piece.TipSpeed = 0.4f;
	Piece.TipAngle += Piece.TipSpeed * Dt;
	if (Piece.TipAngle >= HALF_PI)
	{
		Piece.TipAngle = HALF_PI;
		if (!Piece.bDusted)
		{
			Piece.bDusted = true;
			// 넘어진 쪽 땅에 먼지. 경첩에서 누운 길이(원래 키)의 절반쯤.
			const FVector Fallen = Piece.Hinge + FVector::CrossProduct(Piece.TiltAxis, FVector::UpVector) * -Piece.Half.Z;
			PlayDust(FVector(Fallen.X, Fallen.Y, Piece.Hinge.Z), Piece.Half.Size());
		}
		if (Piece.TipSpeed > 0.6f)
		{
			Piece.TipSpeed = -Piece.TipSpeed * 0.18f;
		}
		else
		{
			Piece.bTipping = false;
			Piece.bResting = true;
			// 다 넘어간 뒤 실제 바닥과 비교: 공중에 떠 있으면 흉내 물리로 떨어뜨리고, 땅에 파묻혔거나 바닥이 없으면 사라지게.
			const FQuat FinalTilt(Piece.TiltAxis, Piece.TipAngle);
			const FVector FinalPos = Piece.Hinge + FinalTilt.RotateVector(Piece.TipStartPos - Piece.Hinge);
			const FQuat FinalRot = FinalTilt * Piece.TipStartRot;
			const float Ground = TraceGround(Piece, FinalPos + FVector(0.0f, 0.0f, Piece.Half.GetMax()));
			const float Bottom = FinalPos.Z - LowestReach(FinalRot, Piece.Half);
			static int32 EndLogs = 0;
			if (++EndLogs <= 8)
				UE_LOG(LogTemp, Display, TEXT("Debris tip end: ground z %.0f bottom z %.0f -> %s"), Ground, Bottom,
					(Ground <= NoGroundZ + 1.0f || Bottom < Ground - 60.0f) ? TEXT("vanish") : (Bottom > Ground + 40.0f ? TEXT("falls") : TEXT("rests")));
			if (Ground <= NoGroundZ + 1.0f || Bottom < Ground - 60.0f)
			{
				Piece.bVanishing = true;
			}
			else if (Bottom > Ground + 40.0f)
			{
				Piece.bResting = false; // TickFly 가 이어서 떨어뜨린다
				Piece.GroundZ = Ground;
				Piece.GroundSampleXY = FVector2D(FinalPos);
			}
		}
	}
	const FQuat Tilt(Piece.TiltAxis, Piece.TipAngle);
	Piece.Rot = Tilt * Piece.TipStartRot;
	Piece.Pos = Piece.Hinge + Tilt.RotateVector(Piece.TipStartPos - Piece.Hinge);
}

void UPGDebrisSubsystem::TickFly(FPiece& Piece, float Dt)
{
	Piece.Vel.Z -= Gravity * Dt;
	Piece.Vel *= FMath::Max(0.0f, 1.0f - 0.15f * Dt); // 공기 저항 조금
	FVector NewPos = Piece.Pos + Piece.Vel * Dt;
	const float MinHalf = Piece.Half.GetMin();

	// 벽: 빠르게 날 때만 진행 방향으로 선 하나. 맞으면 튕겨 나온다(반사 × 0.35).
	if (Piece.Vel.SizeSquared() > FMath::Square(150.0f) && TracesThisFrame < MaxTracesPerFrame)
	{
		FHitResult Hit;
		const FVector Dir = Piece.Vel.GetSafeNormal();
		if (TraceFirst(Piece, Piece.Pos, NewPos + Dir * MinHalf, Hit) && Hit.ImpactNormal.Z < 0.7f)
		{
			const FVector N = Hit.ImpactNormal;
			Piece.Vel = (Piece.Vel - 2.0f * FVector::DotProduct(Piece.Vel, N) * N) * 0.35f;
			Piece.AngVel *= 0.5f;
			NewPos = Hit.ImpactPoint + N * MinHalf;
		}
	}

	// 회전
	const float Spin = Piece.AngVel.Size();
	if (Spin > KINDA_SMALL_NUMBER)
	{
		Piece.Rot = FQuat(Piece.AngVel / Spin, Spin * Dt) * Piece.Rot;
		Piece.Rot.Normalize();
	}

	// 바닥: 옆으로 1m 넘게 움직였을 때만 다시 찾는다(한 조각이 매 프레임 트레이스하지 않게).
	if (FVector2D::DistSquared(FVector2D(NewPos), Piece.GroundSampleXY) > FMath::Square(100.0f) && TracesThisFrame < MaxTracesPerFrame)
	{
		Piece.GroundZ = TraceGround(Piece, NewPos + FVector(0.0f, 0.0f, Piece.Half.GetMax()));
		Piece.GroundSampleXY = FVector2D(NewPos);
	}

	// 바닥이 없는 자리(맵 밖·못 찾음)거나, 이미 바닥보다 한참 아래로 빠졌으면 떨어뜨리지 않고 그 자리에서 사라진다.
	// 사용자 피드백(9/20): 땅속으로 꺼지는 것보다 그냥 사라지는 게 낫다.
	if (Piece.GroundZ <= NoGroundZ + 1.0f || NewPos.Z < Piece.GroundZ - Piece.Half.GetMax() - 50.0f)
	{
		Piece.bVanishing = true;
		Piece.Vel = FVector::ZeroVector;
		return;
	}
	// 가장 낮은 모서리가 바닥 아래로 가면 올려 주고 튕긴다(땅에 박히지 않는다).
	const float Low = NewPos.Z - LowestReach(Piece.Rot, Piece.Half);
	bool bGrounded = false;
	if (Low <= Piece.GroundZ + 1.0f)
	{
		NewPos.Z += Piece.GroundZ - Low;
		bGrounded = true;
		if (Piece.Vel.Z < -80.0f)
		{
			Piece.Vel.Z = -Piece.Vel.Z * 0.25f;
			Piece.Vel.X *= 0.7f;
			Piece.Vel.Y *= 0.7f;
			Piece.AngVel *= 0.7f;
		}
		else
		{
			Piece.Vel.Z = FMath::Max(Piece.Vel.Z, 0.0f);
		}
	}

	if (bGrounded)
	{
		// 땅 마찰
		const float Friction = FMath::Exp(-4.0f * Dt);
		Piece.Vel.X *= Friction;
		Piece.Vel.Y *= Friction;
		Piece.AngVel *= FMath::Exp(-5.0f * Dt);
		// 느려지면 가장 넓은 면으로 눕힌다. 다 누웠으면 멈추고 더는 계산하지 않는다.
		if (Piece.Vel.SizeSquared() < FMath::Square(80.0f))
		{
			float Sign = 1.0f;
			const FVector Axis = PickRestAxis(Piece.Rot, Piece.Half, Sign);
			const FQuat Target = FQuat::FindBetweenNormals(Axis, FVector::UpVector * Sign) * Piece.Rot;
			Piece.Rot = FQuat::Slerp(Piece.Rot, Target, FMath::Min(1.0f, 6.0f * Dt));
			Piece.AngVel *= FMath::Exp(-10.0f * Dt);
			if (Piece.Rot.AngularDistance(Target) < 0.02f && Piece.Vel.SizeSquared() < FMath::Square(15.0f))
			{
				Piece.Rot = Target;
				NewPos.Z = Piece.GroundZ + LowestReach(Piece.Rot, Piece.Half);
				Piece.Vel = FVector::ZeroVector;
				Piece.AngVel = FVector::ZeroVector;
				Piece.bResting = true;
			}
		}
	}
	Piece.Pos = NewPos;
	if (Piece.Pos.Z < HiddenZ * 0.2f)
		Deactivate(Piece);
}

void UPGDebrisSubsystem::ApplyTransform(FPiece& Piece)
{
	if (UBoxComponent* Box = Piece.Box.Get())
		Box->SetWorldLocationAndRotation(Piece.Pos, Piece.Rot, false, nullptr, ETeleportType::TeleportPhysics);
}

void UPGDebrisSubsystem::SetVanish(FPiece& Piece, float Alpha)
{
	Piece.VanishAlpha = FMath::Clamp(Alpha, 0.0f, 1.0f);
	if (UStaticMeshComponent* Visual = Piece.Visual.Get())
	{
		// 상자 중심 기준으로 작아진다(메시 위치 보정값도 같은 비율로).
		Visual->SetRelativeScale3D(Piece.VisualScale * FMath::Max(Piece.VanishAlpha, 0.01f));
		Visual->SetRelativeLocation(Piece.VisualOffset * Piece.VanishAlpha);
	}
}

void UPGDebrisSubsystem::QueueSupportCheck(const FBox& Removed)
{
	// 한꺼번에 수백 건이 쌓이지 않게(공장 붕괴) 상한. 넘치면 가장 오래된 것부터 버린다 — 조금 덜 무너질 뿐이다.
	if (!Removed.IsValid)
		return;
	if (PendingSupportChecks.Num() >= 64)
		PendingSupportChecks.RemoveAt(0);
	PendingSupportChecks.Add({ Removed, 0 });
}

bool UPGDebrisSubsystem::IsSupported(const FBox& Bounds, const UPrimitiveComponent* Candidate, int32 CandidateItem) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return true;
	// 바닥면 다섯 점(가운데 + 안쪽으로 들인 네 모서리)에서 1.2m 아래까지 재서, 하나라도 받쳐 주는 게 있으면 서 있다.
	// 벽 위에 가장자리만 걸친 바닥 판도 모서리가 벽에 닿으니 "받쳐짐"이다(진짜 건물처럼).
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSupportCheck), false);
	for (const TObjectPtr<AActor>& PoolActor : PoolActors)
		Params.AddIgnoredActor(PoolActor.Get());
	const bool bInstanced = Candidate && Candidate->IsA<UInstancedStaticMeshComponent>();
	if (Candidate && !bInstanced)
		Params.AddIgnoredComponent(Candidate);
	const FVector C = Bounds.GetCenter();
	const FVector E = Bounds.GetExtent() * 0.7f;
	const FVector2D Offsets[] = { FVector2D(0, 0), FVector2D(-1, -1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(1, 1) };
	for (const FVector2D& Offset : Offsets)
	{
		const FVector Start(C.X + Offset.X * E.X, C.Y + Offset.Y * E.Y, Bounds.Min.Z + 15.0f);
		TArray<FHitResult> Hits;
		World->LineTraceMultiByObjectType(Hits, Start, Start - FVector(0.0f, 0.0f, 135.0f), Objects, Params);
		for (const FHitResult& Hit : Hits)
		{
			const UPrimitiveComponent* Component = Hit.GetComponent();
			if (!IsValid(Component) || (Component == Candidate && Hit.Item == CandidateItem) || Cast<APawn>(Hit.GetActor()))
				continue;
			if (Component->GetCollisionResponseToChannel(ECC_Vehicle) == ECR_Block || (Component->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block && Component->IsVisible()))
				return true;
		}
	}
	return false;
}

void UPGDebrisSubsystem::ProcessSupportChecks()
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || PendingSupportChecks.IsEmpty())
		return;
	TRACE_CPUPROFILER_EVENT_SCOPE(PGDebris_SupportChecks);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSupportOverlap), false);
	for (const TObjectPtr<AActor>& PoolActor : PoolActors)
		Params.AddIgnoredActor(PoolActor.Get());

	TArray<FSupportCheck> Retry;
	for (int32 Done = 0; Done < 3 && !PendingSupportChecks.IsEmpty(); ++Done)
	{
		const FSupportCheck Check = PendingSupportChecks[0];
		PendingSupportChecks.RemoveAt(0);
		const FBox& Removed = Check.Removed;
		// 사라진 조각의 윗면 바로 위(안쪽 90%)에 걸친 것들.
		const FVector Center(Removed.GetCenter().X, Removed.GetCenter().Y, Removed.Max.Z + 60.0f);
		const FVector Half(Removed.GetExtent().X * 0.9f + 20.0f, Removed.GetExtent().Y * 0.9f + 20.0f, 90.0f);
		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, Objects, FCollisionShape::MakeBox(Half), Params);
		int32 Dropped = 0;
		bool bBudgetHit = false;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UPrimitiveComponent* Component = Overlap.GetComponent();
			if (!IsValid(Component) || Dropped >= 6 || Component->ComponentHasTag(PGPhysicsUtil::TerrainTag)
				|| !Component->IsVisible() || Cast<APawn>(Component->GetOwner()) || IsDebris(Component))
				continue;
			FBox Bounds = Component->Bounds.GetBox();
			if (const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(Component))
			{
				FTransform InstanceTransform;
				if (Overlap.ItemIndex < 0 || !Instanced->GetStaticMesh() || !Instanced->GetInstanceTransform(Overlap.ItemIndex, InstanceTransform, true))
					continue;
				Bounds = Instanced->GetStaticMesh()->GetBounds().TransformBy(InstanceTransform).GetBox();
			}
			// "얹혀 있던 것"만: 바닥이 사라진 조각의 윗면 근처(아래 60cm ~ 위 150cm). 그보다 아래에서 시작하는 건 옆 벽이다.
			if (Bounds.Min.Z < Removed.Max.Z - 60.0f || Bounds.Min.Z > Removed.Max.Z + 150.0f)
				continue;
			if (IsSupported(Bounds, Component, Overlap.ItemIndex))
				continue;
			FHitResult Fake;
			Fake.ImpactPoint = Bounds.GetCenter();
			Fake.Item = Overlap.ItemIndex;
			// 크기 제한 없이(받침 없는 2층 바닥 판도) 떨어뜨린다. 힘은 아래로 아주 조금 — 그냥 떨어지는 것.
			if (PGPhysicsUtil::TryKnockProp(Component, Fake, FVector(0.0f, 0.0f, -50.0f), nullptr, 100000.0f, 100.0f, 0.0f, /*bFallOnly*/ true))
				++Dropped;
			else
				bBudgetHit = true; // 이번 프레임 잔해 예산이 찼다 → 다음에 다시 본다
		}
		if (bBudgetHit && Check.Retries < 5)
			Retry.Add({ Removed, Check.Retries + 1 });
	}
	PendingSupportChecks.Append(Retry);
}

void UPGDebrisSubsystem::PlayDust(const FVector& Location, float Size)
{
	UWorld* World = GetWorld();
	if (!DustEffect || !IsValid(World))
		return;
	// 한꺼번에 무너질 때 먼지가 수십 개 겹치면 그것도 비용이다. 0.12초에 하나만.
	const double Now = World->GetTimeSeconds();
	if (Now - LastDustTime < CVarDebrisDustInterval.GetValueOnGameThread())
		return;
	LastDustTime = Now;
	UGameplayStatics::SpawnEmitterAtLocation(World, DustEffect, Location, FRotator::ZeroRotator,
		FVector(FMath::Clamp(Size / 250.0f, 0.5f, 2.5f)), true, EPSCPoolMethod::AutoRelease);
}
