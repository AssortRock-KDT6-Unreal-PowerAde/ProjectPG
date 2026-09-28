// AWarZoneFootprintPreview — 출발 준비 — 플레이어를 시작 지점에 세우고 출발 장비·주변 정리.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"

void AWarZoneFootprintPreview::StartSinglePlayerValidation()
{
	if (bStartedSinglePlayerValidation)
		return;
	const double NowSeconds = FPlatformTime::Seconds();
	if (NowSeconds - LastSinglePlayerValidationAttemptTimeSeconds < 1.0)
		return;
	LastSinglePlayerValidationAttemptTimeSeconds = NowSeconds;

	const FLevelDesignPoint* SpawnPoint = LevelDesignPoints.FindByPredicate(
		[](const FLevelDesignPoint& Point) { return Point.Type == ELevelDesignPointType::Spawn; });
	const FLevelDesignPoint* AIPoint = nullptr;
	const FLevelDesignPoint* LootPoint = nullptr;
	FVector ProjectedAI = FVector::ZeroVector;
	FVector ProjectedLoot = FVector::ZeroVector;
	float SelectedPathLength = 0.0f;
	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (IsValid(NavigationSystem))
	{
		// Use projected open points throughout the local invoker area rather than
		// relying on sparse loot markers to happen to form a pair this seed.
		float BestScore = BIG_NUMBER;
		for (const FLevelDesignPoint& ReferenceAI : LevelDesignPoints)
		{
			if (ReferenceAI.Type != ELevelDesignPointType::AISpawn
				|| FVector::DistSquared2D(ReferenceAI.WorldLocation, GetActorLocation()) >= FMath::Square(10000.0f))
				continue;
			FNavLocation AIProjection;
			if (NavigationSystem->ProjectPointToNavigation(ReferenceAI.WorldLocation, AIProjection, FVector(250, 250, 400)))
			{
				const float TargetDistances[] = { 1500.0f, 2500.0f, 3500.0f, 4500.0f };
				for (const float TargetDistance : TargetDistances)
				{
					for (int32 AngleIndex = 0; AngleIndex < 16; ++AngleIndex)
					{
						const float Angle = FMath::DegreesToRadians(AngleIndex * 22.5f);
						const FVector RawTarget = AIProjection.Location
							+ FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.0f) * TargetDistance;
						FNavLocation TargetProjection;
						if (!NavigationSystem->ProjectPointToNavigation(RawTarget, TargetProjection, FVector(500, 500, 400)))
							continue;
						// Recast can return a valid polygon path whose edge barely clips a
						// non-nav-relevant visual prop. Require a real character capsule to
						// fit along this validation corridor as well.
						FHitResult CorridorHit;
						FCollisionQueryParams CorridorQuery(SCENE_QUERY_STAT(SinglePlayerValidationCorridor), false);
						CorridorQuery.AddIgnoredActor(this);
						const bool bCorridorBlocked = GetWorld()->SweepSingleByChannel(
							CorridorHit,
							AIProjection.Location + FVector(0, 0, 100.0f),
							TargetProjection.Location + FVector(0, 0, 100.0f),
							FQuat::Identity,
							ECC_Pawn,
							FCollisionShape::MakeCapsule(45.0f, 90.0f),
							CorridorQuery);
						if (bCorridorBlocked)
							continue;
						UNavigationPath* CandidatePath = NavigationSystem->FindPathToLocationSynchronously(
							GetWorld(), AIProjection.Location, TargetProjection.Location, nullptr);
						if (!IsValid(CandidatePath) || !CandidatePath->IsValid() || CandidatePath->IsPartial())
							continue;
						const float PathLength = CandidatePath->GetPathLength();
						if (PathLength < 1200.0f || PathLength > 7000.0f)
							continue;
						const float Score = FMath::Abs(PathLength - 3500.0f);
						if (Score < BestScore)
						{
							BestScore = Score;
							AIPoint = &ReferenceAI;
							LootPoint = &ReferenceAI; // label fallback; target is projected below
							ProjectedAI = AIProjection.Location;
							ProjectedLoot = TargetProjection.Location;
							SelectedPathLength = PathLength;
						}
					}
				}
			}
		}
	}
	if (SpawnPoint == nullptr || AIPoint == nullptr || LootPoint == nullptr)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("Single-player validation waiting for a complete local tactical path"));
		return;
	}

	bStartedSinglePlayerValidation = true;
	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = this;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// 게임모드가 진짜 플레이어 캐릭터(팀 BP_CustomPlayerCharacter 등)를 이미 줬으면 검증 캐릭터로 바꿔치기하지 않는다.
	// 기본 폰(날아다니는 카메라)이거나 폰이 없을 때만 검증 캐릭터를 만들어 빙의한다.
	// 없을 때는 팀 캐릭터로 시작해도 바로 검증 캐릭터로 바뀌었다(9/19 팀 코드 합치기).
	// 멀티(9/27): 아래 "첫 플레이어를 시작 자리에 세우기·검증 캐릭터로 바꿔치기·로딩 걷기" 는 혼자 하는 판만 한다.
	//   전용 서버에서 첫 플레이어 = 먼저 들어온 한 사람이라, 그 사람이 아직 캐릭터가 없으면 검증 캐릭터(팀 캐릭터 아님)로 빙의됐고,
	//   아무도 안 들어왔으면 0번 자리에 대머리 검증 캐릭터가 복제되어 서 있었다. 멀티에서는 사람 세우기 = PlaceJoinedPlayers,
	//   로딩·입력 = 각자 컴퓨터의 TickLocalPlayerReady 가 한다.
	const bool bNetGame = GetNetMode() != NM_Standalone;
	APlayerController* PlayerController = bNetGame ? nullptr : GetWorld()->GetFirstPlayerController();
	APawn* ExistingPawn = IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
	const bool bHasGamePawn = IsValid(ExistingPawn) && !ExistingPawn->IsA<ADefaultPawn>();
	if (bNetGame)
	{
		// 아무것도 하지 않는다(위 주석).
	}
	else if (bHasGamePawn)
	{
		ExistingPawn->SetActorLocation(SpawnPoint->WorldLocation + FVector(0.0f, 0.0f, 100.0f), false, nullptr, ETeleportType::TeleportPhysics);
		// 몸과 카메라를 맵 가운데(워존) 쪽으로 돌린다. 시작 지점은 맵 가장자리라 가운데 쪽이 곧 "가야 할 길" 이다.
		// 예전에는 위치만 옮겨서 레벨의 PlayerStart 가 보던 방향(대개 가장자리 벽)을 보고 시작했다(9/22 사용자: "스폰할 때마다 벽 보고 있어").
		// 9/28: 출구가 있으면 출발 구역 담장의 출구 쪽(GetSpawnOpeningYaw — 벽 보고 시작하던 것).
		const FVector ToCentre = (GetActorLocation() - SpawnPoint->WorldLocation).GetSafeNormal2D();
		float OpeningYaw = 0.0f;
		const bool bHasOpening = GetSpawnDoorwayYaw(SpawnPoint->GridCell, SpawnPoint->WorldLocation, OpeningYaw, ExistingPawn);
		if (bHasOpening || !ToCentre.IsNearlyZero())
		{
			const FRotator Facing(0.0f, bHasOpening ? OpeningYaw : ToCentre.Rotation().Yaw, 0.0f);
			ExistingPawn->SetActorRotation(Facing);
			if (IsValid(PlayerController))
				PlayerController->SetControlRotation(FRotator(-10.0f, Facing.Yaw, 0.0f));
		}
		UE_LOG(LogTemp, Display, TEXT("Single-player validation: keeping game mode pawn %s (moved to spawn point, facing yaw %.0f, open ahead %.0fm)"),
			*GetNameSafe(ExistingPawn), ExistingPawn->GetActorRotation().Yaw,
			MeasureOpenAhead(ExistingPawn->GetActorLocation(), ExistingPawn->GetActorRotation().Yaw, 4000.0f, ExistingPawn) * 0.01f);
		// 출격 로딩 화면을 걷는다. 이 순간 전까지는 기본 마네킹·맵 가운데가 보여서 가려 두었다(UPGLoadingScreenSubsystem).
		// 로딩이 걷히면 "게임이 시작되었습니다" (문구는 프로젝트 설정 ProjectPG Flow > Run > Raid Start Message, 9/23).
		HideLoadingWhenFacilitiesVisible(TEXT("player placed at the spawn point"));
		// 입력을 게임 전용으로 되돌린다. 타이틀·로비는 버튼을 누르려고 커서를 보이게(GameAndUI) 해 두는데, 그 설정은 화면(뷰포트)에 남아
		// 게임 맵까지 따라왔다 — 마우스 버튼을 누르고 있어야만 시야가 돌았다(9/22 사용자: "왼쪽·오른쪽 마우스를 눌러야만 시야가 돈다").
		if (IsValid(PlayerController))
		{
			PlayerController->SetInputMode(FInputModeGameOnly());
			PlayerController->bShowMouseCursor = false;
		}
	}
	else
	{
		ValidationPlayerCharacter = GetWorld()->SpawnActor<ALevelDesignValidationCharacter>(
			ALevelDesignValidationCharacter::StaticClass(),
			SpawnPoint->WorldLocation + FVector(0.0f, 0.0f, 100.0f),
			FRotator::ZeroRotator,
			SpawnParameters);
		if (IsValid(PlayerController) && IsValid(ValidationPlayerCharacter))
			PlayerController->Possess(ValidationPlayerCharacter);
		if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(this))
		{
			Loading->HideLoading(TEXT("validation character placed at the spawn point"));
			Loading->AnnounceWhenClear(UPGFlowSettings::Get().RaidStartMessage);
		}
		if (IsValid(PlayerController))
		{
			PlayerController->SetInputMode(FInputModeGameOnly()); // 위 게임 폰 갈래와 같은 이유
			PlayerController->bShowMouseCursor = false;
		}
	}

	ValidationAIStartLocation = ProjectedAI + FVector(0.0f, 0.0f, 100.0f);
	ValidationAITargetLocation = ProjectedLoot;
	ValidationAICharacter = GetWorld()->SpawnActor<ALevelDesignValidationCharacter>(
		ALevelDesignValidationCharacter::StaticClass(),
		ValidationAIStartLocation,
		FRotator::ZeroRotator,
		SpawnParameters);
	if (IsValid(ValidationAICharacter))
	{
		ValidationAICharacter->SpawnDefaultController();
		// 길찾기 검사용 캐릭터일 뿐이라 안 보이게 한다. 실제로 걸어가 봐야 검사가 되니 땅 충돌·이동은 그대로 두고,
		// 다른 폰(플레이어·몬스터)과만 안 부딪히게 한다(보이지 않는 벽이 되지 않게).
		// 왜: 대머리 시험용 캐릭터가 길 한가운데 서 있어서 플레이어가 놀랐다(9/20 PIE). 검사가 끝나면 아래 Verify 에서 지운다.
		ValidationAICharacter->SetActorHiddenInGame(true);
		ValidationAICharacter->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		// 멀티(9/27): 서버 혼자 하는 검사다. 복제하면 클라이언트 쪽 사본은 위의 "폰과 안 부딪힘" 설정을 모른 채(충돌 설정은 복제 안 됨)
		//   보이지 않는 벽이 되어, 클라 캐릭터만 막히고 서버가 위치를 되돌린다. 클라이언트로 보내지 않는다.
		ValidationAICharacter->SetReplicates(false);
	}

	if (HasAuthority())
	{
		// 꾸러미는 사람이 서서 보는 방향(맵 가운데 쪽) 앞에 둔다. 멀티는 그 방향이 곧 PlaceJoinedPlayers 가 세우는 방향이다.
		const APawn* StarterPawn = bNetGame ? nullptr : GetValidationPlayerPawn();
		const FVector StarterToCentre = (GetActorLocation() - SpawnPoint->WorldLocation).GetSafeNormal2D();
		SpawnStarterTransformKit(SpawnPoint->WorldLocation, IsValid(StarterPawn) ? StarterPawn->GetActorRotation().Yaw
			: (StarterToCentre.IsNearlyZero() ? 0.0f : StarterToCentre.Rotation().Yaw));
		// 멀티(9/27): 추가 시작 지역에도 같은 꾸러미(변신 여고생 + 연료통). 한 지역에만 있으면 그 지역에서 나온 사람만 차를 얻는다.
		// 기준 자리 = 그 지역 첫 자리, 방향 = 맵 가운데 쪽(PlaceJoinedPlayers 가 사람을 세우는 방향과 같다).
		for (int32 RegionIndex = 1; RegionIndex < SpawnRegionCells.Num(); ++RegionIndex)
		{
			const FIntPoint RegionCell = SpawnRegionCells[RegionIndex];
			const FLevelDesignPoint* RegionSeat = LevelDesignPoints.FindByPredicate([&RegionCell](const FLevelDesignPoint& Point)
			{
				return Point.Type == ELevelDesignPointType::Spawn && Point.GridCell == RegionCell;
			});
			if (!RegionSeat)
				continue;
			const FVector ToCentre = (GetActorLocation() - RegionSeat->WorldLocation).GetSafeNormal2D();
			SpawnStarterTransformKit(RegionSeat->WorldLocation, ToCentre.IsNearlyZero() ? 0.0f : ToCentre.Rotation().Yaw);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("Single-player validation staged: player=%s spawn=%s ai=%s target=%s complete_path_cm=%.1f"),
		IsValid(GetValidationPlayerPawn()) ? TEXT("possessed") : TEXT("failed"),
		*SpawnPoint->PointId.ToString(),
		*AIPoint->PointId.ToString(),
		*LootPoint->PointId.ToString(),
		SelectedPathLength);
}

