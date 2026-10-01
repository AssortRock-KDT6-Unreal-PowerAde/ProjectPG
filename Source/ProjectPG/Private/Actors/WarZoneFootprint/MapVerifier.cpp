//검사기가 불려서 일하는 곳

#include "MapVerifier.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"
// 레벨스트리밍을 맵에 불러왔는지 객체에 질문,그런 레벨스트리밍을 모아 둔 목록
#include "Engine/LevelStreamingDynamic.h"   
// 레벨스트리밍 할것의 소속 액터 모음
#include "Engine/Level.h"                   
// 액터자체는 이름표같은 빈껍데기니까 그 엑터에 붙은 모양. 컨테이너의 철판모양같은걸 구성하는게 StaticMeshComponent인데 UPrimitiveComponent의 자식이다.
#include "Components/PrimitiveComponent.h"   
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
