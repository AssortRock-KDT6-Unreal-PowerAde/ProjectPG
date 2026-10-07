#include "Actor/InteractActor.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "GameFramework/PlayerController.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/InventoryWindow.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ActorChannel.h"
#include "Net/UnrealNetwork.h"
AInteractActor::AInteractActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	MeshComp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComp"));
	RootComponent = MeshComp;

	InventoryComp = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComp"));

}

void AInteractActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	if (HasAuthority() && GetWorld()->IsGameWorld())
	{
		MyActorGuid = FGuid::NewGuid();
		InventoryComp->RegisterContainer(MyActorGuid, FIntPoint(10, 10));
	}
}

void AInteractActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AInteractActor, MyActorGuid);
}

void AInteractActor::Interact_Implementation(AActor* InteractingController)
{
	if (!InteractingController) return;
	if (!InventoryComp || !MyActorGuid.IsValid()) return;
	if (InventoryComp->IsServerManaged() && !InventoryComp->HasInitialInventory()) return;

	if (UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(InteractingController))
	{
		UUserWidget* CharacterWidget = UISub->OpenUI(EUIType::Character);
		if (UInventoryWindow* Win = Cast<UInventoryWindow>(CharacterWidget))
		{
			if (InventoryComp)
			{
				const auto& ItemsMap = InventoryComp->GetItemsMap();
				UE_LOG(LogTemp, Warning, TEXT("InteractActor::Interact_Implementation - InventoryComp valid, ContainerCount=%d"), ItemsMap.Num());
				for (const auto& Pair : ItemsMap)
				{
					const FGuid& Guid = Pair.Key;
					const FIntPoint Size = InventoryComp->GetInventorySizeByGuid(Guid);
					UE_LOG(LogTemp, Warning, TEXT("  Container GUID=%s Size=(%d,%d) ItemCount=%d"), *Guid.ToString(), Size.X, Size.Y, Pair.Value.Items.Num());
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("InteractActor::Interact_Implementation - InventoryComp is null"));
			}

			if (UEquipComponent* EComp = FindComponentByClass<UEquipComponent>())
			{
				UE_LOG(LogTemp, Warning, TEXT("InteractActor::Interact_Implementation - InitForHuman called"));
				Win->InitForHuman(InventoryComp, EComp);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("InteractActor::Interact_Implementation - InitForContainer called"));
				// Prefer the actor's own registered container GUID (MyActorGuid) if present.
				FGuid PreferredGuid;
				if (InventoryComp)
				{
					if (MyActorGuid.IsValid() && InventoryComp->GetItemsMap().Contains(MyActorGuid))
					{
						PreferredGuid = MyActorGuid;
						UE_LOG(LogTemp, Warning, TEXT("InteractActor: PreferredGuid set to MyActorGuid=%s"), *PreferredGuid.ToString());
					}
					else
					{
						// Fallback: choose the largest available container by area
						int32 BestArea = 0;
						for (const auto& Pair : InventoryComp->GetItemsMap())
						{
							const FGuid& G = Pair.Key;
							const FIntPoint Size = InventoryComp->GetInventorySizeByGuid(G);
							int32 Area = Size.X * Size.Y;
							if (Area > BestArea)
							{
								BestArea = Area;
								PreferredGuid = G;
							}
						}

						// If no positive-size container found, fallback to first available GUID
						if (!PreferredGuid.IsValid())
						{
							for (const auto& Pair : InventoryComp->GetItemsMap())
							{
								PreferredGuid = Pair.Key;
								break;
							}
						}
					}
				}

				if (PreferredGuid.IsValid())
				{
					Win->InitForContainer(InventoryComp, PreferredGuid);
				}
				else
				{
					Win->InitForContainer(InventoryComp);
				}

				UE_LOG(LogTemp, Warning, TEXT("상장열기"));
			}
			// Force a refresh to ensure main inventory grid is created immediately
			UE_LOG(LogTemp, Warning, TEXT("InteractActor::Interact_Implementation - RefreshAllGrids called"));
			Win->RefreshAllGrids();
		}
	}
}

