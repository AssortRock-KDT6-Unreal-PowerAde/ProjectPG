// DataComponent.cpp
#include "Components/DataComponent.h"
#include "Server/InventorySubSystem.h"

#include "Components/InventoryComponent.h"

UDataComponent::UDataComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UDataComponent::BeginPlay()
{
	Super::BeginPlay();

	
		// 인벤토리 담당(UInventorySubSystem)이 칸을 만들면 받는다.
		if (UInventorySubSystem* Subsystem = UInventorySubSystem::Get(GetWorld()))
		{
			Subsystem->OnInventoryReceived.RemoveDynamic(this, &UDataComponent::LoadInventoryData);
			Subsystem->OnInventoryReceived.AddDynamic(this, &UDataComponent::LoadInventoryData);
		}
	
}
void UDataComponent::LoadInventoryData(const FInventoryMapWrapper& ItemsWrapper)
{
	ItemData = ItemsWrapper.InventoryMap;

	if (UInventoryComponent* InvenComp = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr)
	{
		InvenComp->SetServerInventoryData(ItemsWrapper);
		UE_LOG(LogTemp, Warning, TEXT("[DataComponent] 인벤토리 데이터 할당 완료. 총 컨테이너 개수: %d"), ItemsWrapper.InventoryMap.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DataComponent] InventoryComponent를 찾을 수 없습니다."));
	}
}