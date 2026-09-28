// APGBattleshipActor — 선체 짓기·측정 — 팩 부품으로 배 몸통·실내·조명을 조립하고 크기를 잰다.
// (2026-09-26 PGBattleshipActor.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGBattleshipActorInternal.h"
#include "PGShipDeck.h"
#include "PGShipWeapons.h"
#include "PGShipHelm.h"
#include "PGShipHullBuilder.h"

void UPGShipHullBuilder::BuildHull()
{
	if (IsValid(Ship->Hull))
		return;
	UClass* ShipClass = Ship->HullBlueprint.LoadSynchronous();
	if (!ShipClass)
	{
		// 팩이 없는 PC(에셋 팩은 각자 로컬)에서도 피날레가 멈추면 안 된다. 껍데기 없이라도 "조립 끝"으로 치고 굴린다.
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: hull blueprint missing (%s) — flying an invisible ship"), *Ship->HullBlueprint.ToString());
		Ship->bAssembled = true;
		if (Ship->bPendingCruise)
		{
			Ship->bPendingCruise = false;
			Ship->CruiseTo(Ship->TargetLocation);
		}
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = Ship.Get();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Ship->Hull = Ship->GetWorld()->SpawnActor<AActor>(ShipClass, Ship->GetActorTransform(), Params);
	if (!IsValid(Ship->Hull))
		return;
	MakeHierarchyMovable(Ship->Hull);
	Ship->Hull->AttachToActor(Ship.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	// 이 배의 앞(+X)에 배 길이를 맞춘다. 팩의 화물선은 길이축이 로컬 Y 라서 90도 돌려야 한다
	// (콘솔 PG.SpawnBattleship 이 플레이어 요 +90 으로 띄우던 것과 같은 이유).
	Ship->Hull->SetActorRelativeRotation(FRotator(0.0f, 90.0f, 0.0f));
	Ship->Hull->SetActorRelativeScale3D(FVector(Ship->ShipScale));
	// 부품이 다 붙는 1초 동안에도 10배짜리 복합 충돌이 살아 있으면 안 된다(MeasureHull 이 다시 한 번 전부 끈다).
	Ship->Hull->SetActorEnableCollision(false);

	// 크기를 잰 뒤 한 번 더 껍데기 전체를 손본다. 부품(자식 액터)은 스폰 직후엔 아직 안 붙어 있다.
	Ship->GetWorld()->GetTimerManager().SetTimer(MeasureTimer, FTimerDelegate::CreateUObject(this, &UPGShipHullBuilder::MeasureHull) /* 재기는 이제 선체 담당(이 객체)의 일 */, 1.0f, false);
}

void UPGShipHullBuilder::MeasureHull()
{
	if (!IsValid(Ship->Hull))
		return;
	MakeHierarchyMovable(Ship->Hull); // 자식 액터(부품 31개)는 이제야 다 붙어 있다
	TArray<AActor*> Parts;
	Ship->Hull->GetAttachedActors(Parts, true, true);
	Parts.Add(Ship->Hull);
	// 로컬 경계는 부품마다 "액터 기준"으로 다시 계산해 모은다.
	// Primitive->Bounds 는 월드 축 정렬 상자(AABB)라, 배가 돌아간 상태에서 모아 역변환하면 폭이 몇 배로 부푼다
	// (9/20 검토: 요 45도에서 폭 166m → 407m 로 나와 갑판이 선체 밖 허공에 깔렸다).
	const FTransform ToLocal = Ship->GetActorTransform().Inverse();
	FBox Local(ForceInit);
	FBox Box(ForceInit);
	int32 Primitives = 0;
	// 부품을 모아 두고 크기를 다 잰 뒤에 한꺼번에 가른다. 기준선(본체 경계)을 알아야 가를 수 있는데,
	// 그건 부품을 다 돌아 봐야 나온다.
	struct FPGHullCandidate { UPrimitiveComponent* Primitive; FString Name; FBox Local; };
	TArray<FPGHullCandidate> Candidates;
	TArray<FString> UserFloorParts; // 사용자가 블루프린트에 더해 충돌을 살려 둔 부품
	for (AActor* Part : Parts)
	{
		if (!IsValid(Part))
			continue;
		// 껍데기 부품은 "배에 붙은 별도 액터"라 넉백 코드의 "주인이 배인가" 검사에 안 걸린다.
		// 탱크·로봇·드래곤이 배 옆에서 무언가를 날릴 때 배가 뜯기지 않도록 보호 표를 붙인다(9/20 조사).
		Part->Tags.AddUnique(PGPhysicsUtil::ProtectedTag);
		for (UActorComponent* Component : Part->GetComponents())
		{
			UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive || !Primitive->IsRegistered())
				continue;
			// 팩 원래 부품은 눈으로만 본다(헤더의 "왜 충돌을 다 끄나" 참고).
			// 사용자가 블루프린트에 직접 더한 바닥·벽(메시 이름이 팩 화물선 부품이 아닌 것)은 건드리지 않는다 —
			// 그게 이제 밟는 바닥이다(bBuildCodeDeck 주석). 예전 코드 갑판을 켠 경우엔 전처럼 전부 끈다.
			{
				const UStaticMeshComponent* PartMesh = Cast<UStaticMeshComponent>(Primitive);
				// 메시가 없는 부품(사용자가 넣은 "박스 콜리전" 같은 보이지 않는 상자)도 사용자 것으로 본다 — 팩 부품은 전부 메시다.
				const bool bPackPart = PartMesh && PartMesh->GetStaticMesh()
					&& PartMesh->GetStaticMesh()->GetName().StartsWith(TEXT("SM_KB3D_MTM_VehicleCargoShip"));
				if (Ship->bBuildCodeDeck || bPackPart)
					Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				else
					// 이름만으로는 모른다 — 충돌 설정(프리셋)과 차를 실제로 막는지까지 찍는다. 9/21 PIE 에서 차가 배 바닥을
					// 그냥 통과했는데, 부품은 2 개 잡혔다고만 나와 원인을 못 갈랐다(박스 콜리전 기본 프리셋은 "겹치기만" 이다).
					UserFloorParts.Add(FString::Printf(TEXT("%s[%s, car %s, pawn %s]"),
						PartMesh && PartMesh->GetStaticMesh() ? *PartMesh->GetStaticMesh()->GetName() : *Primitive->GetName(),
						*Primitive->GetCollisionProfileName().ToString(),
						Primitive->GetCollisionResponseToChannel(ECC_Vehicle) == ECR_Block ? TEXT("BLOCKED") : TEXT("passes through"),
						Primitive->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block ? TEXT("BLOCKED") : TEXT("passes through")));
			}
			Primitive->SetCastShadow(false);
			// 팩의 장식용 문·해치·램프는 숨긴다. 충돌이 없어 실제로는 그냥 지나갈 수 있는데, 닫힌 문처럼 보여서
			// 거기서 멈추고 F 를 눌러 보게 된다(9/20 PIE: "엘리베이터 문도 안 열리냐... 뭐 막혀 있네").
			// 램프는 배율 10배라 5.8 x 2.5m 짜리가 58 x 25m 로 그려진다. 선미 입구에 비스듬히 걸쳐 보여서
			// "검은 판때기가 문을 막고 있다"로 읽혔다(9/21 사용자). 경계 계산에는 그대로 넣어 배 크기는 안 변한다.
			//
			// 이름은 반드시 "메시" 이름으로 본다. 전에는 Part->GetName()(액터 이름)을 봤는데 그게 죽은 코드였다:
			// 이 껍데기는 자식 액터가 없고 부품 31개가 전부 한 액터의 컴포넌트다(로그: parts=1 primitives=31).
			// 그래서 31번 모두 같은 액터 이름(BP_..._C_0)만 검사했고 세 조건이 전부 거짓이라,
			// 9/20 에 넣은 뒤로 한 번도 아무것도 안 숨겼다(9/21 진단). 같은 실수를 또 하지 않도록 아래에 결과를 찍는다.
			FString MeshName = Primitive->GetName();
			if (const UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Primitive); AsMesh && AsMesh->GetStaticMesh())
				MeshName = AsMesh->GetStaticMesh()->GetName();
			// 치울지 말지는 아래에서 "자리" 로 정한다(ClassifyHullParts). 여기서는 이름과 상자만 모아 둔다.
			Candidates.Add({ Primitive, MeshName, Primitive->CalcBounds(Primitive->GetComponentTransform() * ToLocal).GetBox() });
			Primitive->SetCullDistance(0.0f); // 멀어도 계속 보인다 — 산 너머에서 넘어오는 게 이 연출의 전부다
			Box += Primitive->Bounds.GetBox();
			Local += Primitive->CalcBounds(Primitive->GetComponentTransform() * ToLocal).GetBox();
			++Primitives;
		}
	}
	Ship->HullBounds = Box;
	Ship->HullLocalBounds = Local;
	// 스폰 때 껍데기 액터의 충돌을 통째로 꺼 뒀다(부품이 붙는 1초 동안). 팩 부품은 위에서 하나씩 껐으니 이제 다시 켠다 —
	// 안 켜면 사용자가 블루프린트에 깐 바닥도 같이 꺼져 배 안에서 그대로 떨어진다.
	if (!Ship->bBuildCodeDeck)
		Ship->Hull->SetActorEnableCollision(true);
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: code deck %s — %d blueprint part(s) keep their own collision%s%s"),
		Ship->bBuildCodeDeck ? TEXT("ON") : TEXT("OFF (floor comes from the ship blueprint)"), UserFloorParts.Num(),
		UserFloorParts.IsEmpty() ? TEXT(" — NOTHING TO STAND ON inside the ship yet") : TEXT(": "),
		*FString::Join(UserFloorParts, TEXT(", ")));
	Ship->bAssembled = true;

	// ---- 무엇을 치울지 "자리" 로 정한다 ----
	//
	// 왜 이름으로 안 고르나: 오늘 이름으로 맞히려다 세 번 틀렸다(승강기 -> 경사로 -> 실은 숨김 코드가 죽어 있었음).
	//   사용자도 그게 뭔지 모른다 — "앞부분 밑에 검은 판이랑 동그라미 뭔지는 모르겠는데 안 튀어나왔으면 좋겠어"(9/21).
	//   합격 기준 자체가 "문 닫히면 배 말고 아무것도 안 보인다" 라, 자리가 곧 기준이다.
	//
	// 기준선(본체): 이름이 _A_Body 로 시작하는 부품들의 합. HullLocalBounds 는 부품 31개를 전부 합친 것이라
	//   그 밖으로 나가는 부품이 정의상 없어서 기준으로 못 쓴다. 해치(BodyHatch)는 본체가 아니라 장식이라 뺀다.
	//   이름을 여기서 쓰긴 하지만 "치울 것을 고르는" 게 아니라 "자를 대는" 용도다.
	//
	// 이름을 한 번 더 쓰는 곳은 "남길 것을 지키는" 데다: 엔진(Thruster)과 착륙다리(Legs)는 본체 밖으로 나와야 맞다.
	//   자리만 보고 지우면 엔진 없는 배가 된다. 배에 구멍이 나는 쪽이 지금 증상보다 나쁘다(9/21 조율).
	// 팩 뒷문(경사판)을 이륙 때 들어 올려 닫을 준비. 문 자체·끝 발판(Ramp)·아래 받침대(DoorStrutB)를 같이 돌린다.
	// 사용자가 블루프린트에서 문 부품 밑에 붙인 충돌 상자는 부모를 따라 같이 돈다(9/21 사용자 안내).
	{
		FBox DoorBox(ForceInit);
		Ship->RearDoorParts.Reset();
		Ship->RearDoorStartLocal.Reset();
		for (const FPGHullCandidate& Each : Candidates)
		{
			const bool bDoor = Each.Name.EndsWith(TEXT("_A_Door"));
			if (!bDoor && !Each.Name.EndsWith(TEXT("_A_Ramp")) && !Each.Name.EndsWith(TEXT("_A_DoorStrutB")))
				continue;
			if (bDoor)
				DoorBox = Each.Local;
			Ship->RearDoorParts.Add(Each.Primitive);
			Ship->RearDoorStartLocal.Add(Each.Primitive->GetComponentTransform() * ToLocal);
		}
		if (DoorBox.IsValid)
		{
			// 경첩 = 문에서 배 가운데 쪽(+X)·위쪽 끝. 문은 거기서 뒤(-X) 아래로 비스듬히 내려와 있다(로그: x -20527..-9222, z 0..37m @10배).
			Ship->RearDoorHinge = FVector(DoorBox.Max.X, 0.0, DoorBox.Max.Z);
			// 자동 각도 = 문의 기울기 + 90도. 예전에는 기울기(18도)만 돌려서 경사판이 **수평**까지만 올라와 뒤로 튀어나온 채 멈췄다
			// (9/22 PIE: "함선 뒷부분 닫을 수 없어?"). 닫힌다는 것은 경사판이 뒤쪽 입구를 세로로 막는 것 — 수평을 지나 90도 더 세운다.
			const float Degrees = Ship->RearDoorCloseDegrees > 0.0f ? Ship->RearDoorCloseDegrees
				: FMath::RadiansToDegrees(FMath::Atan2(DoorBox.GetSize().Z, DoorBox.GetSize().X)) + 90.0f;
			// 도는 방향은 계산으로 고른다 — 뒤 아래 끝(Min.X, Min.Z)이 "올라가는" 쪽이 닫히는 쪽이다. 부호를 외워 두면 팩이 바뀔 때 틀린다.
			const FVector Tail = FVector(DoorBox.Min.X, 0.0, DoorBox.Min.Z) - Ship->RearDoorHinge;
			const FQuat Plus(FVector::YAxisVector, FMath::DegreesToRadians(Degrees));
			const FQuat Minus(FVector::YAxisVector, -FMath::DegreesToRadians(Degrees));
			Ship->RearDoorCloseRot = Plus.RotateVector(Tail).Z > Minus.RotateVector(Tail).Z ? Plus : Minus;
			UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: rear door ready — %d part(s), hinge local %s, closes %.1f deg in %.1fs at launch"),
				Ship->RearDoorParts.Num(), *Ship->RearDoorHinge.ToCompactString(), Degrees, Ship->RearDoorCloseSeconds);
		}
		else
		{
			Ship->RearDoorParts.Reset();
			UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: rear door part (_A_Door) not found — the stern stays open"));
		}
	}

	FBox BodyCore(ForceInit);
	BodyMiddleLocal = FBox(ForceInit);
	for (const FPGHullCandidate& Each : Candidates)
	{
		if (!Each.Local.IsValid || !Each.Name.Contains(TEXT("_A_Body")) || Each.Name.Contains(TEXT("Hatch")))
			continue;
		BodyCore += Each.Local;
		// 몸통 가운데 토막(_A_BodyMiddle, 폭 147m)의 앞끝이 "갑판 폭이 유지되는 마지막 X" 다. 그 앞은 뱃머리(폭 25m)뿐이라
		// 갑판을 거기까지 깔면 좌우로 껍데기 밖에 나온다(BuildInterior 의 DeckFrontX 클램프 주석 참고). Misc 는 뺀다 — 더 짧다.
		if (Each.Name.EndsWith(TEXT("_A_BodyMiddle")))
			BodyMiddleLocal = Each.Local;
	}
	BodyCoreLocal = BodyCore;

	TArray<FString> HiddenParts;  // 조립할 때 바로 숨긴 것
	TArray<FString> HiddenGear;   // 그중 착륙다리 — 따로 한 줄 찍는다(9/21 사용자 결정의 확인 로그)
	TArray<FString> KeptRearParts; // 배 뒤쪽으로 나온 팩 부품(뒷문·받침대·경사로) — 입구라서 그대로 둔 것
	if (!BodyCore.IsValid)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: could not find the hull body (no _A_Body part) — leaving every part visible"));
	}
	else
	{
		for (const FPGHullCandidate& Each : Candidates)
		{
			if (!IsValid(Each.Primitive) || !Each.Local.IsValid || Each.Name.Contains(TEXT("_A_Body")))
				continue;
			// 본체 상자 밖으로 제일 많이 나간 거리.
			const float Out = FMath::Max(
				(BodyCore.Min - Each.Local.Min).GetMax(),
				(Each.Local.Max - BodyCore.Max).GetMax());
			if (Out < Ship->HullOutsideMarginCm)
				continue; // 본체 안에 묻혀 있다 — 밖에서 안 보이니 그대로 둔다
			if (Each.Name.Contains(TEXT("Thruster")))
			{
				// 엔진은 진짜로 나와 있어야 한다. 언제나 그대로 둔다.
				UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hull part [%s] kept — engine, sticks out %.0fm"), *Each.Name, Out * 0.01f);
				continue;
			}
			if (Each.Name.Contains(TEXT("Legs")))
			{
				// 착륙다리: 처음부터 숨긴다(9/21 사용자 결정 — "정박 중에도 안 보여야 한다").
				//
				// 정체(9/21 두 번째 진단): 사용자가 본 "뱃머리 아래 큰 흰 원판(노란 사각 무늬)" 이 이것이다.
				//   본체 바닥(z 2357)보다 아래에 있는 보이는 부품은 Legs1/2/3 뿐이고(Legs1 z 24..1890, x -8979..17544 —
				//   뱃머리 바로 밑까지), Legs1 의 재질 슬롯이 MetalPanelDGrayTrimA(검은 판)·MetalWornFillA(흰 금속)·
				//   MetalPanelYellowScratched(노란 사각) 이라 사용자가 말한 색과 무늬가 그대로 맞는다.
				//   코드가 만드는 판은 전부 갑판(z 3802) 위라 그 아래에 원형이 나올 수가 없다.
				// 왜 접지 않고 숨기나: 전에는 "정박 중엔 펴 두고 이륙 때 접는다"(8152eae) 였는데, 사용자가 본 화면은
				//   정박·이륙 전이라 그 설계로는 애초에 안 사라진다. 이 배는 땅에 내리지도 않는다(정박 고도: 밑바닥이 땅 위 80m).
				//   땅에 닿지 않는 다리는 처음부터 없는 게 맞다 — 접는 코드(TickLandingGear)는 그래서 걷어냈다.
				Each.Primitive->SetVisibility(false);
				HiddenParts.Add(Each.Name);
				HiddenGear.Add(Each.Name);
				UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hull part [%s] hidden — landing gear, sticks out %.0fm below the hull (this ship never touches down)"),
					*Each.Name, Out * 0.01f);
				continue;
			}
			if (Each.Local.GetCenter().X < 0.0f)
			{
				// 배 뒤쪽 — 팩의 원래 뒷문(_A_Door)·받침대(_A_DoorStrut*)·경사로(_A_Ramp)다. 이것이 배의 입구다.
				//
				// 9/21 사용자: "에셋 출구 멀쩡하게 있는데 굳이 ... 검은 판때기가 뒤를 막고 있는 상황 자체가 말이 안 된다".
				//   전에는 문이 닫히기 전에 이것들을 2% 로 오그라뜨려 배 속으로 빨아들였는데(RetractParts), 사용자가 원하는 건
				//   이 펼쳐진 뒷문이 입구로 보이는 것이다. 그래서 건드리지 않는다 — 숨기지도, 줄이지도 않는다.
				// 이륙 뒤에도 펼친 채로 둔다(기본값). 팩 문의 경첩(피벗) 자리를 모르므로 "올라가 닫히는" 회전은 추측이 된다 —
				//   좌표로 보면 _A_Door 는 x -20527..-9222, z 0..37m 로 갑판(44m) 아래에 누워 있어 날면서 매달린 문짝보다는
				//   배 밑바닥의 일부로 읽힌다. 닫는 연출은 경첩을 잰 뒤에 붙일 것.
				KeptRearParts.Add(Each.Name);
				UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hull part [%s] kept — the pack's own stern door/ramp (the entrance), sticks out %.0fm"), *Each.Name, Out * 0.01f);
			}
			else
			{
				// 배 앞쪽·옆쪽 — 연출과 상관없다. 처음부터 안 보이게 한다.
				Each.Primitive->SetVisibility(false);
				HiddenParts.Add(Each.Name);
				UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hull part [%s] hidden — sticks out %.0fm at the front (not engine/gear)"), *Each.Name, Out * 0.01f);
			}
		}
	}
	const FVector Extent = Box.IsValid ? Box.GetExtent() : FVector::ZeroVector;
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: assembled scale=%.1f size L/W/H = %.0f / %.0f / %.0f m (parts=%d primitives=%d)"),
		Ship->ShipScale, FMath::Max(Extent.X, Extent.Y) * 0.02f, FMath::Min(Extent.X, Extent.Y) * 0.02f, Extent.Z * 0.02f, Parts.Num(), Primitives);

	// 숨김이 실제로 돌았는지 남긴다. "돌고 있다고 믿는 코드" 를 한 번 겪었으므로 결과를 눈으로 확인할 수 있어야 한다.
	// UE_LOG 는 여러 문장으로 펼쳐지므로 중괄호 없는 if/else 본문으로 쓸 수 없다(C2181).
	if (HiddenGear.IsEmpty())
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: hid no landing gear at assembly — no 'Legs' part stuck out of the body (mesh names changed?) — the white disc under the bow will show"));
	}
	else
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hid %d landing gear part(s) at assembly: %s"),
			HiddenGear.Num(), *FString::Join(HiddenGear, TEXT(", ")));
	}
	if (HiddenParts.IsEmpty())
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: hid no decorative hull parts — the name filter matched nothing (mesh names changed?)"));
	}
	else
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hid %d decorative hull part(s): %s"),
			HiddenParts.Num(), *FString::Join(HiddenParts, TEXT(", ")));
	}
	if (KeptRearParts.IsEmpty())
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: no pack stern door/ramp found sticking out at the rear — the entrance may look like a bare wall (mesh names changed?)"));
	}
	else
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %d pack stern part(s) kept as the entrance, never shrunk or hidden: %s"),
			KeptRearParts.Num(), *FString::Join(KeptRearParts, TEXT(", ")));
	}

	BuildInterior();
	// 부품은 지금 막 붙었다. 숨겨 둔 배라면 새로 붙은 부품에도 숨김을 다시 걸어야 한다.
	// 멀티(9/27): 클라이언트는 복제된 숨김 값(bShipHidden)을 따른다 — 액터의 숨김(bHidden)은 조립 전 시점 값이라 그대로 쓰면
	//   서버가 이미 숨김을 푼 뒤에 조립된 클라 껍데기가 계속 숨어 있었다.
	if (Ship->HasAuthority() ? Ship->IsHidden() : Ship->bShipHidden)
		Ship->SetShipHidden(true);
	else if (!Ship->HasAuthority())
		Ship->SetShipHidden(false);
	if (!Ship->HasAuthority())
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: assembled on this screen — %s"), Ship->bShipHidden ? TEXT("hidden (waiting for the finale)") : TEXT("visible"));
	OnAssembled.Broadcast(Ship.Get());
	// 조립(부품 붙이기 + Movable 전환)이 끝난 뒤에 출발한다. 먼저 움직이면 Static 부품이 제자리에 남는다.
	if (Ship->bPendingCruise)
	{
		Ship->bPendingCruise = false;
		Ship->CruiseTo(Ship->TargetLocation);
	}
}

