#include "Objects/PGWigBeamComponent.h"
#include "Common/PGSoundRouter.h"

#include "Common/PGPhysicsUtil.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGObjectTypes.h"
#include "Flow/PGRunSubsystem.h"
#include "Objects/PGWearableColors.h"
#include "TimerManager.h"
#include "Particles/ParticleSystem.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* const BeamMeshPath = TEXT("/Game/PG/Characters/Quantum/Wig/SM_WigBeam.SM_WigBeam");
}

UPGWigBeamComponent::UPGWigBeamComponent()
{
	// 빔 모양 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 것).
	BeamMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(BeamMeshPath));
	// 터지는 효과 기본값(9/23): 잔해·드래곤이 쓰는 흙먼지 / 보스 팩의 "캐릭터가 맞았을 때" / 연료통 폭발과 같은 불덩이.
	ImpactEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld")));
	BodyHitEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitCharacter.P_Rampage_Rock_HitCharacter")));
	BlastBallMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/SM_MissileBlast.SM_MissileBlast")));
	BlastBallMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/MI_MissileBlast.MI_MissileBlast")));
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // 광선이 보이는 동안만 돈다
	SetIsReplicatedByDefault(true);
}

void UPGWigBeamComponent::BeginPlay()
{
	Super::BeginPlay();
	// 가발을 얻었는지 가볍게 확인해서 머리에 씌운다(인벤토리 변경 신호가 팀 쪽 구조마다 달라서 0.5초 확인으로 둔다).
	GetWorld()->GetTimerManager().SetTimer(WornCheckTimer, this, &UPGWigBeamComponent::RefreshWornVisual, 0.5f, true, 0.5f);
}

USkeletalMeshComponent* UPGWigBeamComponent::GetBodyMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return Character ? Character->GetMesh() : nullptr;
}

void UPGWigBeamComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGWigBeamComponent, bWigWornReplicated);
}

bool UPGWigBeamComponent::IsWigWorn() const
{
	// 서버(혼자 하는 판 포함)는 가방을 직접, 클라이언트는 서버가 알려 준 값.
	if (GetOwner() && GetOwner()->HasAuthority())
		return UPGItemReceiverLibrary::HasItem(GetOwner(), WigItemId, 1);
	return bWigWornReplicated;
}

void UPGWigBeamComponent::RefreshWornVisual()
{
	const bool bWorn = IsWigWorn();
	if (GetOwner() && GetOwner()->HasAuthority())
		bWigWornReplicated = bWorn;
	if (!bWorn)
	{
		if (IsValid(WornWig))
			WornWig->SetVisibility(false);
		return;
	}
	if (!IsValid(WornWig))
	{
		// 착장 표(PGWearableColors)의 가발 행: 메시·재질·붙일 뼈·변환이 모자와 같다.
		FPGWearableColor Wear;
		USkeletalMeshComponent* Body = GetBodyMesh();
		if (!Body || !UPGWearableColorLibrary::FindWearableColor(WigItemId, Wear))
			return;
		UStaticMesh* Mesh = Wear.WornStaticMesh.LoadSynchronous();
		if (!Mesh)
			return;
		WornWig = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("WornWig"));
		WornWig->SetStaticMesh(Mesh);
		if (UMaterialInterface* Material = Wear.Material.LoadSynchronous())
			WornWig->SetMaterial(0, Material);
		WornWig->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WornWig->SetupAttachment(Body, Wear.AttachBone);
		WornWig->SetRelativeTransform(Wear.AttachOffset);
		WornWig->RegisterComponent();
	}
	WornWig->SetVisibility(true);
}

FVector UPGWigBeamComponent::GetBeamOrigin() const
{
	if (IsValid(WornWig) && WornWig->DoesSocketExist(TEXT("Forehead")))
		return WornWig->GetSocketLocation(TEXT("Forehead"));
	if (const USkeletalMeshComponent* Body = GetBodyMesh(); Body && Body->DoesSocketExist(TEXT("head")))
		return Body->GetSocketLocation(TEXT("head")) + GetOwner()->GetActorForwardVector() * 15.0f;
	return GetOwner()->GetActorLocation() + FVector(0.0f, 0.0f, 70.0f);
}

float UPGWigBeamComponent::GetReadyFraction() const
{
	const UWorld* World = GetWorld();
	if (!World || CooldownSeconds <= 0.0f)
		return 1.0f;
	return FMath::Clamp((World->GetTimeSeconds() - LastFireTime) / CooldownSeconds, 0.0f, 1.0f);
}

