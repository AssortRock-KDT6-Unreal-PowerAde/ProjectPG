#include "Combat/PGCombatSpawner.h"
#include "Combat/PGCombatSettings.h"
#include "Common/PGVisualSettings.h"
#include "Components/CapsuleComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Finale/PGFinaleDirector.h"

#include "Actors/WarZoneFootprintPreview.h"
#include "Combat/PGMonsterRespawner.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Monster/PGMonsterCharacter.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectTypes.h"
#include "Robot/PGRobotCharacter.h"
#include "Vehicle/PGTankPawn.h"
#include "Vehicle/PGVehiclePawn.h"
#include "Combat/PGMonsterLookComponent.h"

// 흐름: 포인트 분류 → 워존 3등분(세력 구역) → 보스 → 탈것 로봇 → 몬스터(+리스폰 기록) → 차량.
// 각 단계는 FSpawnContext 하나를 받아 자기 몫만 놓고 놓은 수를 돌려준다. 순서가 중요하다:
// 보스·탈것이 먼저 자리를 잡고(Used), 몬스터는 남은 자리에서 뽑는다.
namespace
{
	constexpr ESpawnActorCollisionHandlingMethod Handling = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	struct FSpawnContext
	{
		UWorld* World = nullptr;
		const UPGCombatSettings* Settings = nullptr; // 9/26: 전투 설정(ProjectPG Combat)
		const TArray<FLevelDesignPoint>* Points = nullptr;
		FRandomStream Stream;

		TArray<int32> SpawnPoints;   // 플레이어 스폰 포인트
		TArray<int32> AiPoints;      // AISpawn 포인트 전부
		TArray<int32> ByDistance;    // AISpawn 을 플레이어 스폰에서 먼 순으로
		TSet<int32> Used;            // 이미 무언가를 놓은 포인트
		TMap<int32, EPGMonsterFaction> SectorFaction; // 워존 포인트 → 구역 세력
		int32 BossIndex = INDEX_NONE;

		const FLevelDesignPoint& Point(int32 Index) const { return (*Points)[Index]; }
		bool IsWarZone(int32 Index) const { return Point(Index).ArchetypeId == TEXT("ScavPatrol"); }

		float DistanceToNearestSpawn(const FVector& Location) const
		{
			float Best = 0.0f;
			bool bAny = false;
			for (int32 Index : SpawnPoints)
			{
				const float D = FVector::Dist2D(Location, Point(Index).WorldLocation);
				if (!bAny || D < Best) { Best = D; bAny = true; }
			}
			return bAny ? Best : 0.0f;
		}

		void Shuffle(TArray<int32>& Arr)
		{
			for (int32 I = Arr.Num() - 1; I > 0; --I)
				Arr.Swap(I, Stream.RandRange(0, I));
		}
	};

