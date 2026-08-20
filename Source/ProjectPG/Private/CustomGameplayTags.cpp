// Fill out your copyright notice in the Description page of Project Settings.


#include "CustomGameplayTags.h"

namespace CustomGameplayTags
{
	UE_DEFINE_GAMEPLAY_TAG(Input_Move, "Input.Move");
	UE_DEFINE_GAMEPLAY_TAG(Input_Look_Mouse, "Input.Look.Mouse");
	UE_DEFINE_GAMEPLAY_TAG(Input_Crouch, "Input.Crouch");
	UE_DEFINE_GAMEPLAY_TAG(Input_Interaction, "Input.Interaction");
	UE_DEFINE_GAMEPLAY_TAG(Input_Ability_Sprint, "Input.Ability.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Input_Ability_Jump, "Input.Ability.Jump");
	UE_DEFINE_GAMEPLAY_TAG(Input_Weapon_Fire, "Input.Weapon.Fire");
	UE_DEFINE_GAMEPLAY_TAG(Input_Weapon_Reload, "Input.Weapon.Reload");
	UE_DEFINE_GAMEPLAY_TAG(Input_Weapon_Scope, "Input.Weapon.Scope");

	UE_DEFINE_GAMEPLAY_TAG(Ability_Sprint, "Ability.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Jump, "Ability.Jump");

	UE_DEFINE_GAMEPLAY_TAG(State_Sprinting, "State.Sprinting");
	UE_DEFINE_GAMEPLAY_TAG(State_Jumping, "State.Jumping");
};
