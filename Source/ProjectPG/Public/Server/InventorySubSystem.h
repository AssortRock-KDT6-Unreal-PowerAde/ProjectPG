#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Common/GameData.h"
#include "InventorySubSystem.generated.h"

class APlayerController;

// Inventory 전용 델리게이트 분배
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryReceived, const FInventoryMapWrapper&, ItemsWrapper);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEquipReceived, const FInventoryMapWrapper&, ItemsWrapper);

// 인벤토리 데이터 담당 (이 컴퓨터 안).
// 예전: 웹 서버와 인벤토리를 주고받는 창구(GET_INVENTORY·REQ_MOVE_ITEM). 10/4 팀 합의로 웹 서버를 빼고 리슨 서버로 가면서
//       서버 통신은 지우고, "처음 갖고 시작하는 짐" 을 채우는 일만 남겼다.
// 하는 일: 창고·주머니·장비 칸을 만들고(칸 이벤트를 쏜다), 시작 짐 표(StarterInventoryTable)대로 아이템을 넣는다.
// 안 하는 일: 아이템 옮기기·장착은 인벤토리 컴포넌트·장비 컴포넌트가 직접 한다(이제 보고할 서버가 없다).
UCLASS(Config = Game)
class PROJECTPG_API UInventorySubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UInventorySubSystem* Get(UWorld* World);

	// 시작 짐 넣기. 로비 흐름이 로비를 띄우기 전에 부른다. 이미 채웠으면(로비로 돌아옴) 아무것도 안 한다.
	// 넣은 아이템 수를 돌려준다(-1 = 플레이어 상태가 아직 없음).
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 LoadStarterInventory(APlayerController* PlayerController);

	// 칸이 만들어졌을 때 알린다 → 인벤토리 컴포넌트가 칸을 만든다. (예전엔 서버 응답 때 울렸다)
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryReceived OnInventoryReceived;

	// 장비 칸 GUID 를 알린다 → 장비 컴포넌트가 칸을 등록한다.
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnEquipReceived OnEquipReceived;

private:
	// 창고·주머니 크기(칸). DefaultGame.ini [/Script/ProjectPG.InventorySubSystem] 에서 바꾼다.
	UPROPERTY(Config)
	FIntPoint StashSize = FIntPoint(10, 10);

	UPROPERTY(Config)
	FIntPoint PocketSize = FIntPoint(5, 4);
};
