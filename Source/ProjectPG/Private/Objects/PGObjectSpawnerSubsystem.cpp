#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectSmokeTest.h"

#include "Actors/ItemContainerActor.h"
#include "LevelDesign/PGMapInfo.h"
#include "Common/PGPhysicsUtil.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Objects/PGDestructibleActor.h"
#include "Objects/PGDeviceActor.h"
#include "Objects/PGDoorActor.h"
#include "Objects/PGExitDressingActor.h"
#include "Objects/PGExtractionZoneActor.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGInteractableActorBase.h"
#include "Objects/PGQuestObjectActor.h"
#include "Objects/PGServiceInteractionActor.h"
#include "Objects/PGBoothActor.h"
#include "Objects/PGSpawnSocketComponent.h"
#include "Objects/PGWearableColors.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// 탈출 타일에만 있는 묶음(HISM) 보호. 타일 부품은 스폰 뒤 WarZoneFootprintPreview 의 RuntimePackedVisual_* 묶음으로 합쳐져서
	// 액터 단위로는 표(PGProtected)를 못 붙인다. 대신 "인스턴스가 전부 탈출 지점 11m 안에 있는 묶음" 만 골라 컴포넌트에 표를 붙인다
	// — 그 타일에만 쓰인 것(빨간 EXIT 표시, 버려진 차, 널판)이 여기 걸리고, 맵 전체에 깔린 울타리·풀 묶음은 안 걸린다.
	// 왜 필요한가: 탈출구 표시물이 차에 치여 나뒹굴었다(사용자 9/22). 검문소 부품은 자기 액터에 표가 있어 여기와 무관하다.
	// (익명 namespace 함수 이름에 파일 접두어(PGSpawner_) — 유니티 빌드 이름 충돌 사고가 있었다.)
	int32 PGSpawner_ProtectExitTileVisuals(UWorld* World, const TArray<FVector>& Exits)
	{
		constexpr float NearExitCm = 1100.0f;
		constexpr int32 MaxInstancesToScan = 200; // 탈출 타일 하나에 같은 메시가 200개 넘게 있을 리 없다. 풀·돌 묶음(수천 개)은 여기서 걸러진다.
		int32 Protected = 0;
		for (TObjectIterator<UInstancedStaticMeshComponent> It; It; ++It)
		{
			UInstancedStaticMeshComponent* Component = *It;
			if (!IsValid(Component) || Component->GetWorld() != World || Component->ComponentHasTag(PGPhysicsUtil::ProtectedTag)
				|| !Component->GetName().StartsWith(TEXT("RuntimePackedVisual_")))
				continue;
			const int32 Count = Component->GetInstanceCount();
			if (Count == 0 || Count > MaxInstancesToScan)
				continue;
			bool bAllNearExit = true;
			for (int32 Index = 0; Index < Count && bAllNearExit; ++Index)
			{
				FTransform InstanceTransform;
				bAllNearExit = Component->GetInstanceTransform(Index, InstanceTransform, true)
					&& Exits.ContainsByPredicate([&InstanceTransform, NearExitCm](const FVector& Exit)
					{
						return FVector::DistSquared2D(InstanceTransform.GetLocation(), Exit) < FMath::Square(NearExitCm);
					});
			}
			if (bAllNearExit)
			{
				Component->ComponentTags.AddUnique(PGPhysicsUtil::ProtectedTag);
				++Protected;
			}
		}
		return Protected;
	}
}

UPGObjectSpawnerSubsystem* UPGObjectSpawnerSubsystem::Get(const UObject* WorldContextObject)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	return IsValid(World) ? World->GetSubsystem<UPGObjectSpawnerSubsystem>() : nullptr;
}

void UPGObjectSpawnerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// 설정에 DataTable이 지정돼 있으면 읽는다. 없어도 코드 등록(RegisterCatalogRow)만으로 동작한다.
	const UPGObjectSettings* Settings = GetDefault<UPGObjectSettings>();
	if (!Settings)
		return;
	UDataTable* CatalogTable = Settings->ObjectCatalogTable.IsNull() ? nullptr : Settings->ObjectCatalogTable.LoadSynchronous();
	UDataTable* LootTable = Settings->LootTableTable.IsNull() ? nullptr : Settings->LootTableTable.LoadSynchronous();
	if (CatalogTable || LootTable)
		LoadFromDataTables(CatalogTable, LootTable);
	// DataTable 이 없으면 코드 카탈로그를 기본값으로 쓴다. 이게 없어서 맵 생성 후 자동 스폰이
	// "points=94 spawned=0" 으로 조용히 아무것도 안 만들고 있었다(콘솔 명령만 기본 카탈로그를 넣고 있었음).
	if (Catalog.Num() == 0)
		PGObjectSmokeTest::RegisterDefaultCatalog(*this);
}

void UPGObjectSpawnerSubsystem::RegisterCatalogRow(const FPGObjectCatalogRow& Row)
{
	if (Row.ObjectId.IsNone())
		return;
	Catalog.Add(Row.ObjectId, Row);
}

void UPGObjectSpawnerSubsystem::RegisterLootTable(FName TableId, const FPGLootTableRow& Table)
{
	if (TableId.IsNone())
		return;
	LootTables.Add(TableId, Table);
}

void UPGObjectSpawnerSubsystem::LoadFromDataTables(UDataTable* CatalogTable, UDataTable* LootTableAsset)
{
	if (IsValid(CatalogTable) && CatalogTable->GetRowStruct() == FPGObjectCatalogRow::StaticStruct())
	{
		CatalogTable->ForeachRow<FPGObjectCatalogRow>(TEXT("PGObjectCatalog"), [this](const FName& RowName, const FPGObjectCatalogRow& Row)
		{
			FPGObjectCatalogRow Copy = Row;
			if (Copy.ObjectId.IsNone())
				Copy.ObjectId = RowName;
			RegisterCatalogRow(Copy);
		});
	}
	if (IsValid(LootTableAsset) && LootTableAsset->GetRowStruct() == FPGLootTableRow::StaticStruct())
	{
		LootTableAsset->ForeachRow<FPGLootTableRow>(TEXT("PGLootTables"), [this](const FName& RowName, const FPGLootTableRow& Row)
		{
			RegisterLootTable(RowName, Row);
		});
	}
	UE_LOG(LogPGObjects, Display, TEXT("Object catalog loaded: rows=%d loot_tables=%d"), Catalog.Num(), LootTables.Num());
}

