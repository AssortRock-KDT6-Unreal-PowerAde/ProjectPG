// 몬스터 모양을 클라이언트에도 — 우리 스포너가 붙이는 부품(2026-09-27 멀티).
//
// 왜: 스포너가 서버에서 몬스터 모양(Visuals: 메시·애니·캡슐 크기)을 넣지만, 그 값은 몬스터 클래스(팀원 APGMonsterCharacter)에서
//   복제되지 않는다. 클라이언트는 몬스터 클래스 기본값(슬라임)으로 그려서, 모든 몬스터가 슬라임 모양이었고 캡슐 크기도 달라
//   떠 있거나 땅에 박혔다(멀티 점검 A5).
// 어떻게: 팀원 클래스는 고치지 않는다. 스폰할 때 이 부품을 붙여 "모양 이름(프리셋)" 만 복제하고, 클라이언트에서는
//   몬스터의 공개 함수(GetPresetVisuals → SetVisuals)로 서버와 같은 모양을 입힌다. 몬스터를 아는 곳은 여기와 스포너뿐이다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PGMonsterLookComponent.generated.h"

UCLASS(ClassGroup = (PG))
class PROJECTPG_API UPGMonsterLookComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGMonsterLookComponent();

	// 서버: 스폰한 몬스터에 모양 이름을 붙인다(없으면 만든다).
	static void Attach(AActor* Monster, FName Preset);

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	UPROPERTY(ReplicatedUsing = OnRep_Preset)
	FName Preset;

	UFUNCTION()
	void OnRep_Preset();

	// 클라이언트에서 몬스터에 모양을 입힌다. 몬스터 BeginPlay 가 기본 모양(슬라임)으로 덮어쓰므로 BeginPlay 다음 틱에도 한 번 더.
	void ApplyOnClient();
};
