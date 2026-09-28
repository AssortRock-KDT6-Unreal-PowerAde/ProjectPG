// APGDragonBoss — 공중전 — 솟아오르기, 선회·돌진, 표적 고르기, 불 뿜기.
// (2026-09-26 PGDragonBoss.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGDragonBossInternal.h"
#include "PGDragonAirCombat.h"

void UPGDragonAirCombat::BeginRise(const FVector& GroundSpot, APGBattleshipActor* InShip)
{
	Dragon->Ship = InShip;
	Dragon->bHadShip = IsValid(InShip); // 전함이 있었다는 기억. 나중에 전함이 지워져도 "추락했다" 로 친다(CheckShipDown)
	// 산 속에서 시작해 하늘로. 시작점을 땅 아래로 두면 "산이 갈라지며 솟는" 그림이 된다.
	// 얼마나 깊이 묻을 것인가: 원본 메시 높이 578cm × 배율 40 = 231m 짜리 몸이다. 90m 만 묻으면
	// 생기는 순간 이미 130m 가 지면 위로 튀어나와 있어서 "툭 나타난" 것으로 보인다(9/20 PIE).
	// 몸 전체가 잠기도록 키만큼 묻는다.
	RiseFrom = GroundSpot - FVector(0.0f, 0.0f, Dragon->BodyHeightCm * 1.1f);
	const float ShipZ = IsValid(Dragon->Ship) ? Dragon->Ship->GetActorLocation().Z : GroundSpot.Z + 20000.0f;
	RiseTo = FVector(GroundSpot.X, GroundSpot.Y, FMath::Max(ShipZ + 3000.0f, Dragon->MinAirborneZ())); // 배가 낮게 떠 있어도 땅 밑에서 멈추지 않게
	Dragon->SetActorLocation(RiseFrom);
	Dragon->EnterState(EPGDragonState::Rising);
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: rising from %s to %s"), *RiseFrom.ToCompactString(), *RiseTo.ToCompactString());
}

void UPGDragonAirCombat::TickRise(float DeltaSeconds)
{
	Dragon->KnockAround(Dragon->GetActorLocation(), Dragon->Hull->GetScaledCapsuleRadius() * 2.2f); // 땅을 뚫고 올라오는 동안 둘레가 날아간다
	// 이륙 동작은 한 번짜리라, 그대로 두면 날갯짓 한 번 하고 뻣뻣하게 솟아오른다(9/20 PIE).
	// 이륙 동작이 끝날 즈음부터 나는 동작으로 갈아타 계속 날갯짓하게 한다.
	if (Dragon->StateTimer > 1.2f && !Dragon->bFlapping)
	{
		Dragon->bFlapping = true;
		Dragon->PlayAnim(TEXT("FlyForwardAnim"), true);
	}
	const FVector To = RiseTo - Dragon->GetActorLocation();
	if (To.Size() < 500.0f)
	{
		OrbitAngle = 0.0f;
		Dragon->EnterState(EPGDragonState::Orbit);
		return;
	}
	// 솟아오르는 동안은 위로만. 3초쯤 걸린다.
	Dragon->SetActorLocation(Dragon->GetActorLocation() + To.GetSafeNormal() * Dragon->FlySpeed * 1.4f * DeltaSeconds);
	Dragon->SetActorRotation(FRotator(35.0f, Dragon->GetActorRotation().Yaw, 0.0f));
}

