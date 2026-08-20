// Fill out your copyright notice in the Description page of Project Settings.

#include "Input/DefaultInput.h"
#include "InputMappingContext.h"
#include "InputAction.h"

#define REGISTER_OBJECT(ClassName, MemberName, Path) \
{ \
	ConstructorHelpers::FObjectFinder<ClassName> foundObject(Path); \
	if (foundObject.Succeeded()) \
		MemberName = foundObject.Object; \
}

UDefaultInput::UDefaultInput()
{
	REGISTER_OBJECT(UInputMappingContext, InputMappingContext,
	                TEXT("/Script/EnhancedInput.InputMappingContext'/Game/PG/Input/IMC_Player.IMC_Player'"));
	REGISTER_OBJECT(UInputAction, IA_Move,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Move.IA_Move'"));
	REGISTER_OBJECT(UInputAction, IA_Look,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Look_Mouse.IA_Look_Mouse'"));
	REGISTER_OBJECT(UInputAction, IA_Crouch,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Crouch.IA_Crouch'"));
	REGISTER_OBJECT(UInputAction, IA_Interaction,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Interaction.IA_Interaction'"));
	REGISTER_OBJECT(UInputAction, IA_Ability_Sprint,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Ability_Sprint.IA_Ability_Sprint'"));
	REGISTER_OBJECT(UInputAction, IA_Ability_Jump,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Ability_Jump.IA_Ability_Jump'"));
	REGISTER_OBJECT(UInputAction, IA_Weapon_Fire,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Weapon_Fire.IA_Weapon_Fire'"));
	REGISTER_OBJECT(UInputAction, IA_Weapon_Scope,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Weapon_Scope.IA_Weapon_Scope'"));
	REGISTER_OBJECT(UInputAction, IA_Weapon_Reload,
	                TEXT("/Script/EnhancedInput.InputAction'/Game/PG/Input/IA_Weapon_Reload.IA_Weapon_Reload'"));
}
