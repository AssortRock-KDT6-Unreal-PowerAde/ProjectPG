// 구역 붕괴 — 땅이 무너져 꺼지는 연출과 구덩이, 무너진 자리 치우기.
// 2026-09-26 SOLID(한 책임): 맵 액터(AWarZoneFootprintPreview)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 맵 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 맵 액터에 그대로 두고 Map-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 맵 액터의 비공개 멤버를 읽어야 해서 맵 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "PGRegionCollapse.generated.h"

UCLASS(Transient)
class UPGRegionCollapse : public UObject
{
	GENERATED_BODY()

public:
	void Init(AWarZoneFootprintPreview* InMap) { Map = InMap; }
	virtual UWorld* GetWorld() const override { return Map ? Map->GetWorld() : nullptr; }

	// ---- 구역 붕괴 ----
	// 한 컴포넌트에서 무너질 인스턴스 묶음.
	//
	// 원본(HISM)의 인스턴스는 붕괴 동안 직접 움직이지 않는다. 큰 조각은 차례가 오면 컬링 트리·충돌·길찾기가 없는
	// 임시 ISM(Mover)으로 옮겨 그쪽을 한 번에 갱신하고, 작은 소품은 터짐이 닿을 때 한 번에 숨긴다.
	// 왜 그런지는 .cpp 의 TickRegionCollapse 주석 참고.
	struct FPGCollapseBatch
	{
		TWeakObjectPtr<UInstancedStaticMeshComponent> Component; // 원본. 옮긴 뒤에는 컴포넌트 정리에만 쓴다
		TWeakObjectPtr<UInstancedStaticMeshComponent> Mover;     // 붕괴 동안 실제로 그려지고 움직이는 임시 ISM(큰 조각만)
		TArray<int32> Instances;    // 원본에서의 번호. 옮기거나 숨기기 전까지만 유효하다
		TArray<FTransform> Start;
		// Mover 에 한 번에 넘기는 이번 갱신의 트랜스폼(Start 로 시작). 배열 하나를 통째로 넘겨야 갱신이 컴포넌트당 한 번이다.
		TArray<FTransform> Work;
		// 중심에서 얼마나 떨어져 있나(0 = 한가운데, 1 = 가장자리).
		// 꺼지는 순서와 안쪽으로 미끄러지는 양에 쓴다.
		TArray<float> Spread;
		// 잔해 시스템으로 이미 날린 조각(1 = 날림). 날린 조각은 Mover 안에서 땅속 깊이 숨겨 두고 마지막에 같이 지운다 —
		// 도중에 인스턴스를 지우면 남은 번호가 밀려 다른 조각을 잘못 움직인다.
		TArray<uint8> Flung;
		float MaxSpread = 0.0f;    // 제일 바깥 조각. 큰 묶음은 이 값이 큰 것부터 옮긴다(떨림이 가장자리부터 시작하므로)
		float MinSpread = 0.0f;    // 제일 안쪽 조각. 작은 묶음은 터짐이 여기 닿는 순간 숨긴다(한가운데부터 번지므로)
		float LastAlpha = 0.0f;    // 이 묶음이 마지막으로 갱신된 진행도. "막 터진 조각" 판정은 묶음마다 따로 한다(갱신 시각이 다르므로)
		float NextUpdateAt = 0.0f; // 다음 갱신 시각(붕괴 경과초). 30Hz, 묶음 절반은 반 박자 어긋나 있다
		bool bWholeComponent = false; // 이 컴포넌트가 통째로 붕괴 구역 안이면 컴포넌트째 없앤다
		// 이 묶음의 메시가 잔해로 날려도 되는 크기·종류인가(돌·상자 같은 작은 소품만. 20m 바닥판·나무는 아니오).
		bool bDebrisEligible = false;
		bool bSmall = false; // 작은 소품(바운드 반지름 1.5m 미만): 움직이지 않는다. 터짐이 닿으면 한 번에 숨긴다
		bool bMoved = false; // 큰 묶음: Mover 로 옮겨졌다 / 작은 묶음: 숨겨졌다
	};