void UPGDragonAirCombat::TickOrbit(float DeltaSeconds)
{
	const FVector Centre = IsValid(Dragon->Ship) ? Dragon->Ship->GetActorLocation() : RiseTo;
	// 선회 반지름은 배 길이(437m)보다 커야 한다. 작으면 배 안을 파고들며 돈다(9/20 PIE).
	const float Radius = FMath::Max(Dragon->OrbitRadius, KeepDistanceCm(Dragon->Ship)); // 안전거리는 한 곳에서만 계산한다
	OrbitAngle += (Dragon->FlySpeed / FMath::Max(Radius, 1.0f)) * DeltaSeconds;
	// 위치에도 Radius 를 써야 한다. 전에는 각속도만 Radius 로 재고 위치는 OrbitRadius 로 잡아서,
	// OrbitRadius 를 줄이는 순간 "배 안을 파고들며 도는" 옛 증상이 그대로 돌아왔다(9/20 조사).
	FVector Want = Centre + FVector(FMath::Cos(OrbitAngle), FMath::Sin(OrbitAngle), 0.0f) * Radius
		+ FVector(0.0f, 0.0f, FMath::Sin(OrbitAngle * 0.7f) * 3000.0f);
	// 목표도 최저 높이 위로. 자리만 잘라 올리면 매 틱 내려가려다 잘려서 머리를 땅 쪽으로 숙인 채 난다(9/23).
	Want.Z = FMath::Max(Want.Z, Dragon->MinAirborneZ());
	Dragon->FaceAndMove(Want, Dragon->FlySpeed, DeltaSeconds);
	// 날갯짓과 활공을 번갈아. 계속 날갯짓만 하면 기계처럼 보인다(TerrorBringer 에는 활공 애니가 있다).
	if (Dragon->StateTimer - Dragon->LastGlideSwap > 2.5f)
	{
		Dragon->LastGlideSwap = Dragon->StateTimer;
		Dragon->bGliding = !Dragon->bGliding;
		Dragon->PlayAnim(Dragon->bGliding ? TEXT("FlyGlideAnim") : TEXT("FlyForwardAnim"), true);
	}
	if (Dragon->StateTimer <= Dragon->AttackIntervalSeconds)
		return;

	// 공격 차례. 몇 번 공격했으면 내려앉는다(땅 공격 + 약점).
	if (Dragon->AttacksBeforeLanding > 0 && Dragon->AttacksSinceLanding >= Dragon->AttacksBeforeLanding)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: %d air attacks since the last landing — going down"), Dragon->AttacksSinceLanding);
		Dragon->EnterState(EPGDragonState::Descend);
		return;
	}
	FString Why;
	AActor* Target = ChooseTarget(Why);
	if (!Target)
	{
		// 때릴 것이 없으면 한 바퀴 더 돈다. 여기서 가만히 멈추면 "고장 난 보스" 로 보인다.
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: no target (%s) — keep circling"), *Why);
		Dragon->StateTimer = 0.0f;
		Dragon->LastGlideSwap = 0.0f;
		return;
	}
	++Dragon->AttacksSinceLanding;
	Dragon->AttackTarget = Target;
	// 전함에게는 브레스와 돌진을 번갈아. 돌진은 "배 옆을 스쳐 지나가기" 라 배에게만 뜻이 있다 —
	// 몬스터·플레이어는 브레스로만 친다.
	const bool bPass = Target == Dragon->Ship.Get() && !bNextAttackIsBreath;
	bNextAttackIsBreath = !bNextAttackIsBreath;
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: attack %d/%d — %s on %s (%s)"),
		Dragon->AttacksSinceLanding, Dragon->AttacksBeforeLanding, bPass ? TEXT("pass") : TEXT("breath"), *GetNameSafe(Target), *Why);
	Dragon->EnterState(bPass ? EPGDragonState::Pass : EPGDragonState::Breath);
}

// 배 한가운데에서 이만큼은 떨어져 있어야 보이는 몸이 선체를 파고들지 않는다.
//
// 배 반길이(218m) + 드래곤이 원점에서 뻗은 거리(206m 남짓) + 여유 60m.
// 캡슐 반지름(60m)으로 재면 안 된다 — 맞는 몸(캡슐)과 보이는 몸(메시)이 세 배 넘게 다르다.
// 배 길이가 0 으로 오면(부품 조립 전) 안전거리가 266m 로 줄어 배 속으로 들어간다. 0 은 믿지 않는다.
float UPGDragonAirCombat::KeepDistanceCm(const APGBattleshipActor* Against) const
{
	if (!IsValid(Against))
		return 30000.0f;
	return FMath::Max(Against->GetShipLengthCm() * 0.5f, 25000.0f) + Dragon->HalfWingCm + 6000.0f;
}