// ---- 사람 크기 속 ----
//
// 껍데기를 10배로 키웠으니 그 안은 길이 410m, 폭 166m, 높이 169m 짜리 빈 통이다. 팩의 배 안쪽 부품(InteriorA/B)도 같이 10배라
// 사람이 서면 개미가 된다. 그래서 "탈 수 있는 속"은 배율 1 짜리 Minerva 건물 모듈로 따로 깐다 — 격납고(RoverGarage),
// 복도(PropTunnel), 함교(CommunityCenter). 사용자 요구: "핵심은 내부구현까지 우주전함을 진짜 타는 느낌".
//
// 바닥·벽은 전부 우리가 놓은 단순 상자다(메시 충돌을 안 쓴다):
//  - 팩 메시의 단순 충돌은 대부분 통짜 상자라 복도 안으로 못 들어간다.
//  - 복잡 충돌은 갑판 한 장에 삼각형 수만 개 — 떠 있는(움직이는) 액터에 붙이면 Chaos 가 매 프레임 다시 만든다.
// 그래서 메시는 눈, 상자는 발이다.

UStaticMeshComponent* UPGShipHullBuilder::AddPart(const TCHAR* MeshName, const FVector& Location, const FRotator& Rotation, float Scale)
{
	const FString Path = FString::Printf(TEXT("%s%s.%s"), MinervaMeshDir, MeshName, MeshName);
	return AddPartByPath(*Path, Location, Rotation, Scale);
}

