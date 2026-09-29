// AWarZoneFootprintPreview — 구역 붕괴 — 땅이 무너져 꺼지는 연출과 구덩이.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"
#include "HAL/IConsoleManager.h"
#include "PGRegionCollapse.h"

// ---- 구역 붕괴 ----
//
// 무엇: 월드의 한 원 안에 있는 타일·소품·시설을 **박살내** 땅 밑으로 꺼뜨린 뒤 완전히 지우고, 그 자리에 구덩이를 깐다.
//   순서(전체 Seconds 초): ① 떨림 — 가장자리부터 잘게 흔들린다(앞 25%). ② 터짐 — 한가운데부터 바깥으로 번지며
//   조각이 기울고, 일부는 6~10m 튀어 올랐다 떨어지고, 작은 소품 몇 개는 잔해 시스템으로 날아가며, 흙먼지가 여기저기 오른다.
//   ③ 지우기 — 다 꺼지면 인스턴스를 지운다. 구덩이 바닥·벽은 ①에서 미리 깔아 두므로(타일 밑에 숨어 있다) 하늘이 뚫려 보이는 프레임이 없다.
//
// 왜 이렇게 만들었나 (사용자 9/20: "타일맵 외곽쪽을 아예 박살내버리고 그 타일에 있던 것들 다 바닥으로
//   꺼지면서 … 밑으로 같이 꺼져버리는 연출 보이며 사라지면 메모리 아낄 수 있는 거 아닌가?"):
//   맞다. 인스턴스를 지우면 HISM 버퍼가 실제로 줄고, 시설 레벨을 내리면 그 레벨의 액터가 통째로 사라진다.
//   공중전 구간에는 아무도 지상에 없으므로 외곽을 들고 있을 이유가 없다.
//
// 왜 기존 넉백(TryKnockProp)을 안 쓰나: 지면 HISM 에는 전부 PGTerrain 태그가 붙어 있어서 원리상 거부된다
//   (로봇이 지나간 자리마다 바닥이 꺼지던 사고 때문에 일부러 막아 둔 것이다). 게다가 넉백은 인스턴스 하나당
//   대리 물리 액터를 하나씩 만드는데, 여기서 지울 것은 수천 개라 그 길로는 프레임이 남아나지 않는다.
//   그래서 "물리 없이 트랜스폼만 내리는" 별도 경로를 쓴다.
//
// 왜 셀 좌표가 아니라 월드 위치로 고르나: 이 프로젝트에는 셀 → 인스턴스 번호 역색인이 없다
//   (타일 액터는 HISM 으로 흡수한 뒤 파괴한다). 남은 단서는 인스턴스의 월드 트랜스폼뿐이다.
//
// 성능(9/22, 4060 8GB 기준 — 9/21 PIE 로그: 100m 원 안에 인스턴스 14,560개·108묶음, 붕괴 중 20~33fps, 평소 66fps):
//   비용의 정체는 "원본 HISM 의 인스턴스를 하나씩 UpdateInstanceTransform 으로 움직이는 것" 이었다. 엔진 5.6 소스로 확인한 그 한 번의 값:
//   ① 월드→로컬 변환 + 행렬 만들기, ② 충돌이 있는 묶음(지면·돌·나무·소품)은 인스턴스마다 Chaos 물리 몸을 옮김,
//   ③ HISM 은 위치가 바뀌면 컬링 트리를 비동기로 다시 짓기 시작하고(BuildTreeIfOutdated), 다음 갱신이 그 결과를 무효화해 또 짓는다 —
//      108개 컴포넌트가 3초 내내 트리를 짓고 버리기를 반복했다. 게다가 트리는 원 안의 조각만이 아니라 그 컴포넌트의 **모든** 인스턴스(덤불 수천 개)를 다룬다.
//   ④ 컴포넌트마다 렌더 상태를 통째로 다시 만듦(bMarkRenderStateDirty).
//   그래서 지금은 원본을 움직이지 않는다:
//   - 작은 소품(바운드 반지름 1.5m 미만: 풀·잔가지·작은 돌·상자)은 아예 움직이지 않고, 터짐이 닿는 순간 한 번에 지운다(먼지 속). 수로는 대부분이 여기서 빠진다.
//   - 큰 조각(20m 바닥판·도로·나무·바위·치마판)만 움직이되, 차례가 오면 충돌·길찾기·트리가 없는 임시 ISM(Mover)으로 옮기고
//     원본에서는 지운다. Mover 는 배열 하나로 한 번에 갱신하고(BatchUpdateInstancesTransforms), 렌더 상태를 다시 만들지 않고
//     바뀐 인스턴스만 프레임 끝에 보낸다(MarkRenderInstancesDirty — 5.6 ISM 의 증분 갱신 경로).
//   - 갱신은 30Hz, 묶음 절반은 반 박자 어긋나게(60fps 면 프레임마다 절반씩). 멀리서 보는 연출이라 눈에 안 띈다.
//   - 옮기기·숨기기는 프레임당 몇 묶음씩 나눠 한다(시작 프레임에 컴포넌트 100여 개 생성·지우기가 몰리지 않게).
//   - 이 액터의 틱은 평소 0.1초 간격이다. 붕괴 동안만 0 으로 바꾼다 — 9/21 로그의 "tremor over at 0.83s" 는 0.75초짜리 떨림을
//     0.1초 단위로 넘긴 흔적이고, 연출도 그 동안 10Hz 슬라이드였다.
namespace
{
	// 이 파일 고유 접두어(WZFP_). 유니티 빌드에서 다른 .cpp 의 익명 namespace 상수와 이름이 겹친 사고가 있었다.
	//
	// 이보다 작은 것(바운드 구 반지름, 월드 기준)은 움직이지 않는다. 풀·잔가지·작은 돌 — 멀리서 보는 연출이라 개별 낙하가 보이지 않는다.
	constexpr float WZFP_CollapseSmallRadiusCm = 150.0f;
	// 큰 조각 갱신 주기. 묶음 절반은 반 박자 어긋나 있어 60fps 에서는 프레임마다 절반씩 갱신된다.
	constexpr float WZFP_CollapseUpdateHz = 30.0f;
	// 한 프레임에 옮기는 큰 묶음 수 / 숨기는 작은 묶음 수. 시작 프레임에 100여 묶음의 컴포넌트 생성·지우기가 몰리지 않게.
	// 9/28: 콘솔 PG.Collapse.MovesPerFrame 로 바꿀 수 있게(드래곤 등장 직후 프레임 드랍 재기). 한 묶음 = 새 인스턴스 메시 하나라
	//   그리기 쪽 준비 비용이 계산(1ms)과 따로 든다.
	//   12 → 3(9/28 화면 그리는 시험: 가장 긴 프레임 114ms → 63ms, 평균 67 → 89fps). 70묶음이면 약 0.3초 — 흔들림(0.75초) 안에 끝난다.
	TAutoConsoleVariable<int32> CVarCollapseMovesPerFrame(TEXT("PG.Collapse.MovesPerFrame"), 3,
		TEXT("How many collapse batches move to their own instanced mesh per frame (lower = smoother, slower start)."));
	constexpr int32 WZFP_CollapseHidesPerFrame = 16;
	// 한 틱에 진행할 수 있는 최대 시간. 드래곤 메시 로딩 끊김(9/21 로그: 같은 프레임에서 0.3~0.5초)이 첫 틱의 DeltaSeconds 에
	// 섞여 들어와 떨림 구간을 통째로 건너뛰던 것을 막는다. 끊김이 없는 프레임(33~50ms)에는 아무 영향이 없다.
	constexpr float WZFP_CollapseMaxStepSeconds = 0.1f;

	// 월드 위치 → 맵 칸(20m). 무너질 칸 고르기·조각 모으기·치우기가 같이 쓴다.
	FIntPoint WZFP_CellOf(const FVector& World)
	{
		return FIntPoint(FMath::RoundToInt(World.X / DesignCellSize), FMath::RoundToInt(World.Y / DesignCellSize));
	}

	// 원본 묶음에서 인스턴스를 지운다. 묶음(HISM)이면 컬링 트리를 그 자리에서 바로 다시 짓는다.
	// 왜(9/28 사용자 PIE: "드래곤 등장할 때 타일 몇 개가 구멍 뚫린 것처럼 전 맵에 잠깐 보였다가 원래대로 돌아온다"):
	//   엔진 5.6 의 HISM::RemoveInstances 는 지운 자리에 맨 뒤 인스턴스를 옮겨 채우고(RemoveAtSwap) 트리를 **비동기로** 다시 짓는다.
	//   다 지어질 때까지 몇 프레임 동안 화면은 옛 트리로 그려서, 옮겨진 인스턴스가 제자리에 안 보인다. 지면 묶음은 맵 전체 한 장이라
	//   구덩이와 먼 곳의 땅판이 잠깐 뚫려 보였다. 에디터만의 일이 아니다(완성본도 같은 코드).
	//   트리를 바로 지으면 그 틈이 없다. 지면 900장·소품 구역 묶음 정도라 짓는 시간은 perf 줄의 tree 값으로 잰다.
	double GWZFP_TreeBuildSeconds = 0.0;
	void WZFP_RemoveFromSource(UInstancedStaticMeshComponent* Source, const TArray<int32>& Instances, bool bWholeComponent)
	{
		if (bWholeComponent)
		{
			Source->ClearInstances();
			return;
		}
		UHierarchicalInstancedStaticMeshComponent* Hism = Cast<UHierarchicalInstancedStaticMeshComponent>(Source);
		if (!Hism)
		{
			Source->RemoveInstances(Instances);
			return;
		}
		const bool bAutoRebuild = Hism->bAutoRebuildTreeOnInstanceChanges;
		Hism->bAutoRebuildTreeOnInstanceChanges = false;
		Hism->RemoveInstances(Instances);
		Hism->bAutoRebuildTreeOnInstanceChanges = bAutoRebuild;
		const double Start = FPlatformTime::Seconds();
		Hism->BuildTreeIfOutdated(/*Async*/false, /*ForceUpdate*/true);
		GWZFP_TreeBuildSeconds += FPlatformTime::Seconds() - Start;
	}
}

