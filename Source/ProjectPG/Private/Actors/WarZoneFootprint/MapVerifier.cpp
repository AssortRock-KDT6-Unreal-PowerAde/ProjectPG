//검사기가 불려서 일하는 곳

#include "MapVerifier.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"

// 칸 크기 (2000cm). 맵 cpp206줄과 같은 값이어야 한다.
// constexpr : 절대 안바뀌는 숫자. 
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