UStaticMeshComponent* UPGShipHullBuilder::AddPartByPath(const TCHAR* AssetPath, const FVector& Location, const FRotator& Rotation, float Scale)
{
	UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, AssetPath);
	if (!Mesh)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: interior mesh missing %s"), AssetPath);
		return nullptr;
	}
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Ship.Get());
	Component->SetStaticMesh(Mesh);
	Component->SetupAttachment(Ship->InteriorRoot);
	Component->SetRelativeLocationAndRotation(Location, Rotation);
	Component->SetRelativeScale3D(FVector(Scale));
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetCanEverAffectNavigation(false);
	Component->SetCastShadow(false); // 배 안은 실내 — 그림자 계산을 아낀다
	Component->RegisterComponent();
	InteriorParts.Add(Component);
	return Component;
}

// 밟는 상자에 "보이는 몸"을 붙인다.
//
// 왜 필요한가: 이 배의 바닥·벽·경사로·승강 발판은 전부 보이지 않는 UBoxComponent 다(껍데기는 눈, 상자는 발).
//   그런데 발판과 경사로처럼 허공에 새로 만든 것은 보이는 몸이 없으면 그냥 공중을 걷는 것으로 보인다
//   (9/20 PIE: "엘베가 갑판까지 이동하는 것도 아니고 뭔 공중을 걷냐"). 팩 메시를 갖다 놨더니 이번엔
//   배 배율이 10배라 발판(6m)보다 훨씬 큰 노란 접시(60m)가 나와 더 헷갈렸다.
//   그래서 팩 메시를 버리고, 밟는 상자와 정확히 같은 크기의 판을 그려 준다. 보이는 것 = 밟는 것.
UStaticMeshComponent* UPGShipHullBuilder::AddSolidBox(const TCHAR* Name, USceneComponent* Parent, const FVector& Centre, const FVector& Extent, const FRotator& Rotation, const TCHAR* MaterialPath)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube || !IsValid(Parent))
		return nullptr;
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Ship.Get(), Name);
	Component->SetStaticMesh(Cube);
	Component->SetupAttachment(Parent);
	Component->SetRelativeLocation(Centre);
	Component->SetRelativeRotation(Rotation);
	Component->SetRelativeScale3D(Extent / 50.0f); // 기본 큐브는 한 변 100cm = 반폭 50
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 막는 것은 짝이 되는 상자다
	Component->SetCastShadow(false);
	Component->SetCanEverAffectNavigation(false);
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, MaterialPath))
		Component->SetMaterial(0, Material);
	Component->RegisterComponent();
	InteriorParts.Add(Component);
	return Component;
}

UBoxComponent* UPGShipHullBuilder::AddWalkBox(const TCHAR* Name, const FVector& Center, const FVector& Extent, const FRotator& Rotation)
{
	UBoxComponent* Box = NewObject<UBoxComponent>(Ship.Get(), Name);
	Box->SetupAttachment(Ship->InteriorRoot);
	Box->SetBoxExtent(Extent);
	Box->SetRelativeLocation(Center);
	Box->SetRelativeRotation(Rotation);
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->SetCanEverAffectNavigation(false);
	Box->bHiddenInGame = true;
	Box->RegisterComponent();
	WalkBoxes.Add(Box);
	return Box;
}

