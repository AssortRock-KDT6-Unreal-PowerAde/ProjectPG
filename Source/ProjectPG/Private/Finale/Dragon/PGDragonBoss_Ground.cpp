// APGDragonBoss — 지상전 — 내려앉기, 땅에서 싸우기·걷기·뛰기, 다시 날아오르기.
// (2026-09-26 PGDragonBoss.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGDragonBossInternal.h"
#include "PGDragonGroundCombat.h"
#include "PGDragonAirCombat.h"

// ─────────────────────────── 내려앉기·땅·이륙 ───────────────────────────

bool UPGDragonGroundCombat::FindGroundZ(const FVector& From, float& OutZ) const
{
	UWorld* World = Dragon->GetWorld();
	if (!IsValid(World))
		return false;
	// 땅(타일·지형)은 WorldStatic 이다. 채널(Visibility)로 쏘면 전함·몬스터·차에 먼저 맞아 그 위에 앉는다.
	FCollisionObjectQueryParams Objects(ECC_WorldStatic);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDragonLand), false, Dragon.Get());
	if (IsValid(Dragon->Ship))
		Params.AddIgnoredActor(Dragon->Ship);
	FHitResult Hit;
	if (!World->LineTraceSingleByObjectType(Hit, From, From - FVector(0.0f, 0.0f, 300000.0f), Objects, Params))
		return false;
	OutZ = Hit.ImpactPoint.Z;
	return true;
}

