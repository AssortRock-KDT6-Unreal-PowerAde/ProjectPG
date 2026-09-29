#include "Finale/PGBattleshipActor.h" // 언리얼 규칙: 클래스 이름과 같은 .cpp 는 자기 헤더를 맨 먼저 포함한다
#include "Battleship/PGBattleshipActorInternal.h"
#include "Battleship/PGShipDeck.h"
#include "Battleship/PGShipWreck.h"
#include "Battleship/PGShipWeapons.h"
#include "Battleship/PGShipHelm.h"
#include "Battleship/PGShipHullBuilder.h"
#include "Finale/PGHelmControlComponent.h"
#include "Net/UnrealNetwork.h"
// 이 파일: 생성·흐름·상태 전환 같은 뼈대. 나머지 책임은 Battleship/ 폴더(Hull, Helm, Weapons, Wreck, Deck).

APGBattleshipActor::APGBattleshipActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
	SetRootComponent(RootScene);
	HullBlueprint = TSoftClassPtr<AActor>(FSoftObjectPath(MinervaCargoShip));
	// 겉모습 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 에셋 그대로).
	BridgePanelMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/MI_ShipBridge_Panel.MI_ShipBridge_Panel")));
	BridgeGlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/MI_ShipBridge_Glow.MI_ShipBridge_Glow")));
	BridgeConsoleMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/SM_ShipBridgeConsole.SM_ShipBridgeConsole")));
	BridgeCanopyMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/SM_ShipBridgeCanopy.SM_ShipBridgeCanopy")));
	BridgeSeatMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/SM_ShipBridgeSeat.SM_ShipBridgeSeat")));
	ShipScreenMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/M_PGShipScreen.M_PGShipScreen")));
	CannonBeamMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Characters/Quantum/Wig/SM_WigBeam.SM_WigBeam")));
	ImpactBlastMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/SM_MissileBlast.SM_MissileBlast")));
	ImpactBlastMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/MI_MissileBlast.MI_MissileBlast")));
	// 불: 드래곤 브레스가 쓰는 Weapon_Pack 횃불 불꽃. 흙먼지: 주포 탄착·드래곤 추락이 쓰는 Paragon 바위 먼지.
	WreckFireEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/Weapon_Pack/Effects/Particles/Fire/P_TorchFire.P_TorchFire")));
	WreckDustEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld")));
	bReplicates = true;
	SetReplicateMovement(true);
	// 436m 짜리를 화면 밖이라고 안 그리면 안 된다. 컬링 거리를 아주 멀리.
	SetNetCullDistanceSquared(FMath::Square(500000.0f));
	// 멀티(9/27): 숨겨 둔 액터는 엔진이 클라이언트에 안 보낸다. 그래서 미리 만들어 둔 배가 클라에는 "나타나는 순간" 처음 가서
	//   그때 조립(부품 31개 + 속)이 돌아 등장할 때 렉이 걸렸다(서버에서 미리 만드는 이유가 사라짐). 늘 보내서 클라도 미리 조립한다.
	bAlwaysRelevant = true;
}

void APGBattleshipActor::BeginPlay()
{
	Super::BeginPlay();

	// 책임별 협력 객체(검사·붕괴·그리기)를 만든다. 전함 액터는 이들을 순서대로 부르기만 한다(2026-09-26 한 책임 정리).
	CreateCollaborators();
	Health = MaxHealth;
	// 임시 조준점을 화면에 그릴 자리를 잡아 둔다. 그리는 것은 앉아 있는 동안만이다(DrawCrosshair).
	CrosshairHandle = UDebugDrawService::Register(TEXT("Game"),
		FDebugDrawDelegate::CreateUObject(this, &APGBattleshipActor::DrawCrosshair));
	HullBuilder->BuildHull();
}

