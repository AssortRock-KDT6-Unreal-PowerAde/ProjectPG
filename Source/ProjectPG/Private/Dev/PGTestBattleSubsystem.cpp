// 타일 맵에서 몬스터와 싸워 보는 시험 도구. 쓰는 법은 PGTestBattleSubsystem.h 맨 위.

#include "Dev/PGTestBattleSubsystem.h"
#include "Actors/MapBuilder.h"
#include "Common/GameData.h"
#include "Common/TableData.h"
#include "Components/InventoryComponent.h"
#include "Core/TableSubSystem.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "NavigationSystem.h"
#include "NavigationInvokerComponent.h"
#include "TimerManager.h"

namespace PGTestBattle
{
	// 몬스터를 놓을 거리(cm). 감지 거리(EnemyTable 2500) 안이라 돌아보면 바로 보이고, 화면에서도 찾기 쉽게 가깝게.
	constexpr float MinDistance = 1500.f;
	constexpr float MaxDistance = 2200.f;
	// 길(네비) 위 자리를 찾을 때 위아래·옆으로 얼마나 넓게 볼지. 맵 검사기(MapVerifier)와 같은 값.
	const FVector NavSearchExtent(300.f, 300.f, 650.f);
	// 몬스터 몸 절반 높이만큼 띄워 놓는다(바닥에 박히지 않게).
	constexpr float SpawnLift = 100.f;
	// 맵 준비 뒤 플레이어를 시작 자리에 옮길 틈(초). MapBuilder 는 0.1초마다 옮기고, 몸이 없으면 2초 뒤 다시 만든다.
	constexpr double PlaceGraceSeconds = 3.0;
	// 기다리는 일의 시간 제한(초). 넘으면 Error 로그를 남기고 그 일은 그만둔다.
	constexpr double MapReadyTimeout = 240.0;
	constexpr int32 MaxNavMisses = 60;  // 0.5초 × 60 = 30초 동안 길이 안 생기면 그 사람 몫은 포기
	constexpr float CheckInterval = 0.5f;
	// 플레이어 둘레에 길을 만드는 반경(cm). 몬스터 놓는 거리(최대 28m)와 다가오는 길까지 덮게 60m, 80m 밖이면 지움.
	constexpr float NavGenerateRadius = 6000.f;
	constexpr float NavRemoveRadius = 8000.f;
}

bool UPGTestBattleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
#if UE_BUILD_SHIPPING
	return false;
#else
	// 명령줄에 시험 옵션이 있을 때만 만든다 → 평소 게임에는 이 부품이 아예 없다.
	FString Unused;
	return Super::ShouldCreateSubsystem(Outer)
		&& (FParse::Value(FCommandLine::Get(), TEXT("PGTestEnemies="), Unused) || FParse::Value(FCommandLine::Get(), TEXT("PGTestRifle="), Unused));
#endif
}

void UPGTestBattleSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// 서버만 몬스터를 만들고 짐을 정한다. 클라이언트는 복제로 받는다.
	if (!InWorld.IsGameWorld() || InWorld.GetNetMode() == NM_Client) return;

	FParse::Value(FCommandLine::Get(), TEXT("PGTestEnemies="), EnemiesPerPlayer);
	FParse::Value(FCommandLine::Get(), TEXT("PGTestEnemyPlayers="), MaxPlayersWithEnemies);
	FString Text;
	if (FParse::Value(FCommandLine::Get(), TEXT("PGTestRifle="), Text)) RifleItemID = FName(*Text);
	if (EnemiesPerPlayer > 0)
	{
		FString ClassPath;
		FParse::Value(FCommandLine::Get(), TEXT("PGTestEnemyClass="), ClassPath);
		EnemyClass = ClassPath.IsEmpty() ? nullptr : LoadClass<APawn>(nullptr, *ClassPath);
		if (!EnemyClass)
		{
			UE_LOG(LogTemp, Error, TEXT("[PGTest] -PGTestEnemyClass='%s' 로 몬스터 클래스를 못 찾아 몬스터는 안 놓음"), *ClassPath);
			EnemiesPerPlayer = 0;
		}
	}
	StartTime = InWorld.GetTimeSeconds();
	// 끝나는 때가 없는 이유: 시험 중에 사람이 늦게 들어와도 총·몬스터를 받게. 할 일이 없으면 확인 한 번 하고 바로 끝난다.
	InWorld.GetTimerManager().SetTimer(CheckTimer, this, &ThisClass::Check, PGTestBattle::CheckInterval, true);
	UE_LOG(LogTemp, Display, TEXT("[PGTest] 시험 전투 켬: 사람마다 몬스터 %d마리, 총 '%s'"), EnemiesPerPlayer, *RifleItemID.ToString());
}

void UPGTestBattleSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld()) World->GetTimerManager().ClearTimer(CheckTimer);
	Super::Deinitialize();
}

