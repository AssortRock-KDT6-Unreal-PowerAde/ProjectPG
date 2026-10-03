// 맵 바닥에 놓인 아이템 하나.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldItemActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;

// 바닥에 떨어져 있는 아이템 (줍기 전).
// 게임에서: 창고 구석의 권총, 폐허 바닥의 붕대처럼 "주울 수 있는 물건" 하나.
// C++ 에는 동작과 칸만 둔다: 어떤 아이템인지(ItemID·개수), 모양 끼우기, 바닥에 내려놓기.
// 보이는 것(표에 메시가 없을 때 쓸 대신 모양, 반짝임·이름표 같은 효과)은 BP_WorldItem 에서 고른다.
// 줍기(F 키·인벤토리에 넣기)는 캐릭터·인벤토리 담당 형님 쪽 일이라 여기서는 안 한다.
//   형님 코드는 GetItemID()·GetQuantity() 를 읽고, 인벤토리 AddItemByID(ItemID, …, Quantity) 후 이 액터를 지우면 된다.
UCLASS()
class PROJECTPG_API AWorldItemActor : public AActor
{
	GENERATED_BODY()

public:
	AWorldItemActor();

	// 어떤 아이템인지 정하고, 아이템 표(ItemTable)의 WorldMesh 로 모양을 바꾼다.
	// 표에 메시가 비어 있으면 BP 에서 고른 FallbackMesh 를 쓴다(물건이 안 보이는 것보다 낫다).
	void SetItem(FName InItemID, int32 InQuantity);

	// 메시 아래면이 FloorPoint 높이에 닿게 내려놓는다. 메시마다 기준점(피벗)이 달라서 크기를 재서 맞춘다.
	void PlaceOnFloor(const FVector& FloorPoint);

	UFUNCTION(BlueprintPure, Category = "Item")
	FName GetItemID() const { return ItemID; }

	UFUNCTION(BlueprintPure, Category = "Item")
	int32 GetQuantity() const { return Quantity; }

protected:
	// 모양이 정해진 뒤 BP 에게 알린다(이름표·반짝임 같은 꾸미기는 BP 에서).
	UFUNCTION(BlueprintImplementableEvent, Category = "Item")
	void OnItemSet();

	UPROPERTY(VisibleAnywhere, Category = "Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 표에 WorldMesh 가 없는 아이템이 쓸 대신 모양. BP_WorldItem 에서 고른다.
	UPROPERTY(EditDefaultsOnly, Category = "Item")
	TObjectPtr<UStaticMesh> FallbackMesh;

	// 바닥 물건이 이 크기(cm, 가장 긴 변)보다 크면 줄인다. 철판 같은 큰 소품 메시가 길을 막지 않게.
	UPROPERTY(EditDefaultsOnly, Category = "Item", meta = (ClampMin = "10"))
	float MaxSizeCm = 120.0f;

	// 대신 모양을 쓸 때의 크기(cm, 가장 긴 변).
	UPROPERTY(EditDefaultsOnly, Category = "Item", meta = (ClampMin = "5"))
	float FallbackSizeCm = 30.0f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Item")
	FName ItemID = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Item")
	int32 Quantity = 1;
};