void APGBattleshipActor::CreateCollaborators()
{
	if (!Deck)
	{
		Deck = NewObject<UPGShipDeck>(this, TEXT("Deck"));
		Deck->Init(this);
	}
	if (!Wreck)
	{
		Wreck = NewObject<UPGShipWreck>(this, TEXT("Wreck"));
		Wreck->Init(this);
	}
	if (!Weapons)
	{
		Weapons = NewObject<UPGShipWeapons>(this, TEXT("Weapons"));
		Weapons->Init(this);
	}
	if (!Helm)
	{
		Helm = NewObject<UPGShipHelm>(this, TEXT("Helm"));
		Helm->Init(this);
	}
	if (!HullBuilder)
	{
		HullBuilder = NewObject<UPGShipHullBuilder>(this, TEXT("HullBuilder"));
		HullBuilder->Init(this);
	}
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGShipHelm(협력 객체)가 한다.
FVector APGBattleshipActor::GetBridgeWorld() const
{
	if (Helm)
		return Helm->GetBridgeWorld();
	return {};
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGShipHelm(협력 객체)가 한다.
void APGBattleshipActor::DrawCrosshair(UCanvas* Canvas, APlayerController* PC)
{
	if (Helm)
		Helm->DrawCrosshair(Canvas, PC);
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGShipDeck(협력 객체)가 한다.
void APGBattleshipActor::DeployElevator(float GroundWorldZ)
{
	if (Deck)
		Deck->DeployElevator(GroundWorldZ);
}

// 맵 정보 약속(IPGMapInfo) 등 공개 창구라 액터에 남기고, 실제 일은 UPGShipDeck(협력 객체)가 한다.
void APGBattleshipActor::RetractElevator()
{
	if (Deck)
		Deck->RetractElevator();
}

void APGBattleshipActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 껍데기는 붙여 둔 별도 액터다. UWorld::DestroyActor 는 자식 액터를 떼기만 하고 없애지 않아서
	// 이걸 안 하면 436m 짜리 화물선이 하늘에 남는다(스모크 테스트를 돌릴 때마다 한 척씩 쌓였다 — 검토 S7).
	if (IsValid(Hull))
		Hull->Destroy();
	// 임시 조준점을 뗀다. 안 떼면 사라진 액터를 계속 부른다.
	if (CrosshairHandle.IsValid())
	{
		UDebugDrawService::Unregister(CrosshairHandle);
		CrosshairHandle.Reset();
	}
	Super::EndPlay(EndPlayReason);
}

void APGBattleshipActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGBattleshipActor, bSeated);
	DOREPLIFETIME(APGBattleshipActor, SeatedPawn);
	DOREPLIFETIME(APGBattleshipActor, bShipHidden);
	DOREPLIFETIME(APGBattleshipActor, bWrecked);
	DOREPLIFETIME(APGBattleshipActor, bWreckGrounded);
	DOREPLIFETIME(APGBattleshipActor, Health);
	DOREPLIFETIME(APGBattleshipActor, Motion);
	DOREPLIFETIME(APGBattleshipActor, bHasArrivedOnce);
}

void APGBattleshipActor::OnRep_ShipHidden()
{
	SetShipHidden(bShipHidden);
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: ship %s on this screen (hull %s)"),
		bShipHidden ? TEXT("hidden") : TEXT("shown"), IsValid(Hull) ? (Hull->IsHidden() ? TEXT("hidden") : TEXT("visible")) : TEXT("not built yet"));
}

void APGBattleshipActor::MulticastCannonFx_Implementation(uint8 MuzzleIndex, FVector_NetQuantize Impact, bool bBig)
{
	if (GetNetMode() != NM_DedicatedServer && Weapons)
		Weapons->PlayCannonFx(MuzzleIndex, Impact, bBig);
}

void APGBattleshipActor::MulticastWreckPop_Implementation(FVector_NetQuantize Local, bool bWithFire)
{
	if (GetNetMode() != NM_DedicatedServer && Wreck)
		Wreck->PlayWreckPopFx(Local, bWithFire);
}

void APGBattleshipActor::MulticastWreckTouchdown_Implementation(FVector_NetQuantize Feet)
{
	if (GetNetMode() != NM_DedicatedServer && Wreck)
		Wreck->PlayTouchdownFx(Feet);
}

