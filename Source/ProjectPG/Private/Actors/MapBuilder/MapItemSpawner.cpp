#include "Actors/MapBuilder/MapItemSpawner.h"

#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actor/InteractActor.h"
#include "Components/InventoryComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "TimerManager.h"

// 맵이 놓은 상자에 붙이는 표. 들어온 사람 쪽에서 "맵이 놓은 상자" 만 세는 데 쓴다.
static const FName MapLootContainerTag(TEXT("MapLootContainer"));

void UMapItemSpawner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapItemSpawner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 상자를 바닥에 붙인다: 상자 전체(몸통·뚜껑) 아래면과 바닥의 높이 차이만큼 올린다(+1cm: 바닥과 같은 높이면 깜빡거림).
// 왜 C++ 상자 함수(ALootCrateActor::PlaceOnFloor)를 안 쓰나: 목록의 상자는 부모가 AInteractActor 면 무엇이든 되고,
// 팩 BP 는 몸통이 MeshComp 가 아니라 따로 단 부품일 수 있어서 상자 전체 크기로 잰다.
void UMapItemSpawner::PlaceOnFloor(AActor* Container, const FVector& FloorPoint)
{
	Container->SetActorLocation(FloorPoint);
	const FBox Bounds = Container->GetComponentsBoundingBox(true);
	if (Bounds.IsValid)
		Container->AddActorWorldOffset(FVector(0.0f, 0.0f, FloorPoint.Z - Bounds.Min.Z + 1.0f));
}

AInteractActor* UMapItemSpawner::SpawnContainer(UClass* ContainerClass, const FVector& FloorPoint, const FRotator& Rotation)
{
	// 주인(Owner)은 두지 않는다: F 상호작용(UInteractComponent)은 맞은 물체에 주인이 있으면 주인을 열려고 해서,
	// 맵을 주인으로 두면 상자를 맞혀도 맵을 열려다 아무 일도 안 일어난다.
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AInteractActor* Container = GetWorld()->SpawnActor<AInteractActor>(ContainerClass, FloorPoint, Rotation, SpawnParameters);
	if (!IsValid(Container))
		return nullptr;
	Container->Tags.AddUnique(MapLootContainerTag);

	// 팩에서 가져온 상자 BP 는 부품(뚜껑 등)이 스태틱으로 저장돼 있을 수 있다.
	// 스태틱이면 게임 중에 못 움직여서 바닥 맞추기가 무시되고, 상자가 떠 있거나 바닥에 박힌다 → 전부 무버블로.
	// 길찾기 그물은 안 바꾼다: 상자 수십 개가 판 시작 때 길찾기를 다시 굽게 만들면 멈칫한다.
	TInlineComponentArray<USceneComponent*> SceneComponents(Container);
	for (USceneComponent* Component : SceneComponents)
	{
		Component->SetMobility(EComponentMobility::Movable);
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			Primitive->SetCanEverAffectNavigation(false);
	}
	PlaceOnFloor(Container, FloorPoint);
#if WITH_EDITOR
	Container->SetFolderPath(TEXT("RuntimeDesign/Items"));
#endif
	SpawnedContainers.Add(Container);
	return Container;
}

void UMapItemSpawner::SpawnTestContainerInFront(const FVector& SeatLocation, float Yaw)
{
#if WITH_EDITOR
	// 에디터에서 플레이(PIE)할 때만. 에디터 실행 파일로 -game 을 돌릴 때도 안 놓는다.
	if (GetWorld()->WorldType != EWorldType::PIE || GetWorld()->GetNetMode() == NM_Client || ContainerClasses.IsEmpty())
		return;

	// 바라보는 쪽 2.5m 앞. 시작 구역 담당이 트인 쪽(입구)을 보게 세우므로 벽 속이 아니다.
	const FVector Front = SeatLocation + FRotator(0.0f, Yaw, 0.0f).Vector() * 250.0f;
	FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MapTestContainerFloor), false);
	FloorQuery.AddIgnoredActors(SpawnedContainers);
	FHitResult Hit;
	const bool bFloor = GetWorld()->LineTraceSingleByChannel(
		Hit, Front + FVector(0.0f, 0.0f, 150.0f), Front - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, FloorQuery);
	const FVector FloorPoint = bFloor ? FVector(Hit.ImpactPoint) : Front - FVector(0.0f, 0.0f, 120.0f);

	// 상자 종류는 돌아가며: 플레이어마다 다른 상자를 열어 볼 수 있게.
	UClass* ContainerClass = ContainerClasses[TestContainerCount % ContainerClasses.Num()];
	++TestContainerCount;
	// 상자가 나를 보게 돌린다(뚜껑 앞면이 보이게).
	if (const AInteractActor* Container = SpawnContainer(ContainerClass, FloorPoint, FRotator(0.0f, Yaw + 180.0f, 0.0f)))
	{
		UE_LOG(LogTemp, Display, TEXT("Loot spawn: test container %s placed 2.5m in front of a start seat (PIE only) floor=%d"),
			*Container->GetName(), bFloor);
	}