void UPGShipHullBuilder::BuildInterior()
{
	if (IsValid(Ship->InteriorRoot) || !Ship->HullLocalBounds.IsValid)
		return;
	Ship->InteriorRoot = NewObject<USceneComponent>(Ship.Get(), TEXT("InteriorRoot"));
	Ship->InteriorRoot->SetupAttachment(Ship->RootScene);
	Ship->InteriorRoot->RegisterComponent();

	const FVector Size = Ship->HullLocalBounds.GetSize();
	// 갑판은 배 아래쪽 1/4 높이에 깐다(화물칸 자리). 값은 PIE 에서 눈으로 맞출 자리 — 콘솔로 못 바꾸면 여기 숫자를 고친다.
	Ship->DeckLocalZ = Ship->HullLocalBounds.Min.Z + Size.Z * 0.26f;
	// 뒤: 차가 날아 들어오는 입구. 껍데기 꼬리 끝(Min.X)에 바싹 붙인다.
	//
	// 전에는 Min.X + Size.X*0.06 = 꼬리에서 26.2m 안쪽이었다. 껍데기에는 충돌이 없어서 차는 그 26.2m 를
	// "보이는 선미를 관통해서" 들어왔다 — 구멍이 안 보이는데 뚫리니 어색했다(9/20 사용자).
	// 게다가 그 26.2m 에는 바닥(DeckRearLip) 12m 만 있고 옆벽도 천장도 없었다(9/20 좌표 진단).
	// 입구를 꼬리 끝에 두면 차는 배 실루엣의 뒤끝에서 곧장 들어오고, 그 뒤로는 관통할 껍데기가 없다.
	// 6m 는 껍데기 꼬리 표면 안쪽으로 문짝·테두리가 살짝 들어가 있을 여유다.
	const float RearX = Ship->HullLocalBounds.Min.X + 600.0f;
	const float FrontX = Ship->HullLocalBounds.Max.X - Size.X * 0.22f;  // 앞: 함교
	const float HalfY = Size.Y * 0.22f;
	Ship->HangarEntranceLocalX = RearX;
	// 함교 자리를 갑판보다 "먼저" 정한다. 전에는 갑판을 다 깐 뒤에 정했는데, 함교는 뱃머리 유리창
	// 안쪽(갑판 앞끝보다 한참 앞)이라 그 사이에 바닥도 벽도 없는 구멍이 남았다. 앞으로 걸어가면
	// 그대로 빠져나가 배 밖으로 떨어졌다(9/20 PIE: "앞으로 쭉 가니 그냥 뚫어버리고 바깥으로 빠져버린다").
	Ship->Helm->ComputeBridgeLocal(FrontX);
	float DeckFrontX = FMath::Max(FrontX, Ship->BridgeLocal.X + 1200.0f); // 바닥·벽은 함교까지 이어 깐다
	// 갑판 앞끝은 몸통 가운데 토막(_A_BodyMiddle)의 앞끝을 넘지 않는다.
	//
	// 왜(9/21 사용자: "앞부분 밑에 검은 판이랑 동그라미"): 전에는 함교 X+12m(21094)까지 깔았다. 그런데 몸통 가운데 토막은
	//   20507 에서 끝나고 그 앞은 뱃머리(BodyFront, 폭 25m)뿐이라, 마지막 6m 구간의 갑판판(폭 107m, 재질 MI_ShipBridge_Panel)이
	//   뱃머리 좌우로 41m 씩·아래로 1.7m 껍데기 밖에 나왔다 — 조종실 바로 아래의 "검은 사각 판" 이 이것이다.
	//   원판(착륙다리)은 MeasureHull 이 숨기고, 이 판은 여기서 짧게 깐다. 둘 다 좌표로 확인한 것이지 이름으로 짚은 게 아니다.
	// 걷는 길이 안 끊기는 이유: 함교 바닥 상자(BridgeFloor)는 함교 X ±9m 로 따로 깔린다(BuildBridge). 갑판이 6m 짧아져도
	//   그 상자가 20507 앞을 덮는다 — 아래 로그가 두 상자의 겹침을 숫자로 찍는다. 하드코딩이 아니라 MeasureHull 이 잰 값이라
	//   껍데기가 바뀌어도 따라간다. 못 쟀으면(팩 없는 PC) 예전 값 그대로.
	// 보이는 판(DeckFloorPlate)과 밟는 상자(DeckFloor)는 둘 다 이 DeckFrontX 로 만들어지므로 짝은 그대로 유지된다.
	if (BodyMiddleLocal.IsValid && DeckFrontX > BodyMiddleLocal.Max.X)
	{
		const float Before = DeckFrontX;
		DeckFrontX = static_cast<float>(BodyMiddleLocal.Max.X);
		const float BridgeFloorMinX = Ship->BridgeLocal.X - 900.0f;
		const float Overlap = DeckFrontX - BridgeFloorMinX; // 양수면 두 바닥 상자가 겹친다 = 걷는 길이 이어진다
		FBox Nose(ForceInit);
		const float NoseWidthM = MeasureHullSliceLocal(DeckFrontX + 1.0f, static_cast<float>(Ship->HullLocalBounds.Max.X) + 1.0f, Nose)
			? static_cast<float>(Nose.GetSize().Y) * 0.01f : 0.0f;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: deck front clamped %.0f -> %.0f (body middle ends there; the bow beyond is only %.0fm wide vs deck %.0fm) — bridge floor box x %.0f..%.0f overlaps the deck by %.0fm%s"),
			Before, DeckFrontX, NoseWidthM, HalfY * 0.02f, BridgeFloorMinX, Ship->BridgeLocal.X + 900.0f, Overlap * 0.01f,
			Overlap >= 0.0f ? TEXT(", walkway continuous") : TEXT(" — GAP, the walkway to the bridge is broken"));
	}
	Ship->DeckFrontLocalX = DeckFrontX;

	// 코드가 까는 갑판(바닥·벽·지붕·판·차고·복도·화물·선반)은 기본으로 끈다.
	// 9/21 사용자: "배 5배로 줄여. 바닥은 내가 블프로 만들면 그만이잖아." — 바닥은 팩 블루프린트에 사용자가 직접 깐다.
	//   우리 상자 바닥은 팩 바닥보다 7m 떠 있어 팩 뒷문 경사판과 따로 놀았고, 보이지 않는 벽이 탑승을 막았다.
	//   함교(조종석·계기)·주포·조명은 그대로 둔다. 예전 갑판이 필요하면 bBuildCodeDeck 를 켠다.
	int32 Plates = 0;
	int32 Tunnels = 0;
	if (!Ship->bBuildCodeDeck)
		LogHullInventory();
	if (Ship->bBuildCodeDeck)
	{
	// 1) 갑판 바닥 — 발이 닿는 건 이 상자들뿐이다.
	const float DeckHalfX = (DeckFrontX - RearX) * 0.5f;
	const FVector DeckCentre((RearX + DeckFrontX) * 0.5f, 0.0f, Ship->DeckLocalZ - 300.0f); // 두껍게 — 얇으면 빠른 물체가 뚫고 지나간다
	// 갑판 바닥은 구멍 없이 통짜다. 전에는 승강기가 뚫고 오르내리도록 가운데를 비웠는데,
	// 그 구멍 위에 차가 내려앉으면 그대로 빠졌다(9/20 PIE: "우주선 뒷편 원판 같은 데 닿으면 떨어진다").
	// 그래서 발판은 선체 밖에서 오르내리고, 갑판에는 "올라선다". 바닥은 계속 통짜다.
	//
	AddWalkBox(TEXT("DeckFloor"), DeckCentre, FVector(DeckHalfX, HalfY, 300.0f));
	// 옆벽·앞벽·천장(뒤는 열어 둔다 — 날으는 차가 들어오는 입구).
	// 껍데기는 충돌이 없어서(눈으로만 보는 것) 벽이 없으면 차가 배 옆구리를 그냥 통과한다(9/20 PIE).
	// 그래서 벽을 배 높이만큼 세우고 천장도 덮는다. 들어오는 길은 뒤쪽 하나뿐이다.
	const float WallHalfZ = FMath::Max(Size.Z * 0.30f, 2000.0f);
	const float WallZ = Ship->DeckLocalZ + WallHalfZ;
	// 판 재질은 칸(BridgePanelMaterial)에서. AddSolidBox 가 경로 글자를 받아서 경로로 넘긴다.
	const FString PanelMaterialPath = Ship->BridgePanelMaterial.ToSoftObjectPath().ToString();
	const TCHAR* const PanelMaterial = *PanelMaterialPath;

	// 선미 단면을 실제로 재서 뒤쪽 입구 크기를 정한다(MeasureHullSliceLocal 주석 참고).
	// HullLocalBounds 는 배에서 가장 넓은 곳(한가운데 날개, 반폭 121m)의 폭이라 선미 입구를 재는 데 못 쓴다.
	// 못 재면(팩이 없는 PC) 예전처럼 갑판 폭·벽 높이 그대로 — 입구가 사라지는 일은 없어야 한다.
	float SternHalfY = HalfY * 2.0f;
	float SternTopZ = Ship->DeckLocalZ + WallHalfZ * 2.0f;
	FBox Stern(ForceInit);
	if (MeasureHullSliceLocal(RearX - 1000.0f, RearX + 3000.0f, Stern))
	{
		SternHalfY = FMath::Min(FMath::Abs(Stern.Min.Y), FMath::Abs(Stern.Max.Y));
		SternTopZ = Stern.Max.Z;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: stern slice at x=%.0f — half width %.0fm (whole hull %.0fm), z %.0f..%.0fm, deck plates reach %.0fm"),
			RearX, SternHalfY * 0.01f, Size.Y * 0.005f, Stern.Min.Z * 0.01f, Stern.Max.Z * 0.01f, HalfY * 0.01f);
	}
	// 문턱판(DeckRearLip)의 반폭. 전에는 미닫이 문짝의 입구 반폭이었다(선미 반폭의 절반 - 2m, 15m..갑판 반폭).
	// 문은 걷어냈지만(아래 "no custom hangar door" 주석) 문턱판 폭은 그대로 둔다 — 팩 경사로(_A_Ramp, y ±29m)와
	// 뒷문(_A_Door, y ±45m) 위에 얹히는 판이라 갑판 폭(±53m)으로 넓히면 그 양옆이 허공에 뜬다.
	const float EntranceHalfY = FMath::Clamp(SternHalfY * 0.5f - 200.0f, 1500.0f, HalfY);
	LogHullInventory();
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: rear entrance is the open deck end at x=%.0f — %.0fm wide x %.0fm tall (no custom door), sill plate %.0fm wide"),
		RearX, HalfY * 0.02f, WallHalfZ * 0.02f, EntranceHalfY * 0.02f);

	// 옆벽은 "뒤 끝(RearX)부터" 세운다. 전에는 입구에서 한참 안쪽(MouthLength=49.2m)부터 세웠는데,
	// 그 49.2m 구간은 바닥만 있고 좌우가 통째로 열린 도랑이었다. 뒤로 날아 들어온 차는 거기서 옆으로
	// 미끄러져 그대로 배 밖으로 떨어졌다 — 9/20 PIE 에서 몇 번이고 되풀이한 "갑판 닿기만 하면 추락"의 정체다.
	// 들어오는 구멍은 뒤쪽 X 평면 하나면 충분하다(폭 72.9m × 높이 101m). 옆구리까지 열어 둘 이유가 없었다.
	const float WallCentreX = (RearX + DeckFrontX) * 0.5f;
	const float WallHalfX = FMath::Max((DeckFrontX - RearX) * 0.5f, 100.0f);
	AddWalkBox(TEXT("DeckWallL"), FVector(WallCentreX, HalfY + 100.0f, WallZ), FVector(WallHalfX, 100.0f, WallHalfZ));
	// 오른쪽 옆벽도 한 장 통짜다. 전에는 승강기 출입구만큼 끊어 두 토막으로 세우고, 그 바깥에 다리·난간·미닫이문을
	// 달았다 — 승강기가 옆구리에 있었기 때문이다. 발판을 선미로 옮기면서 그 일체가 전부 필요 없어졌다.
	AddWalkBox(TEXT("DeckWallR"), FVector(WallCentreX, -HalfY - 100.0f, WallZ), FVector(WallHalfX, 100.0f, WallHalfZ));
	// 앞벽: 뱃머리 유리창 너머로 빠져나가지 못하게 막는다. 보이는 판은 두지 않는다.
	//
	// 한 번 짝지어 봤다가 되돌렸다. 이 벽은 갑판 폭·벽 높이 그대로라 107 x 102m 인데, 그 자리(뱃머리에서 6m 안쪽)의
	// 껍데기는 그보다 훨씬 좁고 낮다. 그래서 판이 껍데기를 뚫고 나와 밖에서 배를 가로막는 검은 평면으로 보였다
	// (9/21 사용자: "무슨 앞뒤 판때기로 막고 있는 디자인 이거 맞아?").
	// 안 보여도 되는 이유: 여기서 까닭 없이 막힌다고 느꼈던 진짜 원인은 함교 난간이었고 그건 없앴다.
	// 이 벽은 뱃머리 끝의 마지막 안전망이라 닿을 일이 드물고, 바로 앞에 유리창이 있어 "여기가 끝" 이 눈으로 읽힌다.
	AddWalkBox(TEXT("DeckWallF"), FVector(DeckFrontX + 100.0f, 0.0f, WallZ), FVector(100.0f, HalfY, WallHalfZ));

	// 뒤쪽 평면에는 아무것도 세우지 않는다 — 문짝·막는 상자·발광 테두리·메움벽(RearFill)·상인방(RearLintel) 전부 없앴다.
	//
	// 9/21 사용자 결정: "에셋 출구 멀쩡하게 있는데 굳이 저 크기 줄어들지도 않는 검은 판때기가 뒤를 막고 있는 상황 자체가
	//   말이 안 된다 ... 저거 왜 있는데?" — 팩 화물선의 펼쳐진 뒷문·경사로가 입구고, 우리가 덧댄 것이 그 앞을 가리고 있었다.
	// 메움벽·상인방까지 같이 없애는 좌표 근거(요청: "선체 구멍을 메우는 역할이면 남기기"):
	//   문 평면 RearX(-21248)는 껍데기 꼬리(팩 경사로 끝 -21848)에서 6m 안쪽이지만, 배 "몸통" 은 그보다 21m 앞에서 끝난다
	//   (9/21 로그: BodyAntenna x -19174, BodyMisc -19005, BodyRear -18818). RearX 평면에 있는 팩 부품은 펼쳐진 뒷문
	//   (_A_Door, y ±45m, z 0..37m — 갑판 44m 보다 아래)과 경사로(_A_Ramp, z 0..9m)뿐이다. 즉 갑판 높이 위의 이 평면은
	//   껍데기가 없는 허공이다. 메움벽 판은 선체 구멍을 메운 게 아니라 뒷문 위 허공에 떠 있었고, 막는 상자는 허공에 선
	//   보이지 않는 벽이었다. 둘 다 없앤다. 아래 로그가 이 거리를 실제 값으로 찍는다.
	// 그래서 남는 것: 뒤쪽 평면은 갑판 폭·벽 높이 그대로 열린 입구다(이 배의 원래 설계 — "들어오는 구멍은 뒤쪽 X 평면 하나").
	//   옆벽 DeckWallL/R 은 RearX 부터 서 있어 갑판 옆으로는 여전히 못 떨어지고, 문턱 밑은 아래 DeckUnderGuard 가 막는다.
	//   차가 뒤로 몰고 나가면 그대로 떨어진다 — 입구가 열려 있다는 뜻이고, 그게 사용자가 고른 그림이다.
	{
		const float BodyRearX = BodyCoreLocal.IsValid ? static_cast<float>(BodyCoreLocal.Min.X) : RearX;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: no custom hangar door — the pack's own stern door/ramp is the entrance (removed gate panels, blocker, glow frame, fill walls and lintel: the door plane x=%.0f lies %.0fm behind the hull body, which ends at x=%.0f, so anything built there hung in the air above the pack's deployed door)"),
			RearX, (BodyRearX - RearX) * 0.01f, BodyRearX);
	}
	// 뒤쪽 입구 "밑"도 막는다. 갑판 바닥은 두께 6m 상자 하나뿐이고 껍데기에는 충돌이 없어서,
	// 낮게 들어온 차가 갑판 뒷면을 스치고 그 아래로 빠질 수 있다.
	// 높이는 선체 바닥(HullLocalBounds.Min.Z)까지만. 더 내리면 배 밑을 지나는 것이 보이지 않는 벽에 처박힌다.
	const float UnderTop = Ship->DeckLocalZ - 600.0f;
	const float UnderHalfZ = FMath::Max((UnderTop - Ship->HullLocalBounds.Min.Z) * 0.5f, 100.0f);
	AddWalkBox(TEXT("DeckUnderGuard"), FVector(RearX, 0.0f, UnderTop - UnderHalfZ), FVector(200.0f, HalfY, UnderHalfZ));
	// 갑판 바닥을 껍데기 꼬리 끝까지 마저 깐다(입구 문턱 6m). 가장자리에 걸치면 바로 미끄러지던 것을 줄인다.
	// 전에는 이 조각이 12m 짜리 밟는 상자만이라 투명한 바닥이었다(9/20 좌표 진단). 폭은 입구 폭에 맞춘다 — 갑판 폭으로
	// 깔면 문턱 양옆 판이 껍데기 밖으로 나간다. 꼬리 끝(Min.X)을 넘지 않는다.
	{
		const float LipBackX = Ship->HullLocalBounds.Min.X;
		const float LipHalfX = FMath::Max((RearX - LipBackX) * 0.5f, 50.0f);
		const FVector LipAt((LipBackX + RearX) * 0.5f, 0.0f, Ship->DeckLocalZ - 300.0f);
		const FVector LipExt(LipHalfX, EntranceHalfY, 300.0f);
		// 선반과 같이 밀어 넣으려고 붙들어 둔다(BuildSternApron 끝부분 주석 참고).
		Ship->DeckRearLipBox = AddWalkBox(TEXT("DeckRearLip"), LipAt, LipExt);
		Ship->DeckRearLipPlate = AddSolidBox(TEXT("DeckRearLipPlate"), Ship->InteriorRoot, LipAt, LipExt, FRotator::ZeroRotator, PanelMaterial);
		// 문턱판은 밟는 상자만 남기고 눈에서 뺀다 — 몸통 뒤끝(-19174)보다 21m 넘게 뒤라 통째로 허공에 뜬 검은 판이었다
		// (9/21 사용자 스크린샷: 배 뒤로 삐져나온 검은 판때기). 아래 "보이는 갑판은 몸통 안에서만" 주석 참고.
		if (IsValid(Ship->DeckRearLipPlate))
			Ship->DeckRearLipPlate->SetVisibility(false);
	}
	// 지붕은 뱃머리(함교)까지 덮는다. 갑판만 덮었더니 앞쪽 함교에는 그늘이 없어
	// 드래곤 그림자가 함교 안까지 들어왔다(9/20 PIE).
	const float RoofFrontX = Ship->HullLocalBounds.Max.X;
	const float RoofHalfX = FMath::Max((RoofFrontX - RearX) * 0.5f, 100.0f);
	UBoxComponent* Roof = AddWalkBox(TEXT("DeckRoof"), FVector((RearX + RoofFrontX) * 0.5f, 0.0f, Ship->DeckLocalZ + WallHalfZ * 2.0f + 100.0f), FVector(RoofHalfX, HalfY, 100.0f));
	// 선체 껍데기는 프레임 때문에 그림자를 끈다 → 햇빛이 배를 그냥 통과해 안이 야외처럼 밝고,
	// 밖을 지나는 드래곤 그림자까지 안에 드리운다(9/20 PIE). 지붕 상자를 "보이지 않지만 그림자는 만드는" 몸으로
	// 만들어 그늘을 만든다. 상자 하나라 껍데기 31개를 켜는 것보다 훨씬 싸다.
	if (IsValid(Roof))
	{
		Roof->SetCastShadow(true);
		Roof->bCastHiddenShadow = true; // 눈에는 안 보여도 그림자는 진다
		Roof->SetCastInsetShadow(false);
	}

	// 2) 갑판 판때기(눈). 68m 짜리 바닥판을 앞뒤로 깐다.
	// 높이: 이 판은 피벗에서 85cm 위가 판 한가운데, 두께 60cm 다(Tools/minerva_parts.json).
	// 판 윗면을 발 닿는 높이(DeckLocalZ)에 맞추려면 피벗을 85+30 만큼 내린다 — 안 그러면 사람이 바닥에 30cm 잠겨 보인다.
	// 먼저 갑판 전체를 덮는 판을 한 장 깐다. 무늬 판(아래)은 그 위에 얹는 장식일 뿐이다.
	//
	// 왜: 무늬 판은 68m 짜리라 갑판 길이를 FrontX 까지만, 그것도 내림으로 세어 깔았다. 그 결과
	//   앞쪽 130m 와 좌우 19m 씩이 "밟히기는 하는데 보이지는 않는" 상태였다. 사용자가 "전함 앞쪽
	//   바닥 어디 갔어" 라고 한 자리가 정확히 그곳이다(9/20 조사: X 8052 부터 판이 없음).
	//   실제 구멍은 아니었고, 투명한 바닥을 걷고 있었던 것이다.
	//
	// 보이는 갑판은 몸통 안에서만 깐다(9/21). 밟는 상자(DeckFloor)는 그대로 RearX 부터 6m 두께다.
	//   왜: 입구 평면 RearX(-21248)는 몸통 뒤끝(BodyCoreLocal.Min.X, -19174)보다 21m 뒤다. 그 21m 에서 갑판 높이(44m)에 있는
	//   팩 부품은 없다 — 팩 뒷문·경사로는 z 0..37m 로 그보다 아래다. 그래서 거기 깐 판은 전부 배 뒤 허공에 뜬 검은 판으로 보였다
	//   (사용자: "저 검은 판때기"). 두께도 6m 를 그대로 그리면 배 밑으로 비쳐 옆에서 검은 띠가 된다 — 눈에는 윗면 20cm 만 그린다.
	const float VisibleRearX = BodyCoreLocal.IsValid ? FMath::Max(RearX, static_cast<float>(BodyCoreLocal.Min.X)) : RearX;
	{
		const float VisHalfX = FMath::Max((DeckFrontX - VisibleRearX) * 0.5f, 50.0f);
		AddSolidBox(TEXT("DeckFloorPlate"), Ship->InteriorRoot, FVector((VisibleRearX + DeckFrontX) * 0.5f, 0.0f, Ship->DeckLocalZ - 15.0f),
			FVector(VisHalfX, HalfY, 10.0f), FRotator::ZeroRotator, *Ship->BridgePanelMaterial.ToSoftObjectPath().ToString());
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: visible deck starts at the hull body x=%.0f (walk box still starts at the entrance x=%.0f, %.0fm of invisible floor behind the body)"),
			VisibleRearX, RearX, (VisibleRearX - RearX) * 0.01f);
	}
	// 무늬 판: 갑판 앞끝(DeckFrontX)을 넘는 장은 깔지 않는다 — 내림으로 센다.
	//
	// 전에는 올림으로 세서 "한 장 넘치는 편이 낫다" 고 했는데, 그 넘친 한 장이 사용자가 본 뱃머리 아래 흰 원판이었다
	//   (9/21 PIE after-sealing: [..CommunityCenter_A_Floor_01a] x 19670..26494, 뱃머리 밖 47m. 이 메시에 원형 무늬·노란 사각이 있다).
	//   착륙다리(Legs)가 아니라 이것이었다 — 착륙다리 숨김은 그대로 두고, 여기서 마지막 장을 뺀다.
	// 모자라는 구간(최대 68m)은 그 밑에 깔린 통짜 DeckFloorPlate 가 그대로 보인다 — 무늬만 없지 구멍은 아니다.
	// 뒤끝은 문 평면(RearX)에 딱 맞춰 시작하므로 껍데기 꼬리(Min.X, 6m 뒤)를 넘지 않는다. 앞뒤 끝을 로그로 찍는다.
	const float PlateSize = 6820.0f;
	// 무늬 판도 몸통 뒤끝(VisibleRearX)부터 — 위 DeckFloorPlate 와 같은 이유.
	Plates = FMath::Clamp(FMath::FloorToInt((DeckFrontX - VisibleRearX) / PlateSize), 1, 8);
	for (int32 Index = 0; Index < Plates; ++Index)
		AddPart(TEXT("SM_KB3D_MTM_BldgLgCommunityCenter_A_Floor_01a"),
			FVector(VisibleRearX + PlateSize * (Index + 0.5f), 0.0f, Ship->DeckLocalZ - 115.0f));
	{
		const float PlatesEndX = VisibleRearX + PlateSize * Plates;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: deck pattern plates %d x %.0fm cover x %.0f..%.0f (deck ends at %.0f, %.0fm of plain floor left in front; hull tail at %.0f)%s"),
			Plates, PlateSize * 0.01f, VisibleRearX, PlatesEndX, DeckFrontX, (DeckFrontX - PlatesEndX) * 0.01f, Ship->HullLocalBounds.Min.X,
			PlatesEndX > DeckFrontX + 1.0f ? TEXT(" — STILL PAST THE DECK FRONT") : TEXT(""));
	}

	// 3) 격납고(차를 대는 곳): 입구 쪽에 좌우로 두 채.
	for (const float Side : { 1.0f, -1.0f })
	{
		const FVector Base(RearX + 4000.0f, Side * 3200.0f, Ship->DeckLocalZ);
		AddPart(TEXT("SM_KB3D_MTM_BldgMdRoverGarage_A_Garage_01a"), Base, FRotator(0.0f, Side > 0.0f ? 0.0f : 180.0f, 0.0f));
		AddPart(TEXT("SM_KB3D_MTM_BldgMdRoverGarage_A_Platform_01a"), Base);
		AddPart(TEXT("SM_KB3D_MTM_BldgMdRoverGarage_A_BeamSystem_01a"), Base);
	}

	// 4) 복도: 격납고에서 함교까지 터널을 이어 붙인다. 터널은 길이축이 메시 Y 라 90도 돌려 배 앞뒤(X)로 눕힌다.
	const float TunnelLength = 1620.0f;
	const float CorridorStart = RearX + 9000.0f;
	Tunnels = FMath::Clamp(FMath::FloorToInt((FrontX - 3000.0f - CorridorStart) / TunnelLength), 0, 8);
	for (int32 Index = 0; Index < Tunnels; ++Index)
	{
		const FVector At(CorridorStart + TunnelLength * Index, 0.0f, Ship->DeckLocalZ);
		AddPart(TEXT("SM_KB3D_MTM_PropTunnelStaight_A_Main"), At, FRotator(0.0f, 90.0f, 0.0f));
		AddPart(TEXT("SM_KB3D_MTM_PropTunnelStaight_A_Base"), At, FRotator(0.0f, 90.0f, 0.0f));
	}

	}
	// 5) 함교: 뱃머리 유리창 안쪽(BuildBridge 주석 참고).
	Ship->Helm->BuildBridge(FrontX, HalfY);

	if (Ship->bBuildCodeDeck)
	{
	// 6) 화물 상자 몇 개 — 텅 빈 갑판은 배가 아니라 주차장처럼 보인다.
	for (int32 Index = 0; Index < 10; ++Index)
	{
		// 발판이 멈추는 자리(RearX+2000, 반폭 7m)와 그 앞 여유를 비운다. 전에는 RearX+6000 부터라
		// 승강기로 들어온 차가 화물에 밀려 자리가 틀어졌다(9/20 사용자: "뒤에 컨테이너 같은 거에 차량이 밀려서").
		const float X = RearX + 9000.0f + Index * 1700.0f;
		const float Y = (Index % 2 == 0 ? 1.0f : -1.0f) * (HalfY - 1500.0f - (Index % 3) * 400.0f);
		AddPart(TEXT("SM_KB3D_MTM_VehicleCargoShip_A_Cargo"), FVector(X, Y, Ship->DeckLocalZ), FRotator(0.0f, Index * 37.0f, 0.0f));
	}

	// 7) (비어 있음) 예전에는 여기서 선미에 승강 발판을 만들었다. 9/21 에 통째로 걷어냈다 —
	//     차는 날 수 있고 선미 문으로 들어오므로 승강기가 없어도 탑승 동선이 끊기지 않는데,
	//     그 하나 때문에 한 판에 세 가지가 깨졌다(발판이 차를 무시하고 올라감 / 발판 위 사람을 "탑승"으로 오판정 /
	//     발판이 문턱에 걸쳐 문이 안 닫힘). 게다가 발판·기둥·빛기둥이 선체 밖 허공에 떠 보였다.
	//     걷어낸 뒤 갑판에 구멍은 생기지 않는다: 발판은 꼬리 "바깥"(X < Min.X)에 있었고, 문턱 바닥은
	//     DeckRearLip 이 Min.X~RearX 를 그대로 덮는다.

	// 8) 배 뒤쪽 선반: 입구 앞에 깔리는 널빤지 세 장(차가 날아와 올라서는 자리). 이륙하면 갑판 밑으로 밀려 들어간다.
	//    (문은 없다 — 위 "no custom hangar door" 주석.)
	Ship->Deck->BuildSternApron(RearX);
	}

	Ship->Weapons->BuildCannons();

	// 9) 조명. 지붕이 햇빛을 막아 안이 어두워서(9/20 PIE) 갑판을 따라 등을 단다.
	BuildInteriorLights(RearX, DeckFrontX, HalfY, RearX + 2000.0f);

	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: interior built deck_z=%.0f length=%.0fm width=%.0fm plates=%d tunnels=%d parts=%d lights=%d"),
		Ship->DeckLocalZ, (FrontX - RearX) * 0.01f, HalfY * 0.02f, Plates, Tunnels, InteriorParts.Num(), InteriorLights.Num());
}