bool AWarZoneFootprintPreview::FindStarterGround(const FVector& Near, FVector& OutGround, bool bCheckOverlap) const
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return false;
	FCollisionObjectQueryParams GroundTypes;
	GroundTypes.AddObjectTypesToQuery(ECC_WorldStatic);
	// 이 액터(맵 생성기)는 무시하면 안 된다 — 바닥 타일(GroundHISM 등)이 전부 이 액터의 부품이라 땅이 하나도 안 잡혔다
	// (9/20 PIE 네 번 연속 "no ground at all"의 진짜 원인).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGStarterGround), false);
	// 막 옮겨 놓은 플레이어 자신은 장애물이 아니다.
	if (const APlayerController* PC = World->GetFirstPlayerController(); PC && PC->GetPawn())
		Params.AddIgnoredActor(PC->GetPawn());
	TArray<FHitResult> Hits;
	World->LineTraceMultiByObjectType(Hits, Near + FVector(0.0f, 0.0f, 600.0f), Near - FVector(0.0f, 0.0f, 1500.0f), GroundTypes, Params);
	for (const FHitResult& Hit : Hits)
	{
		// 길찾기용 보이지 않는 바닥판(탈것 무시)은 땅이 아니다 — 움푹 파인 곳 위에 떠서 서 있게 된다.
		if (!IsValid(Hit.GetComponent()) || Hit.GetComponent()->GetCollisionResponseToChannel(ECC_Vehicle) != ECR_Block)
			continue;
		// 지붕·벽 위(발보다 한참 위)는 건너뛰고 그 아래 땅을 본다. 발보다 한참 아래(구덩이·절벽)면 이 자리는 버린다.
		if (Hit.ImpactPoint.Z > Near.Z + 250.0f)
			continue;
		if (Hit.ImpactPoint.Z < Near.Z - 400.0f)
			return false;
		// 사람 캡슐이 벽·소품과 겹치지 않는 자리만. 바닥에서 25cm 띄워 잰다 — 도로는 땅보다 10cm 높아서,
		// 딱 붙여 재면 도로 판과 겹쳐 "막힘"으로 나왔다(9/20 PIE: 스타터 키트가 안 생김).
		const FVector Center = Hit.ImpactPoint + FVector(0.0f, 0.0f, 25.0f + 90.0f);
		FHitResult Blocker;
		if (bCheckOverlap && World->SweepSingleByChannel(Blocker, Center, Center + FVector(0.0f, 0.0f, 1.0f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(50.0f, 90.0f), Params))
		{
			// 무엇에 막혔는지 남긴다(9/20 PIE 에서 세 번 연속 "빈 땅 없음"이라 원인을 봐야 한다).
			UE_LOG(LogTemp, Verbose, TEXT("Starter ground blocked at %s by %s (%s)"), *Center.ToCompactString(), *GetNameSafe(Blocker.GetActor()), *GetNameSafe(Blocker.GetComponent()));
			LastStarterBlocker = FString::Printf(TEXT("%s/%s"), *GetNameSafe(Blocker.GetActor()), *GetNameSafe(Blocker.GetComponent()));
			return false;
		}
		OutGround = Hit.ImpactPoint;
		return true;
	}
	return false;
}

