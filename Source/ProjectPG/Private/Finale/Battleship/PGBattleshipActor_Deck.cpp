// APGBattleshipActor — 갑판·격납고 — 승강기, 실은 탈것·사람 붙잡기, 떨어지는 사람 받기, 선미 발판·뒷문.
// (2026-09-26 PGBattleshipActor.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "PGBattleshipActorInternal.h"
#include "PGShipDeck.h"
#include "PGShipHullBuilder.h"
#include "Components/CapsuleComponent.h"

// 원래는 승강 발판을 땅까지 내리는 함수였다. 승강기는 9/21 에 걷어냈다(위 7번 주석 참고).
// 지금 남은 일은 갑판에 차를 한 대 놓는 것뿐이다 — 디렉터가 "정박했으니 이제 탈 시간" 이라고
// 알려 주는 자리가 이 호출뿐이라, 차를 놓는 시점을 여기에 그대로 둔다.
// 이름과 인자를 그대로 두는 이유: PGFinaleDirector 가 부르는 함수라 지우면 그쪽이 깨진다.
void UPGShipDeck::DeployElevator(float GroundWorldZ)
{
	SpawnDeckVehicle();
}

// 이륙 전에 디렉터가 부른다. 거둘 발판이 없어져 하는 일이 없다(위와 같은 이유로 이름만 남긴다).
void UPGShipDeck::RetractElevator()
{
}

// ---- 갑판 위의 차를 붙들기 ----
//
// 사람은 UE 의 "움직이는 바닥(MovementBase)" 처리로 갑판을 따라가지만, 물리로 굴러가는 차는 그런 게 없다.
// 배는 물리가 아니라 좌표 이동이라(헤더 주석) 갑판 상자만 옮겨지고 그 위의 차는 그 자리에 남는다.
// 그래서 배가 움직이는 동안만 차를 배에 붙인다(PGFlightKit 이 갑판에 내려앉은 차를 배에 붙이는 것과 같은 방법:
// 물리 끄기 → AttachToComponent(KeepWorld)). 멈추면 떼고 물리를 되돌린다.
// 캐릭터(캡슐, 물리 아님)는 건드리지 않는다 — 붙이면 CharacterMovement 가 깨진다. 아케이드 비행 중인 차
// (물리 꺼짐)도 안 붙인다 — 그건 갑판에 얹힌 게 아니라 날고 있는 것이다.
void UPGShipDeck::HoldPhysicsPawns(USceneComponent* Parent, const FVector& WorldCentre, const FQuat& WorldRotation, const FVector& Extent,
	TArray<APGBattleshipActor::FPGHeldPawn>& Out, const TCHAR* What)
{
	if (!IsValid(Parent))
		return;
	TArray<FOverlapResult> Hits;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_Vehicle);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipHold), false, Ship.Get());
	Ship->GetWorld()->OverlapMultiByObjectType(Hits, WorldCentre, WorldRotation, Objects, FCollisionShape::MakeBox(Extent), Params);
	// 이미 붙들고 있는 것은 건너뛴다 — 이 함수는 순항하는 동안 0.5초마다 다시 불린다(TickDeckCargo).
	TSet<APawn*> Seen;
	for (const APGBattleshipActor::FPGHeldPawn& Each : Out)
		if (APawn* Already = Each.Pawn.Get())
			Seen.Add(Already);
	const int32 Before = Out.Num();
	int32 SkippedCharacters = 0; // 캐릭터 — 일부러 건너뛴 것
	int32 AlreadyRiding = 0;     // 이미 이 배에 붙어 있는 것
	int32 PawnsSeen = 0;
	for (const FOverlapResult& Hit : Hits)
	{
		APawn* Pawn = Cast<APawn>(Hit.GetActor());
		if (!IsValid(Pawn) || Seen.Contains(Pawn))
			continue;
		++PawnsSeen;
		UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
		if (!Body)
			continue;
		// 무엇을 붙드나 — 기준을 "물리가 켜졌나" 에서 "캐릭터가 아닌가" 로 바꿨다.
		//
		// 왜: 전에는 뿌리가 물리로 굴러가는 것만 붙들었다. 그런데 차는 상황에 따라 물리를 꺼 둔다 —
		//   아케이드 비행 중이거나, 갑판에 내려앉아 도킹된 동안이 그렇다. 그래서 갑판 위의 차가 전부
		//   "물리가 아니다" 로 걸러져 하나도 안 붙들렸고, 배가 움직이면 차만 제자리에 남아 갑판 밑으로 빠졌다
		//   (9/21 로그: "5 pawn(s) in range, 0 held, 5 skipped (not physics-driven)" + slipped under the deck 6회).
		//   사용자가 말한 "전함이 움직이는데 탄 차량이 같이 안 움직여서 변신이 풀린다" 가 이것이다.
		// 날고 있는 차를 붙들어도 비행은 안 깨진다: 아케이드 비행은 AddActorWorldOffset 으로 "월드" 좌표를 움직인다.
		//   배에 붙어 있어도 제 힘으로 그대로 날고, 배가 움직이면 같이 실려 간다 — 오히려 그게 맞는 그림이다.
		// 캐릭터만 빼는 이유는 그대로다: CharacterMovement 가 "움직이는 바닥" 으로 알아서 따라오고, 붙이면 그게 깨진다.
		if (Pawn->IsA(ACharacter::StaticClass()))
		{
			++SkippedCharacters;
			continue;
		}
		AActor* Was = Pawn->GetAttachParentActor();
		if (Was == Ship.Get())
		{
			++AlreadyRiding; // 이미 배에 붙어 있다 — 그대로 따라온다
			continue;
		}
		const bool bSimulating = Body->IsSimulatingPhysics();
		Seen.Add(Pawn);
		APGBattleshipActor::FPGHeldPawn& Held = Out.AddDefaulted_GetRef();
		Held.Pawn = Pawn;
		Held.PreviousParent = Was;
		Held.bWasSimulating = bSimulating;
		if (bSimulating)
		{
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			Body->SetSimulatePhysics(false);
		}
		Pawn->AttachToComponent(Parent, FAttachmentTransformRules::KeepWorldTransform);
	}
	// 새로 잡은 게 있거나, 폰은 보이는데 하나도 못 잡았으면 이유까지 남긴다. 붙들기가 왜 안 걸리는지는
	// 로그 없이는 PIE 에서 알 길이 없다(이번에 Size2D 문제를 찾는 데 한 판을 썼다).
	if (Out.Num() > Before || (Out.Num() == Before && PawnsSeen > 0))
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %s hold — %d pawn(s) in range, %d new, %d held, %d character(s) skipped, %d already riding"),
			What, PawnsSeen, Out.Num() - Before, Out.Num(), SkippedCharacters, AlreadyRiding);
}