void UPGRegionCollapse::CollapseRegion(const FVector& WorldCentre, float RadiusCm, float Seconds)
{
	if (Map->bCollapsing)
		return; // 한 번에 한 구역만. 겹쳐 돌리면 같은 인덱스를 두 번 지운다.
	const double StartedAt = FPlatformTime::Seconds();
	CollapseBatches.Reset();
	CollapseMovers.Reset();
	CollapseLargeCount = 0;
	CollapseMoveCursor = 0;
	CollapseHideCursor = 0;
	CollapseAnimatedInstances = 0;
	CollapseSmallInstances = 0;
	CollapseSkirtPlanes = 0;
	CollapseElapsed = 0.0f;
	CollapseDuration = FMath::Max(Seconds, 0.5f);
	CollapseCentre = WorldCentre; // 매 프레임 안쪽으로 미끄러뜨릴 때 쓴다
	CollapseDebrisLaunched = 0;
	CollapseDustSpawned = 0;
	CollapseNextDustTime = 0.0;
	bCollapseLoggedBreakup = false;
	CollapseLastFrameAt = 0.0;
	CollapseFrameTimeSum = 0.0;
	CollapseFrameTimeMax = 0.0;
	CollapseTickCostSum = 0.0;
	CollapseTickCostMax = 0.0;
	CollapseFrameCount = 0;
	CollapseMovesDone = 0;
	CollapseHidesDone = 0;
	// 구덩이 바닥 바로 밑까지만 꺼뜨린다. 바닥판을 지나 한참 더 내려갈 필요가 없다 — 판 밑으로 사라진 뒤 지운다.
	CollapseDepth = CollapsePitDepthCm + 1500.0f;
	// 흙먼지: 잔해 시스템·드래곤과 같은 것을 쓴다(한 번만 읽어 둔다).
	if (!IsValid(CollapseDustEffect))
	{
		// 에셋은 맵 에셋 묶음(DA_PGMapVisuals, 없으면 원래 코드 에셋)에서(9/23 블루프린트 분리).
		const TSoftObjectPtr<UParticleSystem>& Dust = UPGMapVisualSet::GetActive()->CollapseDust;
		CollapseDustEffect = Dust.IsNull() ? nullptr : Dust.LoadSynchronous();
	}
	const double RadiusSq = static_cast<double>(RadiusCm) * RadiusCm;
	const FVector2D Centre2D(WorldCentre.X, WorldCentre.Y);

	TArray<bool> FacilityInside;
	const TSet<FIntPoint> Cells = SelectCollapseCells(Centre2D, RadiusSq, FacilityInside);
	// 가장 먼 칸의 바깥 모서리까지 = 조각 차례(가운데부터 번짐)를 0~1 로 잴 때의 1.
	float Reach = 1.0f;
	for (const FIntPoint& Cell : Cells)
		Reach = FMath::Max(Reach, static_cast<float>(FVector2D::Distance(FVector2D(Cell.X * DesignCellSize, Cell.Y * DesignCellSize), Centre2D)) + DesignCellSize * 0.71f);
	const double ReachSq = static_cast<double>(Reach) * Reach;
	const float SafeRadius = Reach;
	Map->CollapsedCells.Append(Cells);

	int32 SkippedByBounds = 0;
	float SkirtFloorHalf = 0.0f;
	const int32 Total = GatherCollapseBatches(WorldCentre, Cells, RadiusSq, ReachSq, SafeRadius, SkippedByBounds, SkirtFloorHalf);
	const double ScanDoneAt = FPlatformTime::Seconds();

	int32 Facilities = 0;
	int32 Actors = 0;
	int32 Cleared = 0;
	ClearCollapsedContents(WorldCentre, Cells, FacilityInside, Facilities, Actors, Cleared);
	const double ActorsDoneAt = FPlatformTime::Seconds();

	// 구덩이 바닥·벽을 깐다.
	//
	// 9/20 에는 "밑에 판때기 없애자" 로 낭떠러지로 뒀는데, 무너진 뒤 그 자리로 아래 하늘(하늘색)이 뚫려 보였다
	// (사용자 9/21: "드래곤 등장하고 난 다음에 저 지면 바닥이 보이는 건 어쩔 수 없나?"). 그때 어색했던 것은 땅 높이의
	// 회색 판이었지, 바닥이 있다는 것 자체가 아니다. 이번엔 **산과 같은 재질**의 판을 **40m 아래**에 깔고 둘레를 비스듬한
	// 벽으로 둘러서 "파인 구덩이"로 읽히게 한다. 지금 깔아도 타일 밑에 숨어 있다가 조각이 꺼지며 드러난다.
	int32 PitPlanes = 0;
	if (Total > 0)
	{
		BuildCollapsePit(WorldCentre, Cells);
		PitPlanes = IsValid(CollapsePitHISM) ? CollapsePitHISM->GetInstanceCount() : 0;
	}
	const double PitDoneAt = FPlatformTime::Seconds();

	int32 Eligible = 0;
	for (const FPGCollapseBatch& Batch : CollapseBatches)
		if (Batch.bDebrisEligible)
			Eligible += Batch.Instances.Num();

	Map->bCollapsing = Total > 0;
	if (Map->bCollapsing)
	{
		Map->CollapsedAreas.Add(FVector4(WorldCentre.X, WorldCentre.Y, WorldCentre.Z, RadiusCm));
		// 붕괴 동안은 매 프레임 틱. 이 액터는 평소 0.1초 간격이라(EnforceMapBoundary 등이 그 전제) 그대로 두면 연출이
		// 10Hz 슬라이드가 된다. 틱의 다른 일(경계 되밀기·연료통 리스폰)은 3초 동안 더 자주 돌아도 해가 없다. FinishRegionCollapse 가 되돌린다.
		CollapseSavedTickInterval = Map->GetActorTickInterval();
		Map->SetActorTickInterval(0.0f);
		CollapseLastFrameAt = FPlatformTime::Seconds();
	}
	UE_LOG(LogTemp, Display, TEXT("PGCollapse: %d instances in %d batches, %d facility levels, %d facility actors, %d props/npcs in %d cells (cell centres within %.0fm of %s)"),
		Total, CollapseBatches.Num(), Facilities, Actors, Cleared, Cells.Num(), RadiusCm * 0.01f, *WorldCentre.ToCompactString());
	UE_LOG(LogTemp, Display, TEXT("PGCollapse: plan - tremor %.2fs then break-up %.2fs, %d props eligible for debris (budget %d, %d/frame), dust budget %d, pit %d planes %.0fm deep, dust fx %s"),
		CollapseDuration * CollapseTremorFraction, CollapseDuration * (1.0f - CollapseTremorFraction), Eligible,
		CollapseDebrisBudget, CollapseDebrisPerFrame, CollapseDustBudget, PitPlanes, CollapsePitDepthCm * 0.01f,
		IsValid(CollapseDustEffect) ? TEXT("ok") : TEXT("missing"));
	// 다음 PIE 한 판으로 효과를 숫자로 볼 수 있게: 무엇을 움직이고 무엇을 그냥 지우는지, 시작 프레임에 든 시간.
	UE_LOG(LogTemp, Display, TEXT("PGCollapse: perf - %d instances animated in %d batches (moved to plain ISMs, %d/frame), %d small hidden in %d batches (%.0f%% of all, radius < %.1fm, %d/frame), skirt %d planes, update %.0fHz, start frame: scan %.1f ms (%d components skipped by bounds), actors %.1f ms, pit %.1f ms"),
		CollapseAnimatedInstances, CollapseLargeCount, CVarCollapseMovesPerFrame.GetValueOnGameThread(),
		CollapseSmallInstances, CollapseBatches.Num() - CollapseLargeCount,
		Total > 0 ? 100.0f * CollapseSmallInstances / Total : 0.0f, WZFP_CollapseSmallRadiusCm * 0.01f, WZFP_CollapseHidesPerFrame,
		CollapseSkirtPlanes, WZFP_CollapseUpdateHz,
		(ScanDoneAt - StartedAt) * 1000.0, SkippedByBounds, (ActorsDoneAt - ScanDoneAt) * 1000.0, (PitDoneAt - ActorsDoneAt) * 1000.0);
	if (CollapseSkirtPlanes > 0)
		UE_LOG(LogTemp, Display, TEXT("PGCollapse: skirt - %d planes dropped (pit floor widened to +-%.0fm)"), CollapseSkirtPlanes, SkirtFloorHalf * 0.01f);
}

// ---- CollapseRegion 의 단계들 (9/28 나눔 — 404줄 한 함수를 칸 고르기 / 떨어질 조각 모으기 / 그 위 것 치우기로. 동작 그대로) ----

