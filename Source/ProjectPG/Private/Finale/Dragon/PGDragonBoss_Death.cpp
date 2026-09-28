// APGDragonBoss — 죽음 — 죽는 동작, 추락, 땅에 부딪힘과 먼지.
// (2026-09-26 PGDragonBoss.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGDragonBossInternal.h"
#include "PGDragonDeath.h"
#include "PGDragonGroundCombat.h"

// ─────────────────────────── 죽음 ───────────────────────────
//
// 9/21 사용자: "쓰러트렸는데 너무 맥없이 떨어진다. 심지어 산 밑으로 꺼진다. 산이 부서지는 것도 아니고."
//
// 네 단계로 나눈다(EPGDragonDeathStep):
//   Struck  — 마지막 한 방. 피격 동작(비명)을 틀고, 맞은 반대쪽으로 튕기며 몸(메시)이 잘게 떨린다. 머리가 젖혀진다.
//             액터 원점은 안 흔든다 — 매 프레임 무작위로 원점을 옮기면 떨림이 아니라 "걸어간다".
//   Falling — 날갯짓이 멎고(활공 자세로 굳는다) 머리부터 기울어 한쪽으로 돌며 점점 빨리 떨어진다.
//             옆으로도 밀려간다: 목표는 **타일이 깔린 범위 안쪽**(BeginDeathFall). 맵 밖 산 위에서 죽으면
//             떨어지는 시간 안에 맵 안으로 들어오도록 옆 속도를 계산한다.
//   Crashed — 발이 땅에 닿는 순간: 죽는 동작, 흙먼지 여러 개(세 번에 걸쳐 번진다), 폭발 구, 둘레 소품 날리기
//             (몇 초에 걸쳐 프레임당 몇 개씩), 카메라 흔들림.
//   Resting — 누운 채 남는다. CorpseSeconds 가 0 보다 크면 그만큼 뒤에 **그냥 사라진다**(가라앉히지 않는다).
//
// 왜 "산 밑으로 꺼졌나": 배경 산(MountainHISM)과 맵 둘레 바닥판은 충돌이 없다. 아래로 쏜 땅 찾기 선이 산을
//   그대로 뚫고 지나가 아무것도 못 맞히면, 옛 코드는 "원점 아래 2km" 를 땅으로 쳤다. 그래서 산 속으로 사라졌다.
//   이제 못 찾으면 기준 높이(z=20)를 쓰되, 맵 밖에서는 발을 능선 높이(OutsideMapHoldCm) 아래로 안 내리고
//   맵 안쪽으로 미끄러진 뒤에 떨어진다.

