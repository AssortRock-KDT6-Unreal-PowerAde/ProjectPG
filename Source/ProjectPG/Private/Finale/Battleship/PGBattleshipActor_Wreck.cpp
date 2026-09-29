// APGBattleshipActor — 추락 — 격추된 뒤 떨어지고 부서지는 연출.
// (2026-09-26 PGBattleshipActor.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGBattleshipActorInternal.h"
#include "PGShipWreck.h"
#include "PGShipWeapons.h"
#include "PGShipHelm.h"
#include "PGShipHullBuilder.h"

// ─────────────────────────── 추락(체력 0) ───────────────────────────
//
// 무엇인가(9/21 사용자): "전함 체력이 0 이 되면 바닥에 불나면서 추락. 그러면 드래곤도 지면에 따라와서 그때부터는
//   지면에서 총 맞고 싸우는 거지." 드래곤 쪽 지상전은 PGDragonBoss 가 맡는다 — 여기서는 배만.
// 드래곤과의 약속은 하나뿐이다: Ship->GetHealth() 가 0 이하로 "유지" 된다(TakeDamage). IsWrecked() 는 덤이다.
//
// 흐름: BeginWreck(한 번) → TickWreck(매 프레임, 떨어지는 동안) → WreckTouchdown(한 번) → TickWreck(땅 위, 소품 날리기 몇 초).
//  1) 체력 0: 조종석의 사람을 일으키고(카메라가 제 몸으로 돌아간다) 조종·주포 입력을 끊는다. 선체 여러 곳(4~6)에서
//     폭발 구 + 흙먼지가 터지고 그 자리마다 불이 붙는다. 떨어지는 동안 몇 초마다 한 번 더 터진다.
//  2) 배는 물리가 아니라 좌표 이동이다(헤더 주석). 그래서 추락도 물리를 안 켠다 — 218m 짜리 복합 충돌을 물리로
//     떨어뜨리면 Chaos 가 매 프레임 그 전부를 다시 넣었다 뺐다 한다(잔해 때 겪은 NarrowPhase 폭발). 대신 낙하 속도를
//     직접 쌓고(WreckFallAccel), 뱃머리가 숙으며 한쪽으로 기울고(WreckPitchDeg/RollDeg), 앞으로 조금 미끄러진다(WreckSlideSpeed).
//  3) 땅은 배 바로 밑을 선으로 잰다(FindGroundZBelow). 산·둘레판은 충돌이 없어 못 찾으면 z=20(GroundDatumZ)으로 친다.
//     선체 바닥이 땅보다 조금 아래(WreckBuryRatio)까지 내려오면 멈춘다 — 큰 폭발 + 선체 길이 방향의 흙먼지 + 둘레 소품 날리기
//     + 카메라 흔들림(드래곤 추락과 같은 흔들림 클래스). 불은 WreckFiresAfterCrash 개만 남기고 끈다.
//  4) 배 안의 사람·차가 같이 떨어지다 다치지 않게 하는 것까지는 안 한다(9/21 사용자: 욕심 안 내도 된다). 대신 추락이
//     시작될 때 배 안에 있던 플레이어 폰을 로그로 남긴다 — 나중에 "떨어질 때 어디 있었나" 를 되짚을 수 있게.
//     갑판 위의 물리 차는 TickDeckCargo 가 "배가 움직이는 동안" 붙드는 규칙 그대로 배와 함께 내려온다.
// 비용: 불 파티클 4~6개(닿은 뒤 3개) + 몇 초에 한 번 폭발 구 액터 하나 + 땅에서 0.3초마다 소품 몇 개. 4060 에서 문제없다.
void UPGShipWreck::BeginWreck(AActor* Killer, float LastHit)
{
	if (Ship->bWrecked)
		return;
	Ship->bWrecked = true;
	Ship->bWreckGrounded = false;
	Ship->Health = 0.0f;
	// Cruise/Hover 분기가 더는 위치를 잡지 않는다 — 추락은 TickWreck 이 맡는다. HasArrived() 도 false 가 되지만
	// 디렉터는 이륙 단계에서만 그것을 보고, 그때는 아직 드래곤이 없어 체력이 깎일 일이 없다.
	Ship->Motion = EPGShipMotion::Parked;
	WreckElapsed = 0.0f;
	WreckFallSpeed = 0.0f;
	WreckPopCount = 0;
	WreckNextPopIn = Ship->WreckPopInterval;
	WreckGroundScanTime = 0.0f;
	WreckRollSign = FMath::RandBool() ? 1.0f : -1.0f;
	Ship->HelmThrottle = 0.0f;
	Ship->HelmYawRate = 0.0f;

	// 1) 조종석: 앉아 있던 사람을 일으킨다(카메라 복귀 + 걷기 복구). 이 뒤로 TickSeat 은 bWrecked 를 보고 앉기를 막는다.
	const FString PilotName = Ship->bSeated ? GetNameSafe(Ship->SeatedPawn) : FString();
	if (Ship->bSeated)
		Ship->Helm->StandUpFromSeat(Ship->SeatedPawn, TEXT("ship wrecked"));
	// 주포 빔이 켜진 채 떨어지면 안 된다.
	Ship->LastBeamTime = -100.0;
	for (UStaticMeshComponent* Component : Ship->CannonBeams)
		if (IsValid(Component))
			Component->SetVisibility(false);

	// 2) 배 안의 플레이어 폰(사람 또는 사람이 탄 차)을 적어 둔다 — 선체 상자 안(조금 부풀려서)에 있는 것만.
	TArray<FString> Aboard;
	if (UWorld* World = Ship->GetWorld())
	{
		const FTransform& ToWorld = Ship->GetActorTransform();
		const FBox Inside = Ship->HullLocalBounds.IsValid ? Ship->HullLocalBounds.ExpandBy(500.0f) : FBox(FVector(-5000.0f), FVector(5000.0f));
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			const APlayerController* PC = It->Get();
			const APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
			if (!IsValid(Pawn))
				continue;
			const FVector Local = ToWorld.InverseTransformPosition(Pawn->GetActorLocation());
			if (Inside.IsInside(Local))
				Aboard.Add(FString::Printf(TEXT("%s at local (%.0f, %.0f, %.0f)"), *Pawn->GetName(), Local.X, Local.Y, Local.Z));
		}
	}

	// 3) 땅. 못 찾으면(맵 밖·산 위) z=20 기준으로 떨어진다.
	bWreckGroundFound = FindGroundZBelow(Ship->GetActorLocation(), WreckGroundZ);
	if (!bWreckGroundFound)
		WreckGroundZ = ShipGroundDatumZ;

	// 4) 불·폭발. 에셋은 한 번만 찾아 둔다(팩이 없는 PC 는 nullptr — 이펙트 없이 떨어지기만 한다).
	WreckFireFx = Ship->WreckFireEffect.IsNull() ? nullptr : Ship->WreckFireEffect.LoadSynchronous();
	WreckDustFx = Ship->WreckDustEffect.IsNull() ? nullptr : Ship->WreckDustEffect.LoadSynchronous();
	const int32 Fires = FMath::RandRange(FMath::Min(Ship->WreckFireMin, Ship->WreckFireMax), FMath::Max(Ship->WreckFireMin, Ship->WreckFireMax));
	for (int32 Index = 0; Index < Fires; ++Index)
		SpawnWreckPop(Ship->HullBuilder->RandomHullLocal(), true);

	const float BottomZ = static_cast<float>(Ship->GetActorLocation().Z + (Ship->HullLocalBounds.IsValid ? Ship->HullLocalBounds.Min.Z : 0.0));
	UE_LOG(LogPGObjects, Display,
		TEXT("PGBattleship: wreck — hull breached by %s (last hit %.0f), falling from %.0f m over the ground (ground z %.0f, %s) with %d fire(s) (fire fx=%s, dust fx=%s); pilot %s; %d player pawn(s) aboard: %s"),
		*GetNameSafe(Killer), LastHit, (BottomZ - WreckGroundZ) * 0.01f, WreckGroundZ,
		bWreckGroundFound ? TEXT("traced") : TEXT("not found, using z=20"),
		Fires, WreckFireFx ? TEXT("yes") : TEXT("no"), WreckDustFx ? TEXT("yes") : TEXT("no"),
		PilotName.IsEmpty() ? TEXT("nobody was seated") : *FString::Printf(TEXT("%s stood up, helm input cut"), *PilotName),
		Aboard.Num(), Aboard.IsEmpty() ? TEXT("none") : *FString::Join(Aboard, TEXT("; ")));
	Ship->OnWrecked.Broadcast(Ship.Get());
}