// 무너질 칸을 고른다. OutFacilityInside[i] = i 번째 시설을 통째로 무너뜨리나.
TSet<FIntPoint> UPGRegionCollapse::SelectCollapseCells(const FVector2D& Centre2D, double RadiusSq, TArray<bool>& OutFacilityInside) const
{
	// ---- 무너질 칸 고르기 (9/28) ----
	// 원 안의 것을 고르던 것을 "원 안에 한가운데가 든 칸(20m 타일)" 으로 바꿨다. 사용자 9/28: "드래곤 나타난 곳 왜 원으로 보이냐?
	//   그냥 타일만 부서져도 충분한 거 아닌가? 나타날 때 걸리는 위 타일·오브젝트 다 없애 버리면 되잖아."
	//   원으로 고르면 한가운데가 원 안인 타일은 통째로 꺼지는데 구덩이 벽은 둥글게 서서, 네모 구멍과 둥근 벽이 어긋나 보였다.
	//   이제 칸에 든 것(타일·소품·시설·몹)만 없애고, 구덩이 바닥·벽도 칸 경계를 따라 곧게 깐다(BuildCollapsePit).
	// 시설은 쪼개지 않는다: 시설 한가운데가 원 안이면 발자국 전체를 넣고, 아니면 발자국 전체를 뺀다(건물 반쪽이 구멍 위에 뜨지 않게).
	// 맵 칸만 고른다(TileDesignPlacements) — 맵 둘레 바닥판(치마)·바깥 산은 그대로 둔다. 구멍이 맵 끝에서 곧은 벽으로 끝난다.
	auto CellCentreInside = [&Centre2D, RadiusSq](const FIntPoint& Cell)
	{
		return FVector2D::DistSquared(FVector2D(Cell.X * DesignCellSize, Cell.Y * DesignCellSize), Centre2D) <= RadiusSq;
	};
	TSet<FIntPoint> Cells;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		if (CellCentreInside(Placement.GridCell))
			Cells.Add(Placement.GridCell);
	TArray<bool>& FacilityInside = OutFacilityInside;
	FacilityInside.Init(false, Map->FacilityPlacements.Num());
	for (int32 Index = 0; Index < Map->FacilityPlacements.Num(); ++Index)
	{
		const FFacilityPlacement& Facility = Map->FacilityPlacements[Index];
		if (Facility.OccupiedCells.IsEmpty())
			continue;
		FVector2D Mean = FVector2D::ZeroVector;
		for (const FIntPoint& Cell : Facility.OccupiedCells)
			Mean += FVector2D(Cell.X * DesignCellSize, Cell.Y * DesignCellSize);
		Mean /= Facility.OccupiedCells.Num();
		FacilityInside[Index] = FVector2D::DistSquared(Mean, Centre2D) <= RadiusSq;
		for (const FIntPoint& Cell : Facility.OccupiedCells)
		{
			if (FacilityInside[Index])
				Cells.Add(Cell);
			else
				Cells.Remove(Cell);
		}
	}
	// 구덩이 안에 갇힌 칸 메우기 (9/28).
	// 왜: 사용자 9/28 "용 나타났는데 저거 중간에 안 깨진 나무와 타일은 뭐냐?" — 구덩이 한가운데 칸 하나가 네 면 벽에 둘러싸인 기둥으로 남았다.
	//   위에서 칸을 "맵 타일 칸" 과 "시설 발자국" 으로만 고르다 보니, 둘 다에 안 걸린 칸(또는 한가운데가 원 밖이라 빠진 시설 칸)이
	//   둘레가 모두 무너진 칸이어도 혼자 남았다. 벽은 "옆 칸이 안 무너졌으면 세운다" 라서 그 칸 네 면에 벽이 섰다.
	// 방법: 고른 칸들을 감싼 네모보다 한 칸 넓게 바깥에서부터 "안 무너지는 칸" 을 따라 번져 나간다. 바깥에서 닿지 못한 안 무너지는 칸은
	//   구덩이에 갇힌 것이니 무너질 칸에 넣는다. 그 칸이 시설 발자국이면 시설을 쪼개지 않게 발자국 전체를 넣고 다시 본다.
	int32 FilledHoles = 0;
	for (int32 Pass = 0; Pass < 4 && Cells.Num() > 0; ++Pass)
	{
		FIntPoint Min(TNumericLimits<int32>::Max(), TNumericLimits<int32>::Max());
		FIntPoint Max(TNumericLimits<int32>::Lowest(), TNumericLimits<int32>::Lowest());
		for (const FIntPoint& Cell : Cells)
		{
			Min = FIntPoint(FMath::Min(Min.X, Cell.X - 1), FMath::Min(Min.Y, Cell.Y - 1));
			Max = FIntPoint(FMath::Max(Max.X, Cell.X + 1), FMath::Max(Max.Y, Cell.Y + 1));
		}
		TSet<FIntPoint> Outside;
		TArray<FIntPoint> Open;
		Open.Add(Min);
		Outside.Add(Min);
		while (Open.Num() > 0)
		{
			const FIntPoint Cell = Open.Pop(EAllowShrinking::No);
			for (const FIntPoint Step : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const FIntPoint Next = Cell + Step;
				if (Next.X < Min.X || Next.Y < Min.Y || Next.X > Max.X || Next.Y > Max.Y || Cells.Contains(Next) || Outside.Contains(Next))
					continue;
				Outside.Add(Next);
				Open.Add(Next);
			}
		}
		bool bAddedFacility = false;
		for (int32 X = Min.X; X <= Max.X; ++X)
		{
			for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
			{
				const FIntPoint Cell(X, Y);
				if (Cells.Contains(Cell) || Outside.Contains(Cell))
					continue;
				Cells.Add(Cell);
				++FilledHoles;
				for (int32 Index = 0; Index < Map->FacilityPlacements.Num(); ++Index)
				{
					if (FacilityInside[Index] || !Map->FacilityPlacements[Index].OccupiedCells.Contains(Cell))
						continue;
					FacilityInside[Index] = true;
					Cells.Append(Map->FacilityPlacements[Index].OccupiedCells);
					bAddedFacility = true;
				}
			}
		}
		// 시설을 통째로 넣었으면 그 바깥에 새로 갇힌 칸이 생길 수 있어 한 번 더 본다.
		if (!bAddedFacility)
			break;
	}
	if (FilledHoles > 0)
		UE_LOG(LogTemp, Display, TEXT("PGCollapse: filled %d cells enclosed by the pit (not a tile cell or cut from a facility)"), FilledHoles);
	return Cells;
}

