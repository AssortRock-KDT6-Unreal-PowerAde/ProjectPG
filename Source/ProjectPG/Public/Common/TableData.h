#pragma once

#include "CoreMinimal.h"
#include "InputMappingContext.h"
#include "Actor/EquipActor.h"
#include "Engine/DataTable.h"
#include "Common/GameDefine.h"
#include "Common/GameData.h"
#include "TableData.generated.h"

USTRUCT(BlueprintType)
struct FTablePathRow : public FTableRowBase
{
	GENERATED_BODY()

	// 테이블(DataTable)의 경로를 나타냅니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString Path = TEXT("");

	//데이터 테이블을 사용할 지 말지 여부를 결정합니다.
	//만약 UseThis가 true라면 해당 데이터 테이블을 사용합니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool UseThis = true;
};

USTRUCT(BlueprintType)
struct FDefineTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int IntValue = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float FloatValue = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString StringValue = TEXT("");
};

USTRUCT(BlueprintType)
struct FItemBackpackTable : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 BackpackID = 0; //아이템아이디가 들어감

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FIntPoint SlotSize = (0, 0);
};

USTRUCT(BlueprintType)
struct FItemTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	FName UniqueID = TEXT("");

	UPROPERTY(EditAnywhere)
	FName ItemID = TEXT("");

	UPROPERTY(EditAnywhere)
	FName DisPlayName = TEXT("");


	UPROPERTY(EditAnywhere)
	EItemType ItemType = EItemType::ETC;


	UPROPERTY(EditAnywhere)
	EEquipSlot EquipSlot = EEquipSlot::MAX;

	UPROPERTY(EditAnywhere)
	int32 MaxStack = 1;

	//아이템이 차지하는 격차 크기 
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FIntPoint GridSize = FIntPoint(1, 1);

	UPROPERTY(EditAnywhere)
	UTexture2D* Icon;

	UPROPERTY(EditAnywhere)
	FString Description;

	UPROPERTY(EditAnywhere)
	UStaticMesh* WorldMesh;

public:
	FString GetItemTypeString() const;
};


USTRUCT(BlueprintType)
struct FEquipTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	int32 ItemID;

	UPROPERTY(EditAnywhere)
	EEquipSlot EquipType;

	UPROPERTY(EditAnywhere)
	FName SocketName;

	//UPROPERTY(EditAnywhere)
	//TArray<FAbilityData> Abilities;

	UPROPERTY(EditAnywhere)
	TSubclassOf<AEquipActor> EquipActorClass;
};

USTRUCT(BlueprintType)
struct FDropTableaRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	int32 MonsterID = 0;

	UPROPERTY(EditAnywhere)
	TArray<FDropItemData> DropItems;
};

USTRUCT(BlueprintType)
struct FPlayerDefaultActionTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TObjectPtr<UInputMappingContext> InputMappingContext;

	UPROPERTY(EditAnywhere)
	TArray<FTaggedNativeAction> TaggedNativeActions;

	UPROPERTY(EditAnywhere)
	TArray<FTaggedAbility> TaggedAbilities;
};
