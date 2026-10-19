// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#include "Audio/PGCharacterSoundComponent.h"

#include "Animations/AnimNotify_Fire.h"
#include "Animation/AnimMontage.h"
#include "Audio/PGAudioSubsystem.h"
#include "Audio/PGSoundSet.h"
#include "Characters/CustomCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Equipments/FirearmEquipment.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPGCharacterSound, Log, All);

namespace
{
	constexpr float MoveCheckInterval = 0.1f;
	constexpr int32 MaxBindTries = 10;
	// 샷건 한 발에 탄착음이 수십 개 겹치면 시끄럽기만 해서 이만큼만 틈.
	constexpr int32 MaxImpactSoundsPerShot = 3;
}

UPGCharacterSoundComponent::UPGCharacterSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // 소리는 각 클라가 알아서 냄 — 복제할 것 없음
}

void UPGCharacterSoundComponent::BeginPlay()
{
	Super::BeginPlay();
	OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter) return;

	TryBindAnimInstance();
	// 발소리는 반복 확인: 걸음은 끝나는 순간이 없는 계속 바뀌는 값이라 엔진이 걸음마다 알려 주지 않음. 멈춰 있으면 속도만 보고 바로 끝남.
	GetWorld()->GetTimerManager().SetTimer(MoveTimer, this, &UPGCharacterSoundComponent::CheckMovement, MoveCheckInterval, true,
		FMath::FRandRange(0.f, MoveCheckInterval)); // 캐릭터마다 시작을 흩어서 같은 프레임에 몰리지 않게
}

void UPGCharacterSoundComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearAllTimersForObject(this);
	if (OwnerCharacter && OwnerCharacter->GetMesh())
		if (UAnimInstance* Anim = OwnerCharacter->GetMesh()->GetAnimInstance())
			Anim->OnMontageStarted.RemoveDynamic(this, &UPGCharacterSoundComponent::HandleMontageStarted);
	Super::EndPlay(Reason);
}

void UPGCharacterSoundComponent::TryBindAnimInstance()
{
	UAnimInstance* Anim = OwnerCharacter && OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (Anim)
	{
		Anim->OnMontageStarted.AddUniqueDynamic(this, &UPGCharacterSoundComponent::HandleMontageStarted);
		BoundAnim = Anim;
		return;
	}
	if (++BindTries >= MaxBindTries)
	{
		UE_LOG(LogPGCharacterSound, Error, TEXT("%s: 몸 애니메이션(AnimInstance)이 5초 안에 안 생김 → 이 캐릭터는 총소리·장전·피격 소리가 안 남(발소리는 남)."),
			*GetNameSafe(OwnerCharacter));
		return;
	}
	GetWorld()->GetTimerManager().SetTimer(BindTimer, this, &UPGCharacterSoundComponent::TryBindAnimInstance, 0.5f, false);
}

float UPGCharacterSoundComponent::FindFireNotifyTime(UAnimMontage* Montage)
{
	if (const float* Cached = FireNotifyTimeCache.Find(Montage)) return *Cached;

	float Found = -1.f;
	// 1) 몽타주 자체에 찍힌 발사 표시
	for (const FAnimNotifyEvent& Event : Montage->Notifies)
		if (Event.Notify && Event.Notify->IsA<UAnimNotify_Fire>()) { Found = Event.GetTriggerTime(); break; }
	// 2) 없으면 몽타주 안에 들어 있는 애니메이션 조각에 찍힌 것 → 몽타주 시간으로 바꿈
	if (Found < 0.f)
	{
		for (const FSlotAnimationTrack& Slot : Montage->SlotAnimTracks)
		{
			for (const FAnimSegment& Segment : Slot.AnimTrack.AnimSegments)
			{
				const UAnimSequenceBase* Sequence = Segment.GetAnimReference();
				if (!Sequence) continue;
				for (const FAnimNotifyEvent& Event : Sequence->Notifies)
				{
					if (!Event.Notify || !Event.Notify->IsA<UAnimNotify_Fire>()) continue;
					const float Local = Event.GetTriggerTime();
					if (Local < Segment.AnimStartTime || Local > Segment.AnimEndTime) continue;
					Found = Segment.StartPos + (Local - Segment.AnimStartTime) / FMath::Max(Segment.AnimPlayRate, KINDA_SMALL_NUMBER);
					break;
				}
				if (Found >= 0.f) break;
			}
			if (Found >= 0.f) break;
		}
	}
	FireNotifyTimeCache.Add(Montage, Found);
	return Found;
}