void UPGWigBeamComponent::RequestFireWithKey(FKey PressedKey)
{
	if (PressedKey == FireKey)
		RequestFire();
}

void UPGWigBeamComponent::RequestFire()
{
	if (!IsWigWorn())
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireTime < CooldownSeconds)
		return;
	// 조준 = 카메라(컨트롤러 시점)가 보는 방향. 출발점은 이마.
	FVector ViewLocation;
	FRotator ViewRotation;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (Pawn && Pawn->GetController())
		Pawn->GetController()->GetPlayerViewPoint(ViewLocation, ViewRotation);
	else
		ViewRotation = GetOwner()->GetActorRotation();
	LastFireTime = Now; // 클라이언트도 바로 막아 RPC 를 연달아 보내지 않게
	if (GetOwner()->HasAuthority())
		FireAuthoritative(ViewLocation, ViewRotation.Vector());
	else
		ServerFire(ViewLocation, ViewRotation.Vector());
}

void UPGWigBeamComponent::ServerFire_Implementation(FVector_NetQuantize AimStart, FVector_NetQuantizeNormal AimDirection)
{
	// 서버에서 다시 확인한다(가발·재사용 시간). 클라이언트 말만 믿지 않는다.
	if (!IsWigWorn())
		return;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now - LastFireTime < CooldownSeconds - 0.2f)
		return;
	LastFireTime = Now;
	FireAuthoritative(AimStart, AimDirection);
}

void UPGWigBeamComponent::FireAuthoritative(const FVector& AimStart, const FVector& AimDirection)
{
	UWorld* World = GetWorld();
	AActor* Owner = GetOwner();
	// 카메라가 보는 점을 먼저 찾고(화면 가운데에 맞게), 이마에서 그 점으로 쏜다.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGWigBeam), false, Owner);
	FHitResult AimHit;
	const FVector Origin = GetBeamOrigin();
	// [9/23 수정] 조준 선은 카메라가 아니라 "카메라 선 위에서 이마와 가장 가까운 점" 부터 긋는다.
	//   3인칭 카메라는 캐릭터 뒤에 있어서, 카메라와 캐릭터 사이의 덤불·벽이 먼저 맞으면 조준점이 머리 뒤에 잡혀
	//   광선이 머리 뒤로 뚫고 나갔다(사용자 스크린샷). 캐릭터보다 뒤에 있는 것은 조준에서 뺀다(3인칭 게임의 흔한 방식).
	const FVector AimDir = AimDirection.GetSafeNormal();
	const float SkipCm = FMath::Max(static_cast<float>(FVector::DotProduct(Origin - AimStart, AimDir)), 0.0f);
	const FVector AimFrom = AimStart + AimDir * SkipCm;
	const FVector AimEnd = AimStart + AimDir * Range;
	FVector Target = World->LineTraceSingleByChannel(AimHit, AimFrom, AimEnd, ECC_Visibility, Params) ? AimHit.ImpactPoint : AimEnd;
	// 그래도 조준점이 이마보다 뒤(또는 거의 붙어 있음)면 카메라가 보는 방향 그대로 멀리 쏜다.
	if (FVector::DotProduct(Target - Origin, AimDir) < 100.0f)
		Target = Origin + AimDir * Range;
	FHitResult Hit;
	const FVector Direction = (Target - Origin).GetSafeNormal();
	// 광선은 몸을 뚫고 지나간다(9/23 사용자: "몹들 한방에"). 멈추는 곳은 벽·땅·건물(움직이지 않는 것)뿐이다.
	//   그래서 끝점은 사람·몬스터를 빼고 잰다. 몬스터는 아래에서 그 선을 따라 한꺼번에 모은다.
	FCollisionObjectQueryParams WallObjects;
	WallObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	WallObjects.AddObjectTypesToQuery(ECC_WorldDynamic);
	const bool bHit = World->LineTraceSingleByObjectType(Hit, Origin, Origin + Direction * Range, WallObjects, Params);
	const FVector Impact = bHit ? Hit.ImpactPoint : Origin + Direction * Range;
	// 판 기록(사격 수). 플레이어 캐릭터가 쏜 것만 센다 — 판단은 서브시스템이 한다.
	UPGRunSubsystem::NotifyShotFired(Owner);

	// 선을 따라 굵기 PierceRadiusCm 로 훑어 맞은 것 전부에 피해. 플레이어(자기 편)는 빼고, 한 액터는 한 번만.
	const APawn* ShooterPawn = Cast<APawn>(Owner);
	TArray<FHitResult> BodyHits;
	FCollisionObjectQueryParams BodyObjects;
	BodyObjects.AddObjectTypesToQuery(ECC_Pawn);
	BodyObjects.AddObjectTypesToQuery(ECC_PhysicsBody);
	World->SweepMultiByObjectType(BodyHits, Origin, Impact, FQuat::Identity, BodyObjects, FCollisionShape::MakeSphere(PierceRadiusCm), Params);
	TSet<AActor*> Damaged;
	TArray<FVector_NetQuantize> HitPoints; // 뚫은 몸마다 터지는 자리(화면마다 효과를 낸다)
	for (const FHitResult& BodyHit : BodyHits)
	{
		AActor* Victim = BodyHit.GetActor();
		if (!IsValid(Victim) || Victim == Owner || Damaged.Contains(Victim))
			continue;
		if (const APawn* VictimPawn = Cast<APawn>(Victim); VictimPawn && VictimPawn->IsPlayerControlled())
			continue;
		Damaged.Add(Victim);
		if (HitPoints.Num() < 12) // 한 줄에 몬스터가 수십이어도 효과는 12개까지(화면·비용)
			HitPoints.Add(Victim->GetActorLocation());
		UGameplayStatics::ApplyPointDamage(Victim, Damage, Direction, BodyHit, ShooterPawn ? ShooterPawn->GetController() : nullptr, Owner, UDamageType::StaticClass());
	}
	if (bHit)
	{
		// 맞은 자리 둘레의 소품·건물 조각을 날린다(잔해 시스템 그대로 — 미리 만든 묶음이라 끊김이 없다).
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		World->OverlapMultiByObjectType(Overlaps, Impact, FQuat::Identity, Objects, FCollisionShape::MakeSphere(BlastRadius), Params);
		int32 Knocked = 0;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UPrimitiveComponent* Component = Overlap.GetComponent();
			if (!IsValid(Component) || Knocked >= 8)
				continue;
			FHitResult Fake;
			Fake.ImpactPoint = Impact;
			Fake.Item = Overlap.ItemIndex;
			const FVector Push = (Component->GetComponentLocation() - Impact).GetSafeNormal() * 1200.0f + FVector(0.0f, 0.0f, 400.0f);
			if (PGPhysicsUtil::TryKnockProp(Component, Fake, Push, Owner, 600.0f, 100.0f))
				++Knocked;
		}
	}
	UE_LOG(LogPGObjects, Display, TEXT("%s wig beam: pierced %d body(s), stopped at %s (%s)"), *GetNameSafe(Owner), Damaged.Num(),
		bHit ? *GetNameSafe(Hit.GetActor()) : TEXT("nothing"), *Impact.ToCompactString());
	MulticastBeam(Origin, Impact, HitPoints);
}

