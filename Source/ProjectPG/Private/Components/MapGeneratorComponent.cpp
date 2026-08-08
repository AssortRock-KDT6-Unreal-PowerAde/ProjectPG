// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/MapGeneratorComponent.h"

#include "Actors/MapTile.h"
#include "Common/GameDefine.h"
#include "GameModes/GameModePG.h"

// Sets default values for this component's properties
UMapGeneratorComponent::UMapGeneratorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	ConstructorHelpers::FClassFinder<AMapTile> tile(TEXT("/Game/PG/Blueprint/BP_MapTile"));
	
	if (!tile.Succeeded())
		return;
	
	_tileBlueprint = tile.Class;
}

void UMapGeneratorComponent::Generate()
{
	UWorld* world = GetWorld();
	if (!IsValid(world))
		return;

	AGameModePG* gameMode = Cast<AGameModePG>(world->GetAuthGameMode());
	if (!IsValid(gameMode))
		return;

	// 타일 생성
	for (int i = 0; i < _mapSize; ++i)
	{
		for (int j = 0; j < _mapSize; ++j)
		{
			FTransform transform = FTransform::Identity;

			int x = -_mapSize / 2 + j;
			int y = -_mapSize / 2 + i;

			FVector location = FVector::ZeroVector;
			location.X = x * _tileScale;
			location.Y = y * _tileScale;

			transform.SetLocation(location);

			AMapTile* tile = world->SpawnActorDeferred<AMapTile>(
				_tileBlueprint, transform);

			tile->FinishSpawning(transform);

			_tiles.Add(tile);
		}
	}

	/* 9등분
	 * 0b00'00
	 *   UD'LR
	 * 4 0 5
	 * 2 X 3
	 * 6 1 7
	 */
	TArray<uint32> areas;
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			if (1 == i && 1 == j)
				continue;

			uint32 areaKey = 0;

			if (0 == i)
				areaKey |= static_cast<uint8>(EMapDirection::Left);
			else if (2 == i)
				areaKey |= static_cast<uint8>(EMapDirection::Right);

			if (0 == j)
				areaKey |= static_cast<uint8>(EMapDirection::Up);
			else if (2 == j)
				areaKey |= static_cast<uint8>(EMapDirection::Down);

			areas.Add(areaKey);
		}
	}

	// 스폰 & 탈출구 방향 지정
	gameMode->SetRandomSeed(FDateTime::UtcNow().ToUnixTimestamp());
	for (int i = 0; i < 8; ++i)
	{
		int randomIndex = gameMode->GenerateRandomNumber(0, 7);
		Swap(areas[i], areas[randomIndex]);
	}

	// 스폰 & 탈출구 타일 지정
	{
		int removeCounts = gameMode->GenerateRandomNumber(3, 5);
		areas.RemoveAt(0, removeCounts);

		int spawnKey = areas[0];

		areas.Sort([](int a, int b)
		{
			bool isALR = a & EMapDirection::Left || a & EMapDirection::Right;
			bool isAUD = a & EMapDirection::Up || a & EMapDirection::Down;
			bool isBLR = b & EMapDirection::Left || b & EMapDirection::Right;
			bool isBUD = b & EMapDirection::Up || b & EMapDirection::Down;

			bool isADiag = isALR || isAUD;
			bool isBDiag = isBLR || isBUD;

			if (isADiag && !isBDiag)
				return false;

			if (!isADiag && isBDiag)
				return true;

			return a < b;
		});
		for (int i = 0; i < areas.Num(); ++i)
		{
			int endpointKey = areas[i];
			if (endpointKey == spawnKey)
				GenerateRoad(endpointKey, ETileType::Spawn);
			else
				GenerateRoad(endpointKey, ETileType::Exit);
		}
	}

	// 워존 생성
	GenerateWarZone();
}

AMapTile* UMapGeneratorComponent::FindTile(int x, int y)
{
	if (x < 0 || x >= _mapSize)
		return nullptr;

	if (y < 0 || y >= _mapSize)
		return nullptr;

	return _tiles[y * _mapSize + x];
}