void UPGDragonAirCombat::TickPass(float DeltaSeconds)
{
	if (Dragon->StateTimer < Dragon->TellSeconds)
		return; // 예고 중에는 제자리 날갯짓
	if (!Dragon->bAttackSwung)
	{
		// 이 드래곤의 공중 공격 동작은 FlyAttackAnim 하나뿐이다(위 LoadDragonAnim 주석).
		// 네 마리는 뼈 개수가 61/83/119/72 로 전부 달라 다른 드래곤 것을 가져올 수도 없다
		// (Tools/make_dragon_compat.py 가 그것을 확인한다).
		Dragon->bAttackSwung = true;
		Dragon->AttackAnimSeconds = Dragon->PlayAnim(TEXT("FlyAttackAnim"), false);
	}
	else if (Dragon->StateTimer > Dragon->TellSeconds + FMath::Max(1.6f, Dragon->AttackAnimSeconds) && !Dragon->bFlapping)
	{
		Dragon->bFlapping = true; // 공격 동작(한 번짜리)이 끝나면 다시 날갯짓으로
		Dragon->PlayAnim(TEXT("FlyForwardAnim"), true);
	}
	// 껍데기는 충돌이 없어서 그냥 통과해 버린다. 거리는 코드가 직접 지켜야 한다.
	const float Keep = KeepDistanceCm(Dragon->Ship);
	const float ShipHalfCm = IsValid(Dragon->Ship) ? FMath::Max(Dragon->Ship->GetShipLengthCm() * 0.5f, 25000.0f) : 0.0f;
	const FVector Centre = IsValid(Dragon->Ship) ? Dragon->Ship->GetActorLocation() : Dragon->PassTarget;
	const float Gap = FVector::Dist(Dragon->GetActorLocation(), Centre);
	Dragon->PassClosest = FMath::Min(Dragon->PassClosest, Gap);
	// 목표가 배 옆을 지나가는 점이라, 평소에는 그냥 그쪽으로 날면 된다.
	Dragon->FaceAndMove(Dragon->PassTarget, Dragon->ChargeSpeed, DeltaSeconds);
	// 안전망은 Tick 이 상태와 무관하게 건다(KeepClearOfShip). 여기서만 걸면 선회 중에는 안 걸린다.
	// 스치는 순간 한 번 긁는다.
	//
	// 기준이 120m 였는데 안전거리가 484m 라 **영영 들어올 수 없었다** — 이 피해가 한 번도 안 들어가고
	//   있었다(9/20 계산). 기준을 안전거리에 붙여 "가장 가까이 붙은 순간"에 들어가게 한다.
	if (!Dragon->bPassDamaged && IsValid(Dragon->Ship) && Gap < Keep * 1.1f)
	{
		Dragon->bPassDamaged = true;
		UGameplayStatics::ApplyDamage(Dragon->Ship, Dragon->PassDamage, nullptr, Dragon.Get(), UDamageType::StaticClass());
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: pass hit the ship for %.0f at %.0f m (ship hp %.0f)"),
			Dragon->PassDamage, Gap * 0.01f, Dragon->Ship->GetHealth());
	}
	// 지나쳤거나 3초가 지나면 다시 선회.
	if (Dragon->StateTimer > Dragon->TellSeconds + 3.0f || FVector::Dist(Dragon->GetActorLocation(), Dragon->PassTarget) < 2000.0f)
	{
		// 안전거리가 맞는지 PIE 로그에서 바로 대조할 수 있게 찍는다.
		// closest 가 keep 보다 작으면 위 안전망이 늦게 걸린 것이고, keep 보다 한참 크면 너무 멀리서 돈 것이다.
		UE_LOG(LogPGObjects, Display,
			TEXT("PGDragon: pass ended — closest %.0f m, keep %.0f m (ship half %.0f m + reach %.0f m + margin 60 m), hit=%d"),
			Dragon->PassClosest * 0.01f, Keep * 0.01f, ShipHalfCm * 0.01f, Dragon->HalfWingCm * 0.01f, Dragon->bPassDamaged ? 1 : 0);
		Dragon->EnterState(EPGDragonState::Orbit);
	}
}