void UPGCharacterSoundComponent::HandleMontageStarted(UAnimMontage* Montage)
{
	if (!Montage || !OwnerCharacter) return;
	UE_LOG(LogPGCharacterSound, Verbose, TEXT("%s: 몽타주 시작 %s"), *GetNameSafe(OwnerCharacter), *GetNameSafe(Montage));
	UAnimInstance* Anim = OwnerCharacter->GetMesh()->GetAnimInstance();
	const float Rate = FMath::Max(Montage->RateScale * (Anim ? Anim->Montage_GetPlayRate(Montage) : 1.f), KINDA_SMALL_NUMBER);
	FTimerManager& Timers = GetWorld()->GetTimerManager();

	// 발사 몽타주: 서버가 진짜 발사하는 그 시각(발사 표시)에 맞춰 총소리. 연사 중엔 몽타주가 매 발 다시 시작되므로 매 발 불림.
	const float FireTime = FindFireNotifyTime(Montage);
	if (FireTime >= 0.f)
	{
		FTimerHandle Handle;
		if (FireTime / Rate <= KINDA_SMALL_NUMBER) FireNow();
		else Timers.SetTimer(Handle, this, &UPGCharacterSoundComponent::FireNow, FireTime / Rate, false);
		return;
	}

	const UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	const UPGSoundSet* Set = Audio ? Audio->GetSoundSet() : nullptr;
	if (!Set) return;
	for (const FPGMontageSound& Entry : Set->MontageSounds)
	{
		if (Entry.Montage != Montage) continue;
		const FName Cue = Entry.Cue;
		if (Entry.Delay <= 0.f) { OnMontageSound(Montage, Cue); continue; }
		FTimerHandle Handle;
		Timers.SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Montage, Cue]() { OnMontageSound(Montage, Cue); }),
			Entry.Delay / Rate, false);
	}
}

void UPGCharacterSoundComponent::FireNow()
{
	ACustomCharacter* Shooter = Cast<ACustomCharacter>(OwnerCharacter);
	AFirearmEquipment* Firearm = Shooter ? Shooter->GetActiveFirearm() : nullptr;
	if (!Firearm)
	{
		// 총을 내려놓은 사이에 타이머가 끝났거나, 이 클라에 아직 총 정보가 안 온 경우
		UE_LOG(LogPGCharacterSound, Verbose, TEXT("%s: 발사 표시 시각인데 든 총이 없음 → 총소리 안 냄"), *GetNameSafe(OwnerCharacter));
		return;
	}
	FVector Muzzle;
	if (!Firearm->GetMuzzleLocation(Muzzle)) Muzzle = Firearm->GetActorLocation();
	OnGunshot(Muzzle, Firearm->GetWeaponType());
	TraceBulletImpacts(Shooter, Muzzle);
}

void UPGCharacterSoundComponent::TraceBulletImpacts(ACustomCharacter* Shooter, const FVector& Muzzle)
{
	// 진짜 탄착은 서버만 계산하고 클라에 알려 주지 않는다(팀원 코드).
	// 그래서 각 클라가 같은 방식으로 보이기용 선을 직접 쏴서 맞은 자리·재질을 얻는다.
	// - 내 캐릭터 → 내 카메라 방향이라 서버와 거의 같음
	// - 남의 캐릭터 → 복제된 조준 방향이라 조금 다를 수 있음(샷건 퍼짐은 원래 무작위)
	// 나중에 바꿀 것: 서버가 맞은 자리를 멀티캐스트로 알려 주면 이 선 대신 그걸 씀(FirearmEquipment 수정이라 팀원과 상의).
	const AFirearmEquipment* Firearm = Shooter->GetActiveFirearm();
	const FFirearmData& Data = Firearm->GetFirearmData();
	const float Range = Data.Range > 0.f ? Data.Range : 10000.f;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGBulletSound), true, Shooter);
	Params.bReturnPhysicalMaterial = true;
	// 메시 겉면(복잡한 충돌)까지 보고, 맞은 면 번호도 받음 → 한 메시에 머티리얼이 여럿일 때(컨테이너: 콘크리트 테두리 + 철판) 맞은 면의 머티리얼로 재질을 정함.
	Params.bTraceComplex = true;
	Params.bReturnFaceIndex = true;
	Params.AddIgnoredActor(Firearm);
	TArray<AActor*> Attached;
	Shooter->GetAttachedActors(Attached, true, true);
	Params.AddIgnoredActors(Attached);

	FVector ViewLocation;
	FRotator ViewRotation;
	if (Shooter->IsLocallyControlled()) Shooter->GetCombatViewPoint(ViewLocation, ViewRotation);
	else { ViewLocation = Shooter->GetPawnViewLocation(); ViewRotation = Shooter->GetBaseAimRotation(); }

	UWorld* World = GetWorld();
	UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	FHitResult CameraHit;
	const FVector CameraEnd = ViewLocation + ViewRotation.Vector() * Range;
	World->LineTraceSingleByChannel(CameraHit, ViewLocation, CameraEnd, ECC_Visibility, Params);
	const FVector Direction = ((CameraHit.bBlockingHit ? CameraHit.ImpactPoint : CameraEnd) - Muzzle).GetSafeNormal();
	if (Direction.IsNearlyZero()) return;

	const float HalfAngle = FMath::DegreesToRadians(FMath::Clamp(Data.ConeHalfAngle, 0.f, 45.f));
	const int32 Count = FMath::Clamp(Data.TraceCount, 1, MaxImpactSoundsPerShot);
	for (int32 Index = 0; Index < Count; ++Index)
	{
		FHitResult Hit;
		const FVector Ray = Index == 0 ? Direction : FMath::VRandCone(Direction, HalfAngle);
		if (World->LineTraceSingleByChannel(Hit, Muzzle, Muzzle + Ray * Range, ECC_Visibility, Params))
			OnBulletImpact(Hit.ImpactPoint, Audio ? Audio->SurfaceOf(Hit) : SurfaceType_Default);
	}
}

