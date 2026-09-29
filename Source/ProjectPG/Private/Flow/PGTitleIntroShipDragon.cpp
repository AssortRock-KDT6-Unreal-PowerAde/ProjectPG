// 타이틀 인트로 ① 하늘: 전함이 머리 위를 지나 노을 쪽으로 멀어지고, 드래곤이 카메라 위를 스쳐 배 꼬리를 뒤쫓는다.
// 그다음 카메라가 내려와 불타는 잔해 앞의 캐릭터(평소 타이틀 구도)에 멈춘다.
//
// 좌표 적는 법: 하늘 장면의 길은 "하늘 카메라 기준 앞(F)·오른쪽(R)·위(U), 단위 m" 로 적는다.
//   월드 좌표로 적으면 "카메라에서 얼마나 떨어져 지나가나" 가 안 보여서 구도를 고치기 어렵다.
#include "Flow/PGTitleIntro.h"
#include "Flow/PGFlowStageSet.h"
#include "Flow/PGTitleIntroSet.h"

#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"

namespace PGIntroShipLocal
{
	// 배(게임 전함과 같은 팩 화물선)·드래곤(게임 드래곤 보스와 같은 메시·날갯짓) 에셋은 데이터 에셋 DA_PGTitleIntro
	//   (없으면 원래 코드 에셋)에 있다(9/23 블루프린트 분리). 배율은 게임 전함과 같은 5(길이 약 218m).
	constexpr float IntroShipScale = 5.0f;
	// 드래곤 날개 반쪽 길이(cm). 게임(배율 25 = 날개폭 410m)대로 두면 카메라 옆을 스칠 때 화면이 비늘 한 장으로 꽉 찬다.
	//   날개폭 100m 로 맞추면 스칠 때 화면 오른쪽 절반을 채우고, 멀어지면 전함 뒤를 쫓는 크기로 읽힌다.
	constexpr float IntroDragonHalfWingCm = 5000.0f;

	constexpr float IntroShipDuration = 9.6f;
	// 하늘 카메라 자리. 캐릭터(원점) 뒤쪽 위 30m, 노을(해는 +Y·-X 쪽 낮게) 방향을 본다.
	const FVector IntroSkyCamera(4500.0f, -1200.0f, 3000.0f);
	// 하늘 카메라가 보는 방향(요). 150 이면 해가 화면 왼쪽 끝에 걸린다(PGFlowStage 의 타이틀 해 = 요 120 쪽).
	constexpr float IntroSkyYaw = 150.0f;

	struct FSkyKey
	{
		float Time;
		float F, R, U; // m
	};
	// 전함: 카메라 뒤에서 나와 머리 위 30m 를 낮게 지나 노을 쪽으로 멀어지며 천천히 올라간다. 약 95m/s.
	//   점 사이 시간을 고르게 두어야 곡선이 "천천히 출발 → 빨라짐" 없이 일정한 속도로 간다.
	//   너무 가파르게 오르면 화면 위로 빠져나간다(9/22 시험 촬영: 선체 중심이 피벗보다 29m 위라 더 높아 보인다).
	//   끝에서는 오른쪽(-R)으로 비켜 타이틀 화면 왼쪽 밖(약 42도)에 있다 — 치울 때 화면에서 툭 사라지지 않는다.
	const FSkyKey IntroShipKeys[] = {
		{ 0.0f, -150.0f, 20.0f, 30.0f },
		{ 3.1f, 110.0f, 5.0f, 32.0f },
		{ 6.2f, 400.0f, -40.0f, 60.0f },
		{ 9.6f, 760.0f, -150.0f, 110.0f },
	};
	// 드래곤: 전함을 뒤에서 쫓는다. 늘 배 피벗보다 약 170m 뒤(배 반길이 109m + 드래곤 반길이 약 35m + 여유)라서
	//   배의 앞뒤 축으로는 겹칠 수가 없다 — 위아래·좌우는 자유롭게 써도 된다(9/22 사용자: "드래곤이 전함을 뚫고 지나간다").
	//   그래서 배보다 낮게, 카메라 오른쪽 위를 스치며(3.5~4초, 흔들림) 앞으로 나가고, 뒤로는 배 꼬리를 따라 올라간다.
	//   배보다 오른쪽(+R)으로 40~60m 비켜 난다 — 바로 뒤에 붙으면 아래에서 볼 때 배 꼬리와 겹쳐 "배 안에 있는" 것처럼 보였다.
	//   이 길이 모자라면 KeepDragonOffShip 이 마지막으로 밀어낸다(시험 촬영 로그에 밀린 프레임 수가 찍힌다).
	const FSkyKey IntroDragonKeys[] = {
		{ 0.0f, -330.0f, 90.0f, 40.0f }, // 카메라 뒤. 처음부터 배 꼬리 뒤에 있어야 한다 — 첫 점보다 이른 시각엔 첫 점에 서 있으므로
		{ 2.0f, -170.0f, 60.0f, 30.0f },
		{ 3.5f, -35.0f, 35.0f, 18.0f },
		{ 5.0f, 100.0f, 55.0f, 18.0f },
		{ 6.8f, 294.0f, 30.0f, 45.0f },
		{ 9.6f, 590.0f, -90.0f, 90.0f },
	};

