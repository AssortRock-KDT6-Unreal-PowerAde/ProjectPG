//검사기가 불려서 일하는 곳

#include "MapVerifier.h"
#include "PCGComponent.h"
#include "PCGManagedResource.h"


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