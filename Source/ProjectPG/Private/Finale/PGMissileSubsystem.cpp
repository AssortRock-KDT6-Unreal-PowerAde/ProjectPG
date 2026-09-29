#include "Finale/PGMissileSubsystem.h"

#include "Common/PGEffectSet.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Objects/PGObjectTypes.h"

namespace
{
	// 모델링 세션이 만들어 주기로 한 키트. 아직 없으면 안 보이는 채로 날아간다(피해는 그대로).
	const TCHAR* const MissileDir = TEXT("/Game/PG/Finale/Missile/");

	// 미사일 값. 전함이 쏘는 것이라 사람 무기보다 훨씬 크고 느리다 — 날아가는 게 보여야 피할 맛이 난다.
	constexpr float LaunchSpeed = 3000.0f;   // cm/s (30m/s) 로 나가서
	constexpr float TopSpeed = 9000.0f;      // 90m/s 까지 붙는다
	constexpr float Acceleration = 6000.0f;  // cm/s²
	constexpr float TurnRateDeg = 70.0f;     // 초당 도. 이보다 급하게는 못 돈다 → 크게 돌면 피할 수 있다
	constexpr float MaxLifeSeconds = 9.0f;
	constexpr float ExplodeSeconds = 0.3f;
	constexpr float BlastMeshRadiusCm = 100.0f; // SM_MissileBlast 는 반지름 1m 구
	// 보이는 크기 배율. 9/21 사용자: "미사일 크기 좀 늘려야겠다 ... 날아가는지 티도 안 나". 400cm 짜리가 500m 밖에서는 점이었다.
	//   액터째 키우므로 몸통·불꽃이 같이 커진다. 폭발 구는 피해 반경과 맞춰야 해서 이 배율로 나눠 되돌린다.
	constexpr float VisualScale = 3.5f;
	// 근접 신관: 표적 몸에 이만큼 가까이 스치면 터진다. 드래곤 충돌 캡슐은 날개보다 훨씬 작아서, 날개를 뚫고 지나가는데
	//   선 검사에는 안 걸려 그대로 빗나가 엉뚱한 데서 터졌다(9/21 사용자: "엉뚱한 드래곤 왼쪽에 터지는데").
	constexpr float ProximityFuseCm = 1500.0f;

	UStaticMesh* LoadMissileMesh(const TCHAR* Name)
	{
		// 폴더는 이펙트 묶음(DA_PGEffects)의 MissileKitFolder(비었으면 원래 폴더)에서(9/23 블루프린트 분리).
		const UPGEffectSet* Effects = UPGEffectSet::GetActive();
		FString Dir = (Effects && !Effects->MissileKitFolder.Path.IsEmpty()) ? Effects->MissileKitFolder.Path : FString(MissileDir);
		if (!Dir.EndsWith(TEXT("/")))
			Dir += TEXT("/");
		const FString Path = FString::Printf(TEXT("%s%s.%s"), *Dir, Name, Name);
		return LoadObject<UStaticMesh>(nullptr, *Path);
	}
}

UPGMissileSubsystem* UPGMissileSubsystem::Get(const UWorld* World)
{
	return IsValid(World) ? World->GetSubsystem<UPGMissileSubsystem>() : nullptr;
}

bool UPGMissileSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UPGMissileSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPGMissileSubsystem, STATGROUP_Tickables);
}

void UPGMissileSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	Missiles.SetNum(MaxAlive);
	// 액터는 처음 쏠 때 하나씩 만든다. 판 시작에 24개를 한꺼번에 만들면 로딩이 그만큼 길어지는데,
	// 미사일은 피날레에서만 쓰니 그때 몇 개씩 만드는 편이 낫다(잔해는 부수기가 판 내내 일어나 미리 다 만든다).
}

void UPGMissileSubsystem::Deinitialize()
{
	for (FMissile& Missile : Missiles)
		if (AActor* Actor = Missile.Actor.Get())
			Actor->Destroy();
	Missiles.Reset();
	PoolActors.Reset();
	Super::Deinitialize();
}