UPointLightComponent* UPGShipHullBuilder::AddInteriorLight(const FVector& Local, float Radius)
{
	if (!IsValid(Ship->InteriorRoot))
		return nullptr;
	UPointLightComponent* Light = NewObject<UPointLightComponent>(Ship.Get());
	ConfigureShipLight(Light, Ship->InteriorRoot, Local, Ship->SpotLightIntensity, Radius);
	Light->RegisterComponent();
	InteriorLights.Add(Light);
	return Light;
}

// 갑판 천장 등: 아래(-Z)를 보는 면광원. 면의 가로(SourceWidth = 등의 로컬 Y)가 갑판 폭 방향, 세로(SourceHeight = 로컬 Z)가
// 피치 -90 을 주면 배 앞뒤 방향이 된다. 면이 넓으면 등 바로 밑 하이라이트가 퍼져 바닥이 고르게 밝다.
URectLightComponent* UPGShipHullBuilder::AddDeckLight(const FVector& Local)
{
	if (!IsValid(Ship->InteriorRoot))
		return nullptr;
	URectLightComponent* Light = NewObject<URectLightComponent>(Ship.Get());
	ConfigureShipLight(Light, Ship->InteriorRoot, Local, Ship->DeckLightIntensity, Ship->DeckLightRadius);
	Light->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f)); // 로컬 +X(빛이 나가는 쪽)가 아래를 본다
	Light->SetSourceWidth(8000.0f);   // 80m — 갑판 폭 107m 의 대부분
	Light->SetSourceHeight(2000.0f);  // 20m — 앞뒤로는 좁게, 70m 마다 하나씩 띠처럼
	Light->RegisterComponent();
	InteriorLights.Add(Light);
	return Light;
}

