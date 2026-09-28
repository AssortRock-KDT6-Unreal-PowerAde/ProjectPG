// AWarZoneFootprintPreview — 자체 검사(Verify*) — 매 판 맵이 규칙대로 만들어졌는지 확인하고 로그로 남긴다. 게임 동작은 바꾸지 않는다.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"
#include "PGMapTileSpawner.h"
#include "PGGameplayPointBuilder.h"
#include "PGMapVerifier.h"

void UPGMapVerifier::VerifyPCGDressing()
{
	if (bLoggedPCGDressing || !IsValid(Map->DressingPCGComponent) || Map->DressingPCGComponent->IsGenerating())
		return;

	if (!Map->DressingPCGComponent->bGenerated)
		return;

	bLoggedPCGDressing = true;
	int32 ManagedResourceCount = 0;
	Map->DressingPCGComponent->ForEachConstManagedResource(
		[&ManagedResourceCount](const UPCGManagedResource*)
		{
			++ManagedResourceCount;
		});
	UE_LOG(LogTemp, Display,
		TEXT("PCG dressing generated: generated=true procedural_instances=%s managed_resources=%d"),
		Map->DressingPCGComponent->AreProceduralInstancesInUse() ? TEXT("true") : TEXT("false"),
		ManagedResourceCount);
}

void UPGMapVerifier::VerifyWorldCollision()
{
	if (bLoggedWorldCollision
		|| !Map->TileSpawner->AreAllFacilityLevelsLoaded())
	{
		return;
	}

	bLoggedWorldCollision = true;
	int32 HitCount = 0;
	TArray<FIntPoint> MissingCells;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DesignWorldGroundValidation), true);
	for (int32 Y = -22; Y <= 22; ++Y)
	{
		for (int32 X = -22; X <= 22; ++X)
		{
			const FVector CellCenter(X * DesignCellSize, Y * DesignCellSize, 0.0f);
			FHitResult HitResult;
			const bool bHit = Map->GetWorld()->LineTraceSingleByChannel(
				HitResult,
				CellCenter + FVector(0.0f, 0.0f, 1000.0f),
				CellCenter - FVector(0.0f, 0.0f, 1000.0f),
				ECC_Visibility,
				QueryParams);
			if (bHit)
				++HitCount;
			else
				MissingCells.Add(FIntPoint(X, Y));
		}
	}

	FString MissingSummary;
	for (int32 Index = 0; Index < FMath::Min(MissingCells.Num(), 12); ++Index)
	{
		MissingSummary += FString::Printf(
			TEXT("(%d,%d)%s"),
			MissingCells[Index].X,
			MissingCells[Index].Y,
			Index + 1 < FMath::Min(MissingCells.Num(), 12) ? TEXT(",") : TEXT(""));
	}
	UE_LOG(LogTemp, Display,
		TEXT("Design world collision: cells=2025 hits=%d missing=%d sample=[%s]"),
		HitCount, MissingCells.Num(), *MissingSummary);
}