// 선체 한 자리(배 기준)의 폭발: 부푸는 구 + 흙먼지(연기 자리). bWithFire 면 그 자리에 불을 붙여 배와 함께 움직이게 둔다.
// 흙먼지는 월드에 한 번 터지고 사라지므로 배가 떨어지는 동안 뒤에 남아 연기 꼬리처럼 보인다 — 따로 붙일 이유가 없다.
void UPGShipWreck::SpawnWreckPop(const FVector& Local, bool bWithFire)
{
	++WreckPopCount;
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: wreck — pop %d at local (%.0f, %.0f, %.0f)%s"),
		WreckPopCount, Local.X, Local.Y, Local.Z, bWithFire ? TEXT(" + fire") : TEXT(""));
	// 그림은 모든 컴퓨터에서(멀티 9/27) — 불은 배에 붙는 부품이라 컴퓨터마다 제 배에 붙인다.
	Ship->MulticastWreckPop(Local, bWithFire);
}

void UPGShipWreck::PlayWreckPopFx(const FVector& Local, bool bWithFire)
{
	// 클라이언트는 BeginWreck 을 돌지 않으니 에셋을 여기서 처음 찾는다.
	if (!WreckFireFx && !Ship->WreckFireEffect.IsNull())
		WreckFireFx = Ship->WreckFireEffect.LoadSynchronous();
	if (!WreckDustFx && !Ship->WreckDustEffect.IsNull())
		WreckDustFx = Ship->WreckDustEffect.LoadSynchronous();
	const FVector Where = Ship->GetActorTransform().TransformPosition(Local);
	Ship->Weapons->SpawnBlastBall(Where, FMath::FRandRange(14.0f, 22.0f), 0.45f);
	if (WreckDustFx)
		UGameplayStatics::SpawnEmitterAtLocation(Ship.Get(), WreckDustFx, Where, FRotator::ZeroRotator, FVector(5.0f), true, EPSCPoolMethod::AutoRelease);
	if (bWithFire && WreckFireFx && WreckFires.Num() < FMath::Max(Ship->WreckFireMin, Ship->WreckFireMax))
	{
		// 배에 붙인다(RootScene 은 배율 1 이라 상대 크기 = 월드 크기). 횃불 불꽃은 위(+Z)로 타오르니 돌릴 필요가 없다.
		UParticleSystemComponent* Fire = NewObject<UParticleSystemComponent>(Ship.Get());
		Fire->SetTemplate(WreckFireFx);
		Fire->SetupAttachment(Ship->RootScene);
		Fire->SetRelativeLocation(Local);
		Fire->SetRelativeScale3D(FVector(Ship->WreckFireScale));
		Fire->SetCastShadow(false);
		Fire->RegisterComponent();
		Fire->Activate(true);
		WreckFires.Add(Fire);
	}
}