void APGBattleshipActor::OnRep_Seated()
{
	if (Helm)
		Helm->OnSeatChangedLocal();
}

void APGBattleshipActor::ServerSeatRequest(APlayerController* PC, bool bSit)
{
	if (HasAuthority() && Helm)
		Helm->ServerSeatRequest(PC, bSit);
}

void APGBattleshipActor::ServerHelmInput(APlayerController* PC, const FPGHelmInput& Input)
{
	if (HasAuthority() && Helm)
		Helm->ServerHelmInput(PC, Input);
}

void APGBattleshipActor::SetShipHidden(bool bHide)
{
	if (HasAuthority())
		bShipHidden = bHide; // 클라이언트는 OnRep_ShipHidden 으로 자기 껍데기에 같은 일을 한다
	SetActorHiddenInGame(bHide);
	if (!IsValid(Hull))
		return;
	// 껍데기는 붙여 둔 별도 액터고, 그 아래 부품 31개도 각각 액터다. 하나씩 숨겨야 한다.
	TArray<AActor*> Parts;
	Hull->GetAttachedActors(Parts, true, true);
	Parts.Add(Hull);
	for (AActor* Part : Parts)
		if (IsValid(Part))
			Part->SetActorHiddenInGame(bHide);
}

float APGBattleshipActor::TakeDamage(float Damage, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// 이미 추락 중이면 더 안 받는다. 체력은 0 에 그대로 남아야 한다 — 드래곤은 GetHealth() <= 0 을 보고 지상전으로
	// 넘어가는데(헤더 GetHealth 주석), 여기서 값이 흔들리면 그쪽 판단도 흔들린다.
	if (bWrecked)
		return 0.0f;
	const float Applied = Super::TakeDamage(Damage, DamageEvent, EventInstigator, DamageCauser);
	Health = FMath::Max(0.0f, Health - Applied);
	if (Health <= 0.0f && HasAuthority())
		Wreck->BeginWreck(DamageCauser, Applied);
	return Applied;
}

float APGBattleshipActor::GetDeckWorldZ() const
{
	return GetActorLocation().Z + DeckLocalZ;
}

FVector APGBattleshipActor::GetHangarEntranceWorld() const
{
	return GetActorTransform().TransformPosition(FVector(HangarEntranceLocalX, 0.0f, DeckLocalZ + 300.0f));
}

// 배가 지금 움직이고 있나.
//
// Size2D(수평 속도)가 아니라 Size(전체 속도)인 것이 핵심이다. 이륙은 제자리에서 300m 를 오르는 "순수 수직" 이동이라
//   수평 속도가 0 이다. Size2D 로 재던 동안 이륙 내내 붙들기가 한 번도 안 걸렸고, 갑판에 있던 차는 그대로 뒤에 남았다
//   (9/20 PIE: "전함은 움직이는데 같이 탄 차량은 전함이랑 같이 이동 안 해서... 밀린 위치에 그대로 있어. 변신 풀려서").
//   차가 배를 못 따라가면 아무도 안 탄 채 60초가 지나 역변신이 돌고, 여고생이 하늘에 생긴다.
// Motion 도 같이 보는 이유: ShipVelocity 는 "직전 프레임에 움직인 거리"라 출발 첫 프레임에는 아직 0 이다.
//   디렉터가 CruiseTo 를 부른 순간부터 잡아야 한 프레임(1m)도 안 밀린다.
bool APGBattleshipActor::IsShipMoving() const
{
	return Motion == EPGShipMotion::Cruise || ShipVelocity.Size() > 300.0f;
}

void APGBattleshipActor::FlyInFrom(const FVector& Start, const FVector& Hover)
{
	SetActorLocation(Start);
	// 시작하자마자 목표를 바라보게 세운다(돌면서 오는 게 아니라 넘어오는 그림이라야 한다).
	FRotator Facing = (Hover - Start).GetSafeNormal2D().Rotation();
	Facing.Pitch = 0.0f;
	Facing.Roll = 0.0f;
	SetActorRotation(Facing);
	// 받은 높이는 "배 밑바닥이 있을 높이". 크기를 잰 뒤(MeasureHull) 배 중심 높이로 고쳐 잡는다 —
	// 배가 169m 짜리라 중심을 기준으로 잡으면 밑바닥이 땅을 파고들거나 구름 위로 간다.
	bTargetIsBelly = true;
	TargetLocation = Hover;
	if (bAssembled)
		CruiseTo(Hover);
	else
		bPendingCruise = true; // 조립이 끝나면(MeasureHull) 출발한다
}