void AWarZoneFootprintPreview::SpawnStarterTransformKit(const FVector& PlayerStart, float PlayerYaw)
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return;
	// 플레이어가 처음 보는 방향 앞쪽 부채꼴에서 빈 땅을 찾는다. 막혀 있으면 각도를 돌려 가며 다시.
	auto FindSpot = [this, &PlayerStart, PlayerYaw](float Distance, float StartAngle, FVector& Out, bool bCheckOverlap = true)
	{
		for (int32 Step = 0; Step < 12; ++Step)
		{
			const float Angle = PlayerYaw + StartAngle + (Step % 2 == 0 ? 1.0f : -1.0f) * (Step / 2) * 30.0f;
			if (FindStarterGround(PlayerStart + FRotator(0.0f, Angle, 0.0f).Vector() * Distance, Out, bCheckOverlap))
				return true;
		}
		return false;
	};
	// 마지막 수단: 길찾기 지도(걸어 다닐 수 있는 곳)로 찍은 점. 캡슐 검사가 모르는 무언가에 계속 막혀도(9/20 PIE 두 번 실패) 여기선 선다.
	auto FindNavSpot = [this, World, &PlayerStart, PlayerYaw](float Distance, float StartAngle, FVector& Out)
	{
		UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!IsValid(Nav))
			return false;
		for (int32 Step = 0; Step < 12; ++Step)
		{
			const float Angle = PlayerYaw + StartAngle + (Step % 2 == 0 ? 1.0f : -1.0f) * (Step / 2) * 30.0f;
			FNavLocation NavPoint;
			if (!Nav->ProjectPointToNavigation(PlayerStart + FRotator(0.0f, Angle, 0.0f).Vector() * Distance, NavPoint, FVector(300.0f, 300.0f, 500.0f)))
				continue;
			// 길찾기 지도 높이는 실제 땅과 조금 다르다 → 그 점에서 아래로 진짜 땅을 잰다.
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(PGStarterNavGround), false);
			if (World->LineTraceSingleByChannel(Hit, NavPoint.Location + FVector(0.0f, 0.0f, 200.0f), NavPoint.Location - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, Params))
			{
				Out = Hit.ImpactPoint;
				return true;
			}
		}
		return false;
	};
	FVector NPCGround, FuelGround;
	// 시작 자리가 검문소 벽 안처럼 좁으면 가까운 곳이 막힌다. 7m 부터 4m·11m 까지 넓혀 보고, 그래도 없으면 길찾기 지도로.
	if (!FindSpot(700.0f, 15.0f, NPCGround) && !FindSpot(400.0f, 15.0f, NPCGround) && !FindSpot(1100.0f, 15.0f, NPCGround)
		&& !FindNavSpot(600.0f, 15.0f, NPCGround)
		// 끝까지 못 찾으면 겹침 검사 없이 앞쪽 땅에 세운다(스폰이 조금 밀어낸다). 시작 동선에서 빠지는 것보다 낫다.
		&& !FindSpot(500.0f, 15.0f, NPCGround, false))
	{
		UE_LOG(LogTemp, Warning, TEXT("Starter transform kit: no ground at all near spawn %s (last blocker %s)"), *PlayerStart.ToCompactString(), *LastStarterBlocker);
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	// NPC 는 플레이어 쪽을 바라본다.
	const float FaceYaw = (PlayerStart - NPCGround).GetSafeNormal2D().Rotation().Yaw;
	APGTransformNPCActor* NPC = World->SpawnActor<APGTransformNPCActor>(APGTransformNPCActor::GetSpawnClass(),
		FTransform(FRotator(0.0f, FaceYaw, 0.0f), NPCGround), Params);
	APGFloorItemActor* Fuel = nullptr;
	if (FindSpot(350.0f, -25.0f, FuelGround) || FindSpot(200.0f, -25.0f, FuelGround) || FindNavSpot(300.0f, -25.0f, FuelGround) || FindSpot(250.0f, -25.0f, FuelGround, false))
	{
		FStarterFuelSlot& Slot = StarterFuelSlots.AddDefaulted_GetRef();
		Slot.Home = FTransform(FRotator(0.0f, PlayerYaw + 90.0f, 0.0f), FuelGround);
		Fuel = APGFloorItemActor::SpawnDrop(this, TEXT("Fuel"), 1, Slot.Home);
		Slot.Fuel = Fuel;
		if (!IsValid(Fuel))
			StarterFuelSlots.Pop(); // 처음부터 못 놓았으면 다시 놓기 대상도 아니다(예전 bStarterFuelPlaced 와 같다)
	}
	UE_LOG(LogTemp, Display, TEXT("Starter transform kit: npc=%s at %s fuel=%s (last blocker %s)"),
		*GetNameSafe(NPC), *NPCGround.ToCompactString(), *GetNameSafe(Fuel), *LastStarterBlocker);
}

