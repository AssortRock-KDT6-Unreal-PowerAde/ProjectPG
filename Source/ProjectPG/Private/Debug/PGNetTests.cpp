// 멀티 조작 시험(2026-09-27): 화면 없는 클라이언트가 스스로 탈것까지 걸어가 F 로 타고, W 로 달리고(차는 오른쪽 버튼으로 날기까지),
// F 로 내린다. 키는 PGKeyPolling::TestKeysDown 으로 흉내 낸다.
//
// 왜: 멀티 문제는 "클라이언트가 직접 조작할 때" 만 드러난다(탑승 F 가 하차로 읽힘, 차 입력이 서버에서 0 으로 덮임, 운전자 화면에서
//   탱크가 안 움직임 …). 서버 쪽 시험(PG.RideTest)이나 혼자 하는 판으로는 하나도 안 보였다. 사람이 PIE 로 매번 누를 수는 없어서
//   헤드리스 클라이언트가 같은 조작을 하게 했다.
// 사용: 클라이언트 실행 인자에 -ExecCmds="PG.NetRideTest car"  (car | tank | fly)
//   Tools/wbp/headless_multi.ps1 -Dedicated -Clients 1 -ClientCmd "PG.NetRideTest car"
// 결과: 클라이언트 로그의 "NetRideTest" 줄. 마지막 줄이 PASS/FAIL.

#include "Common/PGKeyPolling.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "LevelDesign/PGMapInfo.h"
#include "LevelDesign/PGLevelDesignTypes.h"
#include "Objects/PGInteractionComponent.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGExtractionZoneActor.h"
#include "Interaction/Interactable.h"
#include "TimerManager.h"
#include "Objects/PGObjectTypes.h"
#include "Vehicle/PGTankPawn.h"
#include "Vehicle/PGVehiclePawn.h"
#include "Actors/ItemContainerActor.h"
#include "Vehicle/PGFlightKitComponent.h"
#include "Finale/PGBattleshipActor.h"
#include "Interaction/PGRideable.h"
#include "Robot/PGRobotCharacter.h"
#include "Common/PGPhysicsUtil.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "DynamicRHI.h"
#include "RenderTimer.h"
#include "RHIStats.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "StaticMeshResources.h"
#include "Materials/MaterialInterface.h"
#include "Components/LocalLightComponent.h"
#include "UObject/UObjectIterator.h"

namespace PGNetTests
{
	struct FRideTest
	{
		FString Kind;
		bool bWaitForServerMount = false; // 'mounted': 걸어가지 않고 서버가 태워 줄 때까지 기다린다
		int32 Step = 0;
		double StepStart = -1.0;
		double Began = -1.0;
		TWeakObjectPtr<APawn> Ride;
		TWeakObjectPtr<AActor> RideActor; // helm: 전함
		float StartYaw = 0.0f;
		TWeakObjectPtr<APawn> Walker;
		FVector DriveStart = FVector::ZeroVector;
		double LastProgressAt = 0.0;
		float BestDistance = BIG_NUMBER;
		float SideSign = 1.0f;
		TSet<TWeakObjectPtr<AActor>> LoggedSkip;
		// 떨림 재기(9/28): 달리는 동안 프레임마다 이 화면의 탈것이 움직였나. 끊기는 차는 "안 움직인 프레임" 이 많다.
		FVector LastSample = FVector::ZeroVector;
		int32 Frames = 0;
		int32 StillFrames = 0;
		float BiggestStep = 0.0f;
		float BiggestStepFrameMs = 0.0f;   // 그 튐이 난 프레임의 길이 — 프레임이 길었으면 튐이 아니라 그만큼 간 것
		float BiggestSpeedJump = 0.0f;     // 한 프레임 속도가 직전 프레임보다 얼마나 확 바뀌었나(cm/s) — 진짜 "툭" 은 이것
		float LastFrameSpeed = -1.0f;
		double LastSampleAt = -1.0;
		// 반응 재기(9/28): 비행 중 A 를 누른 시각과 그때 기수 — 기수가 5도 돌 때까지 걸린 시간이 "조작 반응" 이다.
		double TurnPressedAt = -1.0;
		float TurnStartYaw = 0.0f;
		bool bTurnLogged = false;
		bool bPromptChecked = false;
		int32 LastLoggedSecond = -1;
		// 멈칫 재기(9/28 "날다가 조금씩 멈칫"): 2~4초(순항)에 프레임 속도가 직전 10프레임 평균의 절반 아래로 떨어진 프레임 수.
		TArray<float> RecentSpeeds;
		int32 StallFrames = 0;
		int32 CruiseFrames = 0;
		FTSTicker::FDelegateHandle Handle;
	};

