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
enum class ETileType : uint8
{
	Spawn,
	Exit,
	Road,
	Obstacle,
	WarZone UMETA(DisplayName = "War Zone"),
	None
};

namespace EMapDirection
{
	UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
	enum Type : uint8
	{
		Up = 1 << 0,
		Down = 1 << 1,
		Left = 1 << 2,
		Right = 1 << 3,
	};
}
