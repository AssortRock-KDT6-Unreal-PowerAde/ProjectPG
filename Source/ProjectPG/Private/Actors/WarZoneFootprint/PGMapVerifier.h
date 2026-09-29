// 맵 자체 검사 — 매 판 맵이 규칙대로 만들어졌는지 확인하고 로그로 남긴다(게임 동작은 바꾸지 않는다).
// 2026-09-26 SOLID(한 책임): 맵 액터(AWarZoneFootprintPreview)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 맵 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 맵 액터에 그대로 두고 Map-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 맵 액터의 비공개 멤버를 읽어야 해서 맵 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "PGMapVerifier.generated.h"

UCLASS(Transient)
class UPGMapVerifier : public UObject
{
	GENERATED_BODY()

public:
	void Init(AWarZoneFootprintPreview* InMap) { Map = InMap; }
	virtual UWorld* GetWorld() const override { return Map ? Map->GetWorld() : nullptr; }

	void VerifyPCGDressing();
	void VerifySinglePlayerValidation();
	void VerifyLocalPerformance(float DeltaSeconds);
	void VerifyWorldCollision();
	void VerifyNavigation();
	void VerifyTacticalLayoutQuality();
	void VerifyTravelCoverDensity();
	void VerifyGameplayPointDistribution();
	void VerifyCriticalRoutes();
	void VerifyTraversableElevation();
	void VerifyCoplanarSurfaces();
	void VerifyDesignLevelSeparation();

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;

	bool bLoggedDesignLevelSeparation = false;
	bool bLoggedWorldCollision = false;
	bool bLoggedTacticalLayoutQuality = false;
	bool bLoggedTravelCoverDensity = false;
	bool bLoggedGameplayPointDistribution = false;
	bool bLoggedCriticalRoutes = false;
	bool bLoggedTraversableElevation = false;
	bool bLoggedCoplanarSurfaces = false;
	bool bLoggedPCGDressing = false;
	bool bLoggedSinglePlayerValidation = false;
	int32 PerformanceSampleCount = 0;
	double PerformanceDeltaSecondsTotal = 0.0;
};
