// Playable 20x20m straight-road tile prototype. Does not modify the team's generator.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalTileRoadStraight.generated.h"

class UChildActorComponent;
class UArrowComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMeshComponent;

UCLASS(Blueprintable)
class PROJECTPG_API ATacticalTileRoadStraight : public AActor
{
	GENERATED_BODY()

public:
	ATacticalTileRoadStraight();
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile")
	int32 LocalSeed = 1337;

	// 255 selects a stable variant from LocalSeed. Values 0..3 are written by
	// the future TileManifest so server and clients construct the same cover.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile")
	uint8 LayoutVariantOverride = 255;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile")
	bool bShowDynamicProps = true;

	UFUNCTION(BlueprintPure, Category = "Tactical Tile")
	uint8 GetEffectiveLayoutVariant() const;

	void RebuildFromRuntimeSpec(int32 InSeed, uint8 InLayoutVariant = 255);

private:
	void RebuildFixedInstances();
	void ApplyDynamicPropClasses();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Road;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> BoundaryWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> DoorWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ShelterRoofs;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ConcreteCover;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> BarrelCover;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> GrassDressing;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LaneMarkLeft;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> LaneMarkRight;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> DynamicPropNorth;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> DynamicPropSouth;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> DynamicPropMid;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ConnectionWest;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ConnectionEast;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> LootSpawnSlot;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> AISpawnSlot;

	TSoftClassPtr<AActor> AbandonedCarClass;
	TSoftClassPtr<AActor> BarrelClusterClass;
	TSoftClassPtr<AActor> ConcreteBarrierClass;
};