const FPGObjectCatalogRow* UPGObjectSpawnerSubsystem::FindCatalogRow(FName ObjectId) const
{
	return Catalog.Find(ObjectId);
}

const FPGLootTableRow* UPGObjectSpawnerSubsystem::FindLootTable(FName TableId) const
{
	return LootTables.Find(TableId);
}

TArray<FPGItemStack> UPGObjectSpawnerSubsystem::RollLoot(FName TableId, int64 Seed) const
{
	TArray<FPGItemStack> Result;
	const FPGLootTableRow* Table = FindLootTable(TableId);
	if (!Table || Table->Entries.Num() == 0)
	{
		if (!TableId.IsNone())
			UE_LOG(LogPGObjects, Warning, TEXT("Loot table missing or empty: %s"), *TableId.ToString());
		return Result;
	}

	// 64비트 시드를 32비트 스트림에 접어 넣는다. 같은 입력이면 항상 같은 순서로 뽑힌다.
	FRandomStream Stream(static_cast<int32>(Seed ^ (Seed >> 32)));
	TArray<int32> Used;
	for (int32 Roll = 0; Roll < Table->RollCount; ++Roll)
	{
		float TotalWeight = 0.0f;
		for (int32 Index = 0; Index < Table->Entries.Num(); ++Index)
		{
			if (!Table->bAllowDuplicates && Used.Contains(Index))
				continue;
			TotalWeight += FMath::Max(0.0f, Table->Entries[Index].Weight);
		}
		if (TotalWeight <= 0.0f)
			break;

		float Pick = Stream.FRandRange(0.0f, TotalWeight);
		for (int32 Index = 0; Index < Table->Entries.Num(); ++Index)
		{
			if (!Table->bAllowDuplicates && Used.Contains(Index))
				continue;
			const FPGLootEntry& Entry = Table->Entries[Index];
			Pick -= FMath::Max(0.0f, Entry.Weight);
			if (Pick > 0.0f && Index != Table->Entries.Num() - 1)
				continue;

			Used.Add(Index);
			const int32 Count = Stream.RandRange(FMath::Min(Entry.MinCount, Entry.MaxCount), FMath::Max(Entry.MinCount, Entry.MaxCount));
			if (Count > 0 && !Entry.ItemId.IsNone())
			{
				// 옷이면 색을 고른다. 색 고르기는 별도 시드(Seed+Roll)라 기존 뽑기 순서(Stream)는 그대로다.
				const FName ItemId = UPGWearableColorLibrary::PickColorVariant(Entry.ItemId, Seed + Roll);
				// 같은 아이템은 한 묶음으로 합친다.
				FPGItemStack* Existing = Result.FindByPredicate([ItemId](const FPGItemStack& S) { return S.ItemId == ItemId; });
				if (Existing)
					Existing->Count += Count;
				else
					Result.Add({ ItemId, Count });
			}
			break;
		}
	}
	return Result;
}

TSubclassOf<AActor> UPGObjectSpawnerSubsystem::GetDefaultClassForArchetype(EPGObjectArchetype Archetype) const
{
	// 1) 프로젝트 설정 "ProjectPG Objects > Archetype Class Overrides" 에서 바꿔 끼운 클래스(BP 자식 등)가 먼저다 — 코드 수정 없이 확장(OCP, 9/26).
	if (const UPGObjectSettings* Settings = GetDefault<UPGObjectSettings>())
		if (const TSoftClassPtr<AActor>* Override = Settings->ArchetypeClassOverrides.Find(Archetype); Override && !Override->IsNull())
			if (UClass* Loaded = Override->LoadSynchronous())
				return Loaded;

	// 2) 코드 기본값: 원형 → 클래스 표. 한 줄 = 한 원형(전에는 switch). 바닥 아이템만 시각 설정의 BP 를 따르므로 함수로 둔다.
	using FClassGetter = UClass* (*)();
	static const TMap<EPGObjectArchetype, FClassGetter> DefaultClasses = {
		{ EPGObjectArchetype::Container,    []() -> UClass* { return AItemContainerActor::StaticClass(); } },
		{ EPGObjectArchetype::Door,         []() -> UClass* { return APGDoorActor::StaticClass(); } },
		{ EPGObjectArchetype::FloorItem,    []() -> UClass* { return APGFloorItemActor::GetSpawnClass(); } },
		{ EPGObjectArchetype::Extraction,   []() -> UClass* { return APGExtractionZoneActor::StaticClass(); } },
		{ EPGObjectArchetype::Service,      []() -> UClass* { return APGServiceInteractionActor::StaticClass(); } },
		{ EPGObjectArchetype::Device,       []() -> UClass* { return APGDeviceActor::StaticClass(); } },
		{ EPGObjectArchetype::Destructible, []() -> UClass* { return APGDestructibleActor::StaticClass(); } },
		{ EPGObjectArchetype::QuestObject,  []() -> UClass* { return APGQuestObjectActor::StaticClass(); } },
	};
	const FClassGetter* Getter = DefaultClasses.Find(Archetype);
	return Getter ? (*Getter)() : nullptr;
}

