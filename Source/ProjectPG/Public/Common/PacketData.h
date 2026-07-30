// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "PacketData.generated.h"

USTRUCT(BlueprintType)
struct FS_ReqTest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Message;
};

USTRUCT(BlueprintType)
struct FC_ResTest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Message;
};

USTRUCT(BlueprintType)
struct FM_RepTest
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText Message;
};