void AWarZoneFootprintPreview::TickStarterFuelRespawn(float DeltaSeconds)
{
	if (!HasAuthority())
		return;
	// 사라진 즉시 놓으면 주운 사람 발밑에 바로 또 생겨 "안 주워진 것" 처럼 보인다. 잠깐 쉬었다 놓는다.
	constexpr float RespawnDelaySeconds = 5.0f;
	for (FStarterFuelSlot& Slot : StarterFuelSlots) // 시작 지역마다 따로 센다
	{
		if (Slot.Fuel.IsValid())
			continue;
		// 드래곤 구덩이에 들어간 자리면 더 놓지 않는다(9/28 PIE: 구덩이 속 자리에 5초마다 놓아 26번 떨어져 사라졌다).
		const FIntPoint HomeCell(FMath::RoundToInt(Slot.Home.GetLocation().X / DesignCellSize), FMath::RoundToInt(Slot.Home.GetLocation().Y / DesignCellSize));
		if (CollapsedCells.Contains(HomeCell))
			continue;
		Slot.MissingSeconds += DeltaSeconds;
		if (Slot.MissingSeconds < RespawnDelaySeconds)
			continue;
		Slot.MissingSeconds = 0.0f;
		APGFloorItemActor* Fuel = APGFloorItemActor::SpawnDrop(this, TEXT("Fuel"), 1, Slot.Home);
		Slot.Fuel = Fuel;
		UE_LOG(LogTemp, Display, TEXT("Starter fuel: gone (picked up or blown up) — respawned %s at %s"),
			*GetNameSafe(Fuel), *Slot.Home.GetLocation().ToCompactString());
	}
}