// 떨어질 조각(인스턴스)을 묶음별로 모아 CollapseBatches 에 넣고 차례대로 정렬한다. 돌려주는 값 = 조각 수.
int32 UPGRegionCollapse::GatherCollapseBatches(const FVector& WorldCentre, const TSet<FIntPoint>& Cells, double RadiusSq, double ReachSq, float SafeRadius,
	int32& OutSkippedByBounds, float& OutSkirtFloorHalf)
{
	const FVector2D Centre2D(WorldCentre.X, WorldCentre.Y);
	auto InCollapsedCell = [&Cells](const FVector& World) { return Cells.Contains(WZFP_CellOf(World)); };
	TArray<UInstancedStaticMeshComponent*> Candidates;
	auto AddCandidate = [&Candidates](UInstancedStaticMeshComponent* Component)
	{
		// 같은 컴포넌트가 두 번 들어오면 같은 번호를 두 번 지우므로 겹치지 않게 넣는다.
		if (IsValid(Component) && Component->GetInstanceCount() > 0)
			Candidates.AddUnique(Component);
	};
	AddCandidate(Map->GroundHISM);
	AddCandidate(Map->WarZoneGroundHISM);
	AddCandidate(Map->TransitionGroundHISM);
	AddCandidate(Map->RoadSurfaceHISM);
	AddCandidate(Map->LakeBedHISM);
	AddCandidate(Map->LakeWaterHISM);
	AddCandidate(Map->TerrainRockHISM);
	AddCandidate(Map->TerrainTreeHISM);
	AddCandidate(Map->TerrainBushHISM);
	AddCandidate(Map->ShoreRockHISM);
	AddCandidate(Map->ShoreReedHISM);
	for (UHierarchicalInstancedStaticMeshComponent* Feature : Map->TerrainFeatureHISMs)
		AddCandidate(Feature);
	for (UHierarchicalInstancedStaticMeshComponent* Shore : Map->ShoreTransitionHISMs)
		AddCandidate(Shore);
	for (UHierarchicalInstancedStaticMeshComponent* Packed : Map->RuntimePackedVisualHISMs)
		AddCandidate(Packed);
	// 산도 같이 무너뜨린다. 타일만 꺼지고 그 자리 산만 멀쩡히 서 있으면 더 이상해 보인다
	// (사용자 9/20: "그렇게 따지면 저 부근 산도 없어져야 되긴 하네").
	AddCandidate(Map->MountainHISM);
	// 맵 둘레 바닥판(치마)도 무너뜨린다. 드래곤은 맵 끝 가까이에서 솟아 붕괴 원이 치마에 걸치는데, 옆 타일은 부서지는데
	// 치마만 평평히 남아 있었다(9/22). 판은 크므로 원에 **걸치기만** 해도 고른다 — 중심만 보면 구덩이 위로 판 일부가
	// 평평하게 남아 구멍을 덮는다. 그만큼 구덩이 바닥을 넓힌다(아래 SkirtFloorHalf). BuildGroundSkirt 는 건드리지 않는다.
	// (9/28 칸 단위로 바꾸며 뺐다: AddCandidate(Map->GroundSkirtHISM). 치마판은 맵 칸 바깥이라 고를 칸이 없다.)
	// NavigationFloor 는 건드리지 않는다. 맵 전체가 한 장이라 여기서 구멍을 낼 방법이 없고,
	// 이 시점부터는 공중전이라 내비가 필요 없다.

	int32 Total = 0;
	int32& SkippedByBounds = OutSkippedByBounds;
	SkippedByBounds = 0;
	float& SkirtFloorHalf = OutSkirtFloorHalf;
	SkirtFloorHalf = 0.0f;
	for (UInstancedStaticMeshComponent* Component : Candidates)
	{
		const bool bSkirt = Component == Map->GroundSkirtHISM;
		// 컴포넌트 바운드가 원에 안 닿으면 인스턴스를 하나도 안 본다. 소품 묶음은 200m 구역 단위라 대부분 여기서 빠진다.
		// 단, 범위 상자가 비어 있으면(0 크기) 건너뛰지 않고 인스턴스를 하나씩 본다.
		// 왜(9/28): 화면을 그리지 않는 컴퓨터(전용 서버)에서는 묶음의 범위 상자가 끝까지 0 이었다(인스턴스 트리를 안 짓는다).
		//   그래서 서버에서는 드래곤 자리 땅이 하나도 안 무너졌다 — "0 instances, 605 components skipped by bounds".
		//   클라이언트 화면에는 구멍이 났지만 서버의 땅 충돌은 그대로 남아 있었다.
		if (!Component->Bounds.BoxExtent.IsNearlyZero())
		{
			const FBox Box = Component->Bounds.GetBox();
			const double Dx = FMath::Max(FMath::Max(Box.Min.X - WorldCentre.X, WorldCentre.X - Box.Max.X), 0.0);
			const double Dy = FMath::Max(FMath::Max(Box.Min.Y - WorldCentre.Y, WorldCentre.Y - Box.Max.Y), 0.0);
			if (Dx * Dx + Dy * Dy > ReachSq)
			{
				++SkippedByBounds;
				continue;
			}
		}
		const UStaticMesh* Mesh = Component->GetStaticMesh();
		const FVector MeshExtent = IsValid(Mesh) ? Mesh->GetBounds().BoxExtent : FVector::ZeroVector;
		const FTransform ComponentToWorld = Component->GetComponentTransform();
		FPGCollapseBatch Batch;
		Batch.Component = Component;
		const int32 Count = Component->GetInstanceCount();
		float MaxScale = 0.0f;
		float MinSpread = 1.0f;
		float MaxSpread = 0.0f;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			// 위치만 먼저 본다. GetInstanceTransform 은 행렬을 FTransform 으로 풀어(회전·크기 분해) 비싸므로 원 안에 든 것에만 부른다.
			// 남은 컴포넌트들은 맵 전체 것이라(지면 900장, 덤불 수천) 시작 프레임의 시간이 여기서 제일 많이 갔다.
			const FMatrix& Local = Component->PerInstanceSMData[Index].Transform;
			const FVector Location = ComponentToWorld.TransformPosition(Local.GetOrigin());
			// double 로 받는다. float 로 줄이면 C4244(경고=오류)가 난다.
			const double DistSq = FVector2D::DistSquared(FVector2D(Location.X, Location.Y), Centre2D);
			if (bSkirt)
			{
				// 판이 원에 걸치는가(축 정렬 네모 ↔ 원): 네모에서 원 중심에 가장 가까운 점까지의 거리로 본다.
				const FVector Scale = Local.GetScaleVector();
				const double HalfX = MeshExtent.X * FMath::Abs(Scale.X);
				const double HalfY = MeshExtent.Y * FMath::Abs(Scale.Y);
				const double OffX = FMath::Abs(Location.X - WorldCentre.X);
				const double OffY = FMath::Abs(Location.Y - WorldCentre.Y);
				const double Dx = FMath::Max(OffX - HalfX, 0.0);
				const double Dy = FMath::Max(OffY - HalfY, 0.0);
				if (Dx * Dx + Dy * Dy > RadiusSq)
					continue;
				// 이 판이 사라진 자리까지 구덩이 바닥이 덮어야 한다(판의 먼 끝까지).
				SkirtFloorHalf = FMath::Max(SkirtFloorHalf, static_cast<float>(FMath::Max(OffX + HalfX, OffY + HalfY)));
			}
			else if (!InCollapsedCell(Location))
				continue;
			FTransform Instance;
			if (!Component->GetInstanceTransform(Index, Instance, /*bWorldSpace*/true))
				continue;
			// 중심에서의 거리를 0~1 로 저장해 둔다. 꺼지는 차례와 미끄러지는 양이 여기서 나온다.
			const float Spread = FMath::Clamp(static_cast<float>(FMath::Sqrt(DistSq) / SafeRadius), 0.0f, 1.0f);
			Batch.Instances.Add(Index);
			Batch.Start.Add(Instance);
			Batch.Spread.Add(Spread);
			MinSpread = FMath::Min(MinSpread, Spread);
			MaxSpread = FMath::Max(MaxSpread, Spread);
			MaxScale = FMath::Max(MaxScale, static_cast<float>(Instance.GetScale3D().GetAbsMax()));
		}
		if (Batch.Instances.IsEmpty())
			continue;
		// 컴포넌트가 통째로 들어왔으면 인스턴스를 하나씩 지우는 대신 컴포넌트를 없앤다.
		// 소품 HISM 은 200m 청크로 쪼개져 있어서 외곽에서는 이 경우가 자주 나오고, 제일 싸다.
		Batch.bWholeComponent = Batch.Instances.Num() == Count;
		Batch.Flung.Init(0, Batch.Instances.Num());
		Batch.MinSpread = MinSpread;
		Batch.MaxSpread = MaxSpread;
		const bool bTerrain = Component->ComponentTags.Contains(PGPhysicsUtil::TerrainTag);
		const float RadiusWorld = IsValid(Mesh) ? Mesh->GetBounds().SphereRadius * MaxScale : 0.0f;
		// 작은 소품 판정. 땅(PGTerrain)과 치마판은 크기와 상관없이 움직인다 — 바닥이 소리 없이 사라지면 안 된다.
		// 풀·덤불은 크기와 상관없이 움직이지 않고 흙먼지 속에서 사라진다(9/28).
		// 왜: 사용자 9/28 "땅 조각 수천 개? 굳이 수천 개일 필요가 있어? 한 번에 뭉뚱그려서 타일 뽀개지게 하면 안 되나?" — 찍어 보니
		//   움직이던 8,058개 중 7,463개가 풀 덤불(SM_GrassPatch_*)이었고 타일 바닥판은 94장뿐이었다. 풀 카드 하나하나를 기울여 떨어뜨릴 이유가 없다.
		//   무너지는 모양은 바닥판·나무·바위·벽이 만든다.
		const FString MeshPath = IsValid(Mesh) ? Mesh->GetPathName() : FString();
		const bool bPlant = MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_")) || MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
			|| MeshPath.Contains(TEXT("/Foliage/Bush_")) || MeshPath.Contains(TEXT("/Foliage/Shrubs_")) || MeshPath.Contains(TEXT("/Foliage/Flower_Patch"))
			|| MeshPath.Contains(TEXT("/GV_FreeShrubsPack/"));
		Batch.bSmall = !bTerrain && !bSkirt && (bPlant || RadiusWorld < WZFP_CollapseSmallRadiusCm);
		// 잔해로 날려도 되는 묶음인가: 땅(PGTerrain 태그)·나무·큰 것은 아니오. 돌·상자·드럼통 같은 작은 소품만.
		// 왜 크기를 보나: 20m 바닥판이 하늘로 튀면 우스꽝스럽고, 잔해 시스템도 큰 판은 Chaos 에 못 올린다(NarrowPhase 폭발).
		if (IsValid(Mesh) && !bTerrain)
		{
			const FVector Scale = Batch.Start[0].GetScale3D();
			Batch.bDebrisEligible = RadiusWorld >= 40.0f && RadiusWorld <= 600.0f
				&& PGPhysicsUtil::IsKnockableMesh(Mesh, Scale) && !PGPhysicsUtil::IsTreeMesh(Mesh, Scale);
		}
		if (Batch.bSmall)
			CollapseSmallInstances += Batch.Instances.Num();
		else
		{
			CollapseAnimatedInstances += Batch.Instances.Num();
			Batch.Work = Batch.Start;
		}
		if (bSkirt)
			CollapseSkirtPlanes = Batch.Instances.Num();
		Total += Batch.Instances.Num();
		CollapseBatches.Add(MoveTemp(Batch));
	}
	// 차례대로 정렬: 큰 묶음(가장자리부터 옮긴다 — 떨림이 가장자리부터) 앞, 작은 묶음(한가운데부터 숨긴다 — 터짐이 한가운데부터) 뒤.
	// 무엇이 몇 개 움직이나(9/28 "땅 조각 수천 개? 굳이?" — 메시별로 움직이는 수·숨기는 수를 찍어 본다).
	{
		TMap<FString, FIntPoint> ByMesh; // X = 움직임, Y = 그냥 숨김
		for (const FPGCollapseBatch& Batch : CollapseBatches)
		{
			const UInstancedStaticMeshComponent* Source = Batch.Component.Get();
			const FString Name = Source && Source->GetStaticMesh() ? Source->GetStaticMesh()->GetName() : TEXT("?");
			FIntPoint& Row = ByMesh.FindOrAdd(Name);
			(Batch.bSmall ? Row.Y : Row.X) += Batch.Instances.Num();
		}
		ByMesh.ValueSort([](const FIntPoint& A, const FIntPoint& B) { return A.X + A.Y > B.X + B.Y; });
		FString Line;
		int32 Shown = 0;
		for (const TPair<FString, FIntPoint>& Pair : ByMesh)
		{
			if (++Shown > 12)
				break;
			Line += FString::Printf(TEXT(" %s=%d/%d"), *Pair.Key, Pair.Value.X, Pair.Value.Y);
		}
		UE_LOG(LogTemp, Display, TEXT("PGCollapse: by mesh (moved/hidden):%s"), *Line);
	}
	CollapseBatches.Sort([](const FPGCollapseBatch& A, const FPGCollapseBatch& B)
	{
		if (A.bSmall != B.bSmall)
			return !A.bSmall;
		return A.bSmall ? A.MinSpread < B.MinSpread : A.MaxSpread > B.MaxSpread;
	});
	for (const FPGCollapseBatch& Batch : CollapseBatches)
		if (!Batch.bSmall)
			++CollapseLargeCount;
	CollapseHideCursor = CollapseLargeCount;
	return Total;
}