void UPGShipDeck::ReleaseHeldPawns(TArray<APGBattleshipActor::FPGHeldPawn>& Held, const TCHAR* What)
{
	int32 Released = 0;
	for (const APGBattleshipActor::FPGHeldPawn& Each : Held)
	{
		APawn* Pawn = Each.Pawn.Get();
		if (!IsValid(Pawn))
			continue;
		Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		if (AActor* Was = Each.PreviousParent.Get(); IsValid(Was) && !Each.bWasSimulating)
			Pawn->AttachToActor(Was, FAttachmentTransformRules::KeepWorldTransform); // 도킹돼 있던 차는 도킹 상태로 되돌린다
		else if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()); Body && Each.bWasSimulating)
		{
			Body->SetSimulatePhysics(true);
			// 배의 속도를 물려준다. 0 으로 놓으면 그 순간부터 차만 제자리에 남고 배는 계속 가 버려서,
			// 갑판에서 보면 차가 쭉 밀려나 벽에 처박힌다(9/20 PIE: "차가 앞으로 쏠리는데, 차랑 같이 튕겨나가").
			Body->SetPhysicsLinearVelocity(Ship->ShipVelocity);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
		++Released;
	}
	if (Released > 0)
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %s released %d vehicle(s)"), What, Released);
	Held.Reset();
}