// ─────────────────────────── 표적 ───────────────────────────

bool UPGDragonAirCombat::IsTargetAlive(const AActor* Target) const
{
	if (!IsValid(Target))
		return false;
	if (Target == Dragon->Ship.Get())
		return Dragon->Ship->GetHealth() > 0.0f;
	if (const APGMonsterCharacter* Monster = Cast<APGMonsterCharacter>(Target))
		return !Monster->IsDead();
	return true;
}

// 전함 > 밑에 보이는 몬스터 > 플레이어 (9/21 사용자: "드래곤은 함선 최우선, 그다음 밑에 몹들이라도 보이면 공격").
//
// 왜 이 순서인가: 이 싸움의 주인공은 전함과 드래곤이다. 전함이 닿는 거리에 있으면 무조건 전함.
//   전함이 없거나(부서졌거나) 플레이어가 몰고 멀리 도망가면, 그때 발밑의 몬스터를 태운다 — 하늘에서
//   빈손으로 빙빙 도는 것보다 "눈에 보이는 것은 다 태우는 짐승" 으로 읽힌다.
// "보이면" 을 지키려고 몬스터는 입에서 선 하나를 쏴서 가려져 있지 않은지 본다. 가까운 순서로
//   최대 5마리만 본다 — 공격 차례(6초)에 한 번 부르는 함수라 비싸지는 않지만, 몬스터가 수백이면 끝이 없다.
AActor* UPGDragonAirCombat::ChooseTarget(FString& OutWhy) const
{
	UWorld* World = Dragon->GetWorld();
	if (!IsValid(World))
	{
		OutWhy = TEXT("no world");
		return nullptr;
	}
	const FVector Here = Dragon->GetActorLocation();
	if (IsValid(Dragon->Ship) && Dragon->Ship->GetHealth() > 0.0f)
	{
		const float Distance = FVector::Dist(Here, Dragon->Ship->GetActorLocation());
		if (Distance <= Dragon->ShipEngageRangeCm)
		{
			OutWhy = FString::Printf(TEXT("ship %.0f m away, ship hp %.0f"), Distance * 0.01f, Dragon->Ship->GetHealth());
			return Dragon->Ship;
		}
		OutWhy = FString::Printf(TEXT("ship too far (%.0f m > %.0f m); "), Distance * 0.01f, Dragon->ShipEngageRangeCm * 0.01f);
	}
	else
	{
		OutWhy = IsValid(Dragon->Ship) ? TEXT("ship destroyed; ") : TEXT("no ship; ");
	}

	TArray<TPair<float, APGMonsterCharacter*>> Below;
	for (TActorIterator<APGMonsterCharacter> It(World); It; ++It)
	{
		if (It->IsDead())
			continue;
		const float Distance2DSq = FVector::DistSquared2D(It->GetActorLocation(), Here);
		if (Distance2DSq <= FMath::Square(Dragon->MonsterSeekRadiusCm))
			Below.Emplace(Distance2DSq, *It);
	}
	Below.Sort([](const TPair<float, APGMonsterCharacter*>& A, const TPair<float, APGMonsterCharacter*>& B) { return A.Key < B.Key; });
	const FVector Mouth = GetMouthLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDragonSight), false, Dragon.Get());
	if (IsValid(Dragon->Ship))
		Params.AddIgnoredActor(Dragon->Ship);
	int32 Checked = 0;
	for (const TPair<float, APGMonsterCharacter*>& Entry : Below)
	{
		if (++Checked > 5)
			break;
		FHitResult Hit;
		const bool bBlocked = World->LineTraceSingleByChannel(Hit, Mouth, Entry.Value->GetActorLocation(), ECC_Visibility, Params)
			&& Hit.GetActor() != Entry.Value;
		if (!bBlocked)
		{
			OutWhy += FString::Printf(TEXT("monster %s %.0f m out, in sight"), *Entry.Value->GetName(), FMath::Sqrt(Entry.Key) * 0.01f);
			return Entry.Value;
		}
	}
	if (Below.Num() > 0)
		OutWhy += FString::Printf(TEXT("%d monsters below but none in sight; "), Below.Num());

	APawn* Nearest = nullptr;
	float NearestSq = FMath::Square(Dragon->PlayerSeekRadiusCm);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
			continue;
		const float DistanceSq = FVector::DistSquared(Pawn->GetActorLocation(), Here);
		if (DistanceSq < NearestSq)
		{
			NearestSq = DistanceSq;
			Nearest = Pawn;
		}
	}
	if (Nearest)
	{
		OutWhy += FString::Printf(TEXT("player %s %.0f m away"), *Nearest->GetName(), FMath::Sqrt(NearestSq) * 0.01f);
		return Nearest;
	}
	OutWhy += TEXT("nobody in reach");
	return nullptr;
}