void UMapGeneratorComponent::GenerateRoad(int directionKey, ETileType cubeType)
{
	UWorld* world = GetWorld();
	if (!IsValid(world))
		return;

	AGameModePG* gameMode = Cast<AGameModePG>(world->GetAuthGameMode());
	if (!IsValid(gameMode))
		return;

	bool hasLR = directionKey & EMapDirection::Left || directionKey & EMapDirection::Right;
	bool hasUD = directionKey & EMapDirection::Up || directionKey & EMapDirection::Down;

	int posXRan = gameMode->GenerateRandomNumber(1, _startPositionRangeSize - 1);
	int posYRan = gameMode->GenerateRandomNumber(1, _startPositionRangeSize - 1);

	if (hasLR && hasUD)
	{
		if (directionKey & EMapDirection::Right)
			posXRan += _mapSize - 1 - _startPositionRangeSize;

		if (directionKey & EMapDirection::Down)
			posYRan += _mapSize - 1 - _startPositionRangeSize;
	}
	else
	{
		if (!hasUD)
		{
			posYRan += _mapSize / 2 - _startPositionRangeSize / 2;
			if (directionKey & EMapDirection::Right)
				posXRan += _mapSize - 1 - _startPositionRangeSize;
		}

		if (!hasLR)
		{
			posXRan += _mapSize / 2 - _startPositionRangeSize / 2;
			if (directionKey & EMapDirection::Down)
				posYRan += _mapSize - 1 - _startPositionRangeSize;
		}
	}

	GenerateEndPoint(posXRan, posYRan, cubeType);
	GenerateRoadFrom(posXRan, posYRan, directionKey, cubeType);
}

void UMapGeneratorComponent::GenerateEndPoint(int indexX, int indexY, ETileType cubeType)
{
	AMapTile* tile = FindTile(indexX, indexY);
	tile->SetType(cubeType);

	switch (cubeType)
	{
	case ETileType::Spawn:
		_tileSpawn = tile;
		break;
	case ETileType::Exit:
		_tilesExit.Add(tile);
		break;
	default:
		break;
	}
}

