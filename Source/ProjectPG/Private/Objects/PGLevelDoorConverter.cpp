#include "Objects/PGLevelDoorConverter.h"

#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Objects/PGObjectSmokeTest.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectTypes.h"

namespace
{
	// 문 메시 이름 → 카탈로그 행. 새 시설 팩의 문을 쓰려면 여기에 한 줄 추가. 서버 바꾸기·클라 숨기기가 같은 표를 쓴다.
	struct FDoorRule { const TCHAR* MeshNameContains; const TCHAR* ObjectId; };
	const FDoorRule DoorRules[] = {
		{ TEXT("SM_Door"), TEXT("OBJ-015") },   // 공장 셔터문: 위로 올라감
		{ TEXT("Door_01"), TEXT("OBJ-012") },   // 오두막 외여닫이문
	};

	const FDoorRule* FindDoorRule(const AStaticMeshActor* Actor)
	{
		const UStaticMeshComponent* Mesh = IsValid(Actor) ? Actor->GetStaticMeshComponent() : nullptr;
		if (!IsValid(Mesh) || !Mesh->GetStaticMesh())
			return nullptr;
		const FString MeshName = Mesh->GetStaticMesh()->GetName();
		for (const FDoorRule& Rule : DoorRules)
			if (MeshName.Contains(Rule.MeshNameContains))
				return &Rule;
		return nullptr;
	}
}

int32 PGLevelDoorConverter::HideConvertedDoorsOnClient(UWorld* World)
{
	if (!IsValid(World) || World->GetNetMode() != NM_Client)
		return 0;
	int32 Hidden = 0;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (It->IsHidden() || !FindDoorRule(*It))
			continue;
		It->SetActorHiddenInGame(true);
		It->SetActorEnableCollision(false);
		++Hidden;
	}
	if (Hidden > 0)
		UE_LOG(LogPGObjects, Display, TEXT("HideConvertedDoorsOnClient: hid %d level door mesh(es) replaced by the server"), Hidden);
	return Hidden;
}

int32 PGLevelDoorConverter::ConvertLevelDoors(UWorld* World)
{
	UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(World);
	if (!IsValid(World) || !Spawner || World->GetNetMode() == NM_Client)
		return 0;
	if (Spawner->GetCatalogCount() == 0)
		PGObjectSmokeTest::RegisterDefaultCatalog(*Spawner);

	TArray<AStaticMeshActor*> ToReplace;
	TArray<FName> Ids;
	for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
	{
		if (const FDoorRule* Rule = FindDoorRule(*It))
		{
			ToReplace.Add(*It);
			Ids.Add(FName(Rule->ObjectId));
		}
	}

	int32 Converted = 0;
	for (int32 Index = 0; Index < ToReplace.Num(); ++Index)
	{
		AStaticMeshActor* Original = ToReplace[Index];
		const FTransform Transform = Original->GetActorTransform();
		if (Spawner->SpawnFromCatalog(Ids[Index], Transform, static_cast<int64>(Index + 1)))
		{
			Original->Destroy();
			++Converted;
		}
	}
	if (ToReplace.Num() > 0)
		UE_LOG(LogPGObjects, Display, TEXT("ConvertLevelDoors: found=%d converted=%d"), ToReplace.Num(), Converted);
	return Converted;
}