void UPGDragonGroundCombat::TickDescend(float DeltaSeconds)
{
	Dragon->SetActorRotation(FMath::RInterpTo(Dragon->GetActorRotation(), FRotator(0.0f, Dragon->GetActorRotation().Yaw, 0.0f), DeltaSeconds, 2.0f));
	// 앉을 자리가 따로 있으면(지상전·뛰어 옮기기) 착지 동작 전까지 그쪽으로 수평으로 날아간다.
	// 가는 동안 발밑이 바뀌므로(건물·호수·맵 밖) 땅 높이를 0.2초마다 다시 잰다 — 죽음 추락과 같은 규칙.
	float SpotDistance = 0.0f;
	if (Dragon->bDescendToSpot && !Dragon->bLandingAnimStarted)
	{
		const FVector Here = Dragon->GetActorLocation();
		const FVector2D To = Dragon->DescendSpot - FVector2D(Here.X, Here.Y);
		SpotDistance = To.Size();
		if (SpotDistance > 500.0f)
		{
			const FVector2D Step = To / SpotDistance * FMath::Min(Dragon->FlySpeed * DeltaSeconds, SpotDistance);
			Dragon->SetActorLocation(Here + FVector(Step.X, Step.Y, 0.0f));
			Dragon->FaceToward(FVector(Dragon->DescendSpot.X, Dragon->DescendSpot.Y, Here.Z), DeltaSeconds);
		}
		if (Dragon->StateTimer - Dragon->LastGroundProbe > 0.2f)
		{
			Dragon->LastGroundProbe = Dragon->StateTimer;
			float Found = 0.0f;
			if (FindGroundZ(Dragon->GetActorLocation(), Found))
				Dragon->GroundZ = Found;
		}
	}
	const float Feet = Dragon->GetActorLocation().Z - Dragon->FeetOffsetCm();
	const float Height = Feet - Dragon->GroundZ;
	if (!Dragon->bLandingAnimStarted)
	{
		// 착지 동작은 "땅 바로 위에서 내려앉는" 동작이다. 높은 데서 틀면 끝나는 순간 허공에 앉은 자세가 된다.
		// 그래서 활공으로 충분히 내려온 다음(몸 높이의 LandingStartHeightRatio 배)에 튼다.
		const float StartHeight = Dragon->BodyHeightCm * Dragon->LandingStartHeightRatio;
		if (Height > StartHeight)
		{
			Dragon->SetActorLocation(Dragon->GetActorLocation() - FVector(0.0f, 0.0f, FMath::Min(Dragon->FlySpeed * DeltaSeconds, Height - StartHeight)));
		}
		else if (Dragon->bDescendToSpot && SpotDistance > 500.0f)
		{
			// 자리까지 아직 멀다. 이 높이로 날아가서 앉는다(위 수평 이동이 하고 있다).
		}
		else
		{
			Dragon->bLandingAnimStarted = true;
			const float Length = Dragon->PlayAnim(TEXT("LandAnim"), false);
			// 발이 닿는 순간을 동작 안의 시점(LandingTouchdownFraction)에 맞춘다: 남은 높이 ÷ 그때까지의 시간.
			const float Touchdown = FMath::Max(0.3f, Length * Dragon->LandingTouchdownFraction);
			Dragon->DescendRate = FMath::Max(Height, 0.0f) / Touchdown;
			Dragon->LandingTailSeconds = FMath::Max(0.0f, Length - Touchdown);
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: landing motion starts %.0f m above the ground (anim %.1fs, touchdown in %.1fs)"),
				Height * 0.01f, Length, Touchdown);
		}
	}
	else
	{
		const float Step = Dragon->DescendRate * DeltaSeconds;
		if (Height - Step <= 0.0f)
		{
			Dragon->SetActorLocation(FVector(Dragon->GetActorLocation().X, Dragon->GetActorLocation().Y, Dragon->GroundZ + Dragon->FeetOffsetCm()));
			// 전함이 추락한 뒤라면 약점 시간(Landed)이 아니라 지상전으로.
			Dragon->EnterState(Dragon->bGroundFight ? EPGDragonState::GroundFight : EPGDragonState::Landed);
			return;
		}
		Dragon->SetActorLocation(Dragon->GetActorLocation() - FVector(0.0f, 0.0f, Step));
	}
	// 보험: 어떤 이유로든 제때 못 내려오면(배에 밀려 다니는 등) 땅에 붙인다. 자리까지 날아가는 경우(최대 600m, 약 13초)는 넉넉히.
	if (Dragon->StateTimer > (Dragon->bDescendToSpot ? 40.0f : 25.0f))
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: descend timed out %.0f m above the ground — snapping down"), Height * 0.01f);
		Dragon->SetActorLocation(FVector(Dragon->GetActorLocation().X, Dragon->GetActorLocation().Y, Dragon->GroundZ + Dragon->FeetOffsetCm()));
		Dragon->LandingTailSeconds = 0.0f;
		Dragon->EnterState(Dragon->bGroundFight ? EPGDragonState::GroundFight : EPGDragonState::Landed);
	}
}

// 땅 위: 착지 동작이 끝나면 포효 → 지면 공격(불뿜기 · 날개 할퀴기 · 물기)을 되풀이하다가 이륙한다.
// 이 동작들은 전부 발을 딛는 지상 동작이다. 이 상태에서만 튼다 — 공중에서 틀면 허공을 짚는다.
void UPGDragonGroundCombat::TickLanded(float DeltaSeconds)
{
	// 배가 머리 위로 밀고 들어오면 먼저 뜬다. 앉아 있는 동안은 안전망(KeepClearOfShip)이 꺼져 있다 —
	// 앉은 몸을 옆으로 밀면 땅 위를 미끄러진다.
	if (IsValid(Dragon->Ship) && FVector::Dist(Dragon->GetActorLocation(), Dragon->Ship->GetActorLocation()) < Dragon->KeepDistanceCm(Dragon->Ship))
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: the ship came within %.0f m while landed — taking off early"),
			FVector::Dist(Dragon->GetActorLocation(), Dragon->Ship->GetActorLocation()) * 0.01f);
		Dragon->EnterState(EPGDragonState::TakeOff);
		return;
	}
	if (AActor* Facing = Dragon->AttackTarget.Get(); Dragon->AirCombat->IsTargetAlive(Facing))
		Dragon->FaceToward(Facing->GetActorLocation(), DeltaSeconds);
	if (TickGroundAction())
		return;
	if (Dragon->StateTimer >= Dragon->WeakPointSeconds)
	{
		Dragon->EnterState(EPGDragonState::TakeOff);
		return;
	}
	AActor* Victim = Dragon->AirCombat->FindGroundVictim();
	Dragon->AttackTarget = Victim ? Victim : static_cast<AActor*>(Dragon->Ship.Get());
	StartGroundAction();
}