void UPGDragonDeath::TickDying(float DeltaSeconds)
{
	switch (Dragon->DeathStep)
	{
	case EPGDragonDeathStep::Struck:
	{
		const float T = Dragon->DeathStrikeSeconds > 0.0f ? FMath::Clamp(Dragon->StateTimer / Dragon->DeathStrikeSeconds, 0.0f, 1.0f) : 1.0f;
		// 튕김은 금방 잦아들고, 끝으로 갈수록 조금씩 가라앉기 시작한다(떨어지기 직전의 "주저앉음").
		Dragon->DeathDrift *= FMath::Max(0.0f, 1.0f - 2.5f * DeltaSeconds);
		Dragon->SetActorLocation(Dragon->GetActorLocation() + (Dragon->DeathDrift - FVector(0.0f, 0.0f, 800.0f * T)) * DeltaSeconds);
		// 머리를 젖히고 좌우로 요동친다. 요동은 시간이 갈수록 작아진다.
		const FRotator Want(18.0f, Dragon->GetActorRotation().Yaw, FMath::Sin(Dragon->StateTimer * 26.0f) * 12.0f * (1.0f - T));
		Dragon->SetActorRotation(FMath::RInterpTo(Dragon->GetActorRotation(), Want, DeltaSeconds, 6.0f));
		// 몸 떨림: 메시의 상대 위치만 흔든다. 반날개폭의 2.5% (206m 몸이면 ±5m) 에서 시작해 0 으로.
		const float Shake = Dragon->HalfWingCm * 0.025f * (1.0f - T);
		Dragon->DeathJitter = FVector(FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake, Shake), FMath::FRandRange(-Shake * 0.5f, Shake * 0.5f));
		if (IsValid(Dragon->Mesh))
			Dragon->Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -MeshBelowOriginCm) + Dragon->DeathJitter);
		if (Dragon->StateTimer >= Dragon->DeathStrikeSeconds)
			BeginDeathFall();
		return;
	}
	case EPGDragonDeathStep::Falling:
	{
		Dragon->FallSpeed = FMath::Min(Dragon->FallSpeed + Dragon->DeathFallAccel * DeltaSeconds, Dragon->DeathFallMaxSpeed);
		const FVector Here = Dragon->GetActorLocation();
		// 목표 자리를 지나쳤으면 옆 속도를 최소로 줄인다 — 맵 반대편까지 미끄러지지 않게.
		const FVector2D ToTarget = DeathTarget - FVector2D(Here.X, Here.Y);
		if (FVector2D::DotProduct(ToTarget, FVector2D(Dragon->DeathDrift.X, Dragon->DeathDrift.Y)) <= 0.0f && Dragon->DeathDrift.Size() > Dragon->DeathDriftMinSpeed)
			Dragon->DeathDrift = Dragon->DeathDrift.GetSafeNormal() * Dragon->DeathDriftMinSpeed;
		// 땅 높이는 자주 다시 잰다 — 옆으로 밀려가는 동안 발밑이 바뀐다(건물 지붕, 호수, 맵 밖).
		if (Dragon->StateTimer - Dragon->LastGroundProbe > 0.1f)
		{
			Dragon->LastGroundProbe = Dragon->StateTimer;
			float Found = 0.0f;
			const bool bFound = Dragon->GroundCombat->FindGroundZ(Here, Found);
			if (bFound != !Dragon->bDeathOutsideMap)
			{
				UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death fall — %s at %s"),
					bFound ? TEXT("ground found again, dropping") : TEXT("no ground below (outside the tiles), holding above the ridge line and sliding inward"),
					*Here.ToCompactString());
			}
			Dragon->bDeathOutsideMap = !bFound;
			Dragon->GroundZ = bFound ? Found : GroundDatumZ;
		}
		// 맵 밖에서는 발을 능선 높이 아래로 안 내리고, 옆 속도를 최대로 올려 맵 안으로 들어온다.
		const float Floor = Dragon->bDeathOutsideMap ? GroundDatumZ + OutsideMapHoldCm : Dragon->GroundZ;
		FVector Next = Here + (Dragon->DeathDrift - FVector(0.0f, 0.0f, Dragon->FallSpeed)) * DeltaSeconds;
		const float FeetNext = Next.Z - Dragon->FeetOffsetCm();
		// 버티는 시간은 6초까지(예전 40초). 방향은 매번 목표 자리 쪽으로 다시 잡는다.
		// 왜: 예전에는 그때 날던 방향 그대로 최대 속도를 줘서, 목표를 지나친 드래곤이 맵을 가로질러 반대편 밖까지 150m/s 로
		//   미끄러졌고 40초 동안 "날개 펴고 활공" 하다 시간 초과로 떨어졌다(9/22 PIE: "추락이 너무 오래 걸려").
		if (FeetNext <= Floor && Dragon->bDeathOutsideMap && Dragon->StateTimer < 6.0f)
		{
			Next.Z = Floor + Dragon->FeetOffsetCm();
			Dragon->FallSpeed = 0.0f;
			const FVector2D Toward = (DeathTarget - FVector2D(Here.X, Here.Y)).GetSafeNormal();
			Dragon->DeathDrift = FVector(Toward.X, Toward.Y, 0.0f) * Dragon->DeathDriftMaxSpeed;
		}
		Dragon->SetActorLocation(Next);
		// 자세: 머리부터 처박히며 한쪽으로 기울고, 천천히 돈다. 도는 것은 보간이 아니라 직접 더한다 —
		//   보간 목표에 "지금 각도 + 조금" 을 주면 매 프레임 그 조금의 일부만 움직여 거의 안 돈다.
		FRotator Turned = FMath::RInterpTo(Dragon->GetActorRotation(), FRotator(-55.0f, Dragon->GetActorRotation().Yaw, 65.0f * DeathRollSign), DeltaSeconds, 1.4f);
		Turned.Yaw += DeathSpinDegPerSec * DeltaSeconds;
		Dragon->SetActorRotation(Turned);
		if (Next.Z - Dragon->FeetOffsetCm() <= Floor && !Dragon->bDeathOutsideMap)
		{
			CrashLand(true);
		}
		else if (Dragon->StateTimer >= 10.0f || (Dragon->bDeathOutsideMap && Dragon->StateTimer >= 6.0f && Next.Z - Dragon->FeetOffsetCm() <= GroundDatumZ + 100.0f))
		{
			// 보험: 제때 못 닿았다(맵 안으로 못 들어온 경우). 그 자리에서 처박는다 — 영영 떠 있는 것보다 낫다.
			// 40초 → 10초(9/22). 맵 밖이면 6초 버틴 뒤 땅 높이에 닿는 순간 바로 처박는다(산 밑으로 계속 떨어지지 않게).
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death fall timed out at %s — crashing where it is"), *Next.ToCompactString());
			Dragon->GroundZ = Dragon->bDeathOutsideMap ? GroundDatumZ : Dragon->GroundZ;
			CrashLand(true);
		}
		return;
	}
	case EPGDragonDeathStep::Crashed:
	{
		const float Since = Dragon->StateTimer - CrashTime;
		const FVector Feet = Dragon->GetActorLocation() - FVector(0.0f, 0.0f, Dragon->FeetOffsetCm());
		// 폭발 구: CrashBlastSeconds 동안 부풀며(처음이 빠르게), 끝나면 숨긴다. 반지름이 반날개폭의 0.4배(약 80m).
		if (IsValid(CrashBlast) && CrashBlast->IsVisible())
		{
			const float T = FMath::Clamp(Since / CrashBlastSeconds, 0.0f, 1.0f);
			const float MaxScale = Dragon->HalfWingCm * 0.4f / CrashBlastMeshRadiusCm;
			CrashBlast->SetWorldScale3D(FVector(FMath::Lerp(3.0f, MaxScale, FMath::Sqrt(T))));
			if (T >= 1.0f)
				CrashBlast->SetVisibility(false);
		}
		// 먼지가 두 번 더 번진다.
		if (Dragon->CrashDustWave == 1 && Since > 0.35f)
			SpawnCrashDust(Feet, 1);
		else if (Dragon->CrashDustWave == 2 && Since > 0.9f)
			SpawnCrashDust(Feet, 2);
		// 소품은 몇 초에 걸쳐 날린다(KnockAround 가 0.15초마다 5개씩만 처리한다).
		if (Since < Dragon->CrashKnockSeconds)
			Dragon->KnockAround(Feet, Dragon->HalfWingCm * 0.6f);
		if (Since >= FMath::Max(DieAnimSeconds, 1.0f))
		{
			Dragon->DeathStep = EPGDragonDeathStep::Resting;
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death — body at rest at %s (%s)"), *Feet.ToCompactString(),
				Dragon->CorpseSeconds > 0.0f ? *FString::Printf(TEXT("vanishing in %.0fs, no sinking"), Dragon->CorpseSeconds) : TEXT("staying for good"));
		}
		return;
	}
	case EPGDragonDeathStep::Resting:
		// 누워 있다. Z 는 여기서 절대 안 건드린다 — 가라앉는 그림은 금지(9/21).
		if (Dragon->CorpseSeconds > 0.0f && Dragon->StateTimer - CrashTime - FMath::Max(DieAnimSeconds, 1.0f) >= Dragon->CorpseSeconds)
		{
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death — corpse removed after %.0fs"), Dragon->CorpseSeconds);
			Dragon->Destroy();
		}
		return;
	}
}

