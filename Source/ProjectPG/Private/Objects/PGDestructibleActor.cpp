#include "Objects/PGDestructibleActor.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"

APGDestructibleActor::APGDestructibleActor()
{
	SetCanBeDamaged(true);
	DisplayName = NSLOCTEXT("Destructible", "DefaultName", "장애물");
}

void APGDestructibleActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGDestructibleActor, Health);
	DOREPLIFETIME(APGDestructibleActor, bDestroyed);
	DOREPLIFETIME(APGDestructibleActor, Condition);
	DOREPLIFETIME(APGDestructibleActor, RequiredToolItemId);
	DOREPLIFETIME(APGDestructibleActor, DestroyedMesh);
}

void APGDestructibleActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	RequiredToolItemId = Row.ItemId;
	if (!Row.ItemId.IsNone())
		Condition = EPGDestroyCondition::ToolOnly;
	DestroyedMesh = Row.SecondaryMesh;
	Health = MaxHealth;
}

void APGDestructibleActor::Configure(EPGDestroyCondition InCondition, float InMaxHealth, FName InToolItemId)
{
	Condition = InCondition;
	MaxHealth = FMath::Max(1.0f, InMaxHealth);
	Health = MaxHealth;
	RequiredToolItemId = InToolItemId;
}

float APGDestructibleActor::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || bDestroyed || DamageAmount <= 0.0f)
		return 0.0f;

	switch (Condition)
	{
	case EPGDestroyCondition::ToolOnly:
		// 총으로 쏴도 안 부서진다. 도구를 든 플레이어가 F로 제거해야 한다.
		return 0.0f;
	case EPGDestroyCondition::ExplosiveOnly:
	{
		const bool bRadial = DamageEvent.IsOfType(FRadialDamageEvent::ClassID);
		const bool bTypeMatch = RequiredDamageType && DamageEvent.DamageTypeClass && DamageEvent.DamageTypeClass->IsChildOf(RequiredDamageType);
		if (!bRadial && !bTypeMatch)
			return 0.0f;
		break;
	}
	default:
		break;
	}

	Health = FMath::Max(0.0f, Health - DamageAmount);
	if (Health <= 0.0f)
		Break(DamageCauser);
	return Applied;
}

bool APGDestructibleActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (bDestroyed || Condition != EPGDestroyCondition::ToolOnly)
		return false;
	if (!UPGItemReceiverLibrary::HasItem(Interactor, RequiredToolItemId, 1))
	{
		OutReason = FText::Format(NSLOCTEXT("Destructible", "NeedTool", "{0} 필요"), FText::FromName(RequiredToolItemId));
		return false;
	}
	return true;
}

void APGDestructibleActor::HandleInteract(APawn* Interactor)
{
	// 도구는 소모하지 않는다. 절단기로 철조망을 자르면 절단기는 남는다.
	Break(Interactor);
}

void APGDestructibleActor::ForceDestroy(AActor* Causer)
{
	if (HasAuthority() && !bDestroyed)
		Break(Causer);
}

void APGDestructibleActor::Break(AActor* Causer)
{
	bDestroyed = true;
	Health = 0.0f;
	OnRep_Destroyed();
	UE_LOG(LogPGObjects, Display, TEXT("Destructible %s (%s) destroyed by %s"), *GetName(), *ObjectId.ToString(), *GetNameSafe(Causer));

	if (ExplosionRadius > 0.0f && ExplosionDamage > 0.0f)
	{
		// 자기 자신은 제외한다. 연료통 옆의 다른 연료통은 연쇄 폭발한다(그쪽도 ExplosiveOnly면).
		UGameplayStatics::ApplyRadialDamage(this, ExplosionDamage, GetActorLocation(), ExplosionRadius,
			UDamageType::StaticClass(), TArray<AActor*>{ this }, this, nullptr, true);
	}

	OnDestroyedByDamage.Broadcast(this, Causer);
}

void APGDestructibleActor::OnRep_Destroyed()
{
	if (!bDestroyed)
		return;
	// 통과 가능해지는 것이 핵심이다. 연출(파편 VFX)은 VFX 담당이 이 이벤트에 붙인다.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (!DestroyedMesh.IsNull())
		ApplySoftMesh(MeshComponent, DestroyedMesh);
	else
		MeshComponent->SetVisibility(false, true);
}

FText APGDestructibleActor::GetPromptInternal() const
{
	if (Condition == EPGDestroyCondition::ToolOnly && !bDestroyed)
		return FText::Format(NSLOCTEXT("Destructible", "Remove", "{0} 제거"), DisplayName);
	return FText::GetEmpty();
}