// 시작 지점 둘레 비우기.
//
// 왜: 시작 칸은 20m 한 칸짜리 담장 구역(BPP_Tile_Spawn_Staging)이라, 차·연료통·변신 여고생이 한데 몰려 비좁았고
//   차가 담 위에 스폰되는 일도 있었다(9/22 사용자: "스폰 지역이 좀 좁나?"). 맵을 키우는 대신 시작 칸 둘레를 비운다.
// 무엇을: 시작 칸 가운데 ± SpawnClearHalfExtentCm(기본 20m → 40m 네모) 안의 담장·소품·나무·바위. 땅·길 판은 남긴다
//   (높이 60cm 안 되는 납작한 것, 땅 표(PGTerrain) 붙은 것).
// 언제: 타일을 HISM 으로 묶은 직후, 게임플레이 지점을 정하기 전. 지점 검사가 이미 비운 자리를 보게 하려고.
void AWarZoneFootprintPreview::ClearSpawnSurroundings()
{
	if (SpawnClearHalfExtentCm <= 0.0f)
		return;
	TArray<FBox2D> Zones;
	for (const FTileDesignPlacement& Placement : TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Spawn)
		{
			const FVector2D C(Placement.WorldLocation.X, Placement.WorldLocation.Y);
			Zones.Add(FBox2D(C - FVector2D(SpawnClearHalfExtentCm), C + FVector2D(SpawnClearHalfExtentCm)));
		}
	if (Zones.IsEmpty())
		return;

	int32 TouchedComponents = 0;
	const int32 Removed = RemoveTallDressingInZones(Zones, TouchedComponents);
	UE_LOG(LogTemp, Display, TEXT("Spawn clearing: removed %d wall/prop/tree instance(s) from %d batch(es) within %.0fm squares around %d spawn cell(s) (first zone %s, packed batches %d, runtime tile actors %d)"),
		Removed, TouchedComponents, SpawnClearHalfExtentCm * 0.02f, Zones.Num(), *Zones[0].ToString(), RuntimePackedVisualHISMs.Num(), SpawnedRuntimeTiles.Num());
}