void UMapGeneratorComponent::GenerateRoadFrom(int indexX, int indexY, int directionKey, ETileType cubeType)
{
	UWorld* world = GetWorld();
	if (!IsValid(world))
		return;

	AGameModePG* gameMode = Cast<AGameModePG>(world->GetAuthGameMode());
	if (!IsValid(gameMode))
		return;

	int weightUpward = 1;
	int weightDownward = 1;
	int weightLeftward = 1;
	int weightRightward = 1;

	if (directionKey & EMapDirection::Up)
	{
		weightUpward = 0;
		weightDownward = 4;
	}
	else if (directionKey & EMapDirection::Down)
	{
		weightUpward = 4;
		weightDownward = 0;
	}

	if (directionKey & EMapDirection::Left)
	{
		weightLeftward = 0;
		weightRightward = 4;
	}
	else if (directionKey & EMapDirection::Right)
	{
		weightLeftward = 4;
		weightRightward = 0;
	}

	bool makeObstacle = cubeType == ETileType::Exit;
	int obstaclePossibility = 4;

	int threshold = 0;

	int oldIndexX = indexX;
	int oldIndexY = indexY;

	EMapDirection::Type lastDirection = EMapDirection::Up;
	int directionStack = 0;

	while (!IsTileInCenterArea(indexX, indexY))
	{
		if (++threshold > 30)
		{
			FString message = FString::Printf(TEXT("Over Threshold: %d %d"), indexX, indexY);
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Blue, message);
			break;
		}

		if (IsTileInBoundary(indexX, indexY))
			break;

		AMapTile* tileUp = FindTile(indexX, indexY - 1);
		AMapTile* tileDown = FindTile(indexX, indexY + 1);
		AMapTile* tileLeft = FindTile(indexX - 1, indexY);
		AMapTile* tileRight = FindTile(indexX + 1, indexY);

		if (nullptr == tileUp
			|| directionKey & EMapDirection::Up
			|| tileUp->GetType() != ETileType::None
			|| CheckSquareForm(oldIndexX, oldIndexY, indexX, indexY - 1)
			|| IsTileOutOfExpectedArea(indexX, indexY - 1, directionKey)
			|| IsTileARoad(indexX, indexY - 2)
			|| lastDirection == EMapDirection::Up && directionStack >= 3)
			tileUp = nullptr;
		if (nullptr == tileDown
			|| directionKey & EMapDirection::Down
			|| tileDown->GetType() != ETileType::None
			|| CheckSquareForm(oldIndexX, oldIndexY, indexX, indexY + 1)
			|| IsTileOutOfExpectedArea(indexX, indexY + 1, directionKey)
			|| IsTileARoad(indexX, indexY + 2)
			|| lastDirection == EMapDirection::Down && directionStack >= 3)
			tileDown = nullptr;
		if (nullptr == tileLeft
			|| directionKey & EMapDirection::Left
			|| tileLeft->GetType() != ETileType::None
			|| CheckSquareForm(oldIndexX, oldIndexY, indexX - 1, indexY)
			|| IsTileOutOfExpectedArea(indexX - 1, indexY, directionKey)
			|| IsTileARoad(indexX - 2, indexY)
			|| lastDirection == EMapDirection::Left && directionStack >= 3)
			tileLeft = nullptr;
		if (nullptr == tileRight
			|| directionKey & EMapDirection::Right
			|| tileRight->GetType() != ETileType::None
			|| CheckSquareForm(oldIndexX, oldIndexY, indexX + 1, indexY)
			|| IsTileOutOfExpectedArea(indexX + 1, indexY, directionKey)
			|| IsTileARoad(indexX + 2, indexY)
			|| lastDirection == EMapDirection::Right && directionStack >= 3)
			tileRight = nullptr;

		int totalWeight = 0;
		if (nullptr != tileUp)
			totalWeight += weightUpward;
		if (nullptr != tileDown)
			totalWeight += weightDownward;
		if (nullptr != tileLeft)
			totalWeight += weightLeftward;
		if (nullptr != tileRight)
			totalWeight += weightRightward;

		int r = gameMode->GenerateRandomNumber(0, totalWeight - 1);

		int newIndexX = indexX;
		int newIndexY = indexY;

		if (r < weightUpward && nullptr != tileUp)
		{
			newIndexY = FMath::Max(0, newIndexY - 1);
			if (lastDirection != EMapDirection::Up)
			{
				lastDirection = EMapDirection::Up;
				directionStack = 1;
			}
			else
			{
				++directionStack;
			}
		}
		else if (r < weightUpward + weightDownward && nullptr != tileDown)
		{
			newIndexY = FMath::Min(_mapSize - 1, newIndexY + 1);

			if (lastDirection != EMapDirection::Down)
			{
				lastDirection = EMapDirection::Down;
				directionStack = 1;
			}
			else
			{
				++directionStack;
			}
		}
		else if (r < weightUpward + weightDownward + weightLeftward && nullptr != tileLeft)
		{
			newIndexX = FMath::Max(0, newIndexX - 1);
			if (lastDirection != EMapDirection::Left)
			{
				lastDirection = EMapDirection::Left;
				directionStack = 1;
			}
			else
			{
				++directionStack;
			}
		}
		else if (r < weightUpward + weightDownward + weightLeftward + weightRightward && nullptr != tileRight)
		{
			newIndexX = FMath::Min(_mapSize - 1, newIndexX + 1);
			if (lastDirection != EMapDirection::Right)
			{
				lastDirection = EMapDirection::Right;
				directionStack = 1;
			}
			else
			{
				++directionStack;
			}
		}
		else
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Blue, TEXT("No Weight"));
			break;
		}

		AMapTile* tile = FindTile(newIndexX, newIndexY);
		if (nullptr == tile)
			break;

		if (ETileType::Spawn == tile->GetType() || ETileType::Exit == tile->GetType())
			continue;

		if (makeObstacle)
		{
			bool makeObstacleNow = gameMode->CheckPossibility(obstaclePossibility, 10);
			if (makeObstacleNow)
			{
				GenerateRoadTile(newIndexX, newIndexY, true);
				makeObstacle = false;

				oldIndexX = indexX;
				oldIndexY = indexY;

				indexX = newIndexX;
				indexY = newIndexY;

				continue;
			}

			obstaclePossibility += 3;
		}

		GenerateRoadTile(newIndexX, newIndexY);

		oldIndexX = indexX;
		oldIndexY = indexY;

		indexX = newIndexX;
		indexY = newIndexY;
	}

	_tilesRoadEndpointWarzoneSide.Add(TPair<int, int>(indexX, indexY));
}

void UMapGeneratorComponent::GenerateRoadTile(int indexX, int indexY, bool IsObstacle)
{
	AMapTile* tile = FindTile(indexX, indexY);

	if (nullptr == tile)
		return;

	if (tile->GetType() != ETileType::None)
		return;

	if (IsObstacle)
	{
		tile->SetType(ETileType::Obstacle);
		_tilesObstacle.Add(tile);
	}
	else
	{
		tile->SetType(ETileType::Road);
		_tilesRoad.Add(tile);
	}
}