AActor* UPGObjectSpawnerSubsystem::SpawnRowInternal(const FPGObjectCatalogRow& Row, const FTransform& Transform, int64 Seed)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
		return nullptr;

	TSubclassOf<AActor> Class = Row.ActorClass.IsNull() ? nullptr : Row.ActorClass.LoadSynchronous();
	if (!Class)
		Class = GetDefaultClassForArchetype(Row.Archetype);
	if (!Class)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("Catalog row %s has no spawnable class (archetype=%d)"), *Row.ObjectId.ToString(), static_cast<int32>(Row.Archetype));
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Actor = World->SpawnActorDeferred<AActor>(Class, Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Actor))
		return nullptr;

	if (APGInteractableActorBase* Interactable = Cast<APGInteractableActorBase>(Actor))
	{
		Interactable->ApplyCatalogRow(Row);
		if (AItemContainerActor* Container = Cast<AItemContainerActor>(Interactable))
			Container->SetLootSeed(Seed);
		// 카탈로그 행은 원래색 ItemId(Pants 등) 하나다. 색은 스폰할 때 시드로 고른다(행을 색마다 늘리지 않으려고).
		if (APGFloorItemActor* FloorItem = Cast<APGFloorItemActor>(Interactable))
		{
			const FName Colored = UPGWearableColorLibrary::PickColorVariant(Row.ItemId, Seed);
			if (Colored != Row.ItemId)
				FloorItem->SetItem(Colored, Row.ItemCount);
			// 연료통처럼 ItemId 는 하나고 겉모습만 여러 개인 아이템도 시드로 고른다.
			FloorItem->SetVisualVariant(static_cast<int32>(Seed >> 8));
		}
	}
	Actor->Tags.AddUnique(TEXT("PGObject"));
	Actor->Tags.AddUnique(Row.ObjectId);
	Actor->FinishSpawning(Transform);

	if (Row.bUniquePerMap)
		UniqueSpawned.Add(Row.ObjectId);
	SpawnedActors.Add(Actor);
	return Actor;
}

AActor* UPGObjectSpawnerSubsystem::SpawnFromCatalog(FName ObjectId, const FTransform& Transform, int64 Seed)
{
	const FPGObjectCatalogRow* Row = FindCatalogRow(ObjectId);
	if (!Row)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("Catalog row not found: %s"), *ObjectId.ToString());
		return nullptr;
	}
	return SpawnRowInternal(*Row, Transform, Seed);
}

void UPGObjectSpawnerSubsystem::CollectCandidates(EPGSpawnSocketKind Kind, uint8 Tier, const TArray<FName>& AllowedObjectIds, TArray<const FPGObjectCatalogRow*>& OutRows) const
{
	for (const TPair<FName, FPGObjectCatalogRow>& Pair : Catalog)
	{
		const FPGObjectCatalogRow& Row = Pair.Value;
		if (!Row.bIncluded || Row.SpawnWeight <= 0.0f)
			continue;
		if (Row.bUniquePerMap && UniqueSpawned.Contains(Row.ObjectId))
			continue;
		if (AllowedObjectIds.Num() > 0)
		{
			if (!AllowedObjectIds.Contains(Row.ObjectId))
				continue;
		}
		else
		{
			// 범용 소켓은 어떤 종류든 받는다. 특정 소켓은 종류가 같아야 한다.
			if (Kind != EPGSpawnSocketKind::Generic && Row.SocketKind != Kind)
				continue;
			// 범용 소켓이라도 플레이어 시작점과 탈것 탈출구(세워 둔 차·헬기)는 받지 않는다. 방 안 소켓에 트럭이 생긴다.
			if (Kind == EPGSpawnSocketKind::Generic && (Row.SocketKind == EPGSpawnSocketKind::PlayerStart || Row.SocketKind == EPGSpawnSocketKind::Vehicle))
				continue;
		}
		if (Row.Tier != 0 && Tier != 0 && Row.Tier > Tier)
			continue;
		OutRows.Add(&Row);
	}
	// 카탈로그는 TMap이라 순서가 보장되지 않는다. 시드 재현을 위해 ID로 정렬한다.
	OutRows.Sort([](const FPGObjectCatalogRow& A, const FPGObjectCatalogRow& B) { return A.ObjectId.LexicalLess(B.ObjectId); });
}

const FPGObjectCatalogRow* UPGObjectSpawnerSubsystem::PickWeighted(const TArray<const FPGObjectCatalogRow*>& Rows, FRandomStream& Stream) const
{
	float Total = 0.0f;
	for (const FPGObjectCatalogRow* Row : Rows)
		Total += Row->SpawnWeight;
	if (Total <= 0.0f)
		return nullptr;
	float Pick = Stream.FRandRange(0.0f, Total);
	for (const FPGObjectCatalogRow* Row : Rows)
	{
		Pick -= Row->SpawnWeight;
		if (Pick <= 0.0f)
			return Row;
	}
	return Rows.Last();
}

AActor* UPGObjectSpawnerSubsystem::SpawnForSocket(UPGSpawnSocketComponent* Socket, FRandomStream& Stream)
{
	if (!IsValid(Socket) || Socket->IsOccupied())
		return nullptr;
	if (Socket->SpawnChance < 1.0f && Stream.FRand() > Socket->SpawnChance)
		return nullptr;

	TArray<const FPGObjectCatalogRow*> Candidates;
	CollectCandidates(Socket->Kind, Socket->Tier, Socket->AllowedObjectIds, Candidates);
	const FPGObjectCatalogRow* Row = PickWeighted(Candidates, Stream);
	if (!Row)
	{
		UE_LOG(LogPGObjects, Verbose, TEXT("Socket %s kind=%d tier=%d: no candidates"), *Socket->GetName(), static_cast<int32>(Socket->Kind), Socket->Tier);
		return nullptr;
	}

	const int64 ObjectSeed = static_cast<int64>(Stream.GetUnsignedInt()) | (static_cast<int64>(Stream.GetUnsignedInt()) << 32);
	AActor* Actor = SpawnRowInternal(*Row, Socket->GetComponentTransform(), ObjectSeed);
	if (IsValid(Actor))
		Socket->SetSpawnedActor(Actor);
	return Actor;
}