// 네모 구역들 안의 키 큰 타일 소품을 걷어 낸다(ClearSpawnSurroundings 에서 떼어 냄 — 외진 보상 거점도 같은 일을 해야 해서).
int32 AWarZoneFootprintPreview::RemoveTallDressingInZones(const TArray<FBox2D>& Zones, int32& OutTouchedComponents)
{
	OutTouchedComponents = 0;
	if (Zones.IsEmpty())
		return 0;
	TArray<UInstancedStaticMeshComponent*> Components;
	for (UHierarchicalInstancedStaticMeshComponent* Packed : RuntimePackedVisualHISMs)
		Components.Add(Packed);
	for (UHierarchicalInstancedStaticMeshComponent* Feature : TerrainFeatureHISMs)
		Components.Add(Feature);
	Components.Add(TerrainRockHISM);
	Components.Add(TerrainTreeHISM);
	Components.Add(TerrainBushHISM);

	int32 Removed = 0;
	for (UInstancedStaticMeshComponent* Component : Components)
	{
		// 묶음의 범위(Bounds)로 미리 거르지 않는다 — 막 묶은 직후라 범위가 아직 계산 전이어서 전부 "안 겹침" 으로 걸러졌다(첫 시도 0개).
		// 인스턴스 위치를 하나씩 본다. 맵 전체 12만 개쯤이고 판 시작 때 한 번이다.
		if (!IsValid(Component) || Component->GetInstanceCount() == 0 || Component->ComponentTags.Contains(PGPhysicsUtil::TerrainTag))
			continue;
		const UStaticMesh* Mesh = Component->GetStaticMesh();
		if (!IsValid(Mesh))
			continue;
		const FString MeshName = Mesh->GetName();
		// 바닥 판·길 조각은 남긴다(이름으로 한 번, 높이로 한 번).
		if (MeshName.Contains(TEXT("Floor")) || MeshName.Contains(TEXT("Road")) || MeshName.Contains(TEXT("ground")) || MeshName.Contains(TEXT("Ground")))
			continue;
		const float MeshHeight = Mesh->GetBounds().BoxExtent.Z * 2.0f;
		TArray<int32> Doomed;
		for (int32 Index = 0; Index < Component->GetInstanceCount(); ++Index)
		{
			FTransform Instance;
			if (!Component->GetInstanceTransform(Index, Instance, true))
				continue;
			if (MeshHeight * FMath::Abs(Instance.GetScale3D().Z) < 60.0f)
				continue;
			const FVector2D At(Instance.GetLocation().X, Instance.GetLocation().Y);
			if (Zones.ContainsByPredicate([&At](const FBox2D& Z) { return Z.IsInside(At); }))
				Doomed.Add(Index);
		}
		if (Doomed.Num() > 0)
		{
			Component->RemoveInstances(Doomed);
			Removed += Doomed.Num();
			++OutTouchedComponents;
		}
	}
	return Removed;
}