// 땅에 앉아 있을 때 몸을 돌릴 상대: 지면 공격 반경의 3배 안에서 가장 가까운 몬스터·플레이어.
AActor* UPGDragonAirCombat::FindGroundVictim() const
{
	UWorld* World = Dragon->GetWorld();
	if (!IsValid(World))
		return nullptr;
	const FVector Here = Dragon->GetActorLocation();
	AActor* Best = nullptr;
	float BestSq = FMath::Square(Dragon->GroundAttackRadiusCm * 3.0f + Dragon->HalfWingCm);
	for (TActorIterator<APGMonsterCharacter> It(World); It; ++It)
	{
		const float DistanceSq = FVector::DistSquared2D(It->GetActorLocation(), Here);
		if (!It->IsDead() && DistanceSq < BestSq)
		{
			BestSq = DistanceSq;
			Best = *It;
		}
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
			continue;
		const float DistanceSq = FVector::DistSquared2D(Pawn->GetActorLocation(), Here);
		if (DistanceSq < BestSq)
		{
			BestSq = DistanceSq;
			Best = Pawn;
		}
	}
	return Best;
}

FVector UPGDragonAirCombat::GetMouthLocation() const
{
	if (!Dragon->MouthBone.IsNone() && IsValid(Dragon->Mesh))
		return Dragon->Mesh->GetSocketLocation(Dragon->MouthBone);
	// 뼈가 없으면 몸 앞쪽·위쪽 어림값. 머리는 몸 앞끝 근처, 발에서 몸 높이의 절반쯤에 있다.
	return Dragon->GetActorLocation() + Dragon->GetActorForwardVector() * Dragon->HalfWingCm * 0.6f
		+ FVector(0.0f, 0.0f, Dragon->BodyHeightCm * 0.5f - Dragon->FeetOffsetCm());
}

// 전함은 한가운데를 겨누면 안 된다 — 한가운데는 안전거리(484m) 너머라 사거리(450m) 밖이다.
// 입에서 가장 가까운 선체 표면을 겨눈다. 전함 크기는 브레스 시작 때 잰 상자(ShipLocalBox)를 쓴다.
FVector UPGDragonAirCombat::AimPointOf(const AActor* Target) const
{
	if (!IsValid(Target))
		return Dragon->GetActorLocation() + Dragon->GetActorForwardVector() * Dragon->BreathRangeCm;
	if (Target == Dragon->Ship.Get() && Dragon->ShipLocalBox.IsValid)
	{
		const FTransform& ShipTransform = Dragon->Ship->GetActorTransform();
		const FVector LocalMouth = ShipTransform.InverseTransformPosition(GetMouthLocation());
		return ShipTransform.TransformPosition(Dragon->ShipLocalBox.GetClosestPointTo(LocalMouth));
	}
	return Target->GetActorLocation();
}

// ─────────────────────────── 브레스 ───────────────────────────