int32 UPGObjectSpawnerSubsystem::SpawnAllSockets(int64 Seed)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
		return 0;

	// 월드의 모든 소켓을 모아 결정적 순서로 정렬한다. 액터 이터레이터 순서는 신뢰하지 않는다.
	TArray<UPGSpawnSocketComponent*> Sockets;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TInlineComponentArray<UPGSpawnSocketComponent*> Found;
		It->GetComponents(Found);
		Sockets.Append(Found);
	}
	Sockets.Sort([Seed](const UPGSpawnSocketComponent& A, const UPGSpawnSocketComponent& B)
	{
		return A.MakeSeed(Seed) < B.MakeSeed(Seed);
	});

	// 같은 ExclusiveGroup에서는 시드로 하나만 고른다 (저수지 샘플링: k번째 후보를 1/k 확률로 교체).
	TMap<FName, UPGSpawnSocketComponent*> GroupWinners;
	FRandomStream GroupStream(static_cast<int32>(Seed ^ 0x5bd1e995));
	{
		TMap<FName, int32> SeenPerGroup;
		for (UPGSpawnSocketComponent* Socket : Sockets)
		{
			if (Socket->ExclusiveGroup.IsNone())
				continue;
			int32& Seen = SeenPerGroup.FindOrAdd(Socket->ExclusiveGroup);
			++Seen;
			if (GroupStream.RandRange(1, Seen) == 1)
				GroupWinners.Add(Socket->ExclusiveGroup, Socket);
		}
	}

	int32 Spawned = 0;
	for (UPGSpawnSocketComponent* Socket : Sockets)
	{
		if (!Socket->ExclusiveGroup.IsNone() && GroupWinners.FindRef(Socket->ExclusiveGroup) != Socket)
			continue;
		FRandomStream Stream(static_cast<int32>(Socket->MakeSeed(Seed)));
		if (IsValid(SpawnForSocket(Socket, Stream)))
			++Spawned;
	}
	UE_LOG(LogPGObjects, Display, TEXT("Spawn sockets: seed=%lld sockets=%d spawned=%d"), Seed, Sockets.Num(), Spawned);
	return Spawned;
}

int32 UPGObjectSpawnerSubsystem::SpawnFromLevelDesignPoints(const TArray<FLevelDesignPoint>& Points, int64 Seed)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
		return 0;

	int32 Spawned = 0;
	int32 GroundLoot = 0;
	TSet<int32> BoothPointIndices;
	SpawnBooths(Points, Seed, BoothPointIndices);
	// 맵 가운데 ≈ 모든 포인트의 평균. 탈출 세트를 "바깥쪽"을 보게 세우는 데 쓴다(탈출 지점은 맵 가장자리 도로 끝에 있다).
	FVector MapCenter = FVector::ZeroVector;
	for (const FLevelDesignPoint& Point : Points)
		MapCenter += Point.WorldLocation;
	if (Points.Num() > 0)
		MapCenter /= static_cast<float>(Points.Num());
	// 탈출구 번호(로그용)와 자리. 자리는 끝에 탈출 타일 묶음 보호에 쓴다.
	int32 ExitIndex = 0;
	TArray<FVector> ExitLocations;
	for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
	{
		const FLevelDesignPoint& Point = Points[PointIndex];
		if (BoothPointIndices.Contains(PointIndex))
			continue; // 부스가 선 자리
		EPGSpawnSocketKind Kind;
		switch (Point.Type)
		{
		case ELevelDesignPointType::Loot:  Kind = EPGSpawnSocketKind::Container;  break;
		case ELevelDesignPointType::Exit:  Kind = EPGSpawnSocketKind::Extraction; break;
		case ELevelDesignPointType::Quest: Kind = EPGSpawnSocketKind::QuestItem;  break;
		default: continue; // Spawn(플레이어)·AISpawn은 각 담당 시스템이 쓴다.
		}

		TArray<const FPGObjectCatalogRow*> Candidates;
		CollectCandidates(Kind, Point.Tier, {}, Candidates);
		FRandomStream Stream(static_cast<int32>(Point.PointSeed ^ Seed ^ (Seed >> 32)));
		const FPGObjectCatalogRow* Row = PickWeighted(Candidates, Stream);
		if (!Row)
			continue;
		// 바닥에 붙이기.
		// 포인트 높이는 바닥 + 120cm다(WarZoneFootprintPreview::BuildGameplayPointMarkers).
		// 캐릭터 스폰도 같은 포인트를 쓰므로 원본은 두고, 오브젝트용 복사본만 바닥으로 내린다.
		// Point는 const 참조라 직접 못 바꾼다 → 바꿀 수 있는 복사본을 만든다.
		FVector SpawnLocation = Point.WorldLocation;
		// 트레이스 결과(맞은 지점·맞은 액터)를 받을 빈 변수.
		FHitResult HitResult;
		// 포인트에서 아래로 5m 선을 쏴서 처음 부딪히는 충돌체를 찾는다. 맞으면 true.
		// 5m인 이유: 2층 포인트도 자기 층 바닥 120cm 위라 충분하고, 더 길면 아래층까지 뚫을 수 있다.
		const bool bHit = World->LineTraceSingleByChannel(
				HitResult, SpawnLocation, SpawnLocation - FVector(0.0f,0.0f,500.0f),ECC_Visibility);
		// 맞았을 때만 높이를 바닥으로 바꾼다. 못 맞으면(바닥 없음) 원래 높이 그대로 스폰한다.
		if (!bHit||HitResult.ImpactPoint.Z<0.0f)
			continue;
		SpawnLocation.Z = HitResult.ImpactPoint.Z;
		// 탈출 지점은 카탈로그에서 하나를 뽑지 않고 세트로 놓는다(차·헬기·펜스 — 기획서 3.3.2 그림).
		if (Kind == EPGSpawnSocketKind::Extraction)
		{
			FVector Outward = (Point.WorldLocation - MapCenter).GetSafeNormal2D();
			if (Outward.IsNearlyZero())
				Outward = FVector::ForwardVector;
			// 맵 가운데에서 본 방향은 모서리 탈출구에서 비스듬하다. 탈출 타일의 길 축(격자 축)으로 바로잡아 차·게이트·검문소가 길을 따라 선다.
			// 타일 가운데 = 칸 번호 × 칸 크기(2000cm, WarZoneFootprintPreview::BuildGameplayPointMarkers 와 같은 식).
			Outward = APGExitDressingActor::ResolveLaneAxis(SpawnLocation, FVector(Point.GridCell.X * 2000.0f, Point.GridCell.Y * 2000.0f, SpawnLocation.Z), Outward);
			const int32 FirstExitActor = SpawnedActors.Num();
			const int32 ExitPlaced = SpawnExitSet(SpawnLocation, Outward, Stream, Point.PointSeed ^ Seed);
			Spawned += ExitPlaced;
			if (ExitPlaced > 0)
			{
				// 검문소 꾸미기(Objects/PGExitDressingActor): 탈출 세트가 놓은 액터(차·헬기·게이트·영역)의 둘레는 비우고 그 밖을 꾸민다.
				// 종류는 놓인 액터로 본다 — 게이트(문 액터)가 있으면 펜스 세트. 반환값(1/2)으로 보면 게이트만 실패한 펜스 세트를 탈것으로 오해한다.
				FBox KeepClear(ForceInit);
				bool bGateSet = false;
				for (int32 Index = FirstExitActor; Index < SpawnedActors.Num(); ++Index)
				{
					if (AActor* ExitActor = SpawnedActors[Index].Get())
					{
						FVector Origin, Extent;
						ExitActor->GetActorBounds(false, Origin, Extent);
						KeepClear += FBox(Origin - Extent, Origin + Extent);
						bGateSet |= ExitActor->IsA<APGDoorActor>();
					}
				}
				// 꾸미기는 따로 만든 시드 스트림을 쓴다. 탈출구 종류를 고른 Stream 을 이어 쓰면 나중에 꾸미기 순서를 바꿀 때 같은 시드의 탈출구까지 바뀐다.
				FRandomStream DressingStream(static_cast<int32>((Point.PointSeed ^ Seed ^ (Seed >> 32)) * 31 + 0x0D8E55));
				if (APGExitDressingActor* Dressing = APGExitDressingActor::SpawnCheckpoint(World, SpawnLocation, Outward,
					bGateSet ? EPGExitDressingKind::FenceGate : EPGExitDressingKind::Vehicle, KeepClear, DressingStream, ExitIndex))
				{
					SpawnedActors.Add(Dressing);
				}
			}
			++ExitIndex;
			ExitLocations.Add(SpawnLocation);
			continue;
		}
		// 원래 Point.WorldLocation 자리에 바닥으로 내린 SpawnLocation을 넣는다.
		const FTransform Transform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), SpawnLocation);
		if (IsValid(SpawnRowInternal(*Row, Transform, Point.PointSeed ^ Seed)))
			++Spawned;

		// 바닥 루팅: 상자 옆에 무기·탄약·소비품·장비를 몇 개 흩뿌린다. 같은 시드면 같은 결과.
		if (Kind == EPGSpawnSocketKind::Container)
			GroundLoot += ScatterGroundLoot(SpawnLocation, Point.Tier, Stream, Point.PointSeed ^ Seed);
	}
	// 탈출 타일에만 있는 묶음(EXIT 표시·버려진 차)에 부서짐 보호 표를 붙인다. 검문소 부품은 자기 액터에 표가 있다.
	const int32 ProtectedBatches = ExitLocations.Num() > 0 ? PGSpawner_ProtectExitTileVisuals(World, ExitLocations) : 0;
	UE_LOG(LogPGObjects, Display, TEXT("Spawn from level design points: seed=%lld points=%d spawned=%d ground_loot=%d exits=%d exit_protected_batches=%d"),
		Seed, Points.Num(), Spawned, GroundLoot, ExitLocations.Num(), ProtectedBatches);
	return Spawned;
}