// 배 바로 아래 땅. 땅(타일·건물)은 WorldStatic 이라 그것만 본다 — 채널(Visibility)로 쏘면 갑판 위의 차·사람에 먼저 맞는다.
// 배 자신, 껍데기와 그 부품(자식 액터), 갑판 차는 뺀다. 산·둘레판은 충돌이 없어 그 위에서는 false 다(드래곤 FindGroundZ 와 같은 한계).
bool UPGShipWreck::FindGroundZBelow(const FVector& From, float& OutZ) const
{
	UWorld* World = Ship->GetWorld();
	if (!IsValid(World))
		return false;
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipWreckGround), false, Ship.Get());
	if (IsValid(Ship->Hull))
	{
		Params.AddIgnoredActor(Ship->Hull);
		TArray<AActor*> Parts;
		Ship->Hull->GetAttachedActors(Parts, true, true);
		Params.AddIgnoredActors(Parts);
	}
	if (IsValid(Ship->DeckVehicle))
		Params.AddIgnoredActor(Ship->DeckVehicle);
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, From, From - FVector(0.0f, 0.0f, 300000.0f), Objects, Params))
		return false;
	OutZ = static_cast<float>(Hit.ImpactPoint.Z);
	return true;
}

// 둘레 소품을 날린다. 드래곤 KnockAround / 디렉터 KnockGroundUnderShip 과 같은 요령 — 한 번에 Budget 개만.
// 배 본체와 "배에 붙어 있는 것"(껍데기 부품 31개, 갑판 차)은 뺀다. Hit.Item 을 꼭 채운다 — 안 채우면 맵 어디에 있든 0번 인스턴스가 뜯긴다(9/20 조사).
void UPGShipWreck::KnockPropsAround(const FVector& Where, float Reach, int32 Budget)
{
	UWorld* World = Ship->GetWorld();
	if (!IsValid(World) || Budget <= 0)
		return;
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectTypes;
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectTypes.AddObjectTypesToQuery(ECC_WorldDynamic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipWreckKnock), false, Ship.Get());
	World->OverlapMultiByObjectType(Overlaps, Where, FQuat::Identity, ObjectTypes, FCollisionShape::MakeSphere(Reach), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (Budget <= 0)
			break;
		UPrimitiveComponent* Component = Overlap.GetComponent();
		AActor* PropOwner = IsValid(Component) ? Component->GetOwner() : nullptr;
		if (!IsValid(Component) || !IsValid(PropOwner) || PropOwner == Ship.Get() || PropOwner->IsAttachedTo(Ship.Get()) || PropOwner->GetOwner() == Ship.Get()
			|| PropOwner == Ship->DeckVehicle)
			continue;
		FHitResult Hit;
		Hit.Item = Overlap.ItemIndex;
		Hit.ImpactPoint = Component->GetComponentLocation();
		Hit.Location = Hit.ImpactPoint;
		Hit.Component = Component;
		Hit.HitObjectHandle = FActorInstanceHandle(PropOwner);
		const FVector Away = (Hit.ImpactPoint - Where).GetSafeNormal2D() * 1400.0f + FVector(0.0f, 0.0f, 1800.0f);
		if (PGPhysicsUtil::TryKnockProp(Component, Hit, Away, Ship.Get(), 1200.0f, 250.0f))
			--Budget;
	}
}

