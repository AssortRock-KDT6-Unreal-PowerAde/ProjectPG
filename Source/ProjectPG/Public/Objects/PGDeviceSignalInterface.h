// 작동 장치(스위치·레버·제어 패널) → 연결 대상(문·경보·발전기) 신호 경계.
// 장치는 "켜짐/꺼짐"만 보내고, 그 신호로 무엇을 할지는 받는 쪽이 정한다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PGDeviceSignalInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UPGDeviceSignalTarget : public UInterface
{
	GENERATED_BODY()
};

class PROJECTPG_API IPGDeviceSignalTarget
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintNativeEvent, Category = "PG|Device")
	void OnDeviceSignal(bool bOn, AActor* Source);
};