// Struck → Falling. 떨어질 자리와 옆 속도를 여기서 정한다.
void UPGDragonDeath::BeginDeathFall()
{
	Dragon->DeathStep = EPGDragonDeathStep::Falling;
	Dragon->DeathJitter = FVector::ZeroVector;
	if (IsValid(Dragon->Mesh))
		Dragon->Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -MeshBelowOriginCm)); // 떨림을 거둔다. FeetOffsetCm 이 이 값을 읽는다
	const FVector Here = Dragon->GetActorLocation();
	FVector Forward = Dragon->GetActorForwardVector().GetSafeNormal2D();
	if (Forward.IsNearlyZero())
		Forward = FVector(1.0f, 0.0f, 0.0f);

	// 떨어질 자리: 타일이 깔린 범위 안쪽. 몸이 크니 가장자리에서 반날개폭의 0.6배 + 20m 더 안으로 —
	//   원점이 안에 있어도 날개·꼬리가 산에 걸치지 않게. 범위가 그보다 좁으면 한가운데.
	FBox2D Map;
	const bool bMapKnown = Dragon->ResolveMapBox(Map);
	if (!bMapKnown)
		Map = FallbackMapBox;
	const FVector2D Centre = Map.GetCenter();
	const FVector2D Extent = Map.GetExtent();
	const float InsetCm = Dragon->HalfWingCm * 0.6f + 2000.0f;
	const FVector2D SafeExtent(FMath::Max(Extent.X - InsetCm, 0.0f), FMath::Max(Extent.Y - InsetCm, 0.0f));
	// 지금 가던 방향으로 조금 더 나간 자리를 원하되, 안전 범위로 자른다. 밖에 있으면 자연히 가장 가까운 안쪽 가장자리다.
	const FVector2D Want(Here.X + Forward.X * Dragon->HalfWingCm * 0.5f, Here.Y + Forward.Y * Dragon->HalfWingCm * 0.5f);
	DeathTarget = FVector2D(
		FMath::Clamp(Want.X, Centre.X - SafeExtent.X, Centre.X + SafeExtent.X),
		FMath::Clamp(Want.Y, Centre.Y - SafeExtent.Y, Centre.Y + SafeExtent.Y));

	// 그 자리의 땅 높이(못 찾으면 기준 20). 떨어지는 동안에도 다시 재지만 첫 계산은 여기 값으로.
	float TargetGround = 0.0f;
	if (!Dragon->GroundCombat->FindGroundZ(FVector(DeathTarget.X, DeathTarget.Y, Here.Z), TargetGround))
		TargetGround = GroundDatumZ;
	Dragon->GroundZ = TargetGround;
	const float Height = FMath::Max(Here.Z - Dragon->FeetOffsetCm() - TargetGround, 0.0f);
	// 떨어지는 데 걸리는 시간 = 가속 구간 + (있으면) 최고 속도 구간. 옆 속도는 "거리 ÷ 이 시간" — 닿기 전에 도착한다.
	const float Accel = FMath::Max(Dragon->DeathFallAccel, 1.0f);
	const float MaxSpeed = FMath::Max(Dragon->DeathFallMaxSpeed, 1.0f);
	const float AccelDistance = MaxSpeed * MaxSpeed / (2.0f * Accel);
	const float FallTime = Height <= AccelDistance
		? FMath::Sqrt(2.0f * Height / Accel)
		: MaxSpeed / Accel + (Height - AccelDistance) / MaxSpeed;
	const FVector2D To = DeathTarget - FVector2D(Here.X, Here.Y);
	const float Distance = To.Size();
	const float Speed = FMath::Clamp(FallTime > 0.05f ? Distance / FallTime : Dragon->DeathDriftMaxSpeed, Dragon->DeathDriftMinSpeed, Dragon->DeathDriftMaxSpeed);
	const FVector2D Dir = Distance > 1.0f ? To / Distance : FVector2D(Forward.X, Forward.Y);
	Dragon->DeathDrift = FVector(Dir.X, Dir.Y, 0.0f) * Speed;
	DeathRollSign = FMath::RandBool() ? 1.0f : -1.0f;
	DeathSpinDegPerSec = DeathRollSign * 30.0f;
	Dragon->FallSpeed = 0.0f;
	Dragon->LastGroundProbe = Dragon->StateTimer;
	Dragon->bDeathOutsideMap = false;
	Dragon->PlayAnim(*Dragon->DeathFallAnimName, true); // 기본 FlyGlideAnim: 날갯짓이 멎고 편 채로 굳는다
	UE_LOG(LogPGObjects, Display,
		TEXT("PGDragon: death fall — from %s toward (%.0f, %.0f) m, %.0f m sideways and %.0f m down: drift %.0f m/s for ~%.1fs (map %s %.0f..%.0f / %.0f..%.0f m, target ground z=%.0f)"),
		*Here.ToCompactString(), DeathTarget.X * 0.01f, DeathTarget.Y * 0.01f, Distance * 0.01f, Height * 0.01f,
		Speed * 0.01f, FallTime, bMapKnown ? TEXT("from tiles") : TEXT("fallback box"),
		Map.Min.X * 0.01f, Map.Max.X * 0.01f, Map.Min.Y * 0.01f, Map.Max.Y * 0.01f, TargetGround);
}

