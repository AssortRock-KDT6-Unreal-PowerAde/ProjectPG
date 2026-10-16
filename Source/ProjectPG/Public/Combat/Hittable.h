#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Hittable.generated.h"

UINTERFACE(BlueprintType)
class PROJECTPG_API UHittable : public UInterface
{
	GENERATED_BODY()
};

class PROJECTPG_API IHittable
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat")
	bool CanBeHit() const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Combat")
	void Hit(float Damage, AActor* DamageCauser, const FHitResult& HitResult);
};