void UPGMapVerifier::VerifyTacticalLayoutQuality()
{
	int32 ExpectedRuntimeFacilityCount = 0;
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
		if (Placement.VisualSet != EFacilityVisualSet::Checkpoint)
			++ExpectedRuntimeFacilityCount;
	if (bLoggedTacticalLayoutQuality
		|| Map->TileDesignPlacements.IsEmpty()
		|| Map->SpawnedRuntimeTiles.Num() != Map->TileDesignPlacements.Num() + ExpectedRuntimeFacilityCount
		|| !Map->TileSpawner->AreAllFacilityLevelsLoaded())
		return;

	bLoggedTacticalLayoutQuality = true;
	int32 GridMisalignments = 0;
	int32 OutOfBoundsComponents = 0;
	TArray<FString> OverflowSamples;
	int32 InvalidMasks = 0;
	int32 UnsafeAnchorOverlaps = 0;
	TArray<FString> UnsafeAnchorSamples;
	TSet<FIntPoint> OccupiedCells;
	for (AActor* Actor : Map->SpawnedRuntimeTiles)
	{
		if (!IsValid(Actor) || Actor->ActorHasTag(TEXT("RuntimeTacticalFacility")))
			continue;

		const FVector Location = Actor->GetActorLocation();
		const FIntPoint Cell(
			FMath::RoundToInt(Location.X / DesignCellSize),
			FMath::RoundToInt(Location.Y / DesignCellSize));
		if (!FMath::IsNearlyEqual(Location.X, Cell.X * DesignCellSize, 1.0f)
			|| !FMath::IsNearlyEqual(Location.Y, Cell.Y * DesignCellSize, 1.0f)
			|| OccupiedCells.Contains(Cell))
		{
			++GridMisalignments;
		}
		OccupiedCells.Add(Cell);

		FVector Origin;
		FVector Extent;
		Actor->GetActorBounds(false, Origin, Extent, true);
		// Ignore editor-only connection arrows and tall debug vectors. Validate only
		// colliding primitive bounds because those are what can overlap neighbors.
		TArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
		bool bActorOverflow = false;
		for (const UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive)
				|| Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision
				|| !Primitive->IsVisible())
			{
				continue;
			}
			const FString ComponentName = Primitive->GetName();
			if (ComponentName.Contains(TEXT("Ground"))
				|| ComponentName.Contains(TEXT("Road"))
				|| ComponentName.Contains(TEXT("Roof"))
				|| ComponentName.Contains(TEXT("Grass"))
				|| ComponentName.Contains(TEXT("Tree"))
				|| ComponentName.Contains(TEXT("Bush"))
				|| ComponentName.Contains(TEXT("Rock"))
				|| ComponentName.Contains(TEXT("Marking")))
			{
				continue;
			}
			const FBoxSphereBounds Bounds = Primitive->Bounds;
			// Seam dressing and angled CQB cover may overhang the nominal 10 m
			// half-cell slightly. Industrial props are intentionally used as visual
			// bridges between adjacent WarZone cells, so they receive a larger but
			// still bounded allowance. Structural walls retain the stricter limit.
			const bool bIndustrialSeamProp = ComponentName.Contains(TEXT("Container"))
				|| ComponentName.Contains(TEXT("Crane"))
				|| ComponentName.Contains(TEXT("Tank"))
				|| ComponentName.Contains(TEXT("Pallet"))
				|| ComponentName.Contains(TEXT("Fence"))
				|| ComponentName.Contains(TEXT("PipeRack"));
			const float MaxComponentReach = bIndustrialSeamProp ? 1800.0f : 1400.0f;
			if (FMath::Abs(Bounds.Origin.X - Location.X) + Bounds.BoxExtent.X > MaxComponentReach
				|| FMath::Abs(Bounds.Origin.Y - Location.Y) + Bounds.BoxExtent.Y > MaxComponentReach)
			{
				bActorOverflow = true;
				break;
			}
		}
		if (bActorOverflow)
		{
			++OutOfBoundsComponents;
			if (OverflowSamples.Num() < 12)
				OverflowSamples.Add(Actor->GetActorNameOrLabel());
		}

		if (const ATacticalTileActor* TacticalTile = Cast<ATacticalTileActor>(Actor))
		{
			if (TacticalTile->GetEffectiveConnectionMask() > 15
				|| TacticalTile->GetEffectiveLayoutVariant() > 3)
			{
				++InvalidMasks;
			}
		}
	}

	FCollisionQueryParams AnchorQuery(SCENE_QUERY_STAT(TacticalAnchorSafety), false);
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Spawn
			&& Point.Type != ELevelDesignPointType::Exit
			&& Point.Type != ELevelDesignPointType::Loot
			&& Point.Type != ELevelDesignPointType::AISpawn
			&& Point.Type != ELevelDesignPointType::Quest)
		{
			continue;
		}
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(55.0f, 95.0f);
		TArray<FOverlapResult> AnchorOverlaps;
		const bool bHasOverlap = Map->GetWorld()->OverlapMultiByChannel(
			AnchorOverlaps,
			Point.WorldLocation + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			Capsule,
			AnchorQuery);
		const bool bBlockedByTacticalGeometry = bHasOverlap && AnchorOverlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != Map.Get()
					&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
					&& !(Map->bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
					&& IsValid(HitComponent)
					&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
			});
		if (bBlockedByTacticalGeometry)
		{
			++UnsafeAnchorOverlaps;
			if (UnsafeAnchorSamples.Num() < 8)
			{
				const FOverlapResult* Blocking = AnchorOverlaps.FindByPredicate(
					[this](const FOverlapResult& Result)
					{
						const AActor* HitActor = Result.GetActor();
						const UPrimitiveComponent* HitComponent = Result.GetComponent();
						return IsValid(HitActor) && HitActor != Map.Get()
							&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
							&& !(Map->bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
							&& IsValid(HitComponent)
							&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
					});
				UnsafeAnchorSamples.Add(FString::Printf(TEXT("%s:%s/%s"),
					*Point.PointId.ToString(),
					Blocking && IsValid(Blocking->GetActor()) ? *Blocking->GetActor()->GetActorNameOrLabel() : TEXT("UnknownActor"),
					Blocking && IsValid(Blocking->GetComponent()) ? *Blocking->GetComponent()->GetName() : TEXT("UnknownComponent")));
			}
		}
	}
	const FString UnsafeSummary = FString::Join(UnsafeAnchorSamples, TEXT(","));

	UE_LOG(LogTemp, Display,
		TEXT("Tactical layout quality: single_cell_tiles=%d facilities=%d grid_or_duplicate=%d bounds_overflow=%d invalid_specs=%d unsafe_anchors=%d pass=%s overflow_sample=[%s] unsafe_sample=[%s]"),
		Map->TileDesignPlacements.Num(),
		ExpectedRuntimeFacilityCount,
		GridMisalignments,
		OutOfBoundsComponents,
		InvalidMasks,
		UnsafeAnchorOverlaps,
		GridMisalignments == 0 && OutOfBoundsComponents == 0 && InvalidMasks == 0 && UnsafeAnchorOverlaps == 0
			? TEXT("true") : TEXT("false"),
		*FString::Join(OverflowSamples, TEXT(",")),
		*UnsafeSummary);
}

void UPGMapVerifier::VerifyTravelCoverDensity()
{
	int32 ExpectedRuntimeFacilityCount = 0;
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
		if (Placement.VisualSet != EFacilityVisualSet::Checkpoint)
			++ExpectedRuntimeFacilityCount;
	if (bLoggedTravelCoverDensity
		|| Map->TileDesignPlacements.IsEmpty()
		|| Map->SpawnedRuntimeTiles.Num() != Map->TileDesignPlacements.Num() + ExpectedRuntimeFacilityCount)
		return;

	bLoggedTravelCoverDensity = true;
	int32 SampleCount = 0;
	int32 FullyExposedSamples = 0;
	float LongestExposedRunCm = 0.0f;
	for (int32 Y = -22; Y <= 22; Y += 2)
	{
		float CurrentRunCm = 0.0f;
		for (int32 X = -22; X <= 22; ++X)
		{
			++SampleCount;
			// Cover is useful when it protects a crouched player; the previous 1.4m
			// standing-eye trace incorrectly rejected deliberate chest-high cover.
			const FVector EyeLocation(X * DesignCellSize, Y * DesignCellSize, 90.0f);
			bool bHasNearbyCover = false;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(TravelCoverDensity), true);
			for (int32 DirectionIndex = 0; DirectionIndex < 8; ++DirectionIndex)
			{
				const float Angle = FMath::DegreesToRadians(DirectionIndex * 45.0f);
				const FVector Direction(FMath::Cos(Angle) * 900.0f, FMath::Sin(Angle) * 900.0f, 0.0f);
				FHitResult Hit;
				if (Map->GetWorld()->LineTraceSingleByChannel(Hit, EyeLocation, EyeLocation + Direction, ECC_Visibility, Query))
				{
					const UPrimitiveComponent* Component = Hit.GetComponent();
					const FVector Normal = Hit.ImpactNormal;
					// Ground is a horizontal hit; meaningful cover has a lateral face.
					if (IsValid(Component) && FMath::Abs(Normal.Z) < 0.55f)
					{
						bHasNearbyCover = true;
						break;
					}
				}
			}

			if (bHasNearbyCover)
			{
				CurrentRunCm = 0.0f;
			}
			else
			{
				++FullyExposedSamples;
				CurrentRunCm += DesignCellSize;
				LongestExposedRunCm = FMath::Max(LongestExposedRunCm, CurrentRunCm);
			}
		}
	}

	const float ExposedRatio = SampleCount > 0
		? static_cast<float>(FullyExposedSamples) / SampleCount
		: 1.0f;
	UE_LOG(LogTemp, Display,
		TEXT("Travel cover audit (crouch_height=90cm): samples=%d fully_exposed=%d exposed_ratio=%.3f longest_exposed_run_m=%.1f target_ratio<=0.55 target_run<=120m pass=%s"),
		SampleCount,
		FullyExposedSamples,
		ExposedRatio,
		LongestExposedRunCm / 100.0f,
		ExposedRatio <= 0.55f && LongestExposedRunCm <= 12000.0f ? TEXT("true") : TEXT("false"));
}