// 배가 움직이는 동안 갑판 위의 물리 차를 배에 붙든다.
//
// 왜: 배는 물리가 아니라 SetActorLocation 으로 움직인다(헤더 주석). 갑판 상자도 그렇게 통째로 옮겨지는데, 그 위에 얹힌
//   물리 몸은 따라오지 않는다. 순항 6000cm/s 면 60fps 에서 한 프레임에 1m — 상자가 차를 지나쳐 가 버려서 차가 갑판 "밑"에
//   남는다. 9/20 로그에 "slipped under the deck" 이 이륙(cruising to Z=38016) 직후마다 찍힌 것이 이것이다.
//   건지기(TickCatchFallers)는 떨어진 뒤에 줍는 보험이고, 진짜 해법은 움직이는 동안 붙들어 두는 것이다
//   (PGFlightKit::DockToShipIfLanded 가 착륙한 차를 배에 붙이는 것과 같은 생각).
// 판정을 Motion 이 아니라 "실제 속도"로 하는 이유: 조종석에서 몰 때(Motion 은 Hover 그대로)도 배는 18m/s 로 움직인다.
//   Hover 흔들림만으로는 초속 3cm 라 이 기준에 안 걸린다.
// 0.5초마다 다시 훑는 이유: 한 번만 훑으면 그 뒤에 승강기가 내려놓은 차를 놓친다 — 그 차가 갑판에서 튕겨나갔다(9/20 PIE).
// 사람은 잡지 않는다 — 캐릭터는 UE 의 "움직이는 바닥"으로 따라오고, 캡슐을 붙이면 CharacterMovement 가 깨진다.
void UPGShipDeck::TickDeckCargo(float DeltaSeconds)
{
	const bool bWantHold = Ship->IsShipMoving() && IsValid(Ship->InteriorRoot) && Ship->HullLocalBounds.IsValid;
	if (!bWantHold)
	{
		if (bDeckHeld)
		{
			ReleaseHeldPawns(Ship->DeckCargo, TEXT("deck"));
			bDeckHeld = false;
		}
		DeckHoldScanTime = 0.0f;
		return;
	}
	bDeckHeld = true;
	DeckHoldScanTime -= DeltaSeconds;
	if (DeckHoldScanTime > 0.0f)
		return;
	DeckHoldScanTime = 0.5f;
	// 갑판 바로 위 8m 를 훑는다. 그보다 높이 있는 것은 날고 있는 것이지 갑판에 얹힌 것이 아니다.
	const float HalfX = FMath::Max((Ship->DeckFrontLocalX - Ship->HangarEntranceLocalX) * 0.5f, 100.0f);
	const float HalfY = Ship->HullLocalBounds.GetSize().Y * 0.22f;
	const FVector Centre = Ship->GetActorTransform().TransformPosition(
		FVector((Ship->HangarEntranceLocalX + Ship->DeckFrontLocalX) * 0.5f, 0.0f, Ship->DeckLocalZ + 400.0f));
	HoldPhysicsPawns(Ship->RootScene, Centre, Ship->GetActorQuat(), FVector(HalfX, HalfY, 400.0f), Ship->DeckCargo, TEXT("deck"));
}

// 갑판에 차를 한 대 세워 둔다. 배가 437m 라 걸어서 함교까지 가는 데만 한참 걸린다
// (9/20 사용자: "전함 내부 편하게 다니게 차량 하나 두자").
// 왜 비행 키트를 같이 붙이나: 이 차로 배에서 내려 날아갈 수도 있어야 한다 — 변신 차와 같은 몸이다.
void UPGShipDeck::SpawnDeckVehicle()
{
	// 9/22 끔 — 사용자: "실내에 실어 뒀던 차량이 자꾸 떨어진다. 전함에 있던 차량 없애 줘".
	//   코드 갑판을 끄고(bBuildCodeDeck) 바닥을 블루프린트로 바꾼 뒤로는 이 차가 설 자리(DeckLocalZ)에 바닥이 없어 떨어졌다.
	//   예전 코드 갑판을 다시 켜면 차도 다시 세운다.
	if (!Ship->bBuildCodeDeck)
	{
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: no deck car (code deck is off — the blueprint floor is not at DeckLocalZ)"));
		return;
	}
	if (IsValid(Ship->DeckVehicle) || !IsValid(Ship->InteriorRoot))
		return;
	UWorld* World = Ship->GetWorld();
	if (!IsValid(World))
		return;
	// 선미 문 안쪽 20m, 문 한가운데에서 옆으로 25m. 차로 들어오는 길(문 정중앙)을 비우면서도
	// 들어오자마자 눈에 들어와야 "저기 차가 있다"가 읽힌다.
	const FVector Local(Ship->HangarEntranceLocalX + 2000.0f, 2500.0f, Ship->DeckLocalZ + 120.0f);
	const FTransform Spawn(FRotator(0.0f, 0.0f, 0.0f), Ship->GetActorTransform().TransformPosition(Local));
	FActorSpawnParameters Params;
	Params.Owner = Ship.Get();
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Ship->DeckVehicle = World->SpawnActor<APGVehiclePawn>(UPGVisualSettings::VehicleSpawnClass(), Spawn, Params);
	if (!IsValid(Ship->DeckVehicle))
		return;
	UPGFlightKitComponent* Kit = NewObject<UPGFlightKitComponent>(Ship->DeckVehicle, UPGFlightKitComponent::GetSpawnClass(), TEXT("FlightKit"));
	Kit->RegisterComponent();
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: parked a car on the deck (%s, kit %s)"), *Ship->DeckVehicle->GetName(), *Kit->GetClass()->GetName());
}

