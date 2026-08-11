// Fill out your copyright notice in the Description page of Project Settings.


#include "Common/GameData.h"
#include "Common/TableData.h"

FIntPoint FItemInstance::GetCurrentGridSize(const FItemTableRow* ItemData) const
{
	if (!ItemData) return FIntPoint(1, 1);
	return bIsRotated ? FIntPoint(ItemData->GridSize.Y, ItemData->GridSize.X) : ItemData->GridSize;
}