// 무너진 칸 위의 시설 레벨·코드 시설·몹·물건을 치운다(플레이어·보호 표 붙은 것은 남김).
void UPGRegionCollapse::ClearCollapsedContents(const FVector& WorldCentre, const TSet<FIntPoint>& Cells, const TArray<bool>& FacilityInside,
	int32& OutFacilities, int32& OutActors, int32& OutCleared)
{
	auto InCollapsedCell = [&Cells](const FVector& World) { return Cells.Contains(WZFP_CellOf(World)); };
	// 그 구역에 스트리밍된 시설 레벨을 내린다. 여기가 메모리로는 가장 큰 덩어리다.
	int32& Facilities = OutFacilities;
	Facilities = 0;
	for (int32 Index = 0; Index < Map->FacilityDesignLevelInstances.Num(); ++Index)
	{
		ULevelStreamingDynamic* Level = Map->FacilityDesignLevelInstances[Index];
		if (!IsValid(Level) || !Map->FacilityPlacements.IsValidIndex(Index))
			continue;
		if (!FacilityInside[Index])
			continue;
		Level->SetShouldBeLoaded(false);
		Level->SetShouldBeVisible(false);
		++Facilities;
	}
	// 코드로 세운 시설 액터도 같이 없앤다.
	int32& Actors = OutActors;
	Actors = 0;
	for (int32 Index = Map->SpawnedRuntimeTiles.Num() - 1; Index >= 0; --Index)
	{
		AActor* Actor = Map->SpawnedRuntimeTiles[Index];
		if (!IsValid(Actor))
			continue;
		if (!InCollapsedCell(Actor->GetActorLocation()))
			continue;
		Actor->Destroy();
		Map->SpawnedRuntimeTiles.RemoveAt(Index);
		++Actors;
	}
	// 그 땅 위에 서 있던 몹과 물건도 같이 없앤다.
	//
	// 왜: 바닥만 꺼지고 그 위의 적과 상자가 허공에 그대로 떠 있으면 무너진 것으로 안 보인다
	//   (사용자 9/20: "타일이 깨졌는데 왜 저 위에 남은 몹들이랑 옵젝은 뭐야?").
	// 플레이어는 건드리지 않는다 — 이 시점이면 전함 위에 있지만, 만에 하나 지상에 있어도
	//   조종하던 캐릭터를 지워 버리면 판이 끝나 버린다.
	int32& Cleared = OutCleared;
	Cleared = 0;
	for (TActorIterator<AActor> It(Map->GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor == Map.Get() || Actor->IsA(APlayerController::StaticClass()))
			continue;
		// 멀티(9/27): 복제되는 액터(몹·상자·탈것)는 서버가 지우면 클라에서도 사라진다. 클라이언트는 제 것(로컬 소품)만 지운다.
		if (!Actor->HasAuthority())
			continue;
		if (const APawn* AsPawn = Cast<APawn>(Actor); AsPawn && AsPawn->IsPlayerControlled())
			continue;
		// 배·드래곤처럼 "보호 표"가 붙은 것은 건드리지 않는다(넉백 보호와 같은 표를 쓴다).
		// 단 탈출구 검문소는 예외 — 보호 표는 "차에 치여도 안 부서진다" 는 뜻이지 땅이 꺼져도 남으라는 뜻이 아니다.
		// 예전에는 이 표 때문에 무너진 구덩이 위에 펜스·EXIT 문이 허공에 떠 있었다(사용자 9/22).
		// 외진 보상 거점(PGRemoteOutpost)도 같은 이유로 예외 — 보호 표는 넉백 방지용이다. 안 지우면 헬기 잔해가 구덩이 위에 뜬다.
		// 거래소 부스(PGBooth)도 예외(9/23) — 평소엔 보호막·보호 표로 지키지만, 드래곤이 땅을 무너뜨리면 같이 사라진다(사용자 결정).
		if (Actor->Tags.Contains(PGPhysicsUtil::ProtectedTag) && !Actor->Tags.Contains(TEXT("PGExitDressing"))
			&& !Actor->Tags.Contains(TEXT("PGRemoteOutpost")) && !Actor->Tags.Contains(TEXT("PGBooth")))
			continue;
		const FVector Where = Actor->GetActorLocation();
		if (!InCollapsedCell(Where))
			continue;
		// 무너지는 땅에서 위아래로 80m 를 벗어난 것은 남긴다(날아다니는 드래곤·전함).
		//
		// 위쪽만 보던 것을 양쪽으로 바꿨다. 중심 높이가 한 번 잘못 들어오면 "그보다 아래" 가 맵 전체가 되어
		// 지상의 모든 것이 지워진다 — 실제로 부르는 쪽이 산 꼭대기를 땅으로 착각해 294m 공중을 중심으로
		// 넘겼고, 소품·몹 2577개가 한꺼번에 사라졌다(9/20 PIE). 띠로 묶으면 그런 사고가 한 구역에서 끝난다.
		if (FMath::Abs(Where.Z - WorldCentre.Z) > 8000.0f)
			continue;
		Actor->Destroy();
		++Cleared;
	}
}

// 큰 묶음을 Mover(임시 ISM)로 옮긴다. 원본에서는 지운다.
//
// 왜 옮기나: 원본은 HISM 이고 충돌·길찾기·컬링 트리를 갖고 있다. 인스턴스 하나를 움직이면 물리 몸을 옮기고 트리를 다시 짓는다
//   (위 CollapseRegion 주석 ①~④). Mover 는 평범한 ISM 에 충돌·길찾기·그림자 캐시 부담이 없어 "행렬 쓰고 프레임 끝에 한 번 보내기" 만 남는다.
// 왜 지금(차례가 왔을 때) 옮기나: 시작 프레임에 100여 묶음의 컴포넌트를 한꺼번에 만들지 않으려고. 떨림은 가장자리부터라
//   바깥 묶음부터 차례가 오고, 아직 차례가 안 온 묶음은 원본 그대로 서 있다(눈에 띄지 않는다).
// 원본 인스턴스 지우기는 컴포넌트당 한 번(RemoveInstances 는 번호를 뒤에서부터 지워 남은 번호가 안 밀린다).
//   통째로 들어온 컴포넌트는 비우기(ClearInstances)가 제일 싸다. 컴포넌트 자체는 FinishRegionCollapse 에서 정리한다.
bool UPGRegionCollapse::MoveCollapseBatch(FPGCollapseBatch& Batch)
{
	UInstancedStaticMeshComponent* Source = Batch.Component.Get();
	Batch.bMoved = true; // 원본이 사라졌더라도 다시 시도하지 않는다
	if (!IsValid(Source) || !IsValid(Source->GetStaticMesh()))
		return false;
	UInstancedStaticMeshComponent* Mover = NewObject<UInstancedStaticMeshComponent>(Map.Get(), NAME_None, RF_Transient);
	Mover->SetupAttachment(Map->GetRootComponent());
	Mover->SetMobility(EComponentMobility::Movable);
	Mover->SetStaticMesh(Source->GetStaticMesh());
	for (int32 Slot = 0; Slot < Source->GetNumMaterials(); ++Slot)
		Mover->SetMaterial(Slot, Source->GetMaterial(Slot));
	Mover->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mover->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Mover->SetCanEverAffectNavigation(false);
	Mover->SetGenerateOverlapEvents(false);
	// 떨어지는 조각은 그림자·간접광(거리장)에서 뺀다(9/28). 드래곤 등장 첫 1초에 GPU 가 83~88ms 까지 올랐다 —
	//   20m 땅판 수천 장이 움직이면 그 넓이만큼 그림자(VSM)와 Lumen 장면을 매 프레임 다시 그린다. 몇 초 안에 구덩이로 사라지는 조각이라 차이가 안 보인다.
	Mover->SetCastShadow(false);
	Mover->SetAffectDistanceFieldLighting(false);
	Mover->SetAffectDynamicIndirectLighting(false);
	Mover->SetCullDistances(Source->InstanceStartCullDistance, Source->InstanceEndCullDistance);
	Mover->RegisterComponent();
	Mover->AddInstances(Batch.Start, /*bShouldReturnIndices*/false, /*bWorldSpace*/true, /*bUpdateNavigation*/false);
	CollapseMovers.Add(Mover);
	Batch.Mover = Mover;
	WZFP_RemoveFromSource(Source, Batch.Instances, Batch.bWholeComponent);
	++CollapseMovesDone;
	return true;
}

// 작은 묶음을 한 번에 숨긴다(원본에서 지운다). 사라지기 전에 몇 개는 잔해로 날린다.
//
// 왜 안 움직이나: 풀·잔가지·작은 돌은 수천 개인데 멀리서 보는 연출이라 개별 낙하가 보이지 않는다. 터짐이 닿는 순간 먼지 속에서
//   사라지는 것으로 충분하다. 9/21 로그의 14,560개 중 대부분이 여기 든다(비율은 CollapseRegion 의 perf 로그).
// 왜 잔해는 남기나: "작은 소품은 잔해로 날아간다" 는 연출은 그대로 보이게 — 8개 중 1개를 예산 안에서 잔해 시스템에 넘긴다.
void UPGRegionCollapse::HideCollapseBatch(FPGCollapseBatch& Batch, int32& FlungThisFrame)
{
	UInstancedStaticMeshComponent* Source = Batch.Component.Get();
	Batch.bMoved = true;
	++CollapseHidesDone;
	if (!IsValid(Source))
		return;
	if (Batch.bDebrisEligible)
	{
		UPGDebrisSubsystem* Debris = UPGDebrisSubsystem::Get(Map->GetWorld());
		UStaticMesh* Mesh = Source->GetStaticMesh();
		for (int32 Slot = 0; IsValid(Debris) && IsValid(Mesh) && Slot < Batch.Instances.Num(); ++Slot)
		{
			if (FlungThisFrame >= CollapseDebrisPerFrame || CollapseDebrisLaunched >= CollapseDebrisBudget)
				break;
			const uint32 Noise = GetTypeHash(Batch.Instances[Slot] * 2654435761u);
			if (((Noise >> 12) & 7) != 0)
				continue;
			const FTransform& Start = Batch.Start[Slot];
			const FVector Outward = FVector(Start.GetLocation().X - CollapseCentre.X, Start.GetLocation().Y - CollapseCentre.Y, 0.0f).GetSafeNormal();
			const FVector Impulse = Outward * 350.0f + FVector(0.0f, 0.0f, 900.0f + ((Noise >> 20) & 0xFF) / 255.0f * 500.0f);
			if (Debris->Launch(Mesh, Start, Impulse, EPGDebrisMotion::Fly, 80.0f, /*bHeavy*/false, Source, Map.Get(), /*bLightweightOnly*/false) != nullptr)
			{
				++FlungThisFrame;
				++CollapseDebrisLaunched;
			}
		}
	}
	// 컴포넌트당 한 번. 이 컴포넌트에 처음이자 마지막 지우기라 저장해 둔 번호가 아직 맞다.
	WZFP_RemoveFromSource(Source, Batch.Instances, Batch.bWholeComponent);
}