// 진행 중인 지면 동작: 피해가 들어갈 시각이 되면 때리고, 불뿜기 차례면 입에서 앞쪽 땅으로 불꽃을 잇는다.
// 동작이 끝났으면(NextLandedAction) 불을 끄고 false — 부른 쪽이 다음 동작을 고른다.
// Landed(약점 시간)와 GroundFight(지상전)가 같은 동작을 쓰므로 한 곳에 둔다. 두 벌이면 한쪽만 고치게 된다.
bool UPGDragonGroundCombat::TickGroundAction()
{
	if (Dragon->PendingGroundHit >= 0.0f && Dragon->StateTimer >= Dragon->PendingGroundHit)
	{
		Dragon->PendingGroundHit = -1.0f;
		DoGroundAttack();
	}
	if (Dragon->bGroundFlame)
	{
		// 땅 위 불뿜기: 입에서 몸 앞쪽 땅으로.
		const FVector Mouth = Dragon->AirCombat->GetMouthLocation();
		FVector Ahead = Dragon->GetActorLocation() + Dragon->GetActorForwardVector() * (Dragon->HalfWingCm * 0.8f + Dragon->GroundAttackRadiusCm * 0.5f);
		Ahead.Z = Dragon->GroundZ;
		Dragon->SetBreath(true, Mouth, (Ahead - Mouth).GetSafeNormal(), FVector::Dist(Mouth, Ahead));
	}
	if (Dragon->StateTimer < Dragon->NextLandedAction)
		return true;
	// 동작 하나가 끝났다.
	if (Dragon->bGroundFlame)
	{
		Dragon->bGroundFlame = false;
		Dragon->SetBreath(false);
	}
	return false;
}

// 다음 지면 동작을 튼다. 첫 동작(LandedStep 0)은 포효(약점·지상전을 알리는 신호), 그 뒤로 세 가지 지면 공격을 돌아가며.
void UPGDragonGroundCombat::StartGroundAction()
{
	static const TCHAR* const GroundAttacks[] = { TEXT("AttackFlameAnim"), TEXT("AttackWingClawAnim"), TEXT("AttackMouthAnim") };
	constexpr int32 GroundAttackCount = UE_ARRAY_COUNT(GroundAttacks);
	const TCHAR* Name = Dragon->LandedStep == 0 ? TEXT("ScreamAnim") : GroundAttacks[(Dragon->LandedStep - 1) % GroundAttackCount];
	const float Length = Dragon->PlayAnim(Name, false);
	if (Dragon->LandedStep > 0)
	{
		// 피해는 동작 중간쯤(내리치는 순간)에. 동작 시작에 넣으면 아직 발톱이 올라가 있는데 맞는다.
		Dragon->PendingGroundHit = Dragon->StateTimer + FMath::Max(Length, 1.0f) * 0.45f;
		Dragon->bGroundFlame = (Dragon->LandedStep - 1) % GroundAttackCount == 0; // 불뿜기 차례(AttackFlameAnim)
	}
	Dragon->NextLandedAction = Dragon->StateTimer + FMath::Max(Length, 1.0f) + 0.3f;
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground step %d — %s (%.1fs), facing %s (state %s)"),
		Dragon->LandedStep, Name, Length, *GetNameSafe(Dragon->AttackTarget.Get()), Dragon->StateName(Dragon->State));
	++Dragon->LandedStep;
}