void UPGCharacterSoundComponent::CheckMovement()
{
	const UCharacterMovementComponent* Move = OwnerCharacter ? OwnerCharacter->GetCharacterMovement() : nullptr;
	if (!Move) return;

	// 무기를 바꿀 때 몸 애니메이션 묶음(AnimInstance)이 새로 만들어지면 "몽타주 시작" 연결이 끊김 → 바뀌었으면 다시 연결.
	// 따로 알려 주는 신호가 없어서 어차피 도는 이 확인에 얹음(비교 한 번이라 부담 없음).
	const UAnimInstance* CurrentAnim = OwnerCharacter->GetMesh() ? OwnerCharacter->GetMesh()->GetAnimInstance() : nullptr;
	if (CurrentAnim && CurrentAnim != BoundAnim.Get())
	{
		UE_LOG(LogPGCharacterSound, Verbose, TEXT("%s: 몸 애니메이션이 바뀜 → 몽타주 알림 다시 연결"), *GetNameSafe(OwnerCharacter));
		BindTries = 0;
		TryBindAnimInstance();
	}

	const bool bFalling = Move->IsFalling();
	if (bWasFalling && !bFalling && Move->IsMovingOnGround())
	{
		StepNow(1.f, true);
		DistanceSinceStep = 0.f;
	}
	bWasFalling = bFalling;
	if (!Move->IsMovingOnGround()) return;

	const float Speed = OwnerCharacter->GetVelocity().Size2D();
	if (Speed < 50.f) { DistanceSinceStep = 0.f; return; } // 거의 서 있음

	const UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	const UPGSoundSet* Set = Audio ? Audio->GetSoundSet() : nullptr;
	if (!Set) return;
	// 느리게 걸을수록 보폭이 짧아짐(뛸 때 보폭의 60%까지).
	const float SpeedRatio = FMath::Clamp(Speed / Set->RunSpeed, 0.f, 1.5f);
	const float Stride = Set->StrideAtRun * FMath::Lerp(0.6f, 1.f, FMath::Min(SpeedRatio, 1.f));
	DistanceSinceStep += Speed * MoveCheckInterval;
	if (DistanceSinceStep < Stride) return;
	DistanceSinceStep -= Stride;
	StepNow(FMath::Lerp(0.3f, 1.f, FMath::Min(SpeedRatio, 1.f)), false);
}

void UPGCharacterSoundComponent::StepNow(float VolumeScale, bool bLanding)
{
	const UCapsuleComponent* Capsule = OwnerCharacter->GetCapsuleComponent();
	const FVector Start = OwnerCharacter->GetActorLocation();
	const FVector End = Start - FVector(0, 0, Capsule->GetScaledCapsuleHalfHeight() + 60.f);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFootstep), false, OwnerCharacter);
	Params.bReturnPhysicalMaterial = true;
	FHitResult Hit;
	if (!GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)) return;
	UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	OnFootstep(Hit.ImpactPoint, Audio ? Audio->SurfaceOf(Hit) : SurfaceType_Default, VolumeScale, bLanding);
}

void UPGCharacterSoundComponent::OnGunshot_Implementation(FVector MuzzleLocation, EWeaponType WeaponType)
{
	UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	const UPGSoundSet* Set = Audio ? Audio->GetSoundSet() : nullptr;
	if (!Set) return;
	if (const FPGSoundCue* Cue = Set->Gunshots.Find(WeaponType)) Audio->PlayCue(*Cue, MuzzleLocation);
}

void UPGCharacterSoundComponent::OnBulletImpact_Implementation(FVector ImpactLocation, EPhysicalSurface Surface)
{
	UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	const UPGSoundSet* Set = Audio ? Audio->GetSoundSet() : nullptr;
	if (!Set) return;
	if (const FPGSoundCue* Cue = UPGSoundSet::FindBySurface(Set->BulletImpacts, Surface)) Audio->PlayCue(*Cue, ImpactLocation);
}

void UPGCharacterSoundComponent::OnFootstep_Implementation(FVector FootLocation, EPhysicalSurface Surface, float VolumeScale, bool bLanding)
{
	UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this);
	const UPGSoundSet* Set = Audio ? Audio->GetSoundSet() : nullptr;
	if (!Set) return;
	if (const FPGSoundCue* Cue = UPGSoundSet::FindBySurface(Set->Footsteps, Surface)) Audio->PlayCue(*Cue, FootLocation, VolumeScale);
	if (bLanding) Audio->PlayCue(Set->Landing, FootLocation, VolumeScale);
}

void UPGCharacterSoundComponent::OnMontageSound_Implementation(UAnimMontage* Montage, FName CueName)
{
	if (UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(this))
		Audio->PlayCueByName(CueName, OwnerCharacter ? OwnerCharacter->GetActorLocation() : FVector::ZeroVector);
}