void UPGShipWreck::TickWreck(float DeltaSeconds)
{
	WreckElapsed += DeltaSeconds;
	const float HullHeight = Ship->HullLocalBounds.IsValid ? static_cast<float>(Ship->HullLocalBounds.GetSize().Z) : 0.0f;
	const float HullHalfLength = FMath::Max(Ship->GetShipLengthCm() * 0.5f, 3000.0f);

	if (Ship->bWreckGrounded)
	{
		// 땅 위: 몇 초 동안 0.3초마다 둘레 소품을 몇 개씩 더 날린다(한 번에 다 털면 프레임이 죽는다). 그 뒤로는 불만 탄다.
		if (WreckKnockLeft > 0.0f)
		{
			WreckKnockLeft -= DeltaSeconds;
			WreckKnockTimer += DeltaSeconds;
			if (WreckKnockTimer >= 0.3f)
			{
				WreckKnockTimer = 0.0f;
				const FVector Feet(Ship->GetActorLocation().X, Ship->GetActorLocation().Y, WreckGroundZ);
				KnockPropsAround(Feet, HullHalfLength, 3);
			}
		}
		return;
	}

	// 떨어지는 동안 몇 초마다 한 번 더 터진다(불은 더 안 붙인다 — 처음 붙은 것이 계속 탄다).
	WreckNextPopIn -= DeltaSeconds;
	if (WreckNextPopIn <= 0.0f)
	{
		WreckNextPopIn = Ship->WreckPopInterval * FMath::FRandRange(0.7f, 1.3f);
		SpawnWreckPop(Ship->HullBuilder->RandomHullLocal(), false);
	}
	// 앞으로 미끄러지므로 밑의 땅이 바뀐다(건물 지붕 위로 갈 수도 있다). 0.5초마다 다시 잰다 — 매 프레임 쏠 이유는 없다.
	WreckGroundScanTime += DeltaSeconds;
	if (WreckGroundScanTime >= 0.5f)
	{
		WreckGroundScanTime = 0.0f;
		float GroundZ = 0.0f;
		if (FindGroundZBelow(Ship->GetActorLocation(), GroundZ))
		{
			bWreckGroundFound = true;
			WreckGroundZ = GroundZ;
		}
	}

	// 가속 낙하 + 기울기 + 앞으로 미끄러짐.
	WreckFallSpeed = FMath::Min(WreckFallSpeed + Ship->WreckFallAccel * DeltaSeconds, Ship->WreckMaxFallSpeed);
	const FRotator Now = Ship->GetActorRotation();
	const FRotator Want(-Ship->WreckPitchDeg, Now.Yaw, WreckRollSign * Ship->WreckRollDeg); // 뱃머리 숙임 = 음의 피치
	const FRotator NewRotation = FMath::RInterpTo(Now, Want, DeltaSeconds, 0.6f); // 218m 짜리가 천천히 기운다
	FVector Location = Ship->GetActorLocation() + FRotator(0.0f, Now.Yaw, 0.0f).Vector() * (Ship->WreckSlideSpeed * DeltaSeconds);
	Location.Z -= WreckFallSpeed * DeltaSeconds;
	// 선체 바닥(배 원점 + Min.Z)이 땅보다 조금 아래(파묻힘)까지 내려오면 멈춘다.
	const float MinZ = Ship->HullLocalBounds.IsValid ? static_cast<float>(Ship->HullLocalBounds.Min.Z) : 0.0f;
	const float StopBottomZ = WreckGroundZ - HullHeight * Ship->WreckBuryRatio;
	if (Location.Z + MinZ <= StopBottomZ)
	{
		Location.Z = StopBottomZ - MinZ;
		Ship->SetActorLocationAndRotation(Location, NewRotation);
		WreckTouchdown();
		return;
	}
	Ship->SetActorLocationAndRotation(Location, NewRotation);
}