void UPGMapVerifier::VerifyGameplayPointDistribution()
{
	if (bLoggedGameplayPointDistribution || Map->LevelDesignPoints.IsEmpty() || !Map->bResolvedGameplayPointSafety)
		return;

	bLoggedGameplayPointDistribution = true;
	TMap<ELevelDesignPointType, int32> Counts;
	TSet<FName> UniqueIds;
	int32 InvalidMetadata = 0;
	TArray<const FLevelDesignPoint*> SpawnPoints;
	TArray<const FLevelDesignPoint*> LootPoints;
	TArray<const FLevelDesignPoint*> AIPoints;
	auto IsProtectedFromAI = [this](const FIntPoint& Cell)
	{
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		{
			const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
				+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
			if ((Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
				|| (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2))
				return true;
		}
		// Mirror of the point-building lambda: the boat-landing hamlet is a spawn
		// area even though no Spawn tile marks it. Both copies must agree or the
		// verifier fails layouts the builder considers legal.
		if (Map->bHasBorderLake)
		{
			for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
			{
				if (Facility.VisualSet != EFacilityVisualSet::RuralHideout)
					continue;
				for (const FIntPoint& Occupied : Facility.OccupiedCells)
					if (FMath::Abs(Cell.X - Occupied.X) + FMath::Abs(Cell.Y - Occupied.Y) < 3)
						return true;
			}
		}
		return false;
	};
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		Counts.FindOrAdd(Point.Type)++;
		if (Point.PointId.IsNone() || UniqueIds.Contains(Point.PointId)
			|| Point.ArchetypeId.IsNone() || Point.Tier < 1 || Point.Tier > 3
			|| Point.RadiusCm < 50.0f || Point.Capacity < 1)
		{
			++InvalidMetadata;
		}
		UniqueIds.Add(Point.PointId);
		if (Point.Type == ELevelDesignPointType::Spawn) SpawnPoints.Add(&Point);
		else if (Point.Type == ELevelDesignPointType::Loot) LootPoints.Add(&Point);
		else if (Point.Type == ELevelDesignPointType::AISpawn) AIPoints.Add(&Point);
	}

	float MinimumSpawnAIDistanceCm = BIG_NUMBER;
	for (const FLevelDesignPoint* Spawn : SpawnPoints)
		for (const FLevelDesignPoint* AI : AIPoints)
			MinimumSpawnAIDistanceCm = FMath::Min(
				MinimumSpawnAIDistanceCm,
				FVector::Dist2D(Spawn->WorldLocation, AI->WorldLocation));
	float MinimumSpawnSeparationCm = BIG_NUMBER;
	for (int32 A = 0; A < SpawnPoints.Num(); ++A)
		for (int32 B = A + 1; B < SpawnPoints.Num(); ++B)
			MinimumSpawnSeparationCm = FMath::Min(
				MinimumSpawnSeparationCm,
				FVector::Dist2D(SpawnPoints[A]->WorldLocation, SpawnPoints[B]->WorldLocation));

	// Sample the playable cell field every 100m. A loot-shooter can contain open
	// traversal space, but no sampled region should be excessively far from both
	// a loot opportunity and a possible encounter.
	float MaximumLootGapCm = 0.0f;
	float MaximumAIGapCm = 0.0f;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		if (FMath::Abs(Placement.GridCell.X) % 5 != 0
			|| FMath::Abs(Placement.GridCell.Y) % 5 != 0)
			continue;
		float NearestLoot = BIG_NUMBER;
		for (const FLevelDesignPoint* Loot : LootPoints)
			NearestLoot = FMath::Min(NearestLoot, FVector::Dist2D(Placement.WorldLocation, Loot->WorldLocation));
		float NearestAI = BIG_NUMBER;
		for (const FLevelDesignPoint* AI : AIPoints)
			NearestAI = FMath::Min(NearestAI, FVector::Dist2D(Placement.WorldLocation, AI->WorldLocation));
		MaximumLootGapCm = FMath::Max(MaximumLootGapCm, NearestLoot);
		MaximumAIGapCm = FMath::Max(MaximumAIGapCm, NearestAI);
	}

	int32 FacilitiesMissingLoot = 0;
	int32 FacilitiesMissingAI = 0;
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		const bool bHasLoot = LootPoints.ContainsByPredicate(
			[&Facility](const FLevelDesignPoint* Point)
			{
				return Facility.OccupiedCells.Contains(Point->GridCell);
			});
		const bool bHasAI = AIPoints.ContainsByPredicate(
			[&Facility](const FLevelDesignPoint* Point)
			{
				return Facility.OccupiedCells.Contains(Point->GridCell);
			});
		if (!bHasLoot) ++FacilitiesMissingLoot;
		// A facility inside the spawn safe band intentionally has no resident AI.
		if (!bHasAI && !Facility.OccupiedCells.ContainsByPredicate(IsProtectedFromAI))
			++FacilitiesMissingAI;
	}

	const bool bPass = Counts.FindRef(ELevelDesignPointType::Spawn) >= 4
		&& Counts.FindRef(ELevelDesignPointType::Loot) >= 40
		&& Counts.FindRef(ELevelDesignPointType::AISpawn) >= 24
		&& Counts.FindRef(ELevelDesignPointType::Exit) >= 2
		&& Counts.FindRef(ELevelDesignPointType::Quest) >= 1
		&& InvalidMetadata == 0
		&& MinimumSpawnSeparationCm >= 250.0f
		&& MinimumSpawnAIDistanceCm >= 5000.0f
		&& MaximumLootGapCm <= 25000.0f
		&& MaximumAIGapCm <= 30000.0f
		&& FacilitiesMissingLoot == 0
		&& FacilitiesMissingAI == 0;
	UE_LOG(LogTemp, Display,
		TEXT("Gameplay point distribution: spawn=%d loot=%d ai=%d exit=%d quest=%d invalid=%d min_spawn_spacing_m=%.1f min_spawn_ai_m=%.1f max_loot_gap_m=%.1f max_ai_gap_m=%.1f facilities_missing_loot=%d facilities_missing_ai=%d point_hash=%08X pass=%s"),
		Counts.FindRef(ELevelDesignPointType::Spawn),
		Counts.FindRef(ELevelDesignPointType::Loot),
		Counts.FindRef(ELevelDesignPointType::AISpawn),
		Counts.FindRef(ELevelDesignPointType::Exit),
		Counts.FindRef(ELevelDesignPointType::Quest),
		InvalidMetadata,
		MinimumSpawnSeparationCm / 100.0f,
		MinimumSpawnAIDistanceCm / 100.0f,
		MaximumLootGapCm / 100.0f,
		MaximumAIGapCm / 100.0f,
		FacilitiesMissingLoot,
		FacilitiesMissingAI,
		Map->GameplayPointHash,
		bPass ? TEXT("true") : TEXT("false"));
}

