// 탈것(차·탱크·로봇) 3인칭 카메라가 땅 밑으로 들어가지 않게, 고개를 들면 카메라 팔을 그만큼 줄인다. (2026-09-20)
//
// 왜: 탈것 카메라 팔은 충돌 검사를 끈 채(소품·선반에 걸릴 때마다 카메라가 확확 당겨지는 게 불편해서) 쓰는데,
//     하늘을 보려고 고개를 들면 팔이 아래로 돌아 카메라가 땅속으로 들어갔다(9/20 PIE: 화면이 땅 밑으로 잠김).
//     사람 캐릭터는 팔 충돌 검사가 켜져 있어서 괜찮았다.
// 방법: 매 틱 카메라 기둥(팔 시작점) 아래 땅 높이를 재서, 카메라가 땅 위 Clearance 에 머물도록 팔 길이만 줄인다.
//      팔 충돌 검사를 켜지 않으니 소품에 걸려 당겨지는 일은 그대로 없다.
//      내리면 사람 캐릭터가 조종을 넘겨받을 때 원래 값(89.9)으로 돌려놓는다(ACustomPlayerCharacter::Tick).
#pragma once

#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace PGCameraUtil
{
	constexpr float DefaultViewPitchMax = 89.9f;

	// DefaultArmLength: 평소 팔 길이. 고개를 들어 카메라가 땅에 닿을 만큼 내려가면 그만큼만 팔을 줄인다(사람 캐릭터의 팔 충돌과 같은 효과).
	// 처음엔 위쪽 각도 자체를 막았는데, 탱크(팔 13m)는 19도밖에 못 들어 하늘의 전함을 볼 수 없었다(9/20 PIE).
	inline void KeepCameraAboveGround(APawn* Pawn, USpringArmComponent* Arm, float DefaultArmLength, float Clearance = 150.0f, float MinArmLength = 250.0f)
	{
		if (!IsValid(Pawn) || !IsValid(Arm) || !Pawn->IsLocallyControlled())
			return;
		APlayerController* PC = Cast<APlayerController>(Pawn->GetController());
		if (!PC || !PC->PlayerCameraManager)
			return;
		const FVector Pivot = Arm->GetComponentLocation() + FVector(0.0f, 0.0f, Arm->SocketOffset.Z);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGCameraGround), false, Pawn);
		const float GroundZ = Pawn->GetWorld()->LineTraceSingleByChannel(Hit, Pivot, Pivot - FVector(0.0f, 0.0f, 20000.0f), ECC_Visibility, Params)
			? Hit.ImpactPoint.Z : Pivot.Z - 20000.0f;
		const float Room = FMath::Max(Pivot.Z - GroundZ - Clearance, 0.0f);
		// 위쪽 각도 p 일 때 카메라 높이 = 기둥 - 팔길이 × sin(p). 땅 + Clearance 위에 있으려면 팔길이 ≤ Room / sin(p).
		const float Pitch = FRotator::NormalizeAxis(PC->GetControlRotation().Pitch);
		float Wanted = DefaultArmLength;
		if (Pitch > 1.0f)
		{
			const float Sin = FMath::Sin(FMath::DegreesToRadians(Pitch));
			if (Wanted * Sin > Room)
				Wanted = FMath::Max(MinArmLength, Room / Sin);
		}
		Arm->TargetArmLength = Wanted;
		PC->PlayerCameraManager->ViewPitchMax = 80.0f;
	}

	inline void RestoreCameraPitch(APawn* Pawn)
	{
		if (!IsValid(Pawn) || !Pawn->IsLocallyControlled())
			return;
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()); PC && PC->PlayerCameraManager
			&& PC->PlayerCameraManager->ViewPitchMax != DefaultViewPitchMax)
			PC->PlayerCameraManager->ViewPitchMax = DefaultViewPitchMax;
	}
}
