#include "Objects/PGSpawnSocketComponent.h"

UPGSpawnSocketComponent::UPGSpawnSocketComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// 마커는 서버에서만 의미가 있다. 클라이언트는 결과 오브젝트만 복제로 받는다.
	SetIsReplicatedByDefault(false);
}

void UPGSpawnSocketComponent::Configure(EPGSpawnSocketKind InKind, uint8 InTier, float InSpawnChance)
{
	Kind = InKind;
	Tier = FMath::Clamp<uint8>(InTier, 1, 3);
	SpawnChance = FMath::Clamp(InSpawnChance, 0.0f, 1.0f);
}

int64 UPGSpawnSocketComponent::MakeSeed(int64 WorldSeed) const
{
	// 위치를 10cm 단위로 양자화해서 해시한다. 부동소수 오차로 시드가 흔들리지 않게.
	const FVector Location = GetComponentLocation();
	const FIntVector Quantized(
		FMath::RoundToInt(Location.X / 10.0),
		FMath::RoundToInt(Location.Y / 10.0),
		FMath::RoundToInt(Location.Z / 10.0));
	uint32 Hash = GetTypeHash(Quantized);
	Hash = HashCombine(Hash, static_cast<uint32>(Kind));
	Hash = HashCombine(Hash, static_cast<uint32>(WorldSeed & 0xFFFFFFFF));
	Hash = HashCombine(Hash, static_cast<uint32>((WorldSeed >> 32) & 0xFFFFFFFF));
	return static_cast<int64>(Hash);
}

APGSpawnSocketActor::APGSpawnSocketActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	Socket = CreateDefaultSubobject<UPGSpawnSocketComponent>(TEXT("Socket"));
	SetRootComponent(Socket);
}
