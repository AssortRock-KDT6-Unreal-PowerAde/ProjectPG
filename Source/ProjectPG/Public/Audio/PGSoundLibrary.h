// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "PGSoundLibrary.generated.h"

// BP 에서 소리 표(DA_PGSounds)의 이름 소리를 트는 노드. 문·상자·폭발·피날레처럼 BP 로 만든 사건에 쓴다.
// 데디 서버에서 불리면 아무것도 안 함 → 소리를 듣게 하려면 모든 클라에서 도는 이벤트(멀티캐스트·OnRep)에서 부를 것.
UCLASS()
class PROJECTPG_API UPGSoundLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="PG|Audio", meta=(WorldContext="WorldContext"))
	static void PlayPGSound(const UObject* WorldContext, FName CueName, FVector Location, float VolumeScale = 1.f);
};
