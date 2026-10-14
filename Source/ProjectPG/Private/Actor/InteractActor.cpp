#include "Actor/InteractActor.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "GameFramework/PlayerController.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/InventoryWindow.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/ActorChannel.h"
#include "Net/UnrealNetwork.h"
#include "Core/ItemSubSystem.h"
#include <Core/TableSubSystem.h>
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
		RandomItemSetting();

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
	//if (InventoryComp->IsServerManaged() && !InventoryComp->HasInitialInventory()) return;

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

void AInteractActor::RandomItemSetting()
{
	//아이템 테이블에 있는 목록가져오기
	//몇개의아이템을 인벤에 넣을지 지정
	//아이템 별 gridsize가 있는데 그게 다찰수있도록 해야됌(5,5 / 3,3) 위아래로 배치될수있음 총가로 횟수는 10으로 제한	
	int max = FMath::RandRange(1, 10);
	UTableSubSystem* subsytem = UTableSubSystem::Get(GetWorld());
	if (false == IsValid(subsytem)) return;

	UDataTable *table = subsytem->FindTable(TEXT("ItemTable"));
	if (nullptr == table) return;

	//테이블에 있는 목록개수
	auto it = table->GetRowMap().CreateConstIterator();
	TArray<FName> RowNames = table->GetRowNames();
	int32 tableCount = RowNames.Num();
	
	const int32 GridWidth = 10;
	int32 CurrentGridHeight = 10;
	TArray<bool> GridSlots;
	GridSlots.Init(false, GridWidth * CurrentGridHeight);

	for (int i = 0; i < max;i++)
	{
		int32 randomIndex = FMath::RandRange(0, tableCount - 1);
		FName targetRowName = RowNames[randomIndex];
		FItemTableRow* itemData = table->FindRow<FItemTableRow>(targetRowName, TEXT("RandomItemPickContext"));

		if (!itemData) continue;

		// 이 아이템이 차지하는 크기 (예: 가로 2칸, 세로 2칸 등)
		FIntPoint ItemSize = itemData->GridSize;

		// 아이템이 들어갈 자리를 찾았는지 여부
		bool bFoundSlot = false;
		int32 SpawnX = 0;
		int32 SpawnY = 0;

		// 2. 빈 공간을 찾을 때까지 세로(Y)를 무한히 탐색 (공간이 부족하면 자동으로 Y가 늘어남)
		for (int32 y = 0; !bFoundSlot; y++)
		{
			// 만약 현재 그리드 세로 범위를 넘어가려고 하면 그리드 크기를 동적으로 확장
			if (y + ItemSize.Y > CurrentGridHeight)
			{
				int32 NewHeight = y + ItemSize.Y;
				GridSlots.AddZeroed(GridWidth * (NewHeight - CurrentGridHeight));
				CurrentGridHeight = NewHeight;
			}

			// 가로(X) 검색 (아이템이 가로 경계를 벗어나지 않는 선까지)
			for (int32 x = 0; x <= GridWidth - ItemSize.X; x++)
			{
				bool bCanPlace = true;

				// 아이템 크기만큼 공간이 모두 비어있는지(`false`) 확인
				for (int32 ItemY = 0; ItemY < ItemSize.Y; ItemY++)
				{
					for (int32 ItemX = 0; ItemX < ItemSize.X; ItemX++)
					{
						int32 SlotIndex = (y + ItemY) * GridWidth + (x + ItemX);
						if (GridSlots[SlotIndex])
						{
							bCanPlace = false;
							break;
						}
					}
					if (!bCanPlace) break;
				}

				// 배치 가능한 빈 공간을 찾음!
				if (bCanPlace)
				{
					SpawnX = x;
					SpawnY = y;
					bFoundSlot = true;
					break;
				}
			}
		}

		// 3. 찾은 자리에 아이템 크기만큼 점유 표시 (`true`)
		for (int32 ItemY = 0; ItemY < ItemSize.Y; ItemY++)
		{
			for (int32 ItemX = 0; ItemX < ItemSize.X; ItemX++)
			{
				int32 SlotIndex = (SpawnY + ItemY) * GridWidth + (SpawnX + ItemX);
				GridSlots[SlotIndex] = true;
			}
		}

		// 4. 최종 결정된 그리드 크기를 컴포넌트에 갱신등록
		FIntPoint FinalGridSize = FIntPoint(GridWidth, CurrentGridHeight);
		InventoryComp->RegisterContainer(MyActorGuid, FinalGridSize);

		// 5. 아이템 인스턴스 생성 및 지정된 좌표에 추가
		FItemInstance item;
		item.parent_inventory_guid = MyActorGuid;
		item.ItemID = itemData->ItemID;

		// 컴포넌트의 AddItemAt 함수 명세에 따라 적절히 좌표를 전달합니다.
		// 보통 FIntPoint(SpawnX, SpawnY)나 인덱스를 받습니다.
		InventoryComp->AddItemAt(item, FIntPoint(SpawnX, SpawnY));

	}
}

