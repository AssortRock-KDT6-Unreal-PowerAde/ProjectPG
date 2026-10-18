#include "Actors/MapBuilder/MapEnemySpawner.h"

#include "Actors/MapBuilder/MapAssetSet.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "TimerManager.h"

namespace MapEnemySpawn
{
	// 지점 담당(MapPointPlanner)은 자리를 땅에서 120cm 띄워 찍는다. 땅 높이를 알려면 이만큼 뺀다.
	constexpr float DesignPointLiftCm = 120.0f;
	// 시작 자리끼리 이 거리(cm) 안이면 같은 시작 구역으로 친다. 한 구역의 자리 4개는 서로 9m 안에 있다.
	constexpr float StartRegionMergeCm = 2000.0f;
	// 길 위 점을 찾을 때 옆·위아래로 얼마나 넓게 볼지. 위아래를 좁게 두어 담 꼭대기 길을 덜 집는다.
	const FVector NavSearchExtent(200.0f, 200.0f, 300.0f);
	// 한 마리 자리를 찾을 때 몇 번까지 옮겨 보나.
	constexpr int32 SpotTries = 6;
	// 몬스터 몸 절반 높이를 BP 에서 못 읽을 때 쓰는 값.
	constexpr float FallbackHalfHeightCm = 100.0f;
}

void UMapEnemySpawner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapEnemySpawner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

void UMapEnemySpawner::SpawnWhenNavigationReady()
{
	// 몬스터는 서버만 만든다. 클라이언트는 복제로 받는다.
	if (GetWorld()->GetNetMode() == NM_Client || bSpawned || bWaiting)
		return;
	if (Map->IsNavigationReady())
	{
		SpawnEnemiesOnce();
		return;
	}
	bWaiting = true;
	NavigationReadyHandle = Map->OnNavigationReady.AddUObject(this, &UMapEnemySpawner::HandleNavigationReady);
	GetWorld()->GetTimerManager().SetTimer(NavigationWaitTimer,
		FTimerDelegate::CreateUObject(this, &UMapEnemySpawner::HandleNavigationWaitTimeout),
		Map->GetMapAssets().EnemyNavigationWaitTimeoutSeconds, false);
	UE_LOG(LogTemp, Display, TEXT("Warzone enemies: waiting for the navigation build before spawning"));
}

void UMapEnemySpawner::HandleNavigationReady()
{
	bWaiting = false;
	Map->OnNavigationReady.Remove(NavigationReadyHandle);
	GetWorld()->GetTimerManager().ClearTimer(NavigationWaitTimer);
	SpawnEnemiesOnce();
}

// 지도가 정한 시간 안에 안 구워졌다. 알림은 그대로 걸어 두고 계속 기다린다(늦게라도 오면 그때 놓음).
void UMapEnemySpawner::HandleNavigationWaitTimeout()
{
	UE_LOG(LogTemp, Error,
		TEXT("Warzone enemies: navigation not ready after %.0fs - no monsters yet, still waiting (check 'Navigation build finished' / RuntimeGeneration=Dynamic)"),
		Map->GetMapAssets().EnemyNavigationWaitTimeoutSeconds);
}

void UMapEnemySpawner::LoadEnemyClasses()
{
	EnemyClasses.Reset();
	for (const TSoftClassPtr<APawn>& Entry : Map->GetMapAssets().EnemyClasses)
	{
		if (UClass* Class = Entry.LoadSynchronous())
			EnemyClasses.Add(Class);
		else
			UE_LOG(LogTemp, Error, TEXT("Warzone enemies: could not load '%s' from DA_MapAssets EnemyClasses - packaged build? check the cook list"),
				*Entry.ToString());
	}
}