APawn* AWarZoneFootprintPreview::GetValidationPlayerPawn() const
{
	if (IsValid(ValidationPlayerCharacter))
		return ValidationPlayerCharacter;
	const APlayerController* PlayerController = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	return IsValid(PlayerController) ? PlayerController->GetPawn() : nullptr;
}

void AWarZoneFootprintPreview::TryIssueSinglePlayerValidationMove()
{
	if (!bStartedSinglePlayerValidation || bIssuedSinglePlayerValidationMove
		|| !IsValid(ValidationAICharacter) || !IsValid(GetValidationPlayerPawn()))
		return;
	if (FPlatformTime::Seconds() - LastPlayerNavigationBlockerUpdateTimeSeconds < 0.5)
		return;

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavigationSystem) || NavigationSystem->IsNavigationBuildInProgress())
		return;

	UNavigationPath* ConfirmedPath = NavigationSystem->FindPathToLocationSynchronously(
		GetWorld(),
		ValidationAICharacter->GetActorLocation(),
		ValidationAITargetLocation,
		nullptr);
	if (!IsValid(ConfirmedPath) || !ConfirmedPath->IsValid() || ConfirmedPath->IsPartial())
		return;

	AAIController* AIController = Cast<AAIController>(ValidationAICharacter->GetController());
	if (!IsValid(AIController))
		return;
	const EPathFollowingRequestResult::Type RequestResult = AIController->MoveToLocation(
		ValidationAITargetLocation,
		100.0f,
		true,
		true,
		true,
		false,
		nullptr,
		true);
	if (RequestResult == EPathFollowingRequestResult::Failed)
		return;

	bIssuedSinglePlayerValidationMove = true;
	SinglePlayerValidationStartTimeSeconds = FPlatformTime::Seconds();
	ValidationAIStartLocation = ValidationAICharacter->GetActorLocation();
	UE_LOG(LogTemp, Display,
		TEXT("Single-player validation move issued after nav stabilization: path_cm=%.1f request=%d"),
		ConfirmedPath->GetPathLength(),
		static_cast<int32>(RequestResult));
}