void UMapGeneratorComponent::GenerateWarZone()
{
	AMapTile* tileFirst = FindTile(0, 0);
	AMapTile* tileLast = FindTile(_mapSize - 1, _mapSize - 1);
	AMapTile* tileMidpoint = FindTile(_mapSize / 2, _mapSize / 2);

	FVector vectorDiagonal = tileLast->GetActorLocation() - tileFirst->GetActorLocation();
	FVector midpoint = tileMidpoint->GetActorLocation();

	TArray<TTuple<FVector, double>> metaballInfos;
	metaballInfos.Add(TTuple<FVector, double>(midpoint, FMath::Abs(vectorDiagonal.X / 3)));


	for (auto& tileIndice : _tilesRoadEndpointWarzoneSide)
	{
		AMapTile* tile = FindTile(tileIndice.Key, tileIndice.Value);
		FVector tileLocation = tile->GetActorLocation();

		FVector metaballLocation = (_metaballCenterBias * tileLocation + (1 - _metaballCenterBias) * midpoint);
		double metaballSize = FVector::Dist(tileLocation, metaballLocation);

		metaballInfos.Add(
			TTuple<FVector, double>(metaballLocation, metaballSize + _metaballSizeBonus));
	}

	for (auto& tile : _tiles)
	{
		FVector tileLocation = tile->GetActorLocation();

		double d = 0;
		for (auto& info : metaballInfos)
		{
			d += info.Get<1>() / FMath::Pow(FVector::Dist(tileLocation, info.Get<0>()), _gooness);
		}

		if (d >= _metaballThreshold && (tile->GetType() == ETileType::None || tile->GetType() == ETileType::Road))
		{
			tile->SetType(ETileType::WarZone);
		}
	}
}

bool UMapGeneratorComponent::IsTileARoad(int indexX, int indexY)
{
	AMapTile* tile = FindTile(indexX, indexY);
	if (nullptr == tile)
		return true;

	bool result = tile->GetType() == ETileType::Road
		|| tile->GetType() == ETileType::Obstacle
		|| tile->GetType() == ETileType::Spawn
		|| tile->GetType() == ETileType::Exit;

	return result;
}

bool UMapGeneratorComponent::IsTileInCenterArea(int indexX, int indexY)
{
	return indexX > _mapSize / _sectionCoreSize - 1
		&& indexX <= (_sectionCoreSize - 1) * _mapSize / _sectionCoreSize + 1
		&& indexY > _mapSize / _sectionCoreSize - 1
		&& indexY <= (_sectionCoreSize - 1) * _mapSize / _sectionCoreSize + 1;
}

bool UMapGeneratorComponent::IsTileOutOfExpectedArea(int indexX, int indexY, int directionKey)
{
	if (directionKey & EMapDirection::Left && indexX > _mapSize / _sectionCoreSize)
		return true;
	if (directionKey & EMapDirection::Right && indexX < _mapSize * (_sectionCoreSize - 1) / _sectionCoreSize - 1)
		return true;
	if (directionKey & EMapDirection::Up && indexY > _mapSize / _sectionCoreSize)
		return true;
	if (directionKey & EMapDirection::Down && indexY < _mapSize * (_sectionCoreSize - 1) / _sectionCoreSize - 1)
		return true;

	if (!(directionKey & EMapDirection::Left || directionKey & EMapDirection::Right))
		return indexX > _mapSize * (_sectionCoreSize - 1) / _sectionCoreSize - 1 || indexX < _mapSize /
			_sectionCoreSize;
	if (!(directionKey & EMapDirection::Up || directionKey & EMapDirection::Down))
		return indexY > _mapSize * (_sectionCoreSize - 1) / _sectionCoreSize - 1 || indexY < _mapSize /
			_sectionCoreSize;

	return false;
}

bool UMapGeneratorComponent::IsTileInBoundary(int indexX, int indexY)
{
	return indexX < 1 || indexX >= _mapSize - 1
		|| indexY < 1 || indexY >= _mapSize - 1;
}

bool UMapGeneratorComponent::CheckSquareForm(int oldIndexX, int oldIndexY, int newIndexX, int newIndexY)
{
	if (FMath::Abs(oldIndexX - newIndexX) != 1
		|| FMath::Abs(oldIndexY - newIndexY) != 1)
		return false;

	AMapTile* tileDiag = FindTile(oldIndexX, newIndexY);
	AMapTile* tileAntiDiag = FindTile(newIndexX, oldIndexY);

	bool isTileDiagSet = nullptr == tileDiag || tileDiag->GetType() != ETileType::None;
	bool isTileAntiDiagSet = nullptr == tileAntiDiag || tileAntiDiag->GetType() != ETileType::None;

	return isTileDiagSet && isTileAntiDiagSet;
}
