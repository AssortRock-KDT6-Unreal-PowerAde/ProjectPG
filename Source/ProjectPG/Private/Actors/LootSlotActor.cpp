#include "Actors/LootSlotActor.h"

#include "Actors/LootSlotComponent.h"

ALootSlotActor::ALootSlotActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetHidden(true);
	Slot = CreateDefaultSubobject<ULootSlotComponent>(TEXT("Slot"));
	RootComponent = Slot;
}