// 땅에 닿는 순간 한 번. 드래곤 추락(PGDragonBoss::BeginDeathCrash/SpawnCrashDust)과 같은 그림으로 맞췄다 —
// 같은 판에서 둘 다 떨어지니 폭발·먼지·흔들림이 서로 다른 언어로 보이면 안 된다.
void UPGShipWreck::WreckTouchdown()
{
	Ship->bWreckGrounded = true;
	WreckKnockLeft = Ship->WreckKnockSeconds;
	WreckKnockTimer = 0.0f;
	const FVector Centre = Ship->GetActorLocation();
	const FVector Feet(Centre.X, Centre.Y, WreckGroundZ);
	const float HullHalfLength = FMath::Max(Ship->GetShipLengthCm() * 0.5f, 3000.0f);
	// 폭발·먼지·흔들림·불 줄이기는 모든 컴퓨터에서(멀티 9/27). 흔들림도 "이 컴퓨터의 카메라" 라 서버에서만 부르면 아무도 못 느꼈다.
	Ship->MulticastWreckTouchdown(Feet);
	// 둘레 소품 첫 물결(서버). 나머지는 TickWreck 이 WreckKnockSeconds 동안 이어서 날린다.
	KnockPropsAround(Feet, HullHalfLength, 6);
	UE_LOG(LogPGObjects, Display,
		TEXT("PGBattleship: wreck — hit the ground at %s after %.1fs (%.0f m/s, ground z %.0f %s): blast r=45 m, dust + camera shake sent to every screen, knocking props for %.1fs"),
		*Feet.ToCompactString(), WreckElapsed, WreckFallSpeed * 0.01f, WreckGroundZ,
		bWreckGroundFound ? TEXT("traced") : TEXT("not found, used z=20"), Ship->WreckKnockSeconds);
}

