// 서버가 보낸 위치를 매 프레임 부드럽게 따라가기(2026-09-28 멀티).
//
// 왜: 탱크·날아다니는 차는 물리가 아니라 서버 코드가 SetActorLocation 으로 옮긴다. 클라이언트는 서버 위치가 올 때마다
//   그 자리로 순간이동했는데, 위치는 1초에 수십 번만 오고(서버 틱·망 속도) 화면은 그보다 자주 그려서 차가 "버버벅" 끊겼다
//   (9/28 사용자 PIE: 두 번째 창에서 날아다니는 차가 떨림). 모는 본인(서버가 따로 보내는 운전자 위치)도, 구경하는 사람(엔진 복제
//   위치)도 같은 문제였다.
// 어떻게: 받은 위치와 직전 위치로 속도를 어림해 "지금쯤 있을 자리"(받은 위치 + 속도 × 지난 시간, 최대 0.25초)를 만들고,
//   그 자리로 매 프레임 조금씩 다가간다(지수 보간). 10m 넘게 어긋나면(순간이동·리스폰) 바로 옮긴다.
// 비용: 탈것 하나에 벡터 몇 개. 틱마다 SetActorLocationAndRotation 한 번.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

struct FPGNetPoseSmoother
{
	FVector Target = FVector::ZeroVector;
	FQuat TargetRotation = FQuat::Identity;
	FVector Velocity = FVector::ZeroVector;
	double TargetAt = -1.0;
	bool bHasTarget = false;

	// 서버 위치를 받았다(OnRep·PostNetReceiveLocationAndRotation 에서).
	void Receive(const FVector& Location, const FRotator& Rotation, double Now)
	{
		if (bHasTarget && Now - TargetAt > 0.005)
		{
			const FVector Measured = (Location - Target) / static_cast<float>(Now - TargetAt);
			// 한 번 튄 값에 끌려가지 않게 반만 섞는다. 말도 안 되게 빠르면(순간이동) 속도를 버린다.
			Velocity = Measured.SizeSquared() > FMath::Square(20000.0f) ? FVector::ZeroVector : FMath::Lerp(Velocity, Measured, 0.5f);
		}
		Target = Location;
		TargetRotation = Rotation.Quaternion();
		TargetAt = Now;
		bHasTarget = true;
	}

	// 매 프레임: 액터를 "지금쯤 있을 자리" 쪽으로. 받은 게 없으면 false.
	bool Step(AActor* Owner, double Now, float DeltaSeconds, float Sharpness = 15.0f)
	{
		if (!bHasTarget || !Owner)
			return false;
		if (!IsEnabled())
		{
			Owner->SetActorLocationAndRotation(Target, TargetRotation, false, nullptr, ETeleportType::TeleportPhysics);
			return true;
		}
		const float Ahead = static_cast<float>(FMath::Clamp(Now - TargetAt, 0.0, 0.25));
		const FVector Want = Target + Velocity * Ahead;
		const FVector Current = Owner->GetActorLocation();
		const float Alpha = 1.0f - FMath::Exp(-Sharpness * FMath::Max(DeltaSeconds, 0.0f));
		FVector NewLocation;
		FQuat NewRotation;
		if (FVector::DistSquared(Current, Want) > FMath::Square(1000.0f))
		{
			NewLocation = Want;
			NewRotation = TargetRotation;
		}
		else
		{
			NewLocation = FMath::Lerp(Current, Want, Alpha);
			NewRotation = FQuat::Slerp(Owner->GetActorQuat(), TargetRotation, Alpha);
		}
		Owner->SetActorLocationAndRotation(NewLocation, NewRotation, false, nullptr, ETeleportType::TeleportPhysics);
		return true;
	}

	// 콘솔 PG.NetSmooth 0 이면 끈다(비교 시험용). PGNetPoseSmoother.cpp
	static PROJECTPG_API bool IsEnabled();
	// 비교 시험용 값 그대로(0 = 바로 옮김, 1 = 기본, 2 = 비행차 운전자도 따라가기만).
	static PROJECTPG_API int32 Mode();

	void Reset()
	{
		bHasTarget = false;
		Velocity = FVector::ZeroVector;
	}
};