// 갑판 밑으로 새어 들어간 것을 건져 올린다.
//
// 왜 필요한가: 갑판은 상자 하나고 선체 껍데기에는 충돌이 없다. 빠르게 들어오는 물리 몸(차)은 어떤 각도에서는
//   상자 모서리를 스치고 그 아래로 빠진다 — 그러면 배 안인데 바닥이 없어 그대로 떨어진다. 9/20 PIE 에서
//   "뒤에 갑판 닿기만 하면 차량 추락" 으로 몇 번을 되풀이했다. 막는 벽(DeckUnderGuard)을 세웠지만
//   물리 뚫림은 벽으로 100% 막지 못한다. 그래서 떨어진 것은 갑판 위로 되돌려 놓는다.
// 범위를 선체 안(가로·세로)으로 좁히는 이유: 배 밖에서 날아다니는 차까지 빨아올리면 안 된다.
void UPGShipDeck::TickCatchFallers(float DeltaSeconds)
{
	// 코드 갑판을 안 깔면 건져 올릴 "갑판" 자체가 없다 — 사용자 블루프린트 바닥 높이를 모르니 끈다.
	if (!Ship->bBuildCodeDeck || !Ship->HullLocalBounds.IsValid || !IsValid(Ship->InteriorRoot))
		return;
	CatchScanTime += DeltaSeconds;
	if (CatchScanTime < 0.25f) // 초당 4번이면 충분하다. 매 프레임 훑을 이유가 없다.
		return;
	CatchScanTime = 0.0f;
	const FTransform& ToWorld = Ship->GetActorTransform();
	const float HalfY = Ship->HullLocalBounds.GetSize().Y * 0.22f;
	for (TActorIterator<APawn> It(Ship->GetWorld()); It; ++It)
	{
		APawn* Pawn = *It;
		if (!IsValid(Pawn) || Pawn->IsAttachedTo(Ship.Get()))
			continue;
		const FVector Local = ToWorld.InverseTransformPosition(Pawn->GetActorLocation());
		const bool bInFootprint = Local.X > Ship->HangarEntranceLocalX - 1500.0f && Local.X < Ship->HullLocalBounds.Max.X
			&& FMath::Abs(Local.Y) < HalfY;
		// 갑판보다 3m 아래 ~ 60m 아래. 그보다 더 내려갔으면 이미 배 밖이라 건드리지 않는다.
		// 아래 한계는 선체 바닥까지만. 더 내려가면 이미 배 밖이라, 배 밑을 지나는 차까지 빨아올린다.
		const float Floor = FMath::Max(Ship->DeckLocalZ - 6000.0f, Ship->HullLocalBounds.Min.Z);
		if (!bInFootprint || Local.Z > Ship->DeckLocalZ - 300.0f || Local.Z < Floor)
			continue;
		// 되돌릴 자리는 반드시 갑판 상자 안쪽. 바닥이 없는 뱃머리 끝이나 벽 상자 속에 올려놓으면
		// 올려놓자마자 다시 떨어져 초당 4번 건지기를 무한 반복한다.
		const FVector Rescue = ToWorld.TransformPosition(FVector(
			FMath::Clamp(Local.X, Ship->HangarEntranceLocalX + 400.0f, Ship->DeckFrontLocalX - 400.0f),
			FMath::Clamp(Local.Y, -(HalfY - 400.0f), HalfY - 400.0f),
			Ship->DeckLocalZ + 150.0f));
		Pawn->SetActorLocation(Rescue, false, nullptr, ETeleportType::TeleportPhysics);
		if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()); Body && Body->IsSimulatingPhysics())
		{
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		}
		// 좌표를 같이 찍는다. "어딘가 구멍이 있다"까지는 알아도 어디인지는 이름만으로 알 수 없어서, 다음 판에서 바로
		// 짚으려고 배 기준 좌표와 그때의 상황(순항 중인지, 발판이 어디 있는지)을 남긴다(9/20 조율 요청).
		const TCHAR* MotionText = Ship->Motion == EPGShipMotion::Cruise ? TEXT("cruise") : (Ship->Motion == EPGShipMotion::Hover ? TEXT("hover") : TEXT("parked"));
		UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: %s slipped under the deck at local (%.0f, %.0f, %.0f) — deck z=%.0f, %s — put it back on top"),
			*Pawn->GetName(), Local.X, Local.Y, Local.Z, Ship->DeckLocalZ, MotionText);
	}
}

