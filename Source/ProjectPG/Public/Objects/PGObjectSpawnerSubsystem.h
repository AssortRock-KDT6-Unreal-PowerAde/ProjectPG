// 오브젝트 카탈로그·루팅 테이블 저장소 + 시드 기반 생성기.
//
// 역할 두 가지:
//  1. 데이터 저장소: 노션 목록(카탈로그 행)과 루팅 테이블을 DataTable 또는 코드 등록으로 받아 둔다.
//  2. 생성기: 레벨의 생성 소켓(UPGSpawnSocketComponent)이나 레벨 디자인 포인트를 읽어
//     서버 시드로 어떤 오브젝트를 어디에 만들지 결정하고 스폰한다.
//     같은 시드면 같은 결과가 나오므로, 나중에 서버 Manifest로 클라이언트에 재현할 수 있다.
#pragma once

#include "CoreMinimal.h"
#include "Monster/PGMonsterCharacter.h"
#include "Subsystems/WorldSubsystem.h"
#include "Engine/DeveloperSettings.h"
#include "Objects/PGObjectTypes.h"
#include "PGObjectSpawnerSubsystem.generated.h"

class UDataTable;
class UPGSpawnSocketComponent;
struct FLevelDesignPoint;

// 프로젝트 설정 > Game > ProjectPG Objects 에 노출되는 설정.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ProjectPG Objects"))
class PROJECTPG_API UPGObjectSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 노션 목록을 옮긴 DataTable (행 타입 FPGObjectCatalogRow). 비어 있으면 코드 등록분만 쓴다.
	// 9/28: 기본값을 에셋(Tools/wbp/make_object_tables.py 가 만든 DT_PGObjectCatalog)으로 — 물건 목록은 에디터에서 고친다.
	UPROPERTY(Config, EditAnywhere, Category = "Data", meta = (RowType = "/Script/ProjectPG.PGObjectCatalogRow"))
	TSoftObjectPtr<UDataTable> ObjectCatalogTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/PG/Blueprint/Visual/DT_PGObjectCatalog.DT_PGObjectCatalog")));

	// 루팅 테이블 DataTable (행 타입 FPGLootTableRow). 기본값 DT_PGLootTables(9/28).
	UPROPERTY(Config, EditAnywhere, Category = "Data", meta = (RowType = "/Script/ProjectPG.PGLootTableRow"))
	TSoftObjectPtr<UDataTable> LootTableTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/PG/Blueprint/Visual/DT_PGLootTables.DT_PGLootTables")));

	// 켜면 AGameModePG가 맵 생성을 마친 뒤 레벨 디자인 포인트(Loot/Exit/Quest)에 자동으로 오브젝트를 만든다.
	UPROPERTY(Config, EditAnywhere, Category = "Spawning")
	bool bAutoSpawnFromLevelDesignPoints = false;

	// 바닥 루팅(배그식). Loot 포인트마다 상자 옆에 이 확률로 무기·탄약·소비품·장비 몇 개를 바닥에 흩뿌린다.
	// 상자만 있으면 길을 걷다 눈에 띄는 게 없어서 파밍 맛이 안 난다. 0 이면 상자만.
	UPROPERTY(Config, EditAnywhere, Category = "Spawning", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float GroundLootChance = 0.5f;

	UPROPERTY(Config, EditAnywhere, Category = "Spawning", meta = (ClampMin = "0"))
	int32 GroundLootMin = 1;

	UPROPERTY(Config, EditAnywhere, Category = "Spawning", meta = (ClampMin = "0"))
	int32 GroundLootMax = 3;

	// 상자 중심에서 이 반경(cm) 안에 흩뿌린다.
	UPROPERTY(Config, EditAnywhere, Category = "Spawning", meta = (ClampMin = "50.0"))
	float GroundLootRadius = 220.0f;

	// 카탈로그에 해당 종류의 행이 하나도 없을 때 쓰는 기본 회색 상자(greybox) 메시.
	UPROPERTY(Config, EditAnywhere, Category = "Greybox")
	TSoftObjectPtr<UStaticMesh> GreyboxMesh;

	// 원형(상자·문·바닥 아이템…)마다 스폰할 클래스를 바꿔 끼운다. 비어 있는 원형은 코드 기본 클래스(원형 표)를 쓴다.
	// 왜(2026-09-26 OCP): 전에는 원형 → 클래스가 switch 로 박혀 있어 BP 자식으로 바꾸려면 코드를 고쳐야 했다.
	//   이제 여기서 원형 하나에 BP 하나를 고르면 된다. 카탈로그 행에 클래스가 적혀 있으면 그게 먼저다.
	UPROPERTY(Config, EditAnywhere, Category = "Spawning")
	TMap<EPGObjectArchetype, TSoftClassPtr<AActor>> ArchetypeClassOverrides;

	// (전투·세력·피날레 설정은 9/26 에 UPGCombatSettings — 프로젝트 설정 "ProjectPG Combat" — 으로 옮겼다. 이름과 내용을 맞추려고.)
};

