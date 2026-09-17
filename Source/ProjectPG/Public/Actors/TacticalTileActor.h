// Runtime-spawnable 20x20m tactical tile family used by the procedural map visual layer.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TacticalTileActor.generated.h"

class UArrowComponent;
class UChildActorComponent;
class UInstancedStaticMeshComponent;
class USceneComponent;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ETacticalTileKind : uint8
{
	RoadStraight,
	RoadCorner,
	RoadTJunction,
	RoadCross,
	RoadDeadEnd,
	SpawnStaging,
	ExitCheckpoint,
	ObstacleCheckpoint,
	OpenGround,
	Ruins,
	NatureMeadow,
	NatureForestSparse,
	NatureForestDense,
	NatureRocky,
	NatureScrub,
	NatureAmbush,
	NatureServiceCamp,
	NatureDitch,
	WarZoneYard,
	WarZoneWarehouse
};

UCLASS(Blueprintable)
class PROJECTPG_API ATacticalTileActor : public AActor
{
	GENERATED_BODY()

public:
	ATacticalTileActor();
	virtual void OnConstruction(const FTransform& Transform) override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactical Tile")
	ETacticalTileKind TileKind = ETacticalTileKind::OpenGround;

	// Authored visual footprint in 20m grid cells. It is serialized into the
	// future TileManifest; the anchor cell remains the only spawned actor.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Tactical Tile")
	FIntPoint FootprintCells = FIntPoint(1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile")
	int32 LocalSeed = 1337;

	// Stable authored layout variant. 255 derives a 0..3 variant from LocalSeed.
	// The value is part of the future TileManifest contract so multiplayer clients
	// never depend on a different random-stream implementation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 LayoutVariantOverride = 255;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile")
	bool bShowDynamicProps = true;

	// Design-layer POI connector. It keeps the same socket contract as a road
	// tile, but uses varied natural shoulder cover instead of repeating the full
	// authored road dressing every 20 metres.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile")
	bool bIsAccessRoad = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float DressingDensityScale = 1.0f;

	// 255 keeps the authored canonical mask. Values 0..15 override N/E/S/W at runtime.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tactical Tile", meta = (ClampMin = "0", ClampMax = "255"))
	uint8 ConnectionMaskOverride = 255;

	UFUNCTION(BlueprintPure, Category = "Tactical Tile")
	uint8 GetCanonicalConnectionMask() const;

	UFUNCTION(BlueprintPure, Category = "Tactical Tile")
	uint8 GetEffectiveConnectionMask() const;

	UFUNCTION(BlueprintPure, Category = "Tactical Tile")
	FIntPoint GetFootprintCells() const;

	UFUNCTION(BlueprintPure, Category = "Tactical Tile")
	uint8 GetEffectiveLayoutVariant() const;

	void RebuildFromRuntimeSpec(int32 InSeed, uint8 InConnectionMask, uint8 InLayoutVariant = 255);

private:
	void RebuildTile();
	void AddRoad(uint8 Mask, float Width = 650.0f);
	void AddRoadShoulderDressing(uint8 Mask, uint8 Variant, FRandomStream& Stream);
	void AddBrokenBoundary(uint8 OpenMask, int32 Density = 3);
	void AddShelter(const FVector& Center, float Yaw, bool bDoor);
	void AddGrass(int32 Count, FRandomStream& Stream);
	void AddOpenGroundLayout(uint8 Variant, FRandomStream& Stream);
	void AddRuinsLayout(uint8 Variant, FRandomStream& Stream);
	void AddNatureLayout(ETacticalTileKind NatureKind, uint8 Variant, FRandomStream& Stream);
	void AddTree(const FVector& Location, float Height, float CanopyRadius, FRandomStream& Stream);
	void AddRock(const FVector& Location, const FVector& Size, float Yaw);
	void AddBushCluster(const FVector& Center, int32 Count, float Radius, FRandomStream& Stream);
	void AddFallenLog(const FVector& Location, float Yaw, float Length, float Radius);
	void AddRoadTacticalCover(uint8 Mask, uint8 Variant, FRandomStream& Stream);
	void AddLowWall(const FVector& Location, float Yaw = 0.0f, float LengthScale = 1.0f);
	void ApplyDynamicProps(FRandomStream& Stream);

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Ground;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> RoadPieces;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> SolidWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> HalfWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> QuarterWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ShootingWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> DoorWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> WindowWalls;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> RoofFloors;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ConcreteCover;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> BarrelCover;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> GrassDressing;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> GrassDressingB;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> GrassDressingC;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> TreeTrunks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> TreeCanopies;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> RockCover;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> RockCoverB;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> BushDressing;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> BushDressingB;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> HeroShrubDressing;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> FallenLogs;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> TerrainBerms;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> ElevationStairs;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> MarkingPieces;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> UtilityProps;

	// Selected Fab industrial pieces. These remain deterministic ISM dressing so
	// the future TileManifest only needs tile kind, seed and variant.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> FactoryContainers;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> FactoryTanks;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> FactoryPallets;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UInstancedStaticMeshComponent> FactoryFences;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> DynamicPropA;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> DynamicPropB;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UChildActorComponent> DynamicPropC;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ConnectionNorth;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ConnectionEast;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ConnectionSouth;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ConnectionWest;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> SpawnPoint;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> LootPoint;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> AISpawnPoint;

	UPROPERTY(VisibleAnywhere, Category = "Tactical Tile|Sockets")
	TObjectPtr<UArrowComponent> ExitPoint;

	TSoftClassPtr<AActor> CarClass;
	TSoftClassPtr<AActor> BarrelClass;
	TSoftClassPtr<AActor> BarrierClass;
};
