// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "EngineMinimal.h"
#include "Animation/AnimInstance.h"
#include "Common/GameData.h"
#include "CustomAnimInstance.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API UCustomAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UCustomAnimInstance();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|IK")
	FTransform LeftHandIKTransform = FTransform::Identity;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|IK")
	float LeftHandIKAlpha = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|IK")
	FName LeftHandIKReferenceBone = TEXT("hand_r");

	UPROPERTY()
	TWeakObjectPtr<class AEquipment> LeftHandIKEquipment;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector2D AimOffset;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float NormalizedGroundSpeed;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float MovementDirection;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsAiming : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsCrouched : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bWasJumping : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsFalling : 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 bIsSprinting : 1;

public:
	UFUNCTION(BlueprintCallable, Category = "Weapon|IK")
	void SetLeftHandIKEquipment(class AEquipment* Equipment);

	void ClearLeftHandIKEquipment(const class AEquipment* Equipment);

	virtual void NativeUpdateAnimation(float DeltaSeconds) override;

	void SyncAim(FRotator rotation);
	void SyncAim(float Yaw, float Pitch);

	// [멀티 임시수정 2026-10-19 조준 방향 덜 자주]
	// 다른 사람 캐릭터: 서버가 0.1초마다 보내 주는 조준 방향을 목표로 적어 두고, 화면마다 그쪽으로 부드럽게 따라간다.
	// 왜: 받는 간격이 0.1초라 그대로 넣으면 고개·총구가 계단처럼 뚝뚝 움직인다.
	void SetRemoteAimTarget(float Yaw, float Pitch);

private:
	// 내 캐릭터: 마지막으로 서버에 보낸 조준 방향과 그때 시간(초). 바뀌었을 때만, 0.1초에 한 번만 보낸다.
	FVector2D LastSentAim = FVector2D(MAX_flt, MAX_flt);
	double LastAimSendTime = -1.0;
	// 다른 사람 캐릭터: 따라갈 목표 방향. 한 번이라도 받았으면 bHasRemoteAimTarget 이 켜진다.
	FVector2D RemoteAimTarget = FVector2D::ZeroVector;
	bool bHasRemoteAimTarget = false;
	// [멀티 임시수정] 끝
};
