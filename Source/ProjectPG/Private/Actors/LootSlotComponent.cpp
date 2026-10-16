#include "Actors/LootSlotComponent.h"

ULootSlotComponent::ULootSlotComponent()
{
	// 상자 하나 크기의 노란 선(가로 80 × 세로 60 × 높이 40cm). 게임에서는 안 보이고 부딪히지 않는다.
	InitBoxExtent(FVector(40.0f, 30.0f, 20.0f));
	ShapeColor = FColor(255, 200, 0);
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetGenerateOverlapEvents(false);
	SetHiddenInGame(true);
	SetCanEverAffectNavigation(false);
	// 타일은 게임 중에 제자리로 옮겨지므로 표시도 따라 움직여야 한다.
	SetMobility(EComponentMobility::Movable);
}
