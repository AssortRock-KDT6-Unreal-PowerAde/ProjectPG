// APGBattleshipActor — 무기 — 주포·미사일 발사와 폭발.
// (2026-09-26 PGBattleshipActor.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGBattleshipActorInternal.h"
#include "PGShipWeapons.h"
#include "PGShipHullBuilder.h"

// ---- 주포 ----
//
// 드래곤 공중전에서 쓸 전함의 공격 수단. 탱크 주포(APGTankPawn::FireMainGun/Explode)와 같은 방식이다:
//   포구에서 조준점으로 선을 쏘고, 맞은 곳에 직격 + 범위 피해를 주고, 둘레의 소품을 날린다.
//   그래서 "피날레를 위해 새로 짠 것은 상태 기계뿐"이라는 기획서 1절 주장이 유지된다.
// 조준은 조종석에 선 사람의 화면 가운데다 — 1인칭으로 창밖을 보며 겨누면 그대로 탄착점이 된다.
// 포구는 뱃머리 양옆과 등쪽 셋. 빔은 가발 광선 메시(SM_WigBeam: 피벗에서 +X 로 100cm, X 배율 = 거리/100)를 재사용한다.

void UPGShipWeapons::BuildCannons()
{
	const FVector Size = Ship->HullLocalBounds.GetSize();
	MuzzleLocals = {
		FVector(Ship->HullLocalBounds.Max.X - 3000.0f,  Size.Y * 0.28f, Ship->DeckLocalZ + 1500.0f),
		FVector(Ship->HullLocalBounds.Max.X - 3000.0f, -Size.Y * 0.28f, Ship->DeckLocalZ + 1500.0f),
		FVector(Ship->HullLocalBounds.Max.X - 8000.0f,  0.0f,           Ship->HullLocalBounds.Min.Z + Size.Z * 0.62f),
	};
	UStaticMesh* Beam = (Ship->CannonBeamMesh.IsNull() ? nullptr : Ship->CannonBeamMesh.LoadSynchronous());
	for (const FVector& Muzzle : MuzzleLocals)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Ship.Get());
		if (Beam)
			Component->SetStaticMesh(Beam);
		Component->SetupAttachment(Ship->InteriorRoot);
		Component->SetRelativeLocation(Muzzle);
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(false);
		Component->SetCanEverAffectNavigation(false);
		Component->SetVisibility(false);
		Component->RegisterComponent();
		Ship->CannonBeams.Add(Component);
	}
	// 조종석 앞 2m 안에 서 있어야 주포를 쓸 수 있다. 갑판 아무 데서나 쏘면 "함교에 섰다"는 느낌이 안 난다.
	Ship->HelmZone = Ship->HullBuilder->AddWalkBox(TEXT("HelmZone"), Ship->BridgeLocal + FVector(-150.0f, 0.0f, 100.0f), FVector(250.0f, 400.0f, 110.0f));
	Ship->HelmZone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Ship->HelmZone->SetCollisionResponseToAllChannels(ECR_Overlap);
	Ship->HelmZone->SetGenerateOverlapEvents(true);
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: cannons ready (muzzles=%d beam=%s)"), MuzzleLocals.Num(), Beam ? TEXT("on") : TEXT("off"));
}

// 주포가 닿은 자리의 번쩍임: 빛나는 구가 0.35초 동안 부풀며 사라진다 + 흙먼지.
// 왜: 빔만으로는 "맞았다" 가 안 읽혔다(9/21 사용자: "공격 너무 힘없다"). 드래곤에 맞으면 더 크게.
// 비용: 액터 하나 + 30Hz 타이머, 쏠 때마다(1초 간격) 하나라 싸다. 미사일 폭발 구 메시(Content/PG)를 같이 쓴다.
void UPGShipWeapons::SpawnCannonImpactFlash(const FVector& Where, bool bBig)
{
	UWorld* World = Ship->GetWorld();
	if (!World)
		return;
	if (UParticleSystem* Dust = (Ship->WreckDustEffect.IsNull() ? nullptr : Ship->WreckDustEffect.LoadSynchronous()))
		UGameplayStatics::SpawnEmitterAtLocation(World, Dust, Where, FRotator::ZeroRotator, FVector(bBig ? 6.0f : 3.0f), true, EPSCPoolMethod::AutoRelease);
	SpawnBlastBall(Where, bBig ? 25.0f : 12.0f, 0.35f); // 반지름 m (메시가 반지름 1m 구)
}

