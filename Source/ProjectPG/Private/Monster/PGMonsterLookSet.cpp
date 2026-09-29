#include "Monster/PGMonsterLookSet.h"

#include "Animation/AnimSequence.h"
#include "Common/PGVisualSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"

UPGMonsterLookSet::UPGMonsterLookSet()
{
	// 기본값 = 원래 코드 표 전부. 새 에셋을 만들면 이 값으로 채워진다.
	for (const TCHAR* Name : { TEXT("Slime"), TEXT("Cactus"), TEXT("Beholder"), TEXT("Rampage"), TEXT("ChestMonster") })
	{
		FPGMonsterVisuals Look;
		if (BuildDefaultLook(Name, Look))
			Looks.Add(Name, Look);
	}
}

const UPGMonsterLookSet* UPGMonsterLookSet::GetActive()
{
	const TSoftObjectPtr<UPGMonsterLookSet>& Designed = UPGVisualSettings::Get().MonsterLookSet;
	if (!Designed.IsNull())
	{
		if (const UPGMonsterLookSet* Loaded = Designed.LoadSynchronous())
			return Loaded;
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: monster look set %s not found — using the code defaults"), *Designed.ToString());
	}
	return GetDefault<UPGMonsterLookSet>();
}

void UPGMonsterLookSet::PreloadAll()
{
	static TSharedPtr<FStreamableHandle> Handle; // 판이 끝날 때까지 쥐고 있어야 메모리에서 안 내려간다
	if (Handle.IsValid() && Handle->IsActive())
		return;
	const UPGMonsterLookSet* Set = GetActive();
	if (!Set)
		return;
	// 모양 한 벌(FPGMonsterVisuals)의 소프트 참조 칸을 모두 모은다(메시·서기·달리기·공격·맞음·죽음·잠·깨기·공격 변형 목록).
	//   칸 이름을 하나하나 적지 않고 구조체를 훑는다 — 모양표에 칸이 늘어도 여기를 안 고쳐도 된다.
	TArray<FSoftObjectPath> Paths;
	for (const TPair<FName, FPGMonsterVisuals>& Look : Set->Looks)
	{
		for (TFieldIterator<FProperty> It(FPGMonsterVisuals::StaticStruct()); It; ++It)
		{
			const void* Value = It->ContainerPtrToValuePtr<void>(&Look.Value);
			if (const FSoftObjectProperty* Soft = CastField<FSoftObjectProperty>(*It))
			{
				const FSoftObjectPath Path = Soft->GetPropertyValue(Value).ToSoftObjectPath();
				if (Path.IsValid())
					Paths.AddUnique(Path);
			}
			else if (const FArrayProperty* Array = CastField<FArrayProperty>(*It))
			{
				const FSoftObjectProperty* Inner = CastField<FSoftObjectProperty>(Array->Inner);
				if (!Inner)
					continue;
				FScriptArrayHelper Helper(Array, Value);
				for (int32 Index = 0; Index < Helper.Num(); ++Index)
				{
					const FSoftObjectPath Path = Inner->GetPropertyValue(Helper.GetRawPtr(Index)).ToSoftObjectPath();
					if (Path.IsValid())
						Paths.AddUnique(Path);
				}
			}
		}
	}
	if (Paths.IsEmpty())
		return;
	const double StartedAt = FPlatformTime::Seconds();
	const int32 Count = Paths.Num();
	TArray<TSoftObjectPtr<USkeletalMesh>> Meshes;
	for (const TPair<FName, FPGMonsterVisuals>& Look : Set->Looks)
		if (!Look.Value.Mesh.IsNull())
			Meshes.AddUnique(Look.Value.Mesh);
	Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths, FStreamableDelegate::CreateLambda([StartedAt, Count, Meshes]()
	{
		UE_LOG(LogTemp, Display, TEXT("PGVisuals: monster looks preloaded — %d asset(s) in %.1f s"), Count, FPlatformTime::Seconds() - StartedAt);
		// 다 읽으면(화면이 있는 컴퓨터만) 몬스터 몸을 안 보이는 채로 한 번씩 만들어 둔다 — 등록할 때 엔진이 그 재질을 그릴 준비(PSO)를
		//   뒤에서 미리 한다. 9/28 4060 측정: 몬스터가 처음 보이는 순간 "Waited for PSO creation 100ms" 로 0.26초 멈췄다. 드래곤과 같은 방법.
		if (!GEngine)
			return;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
				continue;
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.ObjectFlags |= RF_Transient;
			AActor* Warm = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(FVector(0.0f, 0.0f, -200000.0f)), Params);
			if (!Warm)
				continue;
			int32 Warmed = 0;
			for (const TSoftObjectPtr<USkeletalMesh>& Soft : Meshes)
			{
				USkeletalMesh* Mesh = Soft.Get();
				if (!Mesh)
					continue;
				USkeletalMeshComponent* Body = NewObject<USkeletalMeshComponent>(Warm);
				Body->SetSkeletalMeshAsset(Mesh);
				Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				Body->SetHiddenInGame(true);
				if (!Warm->GetRootComponent())
					Warm->SetRootComponent(Body);
				else
					Body->SetupAttachment(Warm->GetRootComponent());
				Body->RegisterComponent(); // 등록하면서 재질 그릴 준비(PSO 미리 만들기)를 시작한다
				++Warmed;
			}
			Warm->SetLifeSpan(20.0f);
			UE_LOG(LogTemp, Display, TEXT("PGVisuals: warmed %d monster body(ies) for drawing (hidden, %s)"), Warmed, *World->GetName());
		}
	}), FStreamableManager::AsyncLoadHighPriority);
	UE_LOG(LogTemp, Display, TEXT("PGVisuals: preloading %d monster asset(s) (%d looks)"), Count, Set->Looks.Num());
}