void UMapEnemySpawner::SpawnEnemiesOnce()
{
	if (bSpawned)
		return;
	bSpawned = true;

	const UMapAssetSet& Assets = Map->GetMapAssets();
	LoadEnemyClasses();
	if (EnemyClasses.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Warzone enemies: no monster BP in DA_MapAssets EnemyClasses - no monsters placed (run Tools/level/setup_enemy_spawns.py)"));
		return;
	}

	// 판 시드로만 굴린다 → 같은 시드면 같은 자리에 같은 수.
	const int64 Seed = Map->GetRaidSeed();
	FRandomStream Stream(static_cast<int32>(Seed ^ (Seed >> 32)) ^ 0x4E4D5953);
	int32 TooCloseCount = 0;
	const TArray<const FLevelDesignPoint*> Points = PickPoints(Stream, TooCloseCount);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	int32 SpawnedCount = 0;
	int32 UsedPoints = 0;
	int32 NoFloorCount = 0;
	for (const FLevelDesignPoint* Point : Points)
	{
		if (SpawnedCount >= Assets.MaxEnemies)
			break;
		const int32 MaxHere = FMath::Max(1, FMath::Min(Assets.MaxEnemiesPerPoint, Point->Capacity));
		const int32 MinHere = FMath::Clamp(Assets.MinEnemiesPerPoint, 1, MaxHere);
		const int32 Count = FMath::Min(Stream.RandRange(MinHere, MaxHere), Assets.MaxEnemies - SpawnedCount);
		int32 SpawnedHere = 0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FVector Floor;
			if (!FindSpot(*Point, Index, Count, Stream, Floor))
			{
				++NoFloorCount;
				continue;
			}
			UClass* EnemyClass = EnemyClasses[Stream.RandRange(0, EnemyClasses.Num() - 1)];
			// 몸 절반 높이 + 50cm 위에 놓아 땅에 떨어지게 한다.
			// 왜: 길찾기 점은 보이는 땅보다 약 30cm 낮은 투명 길찾기 바닥 위라, 2cm 만 띄우면 발이 땅 속에 박혀
			//     '떨어지는 중' 으로 멈췄다 → 다른 사람 화면에서 공중 자세로 떠 보였다(2026-10-19 서버 기록 7마리).
			const ACharacter* Defaults = Cast<ACharacter>(EnemyClass->GetDefaultObject());
			const float HalfHeight = Defaults && Defaults->GetCapsuleComponent()
				? Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() : MapEnemySpawn::FallbackHalfHeightCm;
			const FRotator Facing(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f);
			APawn* Enemy = GetWorld()->SpawnActor<APawn>(EnemyClass, Floor + FVector(0.0f, 0.0f, HalfHeight + 50.0f), Facing, Params);
			if (!Enemy)
			{
				UE_LOG(LogTemp, Error, TEXT("Warzone enemies: SpawnActor failed for %s at %s"), *GetNameSafe(EnemyClass), *Floor.ToCompactString());
				continue;
			}
#if WITH_EDITOR
			Enemy->SetFolderPath(TEXT("RuntimeDesign/Enemies"));
#endif
			++SpawnedHere;
		}
		if (SpawnedHere > 0)
		{
			SpawnedCount += SpawnedHere;
			++UsedPoints;
			UE_LOG(LogTemp, Display, TEXT("Warzone enemies: %s (%s) +%d at %s"),
				*Point->PointId.ToString(), *Point->ArchetypeId.ToString(), SpawnedHere, *Point->WorldLocation.ToCompactString());
		}
	}

	if (SpawnedCount == 0)
		UE_LOG(LogTemp, Error, TEXT("Warzone enemies: placed 0 monsters (candidate_points=%d too_close_to_start=%d no_floor=%d)"),
			Points.Num(), TooCloseCount, NoFloorCount);
	UE_LOG(LogTemp, Display, TEXT("Warzone enemies spawned: count=%d max=%d points_used=%d candidate_points=%d too_close_to_start=%d no_floor=%d"),
		SpawnedCount, Assets.MaxEnemies, UsedPoints, Points.Num(), TooCloseCount, NoFloorCount);
}

