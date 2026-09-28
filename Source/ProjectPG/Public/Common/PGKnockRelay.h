// 소품 날리기 멀티 중계(2026-09-27 멀티).
//
// 왜: 차·탱크·로봇·드래곤·배가 소품을 치면 PGPhysicsUtil::TryKnockProp 이 원본(타일 묶음의 인스턴스·레벨 메시)을 지우고
//   잔해를 날린다. 이 원본들은 컴퓨터마다 따로 지은 로컬 물건이라(맵은 설계도로 각자 짓는다) 서버에서 지워도 클라이언트에는
//   그대로 남았다 — 클라이언트 화면에는 부서진 벽이 서 있고, 차는 그 자리를 그냥 통과했다(멀티 점검 B).
// 어떻게: 서버가 소품 하나를 날릴 때마다 "이 메시가 이 자리에서 이 힘으로 날아갔다" 를 적어 두고, 프레임마다 한 번 묶어서 방송한다.
//   클라이언트는 받은 것을 줄에 넣고 프레임마다 몇 개씩 그 자리에서 같은 메시를 찾아 지우고 자기 잔해를 날린다. 잔해는 보기용이다(판정은 서버 잔해가 한다).
// 왜 묶나(9/28): 예전에는 소품 하나마다 방송 하나(신뢰 안 함)를 보냈다. 엔진은 신뢰 안 하는 방송을 그 액터의 다음 복제 때
//   몇 개씩만(net.MaxRPCPerNetUpdate 기본 2) 보내고 넘치는 것은 버린다. 이 액터는 1초에 한 번 복제라, 로봇이 중앙 건물을 부수는 동안
//   서버는 20초에 160개를 날렸는데 구경하던 사람에게는 1초에 2개만 갔다(PG.SmashCoreTest). 그 사람 화면에는 부서진 벽이 계속 서 있었다.
//   이제 프레임마다 방송 한 번(최대 두 번)에 여러 개를 담고, 믿을 수 있는 방송(Reliable)으로 보낸다 — 방송 수가 초당 30 남짓이라 보내기 줄이 넘치지 않는다.
// 왜 따로 액터인가: 방송(NetMulticast)은 복제되는 액터에서만 보낼 수 있다. 맵 액터·게임 모드(팀 코드)를 건드리지 않으려고
//   서버가 처음 날릴 때 이 작은 액터를 하나 만든다(항상 모두에게 보냄).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/NetSerialization.h"
#include "PGKnockRelay.generated.h"

class UStaticMesh;

// 날아간 소품 하나(원본 메시의 월드 자리·크기와 받은 힘).
USTRUCT()
struct FPGKnockEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMesh> Mesh = nullptr;
	UPROPERTY()
	FVector_NetQuantize10 Location = FVector::ZeroVector;
	UPROPERTY()
	FRotator Rotation = FRotator::ZeroRotator;
	UPROPERTY()
	FVector_NetQuantize100 Scale = FVector::OneVector;
	UPROPERTY()
	FVector_NetQuantize Impulse = FVector::ZeroVector;
	UPROPERTY()
	float MassKg = 0.0f;
	UPROPERTY()
	bool bFallOnly = false;
};

UCLASS(NotPlaceable)
class PROJECTPG_API APGKnockRelay : public AActor
{
	GENERATED_BODY()

public:
	APGKnockRelay();

	// 서버: 방금 날린 소품 하나를 적어 둔다(이번 프레임 끝의 Tick 에서 모두에게 묶어 보낸다). 혼자 하는 판·클라이언트에서는 아무것도 안 한다.
	static void Send(UWorld* World, UStaticMesh* Mesh, const FTransform& Where, const FVector& Impulse, float MassKg, bool bFallOnly);

	virtual void Tick(float DeltaSeconds) override;

protected:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastKnockBatch(const TArray<FPGKnockEntry>& Entries);

	// 서버: 아직 안 보낸 것. 클라이언트: 받았지만 아직 따라 하지 않은 것(프레임마다 몇 개씩 — 한 프레임에 몰리지 않게).
	TArray<FPGKnockEntry> Pending;
};