// 발이 땅에 닿은 순간: 몸을 바로 세우고, 발밑의 흙먼지·소품을 날린다. Landed 와 GroundFight 진입이 같이 쓴다.
void UPGDragonGroundCombat::OnTouchdown()
{
	Dragon->SetActorRotation(FRotator(0.0f, Dragon->GetActorRotation().Yaw, 0.0f));
	const FVector Feet = Dragon->GetActorLocation() - FVector(0.0f, 0.0f, Dragon->FeetOffsetCm());
	Dragon->KnockAround(Feet, Dragon->Hull->GetScaledCapsuleRadius() * 2.2f); // 내려앉는 흙먼지·소품
	Dragon->PlayImpactFx(Feet, 10.0f);
}

// 지면 공격: 몸 앞쪽 땅을 중심으로 둥글게 피해. 몬스터든 플레이어든 가리지 않는다(짐승이니까).
// 피해를 벽 너머로 주지 않게 가림 검사(Visibility)를 켠 채로 둔다 — 건물 안은 안전하다.
void UPGDragonGroundCombat::DoGroundAttack()
{
	const FVector Here = Dragon->GetActorLocation();
	FVector Centre = FMath::Lerp(Here, Dragon->AirCombat->GetMouthLocation(), 0.7f);
	Centre.Z = Dragon->GroundZ + 300.0f;
	const TArray<AActor*> Ignore = { Dragon.Get() };
	const bool bHitSomething = UGameplayStatics::ApplyRadialDamageWithFalloff(Dragon.Get(), Dragon->GroundAttackDamage, Dragon->GroundAttackDamage * 0.25f,
		Centre, Dragon->GroundAttackRadiusCm * 0.4f, Dragon->GroundAttackRadiusCm, 1.0f, UDamageType::StaticClass(), Ignore, Dragon.Get(), nullptr, ECC_Visibility);
	Dragon->KnockAround(Centre, Dragon->GroundAttackRadiusCm * 0.6f);
	Dragon->PlayImpactFx(Centre, 8.0f);
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground attack at %s, radius %.0f m, damage %.0f — hit something=%d"),
		*Centre.ToCompactString(), Dragon->GroundAttackRadiusCm * 0.01f, Dragon->GroundAttackDamage, bHitSomething ? 1 : 0);
}

void UPGDragonGroundCombat::TickTakeOff(float DeltaSeconds)
{
	if (Dragon->StateTimer < 2.0f)
		Dragon->KnockAround(Dragon->GetActorLocation() - FVector(0.0f, 0.0f, Dragon->FeetOffsetCm()), Dragon->Hull->GetScaledCapsuleRadius() * 2.2f);
	if (Dragon->StateTimer < 0.5f)
		return; // 웅크렸다 뛰는 순간. 이때 몸을 띄우면 발을 딛은 채 떠오른다
	Dragon->bLifted = true;
	// 이륙 동작(한 번짜리)이 끝나 가면 날갯짓 반복으로. 안 바꾸면 한 번 퍼덕이고 굳은 채로 떠오른다.
	if (!Dragon->bFlapping && Dragon->StateTimer > FMath::Max(1.2f, Dragon->AttackAnimSeconds * 0.8f))
	{
		Dragon->bFlapping = true;
		Dragon->PlayAnim(TEXT("FlyForwardAnim"), true);
	}
	const FVector To = Dragon->TakeOffTo - Dragon->GetActorLocation();
	if (To.Size() < 1000.0f || Dragon->StateTimer > 20.0f)
	{
		if (Dragon->StateTimer > 20.0f)
		{
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: take-off timed out %.0f m short — %s from here"),
				To.Size() * 0.01f, Dragon->bHopping ? TEXT("dropping") : TEXT("circling"));
		}
		if (Dragon->bHopping)
		{
			// 지상전의 뛰어 옮기기: 다 올라왔으면 그 자리 밑으로 내려앉는다. 선회로는 안 간다.
			Dragon->bDescendToSpot = true;
			Dragon->DescendSpot = Dragon->HopSpot;
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — hop peak, coming back down at (%.0f, %.0f) m"),
				Dragon->HopSpot.X * 0.01f, Dragon->HopSpot.Y * 0.01f);
			Dragon->EnterState(EPGDragonState::Descend);
			return;
		}
		Dragon->EnterState(EPGDragonState::Orbit);
		return;
	}
	Dragon->SetActorLocation(Dragon->GetActorLocation() + To.GetSafeNormal() * Dragon->FlySpeed * 1.2f * DeltaSeconds);
	Dragon->SetActorRotation(FMath::RInterpTo(Dragon->GetActorRotation(), FRotator(25.0f, Dragon->GetActorRotation().Yaw, 0.0f), DeltaSeconds, 2.0f));
}