void APGBattleshipActor::CruiseTo(const FVector& Target)
{
	TargetLocation = Target;
	// 받은 높이가 "배 밑바닥"이면 여기서 배 중심 높이로 고친다. 조립이 끝난 뒤 다시 불러도(2단계 이륙) 같게 동작해야 한다.
	if (bTargetIsBelly && HullLocalBounds.IsValid)
	{
		TargetLocation.Z -= HullLocalBounds.Min.Z;
		bTargetIsBelly = false;
	}
	Motion = EPGShipMotion::Cruise;
	UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: cruising to %s (%.0f m away)"),
		*Target.ToCompactString(), FVector::Dist(GetActorLocation(), Target) * 0.01f);
}

// 배 안에 있는 플레이어 폰(사람 또는 사람이 탄 차)의 상태를 1초에 한 줄 찍는다.
//
// 왜(9/21 사용자: "함선 내부에서 차가 지나다니지 못한다" — 어디서 막혔는지는 말이 없음): 차가 못 움직이는 이유는
//   눈으로는 다 같아 보이지만 코드에서는 서로 다른 세 가지다 —
//   (a) 보이지 않는 상자에 부딪혔다(벽·문 막이·함교 바닥 턱 78cm),
//   (b) 배가 붙들고 있다(TickDeckCargo: 배가 움직이는 동안 차를 배에 붙이고 물리를 끈다 — 물리가 꺼진 차는 바퀴가 안 돈다),
//   (c) 도킹돼 있다(PGFlightKit: 갑판에 내려앉으면 물리 끄고 배에 붙임 — 조종자가 있으면 스스로 풀리게 돼 있다).
//   이 셋을 한 줄에 같이 찍으면 다음 PIE 한 판으로 갈린다. 추측으로 고치면 또 헛돈다.
// "닿은 상자" 는 폰의 충돌 상자 크기로 이 배의 컴포넌트만 겹침 검사한 결과다. DeckFloor 는 늘 닿으니 그건 정상이고,
//   DeckWallF / BridgeFloor(옆면) / SternApronFloor* 가 찍히면 그게 막은 것이다.
void APGBattleshipActor::TickPlayerInsideDiagnostics(float DeltaSeconds)
{
	static const bool bLogFromCommandLine = FParse::Param(FCommandLine::Get(), TEXT("PGShipInsideLog")); // 시험 실행에서 켜기(멀티 9/27)
	if (!(bLogPlayerInside || bLogFromCommandLine) || !HullLocalBounds.IsValid || !IsValid(InteriorRoot))
		return;
	PlayerInsideLogTime += DeltaSeconds;
	if (PlayerInsideLogTime < (bLogFromCommandLine ? 0.5f : 1.0f))
		return;
	PlayerInsideLogTime = 0.0f;
	const FTransform& ToWorld = GetActorTransform();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
			continue;
		const FVector Local = ToWorld.InverseTransformPosition(Pawn->GetActorLocation());
		// 선체 안, 갑판 높이 근처만. 밖에서 날아다니는 동안은 찍을 게 없다.
		if (Local.X < HullLocalBounds.Min.X || Local.X > HullLocalBounds.Max.X
			|| FMath::Abs(Local.Y) > HullLocalBounds.GetSize().Y * 0.3f
			|| (!bLogFromCommandLine && (Local.Z < DeckLocalZ - 600.0f || Local.Z > DeckLocalZ + 6000.0f))
			|| Local.Z < HullLocalBounds.Min.Z || Local.Z > HullLocalBounds.Max.Z)
			continue;
		UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Pawn->GetRootComponent());
		const bool bSimulating = Body && Body->IsSimulatingPhysics();
		const AActor* Parent = Pawn->GetAttachParentActor();
		bool bHeldByDeck = false;
		for (const FPGHeldPawn& Each : DeckCargo)
			if (Each.Pawn.Get() == Pawn)
				bHeldByDeck = true;
		TArray<FString> Touching;
		if (Body)
		{
			TArray<FOverlapResult> Hits;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGShipInsideDiag), false, Pawn);
			// 폰의 월드 상자보다 20cm 크게 — 딱 맞닿아 멈춘 상자도 잡힌다.
			GetWorld()->OverlapMultiByChannel(Hits, Body->Bounds.Origin, FQuat::Identity, ECC_Pawn,
				FCollisionShape::MakeBox(Body->Bounds.BoxExtent + FVector(20.0f)), Params);
			for (const FOverlapResult& Hit : Hits)
				if (const UPrimitiveComponent* Touched = Hit.GetComponent(); Touched && Touched->GetOwner() == this)
					Touching.AddUnique(Touched->GetName());
		}
		const TCHAR* MotionText = Motion == EPGShipMotion::Cruise ? TEXT("cruise") : (Motion == EPGShipMotion::Hover ? TEXT("hover") : TEXT("parked"));
		// 밟고 선 바닥(캐릭터의 "움직이는 바닥"). 이것이 배와 같이 움직이는 부품이어야 사람이 배를 따라온다.
		const ACharacter* AsCharacter = Cast<ACharacter>(Pawn);
		const UPrimitiveComponent* Base = AsCharacter ? AsCharacter->GetMovementBase() : nullptr;
		const FString BaseText = Base ? FString::Printf(TEXT("%s.%s (%s)"), *GetNameSafe(Base->GetOwner()), *Base->GetName(),
			Base->Mobility == EComponentMobility::Movable ? TEXT("movable") : TEXT("NOT movable")) : FString(TEXT("none"));
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: inside — %s at local (%.0f, %.0f, %.0f) (deck z %.0f), speed %.0f cm/s, standing on %s, physics %s, attached to %s%s, ship %s (moving=%s, v=%.0f), touching ship boxes: %s"),
			*Pawn->GetName(), Local.X, Local.Y, Local.Z, DeckLocalZ, Pawn->GetVelocity().Size(), *BaseText,
			bSimulating ? TEXT("ON") : TEXT("OFF"),
			Parent ? *Parent->GetName() : TEXT("nothing"),
			bHeldByDeck ? TEXT(" [held by the deck hold]") : TEXT(""),
			MotionText, IsShipMoving() ? TEXT("yes") : TEXT("no"), ShipVelocity.Size(),
			Touching.IsEmpty() ? TEXT("none") : *FString::Join(Touching, TEXT(", ")));
	}
}

