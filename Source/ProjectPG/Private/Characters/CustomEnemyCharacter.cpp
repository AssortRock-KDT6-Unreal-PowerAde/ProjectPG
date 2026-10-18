#include "Characters/CustomEnemyCharacter.h"
#include "AI/CustomEnemyAIController.h"
#include "AI/EnemyVoiceComponent.h"
#include "AbilitySystemComponent.h"
#include "Core/TableSubSystem.h"
#include "Core/ItemSubSystem.h"
#include "Equipments/FirearmEquipment.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
#include "Perception/AISense_Damage.h"
#include "Engine/World.h"

ACustomEnemyCharacter::ACustomEnemyCharacter(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	CharacterID = TEXT("Enemy_Rifle");
	AIControllerClass = ACustomEnemyAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AbilitySystemComp = CreateDefaultSubobject<UAbilitySystemComponent>(TEXT("AbilitySystem"));
	AbilitySystemComp->SetIsReplicated(true);
	AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Minimal);
	GetCharacterMovement()->bOrientRotationToMovement = true;
	Voice = CreateDefaultSubobject<UEnemyVoiceComponent>(TEXT("Voice"));
}

void ACustomEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComp->InitAbilityActorInfo(this, this);
	auto* Tables = UTableSubSystem::Get(this);
	const auto* Row = Tables ? Tables->FindTableRow<FEnemyTableRow>(TEXT("EnemyTable"), CharacterID) : nullptr;
	if (!Row)
	{
		// 행이 없으면 체력·무기도 못 정하니 AI 도 켜지 않는다(AI 컨트롤러가 HasEnemyData 를 보고 Error 로그).
		UE_LOG(LogTemp, Error, TEXT("[Enemy] Missing EnemyTable row %s (must match CharacterTable)"), *CharacterID.ToString());
		return;
	}
	EnemyData = *Row;
	bHasEnemyData = true;
	GetCharacterMovement()->MaxWalkSpeed = FMath::Max(1.f, EnemyData.MoveSpeed);
	GetCharacterMovement()->MaxWalkSpeedCrouched = EnemyData.MoveSpeed * 0.5f;
	if (!HasAuthority()) return;
	CharacterAttributeSet->SetHealth(FMath::Max(1.f, EnemyData.Health));
	CharacterAttributeSet->SetMaxHealth(FMath::Max(1.f, EnemyData.Health));
	CharacterAttributeSet->SetMaxStamina(100.f);
	CharacterAttributeSet->SetStamina(100.f);
	auto* Items = UItemSubSystem::Get(this);
	const auto* Equipment = Items ? Items->GetEquip(EnemyData.WeaponItemID) : nullptr;
	if (!Equipment || !Equipment->EquipActorClass || !Equipment->EquipActorClass->IsChildOf(AFirearmEquipment::StaticClass()))
	{
		UE_LOG(LogTemp, Error, TEXT("[Enemy] Weapon %s must use FirearmEquipment"), *EnemyData.WeaponItemID.ToString());
		return;
	}
	FActorSpawnParameters Params;
	Params.Owner = this;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	EnemyWeapon = GetWorld()->SpawnActor<AFirearmEquipment>(Equipment->EquipActorClass, GetActorTransform(), Params);
	if (EnemyWeapon)
	{
		EnemyWeapon->InitializeEquipment(EnemyData.WeaponItemID);
		EnemyWeapon->Equip(this);
	}
	// 표·무기 준비가 끝났으니 AI 를 켠다(빙의가 BeginPlay 보다 먼저라 그때는 못 켬).
	if (auto* AI = Cast<ACustomEnemyAIController>(GetController())) AI->StartAI();
}

void ACustomEnemyCharacter::SetAimTracking(bool bEnabled)
{
	bAimTracking = bEnabled;
	if (!bEnabled) ReplicatedAimOffset = FVector2D::ZeroVector;
}

void ACustomEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 목표를 보고 있을 때만 위아래·좌우 조준 각도를 클라이언트에 보낸다.
	// 매 프레임인 이유: AI 가 목표를 따라 고개를 돌리는 동안 각도가 계속 바뀌어 애니메이션이 부드러워야 함.
	// 목표가 없을 땐 0 으로 두고 계산·복제를 안 한다.
	if (!bAimTracking || !HasAuthority() || IsDead()) return;
	const FRotator RelativeAim = (GetControlRotation() - GetActorRotation()).GetNormalized();
	ReplicatedAimOffset = FVector2D(RelativeAim.Yaw, RelativeAim.Pitch);
}

void ACustomEnemyCharacter::Hit_Implementation(float Damage, AActor* DamageCauser, const FHitResult& HitResult)
{
	const float HealthBefore = CharacterAttributeSet ? CharacterAttributeSet->GetHealth() : 0.f;
	Super::Hit_Implementation(Damage, DamageCauser, HitResult);
	if (!HasAuthority()) return;
	// 맞은 소리: 체력이 실제로 깎였을 때만(이미 죽은 몸·0 피해는 조용히).
	// - 이번 총알로 죽음 → 죽는 소리 / 살아 있음 → "악!"(짧은 쉬는 시간 안에 또 맞으면 생략)
	if (CharacterAttributeSet && CharacterAttributeSet->GetHealth() < HealthBefore)
		Voice->TryPlayLine(IsDead() ? EEnemyVoiceLine::Death : EEnemyVoiceLine::Hurt);
	if (!DamageCauser) return;
	// 누가 쐈는지 AI 의 "피격 감각"에 알린다 → 안 보이는 데서 맞아도 쏜 사람을 목표로 삼는다.
	// DamageCauser 가 총이면 그 총의 주인(사람)을 찾는다.
	AActor* Shooter = DamageCauser->IsA<APawn>() ? DamageCauser : DamageCauser->GetInstigator();
	if (!Shooter) Shooter = DamageCauser->GetOwner();
	if (Shooter && Shooter != this)
		UAISense_Damage::ReportDamageEvent(this, this, Shooter, Damage, Shooter->GetActorLocation(), HitResult.ImpactPoint);
}

void ACustomEnemyCharacter::GetCombatViewPoint(FVector& Location, FRotator& Rotation) const
{
	Super::GetCombatViewPoint(Location, Rotation);
	const float ErrorAngle = FMath::DegreesToRadians(10.f * (1.f - FMath::Clamp(EnemyData.Accuracy, 0.f, 1.f)));
	Rotation = FMath::VRandCone(Rotation.Vector(), ErrorAngle).Rotation();
}

void ACustomEnemyCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACustomEnemyCharacter, ReplicatedAimOffset);
}

void ACustomEnemyCharacter::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority() && IsValid(EnemyWeapon)) EnemyWeapon->Destroy();
	Super::EndPlay(Reason);
}
