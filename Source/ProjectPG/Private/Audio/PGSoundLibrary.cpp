// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#include "Audio/PGSoundLibrary.h"
#include "Audio/PGAudioSubsystem.h"

void UPGSoundLibrary::PlayPGSound(const UObject* WorldContext, FName CueName, FVector Location, float VolumeScale)
{
	// 서브시스템이 없으면(데디 서버·에디터 미리보기) 조용히 끝남 — 서버엔 원래 소리가 없어서 정상.
	if (UPGAudioSubsystem* Audio = UPGAudioSubsystem::Get(WorldContext))
		Audio->PlayCueByName(CueName, Location, VolumeScale);
}