// 이 화면 사람이 배 위에 서 있나(밟은 바닥이 배 또는 배에 붙은 부품). 조종석에 앉은 사람은 걷기가 꺼져 밟은 바닥이 없다.
bool APGBattleshipActor::IsLocalWalkerAboard() const
{
	if (const APlayerController* LocalPC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (const ACharacter* Walker = Cast<ACharacter>(LocalPC->GetPawn()))
			if (const UPrimitiveComponent* Floor = Walker->GetMovementBase())
				if (const AActor* FloorOwner = Floor->GetOwner())
					return FloorOwner == this || FloorOwner->IsAttachedTo(this);
	return false;
}

void APGBattleshipActor::PostNetReceiveLocationAndRotation()
{
	// 배 위에 서 있는 사람 화면은 엔진 그대로(받는 즉시 옮김) — 부드럽게 하면 배가 빨리 움직일 때 서 있는 사람이 배를 못 따라왔다
	//   (두 사람 시험 57m). 받는 즉시 옮겨야 캐릭터가 같은 프레임에 "움직이는 바닥" 을 따라간다.
	if (GetLocalRole() == ROLE_SimulatedProxy && IsLocalWalkerAboard())
	{
		ProxySmoother.Reset();
		Super::PostNetReceiveLocationAndRotation();
		return;
	}
	if (GetLocalRole() == ROLE_SimulatedProxy)
	{
		const FRepMovement& Rep = GetReplicatedMovement();
		ProxySmoother.Receive(FRepMovement::RebaseOntoLocalOrigin(Rep.Location, this), Rep.Rotation, GetWorld()->GetTimeSeconds());
		return;
	}
	Super::PostNetReceiveLocationAndRotation();
}

void APGBattleshipActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
	{
		// 멀티(9/28): 받은 위치로 부드럽게 — 단, 이 화면 사람이 배 위에 서 있으면(밟은 바닥이 배·배 부품) 예전처럼 받은 위치 그대로.
		//   부드럽게 하면 배가 빨리 움직이는 동안 서 있는 사람이 배를 못 따라와 57m 까지 떨어졌다(두 사람 시험, 넣기 전 13cm).
		//   조종석에 앉은 사람(걷기 꺼짐, 밟은 바닥 없음)과 밖에서 보는 사람은 부드럽게 본다.
		if (!IsLocalWalkerAboard())
			ProxySmoother.Step(this, GetWorld()->GetTimeSeconds(), DeltaSeconds);
	}
	// 배가 실제로 움직인 속도를 잰다(물리가 아니라 좌표 이동이라 엔진이 알려 주지 않는다). 클라이언트도 잰다 — 계기판 속도.
	const FVector NowLocation = GetActorLocation();
	ShipVelocity = DeltaSeconds > KINDA_SMALL_NUMBER ? (NowLocation - LastShipLocation) / DeltaSeconds : FVector::ZeroVector;
	if (LastShipLocation.IsZero())
		ShipVelocity = FVector::ZeroVector; // 첫 프레임: 원점에서 온 것처럼 잡히면 안 된다
	LastShipLocation = NowLocation;
	// 멀티(9/27): 모든 컴퓨터에서 — 주포 빛줄기 줄이기, 계기판·옆 화면, 이 컴퓨터 사람의 조종석(키 읽기·서버로 보내기).
	Helm->TickBeamFade(DeltaSeconds);
	Helm->TickBridge(DeltaSeconds);
	Helm->TickLocalPilot(DeltaSeconds);
	// 멀티(9/27): 뒤쪽 선반·뒷문도 모든 컴퓨터에서 돌린다. 부품(컴포넌트) 위치는 복제되지 않아 서버만 돌리면
	//   클라이언트에서는 문이 영영 열려 있었다. 조건(정박 여부 Motion, 조종석 bSeated, 피날레 단계)은 모두 복제되는 값이라 같은 때 움직인다.
	Deck->TickHullRetract(DeltaSeconds);
	Deck->TickRearDoorClose(DeltaSeconds);
	Deck->TickRearDoorReopen(DeltaSeconds);
	Helm->TickLocalPilot(DeltaSeconds);
	if (!HasAuthority())
	{
		Helm->TickBridgeView(DeltaSeconds); // 앉은 사람 화면의 궤도 카메라(이 컴퓨터에서)
		return; // 자리는 서버가 정하고 복제된다
	}

	Deck->TickDeckCargo(DeltaSeconds);
	Deck->TickCatchFallers(DeltaSeconds);
	TickPlayerInsideDiagnostics(DeltaSeconds);
	Helm->TickSeatServer(DeltaSeconds);
	Helm->TickHelm(DeltaSeconds);
	if (bWrecked)
	{
		// 체력 0 뒤로는 추락이 위치를 맡는다(Cruise/Hover 는 BeginWreck 이 Parked 로 돌려놨다).
		Wreck->TickWreck(DeltaSeconds);
	}
	else if (Motion == EPGShipMotion::Cruise)
	{
		TickCruise(DeltaSeconds);
	}
	else if (Motion == EPGShipMotion::Hover)
	{
		// 정박한 뒤에는 조종석에 앉은 사람이 몬다. 고도는 건드리지 않는다(아래 흔들림이 Z 를 맡는다).
		Helm->TickHelmDrive(DeltaSeconds);
		Helm->TickHelmClimb(DeltaSeconds);
		// 떠 있는 동안 아주 느리게 위아래로. 완전히 멈춰 있으면 붙여 넣은 그림처럼 보인다.
		const float Seconds = static_cast<float>(GetWorld()->GetTimeSeconds() - HoverStartTime);
		FVector Location = GetActorLocation();
		// 흔들림 폭은 아주 작게. 조금만 커도 갑판에 내려앉은 차(물리 몸)가 갑판을 뚫고 떨어진다
		// (9/20 PIE: "차를 안에 넣었는데 갑자기 떨어졌다"). 눈으로 알아볼 정도만 남긴다.
		Location.Z = HoverBaseZ + FMath::Sin(Seconds * 0.25f) * 12.0f;
		SetActorLocation(Location);
	}
	// 배가 이번 프레임에 다 움직인 뒤에 카메라를 놓는다(TickBridgeView 주석 참고).
	Helm->TickBridgeView(DeltaSeconds);
}

