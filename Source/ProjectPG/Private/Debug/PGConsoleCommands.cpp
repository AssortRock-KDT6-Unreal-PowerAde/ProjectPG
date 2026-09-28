// 게임플레이 디버그 콘솔 명령 모음 (PG.*). PIE 콘솔에서 친다. 전부 서버(리슨/PIE) 기준.
//
//  PG.ObjectSmokeTest          오브젝트 원형 스모크 테스트
//  PG.SpawnObjectsFromPoints   현재 맵 Loot/Exit/Quest 포인트에 카탈로그 오브젝트 생성
//  PG.ShowAllObjects           카탈로그 전부 플레이어 앞에 격자로
//  PG.SpawnMonster [preset]    PG.SpawnRobot [boss|ride] [scale]   PG.SpawnVehicle [preset]
//  PG.GiveItem <ItemId> [n]    PG.WeaponTune fp|hand x y z p y r [scale]
//  PG.ItemValues [shopSeed]    아이템 기본가치·등급 표와 루팅 테이블 기대가치
//  PG.ConvertLevelDoors        PG.SpawnCombatFromPoints
//  PG.GoTo boss|robot|vehicle|monster|box|item|booth|exchange     PG.ShowFactions [sec]
//  PG.SpawnBattleship [scale] [height m]           PG.Finale.Start [warn sec]   PG.Finale.Status   PG.Finale.Board   PG.Finale.Bridge   PG.Finale.Launch
//  PG.Flow.Title / Lobby / Game / Scoreboard / Finish extract|die|timeout / Status / StashAdd / StashClear   (Private/Flow/PGRunSubsystem.cpp)
// 모듈 파일(ProjectPG.cpp)에는 에디터 전용 빌드 도구만 남긴다.
#include "Actors/ItemContainerActor.h"
#include "PhysicsEngine/BodySetup.h"
#include "Common/PGVisualSettings.h"
#include "LevelDesign/PGMapInfo.h"
#include "Combat/PGCombatSpawner.h"
#include "Combat/PGCombatSettings.h"
#include "DrawDebugHelpers.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Finale/PGBattleshipActor.h"
#include "Finale/PGFinaleDirector.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameModes/GameModePG.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Monster/PGMonsterCharacter.h"
#include "Objects/PGFloorItemActor.h"
#include "TimerManager.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGItemValue.h"
#include "Objects/PGLevelDoorConverter.h"
#include "Objects/PGObjectSmokeTest.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGBoothActor.h"
#include "Objects/PGWigBeamComponent.h"
#include "Objects/PGWearableColors.h"
#include "Objects/PGWearableComponent.h"
#include "Robot/PGRobotCharacter.h"
#include "Vehicle/PGTankPawn.h"
#include "Vehicle/PGVehiclePawn.h"
#include "Vehicle/PGFlightKitComponent.h"
#include "Finale/PGMissileSubsystem.h"
#include "GameFramework/DamageType.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Weapons/PGWeaponComponent.h"
#include "Objects/PGTransformNPCActor.h"
#include "Combat/PGMonsterLookComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/LevelStreamingDynamic.h"
#include "GameplayAbilities/CharacterAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"

namespace ProjectPGObjects
{
	// 오브젝트 원형 스모크 테스트. PIE 콘솔에서 PG.ObjectSmokeTest 로 실행하거나,
	// 에디터 없이 -game -nullrhi -PGObjectSmokeTest 로 실행한다(APGObjectTestGameMode 참조).
	static FAutoConsoleCommandWithWorld ObjectSmokeTestCommand(
		TEXT("PG.ObjectSmokeTest"),
		TEXT("Spawns one of every object archetype and exercises it; logs pass=true/false per case."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&PGObjectSmokeTest::Run));

	// 현재 맵의 레벨 디자인 포인트(Loot/Exit/Quest)에 카탈로그 오브젝트를 생성한다.
	static void SpawnObjectsFromPoints(UWorld* World)
	{
		if (!IsValid(World))
			return;
		const IPGMapInfo* Preview = UPGMapInfoSubsystem::FindMap(World);
		UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(World);
		if (!Preview || !Spawner)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnObjectsFromPoints: preview or spawner missing"));
			return;
		}
		if (Spawner->GetCatalogCount() == 0)
			PGObjectSmokeTest::RegisterDefaultCatalog(*Spawner);
		const AGameModePG* GameMode = World->GetAuthGameMode<AGameModePG>();
		const int64 Seed = IsValid(GameMode) ? GameMode->GetMapGenerationSeed() : 0;
		Spawner->SpawnFromLevelDesignPoints(Preview->GetLevelDesignPoints(), Seed);
	}

	static FAutoConsoleCommandWithWorld SpawnObjectsFromPointsCommand(
		TEXT("PG.SpawnObjectsFromPoints"),
		TEXT("Spawns catalog objects on the current map's Loot/Exit/Quest level design points using the map seed."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnObjectsFromPoints));

	// 맵이 600x600m라 스폰된 오브젝트를 걸어서 찾기 어렵다. 카탈로그에서 켜져 있는 행을
	// 전부 플레이어 앞에 격자로 늘어놓아 한자리에서 눈으로 확인하기 위한 디버그 명령이다.
	// 메시가 안 붙은 행은 로그에 mesh=NONE 으로 남는다(경로가 틀려도 에러가 안 나기 때문).
	static void ShowAllObjects(UWorld* World)
	{
		if (!IsValid(World))
			return;

		UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(World);
		if (!Spawner)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.ShowAllObjects: spawner missing"));
			return;
		}
		if (Spawner->GetCatalogCount() == 0)
			PGObjectSmokeTest::RegisterDefaultCatalog(*Spawner);

		const APlayerController* PC = World->GetFirstPlayerController();
		const APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.ShowAllObjects: player pawn missing (PIE에서 실행할 것)"));
			return;
		}

		const FVector Forward = Pawn->GetActorForwardVector();
		const FVector Right = Pawn->GetActorRightVector();
		// 발밑 높이에서 시작한다. 캡슐 중심을 쓰면 오브젝트가 공중에 뜬다.
		FVector Origin = Pawn->GetActorLocation() + Forward * 500.0f;
		Origin.Z -= Pawn->GetSimpleCollisionHalfHeight();

		TArray<FPGObjectCatalogRow> Rows;
		Spawner->GetAllCatalogRows(Rows);
		Rows.Sort([](const FPGObjectCatalogRow& A, const FPGObjectCatalogRow& B)
		{
			return A.ObjectId.Compare(B.ObjectId) < 0;
		});

		const float Spacing = 250.0f;
		const int32 PerRow = 10;
		int32 Placed = 0;
		int32 Spawned = 0;
		int32 MissingMesh = 0;

		for (const FPGObjectCatalogRow& Row : Rows)
		{
			if (!Row.bIncluded)
				continue;
			// 시체는 캐릭터에 붙는 UPGLootableComponent 로 구현돼 스폰 대상이 아니다.
			// 걸러내지 않으면 actor=FAIL 로그가 남아 진짜 실패와 헷갈린다.
			if (Row.Archetype == EPGObjectArchetype::Corpse)
				continue;

			const int32 Column = Placed % PerRow;
			const int32 Line = Placed / PerRow;
			const FVector Location = Origin
				+ Right * ((Column - PerRow / 2) * Spacing)
				+ Forward * (Line * Spacing);

			AActor* Actor = Spawner->SpawnFromCatalog(Row.ObjectId, FTransform(Location), 1);
			const bool bHasMesh = !Row.Mesh.IsNull();
			if (!bHasMesh)
				++MissingMesh;
			if (IsValid(Actor))
			{
				++Spawned;
				// 격자가 25m×15m 라 작은 바닥 아이템은 눈으로 찾기 어렵다. 머리 위 글자 + 아웃라이너 라벨로 ID 검색이 되게 한다.
				// (아웃라이너는 액터 라벨만 검색한다. 메시 이름으로는 안 나온다.)
				const FString Tag = FString::Printf(TEXT("%s %s"), *Row.ObjectId.ToString(), *Row.DisplayName.ToString());
				DrawDebugString(World, Location + FVector(0.0f, 0.0f, 150.0f), Tag, nullptr, bHasMesh ? FColor::White : FColor::Red, 120.0f, true);
#if WITH_EDITOR
				Actor->SetActorLabel(FString::Printf(TEXT("%s_%s"), *Row.ObjectId.ToString(), *Row.DisplayName.ToString()));
#endif
			}

			UE_LOG(LogPGObjects, Display, TEXT("ShowAllObjects: %s %s mesh=%s actor=%s row=%d col=%d"),
				*Row.ObjectId.ToString(),
				*Row.DisplayName.ToString(),
				bHasMesh ? TEXT("set") : TEXT("NONE"),
				IsValid(Actor) ? TEXT("ok") : TEXT("FAIL"),
				Line, Column);

			++Placed;
		}

		UE_LOG(LogPGObjects, Display, TEXT("PG.ShowAllObjects: placed=%d spawned=%d mesh_missing=%d"),
			Placed, Spawned, MissingMesh);
	}

	// 겹친 상호작용 대상(휠 선택) 확인용: 플레이어 1.5m 앞 바닥에 아이템 여러 개를 30cm 안에 뭉쳐 놓는다.
	// 실제 맵에서는 상자 옆 바닥 루팅이 이렇게 뭉치는데, 그걸 찾아다니지 않고 바로 보려고 만든 명령.
	static void SpawnItemPile(UWorld* World)
	{
		const APlayerController* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
		const APawn* PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(PlayerPawn))
			return;
		FVector Center = PlayerPawn->GetActorLocation() + PlayerPawn->GetActorForwardVector() * 150.0f;
		Center.Z -= PlayerPawn->GetSimpleCollisionHalfHeight();
		const FName Items[] = { TEXT("Shirt_Red"), TEXT("Pants_Black"), TEXT("Armor_Vest_Olive"), TEXT("Helmet_Black"), TEXT("ChestPouch"), TEXT("Holster_Black"), TEXT("Backpack_Olive"), TEXT("Shirt_Olive"), TEXT("Pistol"), TEXT("Holster"), TEXT("Ammo_Pistol"), TEXT("Revolver"), TEXT("Rifle_AK"), TEXT("Ammo_Rifle"), TEXT("Shotgun"), TEXT("Ammo_Shotgun"), TEXT("Fuel"), TEXT("Key_Common") };
		const FVector Right = PlayerPawn->GetActorRightVector();
		const FVector Forward = PlayerPawn->GetActorForwardVector();
		int32 Index = 0;
		for (const FName& ItemId : Items)
		{
			// 4열로 45cm 간격. 가까운 것끼리는 후보 반경(90cm) 안에 겹쳐 들어와 휠로 고를 수 있다.
			const FVector Location = Center + Right * ((Index % 4) * 45.0f - 68.0f) + Forward * ((Index / 4) * 60.0f);
			// 탄약은 한 발씩 주면 쏴 볼 수가 없다. 한 묶음(30발)으로.
			const int32 Count = ItemId.ToString().StartsWith(TEXT("Ammo_")) ? 30 : 1;
			APGFloorItemActor::SpawnDrop(World, ItemId, Count, FTransform(FRotator(0.0f, Index * 70.0f, 0.0f), Location));
			++Index;
		}
		UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnItemPile: %d items at %s (look down, scroll the wheel, press F)"), Index, *Center.ToCompactString());
	}

	// 착장 파츠 위치 맞추기: PG.WearTune <Slot> x y z [pitch yaw roll] [scale]
	// 좌표는 몸 메시 공간(Z 위, -Y 등 뒤, 발바닥이 0). 바로 반영되고, 표(PGWearableColors.cpp)에 붙여 넣을 줄이 로그에 찍힌다.
	static void WearTune(const TArray<FString>& Args, UWorld* World)
	{
		const APlayerController* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
		const APawn* PlayerPawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		UPGWearableComponent* Wear = IsValid(PlayerPawn) ? PlayerPawn->FindComponentByClass<UPGWearableComponent>() : nullptr;
		const int64 SlotValue = Args.Num() > 0 ? StaticEnum<EPGWearSlot>()->GetValueByNameString(Args[0]) : INDEX_NONE;
		if (!Wear || SlotValue == INDEX_NONE || Args.Num() < 4)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.WearTune <Cap|Backpack> x y z [pitch yaw roll] [scale]  (body space: Z up, -Y back)"));
			return;
		}
		auto Num = [&Args](int32 I, float Default) { return Args.IsValidIndex(I) ? FCString::Atof(*Args[I]) : Default; };
		const FTransform Offset(FRotator(Num(4, 0.0f), Num(5, 0.0f), Num(6, 0.0f)), FVector(Num(1, 0.0f), Num(2, 0.0f), Num(3, 0.0f)), FVector(Num(7, 1.0f)));
		Wear->TuneSlot(static_cast<EPGWearSlot>(SlotValue), Offset);
	}

	static FAutoConsoleCommandWithWorldAndArgs WearTuneCommand(
		TEXT("PG.WearTune"),
		TEXT("PG.WearTune <Cap|Backpack> x y z [pitch yaw roll] [scale] - move a bone-attached wearable in body space and log the table line."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WearTune));

	static FAutoConsoleCommandWithWorld SpawnItemPileCommand(
		TEXT("PG.SpawnItemPile"),
		TEXT("Drops four clothing items in a tight pile in front of the player to try wheel target selection."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&SpawnItemPile));

	static FAutoConsoleCommandWithWorld ShowAllObjectsCommand(
		TEXT("PG.ShowAllObjects"),
		TEXT("Spawns every included catalog row in a grid in front of the player so they can be inspected in one place."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&ShowAllObjects));
}

