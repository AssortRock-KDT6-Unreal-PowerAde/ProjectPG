// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameDefine.generated.h"

UENUM(BlueprintType)
enum class EAbilType : uint8
{
	Hp UMETA(DisplayName = "HP"),
	MaxHp UMETA(DisplayName = "Max HP"),

	Weight,

	RateOfFire UMETA(DisplayName = "Rate of Fire"),

	Damage,
	Recoil,
	Armor,
	ArmorPiercing UMETA(DisplayName = "AP"),
	Accuracy,
	Range,

	MovementSpeed UMETA(DisplayName = "Movement Speed"),
	ProjectileSpeed UMETA(DisplayName = "Projectile Speed"),

	HealHP UMETA(DisplayName = "Heal HP"),
	HealHunger UMETA(DisplayName = "Heal Hunger"),
	HealThirst UMETA(DisplayName = "Heal Thirst"),
	UseTime UMETA(DisplayName = "Use Time"),

	None,
};
UENUM(BlueprintType)
enum class EMessageType :uint8
{
	Default, oneButton
};
UENUM(BlueprintType)
enum class ESceneType : uint8
{
	LobbyScene, InGameScene,
};
UENUM(BlueprintType)
enum class EEquipSlot : uint8
{
	MainWeapon,
	SubWeapon,
	HelMet,
	Cloth,
	Pants,
	Shose,
	BackPack,
	Accuracy1, Accuracy2, MAX
};
UENUM(BlueprintType)
enum class EItemType : uint8
{
	Weapon, Armor, Consumable, Quest, Bag , ETC
};

UENUM(BlueprintType)
enum class EDropType : uint8
{
	Monster,Chest,Quest
};
UENUM(BlueprintType)
enum class EAbility : uint8
{
	MoveSP UMETA(DisplayName = "MoveSP"),//이동속도
	Damage UMETA(DisplayName = "Damage"),
	ReloadSP UMETA(DisplayName = "ReloadSP"),//재장전 및 변경속도
	FireSP UMETA(DisplayName = "FireSP"),//발사속도
	Noise UMETA(DisplayName = "Noise"),//소음(이동,공격 등 소음발생크기)
	InteractSP UMETA(DisplayName = "InteractSP")//상호작용 속도
};

UENUM(BlueprintType)
enum class eParticletype : uint8
{
	Particle, Niagara, None
};


UENUM(BlueprintType)
enum class EBorderHighlightState : uint8
{
	None        UMETA(DisplayName = "None"),     // 기본 (하이라이트 없음)
	Valid       UMETA(DisplayName = "Valid"),    // 배치 가능 (초록색)
	Invalid     UMETA(DisplayName = "Invalid"),  // 배치 불가 (빨간색)
	Hovered     UMETA(DisplayName = "Hovered"),  // 마우스 호버 (선택 사항)
	Selected    UMETA(DisplayName = "Selected")  // 선택됨 (선택 사항)
};