void APGBattleshipActor::TickCruise(float DeltaSeconds)
{
	const FVector Location = GetActorLocation();
	const FVector ToTarget = TargetLocation - Location;
	const float Distance = ToTarget.Size();
	if (Distance < 200.0f)
	{
		SetActorLocation(TargetLocation);
		SetActorRotation(FRotator(0.0f, GetActorRotation().Yaw, 0.0f));
		Motion = EPGShipMotion::Hover;
		bHasArrivedOnce = true; // 이 뒤로 다시 움직이면 이륙이다 — 그때 뒤쪽 선반이 들어간다(TickHullRetract 주석)
		HoverStartTime = GetWorld()->GetTimeSeconds();
		HoverBaseZ = TargetLocation.Z;
		HelmAnchor = TargetLocation; // 여기서부터 HelmRangeCm 안까지만 몰 수 있다
		Bank = 0.0f;
		UE_LOG(LogPGObjects, Display, TEXT("PGBattleship: arrived, hovering at %s"), *TargetLocation.ToCompactString());
		OnArrived.Broadcast(this);
		return;
	}

	// 방향: 목표 쪽으로 정해진 각속도까지만 돈다. 도는 양에 비례해서 기운다(뱅크).
	const FRotator Rotation = GetActorRotation();
	// 수직으로만 오를 때(이륙)는 목표의 수평 거리가 0 이라 방향이 0도로 나온다.
	// 그대로 두면 437m 짜리가 상승 중에 제자리에서 돌고, 뒤쪽 177m 에 매달린 발판이 크게 휩쓸린다(9/20 조사).
	const float WantYaw = ToTarget.Size2D() > 100.0f ? ToTarget.GetSafeNormal2D().Rotation().Yaw : Rotation.Yaw;
	const float NewYaw = FMath::FixedTurn(Rotation.Yaw, WantYaw, TurnRateDeg * DeltaSeconds);
	const float YawDelta = FRotator::NormalizeAxis(NewYaw - Rotation.Yaw);
	const float WantBank = FMath::Clamp(YawDelta / FMath::Max(KINDA_SMALL_NUMBER, TurnRateDeg * DeltaSeconds), -1.0f, 1.0f) * MaxBankDeg;
	Bank = FMath::FInterpTo(Bank, WantBank, DeltaSeconds, 1.5f);
	// 고도 차이는 뱃머리 각도로 보여 준다(내려오는 동안 코가 살짝 숙는다).
	const float WantPitch = FMath::Clamp(FMath::RadiansToDegrees(FMath::Atan2(ToTarget.Z, ToTarget.Size2D())), -6.0f, 6.0f);
	const float NewPitch = FMath::FInterpTo(Rotation.Pitch, WantPitch, DeltaSeconds, 1.0f);

	const FVector Step = ToTarget / Distance * FMath::Min(CruiseSpeed * DeltaSeconds, Distance);
	SetActorLocationAndRotation(Location + Step, FRotator(NewPitch, NewYaw, Bank));
}
