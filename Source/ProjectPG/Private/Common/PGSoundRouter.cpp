#include "Common/PGSoundRouter.h"

#include "Common/PGVisualSettings.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Objects/PGObjectTypes.h"

APGSoundRouter::APGSoundRouter()
{
	PrimaryActorTick.bCanEverTick = false;
	// 서버가 하나 만들어 모든 사람에게 복제한다. 거리와 상관없이 늘 보이게(소리 신호를 놓치지 않게).
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
}

void APGSoundRouter::MulticastCue_Implementation(FName Cue, AActor* Source, FVector_NetQuantize Location)
{
	PlayHere(Cue, Source, Location);
}

void APGSoundRouter::PlayHere(FName Cue, AActor* Source, const FVector& Location)
{
	// 어떤 신호가 언제 지나가는지 보고 싶으면: Log LogPGObjects Verbose
	UE_LOG(LogPGObjects, Verbose, TEXT("PGSound: %s at %s (source %s)"), *Cue.ToString(), *Location.ToCompactString(), *GetNameSafe(Source));
	PlayCue(Cue, Source, Location);
}

namespace PGSound
{
	// 월드마다 담당 하나. 매번 찾지 않게 기억해 둔다(잔해 먼지처럼 초당 여러 번 부르는 곳이 있다).
	static TWeakObjectPtr<UWorld> GCachedWorld;
	static TWeakObjectPtr<APGSoundRouter> GCachedRouter;

	static APGSoundRouter* FindOrSpawnRouter(const UObject* WorldContext)
	{
		UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		if (!World || !World->IsGameWorld())
			return nullptr; // 에디터 미리보기·스모크 검사 준비 단계에서는 소리 없음
		if (GCachedWorld.Get() == World && GCachedRouter.IsValid())
			return GCachedRouter.Get();
		APGSoundRouter* Router = nullptr;
		for (TActorIterator<APGSoundRouter> It(World); It; ++It)
		{
			Router = *It;
			break;
		}
		// 없으면 서버(혼자 할 때 포함)가 만든다. 클라이언트는 서버 것이 복제돼 올 때까지 기다린다(그동안은 소리 없음).
		if (!Router && World->GetNetMode() != NM_Client && World->HasBegunPlay())
		{
			UClass* RouterClass = UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().SoundRouterClass, APGSoundRouter::StaticClass());
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Router = World->SpawnActor<APGSoundRouter>(RouterClass, FTransform::Identity, Params);
			UE_LOG(LogPGObjects, Display, TEXT("PGSound: router %s"), Router ? *Router->GetClass()->GetName() : TEXT("spawn failed"));
		}
		GCachedWorld = World;
		GCachedRouter = Router;
		return Router;
	}

	void PlayAll(const UObject* WorldContext, FName Cue, AActor* Source, const FVector& Location)
	{
		APGSoundRouter* Router = FindOrSpawnRouter(WorldContext);
		if (!Router)
			return;
		if (Router->HasAuthority())
			Router->MulticastCue(Cue, Source, Location);
		else
			Router->PlayHere(Cue, Source, Location);
	}

	void PlayLocal(const UObject* WorldContext, FName Cue, AActor* Source, const FVector& Location)
	{
		if (APGSoundRouter* Router = FindOrSpawnRouter(WorldContext))
			Router->PlayHere(Cue, Source, Location);
	}
}