void UPGWigBeamComponent::SpawnBlastBall(const FVector& Where, float RadiusCm) const
{
	UWorld* World = GetWorld();
	UStaticMesh* BallMesh = BlastBallMesh.IsNull() ? nullptr : BlastBallMesh.LoadSynchronous();
	if (!IsValid(World) || !BallMesh || RadiusCm <= 0.0f)
		return;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Flash = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Where), Params);
	if (!Flash)
		return;
	UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(Flash, TEXT("WigBlast"));
	Ball->SetMobility(EComponentMobility::Movable);
	Ball->SetStaticMesh(BallMesh);
	if (UMaterialInterface* Material = BlastBallMaterial.IsNull() ? nullptr : BlastBallMaterial.LoadSynchronous())
		Ball->SetMaterial(0, Material);
	Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Ball->SetCastShadow(false);
	Flash->SetRootComponent(Ball);
	Ball->RegisterComponent();
	Ball->SetWorldLocation(Where);
	// 0.4초 동안 확 커지며 사라진다(연료통 폭발 APGFloorItemActor::Explode 와 같은 방식, 조금 더 빠르게).
	const double Start = World->GetTimeSeconds();
	const float MaxScale = RadiusCm / 100.0f; // 불덩이 메시는 반지름 1m 구
	TWeakObjectPtr<UStaticMeshComponent> WeakBall(Ball);
	TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Flash, [WeakBall, Start, MaxScale, Handle]()
	{
		UStaticMeshComponent* B = WeakBall.Get();
		if (!IsValid(B))
			return;
		const float T = FMath::Clamp(static_cast<float>(B->GetWorld()->GetTimeSeconds() - Start) / 0.4f, 0.0f, 1.0f);
		B->SetWorldScale3D(FVector(FMath::Lerp(0.2f, MaxScale, FMath::Sqrt(T))));
		if (T >= 1.0f)
		{
			B->GetWorld()->GetTimerManager().ClearTimer(*Handle);
			B->GetOwner()->Destroy();
		}
	}), 1.0f / 30.0f, true);
	Flash->SetLifeSpan(2.0f); // 타이머가 어떤 이유로 못 치워도 남지 않게
}