// 빛나는 구 하나가 Seconds 동안 MaxRadiusM 까지 부풀며 사라진다. 주포 탄착(0.35초, 12~25m)과 추락 폭발(더 크고 길게)이
// 같은 함수를 쓴다 — 게임 안에서 폭발이 두 가지 모양으로 보이면 안 되고, 고칠 때도 한 곳만 고치면 된다.
void UPGShipWeapons::SpawnBlastBall(const FVector& Where, float MaxRadiusM, float Seconds)
{
	UWorld* World = Ship->GetWorld();
	if (!World)
		return;
	UStaticMesh* BlastMesh = (Ship->ImpactBlastMesh.IsNull() ? nullptr : Ship->ImpactBlastMesh.LoadSynchronous());
	if (!BlastMesh)
		return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Flash = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Where), Params);
	if (!IsValid(Flash))
		return;
	UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(Flash, TEXT("CannonFlash"));
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->SetStaticMesh(BlastMesh);
	if (UMaterialInterface* BlastMaterial = (Ship->ImpactBlastMaterial.IsNull() ? nullptr : Ship->ImpactBlastMaterial.LoadSynchronous()))
		Ball->SetMaterial(0, BlastMaterial);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetCastShadow(false);
	Flash->SetRootComponent(Ball);
	Ball->RegisterComponent();
	Ball->SetWorldLocation(Where);
	const double Start = World->GetTimeSeconds();
	const float MaxScale = FMath::Max(1.0f, MaxRadiusM);
	const float Duration = FMath::Max(0.1f, Seconds);
	TWeakObjectPtr<UStaticMeshComponent> WeakBall(Ball);
	TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Flash, [WeakBall, Start, MaxScale, Duration, Handle]()
	{
		UStaticMeshComponent* B = WeakBall.Get();
		if (!IsValid(B))
			return;
		const float T = FMath::Clamp(static_cast<float>(B->GetWorld()->GetTimeSeconds() - Start) / Duration, 0.0f, 1.0f);
		B->SetWorldScale3D(FVector(FMath::Lerp(1.0f, MaxScale, FMath::Sqrt(T))));
		if (T >= 1.0f)
		{
			B->GetWorld()->GetTimerManager().ClearTimer(*Handle);
			B->GetOwner()->Destroy();
		}
	}), 1.0f / 30.0f, true);
	Flash->SetLifeSpan(Duration + 2.0f);
}

bool UPGShipWeapons::FireCannon(const FVector& AimPoint, AController* Shooter)
{
	UWorld* World = Ship->GetWorld();
	if (!Ship->HasAuthority() || !World || MuzzleLocals.IsEmpty() || World->GetTimeSeconds() - LastCannonTime < Ship->CannonInterval)
		return false;
	LastCannonTime = World->GetTimeSeconds();
	// 판 기록(사격 수). 전함은 폰이 아니라 지시한 컨트롤러로 "플레이어가 쐈나" 를 가린다.
	UPGRunSubsystem::NotifyShotFired(Shooter);

	// 포구는 번갈아 쓴다(세 문이 한꺼번에 불을 뿜으면 피해가 세 배가 된다).
	const int32 Index = NextMuzzle % MuzzleLocals.Num();
	NextMuzzle = (NextMuzzle + 1) % MuzzleLocals.Num();
	const FVector Muzzle = Ship->GetActorTransform().TransformPosition(MuzzleLocals[Index]);
	const FVector Direction = (AimPoint - Muzzle).GetSafeNormal();

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipCannon), false, Ship.Get());
	const bool bHit = World->LineTraceSingleByChannel(Hit, Muzzle, Muzzle + Direction * Ship->CannonRange, ECC_Visibility, Params);
	const FVector Impact = bHit ? Hit.ImpactPoint : Muzzle + Direction * Ship->CannonRange;
	UAISense_Hearing::ReportNoiseEvent(World, Impact, 1.0f, Ship.Get(), 0.0f, TEXT("ShipCannon"));
	// 드래곤은 CannonHitsToKillDragon 발에 쓰러지게 체력의 몫을 그대로 준다(9/21 사용자: "레이저 빔 더 세게, 한 5 대 맞추면 쓰러뜨리게").
	//   주변 폭발 피해는 드래곤에게 따로 안 준다 — 더하면 5 발보다 빨리 죽는다.
	APGDragonBoss* HitDragon = bHit ? Cast<APGDragonBoss>(Hit.GetActor()) : nullptr;
	if (HitDragon && Ship->CannonHitsToKillDragon > 0)
	{
		UGameplayStatics::ApplyDamage(HitDragon, HitDragon->MaxHealth / Ship->CannonHitsToKillDragon, Shooter, Ship.Get(), UDamageType::StaticClass());
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: cannon hit the dragon — %.0f damage (1/%d of its health), dragon hp now %.0f"),
			HitDragon->MaxHealth / Ship->CannonHitsToKillDragon, Ship->CannonHitsToKillDragon, HitDragon->GetHealth());
	}
	else if (bHit)
		Explode(Impact, Hit.GetActor(), Shooter);
	// 그림(빔·번쩍임)은 모든 컴퓨터에서 — 멀티(9/27) 전에는 여기서 바로 그려 서버 화면에만 보였다.
	Ship->MulticastCannonFx(static_cast<uint8>(Index), Impact, HitDragon != nullptr);
	LastTargetName = GetNameSafe(Hit.GetActor());
	Ship->LastTargetDistanceM = FVector::Dist(Muzzle, Impact) * 0.01f;
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: cannon %d fired, hit=%s at %.0fm"), Index, *LastTargetName, Ship->LastTargetDistanceM);
	return true;
}

