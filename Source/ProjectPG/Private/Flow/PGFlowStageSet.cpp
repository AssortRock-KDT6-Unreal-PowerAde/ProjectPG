#include "Flow/PGFlowStageSet.h"

#include "Animation/AnimSequence.h"
#include "Common/PGVisualSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"

namespace PGFlowStageSetLocal
{
	template <typename T>
	TSoftObjectPtr<T> StageSlot(const TCHAR* Path)
	{
		return TSoftObjectPtr<T>(FSoftObjectPath(Path));
	}
}

UPGFlowStageSet::UPGFlowStageSet()
{
	using namespace PGFlowStageSetLocal;
	// 기본값 = 9/23 블루프린트 분리 전 PGFlowStage.cpp 에 적혀 있던 에셋(전부 2026-09-22 디스크에서 있는 것 확인).
	// 캐릭터: 사용자 지정 Quantum 통짜 메시 + 같은 팩의 대기 동작(같은 뼈대 SK_Military_Character_Skeleton).
	HeroMesh = StageSlot<USkeletalMesh>(TEXT("/Game/QuantumCharacter/Mesh/SKM_QuantumCharacter.SKM_QuantumCharacter"));
	HeroIdle = StageSlot<UAnimSequence>(TEXT("/Game/QuantumCharacter/Demo/Animations/A_MM_Idle.A_MM_Idle"));
	// 바닥: 엔진 평면 + 게임 맵 자연 바닥과 같은 머티리얼(게임 안 풀밭과 색이 맞는다).
	GroundMesh = StageSlot<UStaticMesh>(TEXT("/Engine/BasicShapes/Plane.Plane"));
	GroundMaterial = StageSlot<UMaterialInterface>(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
	// 풀 덤불: 게임 맵 타일이 쓰는 가벼운(런타임 최적화) 풀 조각.
	GrassPatches = {
		StageSlot<UStaticMesh>(TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_1_Runtime.SM_GrassPatch_1_Runtime")),
		StageSlot<UStaticMesh>(TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_2_Runtime.SM_GrassPatch_2_Runtime")),
		StageSlot<UStaticMesh>(TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_Long_Runtime.SM_GrassPatch_Long_Runtime")),
	};
	PineTrees = {
		StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/SM_Pine_Tree_01.SM_Pine_Tree_01")),
		StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/SM_Pine_Tree_02.SM_Pine_Tree_02")),
		StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/SM_Pine_Tree_03.SM_Pine_Tree_03")),
		StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/SM_Pine_Tree_04.SM_Pine_Tree_04")),
		StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/SM_Pine_Tree_05.SM_Pine_Tree_05")),
	};
	Bush = StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Bush_1.Bush_1"));
	BushTree = StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Bush_Tree.Bush_Tree"));
	Rock = StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Rock_1.Rock_1"));
	Stones = StageSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/River_Stone_Set_1.River_Stone_Set_1"));
	// 로비의 세워 둔 차: 게임 안 험비와 같은 메시. 타이틀의 불타는 잔해: 녹슨 폐차 스캔.
	ParkedCar = StageSlot<UStaticMesh>(TEXT("/Game/PG/Vehicles/Humvee/SM_PGV_Humvee.SM_PGV_Humvee"));
	BurningWreck = StageSlot<UStaticMesh>(TEXT("/Game/Fab/Vintage_Abandoned_Car_-_Dutch_License_Plate/scan/StaticMeshes/scan.scan"));
	// 불: 전함 잔해·드래곤 브레스가 이미 쓰는 캐스케이드 불꽃. 불티는 Paragon 팩. 연기·큰 불은 나이아가라.
	FireEffect = StageSlot<UParticleSystem>(TEXT("/Game/Weapon_Pack/Effects/Particles/Fire/P_TorchFire.P_TorchFire"));
	EmbersEffect = StageSlot<UParticleSystem>(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/JungleKing/FX/P_Rampage_E_embers.P_Rampage_E_embers"));
	SmokeEffect = StageSlot<UObject>(TEXT("/Game/Fishermans_Cabin/VFX/VFX_Niagara/NS_Smoke.NS_Smoke"));
	BigFireEffect = StageSlot<UObject>(TEXT("/Game/Fishermans_Cabin/VFX/VFX_Niagara/NS_Fireplace.NS_Fireplace"));
}

const UPGFlowStageSet* UPGFlowStageSet::GetActive()
{
	const TSoftObjectPtr<UPGFlowStageSet>& Designed = UPGVisualSettings::Get().FlowStageSet;
	if (!Designed.IsNull())
	{
		if (const UPGFlowStageSet* Loaded = Designed.LoadSynchronous())
			return Loaded;
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: flow stage set %s not found — using the code defaults"), *Designed.ToString());
	}
	return GetDefault<UPGFlowStageSet>();
}
