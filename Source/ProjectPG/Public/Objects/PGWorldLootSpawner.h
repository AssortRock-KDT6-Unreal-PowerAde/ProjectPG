// 맵 전체에 기본 아이템을 랜덤으로 떨어뜨리는 월드 서브시스템. (2026-09-22)
//
// 왜 만들었나 (9/22 사용자): "상자 안은 UI 로 랜덤 아이템을 넣더라도, 기본적으로 랜덤 맵 바닥·건물·컨테이너·공장·마을에
// 기본 아이템들이 랜덤으로 스폰돼 있게 하자." 지금까지 바닥 아이템은 상자(Loot 포인트) 옆 2m 에만 흩뿌려져서
// (UPGObjectSpawnerSubsystem::ScatterGroundLoot) 상자 없는 곳은 600m 맵이 텅 비어 있었다.
//
// 두 갈래로 놓는다:
//  1) 타일: 논리 셀(AMapTile, 20m)마다 종류(워존·길·들판·숲)별 확률로 몇 개.
//  2) 시설: 스트리밍된 시설 레벨(LD_Facility_*: 공장·시내·마을·검문소)의 발자국 안. 레벨이 보인 뒤에 놓는다.
//     — 레벨이 뜨기 전에 놓으면 바닥이 없어 허공에 뜨거나, 나중에 뜬 벽 속에 갇힌다.
//
// "들어갈 수 없는 건물 안에 놓이지 않게" 는 자리마다 네 가지를 검사한다(cpp 의 TryFindSpot 주석):
//  위에서 아래로 선을 쏴 바닥 찾기 → 머리 위 2m 가 비었나 → 사람 캡슐이 서 있을 수 있나(겹침) → 길찾기 지도(NavMesh)로 닿을 수 있나.
//  NavMesh 는 워존 가운데 120m 안에만 만들어지므로(NavigationInvoker), 그 밖은 기하 검사만으로 판정하고 개수를 따로 센다.
//
// 등급(기획 3.3.2): 워존(가운데) 높게, 가장자리(메이즈) 낮게. 등급은 노말/레어/에픽 세 칸(PGItemValue).
// 어떤 아이템을 어느 비중으로 놓을지는 등급 표 Docs/DT_PGItemValue.csv 의 LootWeight·LootMin·LootMax 열이 정한다(9/22 코드에서 표로 옮김).
// 구역마다 등급을 몇 대 몇으로 굴릴지는 아래 설정의 GradeWeights.
//
// 같은 판 시드(AGameModePG::GetMapGenerationSeed)면 같은 배치. 한 판 상한과 프레임당 개수는 설정(UPGWorldLootSettings).
// 서버에서만 돈다(NM_Client 면 아무것도 안 함). 헤드리스 스모크(Entry 맵, PGObjectTestGameMode)에는 AGameModePG 가 없어 저절로 꺼진다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Objects/PGItemValue.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGWorldLootSpawner.generated.h"

class AActor;
class UPGWeaponComponent;

// 한 구역에서 등급을 굴리는 비중(합이 100 일 필요는 없다 — 비율만 본다).
USTRUCT(BlueprintType)
struct PROJECTPG_API FPGGradeWeights
{
	GENERATED_BODY()

	FPGGradeWeights() = default;
	FPGGradeWeights(float InNormal, float InRare, float InEpic) : Normal(InNormal), Rare(InRare), Epic(InEpic) {}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Loot", meta = (ClampMin = "0.0"))
	float Normal = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Loot", meta = (ClampMin = "0.0"))
	float Rare = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "World Loot", meta = (ClampMin = "0.0"))
	float Epic = 0.0f;
};

