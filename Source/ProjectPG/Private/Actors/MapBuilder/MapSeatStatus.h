#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MapSeatStatus.generated.h"

// "이 사람이 시작 구역 자리에 섰다" 를 그 사람 컴퓨터에 알려 주는 작은 부품. 흐름 그림 → MapBuilder/README.md "로딩 창과 시작 무적"
// 언제 붙나: 서버가 들어온 사람의 PlayerController 를 처음 볼 때(UMapSpawnRegionPlanner 가 붙인다).
//   PlayerController 클래스는 팀원 담당이라 파일은 안 고치고, 실행 중에 부품만 붙인다.
// 왜 한 번 보내는 함수(Client RPC)가 아니라 값(bSeated)인가: 부품이 그 사람 컴퓨터에 생기기 전에 함수가 먼저 가면 사라질 수 있다.
//   값은 늦게라도 꼭 도착하고, 도착하면 OnRep_Seated 가 불린다.
UCLASS()
class UMapSeatStatus : public UActorComponent
{
	GENERATED_BODY()

public:
	UMapSeatStatus();

	// 서버: 자리에 세웠다. 같은 컴퓨터의 사람(방장·혼자 하기)은 값 전달이 없으니 바로 로딩 창을 닫는다.
	void MarkSeated();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION()
	void OnRep_Seated();
	void TellLoadingScreen();

	// 주인(그 사람)에게만 보낸다. 다른 사람은 몰라도 된다.
	UPROPERTY(ReplicatedUsing = OnRep_Seated)
	bool bSeated = false;
};