int32 UPGObjectSpawnerSubsystem::SpawnBooths(const TArray<FLevelDesignPoint>& Points, int64 Seed, TSet<int32>& OutUsedPoints)
{
	UWorld* World = GetWorld();
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
		return 0;

	// 기획서 3.3.3: 상점은 워존 루팅 스폿에 선다. 9/20~22 에는 부스 4종을 한 줄로 붙여 세웠는데,
	// 9/23 사용자: "몰려 있다 — 워존 끝쪽 동서남북 4방향으로 나눠 달라". 그래서 방향마다 하나씩 워존 가장자리에 세운다.
	// 워존은 폐허 루팅 자리(RuinsLooseLoot)들이 깔린 곳이다 → 그 평균이 워존 가운데, 방향별로 가장 바깥 폐허 자리가 워존 끝.
	// 그 방향에 폐허 자리가 없거나 전부 막혀 있으면 같은 방향의 들판·은닉처 자리로 넘어간다(9/23: 한 줄도 못 서던 맵이 있었다).
	// 부스는 보호막으로 둘러싸여 몬스터·탈것이 못 들어온다(APGBoothActor::BuildBarrier) — 예전의 "몬스터에서 먼 자리" 정렬은 뺐다.
	TArray<TArray<int32>> Groups;
	for (const TCHAR* Archetype : { TEXT("RuinsLooseLoot"), TEXT("FieldCache"), TEXT("HiddenStash") })
	{
		TArray<int32>& Group = Groups.AddDefaulted_GetRef();
		for (int32 Index = 0; Index < Points.Num(); ++Index)
			if (Points[Index].Type == ELevelDesignPointType::Loot && Points[Index].ArchetypeId == Archetype)
				Group.Add(Index);
	}
	// 워존 칸: 맵 짓기가 깐 워존 바닥 칸들. 9/23 시험에서 폐허 루팅 자리는 맵마다 0개였다(요즘 워존은 폐허 타일이 아니라
	//   워존 바닥 타일이다) — 폐허 자리로 워존을 재면 워존이 아니라 맵 전체 평균이 나왔다. 그래서 칸을 직접 읽는다.
	TArray<FVector> WarZoneCells;
	if (const IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(World))
		WarZoneCells = Map->GetWarZoneCellCentres();
	// 워존 가운데: 워존 칸 평균. 칸을 못 읽으면 폐허 → 들판 → 은닉처 루팅 자리 평균.
	FVector Centre = FVector::ZeroVector;
	int32 CentreCount = 0;
	for (const FVector& Cell : WarZoneCells)
	{
		Centre += Cell;
		++CentreCount;
	}
	for (int32 GroupIndex = 0; GroupIndex < Groups.Num() && CentreCount == 0; ++GroupIndex)
		for (const int32 Index : Groups[GroupIndex])
		{
			Centre += Points[Index].WorldLocation;
			++CentreCount;
		}
	if (CentreCount == 0)
	{
		UE_LOG(LogPGObjects, Display, TEXT("Booths: no war zone cell and no outdoor loot point"));
		return 0;
	}
	Centre /= CentreCount;

	FRandomStream Stream(static_cast<int32>(Seed ^ (Seed >> 32) ^ 0x5B00F));
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGBoothFit), false);
	constexpr float Depth = 450.0f;          // 부스 깊이 + 앞에 손님 설 자리
	constexpr float MinBoothSpacing = 3000.0f; // 부스끼리 30m 는 띄운다(나눠 세우는 뜻)
	TArray<FVector> Placed;

	// 부스 하나를 자리 근처 평평한 빈 땅에 세운다. 창구(+X)는 워존 가운데를 본다.
	auto TryPlace = [&](const FVector& Where, int32 PointIndex, EPGBoothKind Kind, const TCHAR* DirName, const TCHAR* GroupName) -> bool
	{
		FHitResult Ground;
		const FVector From = Where + FVector(0.0f, 0.0f, 300.0f);
		if (!World->LineTraceSingleByChannel(Ground, From, From - FVector(0.0f, 0.0f, 800.0f), ECC_Visibility)
			|| Ground.ImpactPoint.Z < 0.0f)
			return false;
		for (const FVector& Other : Placed)
			if (FVector::Dist2D(Other, Ground.ImpactPoint) < MinBoothSpacing)
				return false;
		const float Width = APGBoothActor::GetHalfWidth(Kind) * 2.0f;
		const FVector ToCentre = (Centre - Ground.ImpactPoint).GetSafeNormal2D();
		const float FaceYaw = ToCentre.IsNearlyZero() ? 0.0f : ToCentre.Rotation().Yaw;
		// 폐허 칸은 벽·잔해가 많다. 가운데를 보는 방향부터 45도씩 돌려 보고, 그다음 자리를 조금씩 옮겨 가며 빈 땅을 찾는다.
		static const float YawSteps[] = { 0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f };
		for (int32 Try = 0; Try < 24; ++Try)
		{
			const float Yaw = FaceYaw + YawSteps[Try % 8] + Stream.FRandRange(-10.0f, 10.0f);
			const FRotator Facing(0.0f, Yaw, 0.0f);
			const FVector Shift = Try < 8 ? FVector::ZeroVector : FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f).Vector() * Stream.FRandRange(200.0f, 500.0f);
			const FVector Origin = Ground.ImpactPoint + Shift;
			const FVector BoxCenter = Origin + FVector(0.0f, 0.0f, 170.0f) + Facing.Vector() * (Depth * 0.5f - 200.0f);
			if (World->OverlapBlockingTestByChannel(BoxCenter, Facing.Quaternion(), ECC_Pawn,
				FCollisionShape::MakeBox(FVector(Depth * 0.5f, Width * 0.5f + 50.0f, 150.0f)), Params))
				continue;
			// 네 모서리 땅 높이가 비슷해야(경사·구덩이 위에 뜨지 않게).
			bool bFlat = true;
			for (const FVector2D Corner : { FVector2D(-1, -1), FVector2D(-1, 1), FVector2D(1, -1), FVector2D(1, 1) })
			{
				const FVector Probe = Origin + Facing.RotateVector(FVector(Corner.X * 150.0f, Corner.Y * Width * 0.5f, 300.0f));
				FHitResult CornerHit;
				if (!World->LineTraceSingleByChannel(CornerHit, Probe, Probe - FVector(0.0f, 0.0f, 700.0f), ECC_Visibility)
					|| FMath::Abs(CornerHit.ImpactPoint.Z - Origin.Z) > 40.0f)
				{
					bFlat = false;
					break;
				}
			}
			if (!bFlat)
				continue;
			const FTransform BoothTransform(Facing, Origin);
			APGBoothActor* Booth = World->SpawnActorDeferred<APGBoothActor>(APGBoothActor::GetSpawnClass(), BoothTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Booth)
				return false;
			Booth->SetBoothKind(Kind);
			Booth->FinishSpawning(BoothTransform);
			SpawnedActors.Add(Booth);
			Placed.Add(Origin);
			if (PointIndex != INDEX_NONE)
				OutUsedPoints.Add(PointIndex);
			UE_LOG(LogPGObjects, Display, TEXT("Booth: %s at %s — %s side of the war zone (%s point %s, %.0f m from the centre, yaw %.0f)"),
				*StaticEnum<EPGBoothKind>()->GetNameStringByValue(static_cast<int64>(Kind)), *Origin.ToCompactString(), DirName, GroupName,
				PointIndex != INDEX_NONE ? *Points[PointIndex].PointId.ToString() : TEXT("-"), FVector::Dist2D(Origin, Centre) * 0.01f, Yaw);
			return true;
		}
		return false;
	};

	struct FBoothSide { FVector2D Dir; EPGBoothKind Kind; const TCHAR* Name; };
	const FBoothSide Sides[] = {
		{ FVector2D(1.0f, 0.0f), EPGBoothKind::Exchange, TEXT("+X") },
		{ FVector2D(0.0f, 1.0f), EPGBoothKind::Craft, TEXT("+Y") },
		{ FVector2D(-1.0f, 0.0f), EPGBoothKind::Shop, TEXT("-X") },
		{ FVector2D(0.0f, -1.0f), EPGBoothKind::Parcel, TEXT("-Y") },
	};
	static const TCHAR* const GroupNames[] = { TEXT("ruins"), TEXT("field"), TEXT("stash") };
	int32 PlacedCount = 0;
	for (const FBoothSide& Side : Sides)
	{
		// 그 방향 워존 끝까지의 거리(가장 바깥 워존 칸). 칸이 없으면 루팅 자리로 잰다.
		float EdgeDistance = 0.0f;
		for (const FVector& Cell : WarZoneCells)
			EdgeDistance = FMath::Max(EdgeDistance, static_cast<float>(FVector2D::DotProduct(FVector2D(Cell - Centre), Side.Dir)));
		if (WarZoneCells.IsEmpty())
			for (const TArray<int32>& Group : Groups)
				for (const int32 Index : Group)
					EdgeDistance = FMath::Max(EdgeDistance, static_cast<float>(FVector2D::DotProduct(FVector2D(Points[Index].WorldLocation - Centre), Side.Dir)));
		const FVector2D Edge = FVector2D(Centre) + Side.Dir * EdgeDistance;
		auto InCone = [&](const FVector& Location)
		{
			const FVector2D Offset(Location - Centre);
			return FVector2D::DotProduct(Offset.GetSafeNormal(), Side.Dir) > 0.7071f; // 그 방향 가운데에서 45도 안
		};
		bool bPlaced = false;
		// 1순위: 그 방향 가장 바깥 워존 칸(= 워존 끝)부터.
		{
			TArray<FVector> Cells;
			for (const FVector& Cell : WarZoneCells)
				if (InCone(Cell))
					Cells.Add(Cell);
			Cells.Sort([&](const FVector& A, const FVector& B)
			{
				return FVector2D::DotProduct(FVector2D(A - Centre), Side.Dir) > FVector2D::DotProduct(FVector2D(B - Centre), Side.Dir);
			});
			for (const FVector& Cell : Cells)
				if (TryPlace(Cell, INDEX_NONE, Side.Kind, Side.Name, TEXT("war zone edge cell")))
				{
					bPlaced = true;
					break;
				}
		}
		// 2순위: 그 방향 워존 끝에 가까운 바깥 루팅 자리(폐허 → 들판 → 은닉처).
		for (int32 GroupIndex = 0; GroupIndex < Groups.Num() && !bPlaced; ++GroupIndex)
		{
			TArray<int32> Candidates;
			for (const int32 Index : Groups[GroupIndex])
				if (!OutUsedPoints.Contains(Index) && InCone(Points[Index].WorldLocation))
					Candidates.Add(Index);
			Candidates.Sort([&](int32 A, int32 B)
			{
				return FVector2D::DistSquared(FVector2D(Points[A].WorldLocation), Edge) < FVector2D::DistSquared(FVector2D(Points[B].WorldLocation), Edge);
			});
			for (const int32 Index : Candidates)
				if (TryPlace(Points[Index].WorldLocation, Index, Side.Kind, Side.Name, GroupNames[GroupIndex]))
				{
					bPlaced = true;
					break;
				}
		}
		if (bPlaced)
			++PlacedCount;
		else
			UE_LOG(LogPGObjects, Warning, TEXT("Booth: no open flat ground on the %s side for %s"), Side.Name,
				*StaticEnum<EPGBoothKind>()->GetNameStringByValue(static_cast<int64>(Side.Kind)));
	}
	UE_LOG(LogPGObjects, Display, TEXT("Booths: %d of 4 placed around the war zone (centre %s, %d war zone cell(s))"),
		PlacedCount, *Centre.ToCompactString(), WarZoneCells.Num());
	return PlacedCount;
}

