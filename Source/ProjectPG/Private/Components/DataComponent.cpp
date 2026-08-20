// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/DataComponent.h"
#include "Server/WebSocketSubSystem.h"
#include <Components/InventoryComponent.h>
// Sets default values for this component's properties
UDataComponent::UDataComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

void UDataComponent::BeginPlay()
{
	Super::BeginPlay();

	UWebSocketSubSystem* Subsystem = UWebSocketSubSystem::Get(GetWorld());
	if (IsValid(Subsystem))
	{
		Subsystem->OnInventoryReceived.RemoveDynamic(this, &UDataComponent::LoadInventoryData);
		Subsystem->OnInventoryReceived.AddDynamic(this, &UDataComponent::LoadInventoryData);
	}
}

// 💡 매개변수 타입 변경 및 Wrapper 내부 Map 추출
void UDataComponent::LoadInventoryData(FInventoryMapWrapper ItemsWrapper)
{
	// Wrapper 안에서 실제 TMap 추출
	ItemData = ItemsWrapper.InventoryMap;

	UInventoryComponent* InvenComp = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (IsValid(InvenComp))
	{
		InvenComp->AllocateItemDataByGuid(ItemsWrapper.InventoryMap);
		UE_LOG(LogTemp, Warning, TEXT("[DataComponent] Inventory Updated. Total Bags: %d"), ItemsWrapper.InventoryMap.Num());
	}
}