// APGDragonBoss 를 나눈 .cpp 들이 함께 쓰는 인클루드·상수·도우미(2026-09-26 책임별 나누기). 이 폴더 밖에서는 인클루드하지 않는다.
// 원래 한 파일 맨 위에 있던 것이라, 파일을 나눈 뒤에도 값이 두 벌이 되지 않게 한곳에 둔다.
#pragma once

#include "Finale/PGDragonBoss.h"
#include "LevelDesign/PGMapInfo.h"
#include "Animation/AnimSequence.h"
#include "Misc/PackageName.h"
#include "Flow/PGRunSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/SkeletalMesh.h"
#include "Common/PGPhysicsUtil.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Finale/PGBattleshipActor.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Monster/PGMonsterCharacter.h"
#include "Objects/PGObjectTypes.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "TimerManager.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "GameFramework/PlayerState.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGWearableColors.h"

namespace
{
	// 이 팩(FourEvilDragonsPBR)에는 드래곤이 넷 있고 그중 셋이 난다(Nightmare 는 나는 동작이 없다).
	// TerrorBringer 를 쓰는 이유: 나는 동작이 6개로 가장 많고, 그중 활공(FlyGlideAnim)이 있다.
	//   배 주위를 도는 내내 날갯짓만 하면 기계처럼 보이는데, 활공을 섞으면 훨씬 자연스럽다(사용자 선택 9/20).
	// 바꾸려면 이 두 줄만 고치면 된다 — 상태 기계는 그대로 돈다.
	//   SoulEater:     .../Meshes/DrangonTheSoulEater/DragonTheSoulEaterSK   (폴더 이름의 Drangon 은 팩의 오타다)
	//   TerrorBringer: .../Meshes/DragonTheTerrorBringer/DragonTheTerrorBringerSK
	//   Usurper:       .../Meshes/DragonTheUsurper/DragonTheUsurperSK        (FlyAttackAnim 이 없고 FlyFlameAnim 이다)
	const TCHAR* const DragonMeshPath = TEXT("/Game/FourEvilDragonsPBR/Meshes/DragonTheTerrorBringer/DragonTheTerrorBringerSK.DragonTheTerrorBringerSK");
	const TCHAR* const DragonAnimDir = TEXT("/Game/FourEvilDragonsPBR/Animations/DragonTheTerrorBringer/");

	// 브레스 불꽃과 불이 닿은 자리의 먼지. 둘 다 디스크에서 있는 것을 확인한 경로다(9/21).
	//   불꽃: Weapon_Pack 의 횃불 불꽃(Cascade). 드래곤 팩에는 이펙트가 없다.
	//   먼지: 부서지는 건물이 이미 쓰는 것(PGDebrisSubsystem 의 DustEffect)과 같은 것 — 한 번 터지고 사라진다.
	// 팩이 없는 PC 에서는 이펙트 없이 피해만 들어간다(BeginPlay 로그에 fire=no 로 뜬다).
	const TCHAR* const FireFxPath = TEXT("/Game/Weapon_Pack/Effects/Particles/Fire/P_TorchFire.P_TorchFire");
	const TCHAR* const ImpactFxPath = TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld");

	// 입이 어디 있나. TerrorBringer 뼈 목록에서 확인한 이름(Jaw1 = 아래턱 뿌리, Head = 머리).
	const TCHAR* const MouthBoneCandidates[] = { TEXT("Jaw1"), TEXT("Head"), TEXT("UpperHead1") };

	// 땅에 처박힐 때의 폭발 구. 미사일 폭발과 같은 것(Content/PG 에 있어 어느 PC 에서나 뜬다). 반지름 1m 구.
	const TCHAR* const CrashBlastMeshPath = TEXT("/Game/PG/Finale/Missile/SM_MissileBlast.SM_MissileBlast");
	const TCHAR* const CrashBlastMaterialPath = TEXT("/Game/PG/Finale/Missile/MI_MissileBlast.MI_MissileBlast");
	constexpr float CrashBlastMeshRadiusCm = 100.0f;
	constexpr float CrashBlastSeconds = 0.7f;

	// 메시(발바닥 = 피벗)가 액터 원점보다 이만큼 아래에 붙어 있다. FeetOffsetCm 주석 참고.
	constexpr float MeshBelowOriginCm = 900.0f;
	// 이 프로젝트의 땅 높이 기준(타일 윗면 z=20). 땅 찾기가 실패한 자리(맵 밖)는 이 높이로 친다 —
	//   산과 둘레 바닥판은 충돌이 없어 선 검사가 그냥 뚫고 지나간다(9/21 조사).
	constexpr float GroundDatumZ = 20.0f;
	// 맵 밖(땅을 못 찾은 자리)에서는 발을 이 높이 아래로 안 내린다. 산 능선이 대략 이 아래에 있어서,
	//   맵 안으로 들어올 때까지 이 높이로 미끄러진 뒤에 떨어진다 — 산 밑으로 꺼지는 것보다 낫다.
	constexpr float OutsideMapHoldCm = 8000.0f;
	// 타일 범위를 못 읽었을 때의 맵 범위(월드 XY, cm). 30×30 칸 × 2000cm 를 원점 근처에 깐 값.
	const FBox2D FallbackMapBox(FVector2D(-31000.0f, -31000.0f), FVector2D(29000.0f, 29000.0f));