// 프로젝트 설정 > Game > ProjectPG World Loot.
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "ProjectPG World Loot"))
class PROJECTPG_API UPGWorldLootSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 끄면 맵 전체 랜덤 바닥 아이템을 놓지 않는다(상자 옆 흩뿌리기와 시작 연료통은 그대로).
	UPROPERTY(Config, EditAnywhere, Category = "World Loot")
	bool bEnabled = true;

	// 한 판 총 상한. 4060 8GB 기준 — 바닥 아이템 하나가 액터 하나라 수백 개를 넘기면 복제·틱·그리기 비용이 쌓인다.
	// 9/22 200 → 2600: "칸마다 1~3개씩 모두". 바닥 아이템은 틱이 없는 액터라 주로 그리기 비용이 든다 — 느려지면 여기부터 줄인다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "0", ClampMax = "5000"))
	int32 MaxItemsPerMap = 2600;

	// 그중 시설 안에 쓸 몫. 나머지가 타일 몫. 시설을 먼저 채우지 않으면 타일이 상한을 다 써 버려 공장 안이 비게 된다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "0", ClampMax = "2000"))
	int32 FacilityBudget = 320;

	// 한 프레임에 시도하는 자리 수(성공·실패 합쳐서). 자리 하나 = 선 검사 3~4번 + 캡슐 겹침 + 길찾기 투영. 한 번에 다 하면 끊긴다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "1", ClampMax = "200"))
	int32 AttemptsPerFrame = 40;

	// 게임플레이 포인트가 확정된 뒤(시설 레벨이 다 뜬 뒤) 이만큼 기다렸다가 시작한다. 길찾기 지도가 아직 굽는 중이면 "못 닿음" 오판이 난다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float StartDelaySeconds = 3.0f;

	// 길찾기 지도 굽기가 끝나길 최대 이만큼 기다린다. 넘으면 그냥 시작한다(무한 대기로 아이템이 영영 안 나오는 것보다 낫다).
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "0.0", ClampMax = "120.0"))
	float NavWaitTimeoutSeconds = 15.0f;

	// 시작 뒤 이 시간 동안은 늦게 뜬 시설 레벨도 받아서 채운다. 그 뒤에 뜬 것은 비워 둔다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "0.0", ClampMax = "300.0"))
	float LateFacilityWindowSeconds = 30.0f;

	// 연료통은 물리로 굴러다니는 물건이 됐다(9/21). 많이 깔면 차에 치여 사방으로 튀고 터진다 → 몇 개만.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "0", ClampMax = "50"))
	int32 MaxFuel = 12; // 9/22 3 → 12: 연료통이 탈출 조건이라 너무 귀하면 못 나간다

	// ---- 타일(20m 셀)별 확률과 개수. 워존이 제일 빽빽하고 길은 드물게. ----
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WarZoneCellChance = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "1", ClampMax = "8"))
	int32 WarZoneCellMaxItems = 3;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float OpenCellChance = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NatureCellChance = 1.0f;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RoadCellChance = 1.0f;
	// 칸마다 이만큼씩(워존 칸도 최소는 이 값). 9/22 "타일마다 2~3개씩 깔아 버려".
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CellMinItems = 2;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "1", ClampMax = "8"))
	int32 CellMaxItems = 3;
	// 칸마다 상자 하나가 놓일 확률. 9/22 사용자: "상자들도 많이 스폰되게". 레벨 지점(Loot)에 놓는 상자 40개 남짓에 더해진다.
	// 상자는 바닥 아이템 상한(MaxItemsPerMap)에 세지 않는다. 상자 속 내용물은 상자 원형이 루팅 표로 굴린다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float ContainerCellChance = 0.25f;

	// 워존 셀에서 이 칸(체비쇼프 거리) 안은 중간 등급(2), 워존 자신은 3, 나머지 가장자리는 1.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Tiles", meta = (ClampMin = "0", ClampMax = "15"))
	int32 MidZoneRadiusCells = 4;

	// 구역별 등급 비중(노말/레어/에픽). 기획 3.3.2: 워존일수록 좋은 것.
	// 가장자리 에픽이 0 인 이유: 가장자리 칸이 워존보다 열 배 넘게 많아서, 1% 만 줘도 에픽 총 대부분이 가장자리에서 나온다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Grades")
	FPGGradeWeights EdgeGradeWeights = FPGGradeWeights(90.0f, 10.0f, 0.0f);

	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Grades")
	FPGGradeWeights MidGradeWeights = FPGGradeWeights(75.0f, 23.0f, 2.0f);

	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Grades")
	FPGGradeWeights WarZoneGradeWeights = FPGGradeWeights(55.0f, 30.0f, 15.0f);

	// ---- 외진 보상 거점(APGRemoteOutpostActor, 빈 모서리의 추락 헬기 캠프). 9/22 사용자: "멀리까지 갈 이유가 있게".
	// 멀리 간 보람이 있어야 하므로 워존(55/30/15)보다 좋게 굴린다. 거점은 판마다 많아야 하나라 맵 전체 에픽 수를 크게 흔들지 않는다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Remote Outpost")
	FPGGradeWeights RemoteOutpostGradeWeights = FPGGradeWeights(20.0f, 45.0f, 35.0f);

	// 거점 둘레에 흩뿌릴 바닥 아이템 수(상자와 별도).
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Remote Outpost", meta = (ClampMin = "0", ClampMax = "30"))
	int32 RemoteOutpostFloorItems = 6;

	// 거점 상자 자리마다 놓을 상자 카탈로그 행(자리 순서대로). 목록이 자리보다 짧으면 거점 액터가 자리마다 적어 둔 기본값을 쓴다.
	// 워존 상자(군용·탄약·무기·의료·잠긴 상자)에 금고(OBJ-010, LT_Safe)를 더한 것이 "워존보다 좋은" 몫이다. 잠긴 상자는 뺀다 — 열쇠 없이 멀리 왔다가 허탕이면 안 된다.
	// 탄약 상자(OBJ-003, LT_Ammo)도 뺐다 — 9/22 헤드리스에서 소총탄만 나와 "멀리 간 보람" 이 없었다. 대신 무기 상자를 하나 더(레어·에픽 총이 섞인 LT_Weapon).
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Remote Outpost")
	TArray<FName> RemoteOutpostBoxes = { FName(TEXT("OBJ-004")), FName(TEXT("OBJ-010")), FName(TEXT("OBJ-002")), FName(TEXT("OBJ-004")) };

	// ---- 시설. 발자국 셀 수 × 이 값이 그 시설의 목표 개수(최소·최대 사이). 6×6 시내는 최대에 걸린다. ----
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Facilities", meta = (ClampMin = "0.0", ClampMax = "20.0"))
	float FacilityItemsPerCell = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Facilities", meta = (ClampMin = "0", ClampMax = "100"))
	int32 FacilityMinItems = 6;
	UPROPERTY(Config, EditAnywhere, Category = "World Loot|Facilities", meta = (ClampMin = "0", ClampMax = "100"))
	int32 FacilityMaxItems = 60;

	// 한 자리에 몇 번까지 다시 뽑아 보나. 폐허·숲 셀은 벽·바위가 많아 첫 자리가 자주 막힌다.
	UPROPERTY(Config, EditAnywhere, Category = "World Loot", meta = (ClampMin = "1", ClampMax = "16"))
	int32 AttemptsPerItem = 4;
};