	// 포인트는 바닥 + 120cm 에 떠 있다. 발이 땅에 닿도록 바닥을 다시 찾고 캡슐 높이만큼 띄운다.
	bool SnapToGround(UWorld* World, FVector& InOutLocation, float Lift)
	{
		const FVector Start = InOutLocation + FVector(0.0f, 0.0f, 300.0f);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGCombatSpawnGround), false);
		if (!World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0.0f, 0.0f, 3000.0f), ECC_Visibility, Params))
			return false;
		InOutLocation.Z = Hit.ImpactPoint.Z + Lift;
		return true;
	}

	FTransform MakeTransform(const FVector& Location, FRandomStream& Stream)
	{
		return FTransform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), Location);
	}

	// 세력별 몸집 배율(설정). 스폰 전에 바닥 띄우기 높이를 정할 때도 써야 해서 따로 뺐다.
	float FactionScale(EPGMonsterFaction Faction)
	{
		const UPGCombatSettings* S = GetDefault<UPGCombatSettings>();
		if (Faction == EPGMonsterFaction::B) return FMath::Clamp(S->FactionBScale, 0.5f, 12.0f);
		if (Faction == EPGMonsterFaction::C) return FMath::Clamp(S->FactionCScale, 0.5f, 12.0f);
		return 1.0f;
	}

	// 세력별 손맛. A 는 약하고 많고, B 는 크고, C 는 단단하고 귀한 걸 떨군다.
	void ApplyFactionTuning(APGMonsterCharacter* Monster, EPGMonsterFaction Faction, FTransform& Transform)
	{
		switch (Faction)
		{
		case EPGMonsterFaction::A:
			Monster->MaxHealth *= 0.7f;
			Monster->AttackDamage *= 0.7f;
			Monster->LootTableId = TEXT("LT_CorpseA");
			break;
		case EPGMonsterFaction::B:
			Monster->LootTableId = TEXT("LT_CorpseB");
			// 20m 는 5배 몸집에 비해 짧아서, 건물 뒤로 잠깐 숨으면 바로 놓쳤다. 35m 안이면 안 보여도 계속 쫓는다.
			Monster->AggroKeepRadius = 3500.0f;
			// 워존 자기 구역을 지키는 크리처: 좁게 보고, 멀리 끌려 나가면 돌아간다.
			Monster->SightRadiusOverride = GetDefault<UPGCombatSettings>()->FactionBSightRadius;
			Monster->LeashRadius = GetDefault<UPGCombatSettings>()->FactionBLeashRadius;
			break;
		case EPGMonsterFaction::C:
			Monster->MaxHealth *= 3.0f;
			Monster->AttackDamage *= 1.6f;
			Monster->LootTableId = TEXT("LT_CorpseC");
			Monster->AggroKeepRadius = 3000.0f;
			break;
		default:
			break;
		}
		Monster->Faction = Faction;

		// 몸집: 액터 스케일은 메시·캡슐만 키운다. 발 높이·속도는 안 커져서(사거리는 몬스터 BeginPlay 가 프리셋 값 × 몸집으로 맞춘다) 큰 몸으로 종종걸음에 돌부리에 걸린다(보스 로봇과 같은 처리).
		const float Scale = FactionScale(Faction);
		if (!FMath::IsNearlyEqual(Scale, 1.0f))
		{
			Transform.SetScale3D(FVector(Scale));
			Monster->WakeRange *= FMath::Sqrt(Scale);
			Monster->GetCharacterMovement()->MaxWalkSpeed *= FMath::Sqrt(Scale);
			// ×0.6: 몸집 비율보다 낮게. 높으면 건물·차 위를 공중부양하듯 넘어 다닌다. 걸리는 소품은 밀쳐낸다(3배 이상 자동).
			Monster->GetCharacterMovement()->MaxStepHeight = 45.0f * Scale * 0.6f;
			Monster->GetCharacterMovement()->SetWalkableFloorAngle(55.0f);
		}
	}

	// ---- 1) 포인트 분류. AISpawn 이 하나도 없으면 false.
	bool CollectPoints(FSpawnContext& C)
	{
		for (int32 Index = 0; Index < C.Points->Num(); ++Index)
		{
			const ELevelDesignPointType Type = C.Point(Index).Type;
			if (Type == ELevelDesignPointType::Spawn)
				C.SpawnPoints.Add(Index);
			else if (Type == ELevelDesignPointType::AISpawn)
				C.AiPoints.Add(Index);
		}
		if (C.AiPoints.Num() == 0)
			return false;
		// 플레이어 스폰에서 먼 순서. 탈것 로봇은 중간쯤, 워존이 없을 때의 보스는 제일 먼 곳.
		C.ByDistance = C.AiPoints;
		C.ByDistance.Sort([&C](int32 A, int32 B)
		{
			return C.DistanceToNearestSpawn(C.Point(A).WorldLocation) > C.DistanceToNearestSpawn(C.Point(B).WorldLocation);
		});
		return true;
	}

	// ---- 2) 워존 3등분: 중심에서 본 각도로 120도씩 잘라 구역마다 세력 하나. 시드로 어느 구역이 A/B/C 인지 돌린다.
	//      A 구역 = 작은 몬스터 떼, B 구역 = 크리처, C 구역 = 보스 로봇(+FactionCCount 마리).
	//      보스 자리도 여기서 정한다: C 구역 안에서 워존 중심에 가장 가까운 포인트. 워존이 없으면 플레이어에서 가장 먼 포인트.
	void AssignWarZoneSectors(FSpawnContext& C)
	{
		TArray<int32> WarZonePoints;
		FVector Centroid = FVector::ZeroVector;
		for (int32 Index : C.AiPoints)
		{
			if (C.IsWarZone(Index))
			{
				WarZonePoints.Add(Index);
				Centroid += C.Point(Index).WorldLocation;
			}
		}
		if (WarZonePoints.Num() == 0)
		{
			if (C.ByDistance.Num() > 0)
				C.BossIndex = C.ByDistance[0];
			UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: no warzone ai points, boss rule=farthest from spawn"));
			return;
		}

		Centroid /= WarZonePoints.Num();
		const float AngleOffset = C.Stream.FRandRange(0.0f, 360.0f);
		EPGMonsterFaction Order[3] = { EPGMonsterFaction::A, EPGMonsterFaction::B, EPGMonsterFaction::C };
		for (int32 I = 2; I > 0; --I)
			Swap(Order[I], Order[C.Stream.RandRange(0, I)]);

		float Best = FLT_MAX;
		for (int32 Index : WarZonePoints)
		{
			const FVector Rel = C.Point(Index).WorldLocation - Centroid;
			float Angle = FMath::RadiansToDegrees(FMath::Atan2(Rel.Y, Rel.X)) - AngleOffset;
			Angle = FMath::Fmod(Angle + 720.0f, 360.0f);
			const EPGMonsterFaction Faction = Order[FMath::Clamp(static_cast<int32>(Angle / 120.0f), 0, 2)];
			C.SectorFaction.Add(Index, Faction);
		}
		// 보스 자리: C 구역 안에서 다른 세력 포인트와 가장 멀리 떨어진 곳. 예전엔 "워존 중심에 가장 가까운 C 포인트"라
		// 세 구역이 만나는 한가운데라서 보스가 작은 몹 떼 바로 옆에 붙어 있었다(9/20 PIE).
		for (const TPair<int32, EPGMonsterFaction>& Entry : C.SectorFaction)
		{
			if (Entry.Value != EPGMonsterFaction::C)
				continue;
			float NearestOther = FLT_MAX;
			for (const TPair<int32, EPGMonsterFaction>& Other : C.SectorFaction)
				if (Other.Value != EPGMonsterFaction::C)
					NearestOther = FMath::Min(NearestOther, FVector::Dist2D(C.Point(Entry.Key).WorldLocation, C.Point(Other.Key).WorldLocation));
			const float Score = -NearestOther; // 멀수록 좋다
			if (Score < Best) { Best = Score; C.BossIndex = Entry.Key; }
		}
		UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: warzone ai points=%d, boss rule=sector C farthest from other factions"), WarZonePoints.Num());
	}

	// 보스·탈것을 리스폰 기록부에 적는다. 몬스터와 같은 폴링으로 되살아난다.
	void RecordRobot(APGMonsterRespawner* Respawner, APGRobotCharacter* Robot, const FVector& Location, float Yaw, bool bBoss, float Scale, EPGMonsterFaction Faction, float Seconds)
	{
		if (!IsValid(Respawner))
			return;
		FPGRobotRespawnRecord Record;
		Record.Location = Location;
		Record.Yaw = Yaw;
		Record.bBoss = bBoss;
		Record.Scale = Scale;
		Record.Faction = Faction;
		Record.RespawnSeconds = Seconds;
		Record.Robot = Robot;
		Respawner->AddRobot(Record);
	}

	// ---- 3) 보스 로봇
	int32 PlaceBoss(FSpawnContext& C, APGMonsterRespawner* Respawner)
	{
		if (!C.Settings->bSpawnBoss || C.BossIndex == INDEX_NONE)
			return 0;
		const FVector Location = C.Point(C.BossIndex).WorldLocation;
		const float Scale = FMath::Clamp(C.Settings->BossScale, 1.0f, 12.0f);
		const float Yaw = C.Stream.FRandRange(0.0f, 360.0f);
		APGRobotCharacter* Boss = PGCombatSpawner::SpawnRobot(C.World, Location, Yaw, true, Scale, C.Settings->BossFaction);
		if (!IsValid(Boss))
			return 0;
		C.Used.Add(C.BossIndex);
		RecordRobot(Respawner, Boss, Location, Yaw, true, Scale, C.Settings->BossFaction, C.Settings->BossRespawnSeconds);
		UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: boss at %s (%.0fm from player spawn)"),
			*Boss->GetActorLocation().ToCompactString(), C.DistanceToNearestSpawn(Location) / 100.0f);
		// 보스가 있으면 피날레가 가능하다. 전함을 지금(판 만드는 동안) 미리 만들어 산 너머에 숨겨 둔다 —
		// 보스가 죽은 뒤에 만들면 그 순간 화면이 한 번 끊긴다. 피날레를 끄면(bEnableFinale) 이 호출은 그냥 돌아간다.
		APGFinaleDirector::Prewarm(C.World, Location);
		return 1;
	}

	// ---- 4) 탈것 로봇: 플레이어 스폰에서 중간 거리부터
	int32 PlaceRideableRobots(FSpawnContext& C, APGMonsterRespawner* Respawner)
	{
		int32 Placed = 0;
		// 가운데 거리의 포인트부터 차례로 시도한다. 트인 땅이 없는 포인트(옥상 등)는 SpawnRobot 이 거절하므로 다음 포인트로 넘어간다.
		for (int32 Try = 0; Try < C.ByDistance.Num() && Placed < C.Settings->RideableRobotCount; ++Try)
		{
			const int32 Index = C.ByDistance[(C.ByDistance.Num() / 2 + Try) % C.ByDistance.Num()];
			if (C.Used.Contains(Index))
				continue;
			const FVector Location = C.Point(Index).WorldLocation;
			const float Scale = FMath::Max(1.0f, C.Settings->RideableRobotScale);
			const float Yaw = C.Stream.FRandRange(0.0f, 360.0f);
			APGRobotCharacter* Robot = PGCombatSpawner::SpawnRobot(C.World, Location, Yaw, false, Scale, EPGMonsterFaction::None);
			if (!IsValid(Robot))
				continue;
			C.Used.Add(Index);
			RecordRobot(Respawner, Robot, Location, Yaw, false, Scale, EPGMonsterFaction::None, C.Settings->RideableRobotRespawnSeconds);
			++Placed;
		}
		return Placed;
	}

	// ---- 5) 몬스터: 남은 AISpawn 을 시드로 섞어 구역별 정원만큼. 세력 A·B 는 리스폰 기록부에 적는다.
	//      구역별 정원: A 구역 WarZoneMonsterCount, B 구역 FactionBCount, C 구역 FactionCCount. 워존 밖은 전부 A (MonsterCount).
	int32 PlaceMonsters(FSpawnContext& C, APGMonsterRespawner* Respawner, int32 OutCounts[4])
	{
		const UPGCombatSettings* S = C.Settings;
		TArray<int32> WarZoneRemaining;
		TArray<int32> OuterRemaining;
		for (int32 Index : C.AiPoints)
		{
			if (C.Used.Contains(Index))
				continue;
			(C.IsWarZone(Index) ? WarZoneRemaining : OuterRemaining).Add(Index);
		}
		C.Shuffle(WarZoneRemaining);
		C.Shuffle(OuterRemaining);

		TArray<TPair<int32, EPGMonsterFaction>> Picks;
		int32 Quota[4] = { 0, S->WarZoneMonsterCount, S->FactionBCount, S->FactionCCount };
		for (int32 Index : WarZoneRemaining)
		{
			const EPGMonsterFaction* Sector = C.SectorFaction.Find(Index);
			const EPGMonsterFaction Faction = Sector ? *Sector : EPGMonsterFaction::A;
			int32& Left = Quota[static_cast<int32>(Faction)];
			if (Left <= 0)
				continue;
			--Left;
			Picks.Emplace(Index, Faction);
		}
		for (int32 I = 0; I < FMath::Min(S->MonsterCount, OuterRemaining.Num()); ++I)
			Picks.Emplace(OuterRemaining[I], EPGMonsterFaction::A);

		UClass* MonsterClass = S->MonsterClass ? S->MonsterClass.Get() : APGMonsterCharacter::StaticClass();
		auto PresetFor = [&](EPGMonsterFaction Faction) -> FName
		{
			const TArray<FName>* List = &S->MonsterPresets;
			if (Faction == EPGMonsterFaction::A && S->FactionAPresets.Num() > 0) List = &S->FactionAPresets;
			if (Faction == EPGMonsterFaction::B && S->FactionBPresets.Num() > 0) List = &S->FactionBPresets;
			if (Faction == EPGMonsterFaction::C && S->FactionCPresets.Num() > 0) List = &S->FactionCPresets;
			return List->Num() > 0 ? (*List)[C.Stream.RandRange(0, List->Num() - 1)] : NAME_None;
		};
		auto RespawnSecondsFor = [&](EPGMonsterFaction Faction)
		{
			if (Faction == EPGMonsterFaction::A) return S->FactionARespawnSeconds;
			if (Faction == EPGMonsterFaction::B) return S->FactionBRespawnSeconds;
			return 0.0f;
		};

		if (IsValid(Respawner))
		{
			Respawner->Configure(MonsterClass, S->RespawnPlayerClearRadius);
			// 몬스터 정원에서 남은 AI 포인트 = 로봇 리스폰 후보 자리.
			TArray<FVector> Spare;
			for (int32 I = S->MonsterCount; I < OuterRemaining.Num(); ++I)
				Spare.Add(C.Point(OuterRemaining[I]).WorldLocation);
			for (int32 Index : WarZoneRemaining)
				if (!Picks.ContainsByPredicate([Index](const TPair<int32, EPGMonsterFaction>& P) { return P.Key == Index; }))
					Spare.Add(C.Point(Index).WorldLocation);
			Respawner->SetSpareSpots(Spare);
		}

		int32 Placed = 0;
		for (const TPair<int32, EPGMonsterFaction>& Pick : Picks)
		{
			const FName Preset = PresetFor(Pick.Value);
			const float Yaw = C.Stream.FRandRange(0.0f, 360.0f);
			APGMonsterCharacter* Monster = PGCombatSpawner::SpawnMonster(C.World, MonsterClass, C.Point(Pick.Key).WorldLocation, Yaw, Preset, Pick.Value);
			if (!IsValid(Monster))
				continue;
			++Placed;
			++OutCounts[static_cast<int32>(Pick.Value)];
			if (IsValid(Respawner))
			{
				FPGMonsterRespawnRecord Record;
				Record.Location = C.Point(Pick.Key).WorldLocation;
				Record.Yaw = Yaw;
				Record.Preset = Preset;
				Record.Faction = Pick.Value;
				Record.RespawnSeconds = RespawnSecondsFor(Pick.Value);
				Record.Monster = Monster;
				Respawner->Add(Record);
			}
		}
		return Placed;
	}

	// ---- 6) 차량: 플레이어 스폰 옆. 8방향을 돌며 차 크기(약 5m×2.4m)의 상자가 안 겹치는 자리를 고른다(벽에 끼워 넣지 않게).
	int32 PlaceVehicles(FSpawnContext& C)
	{
		int32 Placed = 0;
		// 차보다 사방 60cm 여유. 벽 판자에 스치기만 해도 스폰 순간 물리가 튕겨 뒤집힌다.
		const FCollisionShape CarBox = FCollisionShape::MakeBox(FVector(320.0f, 190.0f, 110.0f));
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGVehicleSpawnClear), false);
		// Vehicle 채널 응답이 아니라 "거기 정적·동적 물체가 있나"로 본다. 시설 벽(패킹 HISM)은 Vehicle 채널을 안 막아 검사를 통과했었다.
		FCollisionObjectQueryParams Solid;
		Solid.AddObjectTypesToQuery(ECC_WorldStatic);
		Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
		Solid.AddObjectTypesToQuery(ECC_PhysicsBody);
		Solid.AddObjectTypesToQuery(ECC_Vehicle);
		Solid.AddObjectTypesToQuery(ECC_Pawn);
		// 멀티(9/27): 시작 지역(같은 칸의 자리 묶음)마다 VehicleCount 대씩. 예전에는 자리 목록 앞에서부터 돌아 차 2대가 모두
		//   0번 지역(첫 시작 칸)에만 섰다 — 다른 지역에서 나온 사람은 차가 없었다. 지역 순서는 자리 목록 순서(0번 지역이 먼저)를 따른다.
		TArray<FIntPoint> RegionCells;
		TArray<TArray<int32>> RegionSeats;
		for (const int32 SeatIndex : C.SpawnPoints)
		{
			// 사람이 출발하는 시작 지역 자리(PlayerSquad)만. 호숫가 마을 선착장(BoatLanding)도 Spawn 지점이지만 예전부터 차를 두지 않았다.
			if (C.Point(SeatIndex).ArchetypeId != FName(TEXT("PlayerSquad")))
				continue;
			const FIntPoint Cell = C.Point(SeatIndex).GridCell;
			int32 Region = RegionCells.IndexOfByKey(Cell);
			if (Region == INDEX_NONE)
			{
				Region = RegionCells.Add(Cell);
				RegionSeats.AddDefaulted();
			}
			RegionSeats[Region].Add(SeatIndex);
		}
		TArray<int32> VehicleOrigins;
		for (const TArray<int32>& Seats : RegionSeats)
			for (int32 V = 0; V < C.Settings->VehicleCount; ++V)
				VehicleOrigins.Add(Seats[V % Seats.Num()]);
		for (const int32 OriginSeat : VehicleOrigins)
		{
			const FVector Origin = C.Point(OriginSeat).WorldLocation;
			const float StartAngle = C.Stream.FRandRange(0.0f, 360.0f);
			FVector OriginGround = Origin;
			const float OriginGroundZ = SnapToGround(C.World, OriginGround, 0.0f) ? OriginGround.Z : Origin.Z;
			bool bFound = false;
			FVector Location = Origin;
			float Yaw = 0.0f;
			for (int32 Step = 0; Step < 8 && !bFound; ++Step)
			{
				Yaw = StartAngle + Step * 45.0f;
				FVector Candidate = Origin + FRotator(0.0f, Yaw, 0.0f).Vector() * 700.0f;
				if (!SnapToGround(C.World, Candidate, 110.0f))
					continue;
				// 땅 찾기 선이 담·컨테이너 꼭대기에 맞으면 그 높이를 땅으로 읽는다. 그러면 아래 겹침 검사는 담 "위" 빈 공간만 보고 통과해서
				// 차가 담 위에 나타났다가 떨어지며 담을 부쉈다(9/22 PIE: "차가 스폰 지역에 벽 위로 스폰돼 벽을 부숴 버리네").
				// 스폰 지점 바닥과 60cm 넘게 차이 나면 그 자리는 버린다(연석·도로 턱은 10~45cm).
				// (Candidate.Z 는 땅 + 110cm 로 띄운 값이라 110 을 빼서 비교한다. 스폰 지점 좌표는 땅보다 1.2m 위에 찍혀 있어서
				//  그대로 비교하면 모든 자리가 버려졌다(차 0대) — 스폰 지점 아래 땅을 따로 재서 비교한다.)
				if (FMath::Abs((Candidate.Z - 110.0f) - OriginGroundZ) > 80.0f)
					continue;
				// 검사 상자는 바닥에서 50cm 띄운다. 바닥도 WorldStatic 이라 상자 밑면이 땅에 닿으면 8방향 전부 "막힘"이 되어 차가 하나도 안 나왔다.
				// 50cm 는 발 높이(45cm)보다 살짝 위라 돌부리·연석은 무시하고 벽·바위·건물만 걸린다.
				const FVector TestCenter = Candidate + FVector(0.0f, 0.0f, 50.0f + CarBox.GetExtent().Z - 110.0f);
				if (!C.World->OverlapAnyTestByObjectType(TestCenter, FRotator(0.0f, Yaw, 0.0f).Quaternion(), Solid, CarBox, Params))
				{
					Location = Candidate;
					bFound = true;
				}
			}
			if (!bFound)
			{
				UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: no clear spot for vehicle near %s"), *Origin.ToCompactString());
				continue;
			}
			const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Location);
			APGVehiclePawn* Vehicle = C.World->SpawnActorDeferred<APGVehiclePawn>(UPGVisualSettings::VehicleSpawnClass(), Transform, nullptr, nullptr, Handling);
			if (!IsValid(Vehicle))
				continue;
			if (C.Settings->VehiclePresets.Num() > 0)
			{
				const FName Preset = C.Settings->VehiclePresets[C.Stream.RandRange(0, C.Settings->VehiclePresets.Num() - 1)];
				FString MeshPath;
				if (Preset != TEXT("SportsCar") && APGVehiclePawn::GetPresetMeshPath(Preset, MeshPath))
					if (USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath))
						Vehicle->SetVehicleMesh(Mesh);
			}
			Vehicle->FinishSpawning(Transform);
			// 무엇으로 세웠는지(겉모습을 블루프린트로 옮기기 전·후 비교용, 9/23).
			UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: vehicle %s class=%s mesh=%s"), *Location.ToCompactString(),
				*Vehicle->GetClass()->GetName(), *GetNameSafe(Vehicle->GetMesh() ? Vehicle->GetMesh()->GetSkeletalMeshAsset() : nullptr));
			++Placed;
		}
		return Placed;
	}
}

