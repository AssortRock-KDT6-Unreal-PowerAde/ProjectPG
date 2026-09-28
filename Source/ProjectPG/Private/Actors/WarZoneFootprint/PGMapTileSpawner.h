// 맵 세우기 — 설계도대로 타일·코드 시설을 세워 묶음(HISM)으로 흡수, 시설 레벨 불러오기, 높은 시설 둔덕, 길찾기 막이.
// 2026-09-28 SOLID(한 책임): 맵 액터(AWarZoneFootprintPreview)에서 이 책임의 "하는 일" 을 떼어 낸 협력 객체(9/26 UPGMapVisualBuilder 와 같은 방식).
// 레벨에 저장되는 칸·부품·상태는 맵 액터에 그대로 두고 Map-> 으로 읽는다. 맵 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "PGMapTileSpawner.generated.h"

UCLASS(Transient)
class UPGMapTileSpawner : public UObject
{
	GENERATED_BODY()

public:
	void Init(AWarZoneFootprintPreview* InMap) { Map = InMap; }
	virtual UWorld* GetWorld() const override { return Map ? Map->GetWorld() : nullptr; }

	void SpawnRuntimeBlueprintTiles();
	void PackTileActorVisuals(AActor* Actor, TMap<FString, UHierarchicalInstancedStaticMeshComponent*>& PackedComponentByKey, int32& PackedVisualInstanceCount);
	TSet<FIntPoint> CollectShoreCellsForTileSkip() const;
	int32 SpawnRuntimeFacilityActors(TMap<FString, UClass*>& ClassCache, int32& SpawnFailures, int32& HiddenTerrainUnderlayCount);
	void BuildElevatedFacilityTerrain();
	void RefreshNavigationBlockerRegion(
		UTacticalTileNavModifierComponent* Modifier,
		const FVector& WorldCenter,
		float RadiusCm);
	void LoadFacilityDesignLevel(const FFacilityPlacement& Placement, int32 PlacementIndex);
	bool AreAllFacilityLevelsLoaded() const;

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;
};
