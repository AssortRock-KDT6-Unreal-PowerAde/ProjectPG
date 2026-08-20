#include "Components/DataComponent.h"
#include "Server/WebSocketSubSystem.h"
#include "Components/InventoryComponent.h"

UDataComponent::UDataComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
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

void UDataComponent::LoadInventoryData(FInventoryMapWrapper ItemsWrapper)
{
	// 1. 데이터 백업
	ItemData = ItemsWrapper.InventoryMap;

	UInventoryComponent* InvenComp = GetOwner() ? GetOwner()->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (IsValid(InvenComp))
	{
		// 2. private 변수에 직접 접근하지 않고 InventoryComponent의 전용 함수 호출
		// (Stash/Pocket GUID 설정 + SizeMap 설정 + 아이템 배치 + GridMap Rebuild가 모두 내부에서 처리됨)
		InvenComp->SetServerInventoryData(ItemsWrapper);

		UE_LOG(LogTemp, Warning, TEXT("[DataComponent] 인벤토리 데이터 할당 완료. 총 컨테이너 개수: %d"), ItemsWrapper.InventoryMap.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[DataComponent] GetOwner()에서 InventoryComponent를 찾을 수 없습니다."));
	}
}