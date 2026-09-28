#pragma once

#include "CoreMinimal.h"

// "완전히 멈춘 뒤 잠깐 지나야 참" 을 재는 작은 타이머. 차·탱크·로봇의 하차 안내가 같이 쓴다.
// 왜: 속도가 기준 아래로 잠깐 내려갈 때마다 "하차" 문구가 깜빡였다(감속 중·방향 전환 순간). 멈춘 상태가 이어져야 띄운다.
struct FPGStopTimer
{
	float StoppedSince = -1.0f;

	// Speed(cm/s)가 Threshold 이하로 HoldSeconds 동안 이어졌으면 true. 넘으면 처음부터 다시 잰다.
	bool IsSettled(float Speed, float Now, float Threshold = 20.0f, float HoldSeconds = 0.6f)
	{
		if (Speed > Threshold)
		{
			StoppedSince = -1.0f;
			return false;
		}
		if (StoppedSince < 0.0f)
			StoppedSince = Now;
		return Now - StoppedSince >= HoldSeconds;
	}
};
