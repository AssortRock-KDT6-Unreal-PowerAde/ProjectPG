// 드래곤 지상전 — 내려앉기, 땅에서 싸우기·걷기·뛰기, 다시 날아오르기.
// 2026-09-26 SOLID(한 책임): 드래곤 액터(APGDragonBoss)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 드래곤 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 드래곤 액터에 그대로 두고 Dragon-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 드래곤 액터의 비공개 멤버를 읽어야 해서 드래곤 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGDragonBoss.h"
#include "PGDragonGroundCombat.generated.h"

UCLASS(Transient)
class UPGDragonGroundCombat : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGDragonBoss* InDragon) { Dragon = InDragon; }
	virtual UWorld* GetWorld() const override { return Dragon ? Dragon->GetWorld() : nullptr; }

	void TickDescend(float DeltaSeconds);
	void TickLanded(float DeltaSeconds);
	void TickTakeOff(float DeltaSeconds);
	// ── 지상전(.cpp 의 "지상전" 절 주석 참고) ──
	// 전함 체력이 0 이면(추락 중) 지상전을 연다. 매 틱 부른다 — 열렸으면 true.
	bool CheckShipDown();
	// 지상전 시작: 지금 상태에 따라 내려앉기(공중) 또는 곧장 지상전(땅).
	void BeginGroundFight();
	void TickGroundFight(float DeltaSeconds);
	// 상대: 가장 가까운 플레이어 폰(차에 탔으면 그 차). 없으면 근처 몬스터(FindGroundVictim).
	AActor* FindGroundFightVictim() const;
	// 지면 공격 한 동작(포효 또는 불뿜기·할퀴기·물기)을 시작한다. Landed 와 GroundFight 가 같이 쓴다.
	void StartGroundAction();
	// 진행 중인 지면 동작의 피해 시점·땅 위 불꽃을 처리한다. 동작이 아직 안 끝났으면 true.
	bool TickGroundAction();
	// 발이 땅에 닿은 순간의 연출(자세 세우기·흙먼지·소품 날리기). Landed 와 GroundFight 진입이 같이 쓴다.
	void OnTouchdown();
	// 땅 위를 Target 쪽으로 걷는다(수평만). 발은 땅 높이를 따라가고, 타일 범위 밖으로는 안 나간다.
	void WalkToward(const FVector& Target, float StopAtCm, float DeltaSeconds);
	// 짧게 뛰어올라 Toward 근처로 옮긴다(TakeOff → Descend → GroundFight).
	void BeginHop(const FVector2D& Toward);
	// 지상 무기 배율에서 "땅" 으로 치는 상태인가. 내려앉았거나(Landed) 지상전이 시작된 뒤(뛰어 옮기는 중 포함).
	bool IsOnGroundForGuns() const;
	// 지면 공격이 닿는 거리(수평, 원점 기준 cm). 이 안이면 걷지 않고 공격한다.
	float GroundReachCm() const;
	void DoGroundAttack();
	// From 바로 밑의 땅 높이. 전함·나·몬스터는 땅으로 치지 않는다.
	bool FindGroundZ(const FVector& From, float& OutZ) const;

private:
	UPROPERTY()
	TObjectPtr<APGDragonBoss> Dragon;

	FVector2D GroundFightSpot = FVector2D::ZeroVector; // 지상전을 열 때 내려앉을 자리(전함 근처, 맵 안쪽)
};
