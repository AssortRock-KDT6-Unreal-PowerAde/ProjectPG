// 맵 바닥에 놓인 아이템 하나.

#pragma once

#include "CoreMinimal.h"
#include "Actor/InteractActor.h"
#include "WorldItemActor.generated.h"

class UStaticMesh;

// 바닥에 떨어져 있는 아이템 (줍기 전).
// 게임에서: 창고 구석의 권총, 폐허 바닥의 붕대처럼 "주울 수 있는 물건" 하나.
// 팀 방식에 맞춘 것(10/7): 팀에는 따로 "바닥 물건" 이 없고, 주울 수 있는 것은 전부 E 로 여는 상자(AInteractActor)다.
//   그래서 바닥 물건도 "그 아이템 모양을 한 작은 상자" 로 만든다. 칸 크기 = 아이템 크기, 안에는 그 아이템 하나.
//   E 를 누르면 형님 인벤토리 창이 열리고, 끌어서 가방에 넣으면 형님 서버 동기화가 옮긴다. 비면 이 액터를 지운다.
// C++ 에는 동작과 칸만 둔다: 어떤 아이템인지(ItemID·개수), 모양 끼우기, 바닥에 내려놓기, 비면 지우기.
// 보이는 것(표에 메시가 없을 때 쓸 대신 모양, 반짝임·이름표 같은 효과)은 BP_WorldItem 에서 고른다.
// 리슨 서버: 서버만 놓고(아이템 담당), 이 액터가 복제돼 들어온 사람에게도 보인다. 위치는 처음 복제 때 같이 간다(움직이지 않음).
UCLASS()
class PROJECTPG_API AWorldItemActor : public AInteractActor
{
	GENERATED_BODY()

public:
	AWorldItemActor();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버만: 어떤 아이템인지 정하고, 상자 칸을 그 아이템 크기로 줄인 뒤 그 아이템 하나를 넣는다. 모양도 바꾼다.
	// 스포너가 액터를 만든 바로 뒤에 부른다(부모가 PostInitializeComponents 에서 상자 칸을 이미 만들어 둔 뒤).
	// 실패(표에 없는 번호, 안 들어감)하면 false — 빈 상자가 바닥에 남지 않게 부른 쪽이 지운다.
	bool SetItem(FName InItemID, int32 InQuantity);

	// 메시 아래면이 FloorPoint 높이에 닿게 내려놓는다. 메시마다 기준점(피벗)이 달라서 크기를 재서 맞춘다.
	void PlaceOnFloor(const FVector& FloorPoint);

	UFUNCTION(BlueprintPure, Category = "Item")
	FName GetItemID() const { return ItemID; }

	UFUNCTION(BlueprintPure, Category = "Item")
	int32 GetQuantity() const { return Quantity; }

protected:
	virtual void BeginPlay() override;

	// 모양이 정해진 뒤 BP 에게 알린다(이름표·반짝임 같은 꾸미기는 BP 에서).
	UFUNCTION(BlueprintImplementableEvent, Category = "Item")
	void OnItemSet();

	// 표에 WorldMesh 가 없는 아이템이 쓸 대신 모양. BP_WorldItem 에서 고른다.
	UPROPERTY(EditDefaultsOnly, Category = "Item")
	TObjectPtr<UStaticMesh> FallbackMesh;

	// 바닥 물건이 이 크기(cm, 가장 긴 변)보다 크면 줄인다. 철판 같은 큰 소품 메시가 길을 막지 않게.
	UPROPERTY(EditDefaultsOnly, Category = "Item", meta = (ClampMin = "10"))
	float MaxSizeCm = 120.0f;

	// 대신 모양을 쓸 때의 크기(cm, 가장 긴 변).
	UPROPERTY(EditDefaultsOnly, Category = "Item", meta = (ClampMin = "5"))
	float FallbackSizeCm = 30.0f;

	// 리슨 서버: 서버가 정한 아이템 번호·개수가 들어온 사람에게 복제되면, 그쪽에서도 같은 모양으로 바꾼다.
	// (상자 안 내용은 형님 인벤토리 복제가 따로 보낸다. 이 둘은 모양용.)
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Item, Category = "Item")
	FName ItemID = NAME_None;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, ReplicatedUsing = OnRep_Item, Category = "Item")
	int32 Quantity = 1;

private:
	// 서버·들어온 사람 둘 다: 아이템 표의 WorldMesh(없으면 대신 모양)로 모양·크기 맞추기.
	void ApplyItemLook();

	UFUNCTION()
	void OnRep_Item();

	// 서버만: 상자 내용이 바뀔 때마다 불린다. 아이템을 꺼내 가서 비었으면 지운다.
	UFUNCTION()
	void HandleInventoryUpdated();

	// 아이템을 한 번이라도 넣었나(넣기 전의 빈 상태에서 지워지지 않게).
	bool bHasHeldItem = false;
	bool bDestroyQueued = false;
};
