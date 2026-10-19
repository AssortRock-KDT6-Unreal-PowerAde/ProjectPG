#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/MapBuilder.h"
#include "MapSpawnRegionPlanner.generated.h"

// 시작 구역 담당 (여럿이 같이 할 때 최대 4명).
// 게임에서: 형님 맵 생성기는 시작 칸을 딱 1개만 만든다. 4명이 한 곳에서 출발하면 나오자마자 싸움이 난다.
//           그래서 맵 가장자리에서 서로 멀리 떨어진 빈 땅을 3곳 더 골라, 벽 친 시작 대기소로 만든다.
// 형님 칸 쪽지(AMapTile)는 안 바꾼다. 우리 설계 카드(TileDesignPlacements)만 바꾼다. 출구 칸을 가져다 쓰지 않으니 출구는 안 줄어든다.
// ProjectTest2 의 WarZoneFootprintPreview_SpawnRegions.cpp 에서 옮겨 온 것이다.
UCLASS(Transient)
class UMapSpawnRegionPlanner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 시작 구역 고르기. 흙길 담당이 시설 흙길을 깐 직후, 시작점·출구 흙길을 깔기 전에 부른다.
	// 왜 그때: 고른 칸에서 워존까지 흙길을 "시작점·출구 → 워존" 과 같은 방법으로 깔아야 해서.
	// BlockedCells = 건물·언덕·이미 흙길인 칸(여기는 고르지 않는다). 결과는 Map->SpawnRegionCells(0번 = 형님 시작 칸).
	void PickSpawnRegions(
		const TMap<FIntPoint, AMapTile*>& TileByCell,
		const TSet<FIntPoint>& BlockedCells,
		int64 RaidSeed);

	// 추가로 고른 칸만(1번부터). 흙길 담당이 쓴다: 이 칸은 지나가지 않고, 이 칸에서도 워존까지 길을 깐다.
	TSet<FIntPoint> GetExtraSpawnCells() const;

	// 고른 칸의 설계 카드를 "시작 대기소" 로 바꾼다. 칸 모양 담당이 지문(LayoutHash)을 다 계산한 뒤 부른다.
	// 담장의 유일한 입구가 워존 가는 흙길 쪽을 보게 돌린다.
	void DressExtraSpawnRegions(const FIntPoint& MinCell, const FIntPoint& MaxCell);

	// 들어온 플레이어를 시작 구역에 나눠 세운다(서버만). 맵 Tick 이 매번 부르고, 아직 안 세운 사람만 처리한다.
	// 게임에서: 1번째 들어온 사람은 1구역, 2번째는 2구역 … 5번째는 다시 1구역의 다른 자리.
	// 방장이 서버를 겸하는 방식(리슨 서버)이든 서버만 따로 도는 방식(전용 서버)이든 똑같이 되도록,
	// "첫 번째 플레이어"만 보지 않고 모든 PlayerController 를 돈다.
	void PlaceJoinedPlayers();

private:
	// 시작 대기소 입구 쪽으로 가장 멀리 트인 방향(입구 방향 ±45도 안, 5도 간격). 시작하자마자 벽을 보고 서지 않게.
	bool GetSpawnDoorwayYaw(const FIntPoint& Cell, const FVector& From, float& OutYaw, const AActor* Ignore) const;
	// JoinIndex 번 자리에 이 사람(폰)을 세운다. 폰이 없으면 게임모드에게 그 자리에 만들어 달라고 한다. 세웠으면 true.
	bool PlaceAtSeat(APlayerController* PlayerController, APawn* Pawn, int32 JoinIndex);
	// From 눈높이에서 Yaw 쪽으로 막힘 없이 몇 cm 트였나(최대 MaxCm). 로그에 막은 물건 이름도 남긴다.
	float MeasureOpenAhead(const FVector& From, float Yaw, float MaxCm, const AActor* Ignore, FString* OutBlocker = nullptr) const;

	// 사람마다 받은 들어온 순서 번호(= 구역·자리). 한 번 정하면 안 바뀐다.
	TMap<TWeakObjectPtr<APlayerController>, int32> JoinIndexByPlayer;
	// 마지막으로 자리에 세운 폰. 게임모드가 캐릭터를 새로 주면(폰이 바뀌면) 같은 자리에 다시 세운다.
	TMap<TWeakObjectPtr<APlayerController>, TWeakObjectPtr<APawn>> PlacedPawnByPlayer;
	// 들어온 순서 번호. 구역 = 번호 % 구역 수, 자리 = 번호 / 구역 수.
	int32 NextPlayerJoinIndex = 0;
	// 캐릭터 없이 들어와 있는 사람을 처음 본 시각(게임모드가 곧 만들 수도 있어 2초 기다린다).
	TMap<TWeakObjectPtr<APlayerController>, double> PawnlessSinceSeconds;

	// ---- 자리에 서기 전 무적 (흐름 그림 → README.md "로딩 창과 시작 무적") ----
	// 아직 자리 번호가 없는 사람의 폰을 막아 둔다: 충돌 끄기 + 제자리 멈춤 + 데미지 안 받음 표시.
	// 왜 충돌까지: 팀원 캐릭터의 피해 처리(Hit)는 "데미지 안 받음 표시(CanBeDamaged)" 를 안 본다.
	//   충돌을 끄면 총알 선·근접 공격이 몸에 안 닿아서, 공격이 어떤 길로 오든 막힌다. 캐릭터 파일은 안 고친다.
	// 자리 알림 부품도 여기서 붙인다(UMapSeatStatus → 그 사람 화면의 로딩 창을 닫는다).
	void ProtectUnseatedPlayers();
	// 자리에 세웠다: 막아 둔 폰을 풀고, 그 사람 컴퓨터에 "섰다" 를 보낸다.
	void FinishSeat(APlayerController* PlayerController);
	void ProtectPawn(APawn* Pawn);
	void UnprotectPawn(APawn* Pawn);

	// 막아 둔 폰과, 막기 전 "데미지 받음" 값(되돌릴 때 원래대로).
	struct FProtectedPawn
	{
		TWeakObjectPtr<APawn> Pawn;
		bool bWasDamageable = true;
	};
	TMap<TWeakObjectPtr<APlayerController>, FProtectedPawn> ProtectedByPlayer;
	// 처음 본 시각. 맵 액터의 SpawnProtectionTimeoutSeconds(기본 60초)가 지나도 못 세우면 막기를 풀고 Error 로그(영영 무적보다 낫다).
	TMap<TWeakObjectPtr<APlayerController>, double> FirstSeenSeconds;
	// 시간 제한으로 막기를 푼 사람(다시 막지 않는다).
	TSet<TWeakObjectPtr<APlayerController>> ProtectionTimedOut;

private:
	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
};