bool UPGMissileSubsystem::CreatePooled(FMissile& Missile)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return false;
	if (!bMeshesResolved)
	{
		bMeshesResolved = true;
		BodyMesh = LoadMissileMesh(TEXT("SM_ShipMissile"));
		TrailMesh = LoadMissileMesh(TEXT("SM_ShipMissileTrail"));
		BlastMesh = LoadMissileMesh(TEXT("SM_MissileBlast"));
		UE_LOG(LogPGObjects, Display, TEXT("PGMissile: meshes body=%s trail=%s blast=%s%s"),
			BodyMesh ? TEXT("ok") : TEXT("missing"), TrailMesh ? TEXT("ok") : TEXT("missing"), BlastMesh ? TEXT("ok") : TEXT("missing"),
			BodyMesh ? TEXT("") : TEXT(" — building from engine basic shapes instead"));
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, Params);
	if (!IsValid(Actor))
		return false;
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	Actor->SetActorScale3D(FVector(VisualScale));

	auto AddMesh = [Actor, Root](UStaticMesh* Mesh, const TCHAR* Name) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor, Name);
		if (Mesh)
			Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(Root);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 충돌은 선 트레이스로 직접 본다
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetVisibility(false);
		Component->RegisterComponent();
		return Component;
	};
	Missile.Actor = Actor;
	if (BodyMesh)
	{
		Missile.Body = AddMesh(BodyMesh, TEXT("Body"));
		Missile.Trail = AddMesh(TrailMesh, TEXT("Trail"));
		Missile.Blast = AddMesh(BlastMesh, TEXT("Blast"));
		// 메시의 피벗은 꼬리 끝이고 +X 로 몸이 뻗는다(모델링 규칙). 그대로 두면 액터 위치가 꼬리가 되어
		// 코끝이 4m 앞서 나가고, 맞는 순간보다 늦게 터진다. 몸을 뒤로 물려 코끝을 액터 위치에 맞춘다.
		const float Length = BodyMesh->GetBounds().BoxExtent.X * 2.0f;
		if (UStaticMeshComponent* Body = Missile.Body.Get())
			Body->SetRelativeLocation(FVector(-Length, 0.0f, 0.0f));
		if (UStaticMeshComponent* Trail = Missile.Trail.Get())
			Trail->SetRelativeLocation(FVector(-Length, 0.0f, 0.0f)); // 불꽃은 꼬리에서 다시 -X 로 뻗는다
	}
	else
	{
		// 전용 메시가 아직 없다. 엔진 기본 도형으로 조립해 둔다 — 안 보이는 채로 날아가는 것보다 낫다.
		bUsingFallbackShapes = true;
		BuildFallbackShapes(Actor, Missile);
	}
	PoolActors.Add(Actor);
	return true;
}