	FVector SkyForward() { return FRotator(0.0f, IntroSkyYaw, 0.0f).Vector(); }
	FVector SkyRight() { return FRotator(0.0f, IntroSkyYaw + 90.0f, 0.0f).Vector(); }
	FVector SkyPoint(float F, float R, float U)
	{
		return IntroSkyCamera + (SkyForward() * F + SkyRight() * R + FVector::UpVector * U) * 100.0f;
	}
	// 하늘 카메라에서 (요 IntroSkyYaw, 위로 PitchDeg) 방향으로 20m 앞의 점 = 카메라가 보는 점.
	FVector SkyLook(const FVector& From, float PitchDeg)
	{
		return From + FRotator(PitchDeg, IntroSkyYaw, 0.0f).Vector() * 2000.0f;
	}
}

APGTitleIntroShipDragon::APGTitleIntroShipDragon()
{
	using namespace PGIntroShipLocal;
	// 카메라 컷. 마지막(9.6초)의 "평소 타이틀 구도" 는 뼈대가 자동으로 붙인다.
	const FVector Sky0 = IntroSkyCamera;
	const FVector Sky1 = IntroSkyCamera + SkyForward() * 150.0f;
	const FVector Sky2 = IntroSkyCamera + SkyForward() * 260.0f - FVector(0.0f, 0.0f, 100.0f);
	// 0초: 고개를 크게 들어(28도) 머리 위로 지나가는 배 밑바닥을 본다. 넓게(72도).
	CamKeys.Add({ 0.0f, Sky0, SkyLook(Sky0, 28.0f), 72.0f, 0.0f });
	// 3초: 배가 앞으로 멀어지니 고개를 내린다. 곧 드래곤이 카메라 위를 스쳐 앞으로 나간다.
	CamKeys.Add({ 3.0f, Sky1, SkyLook(Sky1, 16.0f), 72.0f, 0.0f });
	CamKeys.Add({ 4.8f, Sky2, SkyLook(Sky2, 11.0f), 68.0f, 0.0f });
	// 6.2초: 조금 내려오며 멀어지는 배와 뒤쫓는 드래곤을 한 화면에(보는 점 = 그 시각의 둘 사이).
	//   보는 점은 그 방향으로 30m 앞에 둔다. 350m 밖의 점을 그대로 쓰면 다음 컷(무대 20m 앞)과 곡선으로 이을 때
	//   방향이 크게 넘쳐 카메라가 한순간 뒤·아래를 봤다(9/22 시험 촬영: 8초 무렵 화면이 풀밭으로 번졌다).
	const FVector ChaseCamera(3000.0f, -800.0f, 2000.0f);
	const FVector ChaseTarget = (SkyPoint(400.0f, -40.0f, 60.0f) + SkyPoint(260.0f, 50.0f, 40.0f)) * 0.5f;
	CamKeys.Add({ 6.2f, ChaseCamera, ChaseCamera + (ChaseTarget - ChaseCamera).GetSafeNormal() * 3000.0f, 60.0f, 0.0f });
	// 7.6초: 무대(불타는 잔해·캐릭터) 쪽으로 고개를 숙이며 내려온다 → 8.8초 캐릭터 뒤쪽 낮게 → 9.6초 평소 타이틀 구도.
	//   마지막 두 컷을 촘촘히 둔 이유: 한 번에 17m 를 내려오게 했더니 곡선이 땅 밑으로 꺼지며 화면이 번졌다(9/22 시험 촬영).
	CamKeys.Add({ 7.6f, FVector(1400.0f, -220.0f, 560.0f), FVector(-500.0f, 80.0f, 180.0f), 60.0f, 0.0f });
	CamKeys.Add({ 8.8f, FVector(700.0f, -60.0f, 170.0f), FVector(0.0f, 0.0f, 90.0f), 57.0f, 0.0f });
	ShotTimes = { 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 3.5f, 4.0f, 4.5f, 5.0f, 5.5f, 6.0f, 6.5f, 7.0f, 7.5f, 8.0f, 8.5f, 9.0f, 9.5f };
}