// 매 프레임 조각을 움직인다.
//
// "박살난다"로 읽히게 하는 것들. 처음엔 전부 같은 순간에 같은 속도로 내려서 원판 하나가 승강기처럼
// 내려가는 그림이었고(9/20), 그 다음엔 번지며 꺼지긴 했지만 여전히 "꺼지는 승강기" 였다(사용자 9/21:
// "타일도 드래곤 등장할 때 좀 더 박살이 자연스러웠으면"). 지금은:
//  0) **먼저 떨린다** — 앞 25% 동안 아무것도 안 꺼지고 가장자리부터 잘게 흔들린다(금이 가는 경고).
//     가장자리가 먼저인 이유: 드래곤은 가운데 밑에 있으니 가운데가 먼저 흔들려야 물리적으로 맞지만,
//     보는 사람은 바깥 테두리가 흔들려야 "구역 전체가 위험하다" 를 먼저 알아챈다. 연출은 눈 기준이다.
//  1) **시작 시각을 어긋나게** — 한가운데가 먼저 터지고 바깥으로 번진다. 무너짐은 번지는 것이지 내려가는 것이 아니다.
//  2) **일부는 튀어 오른다** — 4개 중 1개는 6~10m 솟았다가 더 빨리, 더 많이 돌며 떨어진다. 나머지도 꺼지기 직전
//     한 번 들썩인다(밑에서 치받는 느낌 — 실제로 드래곤이 올라오는 자리다).
//  3) **기울고 미끄러진다** — 가장자리는 구덩이 쪽으로 빨려 들어가며 기울어, 조각 사이가 벌어져 "갈라졌다"가 된다.
//  4) **작은 소품은 잔해로 날아간다** — 돌·상자 같은 것 몇 개(예산 18개, 프레임당 2개)를 잔해 시스템에 넘겨 진짜로 튄다.
//  5) **흙먼지가 여기저기 오른다** — 막 터진 자리 중 일부에서(0.07초 간격, 예산 36개). 한가운데 첫 터짐엔 큰 것 하나.
// 전부 인덱스 해시와 중심 거리로만 만든다. 조각마다 난수를 저장하면 수천 개 분량을 들고 있어야 한다.
//
// 한 프레임의 일(성능 — 위 CollapseRegion 주석):
//  a) 차례가 온 큰 묶음을 Mover 로 옮긴다(프레임당 최대 WZFP_CollapseMovesPerFrame).
//  b) 터짐이 닿은 작은 묶음을 숨긴다(프레임당 최대 WZFP_CollapseHidesPerFrame).
//  c) 옮겨진 큰 묶음 중 갱신 시각이 된 것만(30Hz, 절반은 반 박자 어긋남) 트랜스폼 배열을 채워 한 번에 넘긴다.
//     아무 조각도 안 움직인 묶음은 넘기지도 않는다(건드리지 않는 것이 제일 싸다).
void UPGRegionCollapse::TickRegionCollapse(float DeltaSeconds)
{
	const double TickBeginAt = FPlatformTime::Seconds();
	if (CollapseLastFrameAt > 0.0)
	{
		// 프레임 시간은 벽시계로 잰다(엔진 DeltaSeconds 는 시간 배율·틱 간격의 영향을 받는다).
		const double Frame = TickBeginAt - CollapseLastFrameAt;
		CollapseFrameTimeSum += Frame;
		CollapseFrameTimeMax = FMath::Max(CollapseFrameTimeMax, Frame);
		++CollapseFrameCount;
	}
	CollapseLastFrameAt = TickBeginAt;

	CollapseElapsed += FMath::Min(DeltaSeconds, WZFP_CollapseMaxStepSeconds);
	const float Alpha = FMath::Clamp(CollapseElapsed / CollapseDuration, 0.0f, 1.0f);
	// 떨림 구간을 뺀 "터짐 진행도"(0~1). 번짐·꺼짐은 전부 이 값으로 잰다.
	const float Tremor = FMath::Clamp(CollapseTremorFraction, 0.0f, 0.6f);
	const float BreakSpan = FMath::Max(1.0f - Tremor, 0.1f);
	const float Break = FMath::Clamp((Alpha - Tremor) / BreakSpan, 0.0f, 1.0f);
	// 한 조각이 실제로 떨어지는 데 쓰는 시간. 터짐 구간에서 "번지는 데 쓴 몫"을 뺀 나머지다.
	const float FallWindow = FMath::Max(1.0f - CollapseStagger, 0.1f);
	if (!bCollapseLoggedBreakup && Break > 0.0f)
	{
		bCollapseLoggedBreakup = true;
		UE_LOG(LogTemp, Display, TEXT("PGCollapse: tremor over at %.2fs - break-up starts from the centre"), CollapseElapsed);
		// 한가운데가 터지는 순간의 큰 먼지. 예산·간격과 상관없이 하나는 꼭 낸다(여기가 연출의 첫 박자다).
		if (IsValid(CollapseDustEffect))
			UGameplayStatics::SpawnEmitterAtLocation(Map->GetWorld(), CollapseDustEffect, CollapseCentre + FVector(0.0f, 0.0f, 50.0f),
				FRotator::ZeroRotator, FVector(7.0f), true, EPSCPoolMethod::AutoRelease);
	}

	UPGDebrisSubsystem* Debris = UPGDebrisSubsystem::Get(Map->GetWorld());
	int32 FlungThisFrame = 0;
	// 떨림 위상: 14Hz. 조각마다 위상을 어긋내 한 몸처럼 흔들리지 않게 한다.
	const float ShakePhase = CollapseElapsed * 2.0f * PI * 14.0f;

	// a) 큰 묶음 옮기기 — 그 묶음의 제일 바깥 조각이 떨리기 시작할 시각이 되면. 정렬이 MaxSpread 내림차순이라 차례는 앞에서부터 온다.
	for (int32 Moved = 0; CollapseMoveCursor < CollapseLargeCount && Moved < FMath::Max(1, CVarCollapseMovesPerFrame.GetValueOnGameThread()); ++Moved)
	{
		FPGCollapseBatch& Batch = CollapseBatches[CollapseMoveCursor];
		const float TremorStart = (1.0f - Batch.MaxSpread) * 0.6f * Tremor;
		if (Alpha <= TremorStart)
			break;
		MoveCollapseBatch(Batch);
		// 갱신 시각: 홀수 묶음은 반 박자 늦게 시작해 60fps 에서 프레임마다 절반씩 갱신되게.
		Batch.NextUpdateAt = CollapseElapsed + ((CollapseMoveCursor & 1) ? 0.5f / WZFP_CollapseUpdateHz : 0.0f);
		++CollapseMoveCursor;
	}
	// b) 작은 묶음 숨기기 — 터짐이 그 묶음의 제일 안쪽 조각에 닿으면(정렬이 MinSpread 오름차순).
	for (int32 Hidden = 0; CollapseHideCursor < CollapseBatches.Num() && Hidden < WZFP_CollapseHidesPerFrame; ++Hidden)
	{
		FPGCollapseBatch& Batch = CollapseBatches[CollapseHideCursor];
		if (Break <= Batch.MinSpread * CollapseStagger)
			break;
		HideCollapseBatch(Batch, FlungThisFrame);
		++CollapseHideCursor;
	}

	// c) 옮겨진 큰 묶음 갱신
	for (int32 BatchIndex = 0; BatchIndex < CollapseLargeCount; ++BatchIndex)
	{
		FPGCollapseBatch& Batch = CollapseBatches[BatchIndex];
		UInstancedStaticMeshComponent* Mover = Batch.Mover.Get();
		if (!Batch.bMoved || !IsValid(Mover) || CollapseElapsed < Batch.NextUpdateAt)
			continue;
		Batch.NextUpdateAt = CollapseElapsed + 1.0f / WZFP_CollapseUpdateHz;
		// "이번 갱신에 막 터진 조각" 은 이 묶음의 지난 갱신 기준으로 고른다(묶음마다 갱신 시각이 다르다).
		const float PrevAlpha = Batch.LastAlpha;
		const float PrevBreak = FMath::Clamp((PrevAlpha - Tremor) / BreakSpan, 0.0f, 1.0f);
		Batch.LastAlpha = Alpha;
		bool bAnyMoved = false;
		for (int32 Slot = 0; Slot < Batch.Instances.Num(); ++Slot)
		{
			if (Batch.Flung[Slot])
				continue; // 잔해로 날린 조각. Work 에 땅속 깊이 숨겨 뒀고, 마지막에 Mover 째 지운다.
			// 조각마다 다르게 기울고 다르게 빨리 떨어져야 "무너진다"로 보인다.
			// 난수를 저장해 두는 대신 인덱스를 섞어 쓴다 — 매 프레임 같은 값이 나오고 기억할 것이 없다.
			const uint32 Noise = GetTypeHash(Batch.Instances[Slot] * 2654435761u);
			const float Spread = Batch.Spread[Slot];
			// 같은 거리끼리 줄 맞춰 꺼지면 동심원이 눈에 띈다. ±8% 씩 어긋내 테두리를 지운다.
			const float Jitter = (((Noise >> 24) & 0xFF) / 255.0f - 0.5f) * 0.16f;
			const float Begin = FMath::Clamp(Spread + Jitter, 0.0f, 1.0f) * CollapseStagger;
			const float Local = FMath::Clamp((Break - Begin) / FallWindow, 0.0f, 1.0f);
			const FTransform& Start = Batch.Start[Slot];
			FTransform& Next = Batch.Work[Slot];

			if (Local <= 0.0f)
			{
				// ---- 아직 안 터졌다: 떨림 ----
				// 가장자리(Spread 1)는 바로, 한가운데(Spread 0)는 떨림 구간의 60% 지점부터 흔들리기 시작한다.
				// 떨림 구간이 끝나도 제 차례가 올 때까지 계속 흔들린다(진폭 1 유지).
				const float TremorStart = (1.0f - Spread) * 0.6f * Tremor;
				const float RampSpan = FMath::Max(Tremor - TremorStart, 0.05f);
				const float Ramp = FMath::Clamp((Alpha - TremorStart) / RampSpan, 0.0f, 1.0f);
				if (Ramp <= 0.0f)
					continue; // 아직 조용하다. Work 에는 Start 가 그대로 있다(건드리지 않는 것이 제일 싸다).
				// 막 흔들리기 시작한 가장자리 조각 일부에서 작은 먼지. "금이 간다" 를 보여 주는 단서다.
				if (PrevAlpha <= TremorStart && ((Noise >> 9) & 0x3F) == 0)
					SpawnCollapseDust(Start.GetLocation() + FVector(0.0f, 0.0f, 30.0f), 1.5f);
				const float Phase = (Noise & 0xFFFF) / 65535.0f * 2.0f * PI;
				const float Amp = Ramp * (0.4f + ((Noise >> 16) & 0xFF) / 255.0f * 0.6f); // 조각마다 0.4~1배
				const float S1 = FMath::Sin(ShakePhase + Phase);
				const float S2 = FMath::Sin(ShakePhase * 0.73f + Phase * 1.7f);
				// 옆으로 ±7cm, 위로 최대 5cm(아래로는 안 간다 — 땅에 박히면 깜빡인다), 기울기 ±1.5도.
				FVector Where = Start.GetLocation();
				Where.X += S1 * 7.0f * Amp;
				Where.Y += S2 * 7.0f * Amp;
				Where.Z += FMath::Abs(S1) * 5.0f * Amp;
				Next.SetLocation(Where);
				Next.SetRotation(FRotator(S2 * 1.5f * Amp, 0.0f, S1 * 1.5f * Amp).Quaternion() * Start.GetRotation());
				bAnyMoved = true;
				continue;
			}

			// ---- 터졌다: 튀고, 기울고, 꺼진다 ----
			const bool bJustBroke = PrevBreak <= Begin; // 이번 갱신에 막 차례가 됐다
			if (bJustBroke)
			{
				// 작은 소품 8개 중 1개는 잔해 시스템으로 진짜로 날린다(예산 안에서). 위로 9~14m/s, 바깥으로 3.5m/s.
				// 큰 것(반지름 3m 초과)은 Chaos 에 안 올리고 가벼운 흉내로만 — 잔해 시스템 주석의 NarrowPhase 사고.
				if (Batch.bDebrisEligible && IsValid(Debris) && FlungThisFrame < CollapseDebrisPerFrame
					&& CollapseDebrisLaunched < CollapseDebrisBudget && ((Noise >> 12) & 7) == 0)
				{
					const FVector Outward = FVector(Start.GetLocation().X - CollapseCentre.X, Start.GetLocation().Y - CollapseCentre.Y, 0.0f).GetSafeNormal();
					const FVector Impulse = Outward * 350.0f + FVector(0.0f, 0.0f, 900.0f + ((Noise >> 20) & 0xFF) / 255.0f * 500.0f);
					UStaticMesh* Mesh = Mover->GetStaticMesh();
					const float RadiusWorld = IsValid(Mesh) ? Mesh->GetBounds().SphereRadius * Start.GetScale3D().GetAbsMax() : 0.0f;
					if (Debris->Launch(Mesh, Start, Impulse, EPGDebrisMotion::Fly, 80.0f, /*bHeavy*/false, Mover, Map.Get(),
						/*bLightweightOnly*/RadiusWorld > 300.0f) != nullptr)
					{
						Batch.Flung[Slot] = 1;
						++FlungThisFrame;
						++CollapseDebrisLaunched;
						// 원본 자리의 조각은 땅속 깊이 숨긴다. 지우면 번호가 밀리므로 마지막에 Mover 째 지운다.
						Next.SetLocation(Start.GetLocation() - FVector(0.0f, 0.0f, 100000.0f));
						bAnyMoved = true;
						continue;
					}
				}
				// 막 터진 자리 32곳 중 1곳에서 먼지(간격·예산은 SpawnCollapseDust 가 지킨다). 바깥일수록 조금 크게.
				if (((Noise >> 9) & 0x1F) == 0)
					SpawnCollapseDust(Start.GetLocation() + FVector(0.0f, 0.0f, 30.0f), 3.0f + Spread * 2.0f);
			}

			// 4개 중 1개는 "튀는 조각": 6~10m 솟았다가 더 빨리, 더 많이 돌며 떨어진다. 나머지는 1.5m 들썩이고 꺼진다.
			const bool bPop = ((Noise >> 4) & 3) == 0;
			const float PopHeight = bPop ? 600.0f + ((Noise >> 20) & 0xFF) / 255.0f * 400.0f : 150.0f;
			const float PopWindow = bPop ? 0.35f : 0.15f;
			const float Kick = Local < PopWindow ? FMath::Sin(Local / PopWindow * PI) * PopHeight : 0.0f;
			const float Speed = (0.7f + ((Noise >> 16) & 0xFF) / 255.0f * 0.6f) * (bPop ? 1.3f : 1.0f); // 0.7 ~ 1.3배
			const float Fall = CollapseDepth * Local * Local * Speed - Kick;
			const float LeanMax = bPop ? 120.0f : 60.0f;                                               // ±60도 / ±30도
			const float Lean = ((Noise & 0xFF) / 255.0f - 0.5f) * LeanMax * Local;
			const float Twist = (((Noise >> 8) & 0xFF) / 255.0f - 0.5f) * (bPop ? 90.0f : 40.0f) * Local;
			const float Spin = bPop ? (((Noise >> 2) & 0xFF) / 255.0f - 0.5f) * 180.0f * Local : 0.0f; // 튀는 조각은 제자리에서 반 바퀴 가까이 돈다(요)

			FVector Where = Start.GetLocation();
			// 가장자리일수록 구덩이 쪽으로 빨려 들어간다(최대 8m). 한가운데는 그냥 수직으로 꺼진다.
			FVector2D Inward(CollapseCentre.X - Where.X, CollapseCentre.Y - Where.Y);
			if (!Inward.IsNearlyZero())
			{
				Inward.Normalize();
				const float Slide = 800.0f * Spread * Local * Local;
				Where.X += Inward.X * Slide;
				Where.Y += Inward.Y * Slide;
			}
			Where.Z -= Fall;
			Next.SetLocation(Where);
			Next.SetRotation(FRotator(Lean, Spin, Twist).Quaternion() * Start.GetRotation());
			bAnyMoved = true;
		}
		if (bAnyMoved)
		{
			// 컴포넌트당 한 번의 일괄 갱신. 렌더 상태를 통째로 다시 만들지 않고(bMarkRenderStateDirty=false) 바뀐 인스턴스만
			// 프레임 끝에 보낸다 — 5.6 의 ISM 은 UpdateInstanceTransform 안에서 FPrimitiveInstanceDataManager 가 바뀐 번호를
			// 표시해 두고 MarkRenderInstancesDirty 로 넘긴다(엔진 ISMInstanceDataManager.cpp). 명시적으로 한 번 더 켜 두는 것은 안전장치.
			Mover->BatchUpdateInstancesTransforms(0, Batch.Work, /*bWorldSpace*/true, /*bMarkRenderStateDirty*/false, /*bTeleport*/true);
			Mover->MarkRenderInstancesDirty();
		}
	}

	const double TickCost = FPlatformTime::Seconds() - TickBeginAt;
	CollapseTickCostSum += TickCost;
	CollapseTickCostMax = FMath::Max(CollapseTickCostMax, TickCost);
	if (Alpha >= 1.0f)
		FinishRegionCollapse();
}