namespace
{
	// ---- 7) 숨겨 둔 탱크: 시작 지역마다 첫 자리에서 맵 바깥쪽(스폰 지역 벽 너머)으로 15~35m, 좌우로 틀어 가며 빈 자리를 찾는다.
	// 맵 가운데 = 모든 포인트의 평균. 탱크는 맵 쪽을 보고 선다(찾은 사람이 바로 타고 들어가게).
	// 멀티(9/27): 예전에는 자리 목록의 첫 자리(0번 지역) 뒤에만 한 대 — 다른 지역에서 나온 사람은 숨은 탱크가 없었다. 지역마다 한 대.
	int32 PlaceHiddenTankNear(FSpawnContext& C, const FVector& Center, const FVector& Origin);
	int32 PlaceHiddenTank(FSpawnContext& C)
	{
		if (!C.Settings->bSpawnHiddenTank || C.SpawnPoints.Num() == 0)
			return 0;
		FVector Center = FVector::ZeroVector;
		for (const FLevelDesignPoint& P : *C.Points)
			Center += P.WorldLocation;
		Center /= static_cast<float>(FMath::Max(1, C.Points->Num()));
		// 지역(칸)마다 첫 자리. 사람 시작 자리(PlayerSquad)만 — 차 배치(PlaceVehicles)와 같은 기준.
		TArray<FIntPoint> RegionCells;
		int32 Placed = 0;
		for (const int32 SeatIndex : C.SpawnPoints)
		{
			if (C.Point(SeatIndex).ArchetypeId != FName(TEXT("PlayerSquad")))
				continue;
			const FIntPoint Cell = C.Point(SeatIndex).GridCell;
			if (RegionCells.Contains(Cell))
				continue;
			RegionCells.Add(Cell);
			Placed += PlaceHiddenTankNear(C, Center, C.Point(SeatIndex).WorldLocation);
		}
		// 시작 지역 자리가 하나도 없으면(옛 맵) 예전처럼 첫 자리 뒤에.
		if (RegionCells.IsEmpty())
			Placed += PlaceHiddenTankNear(C, Center, C.Point(C.SpawnPoints[0]).WorldLocation);
		return Placed;
	}