float APGTitleIntroShipDragon::GetDuration() const
{
	return PGIntroShipLocal::IntroShipDuration;
}

void APGTitleIntroShipDragon::BeginShots()
{
	using namespace PGIntroShipLocal;
	for (const FSkyKey& Key : IntroShipKeys)
		ShipPath.Add({ Key.Time, SkyPoint(Key.F, Key.R, Key.U) });
	for (const FSkyKey& Key : IntroDragonKeys)
		DragonPath.Add({ Key.Time, SkyPoint(Key.F, Key.R, Key.U) });
	SpawnShip();
	SpawnDragon();

	// 넓은 바닥: 무대 바닥과 같은 평면·재질을 8km 로 늘여 3cm 아래에 깐다. 가까운 곳은 무대 바닥이 덮고, 먼 곳은 안개가 먹는다.
	//   평면·재질은 무대 에셋(DA_PGFlowStage)의 바닥 칸을 같이 쓴다 — 무대 바닥을 바꾸면 인트로 바닥도 따라간다.
	const UPGFlowStageSet* Stage = UPGFlowStageSet::GetActive();
	UStaticMesh* Plane = LoadIntroAsset<UStaticMesh>(*IntroPath(Stage->GroundMesh));
	UMaterialInterface* GroundMaterial = LoadIntroAsset<UMaterialInterface>(*IntroPath(Stage->GroundMaterial));
	if (Plane)
	{
		const FTransform Transform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, -3.0f), FVector(8000.0f));
		if (AStaticMeshActor* Ground = GetWorld()->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, this, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn))
		{
			UStaticMeshComponent* Mesh = Ground->GetStaticMeshComponent();
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetStaticMesh(Plane);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			if (GroundMaterial)
				Mesh->SetMaterial(0, GroundMaterial);
			Ground->FinishSpawning(Transform);
			WideGround = Ground;
		}
	}
}

void APGTitleIntroShipDragon::SpawnShip()
{
	using namespace PGIntroShipLocal;
	UClass* ShipClass = LoadIntroAsset<UClass>(*IntroPath(UPGTitleIntroSet::GetActive()->ShipClass));
	if (!ShipClass)
		return;
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FTransform Start(FRotator::ZeroRotator, EvalPath(ShipPath, 0.0f), FVector(IntroShipScale));
	Ship = GetWorld()->SpawnActor<AActor>(ShipClass, Start, Params);
	if (!IsValid(Ship))
		return;
	// 팩 부품은 Static 으로 들어와 있다. 그대로 두면 SetActorLocation 해도 제자리에 남는다(게임 전함도 같은 이유로 바꾼다).
	// 보기만 하는 배라 충돌·그림자는 끈다. 멀어도 계속 보이게 컬링 거리를 없앤다.
	int32 Thrusters = 0;
	int32 HiddenLegs = 0;
	for (UActorComponent* Component : Ship->GetComponents())
	{
		USceneComponent* Scene = Cast<USceneComponent>(Component);
		if (!Scene)
			continue;
		Scene->SetMobility(EComponentMobility::Movable);
		// 날고 있는 배라 뒷문은 닫혀 있어야 한다. 블루프린트 기본 상태는 열린 문(정박 때 입구)이라, 사용자가 만든 경첩(DoorHinge)을
		// 닫힌 각도(블루프린트 타임라인 B 값: 롤 -55)로 바로 세운다(9/22 사용자: "전함은 또 문이 열려 있네").
		if (Scene->GetFName() == TEXT("DoorHinge"))
			Scene->SetRelativeRotation(FRotator(0.0f, 0.0f, -55.0f));
		UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Scene);
		if (!Primitive)
			continue;
		Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Primitive->SetCastShadow(false);
		Primitive->SetCullDistance(0.0f);
		const UStaticMeshComponent* AsMesh = Cast<UStaticMeshComponent>(Primitive);
		const FString MeshName = AsMesh && AsMesh->GetStaticMesh() ? AsMesh->GetStaticMesh()->GetName() : FString();
		// 착륙다리는 게임에서도 숨긴다(날아다니는 배라 땅에 닿을 일이 없다 — APGBattleshipActor::MeasureHull).
		if (MeshName.Contains(TEXT("Legs")))
		{
			Primitive->SetVisibility(false);
			++HiddenLegs;
			continue;
		}
		// 엔진이 달아오른 느낌: 추진기 부품마다 푸른 불빛 하나. 노을 속 어두운 선체에서 엔진 자리만 빛나 "날고 있다" 가 읽힌다.
		if (MeshName.Contains(TEXT("Thruster")))
		{
			UPointLightComponent* Glow = NewObject<UPointLightComponent>(Ship);
			Glow->SetupAttachment(Primitive);
			Glow->RegisterComponent();
			Glow->SetWorldLocation(Primitive->Bounds.Origin);
			Glow->SetIntensityUnits(ELightUnits::Candelas);
			Glow->SetIntensity(60000.0f);
			Glow->SetLightColor(FLinearColor(0.45f, 0.7f, 1.0f));
			Glow->SetAttenuationRadius(4000.0f);
			Glow->SetCastShadows(false);
			++Thrusters;
		}
	}
	// 선체 상자: 보이는 부품만 모아 "배의 방향·자리 기준(배율 없이, cm)" 으로 잰다. 배는 곧게 날기만 해서 모양이 안 바뀌니 한 번이면 된다.
	//   숨긴 착륙다리는 뺀다 — 넣으면 보이지도 않는 다리 때문에 드래곤이 괜히 멀리 밀린다.
	const FTransform ShipFrame(Ship->GetActorQuat(), Ship->GetActorLocation());
	for (UActorComponent* Component : Ship->GetComponents())
		if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Primitive->IsVisible() && Primitive->IsRegistered())
			ShipLocalBox += Primitive->CalcBounds(Primitive->GetComponentTransform() * ShipFrame.Inverse()).GetBox();
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: ship %s spawned (scale %.0f, %d thruster glow(s), %d leg part(s) hidden, hull %s m)"),
		*ShipClass->GetName(), IntroShipScale, Thrusters, HiddenLegs, *(ShipLocalBox.GetSize() * 0.01f).ToCompactString());
}