// 무기·몬스터·로봇 프로토타입 검증용 콘솔 명령. 넓은 맵에서 걸어가 찾지 않고 눈앞에 바로 만든다.
namespace ProjectPGCombat
{
	static APawn* GetPlayerPawn(UWorld* World)
	{
		const APlayerController* PC = IsValid(World) ? World->GetFirstPlayerController() : nullptr;
		return IsValid(PC) ? PC->GetPawn() : nullptr;
	}

	// 멀티(9/27): 명령 끝의 숫자로 몇 번째 사람(접속 순서, 0부터)인지 고른다. 없으면 첫 사람.
	//   전용 서버에는 "첫 플레이어" 가 곧 첫 접속자라, 두 사람을 서로 다른 곳에 두는 시험을 못 했다.
	static APawn* GetPlayerPawnFromArgs(UWorld* World, const TArray<FString>& Args)
	{
		int32 Wanted = 0;
		for (const FString& Arg : Args)
			if (Arg.IsNumeric())
				Wanted = FCString::Atoi(*Arg);
		if (!IsValid(World))
			return nullptr;
		int32 Index = 0;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It, ++Index)
			if (Index == Wanted)
				return It->Get() ? It->Get()->GetPawn() : nullptr;
		return nullptr;
	}

	// 플레이어 앞 Distance 지점의 "땅 위" 위치. 앞에 건물이 있으면 지붕에 올라가 버리므로(보스가 지붕에서 대기하던 원인)
	// 플레이어 발 높이에서 아래로만 찾는다.
	// YawOffset: 정면에서 몇 도 돌린 방향인가(0 = 정면). 여러 마리를 둘레에 벌려 세울 때 쓴다.
	static FTransform InFrontOfPlayer(const APawn* Pawn, float Distance, float LiftAboveGround = 50.0f, float YawOffset = 0.0f)
	{
		const FVector Direction = Pawn->GetActorForwardVector().RotateAngleAxis(YawOffset, FVector::UpVector);
		FVector Location = Pawn->GetActorLocation() + Direction * Distance;
		const FVector TraceStart = FVector(Location.X, Location.Y, Pawn->GetActorLocation().Z + 100.0f);
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSpawnGround), false, Pawn);
		if (Pawn->GetWorld()->LineTraceSingleByChannel(Ground, TraceStart, TraceStart - FVector(0.0f, 0.0f, 3000.0f), ECC_Visibility, Params))
			Location.Z = Ground.ImpactPoint.Z + LiftAboveGround;
		else
			Location.Z = Pawn->GetActorLocation().Z + LiftAboveGround;
		const FRotator Facing(0.0f, Pawn->GetActorRotation().Yaw + YawOffset + 180.0f, 0.0f);
		return FTransform(Facing, Location);
	}

	// PG.SpawnMonster [Slime|Cactus|Beholder|ChestMonster] [방향(도) 거리(cm)]
	// 방향·거리를 주면 플레이어 둘레의 그 자리에 세운다. AI 측정(Tools/ai_bench.ps1)이 8마리를 늘 같은 자리·같은 순서로 세우려고 쓴다
	//   — 한 자리에 한꺼번에 세우면 겹쳐 쌓여 판마다 플레이어에게 닿는 몬스터 수가 달랐다(9/23: 공격 58번 ↔ 30번).
	static void SpawnMonster(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnMonster: player pawn missing (run in PIE)"));
			return;
		}
		const FName Preset = Args.Num() > 0 ? FName(*Args[0]) : FName(TEXT("Slime"));
		const float YawOffset = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 0.0f;
		const float Distance = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 700.0f;
		const FTransform Transform = InFrontOfPlayer(Pawn, Distance, 120.0f, YawOffset);
		APGMonsterCharacter* Monster = World->SpawnActorDeferred<APGMonsterCharacter>(
			APGMonsterCharacter::StaticClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!IsValid(Monster))
			return;
		FPGMonsterVisuals Visuals;
		if (APGMonsterCharacter::GetPresetVisuals(Preset, Visuals))
		{
			Monster->Visuals = Visuals;
			Monster->bStartDormant = !Visuals.DormantIdle.IsNull(); // 화분 선인장처럼 잠복 모션이 있으면 잠복으로 시작
		}
		else
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnMonster: unknown preset %s, using default"), *Preset.ToString());
		Monster->FinishSpawning(Transform);
		UPGMonsterLookComponent::Attach(Monster, Preset); // 멀티: 클라에서도 같은 모양(전투 스포너와 같이)
		UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnMonster: %s (%s) at %s"), *Monster->GetName(), *Preset.ToString(), *Transform.GetLocation().ToCompactString());
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnMonsterCommand(
		TEXT("PG.SpawnMonster"),
		TEXT("Spawns a test monster in front of the player. Arg: preset (Slime, Cactus, Beholder, ChestMonster)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnMonster));

	// PG.SpawnBattleship [배율] [높이m]   예: PG.SpawnBattleship 1 60
	// 전함 후보(Minerva 화물선, 31 부품 조립 블루프린트)를 플레이어 앞 하늘에 띄워 크기를 눈으로 본다(9/20: 전함 1단계 "공중에 띄워 보기").
	// 로그에 길이·폭·높이(m)를 찍는다. 기획서 목표 길이 60~80m 에 맞는 배율을 찾는 데 쓴다. 다시 부르면 앞의 것을 지우고 새로 띄운다.
	static TWeakObjectPtr<AActor> PreviewBattleship;
	static void SpawnBattleship(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnBattleship: player pawn missing (run in PIE)"));
			return;
		}
		UClass* ShipClass = LoadClass<AActor>(nullptr, TEXT("/Game/kb3d_missiontominerva/Blueprints/Vehicles/BP_KB3D_MTM_VehicleCargoShip_A.BP_KB3D_MTM_VehicleCargoShip_A_C"));
		if (!ShipClass)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnBattleship: Mission to Minerva cargo ship blueprint missing"));
			return;
		}
		if (AActor* Old = PreviewBattleship.Get())
			Old->Destroy();
		const float Scale = Args.Num() > 0 ? FMath::Max(0.1f, FCString::Atof(*Args[0])) : 1.0f;
		const float HeightM = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 60.0f;
		const FVector Forward = FRotator(0.0f, Pawn->GetActorRotation().Yaw, 0.0f).Vector();
		const FVector Location = Pawn->GetActorLocation() + Forward * 8000.0f + FVector(0.0f, 0.0f, HeightM * 100.0f);
		const FTransform Transform(FRotator(0.0f, Pawn->GetActorRotation().Yaw + 90.0f, 0.0f), Location, FVector(Scale));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Ship = World->SpawnActor<AActor>(ShipClass, Transform, Params);
		if (!IsValid(Ship))
			return;
		PreviewBattleship = Ship;
		// 크기는 1초 뒤에 잰다. 블루프린트의 부품(자식 액터 31개)이 스폰 직후엔 아직 안 붙어 있어 0m 로 나왔다(9/20).
		TWeakObjectPtr<AActor> WeakShip(Ship);
		FTimerHandle MeasureHandle;
		World->GetTimerManager().SetTimer(MeasureHandle, FTimerDelegate::CreateLambda([WeakShip, Scale, HeightM]()
		{
			AActor* S = WeakShip.Get();
			if (!IsValid(S))
				return;
			FBox Box(ForceInit);
			TArray<AActor*> Parts;
			S->GetAttachedActors(Parts, true, true);
			Parts.Add(S);
			for (const AActor* Part : Parts)
				for (const UActorComponent* Component : Part->GetComponents())
					if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component); Primitive && Primitive->IsRegistered())
						Box += Primitive->Bounds.GetBox();
			const FVector Extent = Box.IsValid ? Box.GetExtent() : FVector::ZeroVector;
			UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnBattleship: scale %.2f  size L/W/H = %.1f / %.1f / %.1f m (parts %d, %.0fm up). Target length 60~80m."),
				Scale, FMath::Max(Extent.X, Extent.Y) * 0.02f, FMath::Min(Extent.X, Extent.Y) * 0.02f, Extent.Z * 0.02f, Parts.Num(), HeightM);
		}), 1.0f, false);
	}

	// PG.Finale.Start [경고초]   보스를 잡지 않고 피날레를 시작한다(전함 등장 확인용).
	static void FinaleStart(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.Finale.Start: player pawn missing (run in PIE)"));
			return;
		}
		APGFinaleDirector* Director = APGFinaleDirector::Get(World, true);
		if (!Director)
			return;
		if (Args.Num() > 0)
			Director->WarningSeconds = FMath::Max(0.0f, FCString::Atof(*Args[0]));
		Director->StartFinale(Pawn->GetActorLocation());
		UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.Start: warning %.0fs, ship will come over the mountains"), Director->WarningSeconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs FinaleStartCommand(
		TEXT("PG.Finale.Start"),
		TEXT("Starts the finale here without killing the boss. Arg: [warning seconds]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinaleStart));

	// PG.Finale.Status   지금 어느 단계인지, 전함이 어디 있는지 찍는다.
	static void FinaleStatus(const TArray<FString>& Args, UWorld* World)
	{
		APGFinaleDirector* Director = APGFinaleDirector::Get(World);
		if (!Director)
		{
			UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.Status: not started"));
			return;
		}
		const APGBattleshipActor* Ship = Director->GetShip();
		APawn* Pawn = GetPlayerPawn(World);
		UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.Status: state=%s ship=%s length=%.0fm dist=%.0fm"),
			*StaticEnum<EPGFinaleState>()->GetNameStringByValue(static_cast<int64>(Director->GetState())),
			IsValid(Ship) ? *Ship->GetName() : TEXT("none"),
			IsValid(Ship) ? Ship->GetShipLengthCm() * 0.01f : 0.0f,
			(IsValid(Ship) && IsValid(Pawn)) ? FVector::Dist(Ship->GetActorLocation(), Pawn->GetActorLocation()) * 0.01f : 0.0f);
	}

	static FAutoConsoleCommandWithWorldAndArgs FinaleStatusCommand(
		TEXT("PG.Finale.Status"),
		TEXT("Prints the finale state and where the battleship is."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinaleStatus));

	// PG.Finale.Board   전함 갑판 위로 바로 올려 준다. 승강 발판(9초)·날으는 차 없이 속을 확인할 때.
	static void FinaleBoard(const TArray<FString>& Args, UWorld* World)
	{
		APGFinaleDirector* Director = APGFinaleDirector::Get(World);
		APawn* Pawn = GetPlayerPawnFromArgs(World, Args);
		APGBattleshipActor* Ship = Director ? Director->GetShip() : nullptr;
		if (!IsValid(Ship) || !IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.Finale.Board: no ship yet (PG.Finale.Start 0 먼저)"));
			return;
		}
		// 격납고 입구 안쪽, 갑판 위 2m. 70m 안쪽 — 30m 는 아직 뒷문 경사판 위라, 누가 조종석에 앉아 문이 닫히면
		//   문과 함께 들려 올라갔다(멀티 9/27 두 사람 시험).
		const FVector Entrance = Ship->GetHangarEntranceWorld();
		// "ramp" 를 붙이면 예전 자리(경사판 위 30m) — 문이 닫힐 때 사람을 안으로 옮기는지 시험할 때.
		const float InsideCm = Args.Contains(TEXT("ramp")) ? 3000.0f : 7000.0f;
		const FVector Inside = Entrance + Ship->GetActorForwardVector() * InsideCm + FVector(0.0f, 0.0f, 200.0f);
		Pawn->SetActorLocation(Inside, false, nullptr, ETeleportType::TeleportPhysics);
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()); PC && !PC->IsLocalController())
			PC->ClientSetLocation(Inside, Pawn->GetActorRotation()); // 멀티: 원격 사람 컴퓨터에도
		UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.Board: moved %s to deck at %s (deck z=%.0f)"), *Pawn->GetName(), *Inside.ToCompactString(), Ship->GetDeckWorldZ());
	}

	// PG.SpawnGirl [fueled]   변신 여고생을 눈앞에 세우고 연료통을 하나 떨어뜨린다.
	//
	// 왜 만드나: 여고생·연료통·변신·역변신을 확인하려면 판을 처음부터 돌려야 했는데, 지금은 그 앞에
	//   보스 처치와 전함 이륙이 끼어 있어서 한 번 보는 데 몇 분씩 걸린다(사용자 9/20). 이 명령 하나로
	//   그 구간만 바로 본다.
	// 인자: fueled 를 붙이면 "이미 연료를 받은" 상태로 세운다 — 마시는 동작을 건너뛰고 F 한 번에 바로
	//   변신하므로, 차·비행·역변신만 볼 때 쓴다.
	static void SpawnGirl(const TArray<FString>& Args, UWorld* World)
	{
		APawn* PlayerPawn = GetPlayerPawn(World);
		if (!IsValid(PlayerPawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnGirl: player pawn missing (run in PIE)"));
			return;
		}
		const bool bFueled = Args.Num() > 0 && (Args[0] == TEXT("fueled") || Args[0] == TEXT("1"));

		// 눈앞 4m. 발이 땅에 붙도록 실제 표면을 재서 놓는다(공중에 뜨면 떨어지며 시작한다).
		const FVector Forward = FVector(PlayerPawn->GetActorForwardVector().X, PlayerPawn->GetActorForwardVector().Y, 0.0f).GetSafeNormal();
		FVector Where = PlayerPawn->GetActorLocation() + Forward * 400.0f;
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSpawnGirl), false, PlayerPawn);
		const FVector From = Where + FVector(0.0f, 0.0f, 500.0f);
		if (World->LineTraceSingleByChannel(Ground, From, From - FVector(0.0f, 0.0f, 3000.0f), ECC_Visibility, Params))
			Where = Ground.ImpactPoint + FVector(0.0f, 0.0f, 5.0f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		// 플레이어 쪽을 보게 세운다(Forward 의 반대).
		const FTransform Transform(FRotator(0.0f, Forward.Rotation().Yaw + 180.0f, 0.0f), Where);
		APGTransformNPCActor* Girl = World->SpawnActorDeferred<APGTransformNPCActor>(
			APGTransformNPCActor::GetSpawnClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (IsValid(Girl))
		{
			if (bFueled)
				Girl->SetAlreadyFueled(true);
			Girl->FinishSpawning(Transform);
		}

		// 연료통도 발밑에 하나. 인벤토리를 거치지 않아도 바로 주울 수 있다.
		if (!bFueled)
		{
			const FVector CanAt = PlayerPawn->GetActorLocation() + Forward * 120.0f - FVector(0.0f, 0.0f, PlayerPawn->GetSimpleCollisionHalfHeight());
			APGFloorItemActor::SpawnDrop(World, TEXT("Fuel"), 1, FTransform(FRotator::ZeroRotator, CanAt));
		}

		UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnGirl: %s at %s (%s) — 연료통을 주워 F, 변신 뒤 우클릭=상승 / W=전진"),
			*GetNameSafe(Girl), *Where.ToCompactString(),
			bFueled ? TEXT("already fueled — F 한 번이면 바로 변신") : TEXT("연료통도 같이 놓음"));
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnGirlCommand(
		TEXT("PG.SpawnGirl"),
		TEXT("Spawns the transforming school girl in front of the player, with a fuel can. Pass 'fueled' to skip the drink."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnGirl));

	static FAutoConsoleCommandWithWorldAndArgs FinaleBoardCommand(
		TEXT("PG.Finale.Board"),
		TEXT("Teleports the player onto the battleship deck (skips the elevator ride)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinaleBoard));

	// PG.Finale.Launch   갑판에 사람이 없어도 바로 이륙시킨다(드래곤 단계까지 확인용).
	static void FinaleLaunch(const TArray<FString>& Args, UWorld* World)
	{
		if (APGFinaleDirector* Director = APGFinaleDirector::Get(World))
		{
			Director->ForceLaunch();
			UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.Launch: taking off"));
		}
		else
			UE_LOG(LogPGObjects, Warning, TEXT("PG.Finale.Launch: finale not started"));
	}

	// PG.Finale.WreckShip   배 체력을 0 으로(추락 연출 확인용 — 멀티 9/27 추락 이펙트 시험). 서버에서 부른다.
	static void FinaleWreckShip(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<APGBattleshipActor> It(World); It; ++It)
		{
			if (!It->HasAuthority() || It->IsWrecked())
				continue;
			UGameplayStatics::ApplyDamage(*It, It->GetHealth() + 1.0f, nullptr, nullptr, UDamageType::StaticClass());
			UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.WreckShip: %s hp now %.0f, wrecked=%d"), *It->GetName(), It->GetHealth(), It->IsWrecked() ? 1 : 0);
			return;
		}
		UE_LOG(LogPGObjects, Warning, TEXT("PG.Finale.WreckShip: no ship to wreck (PG.Finale.Start 0 먼저)"));
	}

	// PG.RobotAudit   로봇마다 크기·캡슐·메시 높이(메시 기준 자리)를 찍는다. 서버와 클라 "Client monster audit" 을 비교(멀티 9/28).
	static void RobotAudit(const TArray<FString>& Args, UWorld* World)
	{
		FString Robots;
		for (TActorIterator<APGRobotCharacter> It(World); It; ++It)
		{
			const USkeletalMeshComponent* Body = It->GetMesh();
			Robots += FString::Printf(TEXT("%s(%s scale %.1f, capsule %.0f, mesh z %.0f, actor z %.0f, mesh world z %.0f) "), *It->GetName(), It->IsRideable() ? TEXT("ride") : TEXT("boss"),
				It->GetActorScale3D().X, It->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(), Body ? Body->GetRelativeLocation().Z : 0.0f,
				It->GetActorLocation().Z, Body ? Body->GetComponentLocation().Z : 0.0f);
		}
		UE_LOG(LogPGObjects, Display, TEXT("PG.RobotAudit: %s"), *Robots);
	}

	static FAutoConsoleCommandWithWorldAndArgs RobotAuditCommand(
		TEXT("PG.RobotAudit"),
		TEXT("Logs every robot's scale, capsule and mesh height (compare server and client)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RobotAudit));

	// PG.KnockBoxTest   정밀 충돌(물리를 못 켜는 모양) 상자 하나를 골라 날리고 2초 뒤 얼마나 움직였나 찍는다(9/28 SM_box 경고 확인). 서버에서.
	static void KnockBoxTest(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<AItemContainerActor> It(World); It; ++It)
		{
			UStaticMeshComponent* Mesh = It->FindComponentByClass<UStaticMeshComponent>();
			const UBodySetup* Setup = Mesh ? Mesh->GetBodySetup() : nullptr;
			if (!It->HasAuthority() || !Setup || !(Setup->CollisionTraceFlag == CTF_UseComplexAsSimple || Setup->AggGeom.GetElementCount() == 0))
				continue;
			TWeakObjectPtr<AItemContainerActor> Box(*It);
			const FVector Start = It->GetActorLocation();
			It->KnockLoose(FVector(0.0f, 0.0f, 1200.0f) + It->GetActorForwardVector() * 600.0f);
			UE_LOG(LogPGObjects, Display, TEXT("PG.KnockBoxTest: knocked %s (%s) at %s"), *It->GetName(), *GetNameSafe(Mesh->GetStaticMesh()), *Start.ToCompactString());
			FTimerHandle Later;
			World->GetTimerManager().SetTimer(Later, FTimerDelegate::CreateLambda([Box, Start]()
			{
				if (!Box.IsValid())
					return;
				const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Box->GetRootComponent());
				UE_LOG(LogPGObjects, Display, TEXT("PG.KnockBoxTest: %s moved %.0fcm in 2s, root %s simulating=%d"), *Box->GetName(),
					FVector::Dist(Box->GetActorLocation(), Start), *GetNameSafe(Root), Root && Root->IsSimulatingPhysics() ? 1 : 0);
			}), 2.0f, false);
			return;
		}
		UE_LOG(LogPGObjects, Warning, TEXT("PG.KnockBoxTest: no container with complex-only collision"));
	}
	static FAutoConsoleCommandWithWorldAndArgs KnockBoxTestCommand(TEXT("PG.KnockBoxTest"),
		TEXT("Knocks one container whose mesh cannot simulate physics (complex collision) and reports how far it moved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KnockBoxTest));

	// PG.ContainerWatch   (클라 확인용) 처음 부르면 모든 상자 자리를 적고, 다음에 부르면 50cm 넘게 움직인 상자를 찍는다.
	static void ContainerWatch(const TArray<FString>& Args, UWorld* World)
	{
		static TMap<TWeakObjectPtr<AItemContainerActor>, FVector> Seen;
		if (Seen.IsEmpty())
		{
			for (TActorIterator<AItemContainerActor> It(World); It; ++It)
				Seen.Add(*It, It->GetActorLocation());
			UE_LOG(LogPGObjects, Display, TEXT("PG.ContainerWatch: remembered %d container(s)"), Seen.Num());
			return;
		}
		int32 Moved = 0;
		for (const TPair<TWeakObjectPtr<AItemContainerActor>, FVector>& Pair : Seen)
			if (Pair.Key.IsValid() && FVector::Dist(Pair.Key->GetActorLocation(), Pair.Value) > 50.0f)
			{
				++Moved;
				UE_LOG(LogPGObjects, Display, TEXT("PG.ContainerWatch: %s moved %.0fcm on this screen"), *Pair.Key->GetName(), FVector::Dist(Pair.Key->GetActorLocation(), Pair.Value));
			}
		UE_LOG(LogPGObjects, Display, TEXT("PG.ContainerWatch: %d of %d container(s) moved"), Moved, Seen.Num());
		Seen.Reset();
	}
	static FAutoConsoleCommandWithWorldAndArgs ContainerWatchCommand(TEXT("PG.ContainerWatch"),
		TEXT("First call remembers every container position, the next call lists the ones that moved."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ContainerWatch));

	// PG.ExplodeFuel   맵의 연료통 하나를 터뜨린다(멀티 9/27 폭발 그림 시험). 서버에서 부른다.
	static void ExplodeFuel(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<APGFloorItemActor> It(World); It; ++It)
		{
			if (!It->HasAuthority() || !It->IsLooseObject())
				continue;
			const FString Name = It->GetName();
			UGameplayStatics::ApplyDamage(*It, 100000.0f, nullptr, nullptr, UDamageType::StaticClass());
			UE_LOG(LogPGObjects, Display, TEXT("PG.ExplodeFuel: hit %s"), *Name);
			return;
		}
		UE_LOG(LogPGObjects, Warning, TEXT("PG.ExplodeFuel: no loose fuel can on the map"));
	}

	static FAutoConsoleCommandWithWorldAndArgs ExplodeFuelCommand(
		TEXT("PG.ExplodeFuel"),
		TEXT("Blows up one loose fuel can (multiplayer blast visual test)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExplodeFuel));

	static FAutoConsoleCommandWithWorldAndArgs FinaleWreckShipCommand(
		TEXT("PG.Finale.WreckShip"),
		TEXT("Drops the battleship's health to 0 (wreck fall test)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinaleWreckShip));

	static FAutoConsoleCommandWithWorldAndArgs FinaleLaunchCommand(
		TEXT("PG.Finale.Launch"),
		TEXT("Forces the battleship to take off (skips waiting for someone on the deck)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinaleLaunch));

	// PG.Finale.Bridge   함교(뱃머리 조종석) 앞으로 바로 간다. 437m 짜리 배 안에서 걸어 찾기 어렵다.
	static void FinaleBridge(const TArray<FString>& Args, UWorld* World)
	{
		APGFinaleDirector* Director = APGFinaleDirector::Get(World);
		APawn* Pawn = GetPlayerPawnFromArgs(World, Args);
		APGBattleshipActor* Ship = Director ? Director->GetShip() : nullptr;
		if (!IsValid(Ship) || !IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.Finale.Bridge: no ship yet (PG.Finale.Start 0 먼저)"));
			return;
		}
		const FVector Spot = Ship->GetBridgeWorld() + FVector(0.0f, 0.0f, 120.0f);
		Pawn->SetActorLocation(Spot, false, nullptr, ETeleportType::TeleportPhysics);
		// 멀티(9/27): 원격 사람이면 그 컴퓨터에도(서버가 옮긴 위치는 조종하는 본인에게 복제되지 않는다).
		if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()); PC && !PC->IsLocalController())
			PC->ClientSetLocation(Spot, Pawn->GetActorRotation());
		UE_LOG(LogPGObjects, Display, TEXT("PG.Finale.Bridge: moved %s to the bridge at %s"), *Pawn->GetName(), *Spot.ToCompactString());
	}

	static FAutoConsoleCommandWithWorldAndArgs FinaleBridgeCommand(
		TEXT("PG.Finale.Bridge"),
		TEXT("Teleports the player to the battleship bridge (nose console)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&FinaleBridge));

	static FAutoConsoleCommandWithWorldAndArgs SpawnBattleshipCommand(
		TEXT("PG.SpawnBattleship"),
		TEXT("Floats the battleship candidate (Minerva cargo ship) in the sky ahead to judge its size. Args: [scale] [height m]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnBattleship));

	// PG.SpawnRobot [boss|ride] [크기]   기본 ride. 예: PG.SpawnRobot boss 8
	static void SpawnRobot(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnRobot: player pawn missing (run in PIE)"));
			return;
		}
		const bool bBoss = Args.Num() > 0 && Args[0].Equals(TEXT("boss"), ESearchCase::IgnoreCase);
		const float Scale = (bBoss && Args.Num() > 1) ? FCString::Atof(*Args[1]) : 2.5f;
		// 캡슐 반높이 110 × 크기만큼 띄워야 발이 땅에 닿는다.
		const FTransform Transform = InFrontOfPlayer(Pawn, bBoss ? 1500.0f * FMath::Max(1.0f, Scale / 2.5f) : 400.0f, bBoss ? 110.0f * Scale + 20.0f : 130.0f);
		APGRobotCharacter* Robot = World->SpawnActorDeferred<APGRobotCharacter>(
			UPGVisualSettings::RobotSpawnClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!IsValid(Robot))
			return;
		if (bBoss)
			Robot->ConfigureAsBoss(Scale);
		else
			Robot->ConfigureAsRideable();
		Robot->FinishSpawning(Transform);
		UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnRobot: %s (%s, scale %.1f)"), *Robot->GetName(), bBoss ? TEXT("boss") : TEXT("ride, press F to mount"), bBoss ? Scale : 1.0f);
	}

	// PG.SmashCoreTest   (서버, 시험용) 첫 번째 사람을 탈 수 있는 로봇에 태워 중앙 건물(워존 가운데 창고) 바깥 45m 에 건물 쪽을 보게 세우고,
	//   두 번째 사람은 그 옆 70m 에서 건물을 보게 세운다. 9/28 "중앙 건물 박살 낼 때 프레임 드랍" 을 화면 없이 재려고 —
	//   이어서 클라이언트 PG.NetSmashTest 가 로봇을 건물 안으로 몰며 1초마다 프레임 시간을 적는다.
	static void SmashCoreTest(const TArray<FString>& Args, UWorld* World)
	{
		const ULevelStreamingDynamic* Core = nullptr;
		for (ULevelStreaming* Level : World->GetStreamingLevels())
		{
			const ULevelStreamingDynamic* Dynamic = Cast<ULevelStreamingDynamic>(Level);
			const FString Package = IsValid(Dynamic) ? (Dynamic->PackageNameToLoad.IsNone() ? Dynamic->GetWorldAssetPackageName() : Dynamic->PackageNameToLoad.ToString()) : FString();
			if (Package.Contains(TEXT("WarZoneCore")))
				Core = Dynamic;
		}
		APawn* Driver = GetPlayerPawnFromArgs(World, { TEXT("0") });
		if (!Core || !IsValid(Driver))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SmashCoreTest: core level %s, first player %s"), Core ? TEXT("found") : TEXT("missing"), *GetNameSafe(Driver));
			return;
		}
		const FVector Centre = Core->LevelTransform.GetLocation();
		// 건물 바깥 땅: 네 방향 중 45m 밖 바닥이 건물 바닥 높이(지붕 아님)인 쪽.
		FVector Spot = FVector::ZeroVector;
		FVector Out = FVector::ZeroVector;
		for (int32 Side = 0; Side < 4 && Spot.IsZero(); ++Side)
		{
			const FVector Dir = Core->LevelTransform.GetRotation().RotateVector(FVector(1.0f, 0.0f, 0.0f)).RotateAngleAxis(90.0f * Side, FVector::UpVector);
			const FVector Probe = Centre + Dir * 4500.0f;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSmashCoreTest), false, Driver);
			if (World->LineTraceSingleByChannel(Ground, Probe + FVector(0.0f, 0.0f, 3000.0f), Probe - FVector(0.0f, 0.0f, 3000.0f), ECC_Visibility, Params)
				&& Ground.ImpactPoint.Z < Centre.Z + 150.0f)
			{
				Spot = Ground.ImpactPoint;
				Out = Dir;
			}
		}
		if (Spot.IsZero())
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SmashCoreTest: no open ground 45m around the core at %s"), *Centre.ToCompactString());
			return;
		}
		const FRotator Facing(0.0f, (-Out).Rotation().Yaw, 0.0f);
		// 게임과 같은 크기(설정 RideableRobotScale, 지금 8배 ≈ 17m). 1배로 시험했더니 벽을 거의 못 부숴 잴 것이 없었다.
		const float Scale = FMath::Max(1.0f, GetDefault<UPGCombatSettings>()->RideableRobotScale);
		const FTransform RobotAt(Facing, Spot + FVector(0.0f, 0.0f, 110.0f * Scale + 40.0f));
		APGRobotCharacter* Robot = World->SpawnActorDeferred<APGRobotCharacter>(UPGVisualSettings::RobotSpawnClass(),
			RobotAt, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!IsValid(Robot))
			return;
		Robot->ConfigureAsRideable(Scale);
		Robot->FinishSpawning(RobotAt);
		// 탈 수 있는 로봇은 자는 모습으로 태어나 사람이 가까이 오면 깨어난다(변신 모션). 사람을 옆에 세우고 깰 때까지 0.5초마다 태워 본다.
		const FVector Beside = Spot + FVector::CrossProduct(FVector::UpVector, Out) * (400.0f + 80.0f * Scale) + FVector(0.0f, 0.0f, 120.0f);
		Driver->SetActorLocation(Beside, false, nullptr, ETeleportType::TeleportPhysics);
		if (APlayerController* PC = Cast<APlayerController>(Driver->GetController()))
			PC->ClientSetLocation(Beside, Facing);
		TWeakObjectPtr<APGRobotCharacter> WeakRobot(Robot);
		TWeakObjectPtr<APawn> WeakDriver(Driver);
		TSharedRef<int32> Tries = MakeShared<int32>(0);
		TSharedRef<FTimerHandle> MountTimer = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*MountTimer, FTimerDelegate::CreateLambda([WeakRobot, WeakDriver, Tries, MountTimer, World]()
		{
			APGRobotCharacter* R = WeakRobot.Get();
			APawn* D = WeakDriver.Get();
			const bool bMounted = IsValid(R) && IsValid(D) && R->Mount(D);
			if (bMounted || ++(*Tries) > 40)
			{
				UE_LOG(LogPGObjects, Display, TEXT("PG.SmashCoreTest: mount %s after %.1fs"), bMounted ? TEXT("done") : TEXT("FAILED"), *Tries * 0.5f);
				World->GetTimerManager().ClearTimer(*MountTimer);
			}
		}), 0.5f, true);
		const bool bMounted = false; // 위 타이머가 태운다
		// 구경꾼: 옆으로 비켜 선 70m 밖에서 건물을 본다(화면을 그리는 클라이언트의 비용도 재려고).
		if (APawn* Watcher = GetPlayerPawnFromArgs(World, { TEXT("1") }))
		{
			const FVector Side = FVector::CrossProduct(FVector::UpVector, Out);
			FVector At = Centre + Out * 7000.0f + Side * 2000.0f;
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSmashCoreTest), false, Watcher);
			if (World->LineTraceSingleByChannel(Ground, At + FVector(0.0f, 0.0f, 3000.0f), At - FVector(0.0f, 0.0f, 3000.0f), ECC_Visibility, Params))
				At.Z = Ground.ImpactPoint.Z + 120.0f;
			const FRotator Look = (Centre - At).Rotation();
			Watcher->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
			if (APlayerController* PC = Cast<APlayerController>(Watcher->GetController()))
			{
				PC->ClientSetLocation(At, FRotator(0.0f, Look.Yaw, 0.0f));
				PC->ClientSetRotation(FRotator(-5.0f, Look.Yaw, 0.0f));
			}
		}
		UE_LOG(LogPGObjects, Display, TEXT("PG.SmashCoreTest: core at %s, %s put on %s at %s facing the core (mounted=%d)"),
			*Centre.ToCompactString(), *Driver->GetName(), *Robot->GetName(), *Spot.ToCompactString(), bMounted ? 1 : 0);
	}
	static FAutoConsoleCommandWithWorldAndArgs SmashCoreTestCommand(TEXT("PG.SmashCoreTest"),
		TEXT("Server test: mounts the first player on a ride robot outside the WarZone core building, facing it; the second player watches from 70 m."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SmashCoreTest));

	static FAutoConsoleCommandWithWorldAndArgs SpawnRobotCommand(
		TEXT("PG.SpawnRobot"),
		TEXT("Spawns the robot in front of the player. Arg: boss (hostile AI) or ride (press F to mount, default)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnRobot));

	// PG.GiveItem <ItemId> [Count]   예: PG.GiveItem Rifle_AR70 / PG.GiveItem Ammo_Rifle 90
	static void GiveItem(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn) || Args.Num() == 0)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.GiveItem <ItemId> [Count]"));
			return;
		}
		const FName ItemId(*Args[0]);
		const int32 Count = Args.Num() > 1 ? FMath::Max(1, FCString::Atoi(*Args[1])) : 1;
		// 차·탱크·로봇에 탄 동안은 조종 대상이 탈것이라 아이템을 받을 수 없다(given=false 로만 나와 헷갈렸다).
		if (!Pawn->GetClass()->ImplementsInterface(UPGItemReceiver::StaticClass()))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.GiveItem: %s cannot hold items (riding a vehicle?) - dismount and retry"), *Pawn->GetName());
			return;
		}
		const bool bGiven = UPGItemReceiverLibrary::GiveItem(Pawn, ItemId, Count);
		UE_LOG(LogPGObjects, Display, TEXT("PG.GiveItem: %s x%d given=%s"), *ItemId.ToString(), Count, bGiven ? TEXT("true") : TEXT("false"));
	}

	// PG.WeaponTune [fp|hand] x y z pitch yaw roll [scale]   인자 없이 치면 현재 값 출력
	static void WeaponTune(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		UPGWeaponComponent* Weapon = IsValid(Pawn) ? Pawn->FindComponentByClass<UPGWeaponComponent>() : nullptr;
		if (!IsValid(Weapon))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.WeaponTune: weapon component missing"));
			return;
		}
		if (Args.Num() < 7)
		{
			UE_LOG(LogPGObjects, Display, TEXT("PG.WeaponTune: %s"), *Weapon->DescribeEquippedTuning());
			UE_LOG(LogPGObjects, Display, TEXT("usage: PG.WeaponTune fp|hand x y z pitch yaw roll [scale]"));
			return;
		}
		const bool bFirstPerson = !Args[0].Equals(TEXT("hand"), ESearchCase::IgnoreCase);
		const FVector Location(FCString::Atof(*Args[1]), FCString::Atof(*Args[2]), FCString::Atof(*Args[3]));
		const FRotator Rotation(FCString::Atof(*Args[4]), FCString::Atof(*Args[5]), FCString::Atof(*Args[6]));
		const float Scale = Args.Num() > 7 ? FCString::Atof(*Args[7]) : 1.0f;
		Weapon->TuneEquipped(bFirstPerson, Location, Rotation, Scale);
	}

	static FAutoConsoleCommandWithWorldAndArgs WeaponTuneCommand(
		TEXT("PG.WeaponTune"),
		TEXT("Adjusts the equipped weapon attach transform at runtime. Args: fp|hand x y z pitch yaw roll [scale]. No args prints current."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WeaponTune));

	// PG.Collapse [반지름 m, 기본 40] [초, 기본 3] [X Y cm — 주면 그 자리]   플레이어 앞 60m 를 중심으로 구역 붕괴를 일으킨다(IPGMapInfo::CollapseRegion).
	// 왜(2026-09-26): 붕괴를 별도 클래스(UPGRegionCollapse)로 옮긴 뒤, 피날레 자동 진행은 붕괴 자리가 맵 밖이라 조각 0개였다 —
	//   실제로 조각을 옮기는 길을 화면 없이 확인하려고 만든다. 결과 줄은 "PGCollapse:" 로그.
	static void Collapse(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(World);
		if (!IsValid(Pawn) || !Map)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.Collapse: player or map missing"));
			return;
		}
		const float RadiusM = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 40.0f;
		const float Seconds = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 3.0f;
		FVector Centre = Pawn->GetActorLocation() + Pawn->GetActorForwardVector().GetSafeNormal2D() * 6000.0f;
		// 세 번째·네 번째 값(X Y, cm)을 주면 그 자리. 플레이 중 본 구덩이를 같은 시드로 화면 없이 다시 만들어 보려고(9/28).
		if (Args.Num() > 3)
			Centre = FVector(FCString::Atof(*Args[2]), FCString::Atof(*Args[3]), 20.0f);
		UE_LOG(LogPGObjects, Display, TEXT("PG.Collapse: radius %.0f m, %.1f s at %s"), RadiusM, Seconds, *Centre.ToCompactString());
		Map->CollapseRegion(Centre, RadiusM * 100.0f, Seconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs CollapseCommand(
		TEXT("PG.Collapse"),
		TEXT("Collapses a region 60 m in front of the player. Args: [radius m=40] [seconds=3] [x y cm = fixed centre]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Collapse));

	// PG.KillBoss   맵의 보스 로봇(탈 수 없는 로봇)에게 치명 피해를 준다. 보스 역할의 죽음 처리(피날레 열기)를 화면 없이 확인하려고(2026-09-26).
	static void KillBoss(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<APGRobotCharacter> It(World); It; ++It)
		{
			if (It->IsRideable() || It->IsDead())
				continue;
			UE_LOG(LogPGObjects, Display, TEXT("PG.KillBoss: %s (hp %.0f)"), *It->GetName(), It->GetHealth());
			UGameplayStatics::ApplyDamage(*It, It->GetMaxHealth() * 100.0f, nullptr, nullptr, UDamageType::StaticClass());
			return;
		}
		UE_LOG(LogPGObjects, Warning, TEXT("PG.KillBoss: no living boss robot"));
	}

	static FAutoConsoleCommandWithWorldAndArgs KillBossCommand(
		TEXT("PG.KillBoss"),
		TEXT("Applies lethal damage to the boss robot (non-rideable). Checks the boss death path (finale start)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&KillBoss));

	// PG.DamagePlayer [사람 번호=0] [피해=30] [쏜 사람 번호]   (서버) 그 사람 캐릭터에 언리얼 기본 피해를 보내고 체력(GAS Health) 전·후를 찍는다.
	//   9/28: 플레이어가 피해를 받아도 체력이 안 깎이던 것(캐릭터 쪽에 받는 코드가 없음) — 형님이 받는 코드를 넣은 뒤 화면 없이 확인하는 용도.
	//   세 번째 값을 주면 그 사람이 쏜 것처럼(PvP 규칙 확인).
	static void DamagePlayer(const TArray<FString>& Args, UWorld* World)
	{
		const int32 VictimIndex = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const float Amount = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 30.0f;
		APawn* Victim = GetPlayerPawnFromArgs(World, { FString::FromInt(VictimIndex) });
		APawn* Shooter = Args.Num() > 2 ? GetPlayerPawnFromArgs(World, { Args[2] }) : nullptr;
		if (!IsValid(Victim))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.DamagePlayer: no player %d"), VictimIndex);
			return;
		}
		auto ReadHealth = [Victim]() -> float
		{
			const IAbilitySystemInterface* Owner = Cast<IAbilitySystemInterface>(Victim);
			const UAbilitySystemComponent* Asc = Owner ? Owner->GetAbilitySystemComponent() : nullptr;
			return Asc ? Asc->GetNumericAttribute(UCharacterAttributeSet::GetHealthAttribute()) : -1.0f;
		};
		const float Before = ReadHealth();
		const float Applied = UGameplayStatics::ApplyDamage(Victim, Amount, Shooter ? Shooter->GetController() : nullptr, Shooter, UDamageType::StaticClass());
		UE_LOG(LogPGObjects, Display, TEXT("PG.DamagePlayer: %s took %.0f (applied %.0f, by %s) — Health %.0f -> %.0f%s"),
			*Victim->GetName(), Amount, Applied, *GetNameSafe(Shooter), Before, ReadHealth(),
			Before >= 0.0f && FMath::IsNearlyEqual(Before, ReadHealth()) ? TEXT(" (NOT reduced — the character does not turn damage into Health yet)") : TEXT(""));
	}
	static FAutoConsoleCommandWithWorldAndArgs DamagePlayerCommand(TEXT("PG.DamagePlayer"),
		TEXT("Server test: sends engine damage to player N and logs GAS Health before/after. Args: [player index] [amount] [shooter index]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&DamagePlayer));

	// PG.CarPoseProbe   (서버) 사람이 탄 땅 차에 "바닥 4m 아래" 자리를 모는 사람이 보낸 것처럼 넣어 본다 — 거절돼야 한다(9/28 알트탭 바닥 뚫림).
	static void CarPoseProbe(const TArray<FString>& Args, UWorld* World)
	{
		for (TActorIterator<APGVehiclePawn> It(World); It; ++It)
		{
			if (!It->IsPlayerControlled())
				continue;
			const bool bAccepted = It->ProbeBelowFloorPose();
			UE_LOG(LogPGObjects, Display, TEXT("PG.CarPoseProbe: below-floor pose for %s was %s"), *It->GetName(), bAccepted ? TEXT("ACCEPTED (bad)") : TEXT("rejected (good)"));
			return;
		}
		UE_LOG(LogPGObjects, Warning, TEXT("PG.CarPoseProbe: no ridden car"));
	}
	static FAutoConsoleCommandWithWorldAndArgs CarPoseProbeCommand(TEXT("PG.CarPoseProbe"),
		TEXT("Server test: feeds a below-the-floor driver pose to the ridden car and reports whether it was rejected."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&CarPoseProbe));

	// PG.TankToEdge   사람이 탄 탱크를 맵 가장자리(게임 지점들이 있는 네모의 +X 끝) 안쪽 40m 에 바깥쪽을 보게 세운다.
	//   9/28 "탱크로 외곽 가니 아래로 빠진다" 시험용 — 이어서 클라 PG.NetRideTest tank 가 앞으로 몬다. 서버에서.
	static void TankToEdge(const TArray<FString>& Args, UWorld* World)
	{
		const IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(World);
		if (!Map)
			return;
		FBox Box(ForceInit);
		for (const FLevelDesignPoint& Point : Map->GetLevelDesignPoints())
			Box += Point.WorldLocation;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			// 사람이 탄 탈것(탱크·차 — 캐릭터가 아닌 조종 중인 폰)
			if (!It->IsPlayerControlled() || It->IsA<ACharacter>())
				continue;
			FVector Spot(Box.Max.X - (Args.Num() > 0 ? FCString::Atof(*Args[0]) : 4000.0f), Box.GetCenter().Y, 5000.0f);
			FHitResult Ground;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankToEdge), false, *It);
			if (World->LineTraceSingleByObjectType(Ground, Spot, Spot - FVector(0.0f, 0.0f, 20000.0f), FCollisionObjectQueryParams(ECC_WorldStatic), Params))
				Spot.Z = Ground.ImpactPoint.Z + 300.0f;
			It->SetActorLocationAndRotation(Spot, FRotator(0.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			if (APGVehiclePawn* Car = Cast<APGVehiclePawn>(*It))
				Car->NotifyServerTeleport();
			UE_LOG(LogPGObjects, Display, TEXT("PG.TankToEdge: %s moved to %s facing +X (points box max x %.0f)"), *It->GetName(), *Spot.ToCompactString(), Box.Max.X);
			return;
		}
		UE_LOG(LogPGObjects, Warning, TEXT("PG.TankToEdge: no ridden tank"));
	}
	static FAutoConsoleCommandWithWorldAndArgs TankToEdgeCommand(TEXT("PG.TankToEdge"),
		TEXT("Moves the ridden tank near the +X map edge, facing out (edge fall test)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&TankToEdge));

	// PG.RideTest   가장 가까운(40m 안) 차·탱크·탈 수 있는 로봇에 플레이어를 태웠다가 바로 내린다. 서버에서.
	// 왜(2026-09-26): 세 탈것의 타기·내리기를 공통 순서(PGRide)로 합쳤는데 스모크 테스트에 실제 탑승 검사가 없었다.
	//   화면 없이 "PG.SpawnTank → PG.RideTest" 로 확인한다. 결과 줄: "PG.RideTest: <탈것> mount=.. possess=.. dismount=.. back=.. visible=.. collision=.."
	static void RideTest(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		APlayerController* PC = IsValid(Pawn) ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
		if (!IsValid(PC))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.RideTest: player controller missing"));
			return;
		}
		// 인자: car | tank | robot (없으면 셋 다 후보)
		const FString Kind = Args.Num() > 0 ? Args[0].ToLower() : FString();
		AActor* Best = nullptr;
		float BestDist = 4000.0f;
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			// 'fly': 비행 장비 달린 차만(멀티 비행 시험).
			const bool bCar = It->IsA<APGVehiclePawn>() && (Kind.IsEmpty() || Kind == TEXT("car"))
				&& (!Args.Contains(TEXT("fly")) || It->FindComponentByClass<UPGFlightKitComponent>() != nullptr);
			const bool bTank = It->IsA<APGTankPawn>() && (Kind.IsEmpty() || Kind == TEXT("tank"));
			const bool bRobot = It->IsA<APGRobotCharacter>() && Cast<APGRobotCharacter>(*It)->IsRideable()
				&& (Kind.IsEmpty() || Kind == TEXT("robot"));
			const bool bRide = bCar || bTank || bRobot;
			const float Dist = FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation());
			if (bRide && *It != Pawn && Dist < BestDist)
			{
				Best = *It;
				BestDist = Dist;
			}
		}
		if (!Best)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.RideTest: no rideable within 40 m"));
			return;
		}
		bool bMounted = false, bDismounted = false;
		if (APGVehiclePawn* Car = Cast<APGVehiclePawn>(Best)) bMounted = Car->Mount(Pawn);
		else if (APGTankPawn* Tank = Cast<APGTankPawn>(Best)) bMounted = Tank->Mount(Pawn);
		else if (APGRobotCharacter* Robot = Cast<APGRobotCharacter>(Best)) bMounted = Robot->Mount(Pawn);
		const bool bPossessedRide = PC->GetPawn() == Best;
		// 'stay': 태우기만 하고 둔다 — 멀티 조작 시험(클라이언트의 PG.NetRideTest)이 이어서 운전·하차를 해 본다.
		if (Args.Contains(TEXT("stay")))
		{
			UE_LOG(LogPGObjects, Display, TEXT("PG.RideTest: %s mounted=%d possess=%d (stay)"), *GetNameSafe(Best), bMounted, bPossessedRide);
			return;
		}
		const bool bRiderHidden = Pawn->IsHidden() && !Pawn->GetActorEnableCollision() && Pawn->GetAttachParentActor() == Best;
		if (APGVehiclePawn* Car = Cast<APGVehiclePawn>(Best)) bDismounted = Car->Dismount();
		else if (APGTankPawn* Tank = Cast<APGTankPawn>(Best)) bDismounted = Tank->Dismount();
		else if (APGRobotCharacter* Robot = Cast<APGRobotCharacter>(Best)) bDismounted = Robot->Dismount();
		const bool bBack = PC->GetPawn() == Pawn;
		const bool bPass = bMounted && bPossessedRide && bRiderHidden && bDismounted && bBack
			&& !Pawn->IsHidden() && Pawn->GetActorEnableCollision() && Pawn->GetAttachParentActor() == nullptr;
		UE_LOG(LogPGObjects, Display,
			TEXT("PG.RideTest: %s dist=%.0fm mount=%d possess=%d rider_hidden=%d dismount=%d back=%d visible=%d collision=%d attached=%d exit_dist=%.0fm pass=%s"),
			*GetNameSafe(Best), BestDist * 0.01f, bMounted, bPossessedRide, bRiderHidden, bDismounted, bBack,
			!Pawn->IsHidden(), Pawn->GetActorEnableCollision(), Pawn->GetAttachParentActor() != nullptr,
			FVector::Dist(Pawn->GetActorLocation(), Best->GetActorLocation()) * 0.01f, bPass ? TEXT("true") : TEXT("false"));
	}

	static FAutoConsoleCommandWithWorldAndArgs RideTestCommand(
		TEXT("PG.RideTest"),
		TEXT("Mounts the player on the nearest car/tank/rideable robot (40 m) and dismounts at once; logs pass=true/false."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RideTest));

	// PG.SpawnTank   플레이어 앞 12m 에 탱크. 다가가서 F 로 탑승, 좌클릭 주포.
	static void SpawnTank(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnTank: player pawn missing (run in PIE)"));
			return;
		}
		// 땅 위 3m 에 놓으면 스스로 내려앉는다.
		// 인자: 앞으로 몇 m(기본 12). 멀티 조작 시험은 출발 구역 담장 안쪽에 두려고 가깝게 준다(PG.NetRideTest tank).
		const float Meters = Args.Num() > 0 ? FMath::Clamp(FCString::Atof(*Args[0]), 4.0f, 50.0f) : 12.0f;
		const FTransform Transform = InFrontOfPlayer(Pawn, Meters * 100.0f, 300.0f);
		APGTankPawn* Tank = World->SpawnActor<APGTankPawn>(UPGVisualSettings::TankSpawnClass(), Transform);
		UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnTank: %s, press F to mount"), *GetNameSafe(Tank));
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnTankCommand(
		TEXT("PG.SpawnTank"),
		TEXT("Spawns a drivable tank 12 m in front of the player. F to mount, LMB fires the main gun."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnTank));

	// PG.SpawnVehicle [SportsCar|Hatchback|Pickup|SUV]   기본 SportsCar. 다가가서 F 로 탑승.
	static void SpawnVehicle(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnVehicle: player pawn missing (run in PIE)"));
			return;
		}
		const FName Preset = Args.Num() > 0 && Args[0] != TEXT("fly") ? FName(*Args[0]) : FName(TEXT("SportsCar"));
		const FTransform Transform = InFrontOfPlayer(Pawn, 700.0f, 110.0f); // 바퀴가 땅에 박히지 않게 조금 띄운다
		APGVehiclePawn* Vehicle = World->SpawnActorDeferred<APGVehiclePawn>(
			UPGVisualSettings::VehicleSpawnClass(), Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
		if (!IsValid(Vehicle))
			return;
		FString MeshPath;
		if (Preset != TEXT("SportsCar") && APGVehiclePawn::GetPresetMeshPath(Preset, MeshPath))
		{
			if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
				Vehicle->SetVehicleMesh(Mesh);
		}
		Vehicle->FinishSpawning(Transform);
		// 'fly': 변신차처럼 비행 장비를 단다(멀티 비행 시험 PG.NetRideTest fly 용).
		if (Args.Contains(TEXT("fly")))
		{
			UPGFlightKitComponent* Kit = NewObject<UPGFlightKitComponent>(Vehicle, UPGFlightKitComponent::GetSpawnClass(), TEXT("FlightKit"));
			Kit->RegisterComponent();
		}
		UE_LOG(LogPGObjects, Display, TEXT("PG.SpawnVehicle: %s (%s), press F to mount"), *Vehicle->GetName(), *Preset.ToString());
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnVehicleCommand(
		TEXT("PG.SpawnVehicle"),
		TEXT("Spawns a drivable car in front of the player. Arg: SportsCar (default), Hatchback, Pickup, SUV."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnVehicle));

	// PG.VisualProbe   겉모습 칸 확인(2026-09-23 블루프린트 분리 검증용).
	// 왜: 미사일·연료통 폭발·비행 키트는 플레이어가 무언가 해야 나온다. 화면 없는 시험 실행에서도 한 번씩 돌려
	//   블루프린트/데이터 에셋에서 모양을 제대로 읽는지 로그로 본다. 피해는 0 이라 게임에 영향이 없다.
	static void VisualProbe(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.VisualProbe: player pawn missing (run in PIE)"));
			return;
		}
		// 1) 미사일 한 발(피해 0). 처음 쏠 때 "PGMissile: meshes ..." 로그가 나온다.
		if (UPGMissileSubsystem* Missiles = UPGMissileSubsystem::Get(World))
		{
			const FVector Start = Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 3000.0f);
			const bool bLaunched = Missiles->Launch(Start, Pawn->GetActorForwardVector(), nullptr, Pawn, 0.0f, 0.0f, 100.0f);
			UE_LOG(LogPGObjects, Display, TEXT("PG.VisualProbe: missile launched=%d"), bLaunched ? 1 : 0);
		}
		// 2) 연료통을 떨어뜨리고 터뜨린다(폭발 피해 0 으로 낮춘 뒤).
		if (APGFloorItemActor* Can = APGFloorItemActor::SpawnDrop(World, TEXT("Fuel"), 1, InFrontOfPlayer(Pawn, 3000.0f, 100.0f)))
		{
			Can->FuelBlastDamage = 0.0f;
			UE_LOG(LogPGObjects, Display, TEXT("PG.VisualProbe: floor item %s loose=%d dust=%d mesh=%d material=%d"), *Can->GetClass()->GetName(),
				Can->IsLooseObject() ? 1 : 0, Can->FuelBlastDust.LoadSynchronous() ? 1 : 0, Can->FuelBlastMesh.LoadSynchronous() ? 1 : 0,
				Can->FuelBlastMaterial.LoadSynchronous() ? 1 : 0);
			UGameplayStatics::ApplyDamage(Can, 10000.0f, nullptr, Pawn, UDamageType::StaticClass());
		}
		// 3) 차 한 대에 비행 키트를 붙인다(변신 차와 같은 방법).
		const FTransform CarTransform = InFrontOfPlayer(Pawn, 5000.0f, 110.0f);
		if (APGVehiclePawn* Car = World->SpawnActor<APGVehiclePawn>(UPGVisualSettings::VehicleSpawnClass(), CarTransform))
		{
			UPGFlightKitComponent* Kit = NewObject<UPGFlightKitComponent>(Car, UPGFlightKitComponent::GetSpawnClass(), TEXT("FlightKit"));
			Kit->RegisterComponent();
			UE_LOG(LogPGObjects, Display, TEXT("PG.VisualProbe: flight kit %s on %s booster=%d flame=%d beam=%d"), *Kit->GetClass()->GetName(),
				*Car->GetName(), Kit->BoosterMesh.LoadSynchronous() ? 1 : 0, Kit->FlameMesh.LoadSynchronous() ? 1 : 0,
				Kit->BeamMeshAsset.LoadSynchronous() ? 1 : 0);
		}
		// 4) 새 무기 부품 하나를 빈 액터에 붙여(무기 표가 이때 들어간다) 표를 한 줄씩 적고 치운다.
		if (AActor* Holder = World->SpawnActor<AActor>(AActor::StaticClass(), CarTransform))
		{
			UPGWeaponComponent* Probe = NewObject<UPGWeaponComponent>(Holder, TEXT("ProbeWeapon"));
			Probe->RegisterComponent();
			Probe->LogWeaponDefs();
			Holder->Destroy();
		}
		// 5) 착장 표 한 줄씩 + 연료통 색 + 색 뽑기(같은 시드 = 같은 색인지, 표의 행 순서가 코드와 같은지 본다).
		for (const FPGWearableColor& C : UPGWearableColorLibrary::GetAllColors())
		{
			UE_LOG(LogPGObjects, Display, TEXT("PGWear: %s base=%s slot=%d name=%s mat=%s floor=%s skel=%s static=%s bone=%s offset=%s body=%d"),
				*C.ItemId.ToString(), *C.BaseItemId.ToString(), static_cast<int32>(C.Slot), *C.DisplayName.ToString(),
				*C.Material.ToSoftObjectPath().ToString(), *C.FloorMesh.ToSoftObjectPath().ToString(), *C.WornSkeletalMesh.ToSoftObjectPath().ToString(),
				*C.WornStaticMesh.ToSoftObjectPath().ToString(), *C.AttachBone.ToString(), *C.AttachOffset.ToString(), C.bOffsetInBodySpace ? 1 : 0);
		}
		for (int32 Variant = 0; Variant < 4; ++Variant)
			UE_LOG(LogPGObjects, Display, TEXT("PGWear: Fuel variant %d = %s"), Variant,
				*UPGWearableColorLibrary::FindItemFloorMeshVariant(TEXT("Fuel"), Variant).ToSoftObjectPath().ToString());
		for (const TCHAR* Base : { TEXT("Shirt"), TEXT("Pants"), TEXT("Backpack") })
			for (int64 Seed = 1; Seed <= 4; ++Seed)
				UE_LOG(LogPGObjects, Display, TEXT("PGWear: pick %s seed %lld -> %s"), Base, Seed, *UPGWearableColorLibrary::PickColorVariant(Base, Seed).ToString());
	}

	static FAutoConsoleCommandWithWorldAndArgs VisualProbeCommand(
		TEXT("PG.VisualProbe"),
		TEXT("Test only: fires one harmless missile, blows up a harmless fuel can and bolts a flight kit onto a car, logging which looks were loaded."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&VisualProbe));

	// PG.ConvertLevelDoors
	// 시설 레벨에 박혀 있는 문 "모양" 스태틱 메시(공장 SM_Door, 오두막 Door_01)를 같은 자리의 문 액터로 바꾼다.
	// 레벨 에셋은 건드리지 않고 PIE 때마다 런타임에 바꾼다. 문 메시 이름 → 카탈로그 행은 여기 표가 정한다.
	static void ConvertLevelDoors(const TArray<FString>& Args, UWorld* World)
	{
		const int32 Converted = PGLevelDoorConverter::ConvertLevelDoors(World);
		UE_LOG(LogPGObjects, Display, TEXT("PG.ConvertLevelDoors: converted=%d"), Converted);
	}

	static FAutoConsoleCommandWithWorldAndArgs ConvertLevelDoorsCommand(
		TEXT("PG.ConvertLevelDoors"),
		TEXT("Replaces door-shaped static meshes in streamed facility levels with catalog door actors at the same transform."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ConvertLevelDoors));

	// PG.SpawnCombatFromPoints — 현재 맵의 AISpawn/Spawn 포인트에 몬스터·보스·탈것·차량을 맵 시드로 배치(자동 스폰과 같은 코드).
	static void SpawnCombatFromPoints(const TArray<FString>& Args, UWorld* World)
	{
		const IPGMapInfo* Preview = UPGMapInfoSubsystem::FindMap(World);
		if (!Preview || !Preview->AreLevelDesignPointsBuilt())
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.SpawnCombatFromPoints: level design points not ready"));
			return;
		}
		const AGameModePG* GameMode = World->GetAuthGameMode<AGameModePG>();
		const int64 Seed = IsValid(GameMode) ? GameMode->GetMapGenerationSeed() : 0;
		PGCombatSpawner::SpawnFromPoints(World, Preview->GetLevelDesignPoints(), Seed);
	}

	static FAutoConsoleCommandWithWorldAndArgs SpawnCombatFromPointsCommand(
		TEXT("PG.SpawnCombatFromPoints"),
		TEXT("Spawns monsters, boss robot, rideable robot and vehicles on the map's design points using the map seed."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&SpawnCombatFromPoints));

	// PG.Delay <초> <명령...> — 명령을 몇 초 뒤에 실행한다. 화면 없는 시험(-ExecCmds)은 게임이 켜지자마자 돌아서,
	//   맵·부스가 생기기 전에 실행되던 것을 늦추려고 둔다(9/23). 예: PG.Delay 20 PG.GoTo exchange
	static void Delay(const TArray<FString>& Args, UWorld* World)
	{
		if (!IsValid(World) || Args.Num() < 2)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.Delay <seconds> <command...>"));
			return;
		}
		const float Seconds = FMath::Max(FCString::Atof(*Args[0]), 0.0f);
		TArray<FString> Rest(Args);
		Rest.RemoveAt(0);
		const FString Command = FString::Join(Rest, TEXT(" "));
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> WeakWorld(World);
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Command]()
		{
			if (UWorld* W = WeakWorld.Get(); IsValid(W) && GEngine)
			{
				UE_LOG(LogPGObjects, Display, TEXT("PG.Delay: running \"%s\""), *Command);
				// 플레이어 콘솔로 넘긴다 — 화면 캡처(shot) 처럼 화면(뷰포트)이 받는 명령도 콘솔에 친 것과 똑같이 돈다.
				if (APlayerController* PC = W->GetFirstPlayerController())
					PC->ConsoleCommand(Command, true);
				else
					GEngine->Exec(W, *Command);
			}
		}), FMath::Max(Seconds, 0.01f), false);
		UE_LOG(LogPGObjects, Display, TEXT("PG.Delay: \"%s\" in %.1fs"), *Command, Seconds);
	}

	static FAutoConsoleCommandWithWorldAndArgs DelayCommand(
		TEXT("PG.Delay"),
		TEXT("Runs a console command after N seconds. Args: <seconds> <command...>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Delay));

	// PG.WigFire — 가발 광선을 버튼 없이 한 번 쏜다(가발이 있어야 한다). 화면 없는 시험에서 관통·폭발 로그를 보려고(9/23).
	static void WigFire(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		UPGWigBeamComponent* Beam = IsValid(Pawn) ? Pawn->FindComponentByClass<UPGWigBeamComponent>() : nullptr;
		if (!Beam)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.WigFire: player has no wig beam part"));
			return;
		}
		UE_LOG(LogPGObjects, Display, TEXT("PG.WigFire: wig worn=%d"), Beam->IsWigWorn() ? 1 : 0);
		Beam->RequestFire();
	}

	static FAutoConsoleCommandWithWorldAndArgs WigFireCommand(
		TEXT("PG.WigFire"),
		TEXT("Test: fires the pink wig beam once without a mouse button (needs the wig)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&WigFire));

	// PG.GoTo boss|robot|vehicle|monster — 맵이 넓어 찾기 어려운 것 앞으로 플레이어(또는 타고 있는 것)를 옮긴다.
	static void GoTo(const TArray<FString>& Args, UWorld* World)
	{
		APawn* Pawn = GetPlayerPawn(World);
		if (!IsValid(Pawn) || Args.Num() == 0)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.GoTo boss|robot|vehicle|monster|box|item|booth|exchange"));
			return;
		}
		const FString What = Args[0].ToLower();
		// 가장 가까운 것을 고른다(첫 번째가 아니라). 상자·바닥 아이템은 수십 개라 가까운 게 의미 있다.
		AActor* Target = nullptr;
		double BestDist = TNumericLimits<double>::Max();
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (Actor == Pawn)
				continue;
			bool bMatch = false;
			if (What == TEXT("boss") || What == TEXT("robot"))
			{
				if (const APGRobotCharacter* Robot = Cast<APGRobotCharacter>(Actor))
					bMatch = (What == TEXT("boss")) == !Robot->IsRideable();
			}
			else if (What == TEXT("vehicle"))
				bMatch = Actor->IsA<APGVehiclePawn>();
			else if (What == TEXT("monster"))
				bMatch = Actor->IsA<APGMonsterCharacter>() && !Actor->IsA<APGRobotCharacter>();
			else if (What == TEXT("box"))
				bMatch = Actor->IsA<AItemContainerActor>();
			else if (What == TEXT("item"))
				bMatch = Actor->IsA<APGFloorItemActor>();
			else if (What == TEXT("booth"))
				bMatch = Actor->IsA<APGBoothActor>();
			else if (What == TEXT("exchange"))
				bMatch = Actor->IsA<APGBoothActor>() && Cast<APGBoothActor>(Actor)->GetBoothKind() == EPGBoothKind::Exchange;
			if (!bMatch)
				continue;
			const double Dist = FVector::DistSquared(Actor->GetActorLocation(), Pawn->GetActorLocation());
			if (Dist < BestDist)
			{
				BestDist = Dist;
				Target = Actor;
			}
		}
		if (!IsValid(Target))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.GoTo: no %s found"), *What);
			return;
		}
		// 거래소 부스: 창구 바로 앞(부스 정면 +X 로 2.5m) — 보호막 안이라 들어오는 안내가 뜬다(9/23).
		if (const APGBoothActor* Booth = Cast<APGBoothActor>(Target))
		{
			const FVector Front = Booth->GetActorLocation() + Booth->GetActorForwardVector() * 250.0f + FVector(0.0f, 0.0f, 120.0f);
			const FRotator FaceBooth(0.0f, (-Booth->GetActorForwardVector()).Rotation().Yaw, 0.0f);
			Pawn->SetActorLocationAndRotation(Front, FaceBooth, false, nullptr, ETeleportType::TeleportPhysics);
			if (AController* C = Pawn->GetController())
				C->SetControlRotation(FaceBooth);
			UE_LOG(LogPGObjects, Display, TEXT("PG.GoTo: moved in front of %s at %s"), *Booth->GetName(), *Booth->GetActorLocation().ToCompactString());
			return;
		}
		// 대상에서 15m 떨어진 곳, 대상을 바라보게. 지면 높이는 대상과 같게.
		const FVector Away = (Pawn->GetActorLocation() - Target->GetActorLocation()).GetSafeNormal2D();
		const FVector Dir = Away.IsNearlyZero() ? FVector::ForwardVector : Away;
		// 상자·아이템은 작아서 3m 앞, 나머지는 15m.
		const bool bSmall = Target->IsA<AItemContainerActor>() || Target->IsA<APGFloorItemActor>();
		FVector Location = Target->GetActorLocation() + Dir * (bSmall ? 300.0f : 1500.0f);
		Location.Z = Target->GetActorLocation().Z + 150.0f;
		const FRotator Facing = (-Dir).Rotation();
		Pawn->SetActorLocationAndRotation(Location, FRotator(0.0f, Facing.Yaw, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		if (AController* C = Pawn->GetController())
			C->SetControlRotation(FRotator(0.0f, Facing.Yaw, 0.0f));
		UE_LOG(LogPGObjects, Display, TEXT("PG.GoTo: moved to %s at %s"), *Target->GetName(), *Target->GetActorLocation().ToCompactString());
	}

	// PG.ShowFactions [seconds] — 모든 몬스터 머리 위에 세력 글자(A 초록 / B 노랑 / C 빨강)를 잠시 띄운다. 세력전 확인용.
	static void ShowFactions(const TArray<FString>& Args, UWorld* World)
	{
		const float Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 15.0f;
		int32 Shown = 0;
		for (TActorIterator<APGMonsterCharacter> It(World); It; ++It)
		{
			const APGMonsterCharacter* Monster = *It;
			if (Monster->IsDead())
				continue;
			const TCHAR* Label = TEXT("-");
			FColor Color = FColor::White;
			switch (Monster->Faction)
			{
			case EPGMonsterFaction::A: Label = TEXT("A"); Color = FColor::Green;  break;
			case EPGMonsterFaction::B: Label = TEXT("B"); Color = FColor::Yellow; break;
			case EPGMonsterFaction::C: Label = TEXT("C"); Color = FColor::Red;    break;
			default: break;
			}
			const FVector Top = Monster->GetActorLocation() + FVector(0.0f, 0.0f, Monster->GetSimpleCollisionHalfHeight() + 60.0f);
			DrawDebugString(World, Top, FString::Printf(TEXT("%s  hp %.0f"), Label, Monster->GetHealth()), nullptr, Color, Seconds, true, 1.5f);
			++Shown;
		}
		UE_LOG(LogPGObjects, Display, TEXT("PG.ShowFactions: %d monsters labeled for %.0fs"), Shown, Seconds);
	}

	// 아이템 기본가치 표를 로그로 찍는다. 값을 바꾼 뒤 "층이 제대로 졌는지"를 눈으로 보는 자리다.
	// 임시 UI 를 만들지 않는 이유: 나중에 버릴 위젯이 시스템을 물고 늘어진다. 로그는 버릴 것이 없다.
	//   PG.ItemValues            표만
	//   PG.ItemValues 12345      그 시드의 상점 구매가·판매가까지
	static void ItemValues(const TArray<FString>& Args, UWorld* World)
	{
		const bool bWithPrices = Args.Num() > 0;
		const int64 ShopSeed = bWithPrices ? FCString::Atoi64(*Args[0]) : 0;

		TArray<FPGItemValueRow> Rows = UPGItemValueLibrary::GetAllRows();
		Rows.Sort([](const FPGItemValueRow& A, const FPGItemValueRow& B) { return A.BaseValue > B.BaseValue; });

		UE_LOG(LogPGObjects, Display, TEXT("PG.ItemValues: %d rows (기준: 재화 Money 한 개 = 1)%s"),
			Rows.Num(), bWithPrices ? *FString::Printf(TEXT(" shopSeed=%lld"), ShopSeed) : TEXT(""));
		for (const FPGItemValueRow& Row : Rows)
		{
			const FString Grade = StaticEnum<EPGItemGrade>()->GetNameStringByValue(static_cast<int64>(Row.Grade));
			FString Prices;
			if (bWithPrices && Row.bTradable)
				Prices = FString::Printf(TEXT(" 살때 %d / 팔때 %d"),
					UPGItemValueLibrary::GetBuyPrice(Row.ItemId, ShopSeed), UPGItemValueLibrary::GetSellPrice(Row.ItemId, ShopSeed));
			UE_LOG(LogPGObjects, Display, TEXT("  %-16s %7d  %-9s%s%s  | %s"),
				*Row.ItemId.ToString(), Row.BaseValue, *Grade,
				Row.bTradable ? TEXT("") : TEXT(" [거래금지]"), *Prices, *Row.Reason);
		}

		// 루팅 테이블 기대가치. 값 하나를 고치면 어느 상자가 비싸지는지가 여기서 드러난다 — 표를 검산하는 진짜 자리.
		// 가중치 비례 평균 x 굴림 수로 계산한다(전 테이블이 bAllowDuplicates=true 라 굴림마다 후보가 같다).
		UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(World);
		if (!Spawner)
			return;
		if (Spawner->GetCatalogCount() == 0)
			PGObjectSmokeTest::RegisterDefaultCatalog(*Spawner);

		TArray<FName> TableIds;
		Spawner->GetAllLootTableIds(TableIds);
		struct FRow { FName Id; double Expected; int32 Min; int32 Max; };
		TArray<FRow> Table;
		for (const FName& TableId : TableIds)
		{
			const FPGLootTableRow* Loot = Spawner->FindLootTable(TableId);
			if (!Loot || Loot->Entries.Num() == 0)
				continue;
			double TotalWeight = 0.0;
			for (const FPGLootEntry& Entry : Loot->Entries)
				TotalWeight += FMath::Max(0.0f, Entry.Weight);
			if (TotalWeight <= 0.0)
				continue;
			double Expected = 0.0;
			int32 Min = MAX_int32;
			int32 Max = 0;
			for (const FPGLootEntry& Entry : Loot->Entries)
			{
				const int32 Value = UPGItemValueLibrary::GetItemValue(Entry.ItemId);
				Expected += (FMath::Max(0.0f, Entry.Weight) / TotalWeight) * Value * (Entry.MinCount + Entry.MaxCount) * 0.5;
				Min = FMath::Min(Min, Value * Entry.MinCount);
				Max = FMath::Max(Max, Value * Entry.MaxCount);
			}
			Table.Add({ TableId, Expected * Loot->RollCount, Min * Loot->RollCount, Max * Loot->RollCount });
		}
		Table.Sort([](const FRow& A, const FRow& B) { return A.Expected < B.Expected; });

		UE_LOG(LogPGObjects, Display, TEXT("PG.ItemValues: 루팅 테이블 기대가치 (싼 것부터)"));
		for (const FRow& Row : Table)
			UE_LOG(LogPGObjects, Display, TEXT("  %-14s 기대 %8.0f   (최소 %d ~ 최대 %d)"),
				*Row.Id.ToString(), Row.Expected, Row.Min, Row.Max);
	}

	static FAutoConsoleCommandWithWorldAndArgs ShowFactionsCommand(
		TEXT("PG.ShowFactions"),
		TEXT("Draws faction letter (A/B/C) and hp above every monster. Arg: seconds (default 15)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ShowFactions));

	static FAutoConsoleCommandWithWorldAndArgs GoToCommand(
		TEXT("PG.GoTo"),
		TEXT("Teleports the player next to the nearest matching actor. Arg: boss, robot, vehicle, monster, box, item."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GoTo));

	static FAutoConsoleCommandWithWorldAndArgs GiveItemCommand(
		TEXT("PG.GiveItem"),
		TEXT("Gives an item to the player pawn through IPGItemReceiver. Args: ItemId [Count]."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GiveItem));

	static FAutoConsoleCommandWithWorldAndArgs ItemValuesCommand(
		TEXT("PG.ItemValues"),
		TEXT("Logs the item base-value/grade table and each loot table's expected value. Arg: shop seed (optional, adds buy/sell prices)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ItemValues));
}
