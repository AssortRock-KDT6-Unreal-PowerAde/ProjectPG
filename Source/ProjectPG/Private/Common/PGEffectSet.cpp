#include "Common/PGEffectSet.h"

#include "Common/PGVisualSettings.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"

UPGEffectSet::UPGEffectSet()
{
	// 기본값 = 9/23 블루프린트 분리 전 코드에 적혀 있던 에셋.
	DebrisDustEffect = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld")));
	MissileKitFolder.Path = TEXT("/Game/PG/Finale/Missile/");
	MissileBodyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/MI_ShipBridge_Frame.MI_ShipBridge_Frame")));
	MissileDarkMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/MI_ShipBridge_Panel.MI_ShipBridge_Panel")));
	MissileGlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Bridge/MI_ShipBridge_Glow.MI_ShipBridge_Glow")));
}

const UPGEffectSet* UPGEffectSet::GetActive()
{
	const TSoftObjectPtr<UPGEffectSet>& Designed = UPGVisualSettings::Get().EffectSet;
	if (!Designed.IsNull())
	{
		if (const UPGEffectSet* Loaded = Designed.LoadSynchronous())
			return Loaded;
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: effect set %s not found — using the code defaults"), *Designed.ToString());
	}
	return GetDefault<UPGEffectSet>();
}
