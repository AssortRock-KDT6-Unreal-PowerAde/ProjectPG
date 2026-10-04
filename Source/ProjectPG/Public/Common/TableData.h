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

// 시작 짐이 들어갈 곳.
UENUM(BlueprintType)
enum class EStarterContainer : uint8
{
	Stash,   // 창고(로비 캐릭터 화면 오른쪽 큰 격자)
	Pocket,  // 주머니
	Equip    // 바로 장착(아이템 표의 장비 칸으로)
};

// 시작 짐 한 줄 (StarterInventoryTable). 처음 로비에 들어왔을 때 갖고 있는 아이템.
// 왜 표로 뺐나: 무엇을 주고 시작할지는 기획 값이라 코드 없이 바꿀 수 있어야 해서.
USTRUCT(BlueprintType)
struct FStarterInventoryRow : public FTableRowBase
{
	GENERATED_BODY()

	// 아이템 표(ItemTable)의 번호.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName ItemID = NAME_None;

	// 개수(쌓이는 아이템만 의미 있음, 아이템 표 MaxStack 을 넘으면 거기까지).
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EStarterContainer Container = EStarterContainer::Stash;
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
	FName ItemID = TEXT("");

	UPROPERTY(EditAnywhere)
	FName DisPlayName = TEXT("");


	UPROPERTY(EditAnywhere)
	EItemType ItemType = EItemType::ETC;


	UPROPERTY(EditAnywhere)
	EEquipSlot EquipSlotType = EEquipSlot::MAX;

	UPROPERTY(EditAnywhere)
	int32 MaxStack = 1;

	//아이템이 차지하는 격차 크기 
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FIntPoint GridSize = FIntPoint(1, 1);

	UPROPERTY(EditAnywhere)
	UTexture2D* Icon = nullptr;

	UPROPERTY(EditAnywhere)
	FString Description;

	UPROPERTY(EditAnywhere)
	UStaticMesh* WorldMesh = nullptr;

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