UCLASS()
class PROJECTPG_API UPGObjectSpawnerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGObjectSpawnerSubsystem* Get(const UObject* WorldContextObject);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// ---- 데이터 등록 ----
	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	void RegisterCatalogRow(const FPGObjectCatalogRow& Row);

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	void RegisterLootTable(FName TableId, const FPGLootTableRow& Table);

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	void LoadFromDataTables(UDataTable* CatalogTable, UDataTable* LootTables);

	const FPGObjectCatalogRow* FindCatalogRow(FName ObjectId) const;
	const FPGLootTableRow* FindLootTable(FName TableId) const;

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	int32 GetCatalogCount() const { return Catalog.Num(); }

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	void GetAllCatalogRows(TArray<FPGObjectCatalogRow>& OutRows) const { Catalog.GenerateValueArray(OutRows); }

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	void GetAllLootTableIds(TArray<FName>& OutIds) const { LootTables.GenerateKeyArray(OutIds); }

	// ---- 루팅 ----
	// 같은 TableId + 같은 Seed면 항상 같은 결과. 서버가 굴리고 결과만 클라이언트에 준다.
	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	TArray<FPGItemStack> RollLoot(FName TableId, int64 Seed) const;

	// ---- 생성 ----
	// 카탈로그 행 하나를 지정 위치에 스폰하고 ApplyCatalogRow까지 마친다.
	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	AActor* SpawnFromCatalog(FName ObjectId, const FTransform& Transform, int64 Seed = 0);

	// 소켓 하나에 맞는 후보를 시드로 골라 스폰한다. 후보가 없으면 nullptr.
	AActor* SpawnForSocket(UPGSpawnSocketComponent* Socket, FRandomStream& Stream);

	// 월드의 모든 빈 소켓을 채운다. 반환값은 스폰한 개수.
	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	int32 SpawnAllSockets(int64 Seed);

	// AWarZoneFootprintPreview가 만든 Loot/Exit/Quest 포인트에 오브젝트를 만든다.
	int32 SpawnFromLevelDesignPoints(const TArray<FLevelDesignPoint>& Points, int64 Seed);

	// 원형별 기본 C++ 클래스. 카탈로그 행에 ActorClass가 없을 때 쓴다.
	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	TSubclassOf<AActor> GetDefaultClassForArchetype(EPGObjectArchetype Archetype) const;

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	void ResetSpawnState();

	UFUNCTION(BlueprintCallable, Category = "PG|Spawner")
	int32 GetSpawnedCount() const { return SpawnedActors.Num(); }

	const TArray<TWeakObjectPtr<AActor>>& GetSpawnedActors() const { return SpawnedActors; }

	// 소켓 종류 + Tier에 맞는 카탈로그 후보 목록. 스모크 테스트와 에디터 검증이 같이 쓴다.
	void CollectCandidates(EPGSpawnSocketKind Kind, uint8 Tier, const TArray<FName>& AllowedObjectIds, TArray<const FPGObjectCatalogRow*>& OutRows) const;
	// 상자 옆 바닥 루팅. 놓은 개수를 돌려준다.
	int32 ScatterGroundLoot(const FVector& Center, uint8 Tier, FRandomStream& Stream, int64 Seed);

	// 탈출 지점 하나에 탈출 세트(차 / 헬기 / 잠긴 펜스+영역) 중 하나를 놓는다. Outward = 맵 바깥쪽(수평).
	// 놓은 액터 수를 돌려준다(펜스 세트는 2).
	int32 SpawnExitSet(const FVector& Ground, const FVector& Outward, FRandomStream& Stream, int64 Seed);

	// 거래소 부스 4종(교환소·제조·상점·택배)을 워존 끝 네 방향에 하나씩 세운다(9/23). 세운 개수를 돌려주고,
	// 부스가 선 포인트 번호를 OutUsedPoints 에 넣는다 — 그 자리의 상자는 만들지 않는다(부스와 겹치지 않게).
	int32 SpawnBooths(const TArray<FLevelDesignPoint>& Points, int64 Seed, TSet<int32>& OutUsedPoints);

protected:
	AActor* SpawnRowInternal(const FPGObjectCatalogRow& Row, const FTransform& Transform, int64 Seed);
	const FPGObjectCatalogRow* PickWeighted(const TArray<const FPGObjectCatalogRow*>& Rows, FRandomStream& Stream) const;

	UPROPERTY()
	TMap<FName, FPGObjectCatalogRow> Catalog;

	UPROPERTY()
	TMap<FName, FPGLootTableRow> LootTables;

	// bUniquePerMap 행이 이미 스폰됐는지.
	UPROPERTY()
	TSet<FName> UniqueSpawned;

	TArray<TWeakObjectPtr<AActor>> SpawnedActors;
};
