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
	UE_DEFINE_GAMEPLAY_TAG(Input_Weapon_IronSight, "Input.Weapon.Ironsight");

	UE_DEFINE_GAMEPLAY_TAG(Ability_Sprint, "Ability.Sprint");
	UE_DEFINE_GAMEPLAY_TAG(Ability_Jump, "Ability.Jump");

	UE_DEFINE_GAMEPLAY_TAG(State_Sprinting, "State.Sprinting");
	UE_DEFINE_GAMEPLAY_TAG(State_UsingStamina, "State.UsingStamina");
	UE_DEFINE_GAMEPLAY_TAG(State_Jumping, "State.Jumping");

	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Equip, "WeaponAnimation.Equip");
	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Fire_Hip, "WeaponAnimation.Fire.Hip");
	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Fire_Ironsights, "WeaponAnimation.Fire.Ironsights");
	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Reload_Hip, "WeaponAnimation.Reload.Hip");
	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Reload_Ironsights, "WeaponAnimation.Reload.Ironsights");
	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Death_Hip, "WeaponAnimation.Death.Hip");
	UE_DEFINE_GAMEPLAY_TAG(WeaponAnimation_Death_Ironsights, "WeaponAnimation.Death.Ironsights");
};