	int32 PlaceHiddenTankNear(FSpawnContext& C, const FVector& Center, const FVector& Origin)
	{
		FVector Outward = (Origin - Center).GetSafeNormal2D();
		if (Outward.IsNearlyZero())
			Outward = FVector::ForwardVector;

		// 셔먼 1.5배 ≈ 9m × 4.5m. 사방 1m 여유, 바닥에서 60cm 띄운 상자로 막힌 곳을 거른다(차와 같은 방식).
		const FCollisionShape TankBox = FCollisionShape::MakeBox(FVector(550.0f, 330.0f, 200.0f));
		FCollisionObjectQueryParams Solid;
		Solid.AddObjectTypesToQuery(ECC_WorldStatic);
		Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
		Solid.AddObjectTypesToQuery(ECC_PhysicsBody);
		Solid.AddObjectTypesToQuery(ECC_Vehicle);
		Solid.AddObjectTypesToQuery(ECC_Pawn);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGTankSpawnClear), false);
		// 마른 땅인가(9/28 사용자: "탱크가 물가에 스폰돼서 움직이질 못한다"). 탱크는 시작 자리에서 맵 바깥쪽으로 15~35m 에 두는데
		//   그쪽이 호숫가일 때가 있다. 호수 바닥·물(땅보다 55~130cm 낮다)은 겹침 검사를 그냥 통과해서 물속에 섰다.
		//   차 배치(PlaceVehicles)처럼 시작 자리 땅 높이와 비교한다 — 탱크는 길어서 가운데와 네 귀퉁이를 다 본다.
		//   한도 20cm(도로 턱 10cm 는 통과): 물 표면은 땅보다 55cm 낮아 80cm 한도로는 걸러지지 않았고, 45cm 로 줄여도
		//   호숫가 경사(땅보다 30cm 낮은 곳)에 섰다(9/28 시험). 물가 경사는 물속으로 이어져 탱크가 미끄러져 들어간다.
		FVector OriginGround = Origin;
		const float OriginGroundZ = SnapToGround(C.World, OriginGround, 0.0f) ? OriginGround.Z : Origin.Z;
		auto IsDryFlat = [&C, OriginGroundZ](const FVector& At, float Yaw)
		{
			const FQuat Turn = FRotator(0.0f, Yaw, 0.0f).Quaternion();
			static const FVector2D Corners[] = { { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, -1.0f }, { -1.0f, 1.0f }, { -1.0f, -1.0f } };
			for (const FVector2D& Corner : Corners)
			{
				FVector Probe = At + Turn.RotateVector(FVector(Corner.X * 450.0f, Corner.Y * 220.0f, 0.0f));
				if (!SnapToGround(C.World, Probe, 0.0f) || FMath::Abs(Probe.Z - OriginGroundZ) > 20.0f)
					return false;
			}
			return true;
		};
		int32 Wet = 0;
		// 바깥쪽부터 보고, 다 막혔으면(호숫가 지역 — 9/28 시험에서 한 지역이 23곳 모두 물가라 탱크가 없었다) 옆·안쪽까지 돌아본다.
		static const float Angles[] = { 0.0f, 25.0f, -25.0f, 50.0f, -50.0f, 75.0f, -75.0f, 105.0f, -105.0f, 135.0f, -135.0f, 165.0f, -165.0f };
		// 1차: 바깥쪽 7방향(앞 7개)을 모든 거리에서. 2차: 나머지 방향.
		for (int32 Pass = 0; Pass < 2; ++Pass)
		for (float Distance = 1500.0f; Distance <= 3500.0f; Distance += 500.0f)
		{
			for (int32 AngleIndex = Pass == 0 ? 0 : 7; AngleIndex < (Pass == 0 ? 7 : UE_ARRAY_COUNT(Angles)); ++AngleIndex)
			{
				const float Angle = Angles[AngleIndex];
				const FVector Dir = FRotator(0.0f, Angle, 0.0f).RotateVector(Outward);
				FVector Candidate = Origin + Dir * Distance;
				if (!SnapToGround(C.World, Candidate, 0.0f))
					continue;
				const float Yaw = (-Dir).Rotation().Yaw;
				if (!IsDryFlat(Candidate, Yaw))
				{
					++Wet;
					continue;
				}
				if (C.World->OverlapAnyTestByObjectType(Candidate + FVector(0.0f, 0.0f, 60.0f + TankBox.GetExtent().Z), FRotator(0.0f, Yaw, 0.0f).Quaternion(), Solid, TankBox, Params))
					continue;
				// 땅 위 3m 에 놓으면 탱크가 스스로 땅에 내려앉는다(APGTankPawn::TickDrive).
				const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Candidate + FVector(0.0f, 0.0f, 300.0f));
				if (APGTankPawn* Tank = C.World->SpawnActor<APGTankPawn>(UPGVisualSettings::TankSpawnClass(), Transform))
				{
					TArray<UStaticMeshComponent*> TankParts;
					Tank->GetComponents(TankParts);
					TArray<FString> TankMeshes;
					for (const UStaticMeshComponent* Part : TankParts)
						if (Part && Part->GetStaticMesh())
							TankMeshes.Add(Part->GetStaticMesh()->GetName());
					UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: tank class=%s meshes=%s"), *Tank->GetClass()->GetName(), *FString::Join(TankMeshes, TEXT(",")));
					UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: hidden tank at %s (%.0fm behind player spawn %s, ground %.0f vs spawn %.0f, %d wet/uneven spots skipped)"),
						*Candidate.ToCompactString(), Distance / 100.0f, *Origin.ToCompactString(), Candidate.Z, OriginGroundZ, Wet);
					return 1;
				}
			}
		}
		UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: no clear spot for hidden tank near %s (%d wet/uneven spots skipped)"), *Origin.ToCompactString(), Wet);
		return 0;
	}
}

