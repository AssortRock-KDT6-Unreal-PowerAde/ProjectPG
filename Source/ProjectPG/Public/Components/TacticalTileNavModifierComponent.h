// Stable low-cost navigation blockers for runtime tactical tile instances.

#pragma once

#include "CoreMinimal.h"
#include "NavRelevantComponent.h"
#include "TacticalTileNavModifierComponent.generated.h"

UCLASS(ClassGroup = (Navigation), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UTacticalTileNavModifierComponent : public UNavRelevantComponent
{
	GENERATED_BODY()

public:
	UTacticalTileNavModifierComponent(const FObjectInitializer& ObjectInitializer);

	void SetWorldBlockers(const TArray<FBox>& InWorldBlockers);

	virtual void CalcAndCacheBounds() const override;
	virtual void GetNavigationData(FNavigationRelevantData& Data) const override;

private:
	UPROPERTY(Transient)
	TArray<FBox> WorldBlockers;
};
