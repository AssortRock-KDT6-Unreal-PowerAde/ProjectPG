// 절차 전술 타일에 쓰는 블루프린트 소품의 부모 클래스.

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