void UPGTestBattleSubsystem::Check()
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		if (APlayerController* Controller = It->Get(); Controller && !RifleItemID.IsNone() && !RifleGiven.Contains(Controller))
			GiveRifle(Controller);

	if (EnemiesPerPlayer <= 0 || bGaveUpWaitingForMap) return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		if (APlayerController* Controller = It->Get(); Controller && Controller->GetPawn())
			AddNavInvoker(Controller->GetPawn());
	TActorIterator<AMapBuilder> MapIt(World);
	const AMapBuilder* Map = MapIt ? *MapIt : nullptr;
	if (!Map || !Map->IsGameplayReady())
	{
		if (Now - StartTime > PGTestBattle::MapReadyTimeout)
		{
			UE_LOG(LogTemp, Error, TEXT("[PGTest] %.0f초가 지나도 %s → 몬스터는 안 놓음"), PGTestBattle::MapReadyTimeout,
				Map ? TEXT("맵 준비(시설 레벨·끼임 정리)가 안 끝남") : TEXT("MapBuilder 가 없음(타일 맵이 아님)"));
			bGaveUpWaitingForMap = true;
		}
		return;
	}
	if (MapReadyTime < 0.0) MapReadyTime = Now;
	if (Now - MapReadyTime < PGTestBattle::PlaceGraceSeconds) return;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		if (APlayerController* Controller = It->Get(); Controller && Controller->GetPawn() && !EnemiesPlaced.Contains(Controller)
			&& EnemiesPlaced.Num() < MaxPlayersWithEnemies && SpawnEnemiesAround(Controller))
			EnemiesPlaced.Add(Controller);
}

void UPGTestBattleSubsystem::GiveRifle(APlayerController* Controller)
{
	APlayerState* State = Controller->PlayerState;
	UInventoryComponent* Inventory = State ? State->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inventory) return;  // 플레이어 상태가 아직 안 생김 → 다음 확인 때 다시
	RifleGiven.Add(Controller);  // 아래는 성공이든 실패든 한 번만 한다(실패는 다시 해도 같음)

	if (!Inventory->IsServerManaged())
	{
		if (!bWarnedNotServerManaged)
			UE_LOG(LogTemp, Error, TEXT("[PGTest] 서버가 짐을 관리하지 않는 게임모드라 총을 못 줌 → 서버를 ?game=GM_InGame 으로 띄울 것"));
		bWarnedNotServerManaged = true;
		return;
	}
	if (Inventory->HasInitialInventory())
	{
		UE_LOG(LogTemp, Warning, TEXT("[PGTest] %s: 이미 짐이 있어 총을 안 넣음(덮어쓰지 않음)"), *GetNameSafe(Controller));
		return;
	}
	const UTableSubSystem* Tables = UTableSubSystem::Get(this);
	const FItemTableRow* Item = Tables ? Tables->FindTableRow<FItemTableRow>(TEXT("ItemTable"), RifleItemID) : nullptr;
	if (!Item || Item->EquipSlotType == EEquipSlot::MAX)
	{
		UE_LOG(LogTemp, Error, TEXT("[PGTest] ItemTable 에 '%s' 가 없거나 장착 칸이 없는 아이템이라 총을 못 줌"), *RifleItemID.ToString());
		return;
	}

	// 짐을 서버가 처음 한 번 정한다: 빈 주머니·창고 + 총을 든 무기 칸.
	// 이렇게 정하면 클라이언트가 나중에 보내는 짐(웹 서버가 없어 보통 안 옴)은 무시된다.
	FInventorySnapshot Snapshot;
	Snapshot.bInitialized = true;
	Snapshot.PocketGuid = FGuid::NewGuid();
	Snapshot.StashGuid = FGuid::NewGuid();
	FInventoryContainerSnapshot Pocket;
	Pocket.Guid = Snapshot.PocketGuid;
	Pocket.Size = FIntPoint(5, 4);
	FInventoryContainerSnapshot Stash;
	Stash.Guid = Snapshot.StashGuid;
	Stash.Size = FIntPoint(10, 10);
	FInventoryContainerSnapshot WeaponSlot;
	WeaponSlot.Guid = FGuid::NewGuid();
	WeaponSlot.Size = FIntPoint(1, 1);
	WeaponSlot.EquipSlot = Item->EquipSlotType;
	FItemInstance Rifle;
	Rifle.GUID = FGuid::NewGuid();
	Rifle.inventory_guid = Rifle.GUID;
	Rifle.ItemID = RifleItemID;
	Rifle.parent_inventory_guid = WeaponSlot.Guid;
	Rifle.Position = FIntPoint(0, 0);
	Rifle.StackCount = 1;
	Rifle.type = Item->ItemType;
	Rifle.Owner = State;
	WeaponSlot.Items.Add(Rifle);
	Snapshot.Containers = { Pocket, Stash, WeaponSlot };

	FString Error;
	if (!Inventory->InitializeFromSnapshot(Snapshot, &Error))
	{
		UE_LOG(LogTemp, Error, TEXT("[PGTest] %s: 총 넣기 거절됨 (%s)"), *GetNameSafe(Controller), *Error);
		return;
	}
	State->ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[PGTest] %s: 총 %s 를 장착 칸 %d 에 넣음"), *GetNameSafe(Controller), *RifleItemID.ToString(), static_cast<int32>(Item->EquipSlotType));
}