// ─────────────────────────── 지상전 ───────────────────────────
//
// 9/21 사용자: "전함 체력이 0 이 되면 추락하려나? 그러면 드래곤도 지면에 따라와서 그때부터는 지면에서 총 맞고 싸우는 거지.
//   지면에서 총 맞으면 드래곤 추가 딜 맞게 하자. 공중에서도 지상 총 쏘는 거 딜 맞을 수 있게 하는데 공중에서는 안 아프게 맞고."
//
// 흐름:
//   전함 체력 0(= 추락 중, 전함 쪽 계약은 GetHealth() 하나뿐) → BeginGroundFight
//     공중이면: Descend 를 "자리 지정" 으로 연다(bDescendToSpot). 자리는 전함 근처, 타일 범위 안쪽(ClampToMap).
//               발이 닿으면 Landed 가 아니라 GroundFight 로.
//     앉아 있으면(Landed): 하던 동작을 이어서 곧장 GroundFight.
//     솟는 중(Rising): 다 솟은 뒤(Orbit) 다음 틱에 걸린다. 죽는 중(Dying): 아무것도 안 한다.
//   GroundFight: 가장 가까운 플레이어 폰(차를 몰면 그 차)을 상대로
//     닿는 거리 안 → 지면 공격(Landed 와 같은 포효·불뿜기·할퀴기·물기, StartGroundAction)
//     멀다 → 걷는다(WalkToward). 발은 땅 높이를 따라가고 타일 범위 밖으로는 안 나간다(산은 충돌이 없어 땅 찾기가 뚫린다).
//     아주 멀고 뜀 간격이 지났다 → 짧게 뛰어 옮긴다(BeginHop: TakeOff 를 낮게 → Descend 자리 지정 → 다시 GroundFight).
//     선회(Orbit)로 돌아가는 길은 없다. 이륙(TakeOff)의 도착 처리와 내려앉기(Descend)의 땅 못 찾음 처리가 bHopping / bGroundFight 를 본다.
//   피해: IsOnGroundForGuns 가 참이면 지상 무기 x GroundGunDamageMultiplier, 날고 있으면 x AirGunDamageMultiplier (TakeDamage).

bool UPGDragonGroundCombat::CheckShipDown()
{
	if (!Dragon->bHadShip || Dragon->State == EPGDragonState::Dying || Dragon->State == EPGDragonState::Rising)
		return false; // 전함이 애초에 없었거나, 죽는 중이거나, 아직 산에서 솟는 중(다 솟으면 다음 틱에 본다)
	// 전함이 지워졌으면(잔해 정리 뒤 GC) 그것도 "추락했다" 다. 살아 있으면 체력만 본다 — 추락 연출은 전함 쪽 일이다.
	const bool bShipDown = !IsValid(Dragon->Ship) || Dragon->Ship->GetHealth() <= 0.0f;
	if (!bShipDown)
		return false;
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — the ship is down (%s), dragon is %s at %s"),
		IsValid(Dragon->Ship) ? *FString::Printf(TEXT("hp %.0f"), Dragon->Ship->GetHealth()) : TEXT("actor gone"),
		Dragon->StateName(Dragon->State), *Dragon->GetActorLocation().ToCompactString());
	BeginGroundFight();
	return true;
}

