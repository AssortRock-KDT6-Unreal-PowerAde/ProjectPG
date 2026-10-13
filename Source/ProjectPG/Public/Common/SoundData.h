#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "SoundData.generated.h"

class UAkAudioEvent;

// 소리 표의 한 줄. 줄 이름(예: UI_Click)이 코드에서 부르는 소리 이름이다.
USTRUCT(BlueprintType)
struct FSoundTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere)
	TObjectPtr<UAkAudioEvent> Event;
};