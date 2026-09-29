#include "LevelDesign/PGMapVisualSet.h"

#include "Common/PGVisualSettings.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"

namespace PGMapVisualSetLocal
{
	template <typename T>
	TSoftObjectPtr<T> MapSlot(const TCHAR* Path)
	{
		return TSoftObjectPtr<T>(FSoftObjectPath(Path));
	}
}

UPGMapVisualSet::UPGMapVisualSet()
{
	using namespace PGMapVisualSetLocal;
	// 기본값 = 9/23 블루프린트 분리 전 WarZoneFootprintPreview.cpp / TacticalTileActor.cpp 에 적혀 있던 에셋.
	CollapseDust = MapSlot<UParticleSystem>(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld"));
	RockEdgeMaterial = MapSlot<UMaterialInterface>(TEXT("/Game/Downtown_West/Assets/background_mountain/MI_Mountains.MI_Mountains"));
	DressingGrass = {
		MapSlot<UStaticMesh>(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarA.SM_vlkhcbxia_VarA")),
		MapSlot<UStaticMesh>(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarC.SM_vlkhcbxia_VarC")),
		MapSlot<UStaticMesh>(TEXT("/Game/Fab/Megascans/Plants/Wild_Grass_vlkhcbxia/Medium/vlkhcbxia_tier_2/StaticMeshes/SM_vlkhcbxia_VarF.SM_vlkhcbxia_VarF")),
	};
	DressingShrubs = {
		MapSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Foliage/Shrubs_1.Shrubs_1")),
		MapSlot<UStaticMesh>(TEXT("/Game/GV_FreeShrubsPack/Meshes/Shrubs/Wind/Shrub_A/GV_Vol7_Shrub_A_type1_L2.GV_Vol7_Shrub_A_type1_L2")),
	};
	LakeBoat = MapSlot<UStaticMesh>(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Wooden_Boat.Wooden_Boat"));
	RampMaterial = MapSlot<UMaterialInterface>(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeRoad_AsphaltClean.MI_RuntimeRoad_AsphaltClean"));
	UnifiedGroundMaterial = MapSlot<UMaterialInterface>(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeGround_NatureUnified.MI_RuntimeGround_NatureUnified"));
	HeroShrubLeafMaterial = MapSlot<UMaterialInterface>(TEXT("/Game/PG/LevelDesign/Materials/MI_RuntimeHeroShrub_Dark.MI_RuntimeHeroShrub_Dark"));
	TileGrassA = MapSlot<UStaticMesh>(TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_2_Runtime.SM_GrassPatch_2_Runtime"));
	TileGrassB = MapSlot<UStaticMesh>(TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_1_Runtime.SM_GrassPatch_1_Runtime"));
	TileGrassLong = MapSlot<UStaticMesh>(TEXT("/Game/PG/LevelDesign/RuntimeOptimized/SM_GrassPatch_Long_Runtime.SM_GrassPatch_Long_Runtime"));
	TileRockLarge = MapSlot<UStaticMesh>(TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_large_a_low.SM_rock_large_a_low"));
	TileRockMedium = MapSlot<UStaticMesh>(TEXT("/Game/Downtown_West/Assets/props/prop_rocks/SM_rock_medium_a_low.SM_rock_medium_a_low"));
}

const UPGMapVisualSet* UPGMapVisualSet::GetActive()
{
	const TSoftObjectPtr<UPGMapVisualSet>& Designed = UPGVisualSettings::Get().MapVisualSet;
	if (!Designed.IsNull())
	{
		if (const UPGMapVisualSet* Loaded = Designed.LoadSynchronous())
			return Loaded;
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: map visual set %s not found — using the code defaults"), *Designed.ToString());
	}
	return GetDefault<UPGMapVisualSet>();
}