void UPGMapVerifier::VerifyNavigation()
{
	if (Map->bLoggedNavigation || !bLoggedWorldCollision || Map->LevelDesignPoints.IsEmpty())
		return;

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Map->GetWorld());
	if (!IsValid(NavigationSystem))
		return;

	const double ElapsedSeconds = FPlatformTime::Seconds() - Map->NavigationValidationStartTimeSeconds;
	if (NavigationSystem->IsNavigationBuildInProgress() && ElapsedSeconds < 20.0)
		return;

	int32 CandidateCount = 0;
	int32 ProjectedCount = 0;
	TArray<FName> FailedPointIds;
	const FVector QueryExtent(180.0f, 180.0f, 650.0f);
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		// Navigation is generated only around the central invoker. Points outside
		// its 120 m generation radius are validated later when a player/AI invoker
		// approaches them.
		if (FVector::DistSquared2D(Point.WorldLocation, Map->GetActorLocation()) > FMath::Square(10000.0f))
			continue;

		++CandidateCount;
		FNavLocation ProjectedLocation;
		if (NavigationSystem->ProjectPointToNavigation(Point.WorldLocation, ProjectedLocation, QueryExtent))
		{
			++ProjectedCount;
			// Projection alone can succeed on an isolated polygon. Accept a movement
			// pocket in any cardinal direction; the old +X-only check falsely rejected
			// valid narrow rooms and rotated upper decks.
			const FVector NeighborOffsets[] = {
				FVector(350.0f, 0.0f, 0.0f), FVector(-350.0f, 0.0f, 0.0f),
				FVector(0.0f, 350.0f, 0.0f), FVector(0.0f, -350.0f, 0.0f)
			};
			const bool bHasMovementPocket = Algo::AnyOf(
				NeighborOffsets,
				[NavigationSystem, &ProjectedLocation, &QueryExtent](const FVector& Offset)
				{
					FNavLocation NeighborLocation;
					return NavigationSystem->ProjectPointToNavigation(
						ProjectedLocation.Location + Offset,
						NeighborLocation,
						QueryExtent);
				});
			if (!bHasMovementPocket)
			{
				--ProjectedCount;
				FailedPointIds.Add(Point.PointId);
			}
		}
		else
			FailedPointIds.Add(Point.PointId);
	}

	// Wait a little longer when the dynamic Recast generator has not exposed
	// any polygon yet, then report a deterministic pass/fail result.
	if (ProjectedCount == 0 && ElapsedSeconds < 20.0)
		return;

	Map->bLoggedNavigation = true;
	FString FailedSummary;
	for (int32 Index = 0; Index < FMath::Min(FailedPointIds.Num(), 8); ++Index)
	{
		FailedSummary += FString::Printf(
			TEXT("%s%s"),
			*FailedPointIds[Index].ToString(),
			Index + 1 < FMath::Min(FailedPointIds.Num(), 8) ? TEXT(",") : TEXT(""));
	}

	UE_LOG(LogTemp, Display,
		TEXT("Design navigation: invoker_radius_cm=12000 candidates=%d projected=%d failed=%d build_pending=%s elapsed_ms=%.2f sample=[%s]"),
		CandidateCount,
		ProjectedCount,
		FailedPointIds.Num(),
		NavigationSystem->IsNavigationBuildInProgress() ? TEXT("true") : TEXT("false"),
		ElapsedSeconds * 1000.0,
		*FailedSummary);

	if (FailedPointIds.IsEmpty())
		Map->StartSinglePlayerValidation();
}

void UPGMapVerifier::VerifyCriticalRoutes()
{
	if (bLoggedCriticalRoutes || !Map->bLoggedNavigation || Map->TileDesignPlacements.IsEmpty())
		return;

	const FLevelDesignPoint* Spawn = Map->LevelDesignPoints.FindByPredicate(
		[](const FLevelDesignPoint& Point) { return Point.Type == ELevelDesignPointType::Spawn; });
	if (Spawn == nullptr)
		return;

	// Whole-raid reachability is a logical graph question. Runtime Recast is
	// generated only around invokers, so attempting one 900m nav query reports
	// false failures. BFS proves the generated walkable cell field is connected;
	// local Recast and AI movement are verified separately below.
	TSet<FIntPoint> WalkableCells;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		if (Placement.Visual == ETileDesignVisual::Water)
			continue;
		WalkableCells.Add(Placement.GridCell);
	}
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
		for (const FIntPoint& Cell : Facility.OccupiedCells)
			WalkableCells.Add(Cell);

	TSet<FIntPoint> Visited;
	TQueue<FIntPoint> Queue;
	Queue.Enqueue(Spawn->GridCell);
	Visited.Add(Spawn->GridCell);
	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
	FIntPoint Cell;
	while (Queue.Dequeue(Cell))
	{
		for (const FIntPoint& Direction : Directions)
		{
			const FIntPoint Neighbor = Cell + Direction;
			if (WalkableCells.Contains(Neighbor) && !Visited.Contains(Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	int32 TargetCount = 0;
	int32 ReachableTargets = 0;
	TArray<FString> FailedRoutes;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Exit)
			continue;
		++TargetCount;
		if (Visited.Contains(Point.GridCell)) ++ReachableTargets;
		else FailedRoutes.Add(Point.PointId.ToString());
	}
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		++TargetCount;
		const bool bReached = Facility.OccupiedCells.ContainsByPredicate(
			[&Visited](const FIntPoint& FacilityCell) { return Visited.Contains(FacilityCell); });
		if (bReached) ++ReachableTargets;
		else FailedRoutes.Add(FString::Printf(TEXT("Facility_%d_%d"), Facility.AnchorCell.X, Facility.AnchorCell.Y));
	}

	bLoggedCriticalRoutes = true;
	UE_LOG(LogTemp, Display,
		TEXT("Critical logical route audit: from=%s visited_cells=%d/%d targets=%d reachable=%d failed=%d pass=%s sample=[%s]"),
		*Spawn->PointId.ToString(), Visited.Num(), WalkableCells.Num(), TargetCount, ReachableTargets,
		TargetCount - ReachableTargets,
		TargetCount == ReachableTargets ? TEXT("true") : TEXT("false"),
		*FString::Join(FailedRoutes, TEXT(",")));
}