void UPGDragonGroundCombat::BeginGroundFight()
{
	Dragon->bGroundFight = true;
	Dragon->bUsedWeakPoint = true; // 반체력 내려앉기는 이제 뜻이 없다(이미 땅으로 간다)
	const FVector Here = Dragon->GetActorLocation();
	// 내려앉을 자리: 추락하는 전함 옆. 전함 한가운데에서 나 쪽으로 (배 반길이 + 반날개폭의 절반) 만큼 — 잔해 위가 아니라 옆.
	//   그 자리를 타일 범위 안으로 자른다. 배가 맵 밖으로 떨어지면 자연히 가장 가까운 맵 안쪽 가장자리가 된다.
	//   전함이 없으면(지워졌으면) 지금 자리 밑.
	FVector2D Want(Here.X, Here.Y);
	if (IsValid(Dragon->Ship))
	{
		const FVector ShipAt = Dragon->Ship->GetActorLocation();
		FVector Away = (Here - ShipAt).GetSafeNormal2D();
		if (Away.IsNearlyZero())
			Away = Dragon->GetActorForwardVector().GetSafeNormal2D();
		const float Beside = FMath::Max(Dragon->Ship->GetShipLengthCm() * 0.5f, 25000.0f) + Dragon->HalfWingCm * 0.5f;
		Want = FVector2D(ShipAt.X + Away.X * Beside, ShipAt.Y + Away.Y * Beside);
	}
	GroundFightSpot = Dragon->ClampToMap(Want, Dragon->HalfWingCm * 0.3f + 2000.0f);
	switch (Dragon->State)
	{
	case EPGDragonState::Landed:
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — already on the ground, staying down"));
		Dragon->EnterState(EPGDragonState::GroundFight);
		break;
	case EPGDragonState::Descend:
		// 이미 내려가는 중. 발이 닿으면 TickDescend 가 bGroundFight 를 보고 GroundFight 로 넘긴다.
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — already descending, will fight where it lands"));
		break;
	default:
		// 공중(Orbit/Pass/Breath/TakeOff). 자리를 정해 내려간다. 이륙 중 웅크린 순간(발이 땅)도 같은 길 —
		//   높이가 0 이라 착지 동작이 바로 시작되고 다음 틱에 GroundFight 가 된다.
		Dragon->bHopping = false;
		Dragon->bDescendToSpot = true;
		Dragon->DescendSpot = GroundFightSpot;
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — coming down beside the ship at (%.0f, %.0f) m, %.0f m away"),
			GroundFightSpot.X * 0.01f, GroundFightSpot.Y * 0.01f, FVector2D::Distance(FVector2D(Here.X, Here.Y), GroundFightSpot) * 0.01f);
		Dragon->EnterState(EPGDragonState::Descend);
		break;
	}
}

// 지상 무기가 "땅에 선 드래곤" 을 때리는 것으로 치는 상태.
// 앉아 있는 동안(Landed)과, 지상전이 시작된 뒤 전부(걷기·공격은 물론 잠깐 뛰어 옮기는 TakeOff/Descend 도).
//   뛰는 몇 초를 공중으로 치면 배율이 6 ↔ 0.3 으로 깜빡여서 "맞다가 갑자기 안 들어가는" 느낌이 된다.
//   지상전 앞의 첫 내려앉기(Descend)만은 아직 공중이다 — 그때는 진짜로 높이 날고 있다.
bool UPGDragonGroundCombat::IsOnGroundForGuns() const
{
	if (Dragon->State == EPGDragonState::Landed || Dragon->State == EPGDragonState::GroundFight)
		return true;
	return Dragon->bGroundFight && (Dragon->State == EPGDragonState::TakeOff || (Dragon->State == EPGDragonState::Descend && Dragon->bHopping));
}

