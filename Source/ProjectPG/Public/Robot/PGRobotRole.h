// 로봇의 역할(보스 / 탑승용) — 로봇이 "누구로서" 행동하는지를 한 곳에 모은 전략 객체.
//
// 왜 만들었나(2026-09-26 SOLID — 리스코프·개방폐쇄): APGRobotCharacter 한 클래스가 bRideable 값 하나로 11군데에서
//   if 로 갈라졌다(크기·깨우기·밟기·공격 피해·받는 피해·소품 밀기·죽을 때 피날레…). 새 역할(예: 호위 로봇)을 넣으려면
//   그 11군데를 다 찾아 고쳐야 했고, "탈것인데 몬스터" 인 자리에서 부모(몬스터)의 가정과 어긋나는 행동이 if 뒤에 숨어 있었다.
//   이제 역할마다 자기 행동을 가진 클래스이고, 로봇은 GetRole() 한 곳에서 역할을 고른 뒤 맡기기만 한다.
// 상태가 없다(판단·행동만): 기본 인스턴스(GetDefault)를 그대로 쓴다 — 레벨·BP 에 저장될 것이 없어 BP_PGRobot 은 그대로다.
// 부모 클래스와의 관계: 로봇의 부모는 팀원 몬스터 클래스(APGMonsterCharacter)다. 팀원이 새 몬스터 클래스를 만들면
//   PGRobotCharacter.h 의 부모 한 줄만 바꾸면 되고, 역할 코드는 그대로다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PGRobotRole.generated.h"

class APGRobotCharacter;
class APGMonsterCharacter;

UCLASS(Abstract)
class PROJECTPG_API UPGRobotRole : public UObject
{
	GENERATED_BODY()

public:
	virtual bool IsRideable() const { return false; }
	// BeginPlay: 역할에 맞게 크기·걸음·카메라·AI 를 갖춘다.
	virtual void OnBeginPlay(APGRobotCharacter& Robot) const {}
	// F 로 탈 수 있는 역할인가(몸 상태 검사는 로봇이 한다).
	virtual bool CanBeMounted() const { return false; }
	// 소품 밀쳐내기 세기(2.5배 크기 대비 비율)와, 지금 밀쳐내도 되는가.
	virtual float GetKnockRatio(const APGRobotCharacter& Robot) const;
	virtual bool CanKnockProps(const APGRobotCharacter& Robot) const { return true; }
	// 받는 피해 조정(보스는 중장비에게 더 아프다).
	virtual float ModifyIncomingDamage(const APGRobotCharacter& Robot, float Damage, const AActor* Causer) const { return Damage; }
	// 이 역할로 한 방 칠 때 상대에게 주는 피해.
	virtual float AttackDamageFor(const APGRobotCharacter& Robot, const APGMonsterCharacter* Target) const;
	// 매 틱: 사람이 탔을 때 / 비어 있을 때(서버).
	virtual void TickRidden(APGRobotCharacter& Robot) const {}
	virtual void TickUnridden(APGRobotCharacter& Robot) const {}
	// 죽었을 때(서버). 보스는 피날레를 연다.
	virtual void OnDied(APGRobotCharacter& Robot) const {}
};

// 보스: 몬스터 AI 가 붙는 적. 크게 키우고, 중장비에게 더 아프고, 쓰러지면 피날레.
UCLASS()
class PROJECTPG_API UPGRobotBossRole : public UPGRobotRole
{
	GENERATED_BODY()

public:
	virtual void OnBeginPlay(APGRobotCharacter& Robot) const override;
	virtual float GetKnockRatio(const APGRobotCharacter& Robot) const override;
	virtual float ModifyIncomingDamage(const APGRobotCharacter& Robot, float Damage, const AActor* Causer) const override;
	virtual void OnDied(APGRobotCharacter& Robot) const override;
};

// 탑승용: AI 없이 서 있다가 플레이어가 F 로 탄다. 타면 작은 몹을 밟고, 한 방이 세다. 비어 있으면 가까이 오는 사람에 맞춰 깨고 잔다.
UCLASS()
class PROJECTPG_API UPGRobotRideRole : public UPGRobotRole
{
	GENERATED_BODY()

public:
	virtual bool IsRideable() const override { return true; }
	virtual void OnBeginPlay(APGRobotCharacter& Robot) const override;
	virtual bool CanBeMounted() const override { return true; }
	virtual float GetKnockRatio(const APGRobotCharacter& Robot) const override;
	virtual bool CanKnockProps(const APGRobotCharacter& Robot) const override;
	virtual float AttackDamageFor(const APGRobotCharacter& Robot, const APGMonsterCharacter* Target) const override;
	virtual void TickRidden(APGRobotCharacter& Robot) const override;
	virtual void TickUnridden(APGRobotCharacter& Robot) const override;
};
