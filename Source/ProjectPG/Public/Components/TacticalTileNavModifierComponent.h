// 런타임 전술 타일 인스턴스에 쓰는, 안정적이고 가벼운 길찾기 막는 부품.

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
