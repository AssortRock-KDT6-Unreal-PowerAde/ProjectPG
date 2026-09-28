// 오브젝트 → 인벤토리 경계.
//
// 상자·바닥 아이템·시체는 "아이템을 주고 싶다"고만 말하고, 실제 가방 격자 배치는
// 인벤토리 담당의 컴포넌트가 한다. 팀 저장소의 UInventoryComponent(AddItemByID)에
// 연결할 때는 캐릭터가 이 인터페이스를 구현해 AddItemByID를 호출하면 된다.
// 오브젝트 코드는 인벤토리 헤더를 전혀 포함하지 않으므로 담당 경계가 유지된다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PGItemReceiverInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UPGItemReceiver : public UInterface
{
	GENERATED_BODY()
};

class PROJECTPG_API IPGItemReceiver
{
	GENERATED_BODY()

public:
	// 아이템을 받는다. 가방이 꽉 차서 못 받으면 false. 오브젝트는 false면 상태를 바꾸지 않는다.
	UFUNCTION(BlueprintNativeEvent, Category = "PG|Inventory")
	bool ReceiveItem(FName ItemId, int32 Count);

	// 열쇠·연료·납품 아이템 확인용.
	UFUNCTION(BlueprintNativeEvent, Category = "PG|Inventory")
	bool HasItem(FName ItemId, int32 Count) const;

	// 열쇠 소모, 연료 주입, 퀘스트 납품에서 쓴다. 원자적으로 처리해야 하므로
	// 호출 전에 HasItem으로 확인하고 서버에서만 부른다.
	UFUNCTION(BlueprintNativeEvent, Category = "PG|Inventory")
	bool ConsumeItem(FName ItemId, int32 Count);

	// 플레이어별 탈출구, 플레이어별 1회 퀘스트 트리거를 구분하는 키.
	UFUNCTION(BlueprintNativeEvent, Category = "PG|Inventory")
	FName GetPlayerKey() const;
};

// 인터페이스 호출을 한 곳에 모은 도우미. Pawn이 인터페이스를 구현하지 않으면 전부 실패로 취급한다.
UCLASS()
class PROJECTPG_API UPGItemReceiverLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "PG|Inventory")
	static bool GiveItem(AActor* Target, FName ItemId, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "PG|Inventory")
	static bool HasItem(const AActor* Target, FName ItemId, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "PG|Inventory")
	static bool ConsumeItem(AActor* Target, FName ItemId, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "PG|Inventory")
	static FName GetPlayerKey(const AActor* Target);
};
