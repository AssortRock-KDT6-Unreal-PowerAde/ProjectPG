// Fill out your copyright notice in the Description page of Project Settings.


#include "Common/TableData.h"

FString FItemTableRow::GetItemTypeString() const
{
	switch (ItemType)
	{
	case EItemType::Weapon:
		return FString("무기");
	case EItemType::Armor:
		return FString("방어구");
	case EItemType::Consumable:
		return FString("소비품");
	case EItemType::Quest:
		return FString("퀘스트템");
	case EItemType::ETC:
		return FString("기타");
	}
	return FString("");
}