// 발이 땅에 닿았다. bFromTheSky 가 거짓이면 땅에서 맞아 죽은 것 — 처박히는 연출 없이 그 자리에서 쓰러진다.
void UPGDragonDeath::CrashLand(bool bFromTheSky)
{
	Dragon->bOnGround = true;
	Dragon->DeathStep = EPGDragonDeathStep::Crashed;
	CrashTime = Dragon->StateTimer;
	Dragon->CrashDustWave = 0;
	Dragon->FallSpeed = 0.0f;
	Dragon->DeathDrift = FVector::ZeroVector;
	if (IsValid(Dragon->Mesh))
		Dragon->Mesh->SetRelativeLocation(FVector(0.0f, 0.0f, -MeshBelowOriginCm));
	const FVector Here = Dragon->GetActorLocation();
	// 죽는 동작은 선 자세에서 시작한다. 기울어진 채 틀면 땅에 비스듬히 박힌다.
	Dragon->SetActorLocationAndRotation(FVector(Here.X, Here.Y, Dragon->GroundZ + Dragon->FeetOffsetCm()), FRotator(0.0f, Dragon->GetActorRotation().Yaw, 0.0f));
	DieAnimSeconds = Dragon->PlayAnim(*Dragon->DeathCrashAnimName, false); // 기본 DieAnim. 땅에 닿았으니(bOnGround) 지상 동작이 허용된다
	const FVector Feet = Dragon->GetActorLocation() - FVector(0.0f, 0.0f, Dragon->FeetOffsetCm());
	if (!bFromTheSky)
	{
		Dragon->PlayImpactFx(Feet, 10.0f);
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death — killed on the ground at %s, collapsing where it stands (%.1fs)"),
			*Feet.ToCompactString(), DieAnimSeconds);
		return;
	}
	SpawnCrashDust(Feet, 0); // 폭발 구·흔들림·첫 먼지(모든 화면 — PlayCrashFx)
	Dragon->KnockAround(Feet, Dragon->HalfWingCm * 0.6f); // 이어서 CrashKnockSeconds 동안 Tick 이 계속 부른다
	UE_LOG(LogPGObjects, Display,
		TEXT("PGDragon: death — crashed at %s (ground z=%.0f, %s): death motion %.1fs, knocking props for %.1fs, blast + camera shake sent to every screen"),
		*Feet.ToCompactString(), Dragon->GroundZ, Dragon->bDeathOutsideMap ? TEXT("outside the tiles!") : TEXT("on the tiles"),
		DieAnimSeconds, Dragon->CrashKnockSeconds);
}