// 지면 공격의 중심은 원점과 입 사이(DoGroundAttack 의 0.7 지점), 반경 GroundAttackRadiusCm. 그 원이 닿는 거리를 원점 기준으로.
//   입은 원점에서 반날개폭의 0.6배 앞(GetMouthLocation 어림값)이니 중심은 0.42배 앞. 반경의 80% 만 쳐서 가장자리 헛스윙을 줄인다.
float UPGDragonGroundCombat::GroundReachCm() const
{
	return Dragon->HalfWingCm * 0.42f + Dragon->GroundAttackRadiusCm * 0.8f;
}

// 가장 가까운 플레이어 폰. 차·탱크를 몰고 있으면 컨트롤러가 그 폰을 잡고 있으니 자연히 "차" 가 상대가 된다.
// 거리 제한은 없다 — 맵이 600m 이고 이제 이 싸움이 마지막이라, 아무리 멀어도 쫓아간다(멀면 걷지 않고 뛰어 옮긴다).
// 플레이어가 하나도 없으면(죽었거나 아직 안 태어났거나) 근처 몬스터라도 — 빈 땅에서 가만히 서 있는 것보다 낫다.
AActor* UPGDragonGroundCombat::FindGroundFightVictim() const
{
	UWorld* World = Dragon->GetWorld();
	if (!IsValid(World))
		return nullptr;
	const FVector Here = Dragon->GetActorLocation();
	APawn* Nearest = nullptr;
	float NearestSq = MAX_flt;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
			continue;
		const float DistanceSq = FVector::DistSquared2D(Pawn->GetActorLocation(), Here);
		if (DistanceSq < NearestSq)
		{
			NearestSq = DistanceSq;
			Nearest = Pawn;
		}
	}
	return Nearest ? static_cast<AActor*>(Nearest) : Dragon->AirCombat->FindGroundVictim();
}

void UPGDragonGroundCombat::TickGroundFight(float DeltaSeconds)
{
	AActor* Victim = FindGroundFightVictim();
	Dragon->AttackTarget = Victim;
	// 동작(포효·공격) 중: 상대 쪽으로 몸만 돌리고 피해 시점·불꽃을 처리한다.
	if (Dragon->StateTimer < Dragon->NextLandedAction)
	{
		if (Victim)
			Dragon->FaceToward(Victim->GetActorLocation(), DeltaSeconds);
		TickGroundAction();
		return;
	}
	TickGroundAction(); // 끝난 동작의 뒷정리(불 끄기)
	if (!Victim)
	{
		// 아무도 없다. 서서 기다리되 1초마다 다시 찾는다.
		if (!Dragon->bIdling)
		{
			Dragon->bIdling = true;
			Dragon->bWalking = false;
			Dragon->PlayAnim(TEXT("Idle01Anim"), true);
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — nobody to fight, standing at %s"),
				*Dragon->GetActorLocation().ToCompactString());
		}
		Dragon->NextLandedAction = Dragon->StateTimer + 1.0f;
		return;
	}
	Dragon->bIdling = false;
	const FVector Here = Dragon->GetActorLocation();
	const float Distance = FVector::Dist2D(Victim->GetActorLocation(), Here);
	const float Reach = GroundReachCm();
	if (Distance <= Reach)
	{
		if (Dragon->bWalking)
		{
			Dragon->bWalking = false;
			UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — %s is %.0f m away (reach %.0f m), attacking"),
				*GetNameSafe(Victim), Distance * 0.01f, Reach * 0.01f);
		}
		StartGroundAction();
		return;
	}
	// 멀다. 아주 멀고 뜀 간격이 지났으면 짧게 뛰어 옮긴다.
	UWorld* World = Dragon->GetWorld();
	if (Distance > Dragon->GroundHopDistanceCm && IsValid(World) && World->TimeSince(Dragon->LastHopWorldTime) > Dragon->GroundHopCooldownSeconds)
	{
		// 상대 바로 위가 아니라 닿는 거리의 60% 앞에서 내려앉는다 — 머리 위로 떨어지면 착지 순간 밀려난 것처럼 보인다.
		const FVector2D VictimAt(Victim->GetActorLocation().X, Victim->GetActorLocation().Y);
		const FVector2D Back = (FVector2D(Here.X, Here.Y) - VictimAt).GetSafeNormal();
		BeginHop(VictimAt + Back * Reach * 0.6f);
		return;
	}
	if (!Dragon->bWalking)
	{
		Dragon->bWalking = true;
		Dragon->PlayAnim(TEXT("WalkAnim"), true);
		UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — walking toward %s, %.0f m away (reach %.0f m)"),
			*GetNameSafe(Victim), Distance * 0.01f, Reach * 0.01f);
	}
	WalkToward(Victim->GetActorLocation(), Reach * 0.8f, DeltaSeconds);
}

