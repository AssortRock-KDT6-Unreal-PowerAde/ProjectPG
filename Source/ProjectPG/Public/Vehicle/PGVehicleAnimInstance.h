// 차량 바퀴 애니메이션을 애님 블루프린트 없이 C++로.
//
// Chaos 차량은 바퀴 회전·조향·서스펜션 값을 UVehicleAnimationInstance(프록시)가 매 프레임 계산해 주지만,
// 그 값을 본에 "적용"하는 건 애님 그래프의 Wheel Controller 노드다. 그래프를 에디터에서 안 만들고도 바퀴가
// 돌게 하려고, 프록시의 Evaluate 를 직접 구현해 레퍼런스 포즈에 그 값을 얹는다. 엔진 노드가 하는 계산과 같다.
#pragma once

#include "CoreMinimal.h"
#include "VehicleAnimationInstance.h"
#include "PGVehicleAnimInstance.generated.h"

class FPGVehicleAnimInstanceProxy : public FVehicleAnimationInstanceProxy
{
public:
	FPGVehicleAnimInstanceProxy() : FVehicleAnimationInstanceProxy() {}
	FPGVehicleAnimInstanceProxy(UAnimInstance* Instance) : FVehicleAnimationInstanceProxy(Instance) {}

	// WheelSetups 순서와 같은 순서의 본 이름. 엔진이 계산한 WheelAnimData 도 같은 순서다.
	void SetWheelBoneNames(const TArray<FName>& InNames) { WheelBoneNames = InNames; }

	virtual bool Evaluate(FPoseContext& Output) override;

private:
	TArray<FName> WheelBoneNames;
};

UCLASS()
class PROJECTPG_API UPGVehicleAnimInstance : public UVehicleAnimationInstance
{
	GENERATED_BODY()

public:
	virtual void NativeInitializeAnimation() override;

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override { return &Proxy; }
	virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override {}

	// 부모의 프록시 멤버 대신 이걸 쓴다. 부모가 차량 컴포넌트를 자기 프록시에만 넣어 주므로
	// NativeInitializeAnimation 에서 우리 프록시에도 같은 컴포넌트를 넣는다.
	FPGVehicleAnimInstanceProxy Proxy;
};