void UPGMapVerifier::VerifyTraversableElevation()
{
	if (bLoggedTraversableElevation || Map->TileDesignPlacements.IsEmpty())
		return;

	const FLevelDesignPoint* Spawn = Map->LevelDesignPoints.FindByPredicate(
		[](const FLevelDesignPoint& Point) { return Point.Type == ELevelDesignPointType::Spawn; });
	if (Spawn == nullptr)
		return;

	bLoggedTraversableElevation = true;

	// VerifyCriticalRoutes answers "is the cell field connected"; it says nothing
	// about height, so a route it calls reachable can still be walled off by a lip
	// the character cannot step over. This audit walks the same field with the
	// authored surface heights applied.
	TMap<FIntPoint, float> SurfaceByCell;
	SurfaceByCell.Reserve(Map->TileDesignPlacements.Num());
	int32 WaterCellCount = 0;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		// Lake cells are deliberately unreachable - the bank is the Maze barrier the
		// design calls for. Counting them as walkable would report every shoreline as
		// an unclimbable lip and mark the whole map unreachable.
		if (Placement.Visual == ETileDesignVisual::Water)
		{
			++WaterCellCount;
			continue;
		}
		SurfaceByCell.Add(Placement.GridCell, Map->GetSurfaceElevationForCell(Placement.GridCell));
	}
	TSet<FIntPoint> FacilityCells;
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		for (const FIntPoint& Cell : Facility.OccupiedCells)
		{
			FacilityCells.Add(Cell);
			SurfaceByCell.Add(Cell, Map->GetSurfaceElevationForCell(Cell));
		}
	}

	// Each vehicle ramp and infantry stair bridges exactly one cell edge: an entrance
	// cell inside the footprint and the cell just outside it. Every other pad edge is
	// a deliberate wall. Read the access points from the same helper the terrain
	// builder uses so the audit cannot drift out of step with what was built.
	TArray<TPair<FIntPoint, FIntPoint>> RampBridgedEdges;
	TArray<TPair<FIntPoint, FIntPoint>> AccessEdges;
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		Map->GetFacilityAccessEdges(Facility, AccessEdges);
		for (const TPair<FIntPoint, FIntPoint>& AccessEdge : AccessEdges)
		{
			const FIntPoint OutsideCell = AccessEdge.Key + AccessEdge.Value;
			RampBridgedEdges.Emplace(AccessEdge.Key, OutsideCell);
			RampBridgedEdges.Emplace(OutsideCell, AccessEdge.Key);
		}
	}

	const FIntPoint Directions[] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
	auto IsTraversableEdge = [&SurfaceByCell, &RampBridgedEdges](
		const FIntPoint& From, const FIntPoint& To)
	{
		const float* FromZ = SurfaceByCell.Find(From);
		const float* ToZ = SurfaceByCell.Find(To);
		if (FromZ == nullptr || ToZ == nullptr)
			return false;
		if (FMath::Abs(*ToZ - *FromZ) <= MaxTraversableStepCm)
			return true;
		return RampBridgedEdges.Contains(TPair<FIntPoint, FIntPoint>(From, To));
	};

	// Census of unclimbable risers, split by whether they belong to a facility pad
	// (intended, and ramped) or sit out on open ground (always a defect).
	int32 OpenGroundHardEdges = 0;
	int32 FacilityWallEdges = 0;
	float WorstOpenGroundStepCm = 0.0f;
	FIntPoint WorstOpenGroundCell = FIntPoint::ZeroValue;
	for (const TPair<FIntPoint, float>& Entry : SurfaceByCell)
	{
		for (const FIntPoint& Direction : Directions)
		{
			const float* NeighborZ = SurfaceByCell.Find(Entry.Key + Direction);
			if (NeighborZ == nullptr)
				continue;
			const float StepCm = FMath::Abs(*NeighborZ - Entry.Value);
			if (StepCm <= MaxTraversableStepCm)
				continue;
			if (FacilityCells.Contains(Entry.Key) || FacilityCells.Contains(Entry.Key + Direction))
			{
				++FacilityWallEdges;
				continue;
			}
			++OpenGroundHardEdges;
			if (StepCm > WorstOpenGroundStepCm)
			{
				WorstOpenGroundStepCm = StepCm;
				WorstOpenGroundCell = Entry.Key;
			}
		}
	}

	TSet<FIntPoint> Visited;
	TQueue<FIntPoint> Queue;
	Queue.Enqueue(Spawn->GridCell);
	Visited.Add(Spawn->GridCell);
	FIntPoint Cell;
	while (Queue.Dequeue(Cell))
	{
		for (const FIntPoint& Direction : Directions)
		{
			const FIntPoint Neighbor = Cell + Direction;
			if (!Visited.Contains(Neighbor) && IsTraversableEdge(Cell, Neighbor))
			{
				Visited.Add(Neighbor);
				Queue.Enqueue(Neighbor);
			}
		}
	}

	// The WarZone core is the raid's headline destination, so it is reported by name
	// rather than folded into the facility tally.
	FIntPoint WarZoneCoreCell = FIntPoint::ZeroValue;
	bool bHasWarZoneCore = false;
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		if (Facility.VisualSet == EFacilityVisualSet::Warehouse
			&& Facility.Footprint == WarZoneCoreFootprint)
		{
			WarZoneCoreCell = Facility.AnchorCell + WarZoneCoreCentreOffset;
			bHasWarZoneCore = true;
			break;
		}
	}
	const bool bWarZoneCoreWalkable = bHasWarZoneCore && Visited.Contains(WarZoneCoreCell);

	int32 TargetCount = 0;
	int32 ReachableTargets = 0;
	TArray<FString> FailedTargets;
	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Exit)
			continue;
		++TargetCount;
		if (Visited.Contains(Point.GridCell)) ++ReachableTargets;
		else FailedTargets.Add(Point.PointId.ToString());
	}
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		++TargetCount;
		const bool bReached = Facility.OccupiedCells.ContainsByPredicate(
			[&Visited](const FIntPoint& FacilityCell) { return Visited.Contains(FacilityCell); });
		if (bReached) ++ReachableTargets;
		else FailedTargets.Add(FString::Printf(TEXT("Facility_%s_%d_%d"),
			*Facility.FacilityId.ToString(), Facility.AnchorCell.X, Facility.AnchorCell.Y));
	}

	const bool bPass = OpenGroundHardEdges == 0
		&& (!bHasWarZoneCore || bWarZoneCoreWalkable)
		&& TargetCount == ReachableTargets;
	UE_LOG(LogTemp, Display,
		TEXT("Ground step continuity: max_step_cm=%.0f water_cells_excluded=%d cells=%d open_ground_hard_edges=%d ")
		TEXT("worst_open_step_cm=%.0f worst_open_cell=(%d,%d) facility_wall_edges=%d ")
		TEXT("step_aware_reachable=%d/%d warzone_core=%s targets=%d failures=%d pass=%s sample=[%s]"),
		MaxTraversableStepCm,
		WaterCellCount,
		SurfaceByCell.Num(),
		OpenGroundHardEdges,
		WorstOpenGroundStepCm,
		WorstOpenGroundCell.X, WorstOpenGroundCell.Y,
		FacilityWallEdges,
		Visited.Num(), SurfaceByCell.Num(),
		bHasWarZoneCore ? (bWarZoneCoreWalkable ? TEXT("reachable") : TEXT("blocked")) : TEXT("absent"),
		TargetCount, TargetCount - ReachableTargets,
		bPass ? TEXT("true") : TEXT("false"),
		*FString::Join(FailedTargets, TEXT(",")));
}

