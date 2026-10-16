#pragma once

#include "CoreMinimal.h"
#include "InputMappingContext.h"
#include "Equipments/Equipment.h"
#include "Engine/DataTable.h"
#include "Common/GameDefine.h"
#include "Common/GameData.h"
#include "TableData.generated.h"

class USkeletalMesh;

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
struct FCharacterTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<USkeletalMesh> SkeletalMesh;

	// 캐릭터 기준 메시의 상대 위치, 회전, 스케일 조정값.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FTransform RootMeshTransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FName, TObjectPtr<USkeletalMesh>> ChildSkeletalMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<EWeaponType, FWeaponAnimationSet> WeaponAnimations;
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
	int32 ItemID = 0;

	UPROPERTY(EditAnywhere)
	EEquipSlot EquipType = EEquipSlot::MAX;

	UPROPERTY(EditAnywhere)
	TObjectPtr<UMaterialInterface> HolsterPistolMaterial;

	UPROPERTY(EditAnywhere)
	TSubclassOf<UAttributeSet> Attributes;

	UPROPERTY(EditAnywhere)
	TSubclassOf<AEquipment> EquipActorClass;

	UPROPERTY(EditAnywhere)
	TMap<FName, FSkeletalMeshEquipmentData> SkeletalMeshes;

	UPROPERTY(EditAnywhere)
	TMap<FName, FStaticMeshEquipmentData> StaticMeshes;

	UPROPERTY(EditAnywhere)
	TMap<FName, FActorEquipmentData> ActorEquipments;

	// 탄창/탄약 소지 장비. 중복 장착해도 소지 한도는 최대 2배입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bDoublesAmmoCapacity = false;
};

// RowName은 ItemTable / EquipTable과 같은 ItemID를 사용합니다.
USTRUCT(BlueprintType)
struct FWeaponTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TSubclassOf<UCustomAnimInstance> AnimBlueprint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EWeaponType WeaponType = EWeaponType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FWeaponHandIKData LeftHandIK;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FFirearmData Firearm;
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
struct FEnemyTableRow : public FTableRowBase
{
	GENERATED_BODY()

	// RowName은 CharacterTable의 캐릭터 ID와 같습니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName WeaponItemID = TEXT("1010");
	// 1은 추가 조준 오차 없음, 0은 최대 10도. 총기 자체 탄퍼짐은 별도입니다.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0", ClampMax="1")) float Accuracy = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float Health = 100.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float MoveSpeed = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float DetectionRange = 2500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="1")) float AttackRange = 1200.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float PatrolRadius = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0")) float CoverSearchRadius = 700.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float CoverWaitTime = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta=(ClampMin="0.1")) float AttackDuration = 1.5f;
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