void UPGShipHullBuilder::BuildInteriorLights(float RearX, float DeckFrontX, float HalfY, float HangarLandingX)
{
	// 갑판: 가운데 한 줄, 앞뒤로 DeckLightSpacing 마다 면광원 하나. 첫 등은 뒤쪽 입구 안쪽 절반 간격에.
	const float Z = Ship->DeckLocalZ + Ship->DeckLightHeight;
	const float Spacing = FMath::Max(Ship->DeckLightSpacing, 2000.0f);
	const float FirstX = RearX + Spacing * 0.5f;
	const int32 Count = FMath::Clamp(FMath::CeilToInt((DeckFrontX - RearX) / Spacing), 1, 8);
	for (int32 Index = 0; Index < Count; ++Index)
		AddDeckLight(FVector(FMath::Min(FirstX + Spacing * Index, DeckFrontX - 1000.0f), 0.0f, Z));
	// 함교: 조종석 바로 위. 사람이 서서 계기를 보는 자리라 따로 밝힌다.
	AddInteriorLight(Ship->BridgeLocal + FVector(0.0f, 0.0f, 400.0f), 2500.0f);
	// 선미 문 안쪽: 차가 들어와 배 안을 처음 보는 곳이다. 어두우면 들어와도 어디로 가야 할지 모른다.
	// [9/23 수정] 코드 갑판이 꺼져 있으면 입구는 팩 뒷문 경첩이다. 예전 자리(HangarLandingX, 옛 코드 갑판 기준)는 열린 뒷문 판
	//   바로 위·문 받침대 안이라, 그림자 없는 이 등이 문 판에 흰 얼룩을 만들었고(사용자 스크린샷) 문이 닫히면 배 밖 37m 허공에 남았다.
	//   경첩에서 배 안쪽으로 10m 옮긴다 — 문이 닫혀도 등은 배 안에 있다.
	const float SternLightX = (!Ship->bBuildCodeDeck && Ship->RearDoorParts.Num() > 0) ? Ship->RearDoorHinge.X + 1000.0f : HangarLandingX;
	AddInteriorLight(FVector(SternLightX, 0.0f, Ship->DeckLocalZ + 600.0f), 2500.0f);
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: stern entrance light at local x=%.0f (door hinge x=%.0f, old spot x=%.0f)"),
		SternLightX, Ship->RearDoorHinge.X, HangarLandingX);
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: interior lights=%d (deck rect lights %d x %.0fcd, r=%.0fm, every %.0fm, %.0fm up; spots=2)"),
		InteriorLights.Num(), Count, Ship->DeckLightIntensity, Ship->DeckLightRadius * 0.01f, Spacing * 0.01f, Ship->DeckLightHeight * 0.01f);
}