void UPGMapVerifier::VerifyCoplanarSurfaces()
{
	if (bLoggedCoplanarSurfaces || !Map->bRunCoplanarSurfaceAudit || Map->TileDesignPlacements.IsEmpty())
		return;

	bLoggedCoplanarSurfaces = true;

	// Z-fighting is two faces landing on the same depth, and the runtime world is
	// assembled from several independent sources - shared ground slabs, the road
	// HISM, facility pads, packed tile geometry and terrain features. No single
	// builder can see the others, so the only way to catch a coplanar pair is to
	// inspect the finished world.
	//
	// An earlier version traced 123,000 rays downward and reported a confident zero.
	// That was worthless: a ray only stops on collision, and packed tile visuals
	// inherit whatever collision their source mesh had. A render-only marking or
	// slab lets every ray straight through while still fighting on screen. Compare
	// instance bounds instead, which is both collision-agnostic and far cheaper.
	constexpr float CoplanarToleranceCm = 5.0f;
	// 0.25 m^2. Neighbouring ground slabs are authored edge to edge, so they share a
	// boundary line but no area; only a real shared area puts two surfaces in the
	// same screen pixels.
	constexpr float MinimumSharedAreaCm2 = 2500.0f;
	constexpr int32 MaxTrackedSurfaces = 60000;
	// Depth precision is the whole reason this artifact exists: a 2 cm separation
	// reads as solid up close and collapses into a shimmer far away. Anything the
	// renderer culls before that distance therefore cannot be the cause of a flicker
	// seen across the map. Grass patches cull out at 80 m and overlap each other by
	// the tens of thousands, which was enough to blow through MaxTrackedSurfaces and
	// hide every long-range pair behind noise. Audit only what stays drawn.
	constexpr float MinAuditDrawDistanceCm = 15000.0f;

	struct FFlatSurface
	{
		FVector2D Min = FVector2D::ZeroVector;
		FVector2D Max = FVector2D::ZeroVector;
		float TopZ = 0.0f;
		FString Label;
	};

	TMap<FIntPoint, TArray<FFlatSurface>> SurfacesByCell;
	int32 InspectedInstanceCount = 0;
	int32 FlatSurfaceCount = 0;
	int32 SkippedNearFieldComponents = 0;
	bool bTruncated = false;

	auto ConsiderBounds = [&SurfacesByCell, &FlatSurfaceCount, &bTruncated](
		const FBox& WorldBounds, const FString& Label)
	{
		if (!WorldBounds.IsValid || bTruncated)
			return;
		const FVector Size = WorldBounds.GetSize();
		const float MinHorizontal = FMath::Min(Size.X, Size.Y);
		// Plate-like geometry only. A wall or a shipping container has a top face
		// too, but it is not a surface another surface can fight with in a way the
		// player sees; including them would bury the real hits in noise.
		if (MinHorizontal < 100.0f || Size.Z > MinHorizontal * 0.5f)
			return;
		if (++FlatSurfaceCount > MaxTrackedSurfaces)
		{
			bTruncated = true;
			return;
		}

		FFlatSurface Surface;
		Surface.Min = FVector2D(WorldBounds.Min.X, WorldBounds.Min.Y);
		Surface.Max = FVector2D(WorldBounds.Max.X, WorldBounds.Max.Y);
		Surface.TopZ = WorldBounds.Max.Z;
		Surface.Label = Label;
		const FVector Center = WorldBounds.GetCenter();
		const FIntPoint Cell(
			FMath::RoundToInt(Center.X / DesignCellSize),
			FMath::RoundToInt(Center.Y / DesignCellSize));
		SurfacesByCell.FindOrAdd(Cell).Add(MoveTemp(Surface));
	};

	for (TActorIterator<AActor> ActorIt(Map->GetWorld()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!IsValid(Actor) || Actor->IsHidden())
			continue;

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		for (UStaticMeshComponent* MeshComponent : MeshComponents)
		{
			if (!IsValid(MeshComponent) || !MeshComponent->IsVisible())
				continue;
			const UStaticMesh* Mesh = MeshComponent->GetStaticMesh();
			if (!IsValid(Mesh))
				continue;

			float EndDrawDistanceCm = MeshComponent->CachedMaxDrawDistance;
			if (const UInstancedStaticMeshComponent* CullSource =
				Cast<UInstancedStaticMeshComponent>(MeshComponent))
			{
				int32 StartCullDistance = 0;
				int32 EndCullDistance = 0;
				CullSource->GetCullDistances(StartCullDistance, EndCullDistance);
				if (EndCullDistance > 0)
					EndDrawDistanceCm = static_cast<float>(EndCullDistance);
			}
			if (EndDrawDistanceCm > 0.0f && EndDrawDistanceCm < MinAuditDrawDistanceCm)
			{
				++SkippedNearFieldComponents;
				continue;
			}

			const FBox LocalBounds = Mesh->GetBoundingBox();
			const FString Label = FString::Printf(TEXT("%s/%s"),
				*MeshComponent->GetName(), *Mesh->GetName());
			if (UInstancedStaticMeshComponent* Instances =
				Cast<UInstancedStaticMeshComponent>(MeshComponent))
			{
				const int32 InstanceCount = Instances->GetInstanceCount();
				for (int32 InstanceIndex = 0; InstanceIndex < InstanceCount; ++InstanceIndex)
				{
					FTransform InstanceTransform;
					if (!Instances->GetInstanceTransform(InstanceIndex, InstanceTransform, true))
						continue;
					++InspectedInstanceCount;
					ConsiderBounds(LocalBounds.TransformBy(InstanceTransform), Label);
				}
			}
			else
			{
				++InspectedInstanceCount;
				ConsiderBounds(
					LocalBounds.TransformBy(MeshComponent->GetComponentTransform()), Label);
			}
		}
	}

	// A slab can straddle a cell boundary, so each cell is compared against itself
	// and the three neighbours on its positive side. That covers every adjacent pair
	// exactly once instead of finding each one twice from both directions.
	const FIntPoint CompareOffsets[] = {
		FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(1, 1)
	};
	int32 CoplanarPairCount = 0;
	float TightestGapCm = CoplanarToleranceCm;
	TArray<FString> Samples;
	TMap<FString, int32> PairCounts;
	for (const TPair<FIntPoint, TArray<FFlatSurface>>& CellEntry : SurfacesByCell)
	{
		for (const FIntPoint& Offset : CompareOffsets)
		{
			const TArray<FFlatSurface>* Neighbours = SurfacesByCell.Find(CellEntry.Key + Offset);
			if (Neighbours == nullptr)
				continue;
			const bool bSameCell = Offset == FIntPoint::ZeroValue;
			for (int32 LeftIndex = 0; LeftIndex < CellEntry.Value.Num(); ++LeftIndex)
			{
				const FFlatSurface& Left = CellEntry.Value[LeftIndex];
				const int32 FirstRight = bSameCell ? LeftIndex + 1 : 0;
				for (int32 RightIndex = FirstRight; RightIndex < Neighbours->Num(); ++RightIndex)
				{
					const FFlatSurface& Right = (*Neighbours)[RightIndex];
					const float GapCm = FMath::Abs(Left.TopZ - Right.TopZ);
					if (GapCm > CoplanarToleranceCm)
						continue;

					const float SharedX = FMath::Min(Left.Max.X, Right.Max.X)
						- FMath::Max(Left.Min.X, Right.Min.X);
					const float SharedY = FMath::Min(Left.Max.Y, Right.Max.Y)
						- FMath::Max(Left.Min.Y, Right.Min.Y);
					if (SharedX <= 0.0f || SharedY <= 0.0f
						|| SharedX * SharedY < MinimumSharedAreaCm2)
					{
						continue;
					}

					++CoplanarPairCount;
					TightestGapCm = FMath::Min(TightestGapCm, GapCm);
					PairCounts.FindOrAdd(FString::Printf(TEXT("%s|%s"), *Left.Label, *Right.Label))++;
					if (Samples.Num() < 6)
					{
						const float SharedCenterX = (FMath::Max(Left.Min.X, Right.Min.X)
							+ FMath::Min(Left.Max.X, Right.Max.X)) * 0.5f;
						const float SharedCenterY = (FMath::Max(Left.Min.Y, Right.Min.Y)
							+ FMath::Min(Left.Max.Y, Right.Max.Y)) * 0.5f;
						Samples.Add(FString::Printf(
							TEXT("world=(%.0f,%.0f) cell=(%d,%d) z=%.1f gap=%.2f area_m2=%.1f %s|%s"),
							SharedCenterX, SharedCenterY,
							FMath::RoundToInt(SharedCenterX / DesignCellSize),
							FMath::RoundToInt(SharedCenterY / DesignCellSize),
							Left.TopZ, GapCm, SharedX * SharedY / 10000.0f,
							*Left.Label, *Right.Label));
					}
				}
			}
		}
	}

	PairCounts.ValueSort([](int32 Left, int32 Right) { return Left > Right; });
	TArray<FString> RankedPairs;
	for (const TPair<FString, int32>& Entry : PairCounts)
	{
		RankedPairs.Add(FString::Printf(TEXT("%s x%d"), *Entry.Key, Entry.Value));
		if (RankedPairs.Num() >= 5)
			break;
	}

	UE_LOG(LogTemp, Display,
		TEXT("Coplanar surface audit: tolerance_cm=%.0f min_draw_distance_cm=%.0f ")
		TEXT("inspected_instances=%d near_field_components_skipped=%d flat_surfaces=%d ")
		TEXT("truncated=%s coplanar_pairs=%d distinct_pairs=%d tightest_gap_cm=%.2f pass=%s ")
		TEXT("top_pairs=[%s] sample=[%s]"),
		CoplanarToleranceCm,
		MinAuditDrawDistanceCm,
		InspectedInstanceCount,
		SkippedNearFieldComponents,
		FlatSurfaceCount,
		bTruncated ? TEXT("true") : TEXT("false"),
		CoplanarPairCount,
		PairCounts.Num(),
		CoplanarPairCount > 0 ? TightestGapCm : 0.0f,
		CoplanarPairCount == 0 ? TEXT("true") : TEXT("false"),
		*FString::Join(RankedPairs, TEXT(" ; ")),
		*FString::Join(Samples, TEXT(" ; ")));
}

