#include "Objects/PGLootableComponent.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGObjectSpawnerSubsystem.h"

UPGLootableComponent::UPGLootableComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	DisplayName = NSLOCTEXT("Lootable", "DefaultName", "시체");
}

void UPGLootableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGLootableComponent, bActivated);
	DOREPLIFETIME(UPGLootableComponent, Contents);
	DOREPLIFETIME(UPGLootableComponent, CurrentUser);
}

void UPGLootableComponent::ActivateLoot(int64 Seed)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || bActivated)
		return;

	bActivated = true;
	// 내용물을 미리 지정했으면(퀘스트 시체 등) 굴리지 않는다.
	if (Contents.Num() == 0 && !LootTableId.IsNone())
	{
		if (UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(this))
			Contents = Spawner->RollLoot(LootTableId, Seed != 0 ? Seed : FDateTime::UtcNow().GetTicks());
	}
}

bool UPGLootableComponent::CanLoot(const APawn* Pawn) const
{
	if (!bActivated || !IsValid(Pawn))
		return false;
	if (IsValid(CurrentUser) && CurrentUser != Pawn)
		return false;
	return Contents.Num() > 0;
}

bool UPGLootableComponent::TryBeginUse(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !IsValid(Pawn))
		return false;
	if (IsValid(CurrentUser) && CurrentUser != Pawn)
		return false;
	CurrentUser = Pawn;
	return true;
}

void UPGLootableComponent::EndUse(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || CurrentUser != Pawn)
		return;
	CurrentUser = nullptr;
}

bool UPGLootableComponent::Loot(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !CanLoot(Pawn))
		return false;

	bool bAny = false;
	for (int32 Index = Contents.Num() - 1; Index >= 0; --Index)
	{
		const FPGItemStack& Stack = Contents[Index];
		if (UPGItemReceiverLibrary::GiveItem(Pawn, Stack.ItemId, Stack.Count))
		{
			Contents.RemoveAt(Index);
			bAny = true;
		}
	}

	if (bAny)
		OnLooted.Broadcast(this, Pawn);
	if (Contents.Num() == 0)
		OnEmptied.Broadcast(this, Pawn);
	return bAny;
}

bool UPGLootableComponent::TakeItem(APawn* Pawn, int32 Index)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !CanLoot(Pawn) || !Contents.IsValidIndex(Index))
		return false;
	const FPGItemStack Stack = Contents[Index];
	if (!UPGItemReceiverLibrary::GiveItem(Pawn, Stack.ItemId, Stack.Count))
		return false;
	Contents.RemoveAt(Index);
	OnLooted.Broadcast(this, Pawn);
	if (Contents.Num() == 0)
		OnEmptied.Broadcast(this, Pawn);
	return true;
}

FText UPGLootableComponent::GetPrompt() const
{
	if (!bActivated)
		return FText::GetEmpty();
	if (IsValid(CurrentUser))
		return NSLOCTEXT("Lootable", "InUse", "사용 중");
	if (Contents.Num() == 0)
		return FText::Format(NSLOCTEXT("Lootable", "Empty", "{0} (비어 있음)"), DisplayName);
	return FText::Format(NSLOCTEXT("Lootable", "Search", "{0} 뒤지기"), DisplayName);
}
