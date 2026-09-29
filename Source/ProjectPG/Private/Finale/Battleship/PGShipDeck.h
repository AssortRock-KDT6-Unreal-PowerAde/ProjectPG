// 전함 갑판·격납고 — 승강기, 실은 것 붙잡기, 떨어지는 사람 받기, 선미 발판·뒷문.
// 2026-09-26 SOLID(한 책임): 전함 액터(APGBattleshipActor)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 전함 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 전함 액터에 그대로 두고 Ship-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 전함 액터의 비공개 멤버를 읽어야 해서 전함 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGBattleshipActor.h"
#include "PGShipDeck.generated.h"

UCLASS(Transient)
class UPGShipDeck : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGBattleshipActor* InShip) { Ship = InShip; }
	virtual UWorld* GetWorld() const override { return Ship ? Ship->GetWorld() : nullptr; }

	// 배 뒤쪽 선반: 문 앞에 깔리는 평평한 널빤지 세 장. 뒤로 갈수록 좁아지고, 닫을 때 서랍처럼 앞으로 밀려 들어간다.
	// 크기는 절대 건드리지 않는다 — 팩 부품을 오그라뜨리는 방식이 안 예뻐서 이 설계로 바꾼 것이다(9/21).
	struct FPGSternApron
	{
		TWeakObjectPtr<USceneComponent> Plank;      // 보이는 널빤지(문턱판도 같은 방식으로 다룬다)
		TWeakObjectPtr<UBoxComponent> Floor;        // 짝이 되는 밟는 상자(없으면 차가 그대로 빠진다)
		FVector OutLocation = FVector::ZeroVector;  // 펼쳐진 자리
		FVector StowLocation = FVector::ZeroVector; // 앞으로 밀어 넣은 자리
		FVector FloorOffset = FVector::ZeroVector;  // 밟는 상자는 널빤지 원점에서 이만큼 떨어져 있다
		float StartDelay = 0.0f;                    // 바깥 장부터 차례로 — 동시에 움직이면 밋밋하다
	};

	void DeployElevator(float GroundWorldZ);
	void RetractElevator();
	// 배가 움직이는 동안 갑판 위의 물리 차를 배에 붙든다(.cpp 주석 참고).
	void TickDeckCargo(float DeltaSeconds);
	// 위 둘이 함께 쓰는 알맹이: 상자 안의 물리 폰을 붙잡아 Parent 에 붙이고, 원래 상태를 적어 둔다.
	void HoldPhysicsPawns(USceneComponent* Parent, const FVector& WorldCentre, const FQuat& WorldRotation, const FVector& Extent,
		TArray<APGBattleshipActor::FPGHeldPawn>& Out, const TCHAR* What);
	void ReleaseHeldPawns(TArray<APGBattleshipActor::FPGHeldPawn>& Held, const TCHAR* What);
	// 갑판에 차를 한 대 세워 둔다(배가 정박할 때 한 번).
	void SpawnDeckVehicle();
	// 갑판 밑으로 새어 들어간 물리 몸을 갑판 위로 되돌린다(주석은 .cpp 참고).
	void TickCatchFallers(float DeltaSeconds);
	// 이륙할 때 배 뒤쪽 선반(널빤지 3장 + 문턱판)을 갑판 밑으로 밀어 넣는다(.cpp 주석 참고).
	void TickHullRetract(float DeltaSeconds);
	// 배 뒤쪽 선반(널빤지 3장)을 깐다. 차가 날아와 올라서는 자리다(.cpp 주석 참고).
	void BuildSternApron(float RearX);
	// 팩 뒷문 올려 닫기(TickRearDoorClose). 부품과 그 처음 자리(배 기준), 경첩, 다 닫혔을 때의 회전.
	void TickRearDoorClose(float DeltaSeconds);
	// 멀티(9/27): 뒷문이 닫히기 시작할 때 문·경사판 위에 선 사람을 배 안쪽 바닥으로 옮긴다(.cpp 주석). 서버에서만.
	void MovePeopleOffRearDoor();
	// 드래곤을 잡은 뒤(디렉터 Victory) 조종석에 아무도 없으면 뒷문을 다시 연다 — 차로 배에서 내려야 한다(9/23 사용자).
	// 블루프린트 이벤트(CloseRearDoor)로 닫았으면 여는 것도 블루프린트 이벤트 OpenRearDoor 로(없으면 경고만), 코드로 닫았으면 코드로 되돌린다.
	void TickRearDoorReopen(float DeltaSeconds);

private:
	UPROPERTY()
	TObjectPtr<APGBattleshipActor> Ship;

	// 뒤쪽 선반 밀어 넣기 상태. 이륙에 한 번만 돈다. 팩 뒷문·경사로를 오그라뜨리던 RetractParts 는 없앴다 —
	// 그것들이 입구다(9/21 사용자 결정, MeasureHull 주석).
	bool bHullRetracting = false;
	bool bHullRetractDone = false;
	float RearDoorElapsed = -1.0f; // 음수 = 아직 안 닫기 시작함
	bool bRearDoorClosed = false;
	bool bRearDoorClosedByBlueprint = false;
	bool bRearDoorReopened = false;
	float RearDoorOpenElapsed = -1.0f; // 음수 = 아직 안 열기 시작함
	TArray<FPGSternApron> SternAprons;
	float RetractElapsed = 0.0f; // 장식 줄이기와 선반 밀기를 같은 시계로 움직인다
	float CatchScanTime = 0.0f;
	bool bDeckHeld = false;
	float DeckHoldScanTime = 0.0f;
};
