// 변신해서 나온 차가 한참 버려져 있으면 도로 여고생으로 돌아간다. (2026-09-20)
//
// 왜: 차를 어디 몰고 가서 버리면 스타터 지역의 동선(연료통 → 여고생 → 변신 → 비행)이 통째로 끊긴다.
//   사용자 요구(9/20): "하차하면 좀 시간 지나서 다시 여고생으로 돌아오게 할 수 있나?"
// 왜 컴포넌트인가: 변신해서 나온 그 차에만 붙인다(APGTransformNPCActor 가 스폰할 때 붙인다).
//   팩 원본 차나 스포너가 놓은 차는 그대로다 — 비행 키트(UPGFlightKitComponent)와 같은 방식.
//
// 돌아간 여고생은 "이미 연료를 받은 상태"로 선다. 연료통을 또 주워 오게 하는 건 번거롭기만 하다
//   (사용자 9/20: "한 번 먹이면 계속 쓸 수 있게"). 그래서 그 다음부터는 F 한 번이면 바로 변신한다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PGCarRevertComponent.generated.h"

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGCarRevertComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGCarRevertComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 아무도 안 타고 멈춰 있은 지 이만큼 지나면 돌아간다(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	float RevertSeconds = 60.0f; // 20초는 잠깐 내려 둘러보는 사이에 사라져 버렸다(9/20 PIE)

	// 이보다 느리면 "멈춰 있다"로 본다(cm/s). 공중에 있거나 굴러가는 중에는 안 돌아간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	float StillSpeed = 60.0f;

protected:
	void Revert();
	// 아래에 밟을 것이 없나. 공중에서 역변신하면 여고생이 하늘에 생긴다(9/20 PIE 에서 실제로 나왔다).
	bool IsInAir() const;
	bool bPausedLogged = false;

	// 돌아가기 전에 미리 알린다(차가 소리 없이 사라지면 버그로 보인다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Transform")
	float WarnSeconds = 10.0f;
	bool bWarned = false;

	float IdleTime = 0.0f;
};
