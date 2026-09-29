// 오브젝트 원형 스모크 테스트.
//
// 에디터 없이도 돌릴 수 있는 검증이다. 원형마다 하나씩 스폰해 실제 함수(Interact, TakeDamage,
// 소켓 스폰 등)를 부르고 결과를 로그에 pass=true/false 로 남긴다.
//  - PIE 콘솔:   PG.ObjectSmokeTest
//  - 헤드리스:   UnrealEditor-Cmd.exe <uproject> "/Engine/Maps/Entry?game=/Script/ProjectPG.PGObjectTestGameMode"
//                -game -nullrhi -unattended -PGObjectSmokeTest
// 검색어: "PGObjectSmokeTest summary" 와 "pass=false".
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGObjectTypes.h"
#include "PGObjectSmokeTest.generated.h"

class APGDestructibleActor;
class APGDoorActor;
class APGExtractionZoneActor;
class APGServiceInteractionActor;
class UPGInteractionComponent;
class UPGLootableComponent;
class UPGObjectSpawnerSubsystem;

// 인벤토리 없는 테스트 폰. 이름→수량 맵으로 오브젝트 계약만 확인한다.
UCLASS()
class PROJECTPG_API APGObjectTestPawn : public APawn, public IPGItemReceiver
{
	GENERATED_BODY()

public:
	APGObjectTestPawn();

	virtual bool ReceiveItem_Implementation(FName ItemId, int32 Count) override;
	virtual bool HasItem_Implementation(FName ItemId, int32 Count) const override;
	virtual bool ConsumeItem_Implementation(FName ItemId, int32 Count) override;
	virtual FName GetPlayerKey_Implementation() const override;

	int32 GetCount(FName ItemId) const;
	UPGInteractionComponent* GetInteraction() const { return Interaction; }

	// 가방이 꽉 찬 상황을 흉내 낼 때 true.
	bool bRefuseItems = false;

private:
	UPROPERTY()
	TObjectPtr<UPGInteractionComponent> Interaction;

	UPROPERTY()
	TMap<FName, int32> Items;
};

// 동적 델리게이트를 받으려면 UObject가 필요하다. 이벤트 횟수만 센다.
UCLASS()
class UPGSmokeTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION() void HandleExtractionCompleted(APGExtractionZoneActor* Zone, APawn* Pawn) { ++ExtractionCompleted; }
	UFUNCTION() void HandleServiceRequested(APGServiceInteractionActor* Service, APawn* Pawn, EPGServiceKind Kind) { ++ServiceRequests; }
	UFUNCTION() void HandleQuestEvent(FName QuestTag, AActor* Source, APawn* Pawn) { ++QuestEvents; LastQuestTag = QuestTag; }
	UFUNCTION() void HandleEmptied(UPGLootableComponent* Lootable, APawn* Pawn) { ++Emptied; }
	UFUNCTION() void HandleDestroyed(APGDestructibleActor* Destructible, AActor* Causer) { ++Destroyed; }
	UFUNCTION() void HandleDoorUsed(APGDoorActor* Door, APawn* Pawn, bool bOpened) { ++DoorUsed; }

	int32 ExtractionCompleted = 0;
	int32 ServiceRequests = 0;
	int32 QuestEvents = 0;
	int32 Emptied = 0;
	int32 Destroyed = 0;
	int32 DoorUsed = 0;
	FName LastQuestTag;
};

namespace PGObjectSmokeTest
{
	// 노션 목록의 확정 항목을 코드로 등록한 기본 카탈로그. DataTable이 없을 때 쓴다.
	PROJECTPG_API void RegisterDefaultCatalog(UPGObjectSpawnerSubsystem& Spawner);

	// 전체 테스트 실행. 결과는 로그. 반환값은 실패 수.
	PROJECTPG_API int32 RunAndCount(UWorld* World);
	PROJECTPG_API void Run(UWorld* World);

	// 등록된 카탈로그·루팅 테이블을 DataTable 가져오기용 CSV로 쓴다.
	PROJECTPG_API bool ExportCatalogCsv(UPGObjectSpawnerSubsystem& Spawner, const FString& CatalogPath, const FString& LootPath);
}
