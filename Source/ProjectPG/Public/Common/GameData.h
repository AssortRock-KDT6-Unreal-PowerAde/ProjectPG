#pragma once

#include "CoreMinimal.h"
#include "GameDefine.h"
#include "GameData.generated.h"

struct FItemTableRow;

USTRUCT(BlueprintType)
struct FAbilData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EAbilType Type  = EAbilType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Value = 0.0f;

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
USTRUCT(BlueprintType)
struct FItemInstance
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGuid GUID;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ItemID =TEXT("");//아이템 

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 StackCount = 0;//stack형일시 사용

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float Durability = 100;//내구도

	// 인벤토리 내 좌상단 시작 위치 (Tile X, Tile Y)
	UPROPERTY(BlueprintReadOnly)
	FIntPoint Position = FIntPoint();
	
	bool bIsRotated = false;//0~4 회전량 (90도 회전)
	bool bEquip = false;//장착여부
	int inventoryType = 0;// 0 ,1 인벤토리 종류(0 main, 1 sub) // 메인창고 인벤 , 인게임에서 사용할 인벤

	// 회전 상태를 반영한 현재 격자 크기 반환 함수
	FIntPoint GetCurrentGridSize(const FItemTableRow* ItemData) const;

};

USTRUCT(BlueprintType)
struct FEquipSlotData
{
	GENERATED_BODY()

	UPROPERTY()
	FItemInstance Item;

	//UPROPERTY()
	//TObjectPtr<class AEquipActor> Actor = nullptr;
};
USTRUCT(BlueprintType)
struct FDropItemData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FName ItemID = TEXT("");

	UPROPERTY(EditAnywhere)
	float DropRate = 0.0f;

	UPROPERTY(EditAnywhere)
	int32 MinCount =0;

	UPROPERTY(EditAnywhere)
	int32 MaxCount =0;
};