void UPGWigBeamComponent::MulticastBeam_Implementation(FVector_NetQuantize From, FVector_NetQuantize To, const TArray<FVector_NetQuantize>& HitPoints)
{
	// 소리 신호. Multicast 라 이미 모든 기계에서 돈다 — PlayLocal.
	PGSound::PlayLocal(this, FName(TEXT("WigBeam_Fire")), GetOwner(), FVector(From));
	PGSound::PlayLocal(this, FName(TEXT("WigBeam_Impact")), nullptr, FVector(To));
	// 터지는 효과: 멈춘 자리 한 번 + 뚫은 몸마다(9/23). 광선보다 먼저 — 광선 메시가 없어도 폭발은 보이게.
	if (UWorld* World = GetWorld())
	{
		if (UParticleSystem* Dust = ImpactEffect.IsNull() ? nullptr : ImpactEffect.LoadSynchronous())
			UGameplayStatics::SpawnEmitterAtLocation(World, Dust, FVector(To), FRotator::ZeroRotator, FVector(3.0f), true, EPSCPoolMethod::AutoRelease);
		SpawnBlastBall(FVector(To), ImpactBlastRadiusCm);
		UParticleSystem* Burst = BodyHitEffect.IsNull() ? nullptr : BodyHitEffect.LoadSynchronous();
		for (const FVector_NetQuantize& Point : HitPoints)
		{
			if (Burst)
				UGameplayStatics::SpawnEmitterAtLocation(World, Burst, FVector(Point), FRotator::ZeroRotator, FVector(2.5f), true, EPSCPoolMethod::AutoRelease);
			SpawnBlastBall(FVector(Point), BodyBlastRadiusCm);
		}
	}

	if (!IsValid(BeamMesh))
	{
		UStaticMesh* Mesh = BeamMeshAsset.IsNull() ? nullptr : BeamMeshAsset.LoadSynchronous();
		if (!Mesh)
			return;
		// [9/23 크래시 수정] 이름을 "WigBeam" 으로 두면 캐릭터의 가발 부품(CreateDefaultSubobject "WigBeam")과 이름이 같아
		//   "Cannot replace existing object of a different class" 로 게임이 죽었다(PIE 에서 광선을 처음 쏠 때). 겹치지 않는 이름으로.
		BeamMesh = NewObject<UStaticMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), UStaticMeshComponent::StaticClass(), TEXT("WigBeamVisual")));
		BeamMesh->SetStaticMesh(Mesh);
		BeamMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BeamMesh->SetCastShadow(false);
		BeamMesh->SetUsingAbsoluteLocation(true);
		BeamMesh->SetUsingAbsoluteRotation(true);
		BeamMesh->SetUsingAbsoluteScale(true);
		BeamMesh->SetupAttachment(GetOwner()->GetRootComponent());
		BeamMesh->RegisterComponent();
	}
	const FVector Delta = FVector(To) - FVector(From);
	BeamMesh->SetWorldLocationAndRotation(From, Delta.Rotation());
	BeamMesh->SetWorldScale3D(FVector(Delta.Size() / 100.0f, 1.0f, 1.0f));
	BeamMesh->SetVisibility(true);
	BeamHideTime = GetWorld()->GetTimeSeconds() + BeamVisibleSeconds;
	SetComponentTickEnabled(true);
}

void UPGWigBeamComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!IsValid(BeamMesh))
	{
		SetComponentTickEnabled(false);
		return;
	}
	// 광선이 보이는 동안 X 축으로 돌려 나선이 감기는 느낌(모델링 README: 컴포넌트를 X 축으로 굴리기).
	BeamMesh->AddLocalRotation(FRotator(0.0f, 0.0f, 720.0f * DeltaTime));
	if (GetWorld()->GetTimeSeconds() >= BeamHideTime)
	{
		BeamMesh->SetVisibility(false);
		SetComponentTickEnabled(false);
	}
}
