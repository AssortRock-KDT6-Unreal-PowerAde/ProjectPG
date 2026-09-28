#include "Objects/PGItemRowFallback.h"

#include "Common/TableData.h"
#include "Engine/Texture2D.h"
#include "Objects/PGWearableColors.h"

namespace
{
	struct FRule
	{
		const TCHAR* Prefix; // 이 글자로 시작하는 ItemId 에 적용
		EItemType Type;
		FIntPoint Grid;
		int32 MaxStack;
	};

	// 칸 크기는 타르코프식 감각: 소총 4×2, 권총 2×1, 탄약 한 묶음 1×1, 구급상자 2×2, 연료통 2×2.
	// 위에서부터 먼저 맞는 것을 쓴다(Ammo_ 가 A 로 시작하는 다른 규칙보다 먼저 오게).
	const FRule ItemRowRules[] = {
		{ TEXT("Ammo_"), EItemType::Consumable, FIntPoint(1, 1), 60 },
		{ TEXT("Magazine_"), EItemType::Consumable, FIntPoint(1, 2), 1 },
		{ TEXT("Rifle_"), EItemType::Weapon, FIntPoint(4, 2), 1 },
		{ TEXT("Shotgun"), EItemType::Weapon, FIntPoint(4, 2), 1 },
		{ TEXT("Pistol"), EItemType::Weapon, FIntPoint(2, 1), 1 },
		{ TEXT("Revolver"), EItemType::Weapon, FIntPoint(2, 1), 1 },
		{ TEXT("Bow"), EItemType::Weapon, FIntPoint(2, 4), 1 },
		{ TEXT("Bat"), EItemType::Weapon, FIntPoint(1, 3), 1 },
		{ TEXT("Axe"), EItemType::Weapon, FIntPoint(1, 3), 1 },
		{ TEXT("ThrowingKnife"), EItemType::Weapon, FIntPoint(1, 1), 5 },
		{ TEXT("Grenade"), EItemType::Weapon, FIntPoint(1, 1), 3 },
		{ TEXT("Medkit"), EItemType::Consumable, FIntPoint(2, 2), 1 },
		{ TEXT("Bandage"), EItemType::Consumable, FIntPoint(1, 1), 5 },
		{ TEXT("Painkiller"), EItemType::Consumable, FIntPoint(1, 1), 5 },
		{ TEXT("Tourniquet"), EItemType::Consumable, FIntPoint(1, 1), 3 },
		{ TEXT("Splint"), EItemType::Consumable, FIntPoint(1, 2), 1 },
		{ TEXT("Water"), EItemType::Consumable, FIntPoint(1, 2), 1 },
		{ TEXT("Ration"), EItemType::Consumable, FIntPoint(1, 1), 3 },
		{ TEXT("Fuel"), EItemType::Quest, FIntPoint(2, 2), 1 },   // 탈출·변신에 쓰는 연료통
		{ TEXT("Key_"), EItemType::Quest, FIntPoint(1, 1), 1 },
		{ TEXT("QuestItem_"), EItemType::Quest, FIntPoint(1, 1), 1 },
		{ TEXT("Tool_"), EItemType::ETC, FIntPoint(1, 2), 1 },
		{ TEXT("Money"), EItemType::ETC, FIntPoint(1, 1), 9999 },
		{ TEXT("Scrap"), EItemType::ETC, FIntPoint(1, 1), 10 },
		{ TEXT("RareMaterial"), EItemType::ETC, FIntPoint(1, 1), 5 },
		{ TEXT("Recipe"), EItemType::ETC, FIntPoint(1, 1), 1 },
	};

	// 옷(착장)은 자리별 칸 크기. 방탄조끼·배낭은 크다.
	FIntPoint WearGrid(EPGWearSlot Slot)
	{
		switch (Slot)
		{
		case EPGWearSlot::Vest:
		case EPGWearSlot::Backpack:
			return FIntPoint(3, 3);
		case EPGWearSlot::Pouch:
		case EPGWearSlot::Holster:
			return FIntPoint(2, 1);
		default:
			return FIntPoint(2, 2);
		}
	}

	// 팀 표의 1001 행과 같은 엔진 기본 아이콘. 아이콘이 비면 인벤토리 칸에 아무것도 안 보였다.
	UTexture2D* DefaultIcon()
	{
		static UTexture2D* Icon = nullptr;
		if (!Icon)
		{
			Icon = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/AICON-Green.AICON-Green"));
			if (Icon)
				Icon->AddToRoot(); // 캐시의 행이 게임 내내 가리키므로 GC 에서 뺀다
		}
		return Icon;
	}

	bool IsNumeric(const FString& Text)
	{
		if (Text.IsEmpty())
			return false;
		for (const TCHAR Char : Text)
			if (!FChar::IsDigit(Char))
				return false;
		return true;
	}

	// 포인터가 게임 내내 유효해야 해서(인벤토리가 행 포인터를 잠깐씩 들고 쓴다) 행마다 따로 할당해 둔다.
	// TMap 에 값으로 넣으면 원소가 늘 때 재배치되어 먼저 준 포인터가 깨진다.
	TMap<FName, TUniquePtr<FItemTableRow>>& Cache()
	{
		static TMap<FName, TUniquePtr<FItemTableRow>> Rows;
		return Rows;
	}
}

const FItemTableRow* PGItemRowFallback::Find(FName ItemID)
{
	const FString Id = ItemID.ToString();
	if (ItemID.IsNone() || IsNumeric(Id))
		return nullptr;
	if (const TUniquePtr<FItemTableRow>* Found = Cache().Find(ItemID))
		return Found->Get();

	TUniquePtr<FItemTableRow> Row = MakeUnique<FItemTableRow>();
	Row->ItemID = ItemID;
	Row->DisPlayName = FName(*UPGWearableColorLibrary::GetItemDisplayName(ItemID).ToString());
	Row->Icon = DefaultIcon();
	// 장착 칸(EquipSlotType)은 비워 둔다(MAX). 채우면 팀 장착 코드가 장비 표(EquipTable)를 찾는데 우리 아이템 줄이 없어 경고가 쌓인다.
	// 착장은 오브젝트 쪽 UPGWearableComponent 가 따로 입힌다.
	Row->EquipSlotType = EEquipSlot::MAX;

	FPGWearableColor Wear;
	bool bMatched = false;
	if (UPGWearableColorLibrary::FindWearableColor(ItemID, Wear))
	{
		Row->ItemType = EItemType::Armor;
		Row->GridSize = WearGrid(Wear.Slot);
		Row->MaxStack = 1;
		bMatched = true;
	}
	else
	{
		for (const FRule& Rule : ItemRowRules)
		{
			if (Id.StartsWith(Rule.Prefix))
			{
				Row->ItemType = Rule.Type;
				Row->GridSize = Rule.Grid;
				Row->MaxStack = Rule.MaxStack;
				bMatched = true;
				break;
			}
		}
	}
	if (!bMatched)
	{
		// 규칙에 없는 이름도 1×1 기타로 받아 준다(줍기가 실패하는 것보다 낫다). 새 아이템이 생기면 위 규칙에 한 줄 넣으면 된다.
		Row->ItemType = EItemType::ETC;
		Row->GridSize = FIntPoint(1, 1);
		Row->MaxStack = 1;
		UE_LOG(LogTemp, Display, TEXT("PGItemRowFallback: %s has no rule, using 1x1 ETC"), *Id);
	}

	const FItemTableRow* Result = Row.Get();
	Cache().Add(ItemID, MoveTemp(Row));
	return Result;
}
