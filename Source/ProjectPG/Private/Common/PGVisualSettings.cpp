#include "Common/PGVisualSettings.h"

#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Robot/PGRobotCharacter.h"
#include "Vehicle/PGTankPawn.h"
#include "Vehicle/PGVehiclePawn.h"

UPGVisualSettings::UPGVisualSettings()
{
	// 설정 파일에 값이 없을 때의 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 연료통 세 색. Tools/make_fuel_can.py).
	for (const TCHAR* Path : { TEXT("/Game/PG/Props/FuelCan/SM_PGFuelCan_Red.SM_PGFuelCan_Red"),
		TEXT("/Game/PG/Props/FuelCan/SM_PGFuelCan_Green.SM_PGFuelCan_Green"), TEXT("/Game/PG/Props/FuelCan/SM_PGFuelCan_Yellow.SM_PGFuelCan_Yellow") })
		FuelCanMeshes.Add(TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path)));
}

UClass* UPGVisualSettings::ResolveActorClass(const TSoftClassPtr<AActor>& Designed, UClass* Fallback)
{
	if (Designed.IsNull())
		return Fallback;
	UClass* Loaded = Designed.LoadSynchronous();
	// 부모가 다른 블루프린트를 잘못 넣으면 그 액터의 함수·칸이 없다 — 차라리 C++ 원래 모습을 쓴다.
	if (!Loaded || (Fallback && !Loaded->IsChildOf(Fallback)))
	{
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: %s not usable (missing or not a %s) — using the C++ class"),
			*Designed.ToString(), *GetNameSafe(Fallback));
		return Fallback;
	}
	return Loaded;
}

UClass* UPGVisualSettings::TankSpawnClass() { return ResolveActorClass(Get().TankClass, APGTankPawn::StaticClass()); }
UClass* UPGVisualSettings::VehicleSpawnClass() { return ResolveActorClass(Get().VehicleClass, APGVehiclePawn::StaticClass()); }
UClass* UPGVisualSettings::RobotSpawnClass() { return ResolveActorClass(Get().RobotClass, APGRobotCharacter::StaticClass()); }