// 배 뒤쪽 선반 — 문 앞에 깔리는 평평한 널빤지 세 장.
//
// 무엇인가: 차가 날아와 올라서는 자리다. 뒤로 갈수록 좁아지는 세 장이 앞뒤로 이어져 있고(망원경·사다리차 붐 구조),
//   문을 닫을 때 바깥 장부터 차례로 앞 장 속으로 밀려 들어간 뒤 셋이 함께 갑판 밑으로 내려간다.
// 왜 크기를 안 건드리나: 팩 부품을 2% 까지 오그라뜨리는 방식(TickHullRetract)이 안 예뻐서 이 에셋이 나왔다.
//   여기서는 자리만 옮긴다.
//
// 배율 주의 — 이 함수에서 제일 중요한 줄: InteriorRoot 에 붙인다(배율 1).
//   껍데기(Hull)에 붙이면 배 전체 배율 10 이 먹어서 79m 짜리가 790m 로 그려진다.
//   지금 팩 장식판이 그렇게 커 보이는 것도 껍데기에 붙어 있어서다(원본 5.8m -> 화면 58m).
// 메시 축 규칙은 임포트 스크립트가 못 박아 두었다(Tools/import_ship_stern_gate.py): X = 앞뒤 깊이, Y = 폭, Z = 두께.
//   그래서 돌릴 필요가 없다. 그래도 실제 크기를 재서 로그로 찍는다 — 100 배 사고를 잡는 그물이다.
// 밟는 상자를 짝지어 두는 이유: 40m 짜리 선반에 보이는 판만 있으면 차가 그대로 빠진다.
//   이 프로젝트가 같은 것으로 두 번 당했다(투명 바닥 / 허공에 뜬 판).
void UPGShipDeck::BuildSternApron(float RearX)
{
	if (!IsValid(Ship->InteriorRoot) || !Ship->HullLocalBounds.IsValid)
		return;
	// 블렌더로 만든 선반 세 장(SM_ShipSternApron1~3)은 깔지 않는다 — 9/21 사용자: "블렌더가 만든 계단 없애 버려".
	//   배 뒤 허공에 층층이 뜬 계단처럼 보였고, 접어 넣은 뒤에도 몸통 뒤끝보다 뒤라 삐져나왔다. 에셋 파일은 지우지 않고 남겨 둔다.
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: stern apron planks skipped (removed by request) — only the sill box stays"));
	(void)RearX;
	// 문턱판도 같은 방식으로 밀어 넣는다.
	//
	// 왜: 문턱판은 문 평면(RearX)보다 뒤에 깔려 있다. 선반만 들어가고 이게 남으면 닫힌 문 뒤에 6m 짜리 선반이
	//   튀어나온 채로 남는다 — 사용자 합격 기준("문 닫히면 배 밖에 아무것도 안 보인다")에 바로 걸린다.
	// 왜 숨기지 않고 미나: 문이 닫히는 3초 동안 툭 사라지는 게 그대로 보인다. 제일 안쪽 장과 같은 시각·같은 거리로
	//   밀면 한 덩어리로 들어가는 것처럼 읽힌다.
	if (IsValid(Ship->DeckRearLipPlate) && IsValid(Ship->DeckRearLipBox))
	{
		FPGSternApron& Lip = SternAprons.AddDefaulted_GetRef();
		Lip.Plank = Ship->DeckRearLipPlate;
		Lip.Floor = Ship->DeckRearLipBox;
		Lip.OutLocation = Ship->DeckRearLipPlate->GetRelativeLocation();
		Lip.StowLocation = Lip.OutLocation + FVector(1500.0f, 0.0f, 0.0f);
		Lip.FloorOffset = Ship->DeckRearLipBox->GetRelativeLocation() - Lip.OutLocation;
		Lip.StartDelay = 0.90f;
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: stern apron built — %d piece(s) including the sill, all stowed by %.2fs"),
		SternAprons.Num(), 0.90f + Ship->ApronSlideSeconds);
}