// 다 꺼졌으면 지운다. 여기서 실제로 메모리가 돌아온다.
// 무너진 자리 위에 뜬 몹·물건 치우기(0.5초마다).
//
// 왜: 무너질 때 그 위의 것은 CollapseRegion 이 한 번 지운다. 그 뒤에 걸어 들어온 몹은 길찾기용 투명 바닥판(맵 전체 한 장, 땅 높이)
//   위를 그대로 걸어서 구덩이 위 허공에 서 있었다(사용자 9/22 "타일 밑으로 꺼지면 재들도 같이 빠져서 사라져야지").
//   판에 구멍을 낼 수 없고, 판의 충돌을 바꾸면 길찾기 지도(Dynamic)가 맵 전체를 다시 짓는다 — 그래서 올라선 쪽을 치운다.
// 무엇을: 몬스터(로봇 포함, 아무도 안 탄 것)와 바닥 아이템만. 플레이어·탈것·전함·드래곤은 건드리지 않는다.
// 어디서: 원 안쪽 90%(둘레는 비스듬한 벽이라 가장자리는 남긴다), 땅 높이 ±6m(구덩이 바닥까지 떨어진 것·날아다니는 것은 제외).
void UPGRegionCollapse::SweepCollapsedAreas()
{
	UWorld* World = Map->GetWorld();
	if (!World)
		return;
	if (!Map->HasAuthority())
		return; // 몹·물건은 복제된다 — 서버가 지운다(멀티 9/27)
	int32 Swept = 0;
	for (const FVector4& Area : Map->CollapsedAreas)
	{
		// 9/28: 원 대신 무너진 칸(CollapsedCells)으로 본다 — 구덩이가 칸 모양이다.
		const TSet<FIntPoint>& Cells = Map->CollapsedCells;
		auto IsOverPit = [&Cells, &Area](const AActor* Actor)
		{
			const FVector Where = Actor->GetActorLocation();
			const FIntPoint Cell(FMath::RoundToInt(Where.X / DesignCellSize), FMath::RoundToInt(Where.Y / DesignCellSize));
			return Cells.Contains(Cell) && FMath::Abs(Where.Z - Area.Z) < 600.0f;
		};
		for (TActorIterator<APGMonsterCharacter> It(World); It; ++It)
		{
			APGMonsterCharacter* Monster = *It;
			if (!IsValid(Monster) || Monster->IsPlayerControlled() || Monster->Tags.Contains(PGPhysicsUtil::ProtectedTag) || !IsOverPit(Monster))
				continue;
			if (const IPGRideable* Rideable = Cast<IPGRideable>(Monster); Rideable && Rideable->GetRiderPawn())
				continue;
			SpawnCollapseDust(Monster->GetActorLocation(), 0.6f);
			Monster->Destroy();
			++Swept;
		}
		for (TActorIterator<APGFloorItemActor> It(World); It; ++It)
		{
			APGFloorItemActor* Item = *It;
			if (!IsValid(Item) || !IsOverPit(Item))
				continue;
			Item->Destroy();
			++Swept;
		}
	}
	if (Swept > 0)
		UE_LOG(LogTemp, Display, TEXT("PGCollapse: swept %d monster(s)/item(s) standing over the pit"), Swept);
}