	// 브레스 불꽃을 몇 조각으로 늘어놓나. 조각마다 이펙트 하나라, 많을수록 비싸다.
	constexpr int32 BreathFxPieces = 5;
	// 불길이 입에서 나올 때의 굵기(cm). 끝으로 갈수록 BreathHalfAngleDeg 만큼 넓어진다.
	constexpr float BreathStartRadiusCm = 1500.0f;

	// 애니 이름은 드래곤마다 조금씩 다르다(착지가 LandAnim 인 놈도 있고 LandingAnim 인 놈도 있다).
	// 찾는 이름이 없으면 같은 뜻의 다른 이름을 차례로 시도한다 — 모델을 바꿔도 조용히 동작 하나가
	// 빠지는 일이 없다. 그래도 없으면 경고를 남긴다(조용히 넘어가면 원인을 못 찾는다).
	//
	// **대체 이름은 반드시 같은 상황(공중/지상)의 것이어야 한다.** 이름만 보고 고르면 안 된다 —
	// TerrorBringer 의 AttackFlameAnim 은 이름 때문에 "공중 브레스" 로 오해했지만 **발을 딛고 뿜는
	// 지상 동작**이라, 공중에서 틀었더니 허공에 발을 짚었다(9/20 사용자: "공중에 날아다니는데
	// 바닥 짚는 공격 모션이 나온다"). 이 드래곤의 공중 동작은 여섯 개뿐이다:
	//   TakeOffAnim / FlyIdleAnim / FlyForwardAnim / FlyGlideAnim / FlyAttackAnim / LandingAnim
	// 나머지(AttackFlame, AttackMouth, AttackWingClaw, Defend, Idle01/02, Run, Walk, Sleep,
	//   Scream, GetHit, Die)는 전부 지상용이다. **공중 공격은 FlyAttackAnim 하나뿐**이고,
	//   공중 브레스 동작은 이 드래곤에 없다(다른 드래곤 것은 뼈 개수가 달라 못 가져온다).
	//   그래서 공중 브레스는 "FlyAttackAnim + 입에서 나오는 불꽃 이펙트" 로 만든다.
	// 지상 공격 동작(AttackFlame/WingClaw/Mouth)은 이제 **땅에 내려앉았을 때만** 쓴다(TickLanded).
	// Dir: 애니메이션 폴더("/Game/.../"). 드래곤 칸 DragonAnimFolder 에서 온다.
	inline UAnimSequence* LoadDragonAnim(const FString& Dir, const TCHAR* Name)
	{
		// 9/28: 이미 메모리에 있으면(판 시작에 미리 읽음 — APGDragonBoss::PreloadAssets) 그대로 쓰고, 파일이 없는 이름은 디스크 목록만 보고 넘어간다.
		//   전에는 LoadObject 로 곧장 읽어서, 없는 이름(FlyAttackAnim 등 — 다른 이름으로 대신한다)도 매번 "읽기가 다 끝날 때까지 기다리기"
		//   (FlushAsyncLoading)를 불러 드래곤 등장·첫 공격 때 게임이 잠깐 멈췄다(9/28 화면 그리는 시험: 90ms 프레임 3번).
		auto TryLoad = [&Dir](const TCHAR* Which) -> UAnimSequence*
		{
			const FString Package = Dir + Which;
			const FString Path = FString::Printf(TEXT("%s.%s"), *Package, Which);
			if (UAnimSequence* Loaded = FindObject<UAnimSequence>(nullptr, *Path))
				return Loaded;
			static TSet<FString> Missing;
			if (Missing.Contains(Package))
				return nullptr;
			if (!FPackageName::DoesPackageExist(Package))
			{
				Missing.Add(Package);
				return nullptr;
			}
			return LoadObject<UAnimSequence>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
		};
		if (UAnimSequence* Found = TryLoad(Name))
			return Found;
		static const TMap<FString, TArray<FString>> Aliases = {
			// 공중 ↔ 공중
			{ TEXT("LandAnim"),      { TEXT("LandingAnim") } },
			{ TEXT("FlyAttackAnim"), { TEXT("FlyFlameAnim"), TEXT("FlyForwardAnim") } },
			{ TEXT("FlyGlideAnim"),  { TEXT("FlyForwardAnim") } },   // SoulEater 는 활공이 없다
			{ TEXT("TakeOffAnim"),   { TEXT("TakeoffAnim"), TEXT("FlyForwardAnim") } },
			// 지상 ↔ 지상
			{ TEXT("ScreamAnim"),         { TEXT("RoarAnim"), TEXT("Idle01Anim") } },
			{ TEXT("GetHitAnim"),         { TEXT("HitAnim"), TEXT("ScreamAnim"), TEXT("DefendAnim") } },
			{ TEXT("DieAnim"),            { TEXT("DeathAnim"), TEXT("Die") } },
			{ TEXT("AttackFlameAnim"),    { TEXT("AttackFireBallAnim"), TEXT("AttackMouthAnim") } },
			{ TEXT("AttackWingClawAnim"), { TEXT("AttackTailAnim"), TEXT("AttackHandAnim"), TEXT("AttackMouthAnim") } },
		};
		if (const TArray<FString>* List = Aliases.Find(Name))
			for (const FString& Alternative : *List)
				if (UAnimSequence* Found = TryLoad(*Alternative))
					return Found;
		UE_LOG(LogPGObjects, Warning, TEXT("PGDragon: no animation called %s in %s"), Name, *Dir);
		return nullptr;
	}
}