// ---- 함교: 뱃머리 유리창 안쪽 ----
//
// 왜 유리창 안쪽인가: 팩 화물선은 앞코에 유리(BodyFrontGlass)가 이미 있다. 그 안에 사람 크기 조종석을 놓으면
//   1인칭으로 봤을 때 진짜 창밖 풍경이 보인다 — 화면을 한 번 더 그리는 "스크린" 방식과 달리 프레임 비용이 0 이다.
// 계기는 UMG 가 아니라 월드에 띄운 글자(UTextRenderComponent)다. 팀 UI 시스템을 건드리지 않고,
//   1인칭이든 3인칭이든 똑같이 조종석 계기판으로 보인다(사용자 요구 9/20: 둘 다 되게).
// 옆 화면(후방 카메라)만 진짜 렌더 타깃이다 — 가까이 있을 때만, 초당 12장만 찍는다.

bool UPGShipHullBuilder::FindHullPartLocal(const TCHAR* MeshNameContains, FBox& OutLocal) const
{
	if (!IsValid(Ship->Hull))
		return false;
	const FTransform ToLocal = Ship->GetActorTransform().Inverse();
	TArray<AActor*> Parts;
	Ship->Hull->GetAttachedActors(Parts, true, true);
	Parts.Add(Ship->Hull);
	FBox Found(ForceInit);
	for (const AActor* Part : Parts)
	{
		if (!IsValid(Part))
			continue;
		for (const UActorComponent* Component : Part->GetComponents())
		{
			const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
			if (!Mesh || !Mesh->IsRegistered() || !Mesh->GetStaticMesh())
				continue;
			if (!Mesh->GetStaticMesh()->GetName().Contains(MeshNameContains))
				continue;
			Found += Mesh->CalcBounds(Mesh->GetComponentTransform() * ToLocal).GetBox();
		}
	}
	if (!Found.IsValid)
		return false;
	OutLocal = Found;
	return true;
}

// 껍데기의 "어느 X 구간"만 재서 그 자리의 실제 폭·높이를 돌려준다.
//
// 왜 필요한가: HullLocalBounds 는 배 전체를 감싼 상자라 가장 넓은 곳(한가운데 날개)의 폭 243m 를 말한다.
//   선미는 그보다 한참 좁다. 전체 폭으로 뒤쪽 입구를 잡으면 문짝이 열릴 때 선체 밖으로 삐져나온다
//   (9/20 사용자: "문짝이 24m 삐져나온다"). 코드 주석에는 "선미 반폭 83m" 라고 적혀 있었지만 그건 잰 적 없는 숫자였다.
// 어떻게: 부품 하나하나의 "배 기준" 상자를 구해, X 범위가 이 구간과 겹치는 것만 모은다(MeasureHull 과 같은 방법).
//   부품 단위라 단면이 정확하지는 않지만, 선미에 없는 부품(뱃머리·날개)이 빠지는 것만으로 충분히 좁아진다.
//   결과는 PIE 로그 "stern slice" 에 찍힌다 — 주석 숫자가 아니라 이 로그를 믿을 것.
// 문이 닫힌 뒤, 배 본체 밖으로 튀어나와 보이는 것이 남았는지 잰다.
//
// 합격 기준(9/21 사용자): "문이 닫히면 에셋 제외 전함 주변 뭐 보이면 안 된다."
//   부품 이름을 하나씩 맞히는 게 아니라 결과가 기준이다. 그래서 이름이 아니라 자리로 잰다.
//
// "본체"를 무엇으로 보나 — 여기가 이 함수의 전부다:
//   HullLocalBounds 는 부품 31개를 전부 합친 것이라, 그 밖으로 나가는 부품은 정의상 하나도 없다.
//   그래서 기준선을 따로 그어야 한다. 이름이 Body 로 시작하는 부품(몸통·유리·부속 11개)만 합쳐 본체로 삼는다.
//   이름을 쓰긴 하지만 "치울 것을 고르는" 용도가 아니라 "자를 대는" 용도다.
//
// 왜 재기만 하고 지우지 않나: 엔진(Thruster)과 착륙다리(Legs)는 원래 본체 밖으로 나와야 맞다.
//   자리만 보고 자동으로 지우면 그것들까지 사라져 배가 망가진다(9/21 조율). 배에 구멍이 나는 쪽이
//   지금 증상보다 나쁘다. 그래서 여기서는 목록만 내고, 무엇을 치울지는 사람이 정한다.
//   원래 나와도 되는 것에는 표를 달아 둬서 목록을 바로 읽을 수 있게 한다.
void UPGShipHullBuilder::LogPartsOutsideHull() const
{
	if (!IsValid(Ship->Hull))
		return;
	const FTransform ToLocal = Ship->GetActorTransform().Inverse();
	TArray<AActor*> Parts;
	Ship->Hull->GetAttachedActors(Parts, true, true);
	Parts.Add(Ship->Hull);

	// 1) 기준선: 몸통(Body*) 부품들만 합친 상자.
	FBox Core(ForceInit);
	for (const AActor* Part : Parts)
	{
		if (!IsValid(Part))
			continue;
		for (const UActorComponent* Component : Part->GetComponents())
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive || !Primitive->IsRegistered())
				continue;
			FString Name = Primitive->GetName();
			if (const UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Primitive); AsMesh && AsMesh->GetStaticMesh())
				Name = AsMesh->GetStaticMesh()->GetName();
			if (!Name.Contains(TEXT("_A_Body")))
				continue;
			const FBox Local = Primitive->CalcBounds(Primitive->GetComponentTransform() * ToLocal).GetBox();
			if (Local.IsValid)
				Core += Local;
		}
	}
	if (!Core.IsValid)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: after launch — could not find the hull body (no _A_Body part), skipping the check"));
		return;
	}

	// 2) 본체가 아닌 "보이는" 부품이 그 밖으로 얼마나 나가는지 잰다.
	int32 StickingOut = 0;
	for (const AActor* Part : Parts)
	{
		if (!IsValid(Part))
			continue;
		for (const UActorComponent* Component : Part->GetComponents())
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive || !Primitive->IsRegistered() || !Primitive->IsVisible())
				continue; // 안 보이는 것은 사용자 눈에 안 띈다
			FString Name = Primitive->GetName();
			if (const UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Primitive); AsMesh && AsMesh->GetStaticMesh())
				Name = AsMesh->GetStaticMesh()->GetName();
			if (Name.Contains(TEXT("_A_Body")))
				continue; // 본체 자신
			const FBox Local = Primitive->CalcBounds(Primitive->GetComponentTransform() * ToLocal).GetBox();
			if (!Local.IsValid)
				continue;
			// 여섯 면 중 가장 많이 삐져나온 거리.
			const float Out = FMath::Max(
				(Core.Min - Local.Min).GetMax(),
				(Local.Max - Core.Max).GetMax());
			if (Out < 300.0f)
				continue; // 3m 안쪽은 본체에 묻힌 것으로 본다
			++StickingOut;
			// 엔진과 팩 뒷문·경사로는 원래 나와 있어야 하는 것이다(입구). 착륙다리는 조립 때 숨겨지므로(MeasureHull)
			// 여기 보이면 숨김이 안 돈 것이다 — 그대로 경고한다.
			const bool bEngine = Name.Contains(TEXT("Thruster"));
			const bool bEntrance = Name.Contains(TEXT("_A_Door")) || Name.Contains(TEXT("_A_Ramp"));
			const FString Line = FString::Printf(TEXT("PGBattleship: after launch — [%s] sticks out %.0fm (%s, size %.0f x %.0f x %.0f m)%s"),
				*Name, Out * 0.01f, Local.GetCenter().X < 0.0f ? TEXT("rear") : TEXT("front"),
				Local.GetSize().X * 0.01f, Local.GetSize().Y * 0.01f, Local.GetSize().Z * 0.01f,
				bEngine ? TEXT(" — engine, meant to") : (bEntrance ? TEXT(" — the pack's stern door/ramp (the entrance), meant to")
					: (Name.Contains(TEXT("Legs")) ? TEXT(" — LANDING GEAR STILL VISIBLE, the assembly-time hide did not run") : TEXT(""))));
			// 원래 나와 있어야 하는 것은 Display, 나머지는 Warning — UE_LOG 의 등급은 컴파일 상수라 둘로 나눈다.
			if (bEngine || bEntrance)
			{
				UE_LOG(LogPGObjects, Display, TEXT("%s"), *Line);
			}
			else
			{
				UE_LOG(LogPGObjects, Warning, TEXT("%s"), *Line);
			}
		}
	}

	// 3) 코드가 만든 판(InteriorParts)도 같은 잣대로 잰다.
	//
	// 전에는 껍데기 부품만 재서, 갑판 판이 뱃머리 옆으로 삐져나와도 이 검사는 "clean" 이라고 했다.
	// 사용자는 팩 부품과 코드 판을 구분하지 않는다 — 튀어나온 것은 전부 같은 기준이다.
	// 기준 상자는 본체 상자를 뒤로 문 평면까지 늘린 것이다: 문·메움벽·문턱판은 본체 뒤끝(BodyAntenna x -19174)보다
	//   20m 뒤인 문 평면(x -21248)에 서 있는 게 설계라, 본체 상자 그대로 재면 그것들이 전부 "튀어나옴" 으로 나와 진짜를 가린다.
	FBox CodeReference = Core;
	CodeReference.Min.X = FMath::Min(CodeReference.Min.X, static_cast<double>(Ship->HangarEntranceLocalX - 300.0f));
	int32 CodeStickingOut = 0;
	for (const UStaticMeshComponent* Plate : InteriorParts)
	{
		if (!IsValid(Plate) || !Plate->IsVisible() || !Plate->IsRegistered())
			continue;
		const FBox Local = Plate->CalcBounds(Plate->GetComponentTransform() * ToLocal).GetBox();
		if (!Local.IsValid)
			continue;
		const float Out = FMath::Max(
			(CodeReference.Min - Local.Min).GetMax(),
			(Local.Max - CodeReference.Max).GetMax());
		if (Out < 300.0f)
			continue;
		++CodeStickingOut;
		// AddSolidBox 는 이름을 붙여 만들고(DeckFloorPlate 등), AddPartByPath 는 기본 이름이라 메시 이름이 더 읽기 쉽다.
		const bool bDefaultName = Plate->GetName().StartsWith(TEXT("StaticMeshComponent"));
		const FString Name = bDefaultName && Plate->GetStaticMesh() ? Plate->GetStaticMesh()->GetName() : Plate->GetName();
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: after launch — code-built [%s] sticks out %.0fm (%s, x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f)"),
			*Name, Out * 0.01f, Local.GetCenter().X < 0.0f ? TEXT("rear") : TEXT("front"),
			Local.Min.X, Local.Max.X, Local.Min.Y, Local.Max.Y, Local.Min.Z, Local.Max.Z);
	}
	StickingOut += CodeStickingOut;

	// 4) 뱃머리 슬라이스 검사. 위 (3) 은 본체 "합집합" 상자(폭 166m)로 재서, 몸통 가운데 토막이 끝나는 x 앞에서
	//    좁아지는 뱃머리(폭 25m) 밖으로 나온 판은 못 잡는다 — 검은 사각 판이 정확히 그 경우였다. 그래서 그 구간만 따로,
	//    그 구간에 실제로 있는 껍데기 부품의 폭·높이로 잰다.
	if (BodyMiddleLocal.IsValid)
	{
		const float NoseX = static_cast<float>(BodyMiddleLocal.Max.X);
		FBox Nose(ForceInit);
		if (MeasureHullSliceLocal(NoseX + 1.0f, static_cast<float>(Core.Max.X) + 1.0f, Nose))
		{
			int32 Reaching = 0;
			int32 NoseStickingOut = 0;
			for (const UStaticMeshComponent* Plate : InteriorParts)
			{
				if (!IsValid(Plate) || !Plate->IsVisible() || !Plate->IsRegistered())
					continue;
				const FBox Local = Plate->CalcBounds(Plate->GetComponentTransform() * ToLocal).GetBox();
				if (!Local.IsValid || Local.Max.X <= NoseX + 300.0f)
					continue; // 뱃머리 구간까지 안 온다
				++Reaching;
				const float Out = FMath::Max(FMath::Max(
					static_cast<float>(Nose.Min.Y - Local.Min.Y), static_cast<float>(Local.Max.Y - Nose.Max.Y)), FMath::Max(
					static_cast<float>(Nose.Min.Z - Local.Min.Z), static_cast<float>(Local.Max.Z - Nose.Max.Z)));
				if (Out < 300.0f)
					continue;
				++NoseStickingOut;
				const bool bDefaultName = Plate->GetName().StartsWith(TEXT("StaticMeshComponent"));
				const FString Name = bDefaultName && Plate->GetStaticMesh() ? Plate->GetStaticMesh()->GetName() : Plate->GetName();
				UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: after launch — code-built [%s] reaches x %.0f (past the body middle at %.0f) and sticks %.0fm out of the bow there (plate y %.0f..%.0f z %.0f..%.0f vs bow y %.0f..%.0f z %.0f..%.0f)"),
					*Name, Local.Max.X, NoseX, Out * 0.01f, Local.Min.Y, Local.Max.Y, Local.Min.Z, Local.Max.Z,
					Nose.Min.Y, Nose.Max.Y, Nose.Min.Z, Nose.Max.Z);
			}
			StickingOut += NoseStickingOut;
			UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: after launch — bow slice x>%.0f is y %.0f..%.0f z %.0f..%.0f; %d code-built plate(s) reach into it, %d stick out"),
				NoseX, Nose.Min.Y, Nose.Max.Y, Nose.Min.Z, Nose.Max.Z, Reaching, NoseStickingOut);
		}
		else
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: after launch — no hull part found ahead of the body middle (x>%.0f), bow slice check skipped"), NoseX);
		}
	}
	else
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: after launch — body middle not measured, bow slice check skipped"));
	}
	// UE_LOG 는 여러 문장으로 펼쳐지므로 중괄호 없는 if/else 본문으로 쓸 수 없다(C2181). 두 번째로 걸렸다.
	if (StickingOut == 0)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: after launch — clean, nothing outside the hull body"));
	}
	else
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: after launch — %d part(s) still stick out (body box x %.0f..%.0f y %.0f..%.0f z %.0f..%.0f)"),
			StickingOut, Core.Min.X, Core.Max.X, Core.Min.Y, Core.Max.Y, Core.Min.Z, Core.Max.Z);
	}
}