// 땅에 닿는 순간의 그림. 발밑(Feet)만 받고 방향·길이는 이 컴퓨터의 배에서 다시 잰다.
void UPGShipWreck::PlayTouchdownFx(const FVector& Feet)
{
	if (!WreckDustFx && !Ship->WreckDustEffect.IsNull())
		WreckDustFx = Ship->WreckDustEffect.LoadSynchronous();
	const float HullHalfLength = FMath::Max(Ship->GetShipLengthCm() * 0.5f, 3000.0f);
	const FVector Forward = Ship->GetActorForwardVector().GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(Forward, FVector::UpVector);

	// 큰 폭발 구: 선체 한가운데, 땅에서 조금 위. 주포 탄착(12~25m)보다 훨씬 크고 길게.
	Ship->Weapons->SpawnBlastBall(Feet + FVector(0.0f, 0.0f, 1500.0f), 45.0f, 0.8f);
	// 흙먼지: 발밑이 가장 크고 뱃머리·꼬리 쪽으로 줄지어, 양옆에도(드래곤 dust wave 0 과 같은 배치).
	int32 Puffs = 0;
	auto Puff = [&](const FVector& At, float Size)
	{
		if (!WreckDustFx)
			return;
		UGameplayStatics::SpawnEmitterAtLocation(Ship.Get(), WreckDustFx, FVector(At.X, At.Y, Feet.Z), FRotator::ZeroRotator, FVector(Size), true, EPSCPoolMethod::AutoRelease);
		++Puffs;
	};
	Puff(Feet, 20.0f);
	Puff(Feet + Forward * HullHalfLength * 0.35f, 14.0f);
	Puff(Feet + Forward * HullHalfLength * 0.7f, 12.0f);
	Puff(Feet - Forward * HullHalfLength * 0.35f, 14.0f);
	Puff(Feet - Forward * HullHalfLength * 0.7f, 12.0f);
	Puff(Feet + Side * HullHalfLength * 0.25f, 10.0f);
	Puff(Feet - Side * HullHalfLength * 0.25f, 10.0f);
	// 카메라 흔들림: 배 길이 안은 최대 세기, 2.5km 까지 줄어든다 — 맵이 600m 라 어디서든 느낀다.
	UGameplayStatics::PlayWorldCameraShake(Ship.Get(), UPGDragonCrashShake::StaticClass(), Feet, HullHalfLength * 2.0f, 250000.0f, 1.0f);
	// 불은 몇 개만 남긴다(비용). 앞쪽(먼저 붙은 것)부터 남기고 뒤는 끈다.
	int32 Trimmed = 0;
	for (int32 Index = WreckFires.Num() - 1; Index >= FMath::Max(0, Ship->WreckFiresAfterCrash); --Index)
	{
		if (UParticleSystemComponent* Fire = WreckFires[Index]; IsValid(Fire))
		{
			Fire->Deactivate();
			Fire->DestroyComponent();
			++Trimmed;
		}
		WreckFires.RemoveAt(Index);
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: wreck touchdown on this screen — %d dust puff(s), %d fire(s) keep burning (%d put out)"),
		Puffs, WreckFires.Num(), Trimmed);
}