UCLASS()
class PROJECTPG_API UPGWorldLootSpawner : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGWorldLootSpawner* Get(const UWorld* World);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// 지금까지 놓은 것을 전부 지우고 처음부터 다시 계획한다(콘솔 PG.WorldLootRespawn). 같은 시드면 같은 자리에 다시 놓인다.
	void Restart();

	int32 GetSpawnedCount() const { return SpawnedItems.Num(); }

private:
	// 진행 단계. 맵(AGameModePG + 프리뷰)이 준비되길 → 길찾기 지도 굽기가 끝나길 → 프레임마다 조금씩 놓기 → 끝.
	enum class EPhase : uint8 { WaitingForMap, WaitingForNav, Spawning, Done, Disabled };

	// 놓을 아이템 후보 하나. cpp 의 BuildPool 이 등급 표·카탈로그·바닥 메시 유무를 검사해서 채운다.
	struct FPoolItem
	{
		FName ItemId;
		EPGItemGrade Grade = EPGItemGrade::Normal;
		float Weight = 1.0f;
		int32 MinCount = 1;
		int32 MaxCount = 1;
		FName CatalogObjectId; // 카탈로그 행이 있으면 그 행으로 스폰(메시·이름이 따라온다). None 이면 SpawnDrop.
		// 자기 메시가 없는 아이템(권총탄·샷건탄)이 빌려 쓰는 카탈로그 행. 그 행의 메시만 가져오고 ItemId·개수·이름은 자기 것.
		FName AliasMeshObjectId;
		FText AliasDisplayName;
		int32 RemainingCap = -1; // -1 = 무제한. 연료통처럼 개수를 막는 것만 양수.
		FName AmmoItemId;        // 총이면 옆에 같이 놓을 탄(등급 표의 AmmoItemId 열)
	};

	// "어디에 몇 개" 한 묶음. 타일 한 셀 또는 시설 하나.
	struct FJob
	{
		FString Label;          // 로그용. 타일은 "tile", 시설은 "DowntownBlock_6x6"
		bool bFacility = false;
		bool bContainer = false; // 이 묶음은 바닥 아이템이 아니라 상자 하나(상자 원형 카탈로그 행)
		FVector Centre = FVector::ZeroVector;
		FVector2D HalfExtent = FVector2D(1000.0f, 1000.0f); // 자리를 뽑는 직사각형(회전 전)
		float YawDegrees = 0.0f; // 시설은 회전해 놓인다
		float BaseZ = 20.0f;     // 이 자리의 기준 바닥 높이. 위에서 쏘는 선의 출발점과 "윗층인가" 판단에 쓴다
		uint8 ZoneTier = 1;      // 1 가장자리 / 2 중간 / 3 워존 / 4 외진 보상 거점(RemoteOutpostGradeWeights)
		FName ForcedObjectId;    // 상자 묶음에서 이 상자 행을 먼저 쓴다(외진 보상 거점 자리마다 정해진 상자)
		int32 Count = 0;
		int64 Seed = 0;
		int32 StatsIndex = INDEX_NONE;
	};

	// 로그에 찍을 집계. 타일 전체가 하나, 시설마다 하나.
	struct FStats
	{
		FString Label;
		int32 Placed = 0;
		int32 Wanted = 0;
		int32 NoFloor = 0;
		int32 Blocked = 0;
		int32 Unreachable = 0;
		int32 NavChecked = 0;
		int32 Cells = 0;
		uint8 ZoneTier = 0;
	};

	enum class ESpotResult : uint8 { Ok, NoFloor, Blocked, Unreachable };

	void Plan();
	void PlanTiles(const TArray<FJob>& FacilityJobs, TArray<FJob>& OutJobs);
	// 외진 보상 거점(있으면)의 상자 자리와 바닥 아이템 묶음. 시설·타일보다 먼저 계획해 자리 간격 검사에서 밀리지 않게 한다.
	void PlanRemoteOutpost(TArray<FJob>& OutJobs);
	bool PlanNewFacilities(TArray<FJob>& OutJobs);
	void ProcessJobs();
	ESpotResult TryFindSpot(const FJob& Job, FRandomStream& Stream, FVector& OutLocation, bool& bOutNavChecked) const;
	const FPoolItem* PickItem(uint8 ZoneTier, FRandomStream& Stream);
	AActor* SpawnItem(const FPoolItem& Item, const FVector& Location, FRandomStream& Stream, int64 Seed);
	void ClearSpawned();

	// 등급 무기(Pistol_Rare 등)를 무기 컴포넌트에 알려 준다. 무기 컴포넌트는 ItemId 로 무기를 찾는데 기본 표에는 원래 총(Pistol)만 있어서,
	// 레어 권총을 주워도 "무기 아님"으로 들 수가 없다. 무기 코드는 건드리지 않고 공개 함수(FindDef·RegisterWeapon)로 원래 총 정의를 복사해 넣는다.
	void HandleActorSpawned(AActor* Actor);
	static void RegisterGradeWeapons(UPGWeaponComponent* Weapon);
	FDelegateHandle ActorSpawnedHandle;
	void LogSummary();
	bool IsInsideAnyFacility(const FVector& Point, const TArray<FJob>& FacilityJobs, float Margin) const;
	uint8 ZoneTierForCell(const FIntPoint& Cell) const;

	EPhase Phase = EPhase::WaitingForMap;
	int64 MapSeed = 0;
	double PhaseStartTime = 0.0;
	double SpawnStartTime = 0.0;
	double NextMapSearchTime = 0.0; // 프리뷰 액터 찾기는 0.5초마다(매 프레임 액터 순회는 낭비)
	TSet<FIntPoint> WarZoneCells; // 논리 격자의 워존 셀. 구역 등급 계산에 쓴다(타일·시설 둘 다).
	bool bWarZoneCellsGathered = false;
	uint8 WarnedEmptyGrades = 0;  // "이 등급에 놓을 아이템이 없다" 경고를 등급마다 한 번만
	// "막힘" 이 어느 검사에서 났는지(0 바닥이 아이템·상자·사람 / 1 머리 위 / 2 캡슐 / 3 간격)와 막은 것의 이름 몇 개.
	// 9/22 PIE 에서 거리 자리 391곳이 "막힘" 으로 버려져 110개 중 18개만 놓였다 — 어느 검사인지 숫자로 보려고.
	mutable int32 BlockedBy[4] = { 0, 0, 0, 0 };
	int32 AmmoWithGuns = 0; // 총 옆에 같이 놓은 탄 묶음 수
	int32 ContainersPlaced = 0; // 칸마다 확률로 놓은 상자 수(ContainerCellChance)
	// 외진 보상 거점 집계(로그용). 상자는 놓은 수 / 계획한 수, 바닥 아이템은 등급별.
	int32 OutpostBoxesPlanned = 0;
	int32 OutpostBoxesPlaced = 0;
	int32 OutpostFloorPlanned = 0;
	int32 OutpostFloorPlaced = 0;
	int32 OutpostGradeCounts[3] = { 0, 0, 0 };
	FString OutpostCorner;
	TArray<FString> OutpostBoxNotes; // "OBJ-004 LT_Weapon [Pistol_Rare x1 R, ...]" — 상자 속을 미리 굴려 본 결과
	mutable TMap<FString, int32> BlockerNames;

	TArray<FPoolItem> Pool;
	TArray<FJob> Jobs;
	int32 JobCursor = 0;
	int32 ItemCursor = 0; // 현재 Job 안에서 몇 번째 아이템까지 했나
	int32 AttemptCursor = 0;
	FRandomStream JobStream;
	TArray<FStats> Stats;
	TArray<FJob> KnownFacilityJobs; // 타일 계획이 시설 발자국을 피하고, 늦게 뜬 시설을 구분하는 데 쓴다
	TSet<FString> ProcessedFacilityKeys;
	int32 TotalPlaced = 0;
	int32 FacilityPlaced = 0;
	int32 TilePlaced = 0;
	int32 GeometryOnlyCount = 0;
	int32 NavCheckedCount = 0;
	int32 GradeCounts[3] = { 0, 0, 0 };
	int32 FuelPlaced = 0;
	bool bLoggedSummary = false;

	// 길찾기 지도로 "닿을 수 있나" 를 물을 때 출발점으로 쓰는 자리(워존 가운데를 지도에 투영한 것). 없으면 투영 검사만 한다.
	bool bHasNavReference = false;
	FVector NavReference = FVector::ZeroVector;

	TArray<TWeakObjectPtr<AActor>> SpawnedItems;
	TArray<FVector> PlacedLocations; // 서로 너무 붙지 않게(같은 자리에 두 개) 거리 검사용
};