	// 이 컴퓨터 사람이 있는 게임 월드(클라이언트는 접속 뒤 월드가 바뀌므로 매번 다시 찾는다).
	UWorld* FindLocalGameWorld(APlayerController*& OutPC)
	{
		OutPC = nullptr;
		if (!GEngine)
			return nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !(Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE))
				continue;
			APlayerController* PC = World->GetFirstPlayerController();
			if (IsValid(PC) && PC->IsLocalController())
			{
				OutPC = PC;
				return World;
			}
		}
		return nullptr;
	}

	void Finish(const TSharedRef<FRideTest>& Test, bool bPass, const FString& Why)
	{
		PGKeyPolling::TestKeysDown().Reset();
		UE_LOG(LogPGObjects, Display, TEXT("NetRideTest %s kind=%s — %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Test->Kind, *Why);
		Test->Step = 99;
	}

	bool Tick(float, TSharedRef<FRideTest> Test)
	{
		if (Test->Step >= 99)
			return false; // 끝(티커에서 빠진다)
		APlayerController* PC = nullptr;
		UWorld* World = FindLocalGameWorld(PC);
		if (!World)
			return true;
		const double Now = World->GetTimeSeconds();
		if (Test->Began < 0.0 || Now < Test->Began)
			Test->Began = Now;
		if (Test->StepStart < 0.0 || Now < Test->StepStart)
			Test->StepStart = Now;
		const double InStep = Now - Test->StepStart;
		auto Next = [&Test, Now](int32 Step) { Test->Step = Step; Test->StepStart = Now; };
		APawn* Pawn = PC->GetPawn();

		if (Test->Kind == TEXT("helm") && Test->Step < 20)
			Test->Step = 20;
		if (Test->Kind == TEXT("deck") && Test->Step < 30)
			Test->Step = 30;
		switch (Test->Step)
		{
		case 20: // 전함 조종석 시험: 함교로 옮겨질 때까지(서버 명령 PG.Finale.Bridge) 기다린다
		{
			APGBattleshipActor* Ship = nullptr;
			for (TActorIterator<APGBattleshipActor> It(World); It; ++It)
				Ship = *It;
			if (Now - Test->Began > 240.0)
			{
				Finish(Test, false, TEXT("no ship / not moved to the bridge within 240s"));
				return false;
			}
			if (!IsValid(Ship) || !IsValid(Pawn) || !Pawn->IsA<ACharacter>() || Ship->GetMotion() != EPGShipMotion::Hover)
				return true;
			if (FVector::DistSquared(Pawn->GetActorLocation(), Ship->GetBridgeWorld()) > FMath::Square(700.0f))
				return true;
			Test->RideActor = Ship;
			Test->Walker = Pawn;
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: at the bridge of %s — pressing F"), *Ship->GetName());
			Next(21);
			return true;
		}
		case 21: // F 를 0.2초
		{
			if (InStep < 0.2)
			{
				PGKeyPolling::TestKeysDown().Add(EKeys::F);
				return true;
			}
			PGKeyPolling::TestKeysDown().Remove(EKeys::F);
			Next(22);
			return true;
		}
		case 22: // 앉았나(복제된 조종석 주인이 나인가)
		{
			const APGBattleshipActor* Ship = Cast<APGBattleshipActor>(Test->RideActor.Get());
			if (IsValid(Ship) && Ship->GetSeatedPawn() == Pawn && IsValid(Pawn))
			{
				if (InStep < 1.0)
					return true;
				Test->DriveStart = Ship->GetActorLocation();
				Test->StartYaw = Ship->GetActorRotation().Yaw;
				PGKeyPolling::TestKeysDown().Add(EKeys::W);
				PGKeyPolling::TestKeysDown().Add(EKeys::D);
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: seated at the helm (view target %s) — holding W + D"),
					*GetNameSafe(PC->GetViewTarget()));
				Next(23);
				return true;
			}
			if (InStep > 6.0)
			{
				Finish(Test, false, FString::Printf(TEXT("not seated 6s after F (seated pawn on this screen: %s)"), IsValid(Ship) ? *GetNameSafe(Ship->GetSeatedPawn()) : TEXT("no ship")));
				return false;
			}
			return true;
		}
		case 23: // 6초 몰아 본다
		{
			const APGBattleshipActor* Ship = Cast<APGBattleshipActor>(Test->RideActor.Get());
			if (InStep < 6.0)
			{
				// 떨림 재기(9/28 "우주선 움직임이 버벅거린다"): 1초 뒤부터 프레임마다 이 화면의 배가 움직였나.
				if (IsValid(Ship))
				{
					const FVector Here = Ship->GetActorLocation();
					if (InStep > 1.0 && !Test->LastSample.IsZero())
					{
						const float Step = FVector::Dist(Here, Test->LastSample);
						++Test->Frames;
						Test->StillFrames += Step < 1.0f ? 1 : 0;
						if (Step > Test->BiggestStep)
						{
							Test->BiggestStep = Step;
							Test->BiggestStepFrameMs = static_cast<float>(FMath::Max(Now - Test->LastSampleAt, 0.0001)) * 1000.0f;
						}
					}
					Test->LastSample = Here;
					Test->LastSampleAt = Now;
				}
				return true;
			}
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: ship smoothness on this screen — %d frames, %d frames did not move (%.0f%%), biggest single-frame jump %.0fcm (that frame %.0f ms)"),
				Test->Frames, Test->StillFrames, Test->Frames > 0 ? 100.0f * Test->StillFrames / Test->Frames : 0.0f, Test->BiggestStep, Test->BiggestStepFrameMs);
			PGKeyPolling::TestKeysDown().Reset();
			const float Moved = IsValid(Ship) ? FVector::Dist2D(Ship->GetActorLocation(), Test->DriveStart) : 0.0f;
			const float Turned = IsValid(Ship) ? FMath::Abs(FMath::FindDeltaAngleDegrees(Test->StartYaw, Ship->GetActorRotation().Yaw)) : 0.0f;
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: the ship moved %.0fcm and turned %.1f deg in 6s on this screen"), Moved, Turned);
			if (Moved < 300.0f && Turned < 3.0f)
			{
				Finish(Test, false, TEXT("the ship did not respond to W/D from this client"));
				return false;
			}
			// 이어서 오른쪽 버튼(오르기) + 왼쪽 버튼(주포)을 4초.
			Test->DriveStart = IsValid(Ship) ? Ship->GetActorLocation() : FVector::ZeroVector;
			PGKeyPolling::TestKeysDown().Add(EKeys::RightMouseButton);
			PGKeyPolling::TestKeysDown().Add(EKeys::LeftMouseButton);
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: holding right mouse (climb) + left mouse (cannon) for 4s"));
			Next(26);
			return true;
		}
		case 26: // 오르기·주포 4초 — 이 화면에서 배가 올라갔나(주포는 서버 "cannon N fired" 와 클라 "cannon fx on this screen" 로그로 본다)
		{
			const APGBattleshipActor* Ship = Cast<APGBattleshipActor>(Test->RideActor.Get());
			if (InStep < 4.0)
				return true;
			PGKeyPolling::TestKeysDown().Reset();
			const float Up = IsValid(Ship) ? static_cast<float>(Ship->GetActorLocation().Z - Test->DriveStart.Z) : 0.0f;
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: the ship climbed %.0fcm in 4s on this screen"), Up);
			if (Up < 100.0f)
			{
				Finish(Test, false, TEXT("the ship did not climb on right mouse from this client"));
				return false;
			}
			Next(24);
			return true;
		}
		case 24: // F 로 일어선다
		{
			if (InStep < 1.0)
				return true;
			if (InStep < 1.2)
			{
				PGKeyPolling::TestKeysDown().Add(EKeys::F);
				return true;
			}
			PGKeyPolling::TestKeysDown().Reset();
			Next(25);
			return true;
		}
		case 25:
		{
			const APGBattleshipActor* Ship = Cast<APGBattleshipActor>(Test->RideActor.Get());
			if (IsValid(Ship) && Ship->GetSeatedPawn() == nullptr)
			{
				if (InStep < 1.0)
					return true;
				const bool bViewBack = PC->GetViewTarget() == Pawn;
				Finish(Test, bViewBack, FString::Printf(TEXT("stood up — view target back on the character=%d"), bViewBack ? 1 : 0));
				return false;
			}
			if (InStep > 6.0)
			{
				Finish(Test, false, TEXT("still seated 6s after F"));
				return false;
			}
			return true;
		}
		case 30: // 배 위에 서 있기 시험: 떠 있는(멈춘) 배 갑판에 올라올 때까지(서버 명령 PG.Finale.Board 1) 기다린다
		{
			APGBattleshipActor* Ship = nullptr;
			for (TActorIterator<APGBattleshipActor> It(World); It; ++It)
				Ship = *It;
			if (Now - Test->Began > 300.0)
			{
				Finish(Test, false, TEXT("never stood on the hovering ship within 300s"));
				return false;
			}
			if (!IsValid(Ship) || !IsValid(Pawn) || Ship->GetMotion() != EPGShipMotion::Hover)
				return true;
			const FVector Local = Ship->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
			const ACharacter* Walker = Cast<ACharacter>(Pawn);
			// 갑판 위에 발을 붙이고 섰나(선체 안쪽 + 떨어지는 중이 아님). 옮겨진 직후 떨어져 내리는 동안은 기다린다.
			if (FVector::Dist(Pawn->GetActorLocation(), Ship->GetActorLocation()) > 15000.0f || !Walker || Walker->GetCharacterMovement()->IsFalling())
				return true;
			if (InStep < 3.0)
				return true; // 발이 붙고 3초 가만히
			Test->RideActor = Ship;
			Test->DriveStart = Local;              // 배 기준 내 자리(배가 움직여도 이 값이 그대로여야 한다)
			Test->StartYaw = 0.0f;                  // 이 시험에서는 "가장 크게 어긋난 거리" 로 쓴다
			Test->BestDistance = 0.0f;              // 배가 움직인 거리
			Test->Walker = Pawn;
			Test->LastProgressAt = Now;
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: standing on the hovering ship at local (%.0f, %.0f, %.0f) — waiting for someone to drive it"),
				Local.X, Local.Y, Local.Z);
			Next(31);
			return true;
		}
		case 31: // 누가 몰기 시작하면 1초마다 "배 기준 내 자리" 가 얼마나 어긋났나 잰다
		{
			const APGBattleshipActor* Ship = Cast<APGBattleshipActor>(Test->RideActor.Get());
			if (!IsValid(Ship) || !IsValid(Pawn))
			{
				Finish(Test, false, TEXT("ship or character gone"));
				return false;
			}
			const FVector Local = Ship->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
			const float Drift = FVector::Dist(Local, Test->DriveStart);
			if (Ship->GetSeatedPawn() != nullptr && !Test->LoggedSkip.Contains(Test->RideActor))
			{
				Test->LoggedSkip.Add(Test->RideActor);
				Test->StepStart = Now; // 누가 앉은 때부터 잰다
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: someone took the helm — measuring my place on the deck while the ship moves"));
				Test->DriveStart = Local;     // 앉은 순간의 내 자리부터 잰다
				Test->LastProgressAt = Now;
				return true;                  // InStep 은 이번 틱 앞에서 잰 값이라 다음 틱부터 센다
			}
			if (!Test->LoggedSkip.Contains(Test->RideActor))
			{
				if (Now - Test->Began > 300.0)
				{
					Finish(Test, false, TEXT("nobody drove the ship"));
					return false;
				}
				return true;
			}
			if (Drift > 300.0f && Drift > Test->StartYaw)
			{
				// 크게 어긋난 순간의 사정(9/28 진단): 밟은 바닥, 배가 이번 프레임에 움직인 거리, 내 속도.
				const ACharacter* Walker = Cast<ACharacter>(Pawn);
				const UPrimitiveComponent* Floor = Walker ? Walker->GetMovementBase() : nullptr;
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: deck slip %.0fcm at t=%.2fs — floor %s, ship moved %.0fcm this frame, my speed %.0f, mode %d"),
					Drift, InStep, Floor ? *FString::Printf(TEXT("%s.%s"), *GetNameSafe(Floor->GetOwner()), *Floor->GetName()) : TEXT("none"),
					FVector::Dist(Ship->GetActorLocation(), Test->LastSample), Pawn->GetVelocity().Size(),
					Walker ? static_cast<int32>(Walker->GetCharacterMovement()->MovementMode.GetValue()) : -1);
			}
			Test->LastSample = Ship->GetActorLocation();
			Test->StartYaw = FMath::Max(Test->StartYaw, Drift);
			if (Now - Test->LastProgressAt >= 1.0)
			{
				Test->LastProgressAt = Now;
				const FVector ShipNow = Ship->GetActorLocation();
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: deck t=%.0fs ship at %s, my place off by %.0fcm (worst %.0fcm), falling=%d"),
					InStep, *ShipNow.ToCompactString(), Drift, Test->StartYaw,
					Cast<ACharacter>(Pawn) && Cast<ACharacter>(Pawn)->GetCharacterMovement()->IsFalling() ? 1 : 0);
			}
			if (InStep < 16.0)
				return true;
			// 배 기준으로 3m 넘게 밀렸으면 실패(서 있기만 했는데 미끄러졌거나 떨어졌다).
			const bool bPass = Test->StartYaw < 300.0f;
			Finish(Test, bPass, FString::Printf(TEXT("stood on the deck while the ship was driven for 16s — worst slip %.0fcm"), Test->StartYaw));
			return false;
		}
		case 0: // 시작 자리에 선 캐릭터를 기다린다
		{
			if (Now - Test->Began > 180.0)
			{
				Finish(Test, false, TEXT("no local character at a spawn point within 180s"));
				return false;
			}
			// 서버가 이미 태워 줬으면(서버 명령 PG.RideTest — 걸어가기가 담장에 막히는 자리용) 바로 운전 시험으로.
			if (IsValid(Pawn) && (Test->Kind == TEXT("tank") ? Pawn->IsA<APGTankPawn>() : Pawn->IsA<APGVehiclePawn>()))
			{
				if (const IPGRideable* Rideable = Cast<IPGRideable>(Pawn))
					Test->Walker = Rideable->GetRiderPawn();
				Test->Ride = Pawn;
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: the server put us in %s — testing drive and dismount"), *Pawn->GetName());
				Next(3);
				return true;
			}
			if (Test->bWaitForServerMount)
				return true;
			const IPGMapInfo* Map = UPGMapInfoSubsystem::FindMap(World);
			// "지점 다 만들었다" 표시는 서버에서만 켜진다 — 클라이언트는 목록이 찼는지로 본다.
			if (!IsValid(Pawn) || !Pawn->IsA<ACharacter>() || !Map || Map->GetLevelDesignPoints().IsEmpty())
				return true;
			const FVector At = Pawn->GetActorLocation();
			const bool bAtSeat = Map->GetLevelDesignPoints().ContainsByPredicate([&At](const FLevelDesignPoint& Point)
			{
				return Point.Type == ELevelDesignPointType::Spawn && FVector::DistSquared2D(Point.WorldLocation, At) < FMath::Square(3000.0f);
			});
			if (!bAtSeat)
			{
				Test->StepStart = Now; // 자리에 온 뒤부터 잰다
				return true;
			}
			if (InStep < 3.0) // 탈것·물건 복제가 올 시간
				return true;
			// 가장 가까운 탈것
			APawn* Best = nullptr;
			APawn* NearestAny = nullptr; // 곧장 못 가더라도 가장 가까운 것(비켜 걸어가 본다)
			double NearestAnyDist = FMath::Square(9000.0);
			double BestDist = FMath::Square(9000.0);
			const bool bTank = Test->Kind == TEXT("tank");
			for (TActorIterator<APawn> It(World); It; ++It)
			{
				const bool bMatch = bTank ? It->IsA<APGTankPawn>() : It->IsA<APGVehiclePawn>();
				const double D = FVector::DistSquared2D(It->GetActorLocation(), At);
				if (!bMatch)
					continue;
				if (D < NearestAnyDist)
				{
					NearestAnyDist = D;
					NearestAny = *It;
				}
				if (D >= BestDist)
					continue;
				// 곧장 걸어갈 수 있는 것만(담장 너머 차는 사람이면 출입구로 돌아가겠지만, 이 시험은 길찾기를 안 한다).
				FCollisionQueryParams Params(SCENE_QUERY_STAT(PGNetRideTestPath), false, Pawn);
				Params.AddIgnoredActor(*It);
				const FVector Goal = It->GetActorLocation() + (At - It->GetActorLocation()).GetSafeNormal2D() * 300.0f;
				FHitResult Block;
				if (World->SweepSingleByChannel(Block, At, FVector(Goal.X, Goal.Y, At.Z), FQuat::Identity, ECC_Pawn,
					FCollisionShape::MakeCapsule(40.0f, 60.0f), Params))
				{
					if (!Test->LoggedSkip.Contains(*It)) { Test->LoggedSkip.Add(*It); UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: %s at %s is behind %s/%s — skipping"), *It->GetName(), *It->GetActorLocation().ToCompactString(), *GetNameSafe(Block.GetActor()), *GetNameSafe(Block.GetComponent())); }
					continue;
				}
				{
					BestDist = D;
					Best = *It;
				}
			}
			// 곧장 갈 수 있는 것이 20초 동안 안 나오면 가장 가까운 것으로(작은 소품은 비켜 걷기로 돌아간다).
			if (!Best && NearestAny && InStep > 23.0)
			{
				Best = NearestAny;
				BestDist = NearestAnyDist;
			}
			if (!Best)
			{
				// 곧장 갈 수 있는 탈것이 아직 없다 — 서버 쪽 시험 명령(PG.Delay … PG.SpawnVehicle)이 옆에 놓아 줄 수 있으니 기다린다.
				if (Now - Test->Began > 150.0)
				{
					Finish(Test, false, FString::Printf(TEXT("no reachable %s within 90m of %s"), *Test->Kind, *At.ToCompactString()));
					return false;
				}
				return true;
			}
			Test->Ride = Best;
			Test->Walker = Pawn;
			Test->LastProgressAt = Now;
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: walking to %s (%.0fm away)"), *Best->GetName(), FMath::Sqrt(BestDist) * 0.01);
			Next(1);
			return true;
		}
		case 1: // 걸어간다
		{
			APawn* Ride = Test->Ride.Get();
			if (!IsValid(Ride) || !IsValid(Pawn))
			{
				Finish(Test, false, TEXT("ride or character vanished while walking"));
				return false;
			}
			const FVector To = Ride->GetActorLocation() - Pawn->GetActorLocation();
			// 탈것 바로 옆까지(F 조준은 카메라에서 4m 까지만 닿는다 — 사람도 차 옆에 붙어서 누른다).
			const bool bStuck = Now - Test->LastProgressAt > 1.5;
			if (To.Size2D() < 330.0f)
			{
				Next(2);
				return true;
			}
			if (To.Size2D() < Test->BestDistance - 20.0f)
			{
				Test->BestDistance = To.Size2D();
				Test->LastProgressAt = Now;
			}
			// 막혔으면 1초 동안 옆으로 비켜 걷는다(소품·다른 차·방어막).
			if (bStuck)
			{
				Pawn->AddMovementInput(FVector::CrossProduct(To.GetSafeNormal2D(), FVector::UpVector) * (Test->SideSign), 1.0f);
				if (Now - Test->LastProgressAt > 2.5)
				{
					Test->LastProgressAt = Now;
					Test->SideSign = -Test->SideSign;
				}
				return true;
			}
			if (InStep > 30.0)
			{
				Finish(Test, false, FString::Printf(TEXT("could not reach %s (still %.0fm, at %s)"), *Ride->GetName(), To.Size2D() * 0.01f, *Pawn->GetActorLocation().ToCompactString()));
				return false;
			}
			PC->SetControlRotation(FRotator(-10.0f, To.Rotation().Yaw, 0.0f));
			Pawn->AddMovementInput(To.GetSafeNormal2D(), 1.0f);
			return true;
		}
		case 2: // 겨누고 F
		{
			APawn* Ride = Test->Ride.Get();
			if (!IsValid(Ride) || !IsValid(Pawn))
			{
				Finish(Test, false, TEXT("ride or character vanished before interacting"));
				return false;
			}
			FVector ViewLocation;
			FRotator ViewRotation;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			// 탈것 윗부분을 겨눈다(앞에 놓인 연료통·상자에 시선이 먼저 걸리지 않게).
			PC->SetControlRotation((Ride->GetActorLocation() + FVector(0.0f, 0.0f, 90.0f) - ViewLocation).Rotation());
			if (InStep < 0.3)
				return true; // 시선이 돈 뒤 한 틱 쉬고 누른다
			UPGInteractionComponent* Interaction = Pawn->FindComponentByClass<UPGInteractionComponent>();
			if (!Interaction)
			{
				Finish(Test, false, TEXT("character has no UPGInteractionComponent"));
				return false;
			}
			const AActor* Target = Interaction->RefreshTarget();
			// 차 앞에 연료통·상자가 있으면 그게 먼저 잡힌다 — 사람처럼 마우스 휠로 후보를 넘겨 탈것을 고른다.
			for (int32 Tries = 0; Tries < 8 && Target != Ride; ++Tries)
			{
				Interaction->CycleTarget(1);
				Target = Interaction->GetCurrentTarget();
			}
			// 그래도 탈것이 안 잡히면 누르지 않고 한 걸음 다가가 다시 겨눈다(9/28: 차 앞 시작 연료통을 눌러 주워 버리고 시험이 멈췄다).
			if (Target != Ride)
			{
				if (InStep > 8.0)
				{
					Finish(Test, false, FString::Printf(TEXT("could not target %s (still %s)"), *Ride->GetName(), *GetNameSafe(Target)));
					return false;
				}
				Pawn->AddMovementInput((Ride->GetActorLocation() - Pawn->GetActorLocation()).GetSafeNormal2D(), 1.0f);
				return true;
			}
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: pressing F, target=%s"), *GetNameSafe(Target));
			Interaction->BeginInteract();
			Interaction->EndInteract();
			Next(3);
			return true;
		}
		case 3: // 탔나(조종이 탈것으로 넘어왔나)
		{
			APawn* Ride = Test->Ride.Get();
			if (IsValid(Ride) && Pawn == Ride)
			{
				if (InStep < 1.0)
					return true; // 탑승 F 가 하차로 읽히던 문제를 보려면 1초 두고 본다
				Test->DriveStart = Ride->GetActorLocation();
				PGKeyPolling::TestKeysDown().Add(EKeys::W);
				if (Test->Kind == TEXT("fly"))
					PGKeyPolling::TestKeysDown().Add(EKeys::RightMouseButton);
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: mounted %s, holding %s"), *Ride->GetName(), Test->Kind == TEXT("fly") ? TEXT("W + right mouse") : TEXT("W"));
				Next(4);
				return true;
			}
			if (InStep > 6.0)
			{
				Finish(Test, false, FString::Printf(TEXT("not mounted after 6s (controlling %s)"), *GetNameSafe(Pawn)));
				return false;
			}
			return true;
		}
		case 4: // 달린다(나는 시험은 4초 올라간다)
		{
			APawn* Ride = Test->Ride.Get();
			if (!IsValid(Ride) || Pawn != Ride)
			{
				Finish(Test, false, FString::Printf(TEXT("lost control of the ride while driving (controlling %s)"), *GetNameSafe(Pawn)));
				return false;
			}
			if (InStep < 4.0)
			{
				// 나는·탱크 시험: 2초째에 A 를 1초 눌러, 이 화면에서 기수가 5도 돌 때까지 걸린 시간을 잰다(조작 반응).
				if (Test->Kind == TEXT("fly") || Test->Kind == TEXT("tank"))
				{
					if (InStep >= 2.0 && Test->TurnPressedAt < 0.0)
					{
						Test->TurnPressedAt = Now;
						Test->TurnStartYaw = Ride->GetActorRotation().Yaw;
						PGKeyPolling::TestKeysDown().Add(EKeys::A);
					}
					else if (Test->TurnPressedAt > 0.0 && !Test->bTurnLogged
						&& FMath::Abs(FMath::FindDeltaAngleDegrees(Test->TurnStartYaw, Ride->GetActorRotation().Yaw)) > 5.0f)
					{
						Test->bTurnLogged = true;
						UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: turn response on this screen — nose turned 5 deg %.0f ms after pressing A"), (Now - Test->TurnPressedAt) * 1000.0);
					}
					if (InStep >= 3.0)
						PGKeyPolling::TestKeysDown().Remove(EKeys::A);
				}
				// 나는 동안 하차 안내가 비어 있나(9/28: 공중에서 "차량 하차" 가 뜨던 문제). 3.5초째 한 번.
				if (Test->Kind == TEXT("fly") && InStep >= 3.5 && !Test->bPromptChecked)
				{
					Test->bPromptChecked = true;
					if (const APGVehiclePawn* Car = Cast<APGVehiclePawn>(Ride))
						UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: rider prompt while flying on this screen = \"%s\""), *Car->GetRiderPrompt().ToString());
				}
				// 탱크: 1초마다 자리와 높이(가장자리에서 땅 밑으로 빠지나 — 9/28).
				if (Test->Kind == TEXT("tank") && FMath::FloorToInt(InStep) > Test->LastLoggedSecond)
				{
					Test->LastLoggedSecond = FMath::FloorToInt(InStep);
					UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: tank t=%.0fs at %s"), InStep, *Ride->GetActorLocation().ToCompactString());
				}
				// 처음 1초(출발)는 빼고 잰다.
				const FVector Here = Ride->GetActorLocation();
				if (InStep > 1.0 && Test->Frames >= 0 && !Test->LastSample.IsZero())
				{
					const float Step = FVector::Dist(Here, Test->LastSample);
					const float FrameSeconds = static_cast<float>(FMath::Max(Now - Test->LastSampleAt, 0.0001));
					++Test->Frames;
					Test->StillFrames += Step < 1.0f ? 1 : 0;
					if (Step > Test->BiggestStep)
					{
						Test->BiggestStep = Step;
						Test->BiggestStepFrameMs = FrameSeconds * 1000.0f;
					}
					const float FrameSpeed = Step / FrameSeconds;
					if (InStep > 2.0)
					{
						float Avg = 0.0f;
						for (const float Past : Test->RecentSpeeds)
							Avg += Past;
						Avg = Test->RecentSpeeds.Num() > 0 ? Avg / Test->RecentSpeeds.Num() : FrameSpeed;
						++Test->CruiseFrames;
						if (Test->RecentSpeeds.Num() >= 5 && Avg > 500.0f && FrameSpeed < Avg * 0.5f)
							++Test->StallFrames;
						Test->RecentSpeeds.Add(FrameSpeed);
						if (Test->RecentSpeeds.Num() > 10)
							Test->RecentSpeeds.RemoveAt(0);
					}
					if (Test->LastFrameSpeed >= 0.0f)
						Test->BiggestSpeedJump = FMath::Max(Test->BiggestSpeedJump, FMath::Abs(FrameSpeed - Test->LastFrameSpeed));
					Test->LastFrameSpeed = FrameSpeed;
				}
				Test->LastSample = Here;
				Test->LastSampleAt = Now;
				return true;
			}
			PGKeyPolling::TestKeysDown().Reset();
			const FVector Moved = Ride->GetActorLocation() - Test->DriveStart;
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: smoothness on this screen — %d frames, %d frames did not move (%.0f%%), biggest single-frame jump %.0fcm (that frame %.0f ms), biggest frame-to-frame speed change %.0f km/h"),
				Test->Frames, Test->StillFrames, Test->Frames > 0 ? 100.0f * Test->StillFrames / Test->Frames : 0.0f, Test->BiggestStep, Test->BiggestStepFrameMs, Test->BiggestSpeedJump * 0.036f);
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: stalls on this screen — %d of %d cruise frames dropped below half the recent speed"), Test->StallFrames, Test->CruiseFrames);
			if (const UPGFlightKitComponent* Kit = Ride->FindComponentByClass<UPGFlightKitComponent>())
				UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: flight vs server — biggest gap %.0fcm, hard corrections %d"),
					Kit->GetDriverMaxErrorCm(), Kit->GetDriverHardCorrections());
			UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: after 4s on this screen the ride moved %.0fcm (horizontal %.0f, up %.0f)"),
				Moved.Size(), Moved.Size2D(), Moved.Z);
			const bool bMoved = Test->Kind == TEXT("fly") ? Moved.Z > 300.0f : Moved.Size2D() > 300.0f;
			if (!bMoved)
			{
				Finish(Test, false, TEXT("the ride did not move on the driver's screen"));
				return false;
			}
			Next(5);
			return true;
		}
		case 5: // 멈추고 F 로 내린다(날았으면 내려앉을 시간)
		{
			const double Settle = Test->Kind == TEXT("fly") ? 8.0 : 2.0;
			if (InStep < Settle)
				return true;
			if (InStep < Settle + 0.3)
			{
				PGKeyPolling::TestKeysDown().Add(EKeys::F);
				return true;
			}
			PGKeyPolling::TestKeysDown().Reset();
			Next(6);
			return true;
		}
		case 6: // 내렸나, 어디에 섰나
		{
			APawn* Walker = Test->Walker.Get();
			if (IsValid(Walker) && Pawn == Walker)
			{
				if (InStep < 1.5)
					return true; // 선 자리가 오는 시간
				const APawn* Ride = Test->Ride.Get();
				const float FromRide = IsValid(Ride) ? FVector::Dist2D(Walker->GetActorLocation(), Ride->GetActorLocation()) : -1.0f;
				const ACharacter* Character = Cast<ACharacter>(Walker);
				const bool bWalking = Character && Character->GetCharacterMovement()->IsMovingOnGround();
				const bool bVisible = !Walker->IsHidden();
				const bool bCollides = Walker->GetActorEnableCollision();
				const FString Why = FString::Printf(TEXT("dismounted %.0fcm from the ride, on ground=%d visible=%d collision=%d"),
					FromRide, bWalking ? 1 : 0, bVisible ? 1 : 0, bCollides ? 1 : 0);
				// 차 옆 내릴 자리는 차 가운데에서 150cm 남짓이다 — 100cm 넘으면 "차 안에 끼지 않았다" 로 본다.
				Finish(Test, FromRide > 100.0f && bWalking && bVisible && bCollides, Why);
				return false;
			}
			if (InStep > 6.0)
			{
				Finish(Test, false, FString::Printf(TEXT("still controlling %s 6s after F"), *GetNameSafe(Pawn)));
				return false;
			}
			return true;
		}
		default:
			return false;
		}
	}

	void Start(const TArray<FString>& Args, UWorld*)
	{
		TSharedRef<FRideTest> Test = MakeShared<FRideTest>();
		Test->Kind = Args.Num() > 0 ? Args[0].ToLower() : TEXT("car");
		Test->bWaitForServerMount = Args.Contains(TEXT("mounted"));
		if (Test->Kind != TEXT("car") && Test->Kind != TEXT("tank") && Test->Kind != TEXT("fly") && Test->Kind != TEXT("helm") && Test->Kind != TEXT("deck"))
			Test->Kind = TEXT("car");
		UE_LOG(LogPGObjects, Display, TEXT("NetRideTest: started (%s) — waiting for the local character at a spawn point"), *Test->Kind);
		Test->Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Test](float Delta) { return Tick(Delta, Test); }), 0.0f);
	}

	// PG.HitchLog   80ms 넘는 프레임을 로그로 남긴다(9/28 "드래곤 등장할 때 프레임 드랍" 찾기). 서버·클라 어디서나.
	//   무엇이 느렸는지는 같은 시각의 다른 줄(PGFinale·PGCollapse·PGDragon)과 맞춰 본다.
	static void HitchLog(const TArray<FString>& Args, UWorld* World)
	{
		static FTSTicker::FDelegateHandle Handle;
		if (Handle.IsValid())
			return;
		TSharedRef<double> Last = MakeShared<double>(FPlatformTime::Seconds());
		Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Last](float)
		{
			const double Now = FPlatformTime::Seconds();
			const double Ms = (Now - *Last) * 1000.0;
			*Last = Now;
			if (Ms > 80.0)
				UE_LOG(LogPGObjects, Display, TEXT("Hitch: frame took %.0f ms"), Ms);
			return true;
		}), 0.0f);
		UE_LOG(LogPGObjects, Display, TEXT("PG.HitchLog: logging frames over 80 ms"));
	}

	// PG.NetContainerWatch [처음 초] [다음 초]   (클라 확인용) 이 화면 사람이 생기고 처음 초 뒤에 모든 상자 자리를 적고,
	//   다음 초 뒤에 50cm 넘게 움직인 상자를 찍는다. 접속하며 월드가 바뀌어도 살아 있게 코어 틱커로 돈다(PG.Delay 는 월드와 같이 지워진다).
	static void NetContainerWatch(const TArray<FString>& Args, UWorld* World)
	{
		const double FirstAt = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 20.0;
		const double SecondAt = Args.Num() > 1 ? FCString::Atod(*Args[1]) : 60.0;
		struct FWatch { double ReadyAt = -1.0; bool bRemembered = false; TMap<TWeakObjectPtr<AActor>, FVector> Seen; };
		TSharedRef<FWatch> Watch = MakeShared<FWatch>();
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Watch, FirstAt, SecondAt](float)
		{
			APlayerController* PC = nullptr;
			UWorld* Game = FindLocalGameWorld(PC);
			if (!Game || !PC || !PC->GetPawn())
				return true;
			const double Now = FPlatformTime::Seconds();
			if (Watch->ReadyAt < 0.0)
				Watch->ReadyAt = Now;
			if (!Watch->bRemembered && Now - Watch->ReadyAt >= FirstAt)
			{
				Watch->bRemembered = true;
				for (TActorIterator<AItemContainerActor> It(Game); It; ++It)
					Watch->Seen.Add(*It, It->GetActorLocation());
				UE_LOG(LogPGObjects, Display, TEXT("NetContainerWatch: remembered %d container(s)"), Watch->Seen.Num());
				return true;
			}
			if (Watch->bRemembered && Now - Watch->ReadyAt >= SecondAt)
			{
				int32 Moved = 0;
				for (const TPair<TWeakObjectPtr<AActor>, FVector>& Pair : Watch->Seen)
					if (Pair.Key.IsValid() && FVector::Dist(Pair.Key->GetActorLocation(), Pair.Value) > 50.0f)
					{
						++Moved;
						UE_LOG(LogPGObjects, Display, TEXT("NetContainerWatch: %s moved %.0fcm on this screen"), *Pair.Key->GetName(), FVector::Dist(Pair.Key->GetActorLocation(), Pair.Value));
					}
				UE_LOG(LogPGObjects, Display, TEXT("NetContainerWatch: %d of %d container(s) moved"), Moved, Watch->Seen.Num());
				return false;
			}
			return true;
		}), 0.5f);
	}
	// PG.NetSmashTest [몰 초]   (클라·서버 어디서나) 1초마다 이 컴퓨터의 프레임 시간(평균·가장 긴 것·50ms 넘은 수)과 잔해 수를 적는다.
	//   이 화면 사람이 로봇을 타고 있으면(서버 PG.SmashCoreTest) W 를 누르고 1.2초마다 왼쪽 버튼으로 휘두르며 앞으로 몬다.
	//   9/28 "중앙 건물 박살 낼 때 프레임 드랍" — 부수는 사람·구경하는 사람·서버 셋의 프레임을 같은 시각으로 맞춰 본다.
	static void NetSmashTest(const TArray<FString>& Args, UWorld*)
	{
		struct FSmash
		{
			double DriveSeconds = 20.0;
			double StandSeconds = 0.0; // 타고 나서 W 를 누르기 전에 가만히 서 있는 시간(부수기 없이 건물만 보는 비용 비교)
			double MountedAt = -1.0;
			double Last = -1.0;
			double SecondStart = -1.0;
			int32 Frames = 0;
			double SumMs = 0.0;
			double WorstMs = 0.0;
			int32 Over50 = 0;
			// 어디가 느린가: 게임(계산)·렌더(그리기 준비)·GPU(그리기) 시간의 1초 평균과 최대(ms).
			double GameSum = 0.0, RenderSum = 0.0, GpuSum = 0.0, GameMax = 0.0, RenderMax = 0.0, GpuMax = 0.0;
			int32 DebrisAtSecond = 0;
			int32 Second = 0;
			double DriveBegan = -1.0;
			double LastSwing = -1.0;
			bool bDone = false;
			// 모는 동안 전체
			int32 DriveFrames = 0;
			double DriveWorst = 0.0;
			int32 DriveOver50 = 0;
			int32 DriveOver100 = 0;
		};
		TSharedRef<FSmash> S = MakeShared<FSmash>();
		if (Args.Num() > 0)
			S->DriveSeconds = FMath::Clamp(FCString::Atod(*Args[0]), 3.0, 120.0);
		if (Args.Num() > 1)
			S->StandSeconds = FMath::Clamp(FCString::Atod(*Args[1]), 0.0, 60.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float)
		{
			const double Now = FPlatformTime::Seconds();
			if (S->Last < 0.0)
			{
				S->Last = S->SecondStart = Now;
				return true;
			}
			const double Ms = (Now - S->Last) * 1000.0;
			S->Last = Now;
			++S->Frames;
			S->SumMs += Ms;
			S->WorstMs = FMath::Max(S->WorstMs, Ms);
			S->Over50 += Ms > 50.0 ? 1 : 0;
			{
				const double Game = FPlatformTime::ToMilliseconds(GGameThreadTime);
				const double Render = FPlatformTime::ToMilliseconds(GRenderThreadTime);
				const double Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
				S->GameSum += Game; S->RenderSum += Render; S->GpuSum += Gpu;
				S->GameMax = FMath::Max(S->GameMax, Game); S->RenderMax = FMath::Max(S->RenderMax, Render); S->GpuMax = FMath::Max(S->GpuMax, Gpu);
			}
			const bool bDriving = S->DriveBegan > 0.0 && !S->bDone;
			if (bDriving)
			{
				++S->DriveFrames;
				S->DriveWorst = FMath::Max(S->DriveWorst, Ms);
				S->DriveOver50 += Ms > 50.0 ? 1 : 0;
				S->DriveOver100 += Ms > 100.0 ? 1 : 0;
			}

			// 로봇 몰기(이 화면 사람이 로봇에 탔을 때만)
			APlayerController* PC = nullptr;
			UWorld* Game = FindLocalGameWorld(PC);
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!S->bDone && Game && Cast<APGRobotCharacter>(Pawn))
			{
				if (S->MountedAt < 0.0)
				{
					S->MountedAt = Now;
					PC->SetControlRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw, 0.0f));
					if (S->StandSeconds > 0.0)
						UE_LOG(LogPGObjects, Display, TEXT("NetSmashTest: riding %s — standing still for %.0fs first"), *Pawn->GetName(), S->StandSeconds);
				}
				if (Now - S->MountedAt < S->StandSeconds)
				{
					// 서 있기: 아무 키도 안 누른다
				}
				else if (S->DriveBegan < 0.0)
				{
					S->DriveBegan = Now;
					PC->SetControlRotation(FRotator(0.0f, Pawn->GetActorRotation().Yaw, 0.0f));
					PGKeyPolling::TestKeysDown().Add(EKeys::W);
					UE_LOG(LogPGObjects, Display, TEXT("NetSmashTest: riding %s at %s — holding W and swinging for %.0fs"), *Pawn->GetName(), *Pawn->GetActorLocation().ToCompactString(), S->DriveSeconds);
				}
				// 서 있는 동안은 휘두르지도, 끝내지도 않는다(9/28 버그: 서 있기를 켜면 DriveBegan 이 아직 -1 이라 첫 틱에 "다 몰았다" 로 끝났다).
				const bool bDriveStarted = S->DriveBegan > 0.0;
				// 휘두르기: 0.1초 누르고 뗀다(누른 순간만 공격으로 읽는다)
				if (bDriveStarted && Now - S->LastSwing > 1.2)
				{
					S->LastSwing = Now;
					PGKeyPolling::TestKeysDown().Add(EKeys::LeftMouseButton);
				}
				else if (bDriveStarted && Now - S->LastSwing > 0.1)
					PGKeyPolling::TestKeysDown().Remove(EKeys::LeftMouseButton);
				if (bDriveStarted && Now - S->DriveBegan > S->DriveSeconds)
				{
					S->bDone = true;
					PGKeyPolling::TestKeysDown().Reset();
					// Insights 기록 중이면 여기서 닫는다(시험 도구가 프로세스를 끄면 파일이 비어 버린다).
					if (GEngine)
						GEngine->Exec(Game, TEXT("Trace.Stop"));
					UE_LOG(LogPGObjects, Display, TEXT("NetSmashTest: drive done on this screen — %d frames, worst %.0f ms, over 50 ms %d, over 100 ms %d, robot now at %s"),
						S->DriveFrames, S->DriveWorst, S->DriveOver50, S->DriveOver100, *Pawn->GetActorLocation().ToCompactString());
				}
			}

			if (Now - S->SecondStart >= 1.0)
			{
				++S->Second;
				const int32 Debris = PGPhysicsUtil::DebrisMadeCount;
				// 쉬는 동안(프레임이 고르고 잔해가 안 생길 때)은 10초에 한 줄만.
				const bool bQuiet = S->WorstMs < 40.0 && Debris == S->DebrisAtSecond && !(S->MountedAt > 0.0 && !S->bDone);
				if (!bQuiet || S->Second % 10 == 0)
				{
					const double N = FMath::Max(S->Frames, 1);
					UE_LOG(LogPGObjects, Display, TEXT("NetSmashTest: t=%d frames=%d avg=%.1fms worst=%.0fms over50=%d debris+%d (remote %d, predicted %d) game %.1f/%.0f render %.1f/%.0f gpu %.1f/%.0f replay find/launch/remove %.1f/%.1f/%.1fms%s%s"),
						S->Second, S->Frames, S->Frames > 0 ? S->SumMs / S->Frames : 0.0, S->WorstMs, S->Over50, Debris - S->DebrisAtSecond,
						PGPhysicsUtil::RemoteKnockCount, PGPhysicsUtil::PredictedKnockCount,
						S->GameSum / N, S->GameMax, S->RenderSum / N, S->RenderMax, S->GpuSum / N, S->GpuMax,
						PGPhysicsUtil::RemoteFindMs, PGPhysicsUtil::RemoteLaunchMs, PGPhysicsUtil::RemoteRemoveMs, bDriving ? TEXT(" [driving]") : TEXT(""),
						S->MountedAt > 0.0 && S->DriveBegan < 0.0 ? TEXT(" [standing]") : TEXT(""));
				}
				S->DebrisAtSecond = Debris;
				S->SecondStart = Now;
				S->Frames = 0;
				S->SumMs = 0.0;
				S->WorstMs = 0.0;
				S->Over50 = 0;
				S->GameSum = S->RenderSum = S->GpuSum = S->GameMax = S->RenderMax = S->GpuMax = 0.0;
				PGPhysicsUtil::RemoteFindMs = PGPhysicsUtil::RemoteLaunchMs = PGPhysicsUtil::RemoteRemoveMs = 0.0;
			}
			return true;
		}), 0.0f);
		UE_LOG(LogPGObjects, Display, TEXT("NetSmashTest: frame log started"));
	}
	// PG.LightAudit [기다릴 초] [그림자 0|1]   (클라 확인용) 이 화면 사람이 생기고 N 초 뒤 월드의 점·스포트 조명을 레벨별로 센다
	//   (켜진 것·그림자 드리우는 것). 두 번째 값을 주면 그림자 드리우기를 전부 그 값으로 바꾼다(비교 측정용).
	//   9/28 "중앙 건물 부술 때 프레임 드랍" — 건물 앞에 서 있기만 해도 그리기가 두 배였다. VSM 원패스 조명 넘침 경고와 같이 본다.
	static void LightAudit(const TArray<FString>& Args, UWorld*)
	{
		const double WaitSeconds = Args.Num() > 0 ? FCString::Atod(*Args[0]) : 30.0;
		const int32 SetShadows = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : -1;
		TSharedRef<double> ReadyAt = MakeShared<double>(-1.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([ReadyAt, WaitSeconds, SetShadows](float)
		{
			APlayerController* PC = nullptr;
			UWorld* Game = FindLocalGameWorld(PC);
			if (!Game || !PC || !PC->GetPawn())
				return true;
			const double Now = FPlatformTime::Seconds();
			if (*ReadyAt < 0.0)
				*ReadyAt = Now;
			if (Now - *ReadyAt < WaitSeconds)
				return true;
			TMap<FString, FIntVector> PerLevel; // 전체·켜짐·그림자
			for (TObjectIterator<ULocalLightComponent> It; It; ++It)
			{
				ULocalLightComponent* Light = *It;
				if (!IsValid(Light) || Light->GetWorld() != Game || !Light->IsRegistered())
					continue;
				const FString Level = Light->GetOwner() && Light->GetOwner()->GetLevel() ? Light->GetOwner()->GetLevel()->GetOuter()->GetName() : TEXT("?");
				FIntVector& Count = PerLevel.FindOrAdd(Level);
				++Count.X;
				Count.Y += Light->IsVisible() && Light->Intensity > 0.0f ? 1 : 0;
				Count.Z += Light->CastShadows ? 1 : 0;
				if (SetShadows >= 0)
					Light->SetCastShadows(SetShadows != 0);
			}
			for (const TPair<FString, FIntVector>& Pair : PerLevel)
				UE_LOG(LogPGObjects, Display, TEXT("LightAudit: %s — %d local lights, %d on, %d cast shadows"), *Pair.Key, Pair.Value.X, Pair.Value.Y, Pair.Value.Z);
			if (SetShadows >= 0)
				UE_LOG(LogPGObjects, Display, TEXT("LightAudit: set every local light's shadows to %d"), SetShadows);
			return false;
		}), 0.5f);
	}
	static FAutoConsoleCommandWithWorldAndArgs LightAuditCommand(TEXT("PG.LightAudit"),
		TEXT("Client test: N seconds after the local pawn appears, counts point/spot lights per level (optionally sets their shadows 0/1)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&LightAudit));

	// PG.AnnounceDedupeTest   (클라 확인용) 이 화면 사람이 생기면 같은 안내를 세 번 + 다른 안내 한 번 넣는다.
	//   로그 "PGAnnounce: ... queued (N waiting)" 의 N 이 1,1,1,2 면 같은 문구가 줄을 서지 않는 것이다(9/28 "변신 중" 세 번).
	static void AnnounceDedupeTest(const TArray<FString>&, UWorld*)
	{
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([](float)
		{
			APlayerController* PC = nullptr;
			UWorld* Game = FindLocalGameWorld(PC);
			if (!Game || !PC || !PC->GetPawn())
				return true;
			if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(PC->GetPawn()))
			{
				for (int32 Try = 0; Try < 3; ++Try)
					Announcer->Announce({ NSLOCTEXT("TransformNPC", "Busy", "변신 중") });
				Announcer->Announce({ NSLOCTEXT("TransformNPC", "NeedFuel", "연료통이 필요합니다") });
				UE_LOG(LogPGObjects, Display, TEXT("AnnounceDedupeTest: done"));
			}
			return false;
		}), 1.0f);
	}
	// PG.GirlSpamTest   (클라 확인용, 서버에서 PG.SpawnGirl fueled 와 같이) 여고생이 10m 안에 보이면 0.3초마다 F 를 12번 누른다.
	//   왜(9/28): 변신 중 F 를 눌러도 "변신 중" 안내가 뜨지 않게 했다 — 누른 동안 "PGAnnounce: ... queued" 줄이 안 나와야 한다.
	static void GirlSpamTest(const TArray<FString>&, UWorld*)
	{
		TSharedRef<int32> Presses = MakeShared<int32>(0);
		TSharedRef<double> NextAt = MakeShared<double>(0.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Presses, NextAt](float)
		{
			APlayerController* PC = nullptr;
			UWorld* Game = FindLocalGameWorld(PC);
			APawn* Pawn = PC ? PC->GetPawn() : nullptr;
			if (!Game || !IsValid(Pawn) || FPlatformTime::Seconds() < *NextAt)
				return true;
			UPGInteractionComponent* Interaction = Pawn->FindComponentByClass<UPGInteractionComponent>();
			const AActor* Target = Interaction ? Interaction->RefreshTarget() : nullptr;
			// 연료통(바닥 아이템)이나 여고생이 보이면 시작한다. 연료통을 먼저 주워 건네는 길(PG.SpawnGirl, fueled 없이)도 잰다.
			if (*Presses == 0 && (!Target || !(Target->GetClass()->GetName().Contains(TEXT("TransformNPC")) || Target->GetClass()->GetName().Contains(TEXT("FloorItem")))))
				return true;
			Interaction->BeginInteract();
			Interaction->EndInteract();
			UE_LOG(LogPGObjects, Display, TEXT("GirlSpamTest: press %d on %s (bag Fuel=%d)"), ++*Presses, *GetNameSafe(Target),
				UPGItemReceiverLibrary::HasItem(Pawn, TEXT("Fuel"), 1) ? 1 : 0);
			*NextAt = FPlatformTime::Seconds() + 0.6;
			if (*Presses < 12)
				return true;
			// 9/28 "연료통 있는데 헬기 탑승 왜 안 되냐": 건넨 뒤 이 화면(접속자)의 가방에 연료통이 남아 보이는지.
			UE_LOG(LogPGObjects, Display, TEXT("GirlSpamTest: done — this screen's bag has Fuel=%d"),
				UPGItemReceiverLibrary::HasItem(Pawn, TEXT("Fuel"), 1) ? 1 : 0);
			return false;
		}), 0.0f);
	}
	static FAutoConsoleCommandWithWorldAndArgs GirlSpamTestCommand(TEXT("PG.GirlSpamTest"),
		TEXT("Client test: once the transform NPC is the interaction target, presses F 12 times 0.3 s apart (no 'busy' line should be announced)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&GirlSpamTest));

	// PG.ExitClusterTest   (서버) 탈출구 두 개가 겹친 자리(검문소 영역 안의 헬기·차)를 찾아 첫 번째 사람을 거기 세우고,
	//   연료통 2개를 준 뒤 두 곳 모두에 "들어옴" 과 F 를 동시에 일으킨다. 초를 세는 곳이 하나뿐이어야 한다.
	//   왜(9/28): 검문소 출구와 헬기가 같이 세면서 접속자 화면에 두 문구가 번갈아 떴다. 접속자 로그 "PGCountdown:" 줄로 순서를 본다.
	static void ExitClusterTest(const TArray<FString>&, UWorld* World)
	{
		APawn* Pawn = nullptr;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It && !Pawn; ++It)
			if (It->IsValid() && (*It)->GetPawn())
				Pawn = (*It)->GetPawn();
		if (!IsValid(Pawn))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("ExitClusterTest: no player pawn"));
			return;
		}
		TArray<APGExtractionZoneActor*> Zones;
		for (TActorIterator<APGExtractionZoneActor> It(World); It; ++It)
			if (It->GetActorEnableCollision())
				Zones.Add(*It);
		TArray<APGExtractionZoneActor*> Cluster;
		for (APGExtractionZoneActor* A : Zones)
		{
			Cluster = { A };
			for (APGExtractionZoneActor* B : Zones)
				if (B != A && FVector::Dist2D(A->GetActorLocation(), B->GetActorLocation()) < 2000.0f)
					Cluster.Add(B);
			if (Cluster.Num() >= 2)
				break;
		}
		if (Cluster.Num() < 2)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("ExitClusterTest: no two exits within 20 m"));
			return;
		}
		Pawn->TeleportTo(Cluster[0]->GetActorLocation() + FVector(0.0f, 0.0f, 120.0f), Pawn->GetActorRotation());
		UPGItemReceiverLibrary::GiveItem(Pawn, TEXT("Fuel"), 2);
		TWeakObjectPtr<APawn> WeakPawn(Pawn);
		TArray<TWeakObjectPtr<APGExtractionZoneActor>> WeakCluster(Cluster);
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([WeakPawn, WeakCluster]()
		{
			APawn* P = WeakPawn.Get();
			if (!IsValid(P))
				return;
			for (const TWeakObjectPtr<APGExtractionZoneActor>& Zone : WeakCluster)
			{
				if (!Zone.IsValid())
					continue;
				UE_LOG(LogPGObjects, Display, TEXT("ExitClusterTest: entering + F on %s"), *Zone->GetName());
				Zone->NotifyPawnEntered(P);
				if (IInteractable::Execute_CanInteract(Zone.Get(), P))
					IInteractable::Execute_Interact(Zone.Get(), P);
			}
		}), 2.0f, false);
		UE_LOG(LogPGObjects, Display, TEXT("ExitClusterTest: %d exits near %s, %s placed there with 2 fuel"),
			Cluster.Num(), *Cluster[0]->GetActorLocation().ToCompactString(), *Pawn->GetName());
	}
	static FAutoConsoleCommandWithWorldAndArgs ExitClusterTestCommand(TEXT("PG.ExitClusterTest"),
		TEXT("Server test: stands the first player where two exits overlap and starts both; only one should count down."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ExitClusterTest));

	// PG.PerfLog [초=300]   (화면 그리는 클라 확인용) 1초마다 프레임(평균·가장 긴 것)·게임·렌더·GPU 시간과 텍스처 메모리를 적는다.
	//   왜(9/28 사용자: "4060 8GB 실측부터 해야지"): 집 PC(5060 Ti 16GB)에서 1920x1080 으로 장면별(시작·이동·부수기·드래곤)로 재고
	//   GPU 시간을 4060 기준으로 환산한다. 프로세스 전체 VRAM 은 Tools/wbp/perf_4060.ps1 이 밖에서(윈도우 GPU 카운터) 같이 잰다.
	static void PerfLog(const TArray<FString>& Args, UWorld*)
	{
		struct FPerf { double Until = 0.0; double SecondStart = 0.0; double Last = 0.0; int32 Frames = 0; double Sum = 0.0; double Worst = 0.0;
			int32 Over33 = 0; double AllSum = 0.0; int32 AllFrames = 0; double AllWorst = 0.0; double GpuSum = 0.0; double GpuWorst = 0.0; int32 Seconds = 0; };
		TSharedRef<FPerf> P = MakeShared<FPerf>();
		const double Duration = Args.Num() > 0 ? FMath::Clamp(FCString::Atod(*Args[0]), 5.0, 3600.0) : 300.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([P, Duration](float)
		{
			const double Now = FPlatformTime::Seconds();
			if (P->Until == 0.0)
			{
				// 맵을 다 짓고 이 화면 사람이 선 뒤부터 잰다(불러오는 시간이 "가장 긴 프레임" 에 섞이지 않게).
				APlayerController* PC = nullptr;
				if (!FindLocalGameWorld(PC) || !PC || !PC->GetPawn())
					return true;
				P->Until = Now + Duration;
				P->SecondStart = P->Last = Now;
				return true;
			}
			const double Ms = (Now - P->Last) * 1000.0;
			P->Last = Now;
			++P->Frames;
			P->Sum += Ms;
			P->Worst = FMath::Max(P->Worst, Ms);
			P->Over33 += Ms > 33.4 ? 1 : 0;
			if (Now - P->SecondStart >= 1.0)
			{
				const double Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
				FTextureMemoryStats Tex;
				RHIGetTextureMemoryStats(Tex);
				++P->Seconds;
				P->AllSum += P->Sum;
				P->AllFrames += P->Frames;
				P->AllWorst = FMath::Max(P->AllWorst, P->Worst);
				P->GpuSum += Gpu;
				P->GpuWorst = FMath::Max(P->GpuWorst, Gpu);
				UE_LOG(LogPGObjects, Display, TEXT("PerfLog: t=%d fps=%.0f avg=%.1fms worst=%.1fms over33=%d game=%.1f render=%.1f gpu=%.1f tex_stream=%.0fMB tex_other=%.0fMB tex_pool=%.0fMB"),
					P->Seconds, P->Frames / (Now - P->SecondStart), P->Sum / FMath::Max(1, P->Frames), P->Worst, P->Over33,
					FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime), Gpu,
					Tex.StreamingMemorySize / 1048576.0, Tex.NonStreamingMemorySize / 1048576.0, Tex.TexturePoolSize / 1048576.0);
				P->SecondStart = Now;
				P->Frames = 0;
				P->Sum = P->Worst = 0.0;
				P->Over33 = 0;
			}
			if (Now < P->Until)
				return true;
			UE_LOG(LogPGObjects, Display, TEXT("PerfLog: done — %d s, avg frame %.1f ms (%.0f fps), worst %.1f ms, gpu avg %.1f ms worst %.1f ms"),
				P->Seconds, P->AllSum / FMath::Max(1, P->AllFrames), 1000.0 * P->AllFrames / FMath::Max(1.0, P->AllSum), P->AllWorst,
				P->GpuSum / FMath::Max(1, P->Seconds), P->GpuWorst);
			return false;
		}), 0.0f);
		UE_LOG(LogPGObjects, Display, TEXT("PG.PerfLog: logging every second for %.0f s"), Duration);
	}
	// PG.AfterReady <초> <명령…>   (클라 확인용) 이 화면 사람이 선 뒤 N초 뒤에 콘솔 명령을 한 번 실행한다(예: ProfileGPU).
	//   왜: PG.Delay 는 월드 타이머라 접속하며 맵이 바뀌면 사라진다. 코어 틱커로 돌아 접속 뒤에도 살아 있다(9/28 4060 측정).
	static void AfterReady(const TArray<FString>& Args, UWorld*)
	{
		if (Args.Num() < 2)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PG.AfterReady <seconds> <command...>"));
			return;
		}
		const double Wait = FCString::Atod(*Args[0]);
		const FString Command = FString::Join(TArrayView<const FString>(Args).RightChop(1), TEXT(" "));
		TSharedRef<double> ReadyAt = MakeShared<double>(-1.0);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([ReadyAt, Wait, Command](float)
		{
			APlayerController* PC = nullptr;
			UWorld* Game = FindLocalGameWorld(PC);
			if (!Game || !PC || !PC->GetPawn())
				return true;
			const double Now = FPlatformTime::Seconds();
			if (*ReadyAt < 0.0)
				*ReadyAt = Now;
			if (Now - *ReadyAt < Wait)
				return true;
			UE_LOG(LogPGObjects, Display, TEXT("PG.AfterReady: running \"%s\""), *Command);
			if (GEngine)
				GEngine->Exec(Game, *Command);
			return false;
		}), 0.0f);
	}
	// PG.ISMReport [개수=40]   (화면 그리는 컴퓨터) 인스턴스 메시 묶음을 메시별로 모아 "인스턴스 수 x LOD0 삼각형" 이 큰 순서로 적는다.
	//   나나이트 여부·보이는 거리(끝)·반투명/마스크 재질 수도 같이. 왜(9/28 4060 측정): 시작 지역 GPU 의 절반 이상이 인스턴스 메시였다
	//   (ShowFlag.InstancedStaticMeshes 0 → 13ms 에서 6ms). 어느 메시가 무거운지 이름으로 봐야 고칠 곳이 정해진다.
	static void ISMReport(const TArray<FString>& Args, UWorld*)
	{
		APlayerController* PC = nullptr;
		UWorld* Game = FindLocalGameWorld(PC);
		if (!Game)
			return;
		const int32 Top = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 40;
		struct FRow { int32 Components = 0; int64 Instances = 0; int64 Tris = 0; bool bNanite = false; float EndCull = 0.0f; int32 Masked = 0; };
		TMap<const UStaticMesh*, FRow> Rows;
		// 두 번째 값이 sm 이면 인스턴스가 아닌 보통 메시(레벨에 놓인 시설 건물·소품)를 본다(9/28: 시설 레벨 안에 자동차 스캔 같은 것이 더 숨어 있나).
		const bool bPlainMeshes = Args.Num() > 1 && Args[1] == TEXT("sm");
		for (TObjectIterator<UStaticMeshComponent> It; It; ++It)
		{
			UStaticMeshComponent* C = *It;
			if (!IsValid(C) || C->GetWorld() != Game || !C->IsVisible() || !C->GetStaticMesh())
				continue;
			const UInstancedStaticMeshComponent* Instanced = Cast<UInstancedStaticMeshComponent>(C);
			if (bPlainMeshes == (Instanced != nullptr))
				continue;
			const int32 Count = Instanced ? Instanced->GetInstanceCount() : 1;
			if (Count == 0)
				continue;
			const UStaticMesh* Mesh = C->GetStaticMesh();
			FRow& Row = Rows.FindOrAdd(Mesh);
			++Row.Components;
			Row.Instances += Count;
			const int32 Lod0 = Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0 ? Mesh->GetRenderData()->LODResources[0].GetNumTriangles() : 0;
			Row.Tris += static_cast<int64>(Lod0) * Count;
			Row.bNanite = Mesh->IsNaniteEnabled();
			Row.EndCull = FMath::Max(Row.EndCull, Instanced ? static_cast<float>(Instanced->InstanceEndCullDistance) : C->LDMaxDrawDistance);
			if (Row.Masked == 0)
				for (int32 Slot = 0; Slot < C->GetNumMaterials(); ++Slot)
					if (const UMaterialInterface* M = C->GetMaterial(Slot); M && M->GetBlendMode() != BLEND_Opaque)
						++Row.Masked;
		}
		Rows.ValueSort([](const FRow& A, const FRow& B) { return A.Tris > B.Tris; });
		int64 AllTris = 0, NaniteTris = 0;
		for (const TPair<const UStaticMesh*, FRow>& Pair : Rows)
		{
			AllTris += Pair.Value.Tris;
			NaniteTris += Pair.Value.bNanite ? Pair.Value.Tris : 0;
		}
		UE_LOG(LogPGObjects, Display, TEXT("ISMReport: %d meshes, %.1fM LOD0 triangles in total (%.1fM on Nanite meshes)"), Rows.Num(), AllTris / 1e6, NaniteTris / 1e6);
		int32 Shown = 0;
		for (const TPair<const UStaticMesh*, FRow>& Pair : Rows)
		{
			if (++Shown > Top)
				break;
			const FRow& R = Pair.Value;
			UE_LOG(LogPGObjects, Display, TEXT("ISMReport: %5.2fM tris  inst=%6lld comps=%3d nanite=%d end_cull=%6.0fm masked_slots=%d  %s"),
				R.Tris / 1e6, R.Instances, R.Components, R.bNanite ? 1 : 0, R.EndCull * 0.01f, R.Masked, *Pair.Key->GetPathName());
		}
	}
	static FAutoConsoleCommandWithWorldAndArgs ISMReportCommand(TEXT("PG.ISMReport"),
		TEXT("Lists instanced meshes (or plain meshes with sm) by instances x LOD0 triangles (Nanite, cull distance, masked slots). Args: [top=40] [sm]"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&ISMReport));

	static FAutoConsoleCommandWithWorldAndArgs AfterReadyCommand(TEXT("PG.AfterReady"),
		TEXT("Client helper: runs a console command once, N seconds after the local pawn appears. Args: <seconds> <command...>"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AfterReady));

	static FAutoConsoleCommandWithWorldAndArgs PerfLogCommand(TEXT("PG.PerfLog"),
		TEXT("Logs fps, frame/game/render/GPU ms and texture memory every second for N seconds (default 300)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&PerfLog));

	static FAutoConsoleCommandWithWorldAndArgs AnnounceDedupeTestCommand(TEXT("PG.AnnounceDedupeTest"),
		TEXT("Client test: announces the same line three times and another once; the queue should hold 2."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&AnnounceDedupeTest));

	static FAutoConsoleCommandWithWorldAndArgs NetSmashTestCommand(TEXT("PG.NetSmashTest"),
		TEXT("Logs frame time per second; if the local player rides a robot, holds W and swings (use with the server PG.SmashCoreTest)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NetSmashTest));

	static FAutoConsoleCommandWithWorldAndArgs NetContainerWatchCommand(TEXT("PG.NetContainerWatch"),
		TEXT("Client test: remembers container positions N seconds after the local pawn appears, then lists the ones that moved M seconds after."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&NetContainerWatch));

	static FAutoConsoleCommandWithWorldAndArgs HitchLogCommand(
		TEXT("PG.HitchLog"),
		TEXT("Logs every frame that takes longer than 80 ms (to find what stalls)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&HitchLog));

	static FAutoConsoleCommandWithWorldAndArgs NetRideTestCommand(
		TEXT("PG.NetRideTest"),
		TEXT("멀티 조작 시험(클라이언트에서): 가까운 탈것까지 걸어가 F 로 타고 W 로 달린 뒤 F 로 내린다. car | tank | fly"),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&Start));
}
