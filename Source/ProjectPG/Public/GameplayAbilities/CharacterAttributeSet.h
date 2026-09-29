// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "AttributeSet.h"
#include "CharacterAttributeSet.generated.h"

#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName) \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 * 
 */
UCLASS()
class PROJECTPG_API UCharacterAttributeSet : public UAttributeSet
{
	GENERATED_BODY()

public:
	// [멀티 임시수정 2026-09-27 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md)
	// 무엇: 속성마다 ReplicatedUsing=OnRep_XXX 를 붙이고, 아래 GetLifetimeReplicatedProps·OnRep 함수들을 더했다.
	// 왜: 전에는 속성이 복제되지 않아 서버에서만 값(체력 100, 걷기 속도 300 …)이 들어가고 클라이언트에서는 전부 0이었다.
	//   걷기 동작 속도 = 이동 속도 ÷ 걷기 속도(CustomAnimInstance)라 클라에서 0 ÷ 0 이 되어, 서 있어도 걷는 동작이 나왔다.
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Health)
	FGameplayAttributeData Health;
	ATTRIBUTE_ACCESSORS(UCharacterAttributeSet, Health)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxHealth)
	FGameplayAttributeData MaxHealth;
	ATTRIBUTE_ACCESSORS(UCharacterAttributeSet, MaxHealth)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Stamina)
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UCharacterAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MaxStamina)
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UCharacterAttributeSet, MaxStamina)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_WalkSpeed)
	FGameplayAttributeData WalkSpeed;
	ATTRIBUTE_ACCESSORS(UCharacterAttributeSet, WalkSpeed)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SprintSpeed)
	FGameplayAttributeData SprintSpeed;
	ATTRIBUTE_ACCESSORS(UCharacterAttributeSet, SprintSpeed)

protected:
	// 값이 클라이언트에 도착했을 때 GAS 에 "바뀌었다" 고 알린다(GAMEPLAYATTRIBUTE_REPNOTIFY). 이게 없으면 예측·효과 계산이 옛 값을 본다.
	UFUNCTION()
	void OnRep_Health(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxHealth(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_WalkSpeed(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_SprintSpeed(const FGameplayAttributeData& OldValue);
	// [멀티 임시수정] 끝
};