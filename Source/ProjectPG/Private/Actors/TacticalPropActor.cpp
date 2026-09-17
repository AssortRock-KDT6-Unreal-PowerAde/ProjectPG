// Blueprint prop base for procedural tactical tiles.

#include "Actors/TacticalPropActor.h"

#include "Components/StaticMeshComponent.h"

ATacticalPropActor::ATacticalPropActor()
{
	PrimaryActorTick.bCanEverTick = false;
	PropMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PropMesh"));
	SetRootComponent(PropMesh);
	PropMesh->SetCollisionProfileName(TEXT("BlockAll"));
	PropMesh->SetGenerateOverlapEvents(false);
	PropMesh->SetCanEverAffectNavigation(false);
}