int32 UPGObjectSpawnerSubsystem::ScatterGroundLoot(const FVector& Center, uint8 Tier, FRandomStream& Stream, int64 Seed)
{
	const UPGObjectSettings* Settings = GetDefault<UPGObjectSettings>();
	UWorld* World = GetWorld();
	if (!IsValid(World) || !Settings || Settings->GroundLootChance <= 0.0f || Stream.FRand() > Settings->GroundLootChance)
		return 0;

	// 종류별 비중. 무기·탄약이 눈에 잘 띄어야 "저기 총 있다"가 된다.
	static const EPGSpawnSocketKind Kinds[] = { EPGSpawnSocketKind::Weapon, EPGSpawnSocketKind::Weapon, EPGSpawnSocketKind::Ammo, EPGSpawnSocketKind::Ammo, EPGSpawnSocketKind::Consumable, EPGSpawnSocketKind::Consumable, EPGSpawnSocketKind::Item };
	const int32 Count = Stream.RandRange(FMath::Max(0, Settings->GroundLootMin), FMath::Max(Settings->GroundLootMin, Settings->GroundLootMax));
	int32 Placed = 0;
	for (int32 I = 0; I < Count; ++I)
	{
		TArray<const FPGObjectCatalogRow*> Candidates;
		CollectCandidates(Kinds[Stream.RandRange(0, UE_ARRAY_COUNT(Kinds) - 1)], Tier, {}, Candidates);
		const FPGObjectCatalogRow* Row = PickWeighted(Candidates, Stream);
		if (!Row)
			continue;
		const float Angle = Stream.FRandRange(0.0f, 2.0f * PI);
		const float Dist = Stream.FRandRange(80.0f, Settings->GroundLootRadius);
		const FVector Probe = Center + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 120.0f);
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Probe, Probe - FVector(0.0f, 0.0f, 500.0f), ECC_Visibility))
			continue;
		// 상자 위·벽 위에 얹히지 않게: 상자 바닥과 높이 차가 크면 버린다.
		if (FMath::Abs(Hit.ImpactPoint.Z - Center.Z) > 60.0f)
			continue;
		const FTransform Transform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), Hit.ImpactPoint);
		if (IsValid(SpawnRowInternal(*Row, Transform, Seed + I + 1)))
			++Placed;
	}
	return Placed;
}