APGMonsterCharacter* PGCombatSpawner::SpawnMonster(UWorld* World, UClass* MonsterClass, const FVector& Location, float Yaw, FName Preset, EPGMonsterFaction Faction)
{
	if (!IsValid(World) || !MonsterClass)
		return nullptr;
	FPGMonsterVisuals Visuals;
	const bool bHasPreset = !Preset.IsNone() && APGMonsterCharacter::GetPresetVisuals(Preset, Visuals);
	FVector Ground = Location;
	// 몸집 배율만큼 캡슐도 커지니 그만큼 더 띄운다. 안 그러면 5배 크리처가 허리까지 땅에 박힌 채 스폰된다.
	if (!SnapToGround(World, Ground, (bHasPreset ? Visuals.CapsuleHalfHeight : 88.0f) * FactionScale(Faction) + 10.0f))
		return nullptr;
	FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Ground);
	APGMonsterCharacter* Monster = World->SpawnActorDeferred<APGMonsterCharacter>(MonsterClass, Transform, nullptr, nullptr, Handling);
	if (!IsValid(Monster))
		return nullptr;
	if (bHasPreset)
	{
		Monster->Visuals = Visuals;
		Monster->bStartDormant = !Visuals.DormantIdle.IsNull();
	}
	ApplyFactionTuning(Monster, Faction, Transform);
	Monster->FinishSpawning(Transform);
	// 멀티(9/27): 모양 이름을 클라이언트에도(몬스터 클래스는 팀원 것이라 부품으로 붙인다, Combat/PGMonsterLookComponent.h).
	if (bHasPreset)
		UPGMonsterLookComponent::Attach(Monster, Preset);
	// 어떤 모양으로 태어났는지(겉모습을 데이터 에셋으로 옮기기 전·후 비교용, 9/23).
	UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: monster preset=%s class=%s mesh=%s idle=%s capsule=%.0f/%.0f"),
		*Preset.ToString(), *Monster->GetClass()->GetName(),
		*GetNameSafe(Monster->GetMesh() ? Monster->GetMesh()->GetSkeletalMeshAsset() : nullptr),
		*Monster->Visuals.Idle.ToSoftObjectPath().GetAssetName(),
		Monster->GetCapsuleComponent()->GetUnscaledCapsuleRadius(), Monster->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight());
	return Monster;
}

