// 탈것 타기·내리기 공통 순서 — 차(APGVehiclePawn)·탱크(APGTankPawn)·로봇(APGRobotCharacter)이 같이 쓴다.
//
// 왜 만들었나(2026-09-26 SOLID 정리): 세 클래스의 Mount/Dismount 가 거의 같은 30줄로 세 번 복사돼 있었다
//   (숨기기·충돌 끄기·붙이기·빙의 / 떼기·보이기·빈 자리 찾기·빙의 되돌리기). 한 곳을 고치면 나머지 두 곳을 잊는다.
//   세 클래스의 부모가 AWheeledVehiclePawn / APawn / ACharacter 로 달라 공통 부모를 만들 수 없으므로, 상속 대신 "같은 순서" 를 함수로 뺐다.
// 탈것마다 다른 것(좌석 확인·시선·주차·걷는 속도·로그)은 각 클래스에 남는다. 여기는 "순서" 만 안다.
// 서버에서만 부른다(빙의는 서버 권한).

#pragma once

#include "CoreMinimal.h"

class APawn;
class APlayerController;
class USceneComponent;

namespace PGRide
{
	// 탑승: 탑승자를 멈추고·숨기고·충돌을 끄고 AttachTo 에 붙인 뒤(RelativeOffset 위치), 컨트롤러를 탈것(Ride)으로 옮긴다.
	// 좌석 확인(Seat->TryEnter)은 부르는 쪽이 먼저 끝낸다.
	PROJECTPG_API void BoardRider(APawn* Ride, APawn* Rider, APlayerController* PC, USceneComponent* AttachTo, const FVector& RelativeOffset);

	// 내릴 자리: 좌석이 준 자리(ExitLocation + 100cm)가 벽에 막혔으면 탈것 둘레 8방향(RingCm 반경, CandidateLiftCm 높이)에서 빈 자리를 찾는다.
	// RingCm <= 0 이면 찾지 않고 좌석 자리를 그대로 쓴다.
	PROJECTPG_API FVector FindExitSpot(const APawn* Ride, const APawn* Rider, const FVector& ExitLocation, float RingCm, float CandidateLiftCm);

	// 하차: 떼고·보이고·충돌을 켜고 StandAt 에 똑바로(탈것의 요 방향) 세운 뒤, 컨트롤러를 탑승자에게 돌려준다.
	// bResetControlRotation: 시야도 똑바로 세운다(탈것이 기울어 있었으면 시야가 비틀려 있으므로).
	PROJECTPG_API void ReleaseRider(APawn* Ride, APawn* Rider, APlayerController* PC, const FVector& StandAt, bool bResetControlRotation);

	// ---- 멀티(9/27) ----
	// 시선(컨트롤 회전)을 정한다. 컨트롤 회전은 복제되지 않아 서버에서만 바꾸면 그 사람 화면은 안 돈다 — 원격이면 클라 RPC 도 보낸다.
	PROJECTPG_API void SetViewRotation(APlayerController* PC, const FRotator& Rotation);
	// 클라이언트: 탈것의 탑승자(복제)가 바뀌었을 때. 탑승자 숨김·붙임은 복제되지만 "충돌 끔" 은 복제되지 않아서
	//   클라에선 숨은 탑승자 캡슐이 차 안에서 부딪히고(차가 제 탑승자에 밀림) 다른 사람 F 에 잡혔다. 새 탑승자는 끄고, 내린 사람은 켠다.
	PROJECTPG_API void OnRiderChangedOnClient(APawn* NewRider, APawn* OldRider);
}
