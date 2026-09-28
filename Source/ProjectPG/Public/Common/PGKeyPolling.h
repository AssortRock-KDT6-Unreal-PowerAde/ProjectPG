// 키를 직접 읽는 임시 입력. Enhanced Input 자산이 없는 검증 캐릭터·로봇·차량이 같은 코드를 세 번 갖고 있어서 모았다.
// 팀 캐릭터는 Enhanced Input 으로 같은 함수(BeginInteract, Attack, Mount…)를 부르면 되고, 이 파일은 안 쓴다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"

namespace PGKeyPolling
{
	// 시험용 "눌린 키"(9/27 멀티 시험 PG.NetRideTest). 화면 없는 클라이언트는 키를 누를 수 없어서, 시험 명령이 여기에 키를 넣으면
	//   이 컴퓨터 사람의 키가 눌린 것으로 본다. 평소에는 비어 있다.
	inline TSet<FKey>& TestKeysDown()
	{
		static TSet<FKey> Keys;
		return Keys;
	}

	// 키가 눌렸나(실제 키 또는 시험용 키).
	inline bool IsDown(const APlayerController* PC, const FKey& Key)
	{
		return PC->IsInputKeyDown(Key) || (PC->IsLocalController() && TestKeysDown().Contains(Key));
	}

	// W/S → X(앞뒤), A/D → Y(좌우). -1 ~ 1.
	inline FVector2D ReadWasd(const APlayerController* PC)
	{
		const float Forward = (IsDown(PC, EKeys::W) ? 1.0f : 0.0f) - (IsDown(PC, EKeys::S) ? 1.0f : 0.0f);
		const float Right = (IsDown(PC, EKeys::D) ? 1.0f : 0.0f) - (IsDown(PC, EKeys::A) ? 1.0f : 0.0f);
		return FVector2D(Forward, Right);
	}

	// 컨트롤 회전의 요(yaw) 기준 앞·옆 방향으로 이동 입력.
	inline void ApplyWasdMovement(APlayerController* PC, APawn* Pawn)
	{
		const FRotator YawRotation(0.0f, PC->GetControlRotation().Yaw, 0.0f);
		const FVector2D Axes = ReadWasd(PC);
		Pawn->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X), Axes.X);
		Pawn->AddMovementInput(FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y), Axes.Y);
	}

	// 마우스 이동을 시선 회전으로.
	inline void ApplyMouseLook(APlayerController* PC, APawn* Pawn, float YawScale = 0.8f, float PitchScale = 0.6f)
	{
		float MouseX = 0.0f;
		float MouseY = 0.0f;
		PC->GetInputMouseDelta(MouseX, MouseY);
		Pawn->AddControllerYawInput(MouseX * YawScale);
		Pawn->AddControllerPitchInput(-MouseY * PitchScale);
	}

	// "이번 프레임에 눌렸다"(누르고 있는 동안 한 번만). bWasDown 은 호출자가 키마다 하나씩 들고 있는다.
	inline bool WasPressed(const APlayerController* PC, const FKey& Key, bool& bWasDown)
	{
		const bool bDown = IsDown(PC, Key);
		const bool bPressed = bDown && !bWasDown;
		bWasDown = bDown;
		return bPressed;
	}

	// "이번 프레임에 떼졌다".
	inline bool WasReleased(const APlayerController* PC, const FKey& Key, bool& bWasDown)
	{
		const bool bDown = IsDown(PC, Key);
		const bool bReleased = !bDown && bWasDown;
		bWasDown = bDown;
		return bReleased;
	}
}
