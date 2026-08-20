// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

namespace CustomGameplayTags
{
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Move);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Look_Mouse);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Crouch);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Interaction);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Ability_Sprint);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Ability_Jump);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Weapon_Fire);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Weapon_Reload);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Input_Weapon_Scope);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Sprint);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(Ability_Jump);

	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Sprinting);
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(State_Jumping);
};
