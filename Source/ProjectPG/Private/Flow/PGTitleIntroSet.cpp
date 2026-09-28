#include "Flow/PGTitleIntroSet.h"

#include "Animation/AnimSequence.h"
#include "Common/PGVisualSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Particles/ParticleSystem.h"

namespace PGTitleIntroSetLocal
{
	template <typename T>
	TSoftObjectPtr<T> IntroSlot(const TCHAR* Path)
	{
		return TSoftObjectPtr<T>(FSoftObjectPath(Path));
	}
}

UPGTitleIntroSet::UPGTitleIntroSet()
{
	using namespace PGTitleIntroSetLocal;
	// 기본값 = 9/23 블루프린트 분리 전 PGTitleIntro*.cpp 에 적혀 있던 에셋.
	BeamMesh = IntroSlot<UStaticMesh>(TEXT("/Game/PG/Characters/Quantum/Wig/SM_WigBeam.SM_WigBeam"));
	DustEffect = IntroSlot<UParticleSystem>(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld"));
	// 총알 전용 효과가 프로젝트에 없어서, 보스 팩의 "캐릭터가 맞았을 때" 효과를 작게 줄여 쓴다.
	HitEffect = IntroSlot<UParticleSystem>(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitCharacter.P_Rampage_Rock_HitCharacter"));

	HeroIdle = IntroSlot<UAnimSequence>(TEXT("/Game/QuantumCharacter/Demo/Animations/A_MM_Idle.A_MM_Idle"));
	HeroRun = IntroSlot<UAnimSequence>(TEXT("/Game/QuantumCharacter/Demo/Animations/A_MM_Run_Fwd.A_MM_Run_Fwd"));
	// 소총 동작을 트는 마네킹(팀 캐릭터 애님 블루프린트가 쓰는 것과 같은 마네킹 뼈대).
	RifleMannequin = IntroSlot<USkeletalMesh>(TEXT("/Game/ControlRig/Characters/Mannequins/Meshes/SKM_Manny.SKM_Manny"));
	RifleJog = IntroSlot<UAnimSequence>(TEXT("/Game/PG/Animations/Sequences/AS_Jog_Fwd_Rifle.AS_Jog_Fwd_Rifle"));
	RifleIdle = IntroSlot<UAnimSequence>(TEXT("/Game/PG/Animations/Sequences/AS_Idle_Rifle_Hip.AS_Idle_Rifle_Hip"));
	RifleFire = IntroSlot<UAnimSequence>(TEXT("/Game/PG/Animations/Sequences/AS_Fire_Rifle_Hip.AS_Fire_Rifle_Hip"));
	RifleReload = IntroSlot<UAnimSequence>(TEXT("/Game/PG/Animations/Sequences/AS_Reload_Rifle_Hip.AS_Reload_Rifle_Hip"));
	// 총: 게임의 소총 AK(UPGWeaponComponent 의 Rifle_AK)와 같은 메시. 들기 값(각도·배율·손 위치)은 코드에 있다.
	RifleMesh = IntroSlot<UStaticMesh>(TEXT("/Game/AK-47/Mesh/SM_AK-47.SM_AK-47"));
	MuzzleFlash = IntroSlot<UParticleSystem>(TEXT("/Game/AK-47/FX/MuzzleFlash/P_AssaultRifle_MuzzleFlash.P_AssaultRifle_MuzzleFlash"));
	// 게임 스포츠카(APGVehiclePawn 의 SportsCar 프리셋) — 변신 여고생이 되는 그 차.
	CarMesh = IntroSlot<USkeletalMesh>(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_SportsCar.SK_SportsCar"));

	// 게임의 전함(APGBattleshipActor)과 같은 팩 화물선. 배율 5 는 코드에 있다.
	ShipClass = TSoftClassPtr<AActor>(FSoftObjectPath(TEXT("/Game/kb3d_missiontominerva/Blueprints/Vehicles/BP_KB3D_MTM_VehicleCargoShip_A.BP_KB3D_MTM_VehicleCargoShip_A_C")));
	// 게임의 드래곤 보스(APGDragonBoss)와 같은 메시와 날갯짓 동작.
	DragonMesh = IntroSlot<USkeletalMesh>(TEXT("/Game/FourEvilDragonsPBR/Meshes/DragonTheTerrorBringer/DragonTheTerrorBringerSK.DragonTheTerrorBringerSK"));
	DragonFly = IntroSlot<UAnimSequence>(TEXT("/Game/FourEvilDragonsPBR/Animations/DragonTheTerrorBringer/FlyForwardAnim.FlyForwardAnim"));
}

const UPGTitleIntroSet* UPGTitleIntroSet::GetActive()
{
	const TSoftObjectPtr<UPGTitleIntroSet>& Designed = UPGVisualSettings::Get().TitleIntroSet;
	if (!Designed.IsNull())
	{
		if (const UPGTitleIntroSet* Loaded = Designed.LoadSynchronous())
			return Loaded;
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: title intro set %s not found — using the code defaults"), *Designed.ToString());
	}
	return GetDefault<UPGTitleIntroSet>();
}
