// 작은 소품을 "밀쳐내기". 차·로봇이 부딪힌 벽돌·드럼통·벽 판자·울타리를 날려 보낸다.
//
// 처음엔 부딪힌 메시 자체를 물리로 바꿨는데, 팩 메시 상당수가 ComplexAsSimple 콜리전이라 물리 시뮬레이션이
// 아예 안 켜진다(메시지 로그: "ComplexAsSimple 콜리전이 있습니다"). 메시 설정을 에디터에서 하나씩 고치는 대신,
// 원본은 없애고 "물리 상자(루트) + 보이기만 하는 메시(자식)" 대리 액터를 그 자리에 만든다.
// 상자는 메시 바운드 크기라 벽 판자·드럼통·블록은 모양이 거의 맞고, 어떤 콜리전 설정이든 무조건 날아간다.
// PCG 가 뿌린 돌·울타리(HISM 인스턴스)도 같은 방식: 인스턴스 하나를 떼어내 대리 액터로.
//
// 쓰는 쪽: APGVehiclePawn::KnockAhead(앞 상자 스윕), APGRobotCharacter::NotifyHit/KnockAhead(보스).
#pragma once

#include "CoreMinimal.h"

class AActor;
class UPrimitiveComponent;
class UStaticMesh;
class UWorld;
struct FHitResult;

namespace PGPhysicsUtil
{
	// 이 태그가 붙은 컴포넌트(바닥 타일·도로·호수·산)는 절대 날리지 않는다.
	inline const FName TerrainTag = TEXT("PGTerrain");
	// 이 태그가 액터나 컴포넌트에 붙어 있으면 거대 로봇·드래곤·전함·탱크 누구도 부수지 못한다.
	// 상점 NPC 부스(중립 상인, 방어막 연출)·탈출구·퀘스트 오브젝트용. 에디터에서 액터 Tags 에 PGProtected 를 넣어도 된다.
	inline const FName ProtectedTag = TEXT("PGProtected");
	// 이 태그가 붙은 액터는 부딪히면 "보이는 메시 조각을 전부 잔해로 날리고 액터째 없앤다"(문짝 등).
	// 왜 태그인가(2026-09-26 의존 역전): 전에는 이 범용 유틸이 Cast<APGDoorActor> 로 문 클래스를 직접 알았다 —
	//   아래층(물리 유틸)이 위층(게임 오브젝트)을 아는 방향 역전. 이제 문이 스스로 태그를 달고, 유틸은 태그만 본다.
	inline const FName ShatterWholeTag = TEXT("PGShatterWhole");

	// 메시 + 월드 트랜스폼으로 물리 대리 액터를 만든다. 성공하면 액터, 실패하면 nullptr.
	// Impulse 는 힘이 아니라 "속도 변화(cm/s)". 돌·바위·콘크리트(이름으로 판별)는 무겁고 덜 튄다.
	// Source: 원본 컴포넌트(있으면 재질을 복사하고 바닥 찾기에서 뺀다). 기본은 미리 만들어 둔 잔해 묶음(UPGDebrisSubsystem)에서 꺼내 쓴다.
	// bFallOnly: 받침 무너짐 — 크기와 상관없이 넘어지지 않고 그대로 떨어진다(가벼운 흉내, 진짜 물리 아님).
	//   전에는 UPGDebrisSubsystem::bCollapseFall 전역 스위치로 넘겼다(인자로 줄 것을 전역 상태로 해결한 것, 9/26 정리).
	AActor* SpawnKnockProxy(UWorld* World, UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse, float MassKg, AActor* Instigator, const UPrimitiveComponent* Source = nullptr, bool bFallOnly = false);

	// 날려도 되는 메시인지. 덤불·풀·가로등·기둥은 아니오.
	bool IsKnockableMesh(const UStaticMesh* Mesh, const FVector& Scale);

	// 나무인지(이름 또는 가늘고 높은 형태). 나무는 날리지 않고 줄기 기둥으로 만들어 "꺾여 쓰러지게" 한다. 차·폰과는 안 부딪힌다.
	bool IsTreeMesh(const UStaticMesh* Mesh, const FVector& Scale);

	// 부딪힌 컴포넌트가 작은 소품이면 대리 액터로 바꿔 날린다. 바꿨으면 true.
	// MaxRadius: 이보다 큰 것(건물 벽·컨테이너)은 손대지 않는다. Hit.Item 이 인스턴스 번호(HISM)일 때 그 인스턴스만 처리.
	// LowHeight: 0 보다 크면, 높이가 이 이하인 메시는 MaxRadius 보다 넓어도 날린다(거대 보스 무릎 아래의 넓은 판때기 지붕·단층 벽).
	// bFallOnly: SpawnKnockProxy 와 같다(받침 무너짐 경로만 true).
	bool TryKnockProp(UPrimitiveComponent* Component, const FHitResult& Hit, const FVector& Impulse, AActor* Instigator, float MaxRadius = 250.0f, float MassKg = 60.0f, float LowHeight = 0.0f, bool bFallOnly = false);

	// 멀티(9/27) 클라이언트: 서버가 날린 소품 하나를 이 화면에서 따라 한다(APGKnockRelay 가 부른다).
	// 그 자리에서 같은 메시(인스턴스·레벨 메시)를 찾아 지우고 잔해를 날린다. 못 찾으면(이미 없어졌으면) 잔해만 날린다.
	void ApplyRemoteKnock(UWorld* World, UStaticMesh* Mesh, const FTransform& WorldTransform, const FVector& Impulse, float MassKg, bool bFallOnly);
	// 멀티(9/28) 클라이언트: 내가 모는 차·로봇이 부딪힌 소품은 이 화면에서 먼저 부순다(서버 알림을 기다리면 그동안 소품에 막혀
	//   멈칫했다 — 9/28 사용자 PIE "충돌체 박을 때 딜레이"). 먼저 부순 것을 적어 두고, 나중에 온 서버 알림은 건너뛴다(두 번 날지 않게).
	void NotePredictedKnock(UWorld* World, const UStaticMesh* Mesh, const FVector& Pivot);
	inline int32 PredictedKnockCount = 0;
	inline int32 PredictedKnockConfirmed = 0;
	// 클라이언트가 받은 소품 알림 수 / 그중 원본을 찾아 지운 수(시험 로그용).
	inline int32 RemoteKnockCount = 0;
	inline int32 RemoteKnockMatched = 0;
	// 이 컴퓨터에서 잔해를 실제로 만든 수(서버·클라 모두, 프레임 드랍 시험 로그용 — PG.NetSmashTest).
	inline int32 DebrisMadeCount = 0;
	// 클라이언트가 서버 부수기를 따라 할 때 든 시간(ms, 누적) — 찾기(겹침 검사)·잔해 만들기·원본 지우기. PG.NetSmashTest 가 1초마다 찍고 비운다.
	inline double RemoteFindMs = 0.0;
	inline double RemoteLaunchMs = 0.0;
	inline double RemoteRemoveMs = 0.0;
}