bool UPGMonsterLookSet::BuildDefaultLook(FName Preset, FPGMonsterVisuals& Out)
{
	auto Anim = [](const FString& Path) { return TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Path)); };
	auto Mesh = [](const FString& Path) { return TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(Path)); };
	const FString Pack = TEXT("/Game/MonsterForSurvivalGame");

	if (Preset == TEXT("Slime"))
	{
		const FString A = Pack + TEXT("/Animation/Polyart/Slime/Slime_");
		Out.Mesh = Mesh(Pack + TEXT("/Mesh/Polyart/Slime_SK.Slime_SK"));
		Out.Idle = Anim(A + TEXT("IdleNormal_ANIM.Slime_IdleNormal_ANIM"));
		Out.Run = Anim(A + TEXT("Run_ANIM.Slime_Run_ANIM"));
		Out.Attack = Anim(A + TEXT("Attack01_ANIM.Slime_Attack01_ANIM"));
		Out.Hit = Anim(A + TEXT("GetHit_ANIM.Slime_GetHit_ANIM"));
		Out.Die = Anim(A + TEXT("Die_ANIM.Slime_Die_ANIM"));
		Out.CapsuleRadius = 50.0f;
		Out.CapsuleHalfHeight = 60.0f;
		Out.MeshOffset = FVector(0.0f, 0.0f, -60.0f);
		return true;
	}
	if (Preset == TEXT("Cactus"))
	{
		const FString A = Pack + TEXT("/Animation/PBR/Cactus/Cactus_");
		Out.Mesh = Mesh(Pack + TEXT("/Mesh/PBR/Cactus_SK.Cactus_SK"));
		Out.Idle = Anim(A + TEXT("IdleNormal_ANIM.Cactus_IdleNormal_ANIM"));
		Out.Run = Anim(A + TEXT("RunFWD_ANIM.Cactus_RunFWD_ANIM"));
		Out.Attack = Anim(A + TEXT("Attack01_ANIM.Cactus_Attack01_ANIM"));
		Out.Die = Anim(A + TEXT("Die_ANIM.Cactus_Die_ANIM"));
		Out.AttackVariants = { Anim(A + TEXT("Attack01_ANIM.Cactus_Attack01_ANIM")), Anim(A + TEXT("Attack02_ANIM.Cactus_Attack02_ANIM")) };
		Out.DormantIdle = Anim(A + TEXT("IdlePlant_ANIM.Cactus_IdlePlant_ANIM"));
		Out.Wake = Anim(A + TEXT("IdlePlantToBattle_ANIM.Cactus_IdlePlantToBattle_ANIM"));
		Out.CapsuleRadius = 40.0f;
		Out.CapsuleHalfHeight = 90.0f;
		Out.MeshOffset = FVector(0.0f, 0.0f, -90.0f);
		return true;
	}
	if (Preset == TEXT("Beholder"))
	{
		const FString A = Pack + TEXT("/Animation/PBR/Beholder/Beholder_");
		Out.Mesh = Mesh(Pack + TEXT("/Mesh/PBR/Beholder_SK.Beholder_SK"));
		Out.Idle = Anim(A + TEXT("IdleNormal_ANIM.Beholder_IdleNormal_ANIM"));
		Out.Run = Anim(A + TEXT("Run_ANIM.Beholder_Run_ANIM"));
		Out.Attack = Anim(A + TEXT("Attack01_ANIM.Beholder_Attack01_ANIM"));
		Out.Die = Anim(A + TEXT("Die_ANIM.Beholder_Die_ANIM"));
		Out.CapsuleRadius = 60.0f;
		Out.CapsuleHalfHeight = 90.0f;
		Out.MeshOffset = FVector(0.0f, 0.0f, -90.0f);
		return true;
	}
	if (Preset == TEXT("Rampage"))
	{
		// Paragon 램페이지. 네발 자세 기본, 공격 모션 3종. 세력 B(적당한 수, 적당한 무장)의 "크리처".
		const FString P = TEXT("/Game/ParagonRampage/Characters/Heroes/Rampage");
		const FString A = P + TEXT("/Animations/");
		Out.Mesh = Mesh(P + TEXT("/Meshes/Rampage.Rampage"));
		Out.Idle = Anim(A + TEXT("Idle.Idle"));
		Out.Run = Anim(A + TEXT("Jog_Quad_Fwd.Jog_Quad_Fwd"));
		Out.Attack = Anim(A + TEXT("Attack_Melee_A.Attack_Melee_A"));
		Out.AttackVariants = { Anim(A + TEXT("Attack_Melee_A.Attack_Melee_A")), Anim(A + TEXT("Attack_Melee_B.Attack_Melee_B")), Anim(A + TEXT("Attack_Melee_C.Attack_Melee_C")) };
		// 피격 모션은 비운다. HitReact_Front 는 애디티브(다른 포즈 위에 얹는 용도)라 단독 재생하면 기준 포즈로 굳은 채 0.87초 멈춘다.
		// 그동안은 IsBusy 라 AI 가 공격도 이동도 안 해서, 맞고 있는 크리처는 "공격 모션이 없는" 것처럼 보였다. 5배 몸집이 움찔하는 것도 어색하다.
		Out.Die = Anim(A + TEXT("Death_A.Death_A"));
		Out.CapsuleRadius = 90.0f;
		Out.CapsuleHalfHeight = 150.0f;
		Out.MeshOffset = FVector(0.0f, 0.0f, -150.0f);
		Out.AttackRange = 380.0f;
		Out.HealthScale = 4.0f;
		Out.DamageScale = 1.8f;
		return true;
	}
	if (Preset == TEXT("ChestMonster"))
	{
		const FString A = Pack + TEXT("/Animation/PBR/ChestMonster/ChestMonster_");
		Out.Mesh = Mesh(Pack + TEXT("/Mesh/PBR/ChestMonster_SK.ChestMonster_SK"));
		Out.Idle = Anim(A + TEXT("IdleNormal_ANIM.ChestMonster_IdleNormal_ANIM"));
		Out.Run = Anim(A + TEXT("Run_ANIM.ChestMonster_Run_ANIM"));
		Out.Attack = Anim(A + TEXT("Attack01_ANIM.ChestMonster_Attack01_ANIM"));
		Out.Die = Anim(A + TEXT("Die_ANIM.ChestMonster_Die_ANIM"));
		Out.CapsuleRadius = 60.0f;
		Out.CapsuleHalfHeight = 70.0f;
		Out.MeshOffset = FVector(0.0f, 0.0f, -70.0f);
		return true;
	}
	return false;
}
