// Fill out your copyright notice in the Description page of Project Settings.


#include "GameplayAbilities/CharacterAttributeSet.h"

// [멀티 임시수정 2026-09-27 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md)
// 속성 복제. REPNOTIFY_Always: 값이 같아도 알림을 보낸다 — 클라가 예측으로 먼저 바꾼 값을 서버 값으로 되돌릴 때도 GAS 가 알아야 해서(GAS 표준 방식).
#include "Net/UnrealNetwork.h"

void UCharacterAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, Health, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, MaxHealth, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, Stamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, MaxStamina, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, WalkSpeed, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UCharacterAttributeSet, SprintSpeed, COND_None, REPNOTIFY_Always);
}

void UCharacterAttributeSet::OnRep_Health(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, Health, OldValue); }
void UCharacterAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, MaxHealth, OldValue); }
void UCharacterAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, Stamina, OldValue); }
void UCharacterAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, MaxStamina, OldValue); }
void UCharacterAttributeSet::OnRep_WalkSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, WalkSpeed, OldValue); }
void UCharacterAttributeSet::OnRep_SprintSpeed(const FGameplayAttributeData& OldValue) { GAMEPLAYATTRIBUTE_REPNOTIFY(UCharacterAttributeSet, SprintSpeed, OldValue); }
// [멀티 임시수정] 끝