void UPGTestBattleSubsystem::AddNavInvoker(APawn* Pawn)
{
	if (Pawn->FindComponentByClass<UNavigationInvokerComponent>()) return;
	// 서버에서만 붙인다(길은 서버의 AI 만 쓴다). 몸이 죽어 새로 생기면 새 몸에 다시 붙는다.
	UNavigationInvokerComponent* Invoker = NewObject<UNavigationInvokerComponent>(Pawn, TEXT("PGTestNavInvoker"));
	Invoker->SetGenerationRadii(PGTestBattle::NavGenerateRadius, PGTestBattle::NavRemoveRadius);
	Invoker->RegisterComponent();
	UE_LOG(LogTemp, Display, TEXT("[PGTest] %s 둘레 %.0fm 에 길(네비)을 만들게 함"), *GetNameSafe(Pawn), PGTestBattle::NavGenerateRadius / 100.f);
}

bool UPGTestBattleSubsystem::SpawnEnemiesAround(APlayerController* Controller)
{
	UWorld* World = GetWorld();
	const APawn* Player = Controller->GetPawn();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const FVector Center = Player->GetActorLocation();

	// 둘레를 고르게 나눠 자리를 찾는다. 자리마다 5번까지 조금씩 옮겨 가며 길 위 점을 찾는다.
	TArray<FVector> Spots;
	for (int32 Index = 0; Nav && Index < EnemiesPerPlayer; ++Index)
		for (int32 Try = 0; Try < 5; ++Try)
		{
			const float Yaw = 360.f * Index / EnemiesPerPlayer + FMath::FRandRange(-25.f, 25.f);
			const FVector Wanted = Center + FRotator(0.f, Yaw, 0.f).Vector() * FMath::FRandRange(PGTestBattle::MinDistance, PGTestBattle::MaxDistance);
			FNavLocation OnNav;
			if (Nav->ProjectPointToNavigation(Wanted, OnNav, PGTestBattle::NavSearchExtent)) { Spots.Add(OnNav.Location); break; }
		}

	if (Spots.Num() < EnemiesPerPlayer)
	{
		// 길이 아직 덜 만들어졌다(플레이어 둘레 길은 붙인 뒤 몇 초 걸림) → 다음 확인 때 다시.
		// - 30초가 지나도 모자라면 → 찾은 자리에만 놓음 / 하나도 없으면 → Error 로그 후 포기
		int32& Misses = NavMisses.FindOrAdd(Controller);
		if (++Misses < PGTestBattle::MaxNavMisses) return false;
		UE_LOG(LogTemp, Error, TEXT("[PGTest] %s 둘레 %.0f~%.0fm 에서 %d초 동안 길(네비) 위 자리를 %d/%d 개만 찾음"),
			*GetNameSafe(Controller), PGTestBattle::MinDistance / 100.f, PGTestBattle::MaxDistance / 100.f, PGTestBattle::MaxNavMisses / 2, Spots.Num(), EnemiesPerPlayer);
		if (Spots.IsEmpty()) return true;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	for (int32 Index = 0; Index < Spots.Num(); ++Index)
	{
		// 모두 등을 돌린다. 왜: 한 마리가 플레이어를 보게 두었더니, 게임 창이 아직 불러오는 중에 쏴서 조작도 못 하고 죽었다.
		// 플레이어가 먼저 쏘면 총소리를 듣고 돌아본다 → 처음 본 몬스터가 외침 → 나머지가 듣고 온다.
		const FVector ToPlayer = (Center - Spots[Index]).GetSafeNormal2D();
		const FRotator Facing = (-ToPlayer).Rotation();
		APawn* Enemy = World->SpawnActor<APawn>(EnemyClass, Spots[Index] + FVector(0.f, 0.f, PGTestBattle::SpawnLift), Facing, Params);
		UE_LOG(LogTemp, Display, TEXT("[PGTest] %s 둘레에 몬스터 %s 놓음 (%d/%d, 거리 %.0fm, %s)"), *GetNameSafe(Controller), *GetNameSafe(Enemy),
			Index + 1, EnemiesPerPlayer, FVector::Dist2D(Center, Spots[Index]) / 100.f, TEXT("등 돌림"));
	}
	return true;
}
