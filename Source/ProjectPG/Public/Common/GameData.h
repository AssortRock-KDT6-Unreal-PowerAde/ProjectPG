// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameDefine.h"
#include "GameData.generated.h"

USTRUCT(BlueprintType)
struct FAbilData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAbilType Type;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value;

	void CopyFrom(const FAbilData& other)
	{
		Type = other.Type;
		Value = other.Value;
	}

	void CopyFrom(EAbilType type, float value)
	{
		Type = type;
		Value = value;
	}
};