void UPGRegionCollapse::FinishRegionCollapse()
{
	int32 Removed = 0;
	int32 Destroyed = 0;
	int32 MoversDropped = 0;
	for (FPGCollapseBatch& Batch : CollapseBatches)
	{
		Removed += Batch.Instances.Num();
		if (UInstancedStaticMeshComponent* Source = Batch.Component.Get(); IsValid(Source))
		{
			// 차례가 안 와서(끊김 등) 아직 옮기지도 숨기지도 못한 묶음은 여기서 바로 지운다.
			if (!Batch.bMoved)
			{
				WZFP_RemoveFromSource(Source, Batch.Instances, Batch.bWholeComponent);
			}
			// 소품 청크는 컴포넌트째 버린다. 고정 멤버(지면·도로 등)는 나중에 다시 쓸 수 있으니 비운 채로 둔다.
			if (Batch.bWholeComponent && Map->RuntimePackedVisualHISMs.Remove(Cast<UHierarchicalInstancedStaticMeshComponent>(Source)) > 0)
			{
				Source->DestroyComponent();
				++Destroyed;
			}
		}
		if (UInstancedStaticMeshComponent* Mover = Batch.Mover.Get(); IsValid(Mover))
		{
			Mover->DestroyComponent();
			++MoversDropped;
		}
	}
	CollapseMovers.Reset();
	CollapseBatches.Reset();
	Map->bCollapsing = false;
	Map->SetActorTickInterval(CollapseSavedTickInterval);
	UE_LOG(LogTemp, Display, TEXT("PGCollapse: done - removed %d instances, dropped %d prop batches, %d mover ISMs, flung %d as debris, %d dust bursts, pit stays (%d planes)"),
		Removed, Destroyed, MoversDropped, CollapseDebrisLaunched, CollapseDustSpawned, IsValid(CollapsePitHISM) ? CollapsePitHISM->GetInstanceCount() : 0);
	// 붕괴 동안의 프레임 시간(벽시계)과 이 코드 자체의 시간. 첫 프레임에는 CollapseRegion 뒤에 남은 시작 프레임 몫이 섞인다.
	const double AvgFrame = CollapseFrameCount > 0 ? CollapseFrameTimeSum / CollapseFrameCount : 0.0;
	const double AvgTick = CollapseFrameCount > 0 ? CollapseTickCostSum / CollapseFrameCount : 0.0;
	UE_LOG(LogTemp, Display, TEXT("PGCollapse: perf - %d frames over %.2fs: avg frame %.1f ms (%.0f fps), worst %.1f ms (min fps %.0f); collapse tick avg %.2f ms, max %.2f ms; %d batches moved, %d hidden; tree rebuild %.2f ms total"),
		CollapseFrameCount, CollapseFrameTimeSum, AvgFrame * 1000.0, AvgFrame > 0.0 ? 1.0 / AvgFrame : 0.0,
		CollapseFrameTimeMax * 1000.0, CollapseFrameTimeMax > 0.0 ? 1.0 / CollapseFrameTimeMax : 0.0,
		AvgTick * 1000.0, CollapseTickCostMax * 1000.0, CollapseMovesDone, CollapseHidesDone, GWZFP_TreeBuildSeconds * 1000.0);
	GWZFP_TreeBuildSeconds = 0.0;
}

// 구덩이: 무너진 자리 밑에 산 재질의 바닥판을 깔고, 둘레를 비스듬한 벽판으로 두른다.
//
// 왜 산 재질인가: 맵 둘레 바닥판(치마)과 같은 이유 — 땅 밑을 "회색 판" 이 아니라 바위·흙으로 읽히게 하려면
//   이미 화면에 있는 산과 같은 겉모습이 제일 안전하다. 새 재질을 만들 시간도 없다.
// 왜 40m 아래인가: 땅 높이의 판은 "안 무너진 것" 처럼 보였다(9/20). 깊어야 구덩이다. 드래곤은 땅속에서 솟으므로
//   판 아래에서 시작해 판을 뚫고 올라온다 — 스윕 없이 위치를 옮기는 액터라 판에 걸리지 않는다.
// 왜 벽도 두나: 바닥판만 깔면 테두리에서 바닥판까지 옆면이 비어 보인다. 둘레를 20 조각의 판으로 둘러 아래로 갈수록
//   안쪽으로 기울게(약 70도) 세우면 "파인 구멍" 의 옆면이 된다. 원통형이라 어느 쪽에서 봐도 같다.
// 왜 HISM 하나인가: 판 70장 남짓을 그리기 호출 한 번으로. 충돌은 질의 전용(물리 몸 없음) — 튀어 오른 잔해가 이 바닥에
//   떨어져 쌓이라고(잔해 시스템은 WorldStatic 을 바닥으로 찾는다). PGTerrain 태그를 붙여 이 판까지 날리지는 못하게 한다.
// MinFloorHalfCm: 둘레 바닥판(치마)이 원에 걸쳐 같이 꺼지면 그 판이 있던 자리(원보다 멀리)까지 바닥을 넓힌다. 벽은 그대로 —
//   벽 바깥으로 넓어진 바닥은 위에서 내려다보면 얕은 턱으로 읽히고, 치마가 남아 구멍을 덮는 것보다 낫다.
void UPGRegionCollapse::BuildCollapsePit(const FVector& WorldCentre, const TSet<FIntPoint>& Cells)
{
	if (!IsValid(CollapsePitHISM))
	{
		CollapsePitHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(Map.Get(), TEXT("CollapsePitHISM"));
		CollapsePitHISM->SetupAttachment(Map->GetRootComponent());
		CollapsePitHISM->SetMobility(EComponentMobility::Movable);
		CollapsePitHISM->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		CollapsePitHISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		CollapsePitHISM->SetCanEverAffectNavigation(false);
		CollapsePitHISM->SetGenerateOverlapEvents(false);
		CollapsePitHISM->SetCastShadow(false); // 구덩이 바닥은 그림자를 받기만 한다
		CollapsePitHISM->ComponentTags.Add(PGPhysicsUtil::TerrainTag);
		if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
			CollapsePitHISM->SetStaticMesh(Plane);
		const TSoftObjectPtr<UMaterialInterface>& PitMaterial = UPGMapVisualSet::GetActive()->RockEdgeMaterial;
		if (UMaterialInterface* MountainMaterial = PitMaterial.IsNull() ? nullptr : PitMaterial.LoadSynchronous())
			CollapsePitHISM->SetMaterial(0, MountainMaterial);
		CollapsePitHISM->RegisterComponent();
	}
	CollapsePitHISM->ClearInstances();
	if (!CollapsePitHISM->GetStaticMesh())
	{
		UE_LOG(LogTemp, Warning, TEXT("PGCollapse: pit skipped - engine plane mesh missing"));
		return;
	}
	const float Depth = FMath::Max(CollapsePitDepthCm, 500.0f);
	const float FloorZ = WorldCentre.Z - Depth;
	// 테두리 높이는 땅보다 30cm 아래(남은 타일과 겹쳐 깜빡이지 않게).
	const float RimZ = WorldCentre.Z - 30.0f;
	const float WallHeight = RimZ - FloorZ;
	const FTransform ComponentToWorld = CollapsePitHISM->GetComponentTransform();
	TArray<FTransform> Planes;
	// 9/28 칸 모양: 무너진 칸마다 바닥판 하나(20m), 옆 칸이 안 무너졌으면 그 경계에 곧은 벽 하나.
	//   전에는 원 둘레에 20 조각 비스듬한 벽을 둘렀는데, 꺼지는 타일은 네모라 둥근 벽이 남은 타일 사이로 드러나 "원" 으로 보였다.
	// 벽 판의 축: X = 경계를 따라, Y = 위, 앞면(+Z) = 구덩이 안쪽. X = Up × 안쪽 이면 X × Y 가 안쪽을 향한다(뒤집힌 판이 안 생긴다).
	static const FIntPoint Sides[] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
	int32 Walls = 0;
	for (const FIntPoint& Cell : Cells)
	{
		const FVector CellCentre(Cell.X * DesignCellSize, Cell.Y * DesignCellSize, 0.0f);
		Planes.Add(FTransform(FRotator::ZeroRotator, CellCentre + FVector(0.0f, 0.0f, FloorZ), FVector(DesignCellSize / 100.0f, DesignCellSize / 100.0f, 1.0f))
			.GetRelativeTransform(ComponentToWorld));
		for (const FIntPoint& Side : Sides)
		{
			if (Cells.Contains(Cell + Side))
				continue;
			const FVector Out(Side.X, Side.Y, 0.0f);
			const FVector Inward = -Out;
			const FVector Along = FVector::CrossProduct(FVector::UpVector, Inward);
			const FVector Middle = CellCentre + Out * (DesignCellSize * 0.5f) + FVector(0.0f, 0.0f, (RimZ + FloorZ) * 0.5f);
			FTransform Wall(FMatrix(Along, FVector::UpVector, Inward, Middle));
			// 2% 넓게: 모서리 이음새가 벌어지지 않게.
			Wall.SetScale3D(FVector(DesignCellSize * 1.02f / 100.0f, WallHeight / 100.0f, 1.0f));
			Planes.Add(Wall.GetRelativeTransform(ComponentToWorld));
			++Walls;
		}
	}
	CollapsePitHISM->AddInstances(Planes, false, false, false);
	CollapsePitHISM->BuildTreeIfOutdated(false, true);
	UE_LOG(LogTemp, Display, TEXT("PGCollapse: pit - %d cell floors at z %.0fm (%.0fm below), %d straight wall planes along cell edges, material %s"),
		Cells.Num(), FloorZ * 0.01f, Depth * 0.01f, Walls, *GetNameSafe(CollapsePitHISM->GetMaterial(0)));
}

// 흙먼지 하나. 간격(0.07초)과 한 판의 예산을 여기서 지킨다 — 부르는 쪽은 "여기서 나면 좋겠다" 만 말한다.
// 왜: 수천 조각이 한꺼번에 터지는데 조각마다 먼지를 내면 파티클이 프레임을 먹는다(4060 기준). 눈에는 36개면 충분히 "여기저기" 다.
void UPGRegionCollapse::SpawnCollapseDust(const FVector& Where, float Scale)
{
	UWorld* World = Map->GetWorld();
	if (!IsValid(CollapseDustEffect) || !IsValid(World) || CollapseDustSpawned >= CollapseDustBudget)
		return;
	const double Now = World->GetTimeSeconds();
	if (Now < CollapseNextDustTime)
		return;
	// 무너지는 소리 — 먼지와 같은 간격 제한을 탄다(한꺼번에 수십 번 울리지 않게).
	PGSound::PlayLocal(Map.Get(), FName(TEXT("Ground_Collapse")), nullptr, Where);
	CollapseNextDustTime = Now + 0.07;
	++CollapseDustSpawned;
	UGameplayStatics::SpawnEmitterAtLocation(World, CollapseDustEffect, Where, FRotator::ZeroRotator, FVector(Scale), true, EPSCPoolMethod::AutoRelease);
}