void UPGDragonAirCombat::TickBreath(float DeltaSeconds)
{
	AActor* Target = Dragon->AttackTarget.Get();
	if (!IsTargetAlive(Target))
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: breath target gone (%s) — back to circling"), *GetNameSafe(Target));
		Dragon->EnterState(EPGDragonState::Orbit);
		return;
	}
	const FVector Mouth = GetMouthLocation();
	const FVector Aim = AimPointOf(Target);
	const float Distance = FVector::Dist(Mouth, Aim);
	if (!Dragon->bBreathing)
	{
		// 예고 중: 표적 쪽으로 돌고, 사거리 밖이면 다가간다(전함 쪽은 KeepClearOfShip 이 안전거리를 지킨다).
		if (Distance > Dragon->BreathRangeCm * 0.85f)
			Dragon->FaceAndMove(Aim, Dragon->FlySpeed, DeltaSeconds);
		else
			Dragon->FaceToward(Aim, DeltaSeconds);
		if (Dragon->StateTimer < Dragon->TellSeconds)
			return;
		if (Distance > Dragon->BreathRangeCm)
		{
			// 6초 안에 못 닿으면 포기한다. 끝없이 쫓아가면 배 둘레를 벗어나 화면에서 사라진다.
			if (Dragon->StateTimer > Dragon->TellSeconds + 6.0f)
			{
				UE_LOG(LogPGObjects, Display, TEXT("PGDragon: breath gave up — %s still %.0f m from the mouth (reach %.0f m)"),
					*GetNameSafe(Target), Distance * 0.01f, Dragon->BreathRangeCm * 0.01f);
				Dragon->EnterState(EPGDragonState::Orbit);
			}
			return;
		}
		Dragon->bBreathing = true;
		BreathStartTime = Dragon->StateTimer;
		LastBreathTick = Dragon->StateTimer - Dragon->BreathTraceInterval; // 첫 판정은 곧바로
		Dragon->AttackAnimSeconds = Dragon->PlayAnim(TEXT("FlyAttackAnim"), false);
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: breath fire on %s, %.0f m from the mouth"), *GetNameSafe(Target), Distance * 0.01f);
	}
	Dragon->FaceToward(Aim, DeltaSeconds);
	const FVector Dir = (Aim - Mouth).GetSafeNormal();
	// 이펙트 위치는 매 프레임 따라가게 둔다(몸이 도는데 불이 제자리에 남으면 이상하다). 위치만 옮기는 것이라 싸다.
	Dragon->SetBreath(true, Mouth, Dir, FMath::Min(Dragon->BreathRangeCm, Distance + 2000.0f)); // 멀티(9/27): 모든 화면에
	// 피해 판정은 간격을 둔다. 지나간 시간만큼 곱해 주니 프레임이 튀어도 총 피해는 같다.
	if (Dragon->StateTimer - LastBreathTick >= Dragon->BreathTraceInterval)
	{
		const float Elapsed = Dragon->StateTimer - LastBreathTick;
		LastBreathTick = Dragon->StateTimer;
		BreathDamageTick(Mouth, Dir, Elapsed);
	}
	// 공중 공격 동작(한 번짜리)이 끝나면 제자리 날갯짓으로. 불은 계속 뿜는다.
	if (!Dragon->bFlapping && Dragon->StateTimer - BreathStartTime > FMath::Max(Dragon->AttackAnimSeconds, 0.5f))
	{
		Dragon->bFlapping = true;
		Dragon->PlayAnim(TEXT("FlyIdleAnim"), true);
	}
	if (Dragon->StateTimer - BreathStartTime >= Dragon->BreathSeconds)
	{
		UE_LOG(LogPGObjects, Display,
			TEXT("PGDragon: breath ended — %d checks, ship burned %d times (ship hp %.0f), monsters %d, players %d"),
			Dragon->BreathTickCount, Dragon->BreathShipHits, IsValid(Dragon->Ship) ? Dragon->Ship->GetHealth() : 0.0f, Dragon->BreathMonsterHits, Dragon->BreathPlayerHits);
		Dragon->EnterState(EPGDragonState::Orbit);
	}
}