void UPGMapVerifier::VerifySinglePlayerValidation()
{
	if (bLoggedSinglePlayerValidation || !Map->bStartedSinglePlayerValidation
		|| !Map->bIssuedSinglePlayerValidationMove)
		return;

	const double ElapsedSeconds = FPlatformTime::Seconds() - Map->SinglePlayerValidationStartTimeSeconds;
	if (ElapsedSeconds < 8.0)
		return;

	bLoggedSinglePlayerValidation = true;
	const APawn* PlayerPawn = Map->GetValidationPlayerPawn();
	const bool bPlayerPossessed = IsValid(PlayerPawn) && PlayerPawn->IsPlayerControlled();
	const float PlayerGroundZ = IsValid(PlayerPawn)
		? PlayerPawn->GetActorLocation().Z
		: -BIG_NUMBER;
	const float AIMovedDistance = IsValid(Map->ValidationAICharacter)
		? FVector::Dist2D(Map->ValidationAICharacter->GetActorLocation(), Map->ValidationAIStartLocation)
		: 0.0f;
	const float AITargetDistance = IsValid(Map->ValidationAICharacter)
		? FVector::Dist2D(Map->ValidationAICharacter->GetActorLocation(), Map->ValidationAITargetLocation)
		: BIG_NUMBER;
	UE_LOG(LogTemp, Display,
		TEXT("Single-player validation: player_possessed=%s player_grounded=%s ai_moved_cm=%.1f ai_target_distance_cm=%.1f ai_path_pass=%s elapsed_ms=%.2f"),
		bPlayerPossessed ? TEXT("true") : TEXT("false"),
		PlayerGroundZ > 0.0f && PlayerGroundZ < 500.0f ? TEXT("true") : TEXT("false"),
		AIMovedDistance,
		AITargetDistance,
		AIMovedDistance > 200.0f ? TEXT("true") : TEXT("false"),
		ElapsedSeconds * 1000.0);
	// 검사 끝: 시험용 캐릭터는 더 할 일이 없다. 판에 남으면 몬스터가 표적으로 삼거나 길을 막는다.
	if (IsValid(Map->ValidationAICharacter))
	{
		Map->ValidationAICharacter->Destroy();
		Map->ValidationAICharacter = nullptr;
	}
}

