#include "Actors/MapBuilder/MapNavigationBuilder.h"

#include "Actors/MapBuilder/MapAssetSet.h"
#include "Components/BrushComponent.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "PhysicsEngine/BodySetup.h"
#include "TimerManager.h"

void UMapNavigationBuilder::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapNavigationBuilder::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 맵 전체 길찾기 영역을 넣고 굽기 시작 (서버만, 판마다 한 번).
// 부담: 600 m × 600 m 맵이면 조각 약 3900개를 게임을 멈추지 않고 뒤에서 굽는다(에디터 서버에서 약 36초).
void UMapNavigationBuilder::StartBuild(const FBox& FloorBounds)
{
	if (GetWorld()->GetNetMode() == NM_Client || bReady)
		return;
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavigationSystem)
	{
		UE_LOG(LogTemp, Error, TEXT("Navigation area not added: no navigation system in this world - monsters cannot path"));
		return;
	}

	const UMapAssetSet& Settings = Map->GetMapAssets();
	const FVector HalfSize(FloorBounds.GetExtent().X, FloorBounds.GetExtent().Y, Settings.NavigationAreaHalfHeightCm);
	const FVector Center(FloorBounds.GetCenter().X, FloorBounds.GetCenter().Y, 0.0f);
	ANavMeshBoundsVolume* Volume = SpawnBoundsVolume(Center, HalfSize);
	if (!Volume)
		return;

	// 끝나는 순간을 엔진 알림으로 받는다(매 프레임 "다 됐나?" 묻지 않음). 영역을 알리기 전에 붙여야 놓치지 않는다.
	NavigationSystem->OnNavigationGenerationFinishedDelegate.AddUniqueDynamic(this, &UMapNavigationBuilder::HandleGenerationFinished);
	BuildStartSeconds = FPlatformTime::Seconds();
	GetWorld()->GetTimerManager().SetTimer(BuildTimeoutTimer,
		FTimerDelegate::CreateUObject(this, &UMapNavigationBuilder::HandleBuildTimeout),
		Settings.NavigationBuildTimeoutSeconds, false);
	NavigationSystem->OnNavigationBoundsUpdated(Volume);
	UE_LOG(LogTemp, Display, TEXT("Navigation area added: center=(%.0f,%.0f) half_size_cm=(%.0f,%.0f,%.0f)"),
		Center.X, Center.Y, HalfSize.X, HalfSize.Y, HalfSize.Z);
}

ANavMeshBoundsVolume* UMapNavigationBuilder::SpawnBoundsVolume(const FVector& Center, const FVector& HalfSize) const
{
	FActorSpawnParameters Parameters;
	Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ANavMeshBoundsVolume* Volume = GetWorld()->SpawnActor<ANavMeshBoundsVolume>(Center, FRotator::ZeroRotator, Parameters);
	if (!IsValid(Volume))
	{
		UE_LOG(LogTemp, Error, TEXT("Navigation area not added: could not spawn the bounds volume - monsters cannot path"));
		return nullptr;
	}
	UBrushComponent* Brush = Volume->GetBrushComponent();
	UBodySetup* BoxShape = NewObject<UBodySetup>(Brush);
	BoxShape->AggGeom.BoxElems.Add(FKBoxElem(HalfSize.X * 2.0f, HalfSize.Y * 2.0f, HalfSize.Z * 2.0f));
	Brush->BrushBodySetup = BoxShape;
	Brush->UpdateBounds();
	return Volume;
}

// 굽기가 끝났을 때 (엔진이 부름).
// 처음 한 번만 "다 구웠다" 로 친다. 그 뒤 일부만 다시 구울 때는 할 일 없음(알림도 떼어 둔다).
// 시간 제한이 지난 뒤 늦게 끝나도 여기서 다 구운 것으로 치고 알린다(로그에 late=true).
void UMapNavigationBuilder::HandleGenerationFinished(ANavigationData* NavigationData)
{
	if (bReady)
		return;
	bReady = true;
	FTimerManager& Timers = GetWorld()->GetTimerManager();
	const bool bLate = !Timers.IsTimerActive(BuildTimeoutTimer);
	Timers.ClearTimer(BuildTimeoutTimer);
	if (UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		NavigationSystem->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this, &UMapNavigationBuilder::HandleGenerationFinished);

	UE_LOG(LogTemp, Display, TEXT("Navigation build finished: seconds=%.1f late=%s nav_data=%s"),
		FPlatformTime::Seconds() - BuildStartSeconds, bLate ? TEXT("true") : TEXT("false"), *GetNameSafe(NavigationData));
	Map->OnNavigationReady.Broadcast();
}

// 정한 시간이 지나도 안 끝났을 때: 무엇이 남았는지 Error 로 남긴다.
// 기다리기는 그만두지 않는다: 늦게라도 끝나면 HandleGenerationFinished 가 알린다.
void UMapNavigationBuilder::HandleBuildTimeout()
{
	const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	UE_LOG(LogTemp, Error, TEXT("Navigation build not finished after %.0f s: remaining_tasks=%d running_tasks=%d - monsters cannot path yet"),
		Map->GetMapAssets().NavigationBuildTimeoutSeconds,
		NavigationSystem ? NavigationSystem->GetNumRemainingBuildTasks() : -1,
		NavigationSystem ? NavigationSystem->GetNumRunningBuildTasks() : -1);
}