namespace
{
	// 탑승 로봇 자리: 맵 바닥 높이의 트인 땅. AISpawn 포인트는 건물 지붕·2층에도 있어서, 탑승 로봇이 옥상에 생기면
	// 내려올 길이 없어 탈 수가 없었다(9/18). 포인트 자리가 안 되면 둘레(8~32m)를 돌며 찾는다.
	//  - 위에서 내려 쏜 첫 바닥이 맵 바닥(지면 Z=20, 도로 30) 근처여야 한다 → 지붕·2층은 탈락.
	//  - 그 바닥에서 로봇 키만큼 위가 트여 있어야 한다 → 건물 안 1층·처마 밑은 탈락.
	bool FindOpenGround(UWorld* World, const FVector& Near, float Height, float BodyRadius, FVector& OutGround)
	{
		constexpr float MaxGroundZ = 250.0f;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGRobotOpenGround), false);
		// 세로선 한 줄만 보면 공장 두 동 사이 틈으로 선이 빠져서, 17m 로봇이 지붕 사이 마당에 스폰돼 끼었다(9/19 PIE).
		// 로봇 몸 굵기(반지름 1.3배)의 캡슐로 그 자리 전체가 비었는지 한 번 더 본다.
		FCollisionObjectQueryParams Solid;
		Solid.AddObjectTypesToQuery(ECC_WorldStatic);
		Solid.AddObjectTypesToQuery(ECC_WorldDynamic);
		const FCollisionShape Body = FCollisionShape::MakeCapsule(BodyRadius * 1.3f, Height * 0.5f);
		for (float Radius = 0.0f; Radius <= 3200.0f; Radius += 800.0f)
		{
			const int32 Steps = Radius <= 0.0f ? 1 : 8;
			for (int32 Step = 0; Step < Steps; ++Step)
			{
				const FVector XY = Near + FRotator(0.0f, Step * 45.0f, 0.0f).Vector() * Radius;
				FHitResult Down;
				if (!World->LineTraceSingleByChannel(Down, FVector(XY.X, XY.Y, Near.Z + 3000.0f), FVector(XY.X, XY.Y, -500.0f), ECC_Visibility, Params))
					continue;
				if (Down.ImpactPoint.Z > MaxGroundZ || Down.ImpactPoint.Z < -30.0f) // 지붕·2층, 또는 호수 바닥
					continue;
				FHitResult Up;
				if (World->LineTraceSingleByChannel(Up, Down.ImpactPoint + FVector(0.0f, 0.0f, 50.0f), Down.ImpactPoint + FVector(0.0f, 0.0f, Height), ECC_Visibility, Params))
					continue;
				if (World->OverlapAnyTestByObjectType(Down.ImpactPoint + FVector(0.0f, 0.0f, Height * 0.5f + 60.0f), FQuat::Identity, Solid, Body, Params))
					continue;
				OutGround = Down.ImpactPoint;
				return true;
			}
		}
		return false;
	}
}