// 처박힘의 그림. 서버(혼자 하는 판 포함)와 클라이언트가 모두 여기로 온다. Feet.Z 는 땅 높이다.
void UPGDragonDeath::PlayCrashFx(const FVector& Feet, int32 Wave)
{
	if (Wave != 0)
	{
		PlayCrashDust(Feet, Wave);
		return;
	}
	LocalCrashAt = Dragon->GetWorld() ? Dragon->GetWorld()->GetTimeSeconds() : 0.0;
	// 폭발 구. 처음 한 번만 만들고 켰다 끈다.
	if (!IsValid(CrashBlast))
	{
		CrashBlast = NewObject<UStaticMeshComponent>(Dragon.Get(), TEXT("CrashBlast"));
		if (UStaticMesh* Ball = Dragon->CrashBlastMesh.IsNull() ? nullptr : Dragon->CrashBlastMesh.LoadSynchronous())
			CrashBlast->SetStaticMesh(Ball);
		if (UMaterialInterface* Glow = Dragon->CrashBlastMaterial.IsNull() ? nullptr : Dragon->CrashBlastMaterial.LoadSynchronous())
			CrashBlast->SetMaterial(0, Glow);
		CrashBlast->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		CrashBlast->SetCastShadow(false);
		CrashBlast->SetCanEverAffectNavigation(false);
		CrashBlast->SetUsingAbsoluteLocation(true);
		CrashBlast->SetUsingAbsoluteRotation(true);
		CrashBlast->SetUsingAbsoluteScale(true);
		CrashBlast->SetupAttachment(Dragon->Hull);
		CrashBlast->RegisterComponent();
	}
	const bool bBlast = IsValid(CrashBlast) && CrashBlast->GetStaticMesh() != nullptr;
	if (bBlast)
	{
		CrashBlast->SetWorldLocation(Feet + FVector(0.0f, 0.0f, Dragon->HalfWingCm * 0.12f));
		CrashBlast->SetWorldScale3D(FVector(3.0f));
		CrashBlast->SetVisibility(true);
	}
	// 카메라 흔들림: 반날개폭의 두 배(약 400m) 안은 최대 세기, 2.5km 까지 줄어든다 — 맵이 600m 라 누구나 느낀다.
	UGameplayStatics::PlayWorldCameraShake(Dragon.Get(), UPGDragonCrashShake::StaticClass(), Feet, Dragon->HalfWingCm * 2.0f, 250000.0f, 1.0f);
	PlayCrashDust(Feet, 0);
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: crash on this screen — blast=%d, camera shake"), bBlast ? 1 : 0);
}