// 전용 메시가 없을 때: 엔진 기본 도형으로 모양을 만든다.
// 왜 여기서 만드나 — 이 모양은 원통 + 원뿔 + 지느러미 네 장이라 굳이 따로 모델링할 것이 없다.
// 엔진 기본 도형(/Engine/BasicShapes)은 전부 100cm 짜리에 피벗이 한가운데라 배율만 주면 된다.
// 나중에 전용 메시가 들어오면 그쪽이 우선이고 이 조각들은 아예 만들어지지 않는다.
void UPGMissileSubsystem::BuildFallbackShapes(AActor* Actor, FMissile& Missile)
{
	UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Cylinder || !Cone)
		return;
	// 몸통은 흰 금속, 뒤쪽 링과 배기 불빛은 노란 발광(함교 키트에서 만든 것을 그대로 쓴다).
	// 재질은 이펙트 묶음(DA_PGEffects, 없으면 원래 에셋)에서(9/23 블루프린트 분리).
	const UPGEffectSet* Effects = UPGEffectSet::GetActive();
	auto LoadMaterial = [](const TSoftObjectPtr<UMaterialInterface>& Slot) { return Slot.IsNull() ? nullptr : Slot.LoadSynchronous(); };
	UMaterialInterface* Body = Effects ? LoadMaterial(Effects->MissileBodyMaterial) : nullptr;
	UMaterialInterface* Dark = Effects ? LoadMaterial(Effects->MissileDarkMaterial) : nullptr;
	UMaterialInterface* Glow = Effects ? LoadMaterial(Effects->MissileGlowMaterial) : nullptr;

	auto Add = [Actor, &Missile](UStaticMesh* Mesh, UMaterialInterface* Material, const FVector& Location,
		const FRotator& Rotation, const FVector& Scale) -> UStaticMeshComponent*
	{
		if (!Mesh)
			return nullptr;
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Actor);
		Component->SetStaticMesh(Mesh);
		if (Material)
			Component->SetMaterial(0, Material);
		Component->SetupAttachment(Actor->GetRootComponent());
		Component->SetRelativeLocationAndRotation(Location, Rotation);
		Component->SetRelativeScale3D(Scale);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetVisibility(false);
		Component->RegisterComponent();
		Missile.Shape.Add(Component);
		return Component;
	};

	// 피벗은 뒤쪽 끝, +X 가 앞. 기본 도형은 축이 Z 라 피치 90 도로 눕힌다.
	const FRotator Lie(90.0f, 0.0f, 0.0f);
	Add(Cylinder, Body, FVector(170.0f, 0.0f, 0.0f), Lie, FVector(0.6f, 0.6f, 2.8f)); // 몸통 280cm
	Add(Cone, Body, FVector(370.0f, 0.0f, 0.0f), Lie, FVector(0.6f, 0.6f, 1.2f));     // 앞쪽 원뿔 120cm
	Add(Cylinder, Dark, FVector(20.0f, 0.0f, 0.0f), Lie, FVector(0.68f, 0.68f, 0.4f)); // 뒤쪽 링
	Add(Cylinder, Glow, FVector(6.0f, 0.0f, 0.0f), Lie, FVector(0.5f, 0.5f, 0.12f));   // 노즐 발광면
	// 지느러미 네 장: 얇은 상자를 90도씩 돌려 붙인다.
	for (int32 Index = 0; Index < 4; ++Index)
	{
		const FRotator Roll(0.0f, 0.0f, Index * 90.0f);
		const FVector Offset = Roll.RotateVector(FVector(0.0f, 0.0f, 38.0f));
		Add(Cube, Dark, FVector(55.0f, Offset.Y, Offset.Z), Roll, FVector(0.9f, 0.06f, 0.55f));
	}
	// 배기 불빛: 피벗에서 -X 로 뻗는 원뿔. 전용 메시와 같은 규칙(X 배율 = 길이)이라 Trail 자리에 그대로 넣는다.
	if (UStaticMeshComponent* Flame = Add(Cone, Glow, FVector(-50.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f), FVector(0.45f, 0.45f, 1.0f)))
	{
		Missile.Trail = Flame;
		Missile.Shape.Remove(Flame); // 꼬리는 따로 켜고 끈다
	}
	// 확산 구체: 기본 구는 지름 100cm(반지름 50) 라 배율 2 가 반지름 100cm 다.
	if (UStaticMeshComponent* Blast = Add(Sphere, Glow, FVector::ZeroVector, FRotator::ZeroRotator, FVector(2.0f)))
	{
		Missile.Blast = Blast;
		Missile.Shape.Remove(Blast);
	}
}

int32 UPGMissileSubsystem::Acquire()
{
	// 빈 자리 먼저. 없으면 가장 오래 날아다닌 것을 거둔다.
	int32 Oldest = INDEX_NONE;
	float OldestAge = -1.0f;
	for (int32 Index = 0; Index < Missiles.Num(); ++Index)
	{
		FMissile& Missile = Missiles[Index];
		if (!Missile.bActive)
		{
			if (!Missile.Actor.IsValid() && !CreatePooled(Missile))
				continue;
			return Index;
		}
		if (Missile.Age > OldestAge)
		{
			OldestAge = Missile.Age;
			Oldest = Index;
		}
	}
	if (Oldest != INDEX_NONE)
	{
		Deactivate(Missiles[Oldest]);
		return Oldest;
	}
	return INDEX_NONE;
}

