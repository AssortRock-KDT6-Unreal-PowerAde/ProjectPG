// Fill out your copyright notice in the Description page of Project Settings.


#include "Equipments/Equippable.h"

// Add default functionality here for any IEquippable functions that are not pure virtual.
void IEquippable::Equip(ACharacter* Character, FName SocketName)
{
}

void IEquippable::Unequip()
{
}

void IEquippable::Attach(FName SocketName)
{
}

void IEquippable::Detach()
{
}
