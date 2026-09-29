// 게임 지점 — 설계도에서 시작·출구·루팅·몬스터·퀘스트 자리를 찍고, 시설이 뜬 뒤 벽 속 지점을 옮기고, 준비되면 알린다.
// 2026-09-28 SOLID(한 책임): 맵 액터(AWarZoneFootprintPreview)에서 이 책임의 "하는 일" 을 떼어 낸 협력 객체(9/26 UPGMapVisualBuilder 와 같은 방식).
// 레벨에 저장되는 칸·부품·상태는 맵 액터에 그대로 두고 Map-> 으로 읽는다. 맵 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "PGGameplayPointBuilder.generated.h"

UCLASS(Transient)
class UPGGameplayPointBuilder : public UObject
{
	GENERATED_BODY()

public:
	void Init(AWarZoneFootprintPreview* InMap) { Map = InMap; }
	virtual UWorld* GetWorld() const override { return Map ? Map->GetWorld() : nullptr; }

	void BuildGameplayPointMarkers();
	bool IsCellProtectedFromAI(const FIntPoint& Cell) const;
	void AddDesignPoint(TMap<ELevelDesignPointType, int32>& Counts, ELevelDesignPointType Type, const FIntPoint& Cell, const FVector& Offset, const TCHAR* Prefix, const TCHAR* Archetype, uint8 Tier, float RadiusCm, int32 Capacity);
	void AddTileDesignPoints(TMap<ELevelDesignPointType, int32>& Counts);
	void AddExtraEdgeExitPoints(TMap<ELevelDesignPointType, int32>& Counts);
	void AddFacilityDesignPoints(TMap<ELevelDesignPointType, int32>& Counts);
	void RebuildGameplayPointHash();
	void BroadcastLevelDesignPointsWhenReady();
	void ResolveGameplayPointSafety();

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;
};