// 땅 위를 걷는다. 물리가 없는 액터라(SetActorLocation) 발 높이는 코드가 직접 땅을 재서 맞춘다.
//   땅은 0.2초마다 다음 자리에서 다시 잰다 — 매 틱 재면 4060 에서 아깝고, 0.2초면 눈에 안 띈다.
//   못 찾으면(타일 밖) 그 걸음은 안 걷는다. 자리는 애초에 ClampToMap 으로 자르니 거의 안 일어난다.
//   발밑 소품은 밟아 날린다(KnockAround 가 0.15초마다 5개씩만).
void UPGDragonGroundCombat::WalkToward(const FVector& Target, float StopAtCm, float DeltaSeconds)
{
	const FVector Here = Dragon->GetActorLocation();
	const FVector2D Here2D(Here.X, Here.Y);
	const FVector2D To = FVector2D(Target.X, Target.Y) - Here2D;
	const float Distance = To.Size();
	Dragon->FaceToward(Target, DeltaSeconds);
	if (Distance <= StopAtCm)
		return;
	const FVector2D Step = To / Distance * FMath::Min(Dragon->GroundWalkSpeed * DeltaSeconds, Distance - StopAtCm);
	const FVector2D Next2D = Dragon->ClampToMap(Here2D + Step, Dragon->HalfWingCm * 0.3f + 2000.0f);
	if (Dragon->StateTimer - Dragon->LastGroundProbe > 0.2f)
	{
		Dragon->LastGroundProbe = Dragon->StateTimer;
		float Found = 0.0f;
		if (FindGroundZ(FVector(Next2D.X, Next2D.Y, Here.Z), Found))
			Dragon->GroundZ = Found;
		else
			return; // 타일 밖. 이 걸음은 건너뛴다(다음 틱에 상대가 움직였으면 방향이 바뀐다)
	}
	// 높이는 부드럽게 따라간다. 계단·잔해 위에서 매 틱 툭툭 튀지 않게.
	const float WantZ = Dragon->GroundZ + Dragon->FeetOffsetCm();
	Dragon->SetActorLocation(FVector(Next2D.X, Next2D.Y, FMath::FInterpTo(Here.Z, WantZ, DeltaSeconds, 4.0f)));
	Dragon->KnockAround(FVector(Next2D.X, Next2D.Y, Dragon->GroundZ), Dragon->Hull->GetScaledCapsuleRadius() * 1.5f);
}

void UPGDragonGroundCombat::BeginHop(const FVector2D& Toward)
{
	Dragon->bHopping = true;
	Dragon->bWalking = false;
	if (IsValid(Dragon->GetWorld()))
		Dragon->LastHopWorldTime = Dragon->GetWorld()->GetTimeSeconds();
	Dragon->HopSpot = Dragon->ClampToMap(Toward, Dragon->HalfWingCm * 0.3f + 2000.0f);
	UE_LOG(LogPGObjects, Display, TEXT("PGDragon: ground fight — target too far to walk (> %.0f m), hopping to (%.0f, %.0f) m"),
		Dragon->GroundHopDistanceCm * 0.01f, Dragon->HopSpot.X * 0.01f, Dragon->HopSpot.Y * 0.01f);
	Dragon->EnterState(EPGDragonState::TakeOff);
}
