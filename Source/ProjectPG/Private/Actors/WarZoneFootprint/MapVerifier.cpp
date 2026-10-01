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
#include "NavigationSystem.h"   // 길찾기 지도 담당
#include "Algo/AnyOf.h"         // "하나라도 맞으면" 판정(동서남북 중 한 곳)
#include "Containers/Queue.h"   // 줄 세우기(먼저 넣은 칸부터 꺼냄) — 출구까지 길 찾을 때 씀
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
	if (bLoggedWorldCollision
		|| !Map->AreAllFacilityLevelsLoaded())
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
// GamePlayPoint(게임지점:시작,상자,몬스터,출구,퀘스트자리)
// Distribution(퍼진모양)= "게임 지점들이 골고루 알맞게 퍼져있나?"
void UMapVerifier::VerifyGameplayPointDistribution()
{
	// 아래 셋 중 하나라도 맞으면 이번 프레임은 그냥 끝낸다.
	//  - 스티커가 붙어 있다 (이미 검사함)
	//  - 게임 지점 목록이 비었다 (아직 안 만들어짐)
	//  - 지점 끼임 정리(벽·물건 속에 박힌 자리를 빈 곳으로 옮기기)가 아직 안 끝났다
	//    (bResolvedGameplayPointSafety = "끼임 정리 끝났음?" 스티커. 몬스터랑은 상관없음)

	if (bLoggedGameplayPointDistribution || Map->LevelDesignPoints.IsEmpty() || !Map->bResolvedGameplayPointSafety)
		return;

	bLoggedGameplayPointDistribution = true;
	// 종류별 개수 (시작 몇 개, 상자 몇 개 …)
	TMap<ELevelDesignPointType, int32> Counts;
	// 지금까지 본 이름들 (중복 찾기용)
	TSet<FName> UniqueIds;
	// 이름표가 잘못된 지점 수
	int32 InvalidMetadata = 0;
	// 시작 자리들 주소 목록
	TArray<const FLevelDesignPoint*> SpawnPoints;
	// 상자 자리들 주소 목록
	TArray<const FLevelDesignPoint*> LootPoints;
	// 몬스터 자리들 주소 목록
	TArray<const FLevelDesignPoint*> AIPoints;
	// [몬스터 금지 구역인가?] 칸 번호(Cell) 하나를 받아서 true/false 를 돌려주는 작은 함수(람다).
	//  - auto      : 이 작은 함수 자체의 타입. 이름이 길어서 컴파일러가 알아서 적게 맡김. (bool 아님)
	//  - [this]    : 검사기 주소 쪽지를 안으로 들고 들어감 → 안에서 Map-> 를 쓸 수 있음.
	//  - 돌려주는 값 : 아래 return true / return false 가 bool.
	// 몬스터 금지 구역 = 시작하자마자 싸우지 않게 일부러 몬스터를 안 두는 곳.
	auto IsProtectedFromAI = [this](const FIntPoint& Cell)
	{
		for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		{
			// 맨해튼 거리 = |가로 칸 차이| + |세로 칸 차이|. 바둑판 길을 가로·세로로만 걸은 칸 수.
			const int32 ManhattanDistance = FMath::Abs(Cell.X - Placement.GridCell.X)
				+ FMath::Abs(Cell.Y - Placement.GridCell.Y);
			//  - 시작 타일에서 4칸 미만이면 몬스터 금지
			if ((Placement.Visual == ETileDesignVisual::Spawn && ManhattanDistance < 4)
				//  - 출구 타일에서 2칸 미만이면 몬스터 금지
				|| (Placement.Visual == ETileDesignVisual::Exit && ManhattanDistance < 2))
				return true;
		}
		// 호수가 있는 판이면(호수는 맵 가장자리 한 모서리), 호숫가 시골 은신처(배 타고 내리는 마을)에서
		// 3칸 미만도 몬스터 금지. 이 마을엔 시작 타일 표시가 없지만 시작 구역으로 친다.
		// 지점 만드는 쪽에도 똑같은 규칙이 있어서, 둘이 다르면 멀쩡한 맵을 불합격 처리하게 된다.
		if (Map->bHasBorderLake)
		{
			for (const FFacilityPlacement& Facility :Map->FacilityPlacements)
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
	// 게임 지점을 하나씩 꺼내서:
	// ① 종류별 개수 세기(시작·상자·몬스터·출구·퀘스트) — 모자라면 4명이 못 들어오거나 심심한 맵.
	// ② 이름표 검사: 이름 비었나·중복인가, 등급 1~3, 크기 50cm 이상, 수용 1 이상 — 틀리면 퀘스트·저장이 엉킴.
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

	// ④ 시작 자리와 몬스터 자리 사이 가장 가까운 거리 — 너무 가까우면 태어나자마자 맞음(기준 50m 이상).
	float MinimumSpawnAIDistanceCm = BIG_NUMBER;
	for (const FLevelDesignPoint* Spawn : SpawnPoints)
		for (const FLevelDesignPoint* AI : AIPoints)
			MinimumSpawnAIDistanceCm = FMath::Min(
				MinimumSpawnAIDistanceCm,
				FVector::Dist2D(Spawn->WorldLocation, AI->WorldLocation));
	// ③ 시작 자리끼리 가장 가까운 거리 — 너무 붙으면 플레이어 둘이 겹쳐 태어남(기준 2.5m 이상).
	float MinimumSpawnSeparationCm = BIG_NUMBER;
	for (int32 A = 0; A < SpawnPoints.Num(); ++A)
		for (int32 B = A + 1; B < SpawnPoints.Num(); ++B)
			MinimumSpawnSeparationCm = FMath::Min(
				MinimumSpawnSeparationCm,
				FVector::Dist2D(SpawnPoints[A]->WorldLocation, SpawnPoints[B]->WorldLocation));

	// ⑤⑥ 맵을 100m마다(칸 번호가 5의 배수인 곳) 찍어서, 거기서 가장 가까운 상자·몬스터까지 거리를 잰다.
	// 그중 제일 먼 값이 기준을 넘으면 "한참 걸어도 줍을 게 없다(250m)" / "싸울 게 없다(300m)".
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

	// ⑦ 큰 건물마다 상자·몬스터 자리가 하나라도 있나 — 없으면 들어갔는데 텅 빈 건물.
	// 단, 몬스터 금지 구역(IsProtectedFromAI) 안 건물은 몬스터가 없어도 봐준다.
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

	// 합격 조건 모음: ① 개수(시작 4·상자 40·몬스터 24·출구 2·퀘스트 1 이상) ② 잘못된 이름표 0
	// ③ 2.5m ④ 50m ⑤ 250m ⑥ 300m ⑦ 빈 건물 0. 전부 맞으면 로그에 pass=true.
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


void UMapVerifier::VerifyNavigation()
{
	if (Map->bLoggedNavigation || !bLoggedWorldCollision || Map->LevelDesignPoints.IsEmpty())
		return;

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
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
}



// Critical(꼭 필요한) Routes(길) = "시작 자리에서 출구랑 큰 건물까지 걸어서 갈 수 있나?"
// 게임에서: 물(호수)이나 막힌 칸 때문에 출구까지 길이 끊긴 맵이면 플레이어가 탈출을 못 한다.
// 방법: 맵을 20m 칸 바둑판으로 보고, 시작 자리 칸에서 출발해 동서남북으로 이어진 칸을 전부 퍼져 나가며 칠한다.
//       칠해진 칸에 출구·큰 건물이 들어 있으면 "갈 수 있음".
void UMapVerifier::VerifyCriticalRoutes()
{
	// 이미 보고서 썼거나, 길찾기 지도 검사가 아직 안 끝났거나(맵 쪽 스티커), 칸 목록이 비었으면 이번엔 그냥 끝.
	// bLoggedNavigation 은 성능 검사(VerifyLocalPerformance)도 아직 맵에서 보고 있어서 맵 집에 두고 빌려 본다.
	if (bLoggedCriticalRoutes || !Map->bLoggedNavigation || Map->TileDesignPlacements.IsEmpty())
		return;

	// 시작 자리(플레이어가 태어나는 곳) 하나를 찾는다. 없으면 출발할 데가 없으니 끝.
	// Pred : 판정함수
	const FLevelDesignPoint* Spawn = Map->LevelDesignPoints.FindByPredicate(
		[](const FLevelDesignPoint& Point) { return Point.Type == ELevelDesignPointType::Spawn; });
	if (Spawn == nullptr)
		return;

	// 걸을 수 있는 칸 목록 만들기: 물 칸만 빼고 전부 + 큰 건물이 차지한 칸들.
	// (길찾기 지도는 플레이어·몬스터 주변에만 깔려서 600m 맵 전체를 한 번에 못 물어본다.
	//  그래서 칸 바둑판으로 "이어져 있나" 만 따진다 — 원래 영어 주석의 뜻.)
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

	// 시작 칸부터 퍼져 나가기(물감 번지듯이):
	// Queue = 다음에 가 볼 칸 줄, Visited = 이미 칠한 칸.
	// 줄에서 칸 하나를 꺼내 → 동서남북 옆 칸이 걸을 수 있고 아직 안 칠했으면 칠하고 줄 끝에 세운다 → 줄이 빌 때까지 반복.
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

	// 다 칠한 뒤 확인 ①: 출구 자리들이 칠해진 칸 안에 있나 — 안 칠해졌으면 그 출구는 못 감.
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
	// 확인 ②: 큰 건물(검문소·은신처 등)마다, 차지한 칸 중 하나라도 칠해졌나 — 아니면 그 건물에 못 들어감.
	for (const FFacilityPlacement& Facility : Map->FacilityPlacements)
	{
		++TargetCount;
		const bool bReached = Facility.OccupiedCells.ContainsByPredicate(
			[&Visited](const FIntPoint& FacilityCell) { return Visited.Contains(FacilityCell); });
		if (bReached) ++ReachableTargets;
		else FailedRoutes.Add(FString::Printf(TEXT("Facility_%d_%d"), Facility.AnchorCell.X, Facility.AnchorCell.Y));
	}

	// 보고서 썼음 스티커 붙이고 로그 한 줄. 출구·건물 전부 갈 수 있으면 pass=true, 못 가는 곳은 sample 에 이름.
	bLoggedCriticalRoutes = true;
	UE_LOG(LogTemp, Display,
		TEXT("Critical logical route audit: from=%s visited_cells=%d/%d targets=%d reachable=%d failed=%d pass=%s sample=[%s]"),
		*Spawn->PointId.ToString(), Visited.Num(), WalkableCells.Num(), TargetCount, ReachableTargets,
		TargetCount - ReachableTargets,
		TargetCount == ReachableTargets ? TEXT("true") : TEXT("false"),
		*FString::Join(FailedRoutes, TEXT(",")));
}