// 입에서 Dir 로 뻗은 원뿔(끝으로 갈수록 넓어진다) 안에 든 것에 피해를 준다.
//
// 왜 물리 쓸기(Sweep)를 안 쓰나: 450m 짜리 쓸기는 땅의 타일·건물 인스턴스를 수십 개씩 건드려 비싸고,
//   드래곤이 태울 것은 정해져 있다(전함, 브레스 시작 때 모은 몬스터·플레이어). 정해진 몇 개만 수학으로 잰다.
//   - 전함: 전함 기준 좌표로 옮긴 선분이 선체 상자(불길 굵기만큼 부풀린)를 지나가나.
//   - 몸들: 중심선에서 옆으로 떨어진 거리가 그 지점의 불길 반지름 + 몸 반지름 안인가.
//   선 검사는 "불이 닿은 자리에 먼지" 를 띄우는 데 한 번만 쓴다(0.4초마다).
void UPGDragonAirCombat::BreathDamageTick(const FVector& Mouth, const FVector& Dir, float Elapsed)
{
	++Dragon->BreathTickCount;
	const float TanHalf = FMath::Tan(FMath::DegreesToRadians(Dragon->BreathHalfAngleDeg));
	const FVector End = Mouth + Dir * Dragon->BreathRangeCm;

	if (IsValid(Dragon->Ship) && Dragon->Ship->GetHealth() > 0.0f && Dragon->ShipLocalBox.IsValid)
	{
		const FTransform& ShipTransform = Dragon->Ship->GetActorTransform();
		// 상자를 불길 굵기만큼 부풀린다. 끝의 굵기 전부를 쓰면 입 근처에서 너무 후하니 절반만.
		const float PadCm = (BreathStartRadiusCm + Dragon->BreathRangeCm * TanHalf * 0.5f)
			/ FMath::Max(ShipTransform.GetScale3D().GetMax(), KINDA_SMALL_NUMBER);
		const FVector LocalStart = ShipTransform.InverseTransformPosition(Mouth);
		const FVector LocalEnd = ShipTransform.InverseTransformPosition(End);
		if (FMath::LineBoxIntersection(Dragon->ShipLocalBox.ExpandBy(PadCm), LocalStart, LocalEnd, LocalEnd - LocalStart))
		{
			UGameplayStatics::ApplyDamage(Dragon->Ship, Dragon->BreathShipDamagePerSecond * Elapsed, nullptr, Dragon.Get(), UDamageType::StaticClass());
			++Dragon->BreathShipHits;
		}
	}

	for (const TWeakObjectPtr<AActor>& Weak : Dragon->BreathCandidates)
	{
		AActor* Victim = Weak.Get();
		if (Victim == Dragon->Ship.Get() || !IsTargetAlive(Victim))
			continue;
		const FVector ToVictim = Victim->GetActorLocation() - Mouth;
		const float Along = FVector::DotProduct(ToVictim, Dir);
		if (Along < 0.0f || Along > Dragon->BreathRangeCm)
			continue;
		const float Side = (ToVictim - Dir * Along).Size();
		if (Side > BreathStartRadiusCm + Along * TanHalf + Victim->GetSimpleCollisionRadius())
			continue;
		const APawn* AsPawn = Cast<APawn>(Victim);
		const bool bPlayer = AsPawn && AsPawn->IsPlayerControlled();
		const float PerSecond = bPlayer ? Dragon->BreathPlayerDamagePerSecond : Dragon->BreathMonsterDamagePerSecond;
		UGameplayStatics::ApplyDamage(Victim, PerSecond * Elapsed, nullptr, Dragon.Get(), UDamageType::StaticClass());
		++(bPlayer ? Dragon->BreathPlayerHits : Dragon->BreathMonsterHits);
	}

	if (Dragon->ImpactFxAsset && (Dragon->BreathTickCount % 2) == 1)
	{
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDragonBreath), false, Dragon.Get());
		if (Dragon->GetWorld()->LineTraceSingleByChannel(Hit, Mouth, End, ECC_Visibility, Params))
			Dragon->PlayImpactFx(Hit.ImpactPoint, 4.0f);
	}
}

