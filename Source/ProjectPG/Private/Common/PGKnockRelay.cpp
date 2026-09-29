#include "Common/PGKnockRelay.h"

#include "Common/PGPhysicsUtil.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"

namespace
{
	// 방송 하나에 담는 수와 한 프레임의 방송 수. 소품 하나가 약 50바이트라 40개면 2KB 남짓 — 패킷 몇 개로 나뉘어도 문제없다.
	constexpr int32 KnockRelayEntriesPerBatch = 40;
	constexpr int32 KnockRelayBatchesPerFrame = 2;
	// 클라이언트가 한 프레임에 따라 하는 수. 잔해 만들기 예산(PG.Knock.PerFrame, 기본 4)과 맞춘다 — 더 하면 잔해 없이 원본만 사라진다.
	TAutoConsoleVariable<int32> CVarKnockRemotePerFrame(TEXT("PG.Knock.RemotePerFrame"), 4,
		TEXT("Client: how many server knocks are replayed per frame (the rest wait for the next frames)."));
}

APGKnockRelay::APGKnockRelay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	bReplicates = true;
	bAlwaysRelevant = true; // 맵 어디서 부서지든 모두 받아야 한다
	SetNetUpdateFrequency(10.0f); // 복제할 값은 없다. 방송만 한다(방송은 믿을 수 있는 쪽이라 복제 주기와 상관없이 바로 나간다)
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void APGKnockRelay::Send(UWorld* World, UStaticMesh* Mesh, const FTransform& Where, const FVector& Impulse, float MassKg, bool bFallOnly)
{
	if (!IsValid(World) || !IsValid(Mesh))
		return;
	const ENetMode Mode = World->GetNetMode();
	if (Mode != NM_DedicatedServer && Mode != NM_ListenServer)
		return;
	APGKnockRelay* Relay = nullptr;
	for (TActorIterator<APGKnockRelay> It(World); It; ++It)
	{
		Relay = *It;
		break;
	}
	if (!IsValid(Relay))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Relay = World->SpawnActor<APGKnockRelay>(APGKnockRelay::StaticClass(), FTransform::Identity, Params);
	}
	if (!IsValid(Relay))
		return;
	FPGKnockEntry& Entry = Relay->Pending.AddDefaulted_GetRef();
	Entry.Mesh = Mesh;
	Entry.Location = Where.GetLocation();
	Entry.Rotation = Where.Rotator();
	Entry.Scale = Where.GetScale3D();
	Entry.Impulse = Impulse;
	Entry.MassKg = MassKg;
	Entry.bFallOnly = bFallOnly;
	static int32 Sent = 0; // 시험 확인용: 서버가 알린 수(클라 "received" 수와 비교)
	if (++Sent <= 5 || Sent % 20 == 0)
		UE_LOG(LogTemp, Display, TEXT("KnockRelay: %d knock(s) sent to clients (last %s at %s)"), Sent, *Mesh->GetName(), *Where.GetLocation().ToCompactString());
}

void APGKnockRelay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Pending.IsEmpty())
		return;
	if (HasAuthority())
	{
		// 서버: 이번 프레임에 모인 것을 묶어 보낸다. 넘치면(공장이 한꺼번에 무너질 때) 다음 프레임으로.
		for (int32 Batch = 0; Batch < KnockRelayBatchesPerFrame && !Pending.IsEmpty(); ++Batch)
		{
			const int32 Count = FMath::Min(Pending.Num(), KnockRelayEntriesPerBatch);
			MulticastKnockBatch(TArray<FPGKnockEntry>(Pending.GetData(), Count));
			Pending.RemoveAt(0, Count, EAllowShrinking::No);
		}
		return;
	}
	// 클라이언트: 받은 차례대로 몇 개씩. 0 은 측정용(따라 하지 않음 — 부수기 없이 같은 길을 걸을 때의 비용 비교).
	const int32 Count = FMath::Min(Pending.Num(), FMath::Max(0, CVarKnockRemotePerFrame.GetValueOnGameThread()));
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FPGKnockEntry& Entry = Pending[Index];
		if (IsValid(Entry.Mesh))
			PGPhysicsUtil::ApplyRemoteKnock(GetWorld(), Entry.Mesh, FTransform(Entry.Rotation, Entry.Location, Entry.Scale), Entry.Impulse, Entry.MassKg, Entry.bFallOnly);
	}
	Pending.RemoveAt(0, Count, EAllowShrinking::No);
}

void APGKnockRelay::MulticastKnockBatch_Implementation(const TArray<FPGKnockEntry>& Entries)
{
	// 서버(듣기 서버 방장 포함)는 이미 자기 손으로 날렸다. 클라이언트만 줄에 넣고 Tick 에서 따라 한다.
	if (GetNetMode() != NM_Client)
		return;
	Pending.Append(Entries);
}