void UPGMapVerifier::VerifyLocalPerformance(float DeltaSeconds)
{
	if (!bLoggedSinglePlayerValidation || PerformanceSampleCount >= 50)
		return;

	PerformanceDeltaSecondsTotal += FApp::GetDeltaTime();
	++PerformanceSampleCount;
	if (PerformanceSampleCount < 50)
		return;

	int32 ActorCount = 0;
	for (TActorIterator<AActor> It(Map->GetWorld()); It; ++It)
		++ActorCount;

	int32 PCGInstanceCount = 0;
	TArray<UActorComponent*> DressingInstanceComponents;
	Map->GetComponents(UInstancedStaticMeshComponent::StaticClass(), DressingInstanceComponents);
	for (UActorComponent* Component : DressingInstanceComponents)
	{
		const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Component);
		if (IsValid(ISM) && ISM->ComponentTags.Contains(TEXT("PCG_Dressing")))
			PCGInstanceCount += ISM->GetInstanceCount();
	}

	const FPlatformMemoryStats MemoryStats = FPlatformMemory::GetStats();
	const double AverageFrameSeconds = PerformanceDeltaSecondsTotal / PerformanceSampleCount;
	UE_LOG(LogTemp, Display,
		TEXT("Local performance: samples=%d avg_frame_ms=%.3f sampled_fps=%.1f actors=%d ground_hism=%d road_hism=%d pcg_instances=%d used_physical_mb=%.1f layout_hash=%08X"),
		PerformanceSampleCount,
		AverageFrameSeconds * 1000.0,
		AverageFrameSeconds > SMALL_NUMBER ? 1.0 / AverageFrameSeconds : 0.0,
		ActorCount,
		Map->GroundHISM->GetInstanceCount(),
		Map->RoadSurfaceHISM->GetInstanceCount(),
		PCGInstanceCount,
		MemoryStats.UsedPhysical / (1024.0 * 1024.0),
		Map->LayoutHash);
}

void UPGMapVerifier::VerifyDesignLevelSeparation()
{
	if (bLoggedDesignLevelSeparation || !Map->TileSpawner->AreAllFacilityLevelsLoaded())
	{
		return;
	}

	auto GetLoadedLevelBounds = [](const ULevel* Level)
	{
		FBox Bounds(EForceInit::ForceInit);
		if (!IsValid(Level))
			return Bounds;

		for (const AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor) || Actor->IsHidden())
				continue;

			TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
			Actor->GetComponents(PrimitiveComponents);
			for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
			{
				if (IsValid(PrimitiveComponent)
					&& PrimitiveComponent->IsRegistered()
					&& PrimitiveComponent->IsVisible())
				{
					Bounds += PrimitiveComponent->Bounds.GetBox();
				}
			}
		}
		return Bounds;
	};

	TArray<FBox> BoundsByFacility;
	if (Map->bUseRuntimeBlueprintTiles)
	{
		for (const AActor* SpawnedActor : Map->SpawnedRuntimeTiles)
		{
			if (!IsValid(SpawnedActor)
				|| !SpawnedActor->Tags.Contains(TEXT("RuntimeTacticalFacility")))
			{
				continue;
			}
			const FBox Bounds = SpawnedActor->GetComponentsBoundingBox(true);
			if (Bounds.IsValid)
				BoundsByFacility.Add(Bounds);
		}
	}
	else
	{
		for (const ULevelStreamingDynamic* Instance : Map->FacilityDesignLevelInstances)
		{
			if (IsValid(Instance))
				BoundsByFacility.Add(GetLoadedLevelBounds(Instance->GetLoadedLevel()));
		}
	}
	int32 OverlapPairs = 0;
	float MinimumGapCm = 0.0f;
	bool bMeasuredGap = false;
	for (int32 A = 0; A < BoundsByFacility.Num(); ++A)
	{
		for (int32 B = A + 1; B < BoundsByFacility.Num(); ++B)
		{
			if (BoundsByFacility[A].Intersect(BoundsByFacility[B]))
				++OverlapPairs;
			const FVector Delta = BoundsByFacility[A].GetCenter() - BoundsByFacility[B].GetCenter();
			const float PairGapX = FMath::Abs(Delta.X)
				- BoundsByFacility[A].GetExtent().X - BoundsByFacility[B].GetExtent().X;
			const float PairGapY = FMath::Abs(Delta.Y)
				- BoundsByFacility[A].GetExtent().Y - BoundsByFacility[B].GetExtent().Y;
			// For axis-aligned facility bounds, separation on either axis is enough.
			// The old radial extent calculation could report a negative gap even when
			// Intersect() correctly said the facilities did not overlap.
			const float PairGapCm = FMath::Max(PairGapX, PairGapY);
			MinimumGapCm = bMeasuredGap ? FMath::Min(MinimumGapCm, PairGapCm) : PairGapCm;
			bMeasuredGap = true;
		}
	}

	bLoggedDesignLevelSeparation = true;
	UE_LOG(LogTemp, Display,
		TEXT("Facility visible separation: count=%d overlap_pairs=%d minimum_gap_cm=%.1f pass=%s"),
		BoundsByFacility.Num(), OverlapPairs, MinimumGapCm,
		OverlapPairs == 0 ? TEXT("true") : TEXT("false"));
}