// 주포 한 발의 그림. 포구 자리는 이 컴퓨터의 배 위치로 다시 잰다 — 배는 움직이므로 서버가 잰 포구 좌표를 보내면 어긋난다.
void UPGShipWeapons::PlayCannonFx(int32 MuzzleIndex, const FVector& Impact, bool bBig)
{
	UWorld* World = Ship->GetWorld();
	if (!World || !MuzzleLocals.IsValidIndex(MuzzleIndex))
		return;
	SpawnCannonImpactFlash(Impact, bBig);
	if (!Ship->HasAuthority())
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: cannon fx on this screen (muzzle %d, impact %s)"), MuzzleIndex, *Impact.ToCompactString());
	// 빔: 포구에서 탄착점까지 늘린다. 0.4초 동안 TickBeamFade 가 가늘게 줄이다 끈다.
	if (Ship->CannonBeams.IsValidIndex(MuzzleIndex) && IsValid(Ship->CannonBeams[MuzzleIndex]))
	{
		const FVector Muzzle = Ship->GetActorTransform().TransformPosition(MuzzleLocals[MuzzleIndex]);
		UStaticMeshComponent* Component = Ship->CannonBeams[MuzzleIndex];
		const float Distance = FVector::Dist(Muzzle, Impact);
		Component->SetWorldRotation((Impact - Muzzle).GetSafeNormal().Rotation());
		Component->SetRelativeScale3D(FVector(Distance / 100.0f, 9.0f, 9.0f)); // 9/21: 3 -> 9. 쏘는 게 보여야 한다
		Component->SetVisibility(true);
		Ship->LastBeamTime = World->GetTimeSeconds();
	}
}

bool UPGShipWeapons::FireMissile(const FVector& AimPoint, AActor* AimActor, AController* Shooter)
{
	UWorld* World = Ship->GetWorld();
	if (!Ship->HasAuthority() || !World || MuzzleLocals.IsEmpty() || World->GetTimeSeconds() - LastMissileTime < Ship->MissileInterval)
		return false;
	UPGMissileSubsystem* Pool = UPGMissileSubsystem::Get(World);
	if (!Pool)
		return false;
	LastMissileTime = World->GetTimeSeconds();
	// 좌우 포구에서 한 발씩. 처음엔 조금 바깥으로 벌어져 나가 두 줄기로 보인다.
	int32 Fired = 0;
	for (int32 Index = 0; Index < FMath::Min(2, MuzzleLocals.Num()); ++Index)
	{
		const FVector Muzzle = Ship->GetActorTransform().TransformPosition(MuzzleLocals[Index]);
		const FVector ToAim = (AimPoint - Muzzle).GetSafeNormal();
		const FVector Spread = Ship->GetActorRightVector() * (Index == 0 ? 1.0f : -1.0f) * 0.25f;
		if (Pool->Launch(Muzzle, (ToAim + Spread).GetSafeNormal(), AimActor, Ship.Get(),
			Ship->MissileDirectDamage, Ship->MissileBlastDamage, Ship->MissileBlastRadius, AimPoint))
			++Fired;
	}
	UPGRunSubsystem::NotifyShotFired(Shooter, Fired); // 판 기록: 나간 미사일 수만큼
	LastTargetName = GetNameSafe(AimActor);
	Ship->LastTargetDistanceM = FVector::Dist(Ship->GetActorLocation(), AimPoint) * 0.01f;
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %d missiles away at %s (%.0fm, pool active=%d)"),
		Fired, *LastTargetName, Ship->LastTargetDistanceM, Pool->CountActive());
	return Fired > 0;
}

void UPGShipWeapons::Explode(const FVector& Location, AActor* DirectHit, AController* Shooter)
{
	UWorld* World = Ship->GetWorld();
	TArray<AActor*> Ignore = { Ship.Get() };
	if (IsValid(Ship->Hull))
		Ignore.Add(Ship->Hull);
	if (IsValid(DirectHit) && DirectHit != Ship.Get() && DirectHit != Ship->Hull)
		UGameplayStatics::ApplyDamage(DirectHit, Ship->CannonDirectDamage, Shooter, Ship.Get(), UDamageType::StaticClass());
	UGameplayStatics::ApplyRadialDamageWithFalloff(World, Ship->CannonBlastDamage, Ship->CannonBlastDamage * 0.2f, Location,
		Ship->CannonBlastRadius * 0.3f, Ship->CannonBlastRadius, 1.0f, UDamageType::StaticClass(), Ignore, Ship.Get(), Shooter, ECC_Visibility);
}
