// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Common/GameDefine.h"
#include "Components/ActorComponent.h"
#include "MapGeneratorComponent.generated.h"


UCLASS()
class PROJECTPG_API UMapGeneratorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UMapGeneratorComponent();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<class AMapTile> _tileBlueprint;

	UPROPERTY(VisibleAnywhere)
	TArray<class AMapTile*> _tiles;

	UPROPERTY(VisibleAnywhere)
	class AMapTile* _tileSpawn;

	UPROPERTY(VisibleAnywhere)
	TArray<class AMapTile*> _tilesRoad;

	UPROPERTY(VisibleAnywhere)
	TArray<class AMapTile*> _tilesObstacle;

	UPROPERTY(VisibleAnywhere)
	TArray<class AMapTile*> _tilesExit;

	UPROPERTY(VisibleAnywhere)
	TArray<class AMapTile*> _tilesWarZone;

	TArray<TPair<int, int>> _tilesRoadEndpointWarzoneSide;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double _tileScale = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double _gooness = 1.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double _metaballThreshold = 0.3;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double _metaballCenterBias = 0.7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	double _metaballSizeBonus = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int _mapSize = 45;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int _startPositionRangeSize = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int _sectionCoreSize = 5;

public:
	void Generate();
	AMapTile* FindTile(int x, int y);

private:
	void GenerateRoad(int directionKey, ETileType cubeType);
	void GenerateEndPoint(int indexX, int indexY, ETileType cubeType);
	void GenerateRoadFrom(int indexX, int indexY, int directionKey, ETileType cubeType);
	void GenerateRoadTile(int indexX, int indexY, bool IsObstacle = false);
	void GenerateWarZone();
	bool IsTileARoad(int indexX, int indexY);
	bool IsTileInCenterArea(int indexX, int indexY);
	bool IsTileOutOfExpectedArea(int indexX, int indexY, int directionKey);
	bool IsTileInBoundary(int indexX, int indexY);
	bool CheckSquareForm(int oldIndexX, int oldIndexY, int newIndexX, int newIndexY);
};