TArray<const FLevelDesignPoint*> UMapEnemySpawner::PickPoints(FRandomStream& Stream, int32& OutTooCloseCount) const
{
	const UMapAssetSet& Assets = Map->GetMapAssets();
	const TArray<FLevelDesignPoint>& AllPoints = Map->GetLevelDesignPoints();

	// 시작 구역 가운데들: 시작 자리를 가까운 것끼리 묶는다.
	TArray<FVector> StartRegions;
	TArray<FVector> StartSpots;
	for (const FLevelDesignPoint& Point : AllPoints)
	{
		if (Point.Type != ELevelDesignPointType::Spawn)
			continue;
		StartSpots.Add(Point.WorldLocation);
		if (!StartRegions.ContainsByPredicate([&Point](const FVector& Region)
			{ return FVector::DistSquared2D(Region, Point.WorldLocation) < FMath::Square(MapEnemySpawn::StartRegionMergeCm); }))
			StartRegions.Add(Point.WorldLocation);
	}
	auto DistanceToNearestStart = [&StartSpots](const FVector& Location)
	{
		float Best = TNumericLimits<float>::Max();
		for (const FVector& Spot : StartSpots)
			Best = FMath::Min(Best, static_cast<float>(FVector::Dist2D(Spot, Location)));
		return Best;
	};

	// 후보: 시작 구역에서 충분히 먼 몬스터 자리.
	TArray<const FLevelDesignPoint*> Candidates;
	OutTooCloseCount = 0;
	for (const FLevelDesignPoint& Point : AllPoints)
	{
		if (Point.Type != ELevelDesignPointType::AISpawn)
			continue;
		if (DistanceToNearestStart(Point.WorldLocation) < Assets.MinDistanceFromPlayerStartCm)
		{
			++OutTooCloseCount;
			continue;
		}
		Candidates.Add(&Point);
	}
	// 중앙에 가까운 자리부터 채운다(워존: 가운데로 갈수록 몬스터가 많다, 2026-10-19 사용자 요청).
	// 맵 중앙 = 시작 자리들의 가운데(시작 구역은 맵 가장자리에 고르게 있다). 거리에 0.75~1.25 를 곱해 판마다 조금씩 섞는다.
	FVector MapCenter = FVector::ZeroVector;
	for (const FVector& Spot : StartSpots)
		MapCenter += Spot / StartSpots.Num();
	TMap<const FLevelDesignPoint*, float> CenterKey;
	for (const FLevelDesignPoint* Point : Candidates)
		CenterKey.Add(Point, FVector::Dist2D(MapCenter, Point->WorldLocation) * Stream.FRandRange(0.75f, 1.25f));
	Candidates.Sort([&CenterKey](const FLevelDesignPoint& A, const FLevelDesignPoint& B) { return CenterKey[&A] < CenterKey[&B]; });

	// 먼저: 시작 구역마다 가까운 자리 몇 곳(DA 칸, 0 이면 안 함). 그다음: 나머지는 중앙에 가까운 순서대로.
	TArray<const FLevelDesignPoint*> Ordered;
	for (const FVector& Region : StartRegions)
	{
		TArray<const FLevelDesignPoint*> Near = Candidates.FilterByPredicate([&Region, &Assets](const FLevelDesignPoint* Point)
			{ return FVector::Dist2D(Region, Point->WorldLocation) <= Assets.NearStartSearchRadiusCm; });
		Near.Sort([&Region](const FLevelDesignPoint& A, const FLevelDesignPoint& B)
			{ return FVector::DistSquared2D(Region, A.WorldLocation) < FVector::DistSquared2D(Region, B.WorldLocation); });
		int32 Taken = 0;
		for (const FLevelDesignPoint* Point : Near)
		{
			if (Taken >= Assets.NearStartPointsPerRegion)
				break;
			if (!Ordered.Contains(Point))
			{
				Ordered.Add(Point);
				++Taken;
			}
		}
	}
	for (const FLevelDesignPoint* Point : Candidates)
		if (!Ordered.Contains(Point))
			Ordered.Add(Point);
	return Ordered;
}

bool UMapEnemySpawner::FindSpot(const FLevelDesignPoint& Point, int32 Index, int32 Count, FRandomStream& Stream, FVector& OutFloor) const
{
	const UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Nav)
		return false;
	const float GroundZ = Point.WorldLocation.Z - MapEnemySpawn::DesignPointLiftCm;
	const float MaxHeightDiff = Map->GetMapAssets().MaxSpawnHeightDiffCm;
	// 같은 자리 몬스터끼리 둘레에 고르게 나눠 선다. 혼자면 가운데부터. 못 찾으면 조금씩 넓혀 다시.
	for (int32 Try = 0; Try < MapEnemySpawn::SpotTries; ++Try)
	{
		const float Yaw = 360.0f * Index / FMath::Max(1, Count) + Stream.FRandRange(-30.0f, 30.0f);
		const float Radius = (Count > 1 || Try > 0) ? Stream.FRandRange(0.4f, 1.0f) * Point.RadiusCm + Try * 150.0f : 0.0f;
		const FVector Wanted = FVector(Point.WorldLocation.X, Point.WorldLocation.Y, GroundZ) + FRotator(0.0f, Yaw, 0.0f).Vector() * Radius;
		FNavLocation OnNav;
		if (Nav->ProjectPointToNavigation(Wanted, OnNav, MapEnemySpawn::NavSearchExtent)
			&& FMath::Abs(OnNav.Location.Z - GroundZ) <= MaxHeightDiff)
		{
			OutFloor = OnNav.Location;
			return true;
		}
	}
	return false;
}