// 클라이언트의 폭발 구: 서버 TickDying(Crashed) 과 같은 식으로 부풀리고 끝나면 숨긴다.
void UPGDragonDeath::TickCrashBlastLocal()
{
	if (LocalCrashAt < 0.0 || !IsValid(CrashBlast) || !CrashBlast->IsVisible() || !Dragon->GetWorld())
		return;
	const float T = FMath::Clamp(static_cast<float>(Dragon->GetWorld()->GetTimeSeconds() - LocalCrashAt) / CrashBlastSeconds, 0.0f, 1.0f);
	const float MaxScale = Dragon->HalfWingCm * 0.4f / CrashBlastMeshRadiusCm;
	CrashBlast->SetWorldScale3D(FVector(FMath::Lerp(3.0f, MaxScale, FMath::Sqrt(T))));
	if (T >= 1.0f)
		CrashBlast->SetVisibility(false);
}

// 처박힌 자리 둘레의 흙먼지. 땅 높이(GroundZ)에 띄운다 — 발 위치는 원점 기준이라 조금 떠 있을 수 있다.
void UPGDragonDeath::SpawnCrashDust(const FVector& Feet, int32 Wave)
{
	Dragon->CrashDustWave = Wave + 1;
	// 그림은 모든 화면에서(멀티 9/27). 먼지는 땅 높이(GroundZ)에 띄우므로 발밑 Z 를 땅으로 맞춰 보낸다.
	Dragon->MulticastCrashFx(FVector(Feet.X, Feet.Y, Dragon->GroundZ), static_cast<uint8>(Wave));
}

void UPGDragonDeath::PlayCrashDust(const FVector& Feet, int32 Wave)
{
	if (!Dragon->ImpactFxAsset)
		return; // BeginPlay 로그에 impact fx=no 로 이미 남겼다
	const FVector Forward = Dragon->GetActorForwardVector().GetSafeNormal2D();
	const FVector Side = FVector::CrossProduct(Forward, FVector::UpVector);
	int32 Puffs = 0;
	auto Puff = [&](const FVector& At, float Size)
	{
		UGameplayStatics::SpawnEmitterAtLocation(Dragon.Get(), Dragon->ImpactFxAsset, FVector(At.X, At.Y, Feet.Z), FRotator::ZeroRotator, FVector(Size), true, EPSCPoolMethod::AutoRelease);
		++Puffs;
	};
	switch (Wave)
	{
	case 0:
		// 닿는 순간: 발밑이 가장 크고, 머리·꼬리 쪽으로 줄지어, 양옆에도.
		Puff(Feet, 18.0f);
		Puff(Feet + Forward * Dragon->HalfWingCm * 0.35f, 13.0f);
		Puff(Feet + Forward * Dragon->HalfWingCm * 0.7f, 11.0f);
		Puff(Feet - Forward * Dragon->HalfWingCm * 0.35f, 12.0f);
		Puff(Feet - Forward * Dragon->HalfWingCm * 0.7f, 10.0f);
		Puff(Feet + Side * Dragon->HalfWingCm * 0.4f, 10.0f);
		Puff(Feet - Side * Dragon->HalfWingCm * 0.4f, 10.0f);
		break;
	case 1:
		for (int32 Index = 0; Index < 4; ++Index)
			Puff(Feet + FVector(FMath::FRandRange(-0.8f, 0.8f), FMath::FRandRange(-0.8f, 0.8f), 0.0f) * Dragon->HalfWingCm, FMath::FRandRange(8.0f, 12.0f));
		break;
	default:
		for (int32 Index = 0; Index < 3; ++Index)
			Puff(Feet + FVector(FMath::FRandRange(-1.0f, 1.0f), FMath::FRandRange(-1.0f, 1.0f), 0.0f) * Dragon->HalfWingCm, FMath::FRandRange(6.0f, 9.0f));
		break;
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: death — dust wave %d, %d puffs"), Wave, Puffs);
}
