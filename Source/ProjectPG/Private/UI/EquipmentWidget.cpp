#include "UI/EquipmentWidget.h"
#include "UI/EquipSlot.h"
#include "Components/Image.h"
#include "Components/EquipComponent.h"
#include "Components/InventoryComponent.h"
#include "UI/InventoryWindow.h"
#include "Core/TableSubSystem.h"
#include "Core/UIManagerSubSystem.h"

void UEquipmentWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 1. 각 슬롯 타입 지정
	if (WeaponSlot) WeaponSlot->SetSlot(EEquipSlot::MainWeapon);
	if (SubWeaponSlot) SubWeaponSlot->SetSlot(EEquipSlot::SubWeapon);
	if (HelmetSlot) HelmetSlot->SetSlot(EEquipSlot::HelMet);
	if (ClothSlot) ClothSlot->SetSlot(EEquipSlot::Cloth);
	if (PantsSlot) PantsSlot->SetSlot(EEquipSlot::Pants);
	if (ShoesSlot) ShoesSlot->SetSlot(EEquipSlot::Shose);
	if (BackPackSlot) BackPackSlot->SetSlot(EEquipSlot::BackPack);
	if (ConsumalSlot) ConsumalSlot->SetSlot(EEquipSlot::Accuracy1);
	if (HealPackSlot) HealPackSlot->SetSlot(EEquipSlot::Accuracy2);

	// 2. TMap 초기화 (중복 방지를 위한 Empty 처리)
	SlotWidgetMap.Empty();
	SlotWidgetMap.Add(EEquipSlot::MainWeapon, WeaponSlot);
	SlotWidgetMap.Add(EEquipSlot::SubWeapon, SubWeaponSlot);
	SlotWidgetMap.Add(EEquipSlot::HelMet, HelmetSlot);
	SlotWidgetMap.Add(EEquipSlot::Cloth, ClothSlot);
	SlotWidgetMap.Add(EEquipSlot::Pants, PantsSlot);
	SlotWidgetMap.Add(EEquipSlot::Shose, ShoesSlot);
	SlotWidgetMap.Add(EEquipSlot::BackPack, BackPackSlot);
	SlotWidgetMap.Add(EEquipSlot::Accuracy1, ConsumalSlot);
	SlotWidgetMap.Add(EEquipSlot::Accuracy2, HealPackSlot);

	
}

void UEquipmentWidget::NativeDestruct()
{
	if (EquipComponent)
	{
		EquipComponent->OnEquipmentChanged.RemoveAll(this);
	}

	Super::NativeDestruct();
}

void UEquipmentWidget::InitWidget(UEquipComponent* InEquipComp, UInventoryComponent* InInvenComp)
{
	UE_LOG(LogTemp, Warning, TEXT("아이템 장착 업데이트"));

	if (EquipComponent)
	{
		EquipComponent->OnEquipmentChanged.RemoveAll(this);
	}

	EquipComponent = InEquipComp;
	InventoryComponent = InInvenComp;

	// 각 개별 슬롯에도 컴포넌트 전달
	for (auto& Pair : SlotWidgetMap)
	{
		if (IsValid(Pair.Value))
		{
			Pair.Value->InitWidget(InEquipComp, InInvenComp);
		}
	}

	if (EquipComponent)
	{
		// 장착 데이터 변경 이벤트 바인딩
		EquipComponent->OnEquipmentChanged.RemoveDynamic(this, &UEquipmentWidget::UpdateEquipmentUI);

		EquipComponent->OnEquipmentChanged.AddDynamic(this, &UEquipmentWidget::UpdateEquipmentUI);

		// 초기 UI 동기화
		UpdateEquipmentUI();
	}
}

void UEquipmentWidget::UpdateEquipmentUI()
{
	if (!EquipComponent) return;

	// 1. 등록된 모든 슬롯 위젯 순회하며 슬롯 UI 갱신
	for (auto& Pair : SlotWidgetMap)
	{
		EEquipSlot SlotType = Pair.Key;
		UEquipSlot* SlotWidget = Pair.Value;

		if (!IsValid(SlotWidget)) continue;

		// EquipComponent에서 해당 슬롯에 장착된 아이템 포인터 가져오기
		const FItemInstance* EquippedItem = EquipComponent->GetEquipment(SlotType);

		// UEquipSlot의 SetItem 호출 (nullptr 전달 시 내부에서 Clear 처리됨)
		SlotWidget->SetItem(EquippedItem);
	}

	// 2. ★ 가방(BackPack) 슬롯 상태 처리 (컨테이너 등록 및 UI 생성/파괴)
	HandleBackpackContainerUpdate();
}
void UEquipmentWidget::HandleBackpackContainerUpdate()
{
	if (!EquipComponent || !InventoryComponent) return;

	const FItemInstance* BackpackItem = EquipComponent->GetEquipment(EEquipSlot::BackPack);
	UInventoryWindow* ParentInvenWindow = Cast<UInventoryWindow>(GetTypedOuter<UUserWidget>());

	if (!ParentInvenWindow) return;

	// [1. 가방을 장착하고 있는 경우] (BackpackItem 안전성 검사를 먼저 수행)
	if (BackpackItem && BackpackItem->GUID.IsValid())
	{
		// 로그 출력은 포인터 검증이 끝난 안전한 이곳에서 수행합니다.
		UE_LOG(LogTemp, Warning, TEXT("아이템 장착 상태 : %d"), BackpackItem->bEquip);

		UTableSubSystem* subSystem = UTableSubSystem::Get(GetWorld());
		if (!subSystem) return;

		const FItemBackpackTable* ItemData = subSystem->FindTableRow<FItemBackpackTable>("BackpackTable", BackpackItem->ItemID);

		if (ItemData && ItemData->SlotSize.X > 0 && ItemData->SlotSize.Y > 0)
		{
			// ★ 1. InventoryComponent에 가방 메모리 공간(크기) 우선 등록
			InventoryComponent->RegisterContainer(
				BackpackItem->GUID,
				FIntPoint(ItemData->SlotSize.X, ItemData->SlotSize.Y)
			);

			// ★ 2. 크기 등록 후 가방 UI 위젯 생성 호출
			UUIManagerSubSystem* UISub = UUIManagerSubSystem::Get(GetWorld());
			if (UISub)
			{
				TSubclassOf<UUserWidget> InvenClass = UISub->GetUIClass(EUIType::Inventory);
				if (InvenClass)
				{
					ParentInvenWindow->SetupBackPackInventoryWidget(InvenClass);
				}
			}
		}
	}
	// [2. 가방을 장착하지 않았거나 해제한 경우]
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("가방을 장착하지 않았거나 해제했습니다."));
		ParentInvenWindow->SetupBackPackInventoryWidget(nullptr);
	}
}