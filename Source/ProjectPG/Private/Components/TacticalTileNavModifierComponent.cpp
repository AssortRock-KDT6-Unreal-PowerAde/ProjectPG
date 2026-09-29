// Stable low-cost navigation blockers for runtime tactical tile instances.

#include "Components/TacticalTileNavModifierComponent.h"

#include "AI/Navigation/NavigationRelevantData.h"
#include "AI/NavigationModifier.h"
#include "GameFramework/Actor.h"
#include "NavAreas/NavArea_Null.h"
#include "NavigationSystem.h"

UTacticalTileNavModifierComponent::UTacticalTileNavModifierComponent(
	const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ForceNavigationRelevancy(true);
}

void UTacticalTileNavModifierComponent::SetWorldBlockers(const TArray<FBox>& InWorldBlockers)
{
	WorldBlockers = InWorldBlockers;
	UpdateNavigationBounds();
	RefreshNavigationModifiers();
}

void UTacticalTileNavModifierComponent::CalcAndCacheBounds() const
{
	Bounds = FBox(ForceInit);
	for (const FBox& WorldBox : WorldBlockers)
		Bounds += WorldBox;

	if (!Bounds.IsValid)
		Bounds = FBox::BuildAABB(GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector, FVector(1.0f));
}

void UTacticalTileNavModifierComponent::GetNavigationData(FNavigationRelevantData& Data) const
{
	for (const FBox& WorldBox : WorldBlockers)
		Data.Modifiers.Add(FAreaNavModifier(WorldBox, FTransform::Identity, UNavArea_Null::StaticClass()));
}
