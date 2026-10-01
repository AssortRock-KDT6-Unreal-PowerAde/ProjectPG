//검사기가 불려서 일하는 곳

#include "MapVerifier.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"
// 레벨스트리밍을 맵에 불러왔는지 객체에 질문,레벨 스트리밍 하나를 담당하는 담당자.
#include "Engine/LevelStreamingDynamic.h"   
// 레벨스트리밍 할것의 소속 액터 모음
#include "Engine/Level.h"                   
// 액터자체는 이름표같은 빈껍데기니까 그 엑터에 붙은 모양. 컨테이너의 철판모양같은걸 구성하는게 StaticMeshComponent인데 UPrimitiveComponent의 자식이다.
#include "Components/PrimitiveComponent.h"   
//// 길 타일 조각(길이 뚫린 방향 번호를 가진 타일)
#include "Actors/TacticalTileActor.h"
// 캡슐을 세웠을 때 닿은 것 하나하나의 기록 
#include "Engine/OverlapResult.h"
// 큰 지형 땅( 산, 들판)
#include "LandscapeProxy.h"
// 칸 크기 (2000cm). 맵 cpp206줄과 같은 값이어야 한다.
// constexpr : 절대 안바뀌는 숫자. 
// const랑 차이는 빌드할때 이미아는 숫자냐. 게임 도는 도중에 정해지냐 차이. 

constexpr float DesignCellSize = 2000.0f;

void UMapVerifier::Init(AWarZoneFootprintPreview* InMap)
{
	
	Map=InMap;
}