void APGTitleIntroShipDragon::SpawnDragon()
{
	using namespace PGIntroShipLocal;
	const UPGTitleIntroSet* Set = UPGTitleIntroSet::GetActive();
	USkeletalMesh* Mesh = LoadIntroAsset<USkeletalMesh>(*IntroPath(Set->DragonMesh));
	if (!Mesh)
		return;
	// 배율은 에셋을 재서 정한다(드래곤 보스와 같은 방식: 피벗이 발밑이라 "원점에서 가장 먼 곳" = |중심| + 반크기).
	const FBoxSphereBounds Bounds = Mesh->GetBounds();
	const FVector Reach = Bounds.Origin.GetAbs() + Bounds.BoxExtent;
	const float Scale = IntroDragonHalfWingCm / FMath::Max(static_cast<float>(FMath::Max(Reach.X, Reach.Y)), 1.0f);
	// 길은 "몸통 한가운데가 지나갈 자리" 로 적었다. 그런데 이 메시는 몸통 중심이 피벗(발밑)보다 한참 위에 있다(원본 약 8.6m → 배율 6.8 이면 58m).
	//   그만큼 길을 내려 준다 — 안 그러면 드래곤이 화면 위로 지나가 버린다(9/22 시험 촬영에서 실제로 안 보였다).
	const float CenterUp = static_cast<float>(Bounds.Origin.Z) * Scale;
	for (FPGIntroPathKey& Key : DragonPath)
		Key.Location.Z -= CenterUp;
	Dragon = SpawnSkeletal(Mesh, FTransform(FRotator::ZeroRotator, EvalPath(DragonPath, 0.0f), FVector(Scale)));
	if (!IsValid(Dragon))
		return;
	// 선체와의 틈을 잴 몸 상자 = 메시 에셋의 상자(메시 좌표). 컴포넌트 경계는 아래에서 3배로 부풀려서 틈을 재는 데 못 쓴다.
	DragonMeshBox = FBox(Bounds.Origin - Bounds.BoxExtent, Bounds.Origin + Bounds.BoxExtent);
	USkeletalMeshComponent* Body = Dragon->GetSkeletalMeshComponent();
	Body->bComponentUseFixedSkelBounds = true; // 날갯짓이 커서 경계가 흔들리면 화면에서 깜빡인다(드래곤 보스와 같은 이유)
	Body->SetBoundsScale(3.0f);
	if (UAnimSequence* Fly = LoadIntroAsset<UAnimSequence>(*IntroPath(Set->DragonFly)))
		Body->PlayAnimation(Fly, true);
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: dragon spawned at scale %.2f (wingspan about %.0f m, body %.0f m above the pivot)"),
		Scale, IntroDragonHalfWingCm * 2.0f / 100.0f, CenterUp / 100.0f);
}