bool UPGMissileSubsystem::Launch(const FVector& Start, const FVector& Direction, AActor* Target, AActor* Shooter,
	float DirectDamage, float BlastDamage, float BlastRadius, const FVector& AimPoint)
{
	const int32 Index = Acquire();
	if (Index == INDEX_NONE)
		return false;
	FMissile& Missile = Missiles[Index];
	if (!Missile.Actor.IsValid())
		return false;
	Missile.bActive = true;
	Missile.bExploding = false;
	Missile.Pos = Start;
	Missile.Dir = Direction.GetSafeNormal();
	Missile.Speed = LaunchSpeed;
	Missile.Age = 0.0f;
	Missile.ExplodeAge = 0.0f;
	Missile.Target = Target;
	Missile.Shooter = Shooter;
	Missile.DirectDamage = DirectDamage;
	Missile.BlastDamage = BlastDamage;
	Missile.BlastRadius = BlastRadius;
	Missile.AimPoint = AimPoint;
	Missile.bHasAimPoint = !AimPoint.IsZero();
	if (UStaticMeshComponent* Body = Missile.Body.Get())
		Body->SetVisibility(true);
	for (TWeakObjectPtr<UStaticMeshComponent> Part : Missile.Shape)
		if (UStaticMeshComponent* Component = Part.Get())
			Component->SetVisibility(true);
	if (UStaticMeshComponent* Trail = Missile.Trail.Get())
	{
		Trail->SetVisibility(true);
		// 꼬리 화염은 부스터 불꽃과 같은 규칙: 피벗에서 -X 로 100cm → X 배율이 곧 길이.
		Trail->SetRelativeScale3D(FVector(6.0f, 1.0f, 1.0f));
	}
	if (UStaticMeshComponent* Blast = Missile.Blast.Get())
		Blast->SetVisibility(false);
	Apply(Missile);
	return true;
}

void UPGMissileSubsystem::Deactivate(FMissile& Missile)
{
	Missile.bActive = false;
	Missile.bExploding = false;
	Missile.Target = nullptr;
	for (TWeakObjectPtr<UStaticMeshComponent> Part : { Missile.Body, Missile.Trail, Missile.Blast })
		if (UStaticMeshComponent* Component = Part.Get())
			Component->SetVisibility(false);
	for (TWeakObjectPtr<UStaticMeshComponent> Part : Missile.Shape)
		if (UStaticMeshComponent* Component = Part.Get())
			Component->SetVisibility(false);
}

void UPGMissileSubsystem::Apply(FMissile& Missile)
{
	AActor* Actor = Missile.Actor.Get();
	if (!Actor)
		return;
	// 미사일 메시는 피벗이 꼬리, +X 가 탄두 쪽이다. 그래서 진행 방향을 그대로 회전으로 준다.
	Actor->SetActorLocationAndRotation(Missile.Pos, Missile.Dir.Rotation(), false, nullptr, ETeleportType::TeleportPhysics);
}

void UPGMissileSubsystem::StartExplosion(FMissile& Missile, const FVector& Location, AActor* DirectHit)
{
	UWorld* World = GetWorld();
	AActor* Shooter = Missile.Shooter.Get();
	AController* ShooterController = Shooter ? Shooter->GetInstigatorController() : nullptr;
	TArray<AActor*> Ignore;
	if (Shooter)
		Ignore.Add(Shooter);
	if (IsValid(DirectHit) && DirectHit != Shooter)
		UGameplayStatics::ApplyDamage(DirectHit, Missile.DirectDamage, ShooterController, Shooter, UDamageType::StaticClass());
	// 범위 피해는 탱크 주포와 같은 규칙(가운데 30% 는 전부, 가장자리는 20%).
	UGameplayStatics::ApplyRadialDamageWithFalloff(World, Missile.BlastDamage, Missile.BlastDamage * 0.2f, Location,
		Missile.BlastRadius * 0.3f, Missile.BlastRadius, 1.0f, UDamageType::StaticClass(), Ignore, Shooter, ShooterController, ECC_Visibility);

	Missile.bExploding = true;
	Missile.ExplodeAge = 0.0f;
	Missile.Pos = Location;
	Apply(Missile);
	if (UStaticMeshComponent* Body = Missile.Body.Get())
		Body->SetVisibility(false);
	for (TWeakObjectPtr<UStaticMeshComponent> Part : Missile.Shape)
		if (UStaticMeshComponent* Component = Part.Get())
			Component->SetVisibility(false);
	if (UStaticMeshComponent* Trail = Missile.Trail.Get())
		Trail->SetVisibility(false);
	if (UStaticMeshComponent* Blast = Missile.Blast.Get())
	{
		Blast->SetVisibility(true);
		Blast->SetRelativeScale3D(FVector(0.2f));
	}
}

void UPGMissileSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UWorld* World = GetWorld();
	if (!IsValid(World) || DeltaTime <= 0.0f)
		return;

	for (FMissile& Missile : Missiles)
	{
		if (!Missile.bActive)
			continue;
		if (Missile.bExploding)
		{
			// 폭발 껍질: 0.3초 동안 0.2 → 껍질 반지름이 피해 반지름이 되도록 키우고 끝나면 거둔다.
			Missile.ExplodeAge += DeltaTime;
			const float Alpha = FMath::Clamp(Missile.ExplodeAge / ExplodeSeconds, 0.0f, 1.0f);
			if (UStaticMeshComponent* Blast = Missile.Blast.Get())
				Blast->SetRelativeScale3D(FVector(FMath::Lerp(0.2f, Missile.BlastRadius / BlastMeshRadiusCm, Alpha) / VisualScale));
			if (Alpha >= 1.0f)
				Deactivate(Missile);
			continue;
		}

		Missile.Age += DeltaTime;
		if (Missile.Age > MaxLifeSeconds)
		{
			StartExplosion(Missile, Missile.Pos, nullptr); // 연료가 다하면 그 자리에서 터진다
			continue;
		}
		// 유도: 목표 쪽으로 도는 양을 초당 TurnRateDeg 로 제한한다. 급하게 못 돌아서 크게 돌면 피할 수 있다.
		// 표적이 없으면 조준점으로 튼다. 전에는 표적이 없을 때 처음 방향 그대로 갔는데, 두 발이 좌우로 14도씩 벌어져
		//   나가니 조준점 양옆 허공으로 날아가 거기서 터졌다(9/21 사용자: "조준점 맞춰도 제대로 드래곤 맞추지도 못하네").
		AActor* Target = Missile.Target.Get();
		if (Target && ProximityFuseCm > 0.0f && FVector::DistSquared(Target->GetActorLocation(), Missile.Pos) < FMath::Square(ProximityFuseCm))
		{
			StartExplosion(Missile, Missile.Pos, Target);
			continue;
		}
		if (Target || Missile.bHasAimPoint)
		{
			const FVector Goal = Target ? Target->GetActorLocation() : Missile.AimPoint;
			// 조준점을 지나쳤으면 더 틀지 않는다 — 안 그러면 그 점 둘레를 빙빙 돈다.
			if (!Target && FVector::DotProduct(Goal - Missile.Pos, Missile.Dir) < 0.0f)
				Missile.bHasAimPoint = false;
			const FVector Want = (Goal - Missile.Pos).GetSafeNormal();
			const float MaxRadians = FMath::DegreesToRadians(TurnRateDeg) * DeltaTime;
			const float Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Missile.Dir, Want), -1.0f, 1.0f));
			Missile.Dir = Angle <= MaxRadians ? Want
				: FMath::VInterpNormalRotationTo(Missile.Dir, Want, DeltaTime, TurnRateDeg);
		}
		Missile.Speed = FMath::Min(TopSpeed, Missile.Speed + Acceleration * DeltaTime);

		// 이동: 이번 프레임에 지나갈 선을 한 번 훑어 막히면 거기서 터진다(빠른 물체가 벽을 뚫지 않게).
		const FVector Next = Missile.Pos + Missile.Dir * Missile.Speed * DeltaTime;
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGMissile), false, Missile.Shooter.Get());
		if (AActor* Actor = Missile.Actor.Get())
			Params.AddIgnoredActor(Actor);
		if (World->LineTraceSingleByChannel(Hit, Missile.Pos, Next, ECC_Visibility, Params))
		{
			StartExplosion(Missile, Hit.ImpactPoint, Hit.GetActor());
			continue;
		}
		Missile.Pos = Next;
		Apply(Missile);
	}
}

int32 UPGMissileSubsystem::CountActive() const
{
	int32 Count = 0;
	for (const FMissile& Missile : Missiles)
		if (Missile.bActive)
			++Count;
	return Count;
}