// 배 뒤쪽 선반(널빤지 3장 + 문턱판)을 이륙할 때 서랍처럼 갑판 밑으로 밀어 넣는다.
//
// 언제: 정박했다가 다시 움직이기 시작할 때(bHasArrivedOnce && Cruise) — 이륙이다. 전에는 문이 닫히기 직전에 돌았는데,
//   문을 걷어내서(9/21, BuildInterior 주석) 이륙에 묶는다. 매달고 나는 32m 선반은 어색하다.
// 팩 장식판을 2% 로 오그라뜨리던 (1) 은 없앴다 — 그 부품(팩 뒷문·경사로)이 입구다(MeasureHull 주석).
// 바깥 장부터 차례로 앞으로 밀려 들어간다. 시작 시각을 어긋나게 둬야 기계처럼 보인다 — 세 장이 동시에 움직이면 밋밋하다.
//   완급은 SmoothStep 이다. alpha*alpha 는 끝이 제일 빠른 곡선이라 "쾅" 하고 멈춰서 기계가 아니라 사고로 보인다
//   (9/21 에셋 쪽 지적). SmoothStep 은 시작과 끝이 둘 다 느려서 미끄러져 들어가다 사뿐히 멈춘다.
//   크기는 건드리지 않는다. 자리만 옮긴다 — 그러라고 나온 에셋이다.
// 다 들어가면 "배 밖으로 튀어나온 것" 검사를 한 번 돈다(LogPartsOutsideHull) — 문이 없으니 이륙이 그 시점이다.
void UPGShipDeck::TickHullRetract(float DeltaSeconds)
{
	if (bHullRetractDone)
		return;
	if (!bHullRetracting)
	{
		if (!(Ship->bHasArrivedOnce && Ship->Motion == EPGShipMotion::Cruise))
			return; // 아직 정박 중이거나 처음 날아오는 중 — 선반은 펼쳐 둔다
		bHullRetracting = true;
		if (SternAprons.IsEmpty())
		{
			// 일찍 빠져나가는 길에도 흔적을 남긴다 — 선반 메시가 없는 PC 에서 검사까지 조용히 건너뛰면 안 된다.
			bHullRetractDone = true;
			UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: launching — no stern apron to stow, checking what sticks out now"));
			Ship->HullBuilder->LogPartsOutsideHull();
			return;
		}
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: launching — stowing %d apron plank(s) under the deck (%.2fs)"),
			SternAprons.Num(), 0.90f + Ship->ApronSlideSeconds);
	}
	RetractElapsed += DeltaSeconds;
	float ApronEndsAt = 0.0f;
	for (const FPGSternApron& Apron : SternAprons)
	{
		ApronEndsAt = FMath::Max(ApronEndsAt, Apron.StartDelay + Ship->ApronSlideSeconds);
		const float Raw = FMath::Clamp((RetractElapsed - Apron.StartDelay) / FMath::Max(Ship->ApronSlideSeconds, 0.1f), 0.0f, 1.0f);
		const FVector Where = FMath::Lerp(Apron.OutLocation, Apron.StowLocation, FMath::SmoothStep(0.0f, 1.0f, Raw));
		if (USceneComponent* Plank = Apron.Plank.Get(); IsValid(Plank))
			Plank->SetRelativeLocation(Where);
		if (UBoxComponent* Floor = Apron.Floor.Get(); IsValid(Floor))
			Floor->SetRelativeLocation(Where + Apron.FloorOffset);
	}
	if (RetractElapsed < ApronEndsAt)
		return;
	// 선반이 갑판 밑으로 들어갔으니 밟는 상자도 끈다 — 갑판 밑에 보이지 않는 발판이 남으면 안 된다.
	// 선반 판도 숨긴다. "갑판 밑" 으로 들어간 자리도 몸통 뒤끝보다 뒤라, 이륙 뒤에 배 뒤로 층층이 삐져나와 보였다
	// (9/21 사용자 스크린샷). 다시 펼 일은 없다 — 이륙은 한 번뿐이다.
	for (const FPGSternApron& Apron : SternAprons)
	{
		if (UBoxComponent* Floor = Apron.Floor.Get(); IsValid(Floor))
			Floor->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (USceneComponent* Plank = Apron.Plank.Get(); IsValid(Plank))
			Plank->SetVisibility(false, true);
	}
	bHullRetractDone = true;
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: stern apron stowed (%d plank(s)) — checking what sticks out of the hull now that the ship has left"),
		SternAprons.Num());
	Ship->HullBuilder->LogPartsOutsideHull();
}