UWorld* UMapVerifier::GetWorld()const
{
	return Map ? Map->GetWorld() : nullptr;
}
void UMapVerifier::VerifyPCGDressing()
{
	if (bLoggedPCGDressing || !IsValid(Map->DressingPCGComponent) || Map->DressingPCGComponent->IsGenerating())
		return;//이미 검사했거나 ,꾸미기가 없거나,아직 만드는 중이면 끝

	if (!Map->DressingPCGComponent->bGenerated)
		return;//아직 안만들어 졌으면 끝.

	bLoggedPCGDressing = true; //"검사했음"표시
	int32 ManagedResourceCount = 0;//개수새서로그찍기.
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

void UMapVerifier::VerifyWorldCollision()
{
	if (Map->bLoggedWorldCollision
		|| !Map->AreAllFacilityLevelsLoaded())
	{
		return;
	}

	Map->bLoggedWorldCollision = true;
	int32 HitCount = 0;
	TArray<FIntPoint> MissingCells;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DesignWorldGroundValidation), true);
	for (int32 Y = -22; Y <= 22; ++Y)
	{
		for (int32 X = -22; X <= 22; ++X)
		{
			const FVector CellCenter(X * DesignCellSize, Y * DesignCellSize, 0.0f);
			FHitResult HitResult;
			const bool bHit = GetWorld()->LineTraceSingleByChannel(
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

void UMapVerifier::VerifyDesignLevelSeparation()
{
	// 시설 조각이 다 끼워질때까지 기다린다. 
	// AreAllFacilityLevelsLoaded : 시설 조각 다 끼워졌나? 
	if (bLoggedDesignLevelSeparation || !Map->AreAllFacilityLevelsLoaded())
	{
		return;
	}
	//큰 건물마다 딱맞는 투명상자를 하나씩 씌운다?
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
	
	// bUseRuntimeBlueprintTiles : 스티커: 건물을 블프 액터로 만드는 방식이니? 
	TArray<FBox> BoundsByFacility;
	if (Map->bUseRuntimeBlueprintTiles)
	{
		// SpawnedRuntimeTiles : 그 방식으로 만들어 둔 건물 목록
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
		// FacilityDesignLevelInstances : 다른 방식(끼워 넣은 시설 조각)의 목록
		for (const ULevelStreamingDynamic* Instance : Map->FacilityDesignLevelInstances)
		{
			if (IsValid(Instance))
				BoundsByFacility.Add(GetLoadedLevelBounds(Instance->GetLoadedLevel()));
		}
	}
	int32 OverlapPairs = 0;
	float MinimumGapCm = 0.0f;
	//상자를 두개씩 짝지어서 겹치는지 보고 가장 가까운 두 상자 사이 거리도 잰다.
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
	// 로그한줄 : "건물 몇 개, 겹친 짝 몇 쌍, 제일 가까운 거리 몇 cm,겹친게 0이면pass=true" 
	UE_LOG(LogTemp, Display,
		TEXT("Facility visible separation: count=%d overlap_pairs=%d minimum_gap_cm=%.1f pass=%s"),
		BoundsByFacility.Num(), OverlapPairs, MinimumGapCm,
		OverlapPairs == 0 ? TEXT("true") : TEXT("false"));
	
}

// Tactical(전투용) + Layout(배치) + Quality(품질) = "전투하기 좋게 타일이 제대로 놓였니?"
void UMapVerifier::VerifyTacticalLayoutQuality()
// 타일과건물이 예정된 개수만큼 다 생길때 까지 기다린다. 
{
	int32 ExpectedRuntimeFacilityCount = 0;
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
		if (Placement.VisualSet != EFacilityVisualSet::Checkpoint)
			++ExpectedRuntimeFacilityCount;
	// bLoggedTacticalLayoutQuality : 로그찍음(true)?
	if (bLoggedTacticalLayoutQuality
		// 타일 배치 목록이 비었나?
		|| Map->TileDesignPlacements.IsEmpty()
		// 만들어진 개수가 "타일 개수+방금 센 개수와 다른가?"
		|| Map->SpawnedRuntimeTiles.Num() != Map->TileDesignPlacements.Num() + ExpectedRuntimeFacilityCount
		// 시설 레벨이 아직 다 안불려 왔나?
		|| !Map->AreAllFacilityLevelsLoaded())
		return;
	// 스티커 붙이기: 이 검사 로그를 이제 찍는다고 표시. 다음 프레임부턴 위 if 에서 바로 return.
	bLoggedTacticalLayoutQuality = true;

	// int32 = 정수 하나(4바이트). 0부터 세는 칸들.
	int32 GridMisalignments = 0;        // 칸 한가운데에 안 맞거나, 한 칸에 타일 두 장 겹친 타일 수
	int32 OutOfBoundsComponents = 0;    // 벽·물건이 옆 칸으로 너무 삐져나간 타일 수

	// TArray<FString> = 글자(이름)를 순서대로 담는 목록.
	TArray<FString> OverflowSamples;    // 삐져나간 타일 이름 몇 개 (로그에 보여 주려고, 최대 12개)

	int32 InvalidMasks = 0;             // 길 연결 번호(0~15)나 모양 번호(0~3)가 범위를 벗어난 타일 수
	int32 UnsafeAnchorOverlaps = 0;     // 위험하게 겹친 지점 수 (뒤쪽 코드에서 셈)
	TArray<FString> UnsafeAnchorSamples;// 그 위험 지점 이름 몇 개 (로그용)

	// TSet<FIntPoint> = 같은 게 두 번 못 들어가는 모음. 순서는 없음. "이거 들어 있나?" 확인이 빠름.
	// FIntPoint = 정수 두 개 (X, Y). 여기선 "몇 번째 칸"(예: 5, 7).
	TSet<FIntPoint> OccupiedCells;      // 이미 타일이 놓인 칸 번호들. 같은 칸이 또 나오면 겹친 것.

	// SpawnedRuntimeTiles(만들어 둔 타일 액터 목록)에서 하나씩 꺼내 Actor 라고 부르며 끝까지 돈다.
	// AActor* = 타일 액터의 주소 쪽지.
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

		// 2. 벽·물건이 칸 밖으로 삐져나갔나
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
				OverflowSamples.Add(Actor->GetActorLabel());
		}
		// 길이 뚫린 방향 번호가 정상인가(틀리면 길이 담벼락에 막힘)
		if (const ATacticalTileActor* TacticalTile = Cast<ATacticalTileActor>(Actor))
		{
			if (TacticalTile->GetEffectiveConnectionMask() > 15
				|| TacticalTile->GetEffectiveLayoutVariant() > 3)
			{
				++InvalidMasks;
			}
		}
	}
	// 4. 시작, 출구 , 루팅 자리에서 사람캡슐 끼이는지. 
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
		// 캡슐 = "여기 사람이 서도 되나?" 확인용 가짜 사람. 시작 자리에 잠깐 세워 본다(새로 만드는 건 없음).
		// 반지름 55cm, 반높이 95cm(대략 사람 크기). 몸에 벽이 닿으면 "여기서 태어나면 벽에 낀다".
		const FCollisionShape Capsule = FCollisionShape::MakeCapsule(55.0f, 95.0f);
		// 캡슐에 닿은 것들의 목록이 나온다. 벽,땅,상자.
		TArray<FOverlapResult> AnchorOverlaps;
		// OverlapMultiByChannel 시작자리에 사람 캡슐을 세움 
		const bool bHasOverlap = GetWorld()->OverlapMultiByChannel(
			AnchorOverlaps,
			Point.WorldLocation + FVector(0, 0, 95.0f),
			FQuat::Identity,
			ECC_Pawn,
			Capsule,
			AnchorQuery);
		const bool bBlockedByTacticalGeometry = bHasOverlap && AnchorOverlaps.ContainsByPredicate(
			[this](const FOverlapResult& Result)
			{
				// 목록에서 하나씩 꺼내 본다
				const AActor* HitActor = Result.GetActor();
				const UPrimitiveComponent* HitComponent = Result.GetComponent();
				return IsValid(HitActor) && HitActor != Map
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
						return IsValid(HitActor) && HitActor != Map
							&& !HitActor->ActorHasTag(TEXT("LevelDesignPoint"))
							&& !(Map->bUseRuntimeBlueprintTiles && HitActor->IsA<ALandscapeProxy>())
							&& IsValid(HitComponent)
							&& HitComponent->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block;
					});
				UnsafeAnchorSamples.Add(FString::Printf(TEXT("%s:%s/%s"),
					*Point.PointId.ToString(),
					Blocking && IsValid(Blocking->GetActor()) ? *Blocking->GetActor()->GetActorLabel() : TEXT("UnknownActor"),
					Blocking && IsValid(Blocking->GetComponent()) ? *Blocking->GetComponent()->GetName() : TEXT("UnknownComponent")));
			}
		}
	}
	const FString UnsafeSummary = FString::Join(UnsafeAnchorSamples, TEXT(","));
	// 5. 로그 남기기.
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
// Travel(이동) + Cove(엄폐물, 몸을 숨길 것) + Density(촘촘함) 
// = 돌아다니는 길에 숨을 곳이 충분히 있나?
void UMapVerifier::VerifyTravelCoverDensity()
{//타일,건물이 다 생길때까지 기다린다. 
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
	float LongestExposedRunCm = 0.0f;//맵을 두 줄 걸러 한줄씩, 왼쪽에서 오른쪽으로 칸마다 걸어가 본다.
	for (int32 Y = -22; Y <= 22; Y += 2)
	{
		float CurrentRunCm = 0.0f;
		for (int32 X = -22; X <= 22; ++X)
		{
			++SampleCount;
			// Cover is useful when it protects a crouched player; the previous 1.4m
			// standing-eye trace incorrectly rejected deliberate chest-high cover.
			const FVector EyeLocation(X * DesignCellSize, Y * DesignCellSize, 90.0f);//각 칸 가운데 쪼그려 앉은 사람 눈높이를 정한다.(90cm)
			bool bHasNearbyCover = false;
			FCollisionQueryParams Query(SCENE_QUERY_STAT(TravelCoverDensity), true);//거기서 8방향(45도씩)9m 짜리 막대기를 뻗어 본다. 
			for (int32 DirectionIndex = 0; DirectionIndex < 8; ++DirectionIndex)
			{
				const float Angle = FMath::DegreesToRadians(DirectionIndex * 45.0f);
				const FVector Direction(FMath::Cos(Angle) * 900.0f, FMath::Sin(Angle) * 900.0f, 0.0f);
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByChannel(Hit, EyeLocation, EyeLocation + Direction, ECC_Visibility, Query))
				{
					const UPrimitiveComponent* Component = Hit.GetComponent();
					const FVector Normal = Hit.ImpactNormal;
					// Ground is a horizontal hit; meaningful cover has a lateral face.
					// 막대기 옆면에 있는 것(담,상자,바위)이 걸리면->"여기 숨을 데 있음". 바닥에 걸린건 엄폐물이 아니라서 안침
					if (IsValid(Component) && FMath::Abs(Normal.Z) < 0.55f)
					{
						bHasNearbyCover = true;
						break;
					}
				}
			}
			//	숨을 데 없는 칸이 연달아 나오면 그 길이를 더해 간다. 숨을 데가 나오면 0으로
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
	//로그: 숨을 데 없는 칸 비율이 55% 이하이고, 가장 긴 무방비 구간이 120m 이하면 pass=true
	UE_LOG(LogTemp, Display,
		TEXT("Travel cover audit (crouch_height=90cm): samples=%d fully_exposed=%d exposed_ratio=%.3f longest_exposed_run_m=%.1f target_ratio<=0.55 target_run<=120m pass=%s"),
		SampleCount,
		FullyExposedSamples,
		ExposedRatio,
		LongestExposedRunCm / 100.0f,
		ExposedRatio <= 0.55f && LongestExposedRunCm <= 12000.0f ? TEXT("true") : TEXT("false"));
}
