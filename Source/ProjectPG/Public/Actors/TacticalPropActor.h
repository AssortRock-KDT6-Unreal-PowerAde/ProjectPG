// Blueprint prop base for procedural tactical tiles.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalPropActor.generated.h"

class UStaticMeshComponent;

UCLASS(Blueprintable)
class PROJECTPG_API ATacticalPropActor : public AActor
{
	GENERATED_BODY()

public:
	ATacticalPropActor();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tactical Prop")
	TObjectPtr<UStaticMeshComponent> PropMesh;
};
