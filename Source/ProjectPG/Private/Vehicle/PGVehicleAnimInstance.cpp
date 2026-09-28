#include "Vehicle/PGVehicleAnimInstance.h"

#include "Animation/AnimNodeBase.h"
#include "BoneContainer.h"
#include "ChaosWheeledVehicleMovementComponent.h"
#include "GameFramework/Actor.h"

bool FPGVehicleAnimInstanceProxy::Evaluate(FPoseContext& Output)
{
	// 차체는 레퍼런스 포즈 그대로. 바퀴 본에만 회전(굴러감·조향)과 위치(서스펜션)를 얹는다.
	Output.ResetToRefPose();

	const TArray<FWheelAnimationData>& WheelData = GetWheelAnimData();
	const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
	for (int32 Index = 0; Index < WheelBoneNames.Num() && Index < WheelData.Num(); ++Index)
	{
		const int32 PoseIndex = Bones.GetPoseBoneIndexForBoneName(WheelBoneNames[Index]);
		if (PoseIndex == INDEX_NONE)
			continue;
		const FCompactPoseBoneIndex BoneIndex(PoseIndex);
		FTransform& BoneTransform = Output.Pose[BoneIndex];
		// 엔진 Wheel Controller 노드와 같은 순서: 회전 오프셋을 곱하고, 위치 오프셋을 더한다.
		BoneTransform.SetRotation(FQuat(WheelData[Index].RotOffset) * BoneTransform.GetRotation());
		BoneTransform.AddToTranslation(WheelData[Index].LocOffset);
	}
	return true;
}

void UPGVehicleAnimInstance::NativeInitializeAnimation()
{
	// 부모의 NativeInitializeAnimation 은 private 이라 Super 를 못 부른다. 하는 일(차량 컴포넌트 찾기)을 여기서 직접 한다.
	const AActor* Owner = GetOwningActor();
	const UChaosWheeledVehicleMovementComponent* Vehicle = IsValid(Owner) ? Owner->FindComponentByClass<UChaosWheeledVehicleMovementComponent>() : nullptr;
	if (!Vehicle)
		return;
	SetWheeledVehicleComponent(Vehicle);   // 부모 프록시용
	Proxy.SetWheeledVehicleComponent(Vehicle); // 실제로 쓰는 우리 프록시

	TArray<FName> BoneNames;
	for (const FChaosWheelSetup& Setup : Vehicle->WheelSetups)
		BoneNames.Add(Setup.BoneName);
	Proxy.SetWheelBoneNames(BoneNames);
}