	void CollapseRegion(const FVector& WorldCentre, float RadiusCm, float Seconds = 3.0f);
	void SweepCollapsedAreas();
	// MinFloorHalfCm: 바닥판을 최소 이만큼(중심에서 반 너비) 깐다 — 둘레 바닥판(치마)이 원에 걸쳐 같이 꺼지면 그 자리까지 덮어야 한다.
	void BuildCollapsePit(const FVector& WorldCentre, const TSet<FIntPoint>& Cells);
	// CollapseRegion 의 단계(9/28 나눔): 무너질 칸 고르기 / 떨어질 조각 모으기 / 그 위 것 치우기.
	TSet<FIntPoint> SelectCollapseCells(const FVector2D& Centre2D, double RadiusSq, TArray<bool>& OutFacilityInside) const;
	int32 GatherCollapseBatches(const FVector& WorldCentre, const TSet<FIntPoint>& Cells, double RadiusSq, double ReachSq, float SafeRadius,
		int32& OutSkippedByBounds, float& OutSkirtFloorHalf);
	void ClearCollapsedContents(const FVector& WorldCentre, const TSet<FIntPoint>& Cells, const TArray<bool>& FacilityInside,
		int32& OutFacilities, int32& OutActors, int32& OutCleared);
	void SpawnCollapseDust(const FVector& Where, float Scale);
	// 큰 묶음을 Mover(임시 ISM)로 옮기고 원본에서는 지운다. 원본이 이미 사라졌으면 false.
	bool MoveCollapseBatch(FPGCollapseBatch& Batch);
	// 작은 묶음을 한 번에 숨긴다(원본에서 지운다). 사라지기 전에 몇 개는 잔해로 날린다.
	void HideCollapseBatch(FPGCollapseBatch& Batch, int32& FlungThisFrame);
	void TickRegionCollapse(float DeltaSeconds);
	void FinishRegionCollapse();

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;

	// 앞쪽 [0, CollapseLargeCount) 가 큰 묶음(MaxSpread 내림차순), 그 뒤가 작은 묶음(MinSpread 오름차순).
	// 둘 다 "차례" 가 배열 순서로 오므로 커서 하나씩으로 훑는다.
	TArray<FPGCollapseBatch> CollapseBatches;
	int32 CollapseLargeCount = 0;
	int32 CollapseMoveCursor = 0;
	int32 CollapseHideCursor = 0;
	int32 CollapseAnimatedInstances = 0;
	int32 CollapseSmallInstances = 0;
	int32 CollapseSkirtPlanes = 0;
	float CollapseElapsed = 0.0f;
	float CollapseDuration = 3.0f;
	float CollapseDepth = 6000.0f;
	// 가장자리 조각이 한가운데보다 얼마나 늦게 꺼지나(터지는 구간 대비).
	// 0 이면 원판이 통째로 내려가 "승강기"로 보인다. 자세한 이유는 .cpp 의 TickRegionCollapse 주석.
	float CollapseStagger = 0.45f;
	// 전체 시간 중 앞부분 이만큼은 "떨림"이다: 아무것도 안 꺼지고 가장자리부터 잘게 흔들린다(금이 가는 소리 대신 눈에 보이는 경고).
	float CollapseTremorFraction = 0.25f;
	// 안쪽으로 미끄러뜨리려면 매 프레임 중심이 필요하다. CollapseRegion 이 채운다.
	FVector CollapseCentre = FVector::ZeroVector;
	// 잔해·먼지 예산(4060 8GB 프레임 방어). 한 판의 붕괴에서 총 몇 개, 한 프레임에 몇 개까지.
	int32 CollapseDebrisBudget = 18;
	int32 CollapseDebrisPerFrame = 2;
	int32 CollapseDebrisLaunched = 0;
	int32 CollapseDustBudget = 36;
	int32 CollapseDustSpawned = 0;
	double CollapseNextDustTime = 0.0;
	bool bCollapseLoggedBreakup = false;
	// 붕괴 동안 액터 틱 간격을 0 으로 바꾼다(평소 0.1초 — 그대로 두면 연출이 10Hz 슬라이드가 된다). 끝나면 돌려놓는다.
	float CollapseSavedTickInterval = 0.1f;
	// 성능 측정(FPlatformTime). 붕괴 동안 프레임 시간과 이 코드 자체의 시간을 재서 끝날 때 한 줄로 찍는다.
	double CollapseLastFrameAt = 0.0;
	double CollapseFrameTimeSum = 0.0;
	double CollapseFrameTimeMax = 0.0;
	double CollapseTickCostSum = 0.0;
	double CollapseTickCostMax = 0.0;
	int32 CollapseFrameCount = 0;
	int32 CollapseMovesDone = 0;
	int32 CollapseHidesDone = 0;
	// 구덩이: 무너진 자리 밑에 까는 산 재질의 바닥판 + 둘레의 비스듬한 벽판. 하늘이 뚫려 보이지 않게.
	float CollapsePitDepthCm = 4000.0f;
	UPROPERTY(Transient)
	TObjectPtr<UHierarchicalInstancedStaticMeshComponent> CollapsePitHISM;
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystem> CollapseDustEffect;
	// 큰 조각을 옮겨 담은 임시 ISM 들. 붕괴가 끝나면 통째로 버린다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInstancedStaticMeshComponent>> CollapseMovers;
};