// 껍데기 부품 전체를 이름과 좌표로 한 번 찍는다.
//
// 왜 필요한가: 사용자가 "문보다 뒤에 있는, 노란 테두리 달린 큰 판" 이라고 한 것의 정체를 두 번 잘못 짚었다
//   (승강기 → 팩 램프). 코드가 만드는 판은 목록을 알지만, 팩 껍데기 31개는 이름도 자리도 모르는 채 추측했다.
//   소거법("코드가 만드는 것 중에는 비스듬한 게 없다")은 무엇이 아닌지만 말해 줄 뿐 무엇인지는 못 짚는다.
//   추측을 끝내려면 그 구간에 걸치는 것을 전부 이름으로 뽑는 수밖에 없다.
// 보임 여부도 같이 찍는다 — 숨김 필터(MeasureHull)가 실제로 먹었는지 같은 줄에서 확인된다.
// 한 판에 한 번만 찍는다(조립 직후 1회). 31줄 안쪽이라 로그가 넘치지 않는다.
// 뒤쪽만이 아니라 전부 찍는 이유: 배 앞쪽에 튀어나온 원판도 이름을 모른다(9/21 사용자). 한 판으로 둘 다 짚는다.
void UPGShipHullBuilder::LogHullInventory() const
{
	if (!IsValid(Ship->Hull))
		return;
	const FTransform ToLocal = Ship->GetActorTransform().Inverse();
	TArray<AActor*> Parts;
	Ship->Hull->GetAttachedActors(Parts, true, true);
	Parts.Add(Ship->Hull);
	int32 Listed = 0;
	for (const AActor* Part : Parts)
	{
		if (!IsValid(Part))
			continue;
		for (const UActorComponent* Component : Part->GetComponents())
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive || !Primitive->IsRegistered())
				continue;
			const FBox Local = Primitive->CalcBounds(Primitive->GetComponentTransform() * ToLocal).GetBox();
			if (!Local.IsValid)
				continue;
			FString Name = Primitive->GetName();
			if (const UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Primitive); AsMesh && AsMesh->GetStaticMesh())
				Name = AsMesh->GetStaticMesh()->GetName();
			// 앞/뒤를 사람이 읽을 수 있게 같이 적는다. 사용자는 좌표가 아니라 모양과 자리를 본다.
			UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hull part [%s] %s  x %.0f..%.0f  y %.0f..%.0f  z %.0f..%.0f  size %.0f x %.0f x %.0f m — %s"),
				*Name, Local.GetCenter().X < 0.0f ? TEXT("(rear)") : TEXT("(front)"),
				Local.Min.X, Local.Max.X, Local.Min.Y, Local.Max.Y, Local.Min.Z, Local.Max.Z,
				Local.GetSize().X * 0.01f, Local.GetSize().Y * 0.01f, Local.GetSize().Z * 0.01f,
				Primitive->IsVisible() ? TEXT("VISIBLE") : TEXT("hidden"));
			++Listed;
		}
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: hull inventory — %d part(s) listed (door plane is x=%.0f, ship x %.0f..%.0f)"),
		Listed, Ship->HangarEntranceLocalX, Ship->HullLocalBounds.Min.X, Ship->HullLocalBounds.Max.X);
}

bool UPGShipHullBuilder::MeasureHullSliceLocal(float SliceMinX, float SliceMaxX, FBox& OutLocal) const
{
	if (!IsValid(Ship->Hull))
		return false;
	const FTransform ToLocal = Ship->GetActorTransform().Inverse();
	TArray<AActor*> Parts;
	Ship->Hull->GetAttachedActors(Parts, true, true);
	Parts.Add(Ship->Hull);
	FBox Slice(ForceInit);
	for (const AActor* Part : Parts)
	{
		if (!IsValid(Part))
			continue;
		for (const UActorComponent* Component : Part->GetComponents())
		{
			const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component);
			if (!Primitive || !Primitive->IsRegistered())
				continue;
			const FBox Local = Primitive->CalcBounds(Primitive->GetComponentTransform() * ToLocal).GetBox();
			if (!Local.IsValid || Local.Max.X < SliceMinX || Local.Min.X > SliceMaxX)
				continue;
			Slice += Local;
		}
	}
	if (!Slice.IsValid)
		return false;
	OutLocal = Slice;
	return true;
}

// 선체 상자 위쪽 절반(갑판~지붕)의 무작위 한 점. 길이 방향은 앞뒤 15% 를 비운다 — 뱃머리·꼬리 끝은 좁아서 불이 허공에 뜬다.
FVector UPGShipHullBuilder::RandomHullLocal() const
{
	if (!Ship->HullLocalBounds.IsValid)
		return FVector::ZeroVector;
	const FVector Size = Ship->HullLocalBounds.GetSize();
	const FVector Centre = Ship->HullLocalBounds.GetCenter();
	return FVector(
		Centre.X + FMath::FRandRange(-0.35f, 0.35f) * Size.X,
		Centre.Y + FMath::FRandRange(-0.25f, 0.25f) * Size.Y,
		Centre.Z + FMath::FRandRange(0.0f, 0.45f) * Size.Z);
}