// 이륙하면 팩 뒷문이 경첩을 축으로 들어 올려져 배 뒤가 닫힌다(9/21 사용자: "저 문이 위로 올라가면서 뒤쪽이 닫혀야지").
// 팩에는 문 애니메이션이 없어서(부품이 따로 놓인 정적 메시) 코드가 돌린다. 시작과 끝이 느린 SmoothStep — 기계가 천천히 닫히는 느낌.
void UPGShipDeck::TickRearDoorClose(float DeltaSeconds)
{
	if (bRearDoorClosed)
		return;
	if (RearDoorElapsed < 0.0f)
	{
		// 조종석에 앉으면 닫는다. 9/21 부터 배는 저절로 떠오르지 않고 앉아야 움직이므로(디렉터 Hover 주석),
		// "이륙(Cruise)" 만 기다리면 문이 영영 안 닫힌다. 예전처럼 자동 이륙을 켠 경우도 그대로 닫힌다.
		bool bLaunching = Ship->bHasArrivedOnce && Ship->Motion == EPGShipMotion::Cruise;
		// 시험용(-PGFinaleAutoLaunch): 아무도 안 앉고 이륙하므로, 이륙 단계에 들어가면 닫는다 — 드래곤 뒤 다시 열기(TickRearDoorReopen)를 화면 없이 보려고(9/23).
		if (FParse::Param(FCommandLine::Get(), TEXT("PGFinaleAutoLaunch")))
			if (const APGFinaleDirector* Director = APGFinaleDirector::Get(Ship->GetWorld()))
				bLaunching |= Director->GetState() >= EPGFinaleState::Launch;
		if (!Ship->bSeated && !bLaunching)
			return; // 정박 중에는 열어 둔다 — 그게 입구다
		if (Ship->HasAuthority())
			MovePeopleOffRearDoor();

		// 배 블루프린트에 "CloseRearDoor" 이벤트(인자 없는 커스텀 이벤트)가 있으면 그걸 부르고 코드 회전은 하지 않는다.
		// 왜: 코드로 경첩 하나를 돌리면 팩 문 부품들(문짝·받침대·경사판)이 한 덩어리로 돌아 어색하게 뒤틀렸다(9/22 PIE).
		//   부품마다 도는 축·각도가 다른 건 에디터에서 눈으로 맞추는 게 빠르다 — 블루프린트 타임라인으로 사용자가 만든다.
		if (IsValid(Ship->Hull))
		{
			if (UFunction* CloseEvent = Ship->Hull->FindFunction(TEXT("CloseRearDoor")); CloseEvent && CloseEvent->NumParms == 0)
			{
				Ship->Hull->ProcessEvent(CloseEvent, nullptr);
				bRearDoorClosed = true;
				bRearDoorClosedByBlueprint = true;
				UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %s — called %s.CloseRearDoor (blueprint animates the door, code rotation skipped)"),
					Ship->bSeated ? TEXT("pilot seated") : TEXT("launching"), *GetNameSafe(Ship->Hull));
				return;
			}
		}
		if (Ship->RearDoorParts.IsEmpty())
			return;
		RearDoorElapsed = 0.0f;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: %s — closing the rear door"), Ship->bSeated ? TEXT("pilot seated") : TEXT("launching"));
	}
	RearDoorElapsed += DeltaSeconds;
	const float Alpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(RearDoorElapsed / FMath::Max(Ship->RearDoorCloseSeconds, 0.1f), 0.0f, 1.0f));
	const FQuat Turn = FQuat::Slerp(FQuat::Identity, Ship->RearDoorCloseRot, Alpha);
	const FTransform& ShipWorld = Ship->GetActorTransform();
	for (int32 Index = 0; Index < Ship->RearDoorParts.Num(); ++Index)
	{
		USceneComponent* Part = Ship->RearDoorParts[Index].Get();
		if (!IsValid(Part) || !Ship->RearDoorStartLocal.IsValidIndex(Index))
			continue;
		const FTransform& Start = Ship->RearDoorStartLocal[Index];
		FTransform Now = Start;
		Now.SetLocation(Ship->RearDoorHinge + Turn.RotateVector(Start.GetLocation() - Ship->RearDoorHinge));
		Now.SetRotation(Turn * Start.GetRotation());
		Part->SetWorldTransform(Now * ShipWorld, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (Alpha >= 1.0f)
	{
		bRearDoorClosed = true;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: rear door closed"));
	}
}

// 뒷문이 닫히기 시작할 때 문·경사판 위에 선 사람을 배 안쪽 바닥으로 옮긴다.
//
// 왜(멀티 9/27): 조종석에 누가 앉으면 뒷문이 3초 동안 경첩을 축으로 들려 닫힌다. 혼자 하는 판에서는 앉은 사람뿐이라
//   문 위에 설 사람이 없었는데, 멀티에서는 다른 사람이 막 올라타다 경사판에 서 있을 수 있다. 그 사람은 닫히는 문에
//   얹힌 채 22m 들려 올라가 선체 속에 끼었다(두 사람 시험: 배 기준 자리가 3초 동안 23m 어긋남).
// 어떻게: 경첩보다 뒤(문 쪽)에 있는 사람을 경첩 안쪽 20m 바닥으로 옮긴다. 바닥 높이는 블루프린트 바닥이라
//   코드가 모른다 — 그 자리 위에서 아래로 선을 쏴 배 부품에 맞는 곳을 바닥으로 친다. 원격 사람 컴퓨터에도 알린다.
void UPGShipDeck::MovePeopleOffRearDoor()
{
	UWorld* World = Ship->GetWorld();
	if (!World || !Ship->HullLocalBounds.IsValid)
		return;
	const FTransform& ToWorld = Ship->GetActorTransform();
	const float HalfY = Ship->HullLocalBounds.GetSize().Y * 0.3f;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		ACharacter* Walker = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
		if (!IsValid(Walker))
			continue;
		const FVector Local = ToWorld.InverseTransformPosition(Walker->GetActorLocation());
		// 경첩보다 뒤(문·경사판 쪽), 선체 폭 안, 선체 높이 안에 있는 사람만.
		if (Local.X > Ship->RearDoorHinge.X + 300.0f || Local.X < Ship->HullLocalBounds.Min.X - 2000.0f
			|| FMath::Abs(Local.Y) > HalfY || Local.Z < Ship->HullLocalBounds.Min.Z - 500.0f || Local.Z > Ship->HullLocalBounds.Max.Z)
			continue;
		const FVector InsideLocalXY(Ship->RearDoorHinge.X + 2000.0f, FMath::Clamp(Local.Y, -1500.0f, 1500.0f), 0.0f);
		const FVector Top = ToWorld.TransformPosition(FVector(InsideLocalXY.X, InsideLocalXY.Y, Ship->RearDoorHinge.Z + 1500.0f));
		const FVector Bottom = ToWorld.TransformPosition(FVector(InsideLocalXY.X, InsideLocalXY.Y, Ship->HullLocalBounds.Min.Z));
		FHitResult Floor;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRearDoorClear), false, Walker);
		FVector Target = ToWorld.TransformPosition(FVector(InsideLocalXY.X, InsideLocalXY.Y, Local.Z));
		if (World->LineTraceSingleByChannel(Floor, Top, Bottom, ECC_Pawn, Params))
			Target = Floor.ImpactPoint + FVector(0.0f, 0.0f, Walker->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0f);
		Walker->SetActorLocation(Target, false, nullptr, ETeleportType::TeleportPhysics);
		if (!PC->IsLocalController())
			PC->ClientSetLocation(Target, Walker->GetActorRotation());
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: rear door closing — moved %s off the door to the deck inside (local x %.0f -> %.0f, floor %s)"),
			*Walker->GetName(), Local.X, InsideLocalXY.X, Floor.bBlockingHit ? *GetNameSafe(Floor.GetComponent()) : TEXT("not found, kept height"));
	}
}