void APGTitleIntroShipDragon::TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation)
{
	if (IsValid(Ship))
	{
		// 팩 화물선은 길이축이 로컬 Y 이고 뱃머리가 -Y 다(게임 전함이 +90 돌려 붙이는 이유). 그래서 "-Y = 진행 방향" 으로 세운다.
		const FVector Direction = PathVelocity(ShipPath, T).GetSafeNormal();
		const FRotator Rotation = FRotationMatrix::MakeFromYZ(-Direction, FVector::UpVector).Rotator();
		Ship->SetActorLocationAndRotation(EvalPath(ShipPath, T), Rotation);
	}
	if (IsValid(Dragon))
	{
		const FVector Velocity = PathVelocity(DragonPath, T);
		// 진짜 원인: 이 드래곤 메시는 머리가 +Y 쪽이다(뼈 확인: Head y=+127). 그래서 "날아가는 방향을 봐라" 가 옆구리를 내밀게 했다
		// (9/22 사용자: "옆으로 날아가며 전함을 따라간다"). 아래에서 액터 회전에 요 -90 을 덧붙여 머리를 앞으로 돌린다.
		// 머리는 날아가는 방향 위주(7), 배 쪽으로 살짝(3) — 쫓는 느낌만 준다.
		FVector Facing = Velocity.GetSafeNormal();
		if (IsValid(Ship))
		{
			const FVector ToShip = (Ship->GetActorLocation() - Dragon->GetActorLocation()).GetSafeNormal();
			if (!ToShip.IsNearlyZero())
				Facing = (ToShip * 0.3f + Facing * 0.7f).GetSafeNormal();
		}
		FRotator Rotation = Facing.Rotation();
		Rotation.Pitch = FMath::Clamp(Rotation.Pitch, -25.0f, 25.0f);
		// 기울기: 도는 빠르기(요 변화/초)에 비례해 도는 쪽으로 몸을 눕힌다. 곧게 날면 0 으로 돌아온다.
		if (bDragonYawValid && DeltaSeconds > KINDA_SMALL_NUMBER)
		{
			const float YawRate = FMath::FindDeltaAngleDegrees(LastDragonYaw, Rotation.Yaw) / DeltaSeconds;
			DragonRoll = FMath::FInterpTo(DragonRoll, FMath::Clamp(YawRate * 0.6f, -30.0f, 30.0f), DeltaSeconds, 3.0f);
		}
		LastDragonYaw = Rotation.Yaw;
		bDragonYawValid = DeltaSeconds > 0.0f;
		Rotation.Roll = DragonRoll;
		// 메시 앞(+Y) 을 진행 방향으로: 원하는 회전 뒤에 요 -90 을 붙인다(게임 드래곤 보스는 메시 컴포넌트에 같은 -90 을 준다).
		const FQuat MeshFix = FRotator(0.0f, -90.0f, 0.0f).Quaternion();
		Dragon->SetActorLocationAndRotation(EvalPath(DragonPath, T), Rotation.Quaternion() * MeshFix);
		KeepDragonOffShip(T);
	}
	// 드래곤이 카메라 위를 스칠 때(4초 전후) 낮게 흔들린다. 바람에 떠밀리는 느낌이라 느리고 묵직하게.
	const float Envelope = FMath::Exp(-FMath::Square((T - 3.9f) / 0.45f));
	OutShakeLocation = FVector(FMath::Sin(T * 23.0f) * 25.0f, FMath::Sin(T * 17.0f + 1.0f) * 30.0f, FMath::Sin(T * 29.0f + 2.0f) * 35.0f) * Envelope;
	OutShakeRotation = FRotator(FMath::Sin(T * 19.0f) * 0.8f, FMath::Sin(T * 13.0f + 0.5f) * 0.4f, FMath::Sin(T * 21.0f + 1.5f) * 1.2f) * Envelope;
}

