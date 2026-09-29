// 전함 조종석 — 조종하는 사람 컴퓨터에서 서버로 보내는 길(2026-09-27 멀티).
//
// 왜: 전함은 서버의 것이고 클라이언트는 전함을 "소유" 하지 않아서, 전함 쪽 Server RPC 는 클라이언트가 부를 수 없다.
//   예전 조종석은 서버가 "첫 번째 플레이어 컨트롤러" 의 키를 직접 읽었다 — 전용 서버에는 키보드가 없고 그 사람은 원격이라
//   아무도 못 앉았고, 피날레가 "떠 있기" 단계에서 영원히 멈췄다(멀티 점검 A7).
// 어떻게: 사람마다 플레이어 컨트롤러에 이 부품을 붙인다(서버에서, 복제 — 컨트롤러는 그 사람 것이라 Server RPC 를 보낼 수 있다).
//   조종석 쪽(UPGShipHelm::TickLocalPilot)이 그 사람 컴퓨터에서 키를 읽어 RequestSeat / SendInput 을 부르면
//   서버에서 전함이 받아 판단한다. 이 컴퓨터가 서버(혼자 하는 판·듣기 서버 방장)면 바로 넘긴다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "PGHelmControlComponent.generated.h"

class APGBattleshipActor;
class APlayerController;

// 조종석 입력 한 묶음. 조종하는 사람 컴퓨터가 만들어 서버로 보낸다.
USTRUCT()
struct FPGHelmInput
{
	GENERATED_BODY()

	UPROPERTY()
	float Throttle = 0.0f;   // W/S: -1 ~ 1 (앉아 있을 때만)
	UPROPERTY()
	float Yaw = 0.0f;        // A/D: -1 ~ 1 (앉아 있을 때만)
	UPROPERTY()
	bool bClimb = false;     // 오른쪽 버튼: 위로 (앉아 있을 때만)
	UPROPERTY()
	bool bFire = false;      // 왼쪽 버튼: 주포 (함교에 서 있거나 앉아 있을 때)
	UPROPERTY()
	FVector_NetQuantize Aim = FVector::ZeroVector; // 그 사람 화면 가운데가 겨눈 곳(조준 보정 포함)

	bool operator==(const FPGHelmInput& Other) const
	{
		return FMath::IsNearlyEqual(Throttle, Other.Throttle) && FMath::IsNearlyEqual(Yaw, Other.Yaw)
			&& bClimb == Other.bClimb && bFire == Other.bFire && Aim.Equals(Other.Aim, 50.0f);
	}
};

UCLASS(ClassGroup = (PG))
class PROJECTPG_API UPGHelmControlComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGHelmControlComponent();

	// 서버: 판 시작 때 사람마다 미리 붙여 둔다(막 붙인 부품은 아직 클라이언트에 없어 RPC 가 안 간다).
	static UPGHelmControlComponent* Ensure(APlayerController* PlayerController);

	// 조종하는 사람 컴퓨터에서 부른다.
	static void RequestSeat(APlayerController* PlayerController, APGBattleshipActor* Ship, bool bSit);
	static void SendInput(APlayerController* PlayerController, APGBattleshipActor* Ship, const FPGHelmInput& Input);

protected:
	UFUNCTION(Server, Reliable)
	void ServerRequestSeat(APGBattleshipActor* Ship, bool bSit);
	UFUNCTION(Server, Reliable)
	void ServerSendInput(APGBattleshipActor* Ship, const FPGHelmInput& Input);
};
