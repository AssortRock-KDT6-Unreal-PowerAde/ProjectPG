#include "Characters/CustomEnemyCharacter.h"
#include "AI/CustomEnemyAIController.h"
#include "AbilitySystemComponent.h"
#include "Core/TableSubSystem.h"
#include "Core/ItemSubSystem.h"
#include "Equipments/FirearmEquipment.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Net/UnrealNetwork.h"
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
}

void ACustomEnemyCharacter::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComp->InitAbilityActorInfo(this, this);
	auto* Tables = UTableSubSystem::Get(this);
	const auto* Row = Tables ? Tables->FindTableRow<FEnemyTableRow>(TEXT("EnemyTable"), CharacterID) : nullptr;
	if (!Row)
	{
		UE_LOG(LogTemp, Error, TEXT("[Enemy] Missing EnemyTable row %s (must match CharacterTable)"), *CharacterID.ToString());
		return;
	}
	EnemyData = *Row;
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
}

void ACustomEnemyCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority() || IsDead()) return;
	const FRotator RelativeAim = (GetControlRotation() - GetActorRotation()).GetNormalized();
	ReplicatedAimOffset = FVector2D(RelativeAim.Yaw, RelativeAim.Pitch);
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