// 불꽃: 횃불 불꽃 다섯 개를 입에서 앞으로 늘어놓고, 멀수록 크게 한다(원뿔처럼 보이게).
// 조각은 처음 한 번만 만들고 켰다 껐다만 한다 — 브레스마다 새로 만들면 그때마다 끊긴다.
// 멀티(9/27): 서버는 APGDragonBoss::SetBreath 를 부르고, 모든 화면이 복제된 BreathCue 로 이 함수를 부른다.
void UPGDragonAirCombat::SetBreathFx(bool bOn, const FVector& From, const FVector& Dir, float Length)
{
	if (!bOn)
	{
		for (UParticleSystemComponent* Piece : BreathFx)
			if (IsValid(Piece) && Piece->IsActive())
				Piece->Deactivate();
		if (IsValid(BreathCone))
			BreathCone->SetVisibility(false);
		return;
	}
	// 불길 몸통: 입에서 표적 쪽으로 뻗는 원뿔 하나. 횃불 입자는 월드 위쪽으로 타오르는 불이라, 돌려 놔도 불꽃이 하늘로 솟아
	//   "하늘에 대고 브레스를 뿜는" 것처럼 보였다(9/21 사용자). 방향이 확실히 읽히도록 원뿔을 깔고 입자는 그 위의 장식으로 둔다.
	// 엔진 기본 원뿔은 높이 100cm·밑면 반지름 50cm·피벗 한가운데·뾰족한 끝이 +Z. 끝을 입에 두려면 +Z 를 입 쪽(-Dir)으로 돌린다.
	if (!IsValid(BreathCone))
	{
		BreathCone = NewObject<UStaticMeshComponent>(Dragon.Get(), TEXT("BreathCone"));
		if (UStaticMesh* Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")))
			BreathCone->SetStaticMesh(Cone);
		if (UMaterialInterface* Flame = Dragon->BreathFlameMaterial.IsNull() ? nullptr : Dragon->BreathFlameMaterial.LoadSynchronous())
			BreathCone->SetMaterial(0, Flame);
		BreathCone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BreathCone->SetCastShadow(false);
		BreathCone->SetCanEverAffectNavigation(false);
		BreathCone->SetUsingAbsoluteLocation(true);
		BreathCone->SetUsingAbsoluteRotation(true);
		BreathCone->SetUsingAbsoluteScale(true);
		BreathCone->SetupAttachment(Dragon->Hull);
		BreathCone->RegisterComponent();
	}
	{
		const float EndRadius = FMath::Tan(FMath::DegreesToRadians(Dragon->BreathHalfAngleDeg)) * Length;
		BreathCone->SetWorldLocationAndRotation(From + Dir * Length * 0.5f, FRotationMatrix::MakeFromZ(-Dir).Rotator());
		BreathCone->SetWorldScale3D(FVector(EndRadius / 50.0f, EndRadius / 50.0f, Length / 100.0f));
		BreathCone->SetVisibility(true);
	}
	if (!Dragon->FireFxAsset)
		return; // 팩이 없다. BeginPlay 로그에 fire fx=no 로 이미 남겼다
	if (BreathFx.Num() == 0)
	{
		for (int32 Index = 0; Index < BreathFxPieces; ++Index)
		{
			UParticleSystemComponent* Piece = NewObject<UParticleSystemComponent>(Dragon.Get());
			Piece->bAutoActivate = false;
			Piece->SetTemplate(Dragon->FireFxAsset);
			Piece->SetUsingAbsoluteLocation(true);
			Piece->SetUsingAbsoluteRotation(true);
			Piece->SetUsingAbsoluteScale(true);
			Piece->SetupAttachment(Dragon->Hull);
			Piece->RegisterComponent();
			BreathFx.Add(Piece);
		}
	}
	// 횃불 불꽃은 위(+Z)로 타오른다. 그 위쪽을 불길 방향에 맞춘다.
	const FRotator Along = FRotationMatrix::MakeFromZ(Dir).Rotator();
	for (int32 Index = 0; Index < BreathFx.Num(); ++Index)
	{
		UParticleSystemComponent* Piece = BreathFx[Index];
		if (!IsValid(Piece))
			continue;
		const float Fraction = static_cast<float>(Index + 1) / static_cast<float>(BreathFx.Num());
		Piece->SetWorldLocationAndRotation(From + Dir * Length * Fraction * 0.9f, Along);
		Piece->SetWorldScale3D(FVector(Dragon->BreathFxScale * (0.4f + Fraction)));
		if (!Piece->IsActive())
			Piece->Activate(true);
	}
}
