// 절차 전술 타일에 쓰는 블루프린트 소품의 부모 클래스.

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