APGRobotCharacter* PGCombatSpawner::SpawnRobot(UWorld* World, const FVector& Location, float Yaw, bool bBoss, float Scale, EPGMonsterFaction Faction)
{
	if (!IsValid(World))
		return nullptr;
	const float HalfHeight = 110.0f * FMath::Max(1.0f, Scale);
	FVector Ground = Location;
	if (!bBoss)
	{
		// 탑승 로봇: 트인 땅이 없으면 놓지 않는다(옥상에 놓아 봐야 못 탄다). 리스폰도 이 함수를 거치므로 같이 고쳐진다.
		if (!FindOpenGround(World, Location, HalfHeight * 2.0f, 45.0f * FMath::Max(1.0f, Scale), Ground))
		{
			UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: no open ground for rideable robot near %s"), *Location.ToCompactString());
			return nullptr;
		}
		Ground.Z += HalfHeight + 20.0f;
	}
	else if (!SnapToGround(World, Ground, HalfHeight + 20.0f))
		return nullptr;
	const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Ground);
	APGRobotCharacter* Robot = World->SpawnActorDeferred<APGRobotCharacter>(UPGVisualSettings::RobotSpawnClass(), Transform, nullptr, nullptr, Handling);
	if (!IsValid(Robot))
		return nullptr;
	if (bBoss)
	{
		Robot->ConfigureAsBoss(Scale);
		Robot->Faction = Faction;
	}
	else
	{
		Robot->ConfigureAsRideable(Scale);
	}
	Robot->FinishSpawning(Transform);
	UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: robot %s class=%s boss=%s mesh=%s"), *Ground.ToCompactString(),
		*Robot->GetClass()->GetName(), bBoss ? TEXT("yes") : TEXT("no"),
		*GetNameSafe(Robot->GetMesh() ? Robot->GetMesh()->GetSkeletalMeshAsset() : nullptr));
	return Robot;
}