// 출격 로딩 화면 걷기(9/28 4060 측정). 예전에는 사람을 시작 자리에 세우는 순간 걷었는데, 그때 시설 레벨(중앙 건물·시가지 블록)이
//   아직 불러와지는 중이라 걷힌 뒤 10초 동안 0.1~0.4초 멈춤이 이어졌다(레벨 올리기 + 그 안의 이펙트 준비 + 텍스처). 다 올라와 보일 때까지
//   로딩 화면을 두고, 무슨 일이 있어도 20초 뒤에는 걷는다(영영 검은 화면보다 낫다). 불러올 시설이 없는 판은 바로 걷힌다.
void AWarZoneFootprintPreview::HideLoadingWhenFacilitiesVisible(const TCHAR* Why)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	const FString Reason(Why);
	const double StartedAt = World->GetTimeSeconds();
	TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
	TWeakObjectPtr<AWarZoneFootprintPreview> WeakThis(this);
	World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(this, [WeakThis, Handle, Reason, StartedAt]()
	{
		AWarZoneFootprintPreview* Self = WeakThis.Get();
		if (!Self || !Self->GetWorld())
			return;
		int32 Pending = 0;
		for (const ULevelStreamingDynamic* Level : Self->FacilityDesignLevelInstances)
			if (IsValid(Level) && (!Level->IsLevelLoaded() || !Level->IsLevelVisible()))
				++Pending;
		const double Waited = Self->GetWorld()->GetTimeSeconds() - StartedAt;
		if (Pending > 0 && Waited < 20.0)
			return;
		// 타이머를 지우면 이 람다(와 담아 둔 Reason)도 같이 지워진다 — 쓸 것을 먼저 복사하고 맨 끝에 지운다(9/28: 로그 글자가 깨졌다).
		const FString Why = Reason;
		const TSharedRef<FTimerHandle> Mine = Handle;
		UWorld* World = Self->GetWorld();
		UE_LOG(LogTemp, Display, TEXT("PGLoading: facility levels %s after %.1f s — lifting the loading screen (%s)"),
			Pending > 0 ? TEXT("still loading (gave up waiting)") : TEXT("all visible"), Waited, *Why);
		if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(Self))
		{
			Loading->HideLoading(*Why);
			Loading->AnnounceWhenClear(UPGFlowSettings::Get().RaidStartMessage);
		}
		World->GetTimerManager().ClearTimer(*Mine);
	}), 0.25f, true, 0.0f);
}
