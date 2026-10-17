// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#include "Audio/PGSoundSet.h"

const FPGSoundCue* UPGSoundSet::FindBySurface(const TMap<TEnumAsByte<EPhysicalSurface>, FPGSoundCue>& Table, EPhysicalSurface Surface)
{
	if (const FPGSoundCue* Found = Table.Find(Surface)) return Found;
	return Table.Find(SurfaceType_Default);
}
