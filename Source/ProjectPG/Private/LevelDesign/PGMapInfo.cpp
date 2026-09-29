#include "LevelDesign/PGMapInfo.h"
#include "Engine/World.h"

IPGMapInfo* UPGMapInfoSubsystem::FindMap(const UObject* WorldContext)
{
	const UWorld* World = IsValid(WorldContext) ? WorldContext->GetWorld() : nullptr;
	const UPGMapInfoSubsystem* Subsystem = IsValid(World) ? World->GetSubsystem<UPGMapInfoSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetMap() : nullptr;
}

void UPGMapInfoSubsystem::RegisterMap(UObject* InMapObject)
{
	// 한 판에 맵은 하나다. 두 번째가 들어오면 로그로 알린다(레벨에 맵 액터를 손으로 또 놓은 경우 등).
	if (MapObject.IsValid() && MapObject.Get() != InMapObject)
		UE_LOG(LogTemp, Warning, TEXT("PGMapInfo: second map %s replaces %s"), *GetNameSafe(InMapObject), *GetNameSafe(MapObject.Get()));
	MapObject = (IsValid(InMapObject) && InMapObject->Implements<UPGMapInfo>()) ? InMapObject : nullptr;
}

void UPGMapInfoSubsystem::UnregisterMap(UObject* InMapObject)
{
	if (MapObject.Get() == InMapObject)
		MapObject.Reset();
}

IPGMapInfo* UPGMapInfoSubsystem::GetMap() const
{
	return MapObject.IsValid() ? Cast<IPGMapInfo>(MapObject.Get()) : nullptr;
}
