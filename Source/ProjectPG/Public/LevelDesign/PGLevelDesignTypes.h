// 맵 설계 자료형 — 칸 배치(FTileDesignPlacement)·게임 지점(FLevelDesignPoint)·시설 배치(FFacilityPlacement)와 그 열거형.
// 왜 따로 뺐나(2026-09-26 SOLID 정리): 원래 AWarZoneFootprintPreview.h 안에 있어서, 게임 지점만 읽으면 되는 스포너·피날레도
// 맵 액터 헤더 전체(필드 200여 개)를 끌어와야 했다. 자료형만 여기 두면 읽는 쪽은 맵 클래스를 몰라도 된다(IPGMapInfo 와 짝).
// 옮기기만 했다 — 구조체·열거형 이름과 값은 그대로라 저장된 에셋·BP 는 영향이 없다(언리얼은 헤더가 아니라 모듈+이름으로 찾는다).

#pragma once

#include "CoreMinimal.h"
#include "PGLevelDesignTypes.generated.h"

class UWorld;

UENUM(BlueprintType)
enum class EFacilityVisualSet : uint8
{
	Warehouse,
	Yard,
	LongBarracks,
	LinearTrench,
	DowntownBlock,
	FactoryConstruction,
	RuralHideout,
	Checkpoint
};

UENUM(BlueprintType)
enum class EFacilityElevationProfile : uint8
{
	Ground,
	RaisedBarracks,
	RaisedCompound,
	WarZoneStronghold
};

UENUM(BlueprintType)
enum class ETileDesignVisual : uint8
{
	OpenGround,
	Ruins,
	NatureMeadow,
	NatureForestSparse,
	NatureForestDense,
	NatureRocky,
	NatureScrub,
	NatureAmbush,
	NatureServiceCamp,
	NatureDitch,
	WarZoneGround,
	RoadStraight,
	RoadCorner,
	RoadTJunction,
	RoadCross,
	RoadDeadEnd,
	Spawn,
	Exit,
	Obstacle,
	// Border lake. The design doc lists water among the Maze's impassable terrain,
	// so these cells are deliberately not walkable: the bank at the shoreline is the
	// barrier, and VerifyTraversableElevation excludes them for that reason.
	Water
};

USTRUCT(BlueprintType)
struct FTileDesignPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ETileDesignVisual Visual = ETileDesignVisual::OpenGround;

	// N=1, E=2, S=4, W=8 in design-world coordinates.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 ConnectionMask = 0;

	// True for deterministic design-layer access roads that connect large POIs.
	// These use the lightweight road builder so a long corridor does not repeat
	// the fully dressed 1x1 authored showcase tile every 20 metres.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	bool bSupplementalAccessRoad = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RotationQuarterTurns = 0;

	// Stable authored combat-layout selection (0..3). This is serialized in the
	// future TileManifest instead of being recomputed differently per client.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 LayoutVariant = 0;

	// Per-cell deterministic prop/dressing seed. The server manifest sends this
	// value directly; clients never use local time or recompute it differently.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LocalSeed = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> VisualLevel;
};

UENUM(BlueprintType)
enum class ELevelDesignPointType : uint8
{
	Spawn,
	Loot,
	AISpawn,
	Exit,
	Quest
};

USTRUCT(BlueprintType)
struct FLevelDesignPoint
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	ELevelDesignPointType Type = ELevelDesignPointType::Spawn;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint GridCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName PointId = NAME_None;

	// Data-only contract consumed later by the authoritative spawn/loot system.
	// No replicated gameplay actor is created by the level designer.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName ArchetypeId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	uint8 Tier = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float RadiusCm = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 Capacity = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 PointSeed = 0;
};

USTRUCT(BlueprintType)
struct FFacilityPlacement
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FName FacilityId = NAME_None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EFacilityVisualSet VisualSet = EFacilityVisualSet::Warehouse;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint AnchorCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint Footprint = FIntPoint(2, 2);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int32 RotationQuarterTurns = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	int64 LocalSeed = 0;

	// Height and access are part of the deterministic facility manifest.  Clients
	// must not infer these from local traces or asset bounds.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	EFacilityElevationProfile ElevationProfile = EFacilityElevationProfile::Ground;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	float BaseElevationCm = 0.0f;

	// Occupied edge cell, adjacent ground cell, and outward cardinal direction.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint EntranceCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint AccessCell = FIntPoint::ZeroValue;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	FIntPoint EntranceDirection = FIntPoint(0, -1);

	// The authored facility level selected for this logical reservation.
	// A soft reference keeps the heavy level unloaded during grid generation.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TSoftObjectPtr<UWorld> FacilityLevel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TArray<FIntPoint> OccupiedCells;
};

// 게임플레이 포인트(Spawn/Loot/AI/Exit/Quest)가 확정됐을 때 한 번 알린다.
// 오브젝트 스포너(UPGObjectSpawnerSubsystem)가 여기에 붙어 Loot/Exit/Quest 자리에 실제 오브젝트를 만든다.
// 논리 격자 설계도 — 서버가 클라이언트에 보내는 "맵의 입력" 전부(2026-09-27 멀티).
// 왜 입력을 보내나: 맵은 이 입력(칸 종류 + 판 시드)만 같으면 어느 컴퓨터에서 돌려도 같은 결과가 나오게 짜여 있다(시간·로컬 난수 안 씀).
//   그래서 수천 개 타일·풀·시설을 하나하나 보내지 않고, 900칸 종류(900바이트)와 시드만 보내 클라이언트가 같은 맵을 스스로 짓는다.
// 칸 종류(ETileType)는 팀원 생성기가 정한 값 그대로다. 255 = 칸 없음.
USTRUCT()
struct FPGLogicalGridManifest
{
	GENERATED_BODY()

	UPROPERTY()
	int64 RaidSeed = 0;

	// 칸 간격(cm)과 칸 액터 높이 — 클라이언트가 같은 자리에 숨긴 칸 사본을 깐다.
	UPROPERTY()
	float GridStep = 0.0f;

	UPROPERTY()
	float TileZ = 0.0f;

	UPROPERTY()
	FIntPoint MinCell = FIntPoint::ZeroValue;

	UPROPERTY()
	int32 Width = 0;

	UPROPERTY()
	int32 Height = 0;

	// 행 우선(Y 가 바깥 반복). Types[(Y - MinCell.Y) * Width + (X - MinCell.X)]
	UPROPERTY()
	TArray<uint8> Types;

	// 생성기의 시작 구역 크기(원격 거점 모서리 판단에 쓴다 — 서버만 게임모드에서 읽을 수 있어 같이 보낸다).
	UPROPERTY()
	int32 StartRangeSize = 4;

	bool IsValid() const { return Width > 0 && Height > 0 && GridStep > 0.0f && Types.Num() == Width * Height; }
};

DECLARE_MULTICAST_DELEGATE_OneParam(FOnLevelDesignPointsBuilt, const TArray<FLevelDesignPoint>&);