// 드래곤 몸이 선체에 15m 보다 가까워지면 가장 짧은 쪽으로 밀어낸다(9/22 사용자: "드래곤이 전함을 뚫고 지나간다").
//   길(IntroDragonKeys)은 원래 배 뒤·아래로 떨어지게 적었고, 이것은 그래도 모자랄 때의 안전망이다. 밀린 프레임 수와 가장 가까운 틈을 끝에 로그로 남긴다.
// 어떻게: 드래곤 메시 상자의 여덟 꼭짓점을 배 기준 좌표로 옮겨 상자를 만들고, 배 상자와 축(앞뒤·좌우·위아래)마다 떨어진 거리를 잰다.
//   어느 한 축이라도 15m 이상 떨어져 있으면 안 닿은 것이다. 모자라면 가장 조금 밀어도 되는 축으로 민다.
//   게임의 드래곤 보스도 같은 방식이다(APGDragonBoss::KeepBodyOutOfHull).
void APGTitleIntroShipDragon::KeepDragonOffShip(float T)
{
	constexpr float IntroDragonShipMarginCm = 1500.0f;
	if (!IsValid(Ship) || !IsValid(Dragon) || !ShipLocalBox.IsValid || !DragonMeshBox.IsValid)
		return;
	const FTransform ShipFrame(Ship->GetActorQuat(), Ship->GetActorLocation());
	auto MeasureGap = [&](FBox& OutBody) -> float
	{
		const FTransform& DragonTransform = Dragon->GetSkeletalMeshComponent()->GetComponentTransform();
		OutBody = FBox(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local((Corner & 1) ? DragonMeshBox.Max.X : DragonMeshBox.Min.X, (Corner & 2) ? DragonMeshBox.Max.Y : DragonMeshBox.Min.Y,
				(Corner & 4) ? DragonMeshBox.Max.Z : DragonMeshBox.Min.Z);
			OutBody += ShipFrame.InverseTransformPosition(DragonTransform.TransformPosition(Local));
		}
		float Gap = -TNumericLimits<float>::Max();
		for (int32 Axis = 0; Axis < 3; ++Axis)
			Gap = FMath::Max(Gap, static_cast<float>(FMath::Max(ShipLocalBox.Min[Axis] - OutBody.Max[Axis], OutBody.Min[Axis] - ShipLocalBox.Max[Axis])));
		return Gap;
	};
	FBox BodyBox;
	const float Before = MeasureGap(BodyBox);
	if (Before < IntroDragonShipMarginCm)
	{
		FVector Push = FVector::ZeroVector;
		float Best = TNumericLimits<float>::Max();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const bool bPositive = BodyBox.GetCenter()[Axis] >= ShipLocalBox.GetCenter()[Axis];
			const float AxisGap = static_cast<float>(bPositive ? BodyBox.Min[Axis] - ShipLocalBox.Max[Axis] : ShipLocalBox.Min[Axis] - BodyBox.Max[Axis]);
			const float Need = IntroDragonShipMarginCm - AxisGap;
			if (Need < Best)
			{
				Best = Need;
				Push = FVector::ZeroVector;
				Push[Axis] = bPositive ? Need : -Need;
			}
		}
		Dragon->AddActorWorldOffset(ShipFrame.TransformVector(Push));
		++DragonPushFrames;
	}
	const float After = MeasureGap(BodyBox);
	MinDragonGap = FMath::Min(MinDragonGap, After);
	// 시험 촬영에서는 0.5초마다 틈을 남긴다 — 스크린샷 한 장으로는 "어느 프레임에도 안 닿았다" 를 확인할 수 없다.
	if (IsTestRun() && T >= NextGapLogAt)
	{
		NextGapLogAt = FMath::FloorToFloat(T / 0.5f) * 0.5f + 0.5f;
		UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: T=%.1fs dragon-hull gap %.1f m (path %.1f m%s)"), T, After * 0.01f, Before * 0.01f,
			Before < IntroDragonShipMarginCm ? TEXT(", pushed") : TEXT(""));
	}
}

void APGTitleIntroShipDragon::CleanupShots(bool bSkipped)
{
	UE_LOG(LogTemp, Display, TEXT("PGTitleIntro: dragon stayed at least %.1f m from the hull (pushed on %d frame(s))"),
		MinDragonGap < TNumericLimits<float>::Max() ? MinDragonGap * 0.01f : -1.0f, DragonPushFrames);
	// 둘 다 이 인트로만의 것이다. 끝나면 흔적 없이 치운다(타이틀 무대는 원래 모습 그대로).
	if (IsValid(Ship))
		Ship->Destroy();
	if (IsValid(Dragon))
		Dragon->Destroy();
	if (IsValid(WideGround))
		WideGround->Destroy();
	Ship = nullptr;
	Dragon = nullptr;
	WideGround = nullptr;
}