int32 PGCombatSpawner::SpawnFromPoints(UWorld* World, const TArray<FLevelDesignPoint>& Points, int64 Seed)
{
	FSpawnContext C;
	C.World = World;
	C.Settings = GetDefault<UPGCombatSettings>();
	C.Points = &Points;
	if (!IsValid(World) || !C.Settings || World->GetNetMode() == NM_Client)
		return 0;
	// 오브젝트 스포너와 다른 시드를 섞어 같은 맵에서 상자 배치와 몬스터 배치가 따로 논다.
	C.Stream.Initialize(static_cast<int32>(Seed ^ (Seed >> 32)) ^ 0x5EED5);

	if (!CollectPoints(C))
	{
		UE_LOG(LogPGObjects, Warning, TEXT("CombatSpawner: no AISpawn points, nothing spawned"));
		return 0;
	}
	AssignWarZoneSectors(C);

	// 리스폰 기록부는 보스보다 먼저 있어야 한다(보스·탈것도 여기 적힌다).
	APGMonsterRespawner* Respawner = World->SpawnActor<APGMonsterRespawner>();
	const int32 Bosses = PlaceBoss(C, Respawner);
	const int32 Robots = PlaceRideableRobots(C, Respawner);
	int32 Counts[4] = { 0, 0, 0, 0 };
	const int32 Monsters = PlaceMonsters(C, Respawner, Counts);
	const int32 Vehicles = PlaceVehicles(C) + PlaceHiddenTank(C);

	UE_LOG(LogPGObjects, Display, TEXT("CombatSpawner: seed=%lld ai_points=%d monsters=%d (A=%d B=%d C=%d) boss=%d robots=%d vehicles=%d"),
		Seed, C.AiPoints.Num(), Monsters, Counts[1], Counts[2], Counts[3], Bosses, Robots, Vehicles);
	return Bosses + Robots + Monsters + Vehicles;
}
