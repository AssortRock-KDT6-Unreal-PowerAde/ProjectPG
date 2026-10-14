#pragma once

#include "CoreMinimal.h"
#include "GameMode/GameMode_InGame.h"
#include "GameMode/PlayerController_InGame.h"
#include "EquipmentInventoryMockGameMode.generated.h"

// Keeps the existing UI/input and inventory RPCs, but never requests login inventory.
UCLASS(NotBlueprintable)
class PROJECTPG_API AEquipmentInventoryMockController : public APlayerController_InGame
{
	GENERATED_BODY()

public:
	AEquipmentInventoryMockController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void ToggleMockInventory();
};

// Opt-in only: use as the TestLevel_Character World Settings GameMode override.
UCLASS()
class PROJECTPG_API AEquipmentInventoryMockGameMode : public AGameMode_InGame
{
	GENERATED_BODY()

public:
	AEquipmentInventoryMockGameMode();

protected:
	UPROPERTY(EditDefaultsOnly, Category = "Mock|Inventory", meta = (ClampMin = "1", ClampMax = "64"))
	FIntPoint PocketSize = FIntPoint(10, 15);

	UPROPERTY(EditDefaultsOnly, Category = "Mock|Inventory", meta = (ClampMin = "1", ClampMax = "64"))
	FIntPoint StashSize = FIntPoint(20, 20);

	// Empty means all compatible ItemTable + EquipTable rows, in row-name order.
	UPROPERTY(EditDefaultsOnly, Category = "Mock|Inventory")
	TArray<FName> TestItemIDs;

private:
	FTimerHandle InitializationTimer;
	TSet<TWeakObjectPtr<APlayerController>> InitializedPlayers;
	int32 InitializationAttempts = 0;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

private:
	void InitializePendingPlayers();
	bool InitializePlayerInventory(APlayerController* Controller);
};