#endif
}

// 상자 자리마다 상자 놓기.
// ① 상자 BP 목록(DA_MapAssets 의 LootContainerClasses)을 읽는다. 못 읽는 칸은 빼고 로그로 알린다.
// ② 상자 자리마다: 그 자리 씨앗으로 주사위 → 목록에서 상자 하나를 고르고, 자리 바로 아래 바닥에 놓는다.
//    아래로 선을 쏴서 바닥(창고 2층이면 2층 바닥)에 내려놓는다 — 벽 속이 아니라 손 닿는 바닥이어야
//    형님 서버의 "거리·시야 검사" 를 통과해 열 수 있다.
// ③ 상자 안은 채우지 않는다. 안에 무엇이 들지는 형님 쪽(AInteractActor)이 정한다.
// ④ 결과를 지문(loot_hash)으로 남긴다. 같은 시드면 같은 값이어야 한다(검사 기준).
void UMapItemSpawner::SpawnLootOnce()
{
	if (bSpawned || !Map->bResolvedGameplayPointSafety)
		return;
	bSpawned = true;
	// 서버만 놓는다. 들어온 사람은 서버가 놓은 상자를 복제로 받는다(여기서 또 놓으면 두 벌이 된다).
	// 확인용: 몇 초 뒤 받은 상자 수와 그 안의 아이템 수를 로그에 남긴다(가까운 것만 오므로 서버 수보다 적을 수 있다).
	if (GetWorld()->GetNetMode() == NM_Client)
	{
		FTimerHandle CountTimer;
		TWeakObjectPtr<UWorld> WeakWorld(GetWorld());
		GetWorld()->GetTimerManager().SetTimer(CountTimer, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (!WeakWorld.IsValid())
				return;
			int32 ContainerCount = 0, Filled = 0, ItemsInside = 0;
			for (TActorIterator<AInteractActor> It(WeakWorld.Get()); It; ++It)
			{
				if (!It->Tags.Contains(MapLootContainerTag))
					continue;
				++ContainerCount;
				// 안 아이템 수: 형님 인벤토리 복제(OnRep)가 채워 준 이 상자 칸(MyActorGuid)의 아이템 수.
				const int32 Inside = It->InventoryComp && It->MyActorGuid.IsValid()
					? It->InventoryComp->GetItems(It->MyActorGuid).Num() : 0;
				ItemsInside += Inside;
				Filled += Inside > 0 ? 1 : 0;
			}
			UE_LOG(LogTemp, Display, TEXT("Loot spawn: client received containers=%d with_contents=%d items_inside=%d"),
				ContainerCount, Filled, ItemsInside);
		}), 5.0f, false);
		return;
	}

	// ① 상자 BP 목록.
	const UMapAssetSet& Assets = Map->GetMapAssets();
	ContainerClasses.Reset();
	TArray<FString> MissingClasses;
	for (const TSoftClassPtr<AInteractActor>& Entry : Assets.LootContainerClasses)
	{
		if (UClass* Class = Entry.LoadSynchronous())
			ContainerClasses.Add(Class);
		else
			MissingClasses.Add(Entry.ToString());
	}
	if (!MissingClasses.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("Loot spawn: %d container BP(s) not found - skipped [%s]"),
			MissingClasses.Num(), *FString::Join(MissingClasses, TEXT(", ")));
	}
	if (ContainerClasses.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Loot spawn: no container BP in DA_MapAssets LootContainerClasses - no containers placed"));
		return;
	}

	// 맵 액터는 빼면 안 된다: 타일 바닥(묶음 HISM)이 맵 액터의 부품이라, 빼면 들판 바닥을 못 찾는다.
	FCollisionQueryParams FloorQuery(SCENE_QUERY_STAT(MapItemFloor), false);

	int32 LootPointCount = 0;
	// 정해 둔 바닥 높이 ±60cm 안에서 바닥을 못 찾은 수(그때는 정해 둔 높이에 그대로 놓는다).
	int32 NoFloorCount = 0;
	TArray<FString> FloorSamples;
	int32 ContainerCount = 0;
	TMap<FString, int32> CountByClass;
	uint32 LootHash = 0;

	// 지점은 바닥보다 120cm 위에 찍혀 있다 → 정해 둔 바닥 = 지점 - 120cm.
	// 그 높이 ±60cm 안에서만 바닥을 찾는다(바닥이 살짝 기울거나 턱이 있는 만큼만 맞춘다).
	// 왜 끝까지 안 쏘나: 그러면 2층 바닥이 눈 선에 안 걸리는 건물에서 1층까지, 호수 칸에서 숨은 호수 바닥까지 떨어졌다.
	auto FindFloor = [&](const FLevelDesignPoint& Point)
	{
		const FVector Designed = Point.WorldLocation - FVector(0.0f, 0.0f, 120.0f);
		FHitResult Hit;
		const bool bFloor = GetWorld()->LineTraceSingleByChannel(
			Hit, Designed + FVector(0.0f, 0.0f, 60.0f), Designed - FVector(0.0f, 0.0f, 60.0f), ECC_Visibility, FloorQuery);
		if (!bFloor)
		{
			++NoFloorCount;
			if (FloorSamples.Num() < 6)
				FloorSamples.Add(Point.PointId.ToString());
		}
		return bFloor ? FVector(Hit.ImpactPoint) : Designed;
	};

	for (const FLevelDesignPoint& Point : Map->LevelDesignPoints)
	{
		if (Point.Type != ELevelDesignPointType::Loot)
			continue;
		++LootPointCount;

		// ② 자리 씨앗으로만 굴린다 → 시각·순서와 상관없이 같은 시드면 같은 자리에 같은 상자.
		// 상자 종류: 씨앗을 한 번 섞어(CRC) 만든 주사위. 자리 씨앗끼리 비슷해서 그대로 쓰면 한쪽으로 쏠렸다.
		FRandomStream ChoiceStream(static_cast<int32>(FCrc::MemCrc32(&Point.PointSeed, sizeof(Point.PointSeed))));
		UClass* ContainerClass = ContainerClasses[ChoiceStream.RandRange(0, ContainerClasses.Num() - 1)];
		// 돌림 방향: 자리 씨앗(64비트)을 32비트로 접어 만든 주사위.
		FRandomStream Stream(static_cast<int32>(Point.PointSeed ^ (Point.PointSeed >> 32)));
		const FRotator Rotation(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f);

		const FVector FloorPoint = FindFloor(Point);
		AInteractActor* Container = SpawnContainer(ContainerClass, FloorPoint, Rotation);
		if (!Container)
			continue;
		FloorQuery.AddIgnoredActor(Container);
		++ContainerCount;
		++CountByClass.FindOrAdd(ContainerClass->GetName());

		// ④ 지문: 어느 자리에, 어떤 상자가, 어디에.
		const FVector Placed = Container->GetActorLocation();
		LootHash = HashCombine(LootHash, FCrc::StrCrc32(*Point.PointId.ToString()));
		LootHash = HashCombine(LootHash, FCrc::StrCrc32(*ContainerClass->GetName()));
		LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.X)));
		LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.Y)));
		LootHash = HashCombine(LootHash, GetTypeHash(FMath::RoundToInt(Placed.Z)));
	}

	TArray<FString> ClassSummary;
	for (const TPair<FString, int32>& Pair : CountByClass)
		ClassSummary.Add(FString::Printf(TEXT("%s=%d"), *Pair.Key, Pair.Value));
	UE_LOG(LogTemp, Display,
		TEXT("Loot spawn: loot_points=%d containers=%d container_types=%d no_floor=%d loot_hash=%08X by_type=[%s] no_floor_sample=[%s]"),
		LootPointCount, ContainerCount, ContainerClasses.Num(), NoFloorCount, LootHash,
		*FString::Join(ClassSummary, TEXT(" ")), *FString::Join(FloorSamples, TEXT(" ")));
}