int32 UPGObjectSpawnerSubsystem::SpawnExitSet(const FVector& Ground, const FVector& Outward, FRandomStream& Stream, int64 Seed)
{
	// 기획서 3.3.2 "시작점/탈출구" 그림: 잠긴 펜스 + 열쇠, 헬기 + 연료통, 배 + 연료통, 맨홀.
	// 이 맵에서는 차·헬기·펜스 세 가지(9/18 결정: 배는 필요 없음, 맨홀은 뒤로). 같은 시드면 같은 세트.
	//  - 차·헬기: 탈출구 액터 자체가 세워 둔 탈것 모양(카탈로그 Mesh). 연료통을 갖고 F. 헬기는 메시가 생겨 행이 켜져야 후보가 된다.
	//  - 펜스: 잠긴 펜스 게이트(일반 열쇠) + 그 바깥 3m 의 탈출 영역(3초 서 있기).
	static const FName FenceSet(TEXT("FenceSet"));
	TArray<FName, TInlineAllocator<3>> Choices;
	for (const TCHAR* VehicleId : { TEXT("OBJ-068"), TEXT("OBJ-066") })
	{
		const FPGObjectCatalogRow* Row = FindCatalogRow(VehicleId);
		if (Row && Row->bIncluded && !Row->Mesh.IsNull())
			Choices.Add(Row->ObjectId);
	}
	const FPGObjectCatalogRow* GateRow = FindCatalogRow(TEXT("OBJ-022"));
	const FPGObjectCatalogRow* FenceZoneRow = FindCatalogRow(TEXT("OBJ-064"));
	if (GateRow && FenceZoneRow)
		Choices.Add(FenceSet);
	if (Choices.Num() == 0)
		return 0;

	const FName Picked = Choices[Stream.RandRange(0, Choices.Num() - 1)];
	const FRotator Facing(0.0f, Outward.Rotation().Yaw, 0.0f);
	if (Picked != FenceSet)
	{
		// 차는 바깥쪽을 보고 서 있다 — "타고 저쪽으로 빠져나간다".
		AActor* VehicleExit = SpawnRowInternal(*FindCatalogRow(Picked), FTransform(Facing, Ground), Seed);
		// 9/22 사용자 결정: 모든 탈출구가 같은 방식 — 연료통을 들고 F, 그 자리에서 5초 가만히 버티면 탈출(화면에 5·4·3·2·1, 움직이면 취소).
		// 카탈로그 행은 스모크 테스트와 같이 쓰므로 행을 바꾸지 않고, 검문소에 세운 이 탈것에만 덮어쓴다.
		if (APGExtractionZoneActor* Zone = Cast<APGExtractionZoneActor>(VehicleExit))
		{
			Zone->Configure(EPGExtractionTrigger::Interact, 5.0f, TEXT("Fuel"), true, 0);
			Zone->SetHoldStill(true);
		}
		return IsValid(VehicleExit) ? 1 : 0;
	}

	int32 Placed = 0;
	FPGObjectCatalogRow LockedGate = *GateRow;
	LockedGate.bLocked = true;
	LockedGate.RequiredKeyId = TEXT("Key_Common");
	// 게이트는 길을 가로질러 선다. 예전에는 바깥쪽을 보게(Facing) 놓아서 문짝(메시 -X 로 2m)이 길과 나란히 누운 펜스 한 칸이었다.
	// 경첩(피벗)을 길 오른쪽 1m 에 두고 90도 돌리면 문짝이 왼쪽으로 뻗어 길 가운데(-1m~+1m)를 막는다.
	// 검문소 꾸미기(PGExitDressingActor)의 펜스 줄이 이 문짝 양옆(|Y| ≥ 1.2m)에서 이어져 "문이 달린 담" 이 된다.
	const FVector Right = Facing.RotateVector(FVector::RightVector);
	const FTransform GateTransform(FRotator(0.0f, Facing.Yaw + 90.0f, 0.0f), Ground + Right * 100.0f);
	if (IsValid(SpawnRowInternal(LockedGate, GateTransform, Seed)))
		++Placed;

	// 영역은 게이트 바깥쪽 3m, 바닥에 붙인다.
	FVector ZoneLocation = Ground + Outward * 300.0f;
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ZoneLocation + FVector(0.0f, 0.0f, 200.0f), ZoneLocation - FVector(0.0f, 0.0f, 300.0f), ECC_Visibility))
		ZoneLocation.Z = Hit.ImpactPoint.Z;
	if (APGExtractionZoneActor* Zone = Cast<APGExtractionZoneActor>(SpawnRowInternal(*FenceZoneRow, FTransform(Facing, ZoneLocation), Seed + 1)))
	{
		// 영역에도 열쇠를 요구한다(소모는 안 함). 아직 펜스가 게이트 한 칸뿐이라 옆으로 돌아 들어올 수 있어서다.
		// 펜스 벽을 게이트 양옆에 세우면 이 요구는 빼도 된다.
		Zone->Configure(EPGExtractionTrigger::Overlap, 3.0f, TEXT("Key_Common"), false, 0);
		Zone->SetZoneBox(FVector(0.0f, 0.0f, 100.0f), FVector(250.0f, 400.0f, 150.0f));
		// 9/22: 검문소 꾸미기(PGExitDressingActor)가 문틀~펜스 문 너머 5m 를 덮는 "열쇠 + 5초 버티기" 영역을 따로 세운다.
		// 이 영역까지 살아 있으면 같은 자리에서 카운트다운이 둘 뜨고, 이쪽은 3초 조용히 끝나 규칙이 둘이 된다 → 겹침 감지만 끈다.
		// 설정·자리는 그대로 둔다(검문소 꾸미기가 이 액터 둘레를 비우는 기준으로 쓰고, 스모크 테스트가 설정을 확인한다).
		Zone->SetActorEnableCollision(false);
		++Placed;
	}
	return Placed;
}

void UPGObjectSpawnerSubsystem::ResetSpawnState()
{
	UniqueSpawned.Reset();
	SpawnedActors.Reset();
}