// 드래곤을 잡으면 다시 내려야 한다(9/23 사용자: "조종석에서 내리면 다시 문 열리게"). 닫을 때와 같은 길로 연다.
// 조건: 디렉터가 Victory(격추) + 조종석이 비어 있음. 앉아 있는 동안 열면 배를 몰다 문이 벌어져 이상하다.
void UPGShipDeck::TickRearDoorReopen(float DeltaSeconds)
{
	if (!bRearDoorClosed || bRearDoorReopened)
		return;
	if (RearDoorOpenElapsed < 0.0f)
	{
		const APGFinaleDirector* Director = APGFinaleDirector::Get(Ship->GetWorld());
		if (Ship->bSeated || !Director || Director->GetState() != EPGFinaleState::Victory)
			return;
		if (bRearDoorClosedByBlueprint)
		{
			// 블루프린트 타임라인으로 닫았으니 여는 것도 블루프린트가 한다(타임라인 Reverse 에 이어 두면 된다).
			bRearDoorReopened = true;
			if (IsValid(Ship->Hull))
			{
				if (UFunction* OpenEvent = Ship->Hull->FindFunction(TEXT("OpenRearDoor")); OpenEvent && OpenEvent->NumParms == 0)
				{
					Ship->Hull->ProcessEvent(OpenEvent, nullptr);
					UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: dragon down, helm empty — called %s.OpenRearDoor"), *GetNameSafe(Ship->Hull));
					return;
				}
			}
			UE_LOG(LogPGObjects, Warning, TEXT("PGBattleship: dragon down, helm empty — but %s has no OpenRearDoor event (add a custom event → DoorTimeline Reverse), the door stays shut"),
				*GetNameSafe(Ship->Hull));
			return;
		}
		if (Ship->RearDoorParts.IsEmpty())
		{
			bRearDoorReopened = true;
			return;
		}
		RearDoorOpenElapsed = 0.0f;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: dragon down, helm empty — opening the rear door"));
	}
	RearDoorOpenElapsed += DeltaSeconds;
	const float Alpha = FMath::SmoothStep(0.0f, 1.0f, FMath::Clamp(RearDoorOpenElapsed / FMath::Max(Ship->RearDoorCloseSeconds, 0.1f), 0.0f, 1.0f));
	const FQuat Turn = FQuat::Slerp(Ship->RearDoorCloseRot, FQuat::Identity, Alpha);
	const FTransform& ShipWorld = Ship->GetActorTransform();
	for (int32 Index = 0; Index < Ship->RearDoorParts.Num(); ++Index)
	{
		USceneComponent* Part = Ship->RearDoorParts[Index].Get();
		if (!IsValid(Part) || !Ship->RearDoorStartLocal.IsValidIndex(Index))
			continue;
		const FTransform& Start = Ship->RearDoorStartLocal[Index];
		FTransform Now = Start;
		Now.SetLocation(Ship->RearDoorHinge + Turn.RotateVector(Start.GetLocation() - Ship->RearDoorHinge));
		Now.SetRotation(Turn * Start.GetRotation());
		Part->SetWorldTransform(Now * ShipWorld, false, nullptr, ETeleportType::TeleportPhysics);
	}
	if (Alpha >= 1.0f)
	{
		bRearDoorReopened = true;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: rear door open again"));
	}
}
