#include "Actors/LootCrateActor.h"

#include "Common/TableData.h"
#include "Components/InventoryComponent.h"
#include "Components/StaticMeshComponent.h"

ALootCrateActor::ALootCrateActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	// 상자는 사람을 막는다(부모 기본 부딪힘 그대로). 길찾기 그물은 안 바꾼다:
	// 상자 수십 개가 판 시작 때 길찾기를 다시 굽게 만들면 멈칫한다.
	MeshComp->SetCanEverAffectNavigation(false);
	Tags.Add(TEXT("LootCrate"));
}

void ALootCrateActor::PostInitializeComponents()
{
	// 부모가 여기서 MyActorGuid 를 만들고 10×10 칸을 등록한다 → 바로 뒤에 이 상자 크기로 바꾼다.
	Super::PostInitializeComponents();
	if (HasAuthority() && GetWorld() && GetWorld()->IsGameWorld() && InventoryComp && MyActorGuid.IsValid())
		InventoryComp->RegisterContainer(MyActorGuid, FIntPoint(FMath::Max(1, CrateSize.X), FMath::Max(1, CrateSize.Y)));
}

bool ALootCrateActor::AddLoot(FName ItemID, int32 Quantity)
{
	// 형님 규칙(InventoryDedicatedServerSync.md): 상자 내용은 서버에서, BeginPlay 뒤에만 채운다.
	if (!HasAuthority() || !HasActorBegunPlay() || !InventoryComp || !MyActorGuid.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("LootCrate: AddLoot skipped (authority=%d begun=%d guid=%d) item=%s"),
			HasAuthority(), HasActorBegunPlay(), MyActorGuid.IsValid(), *ItemID.ToString());
		return false;
	}
	if (InventoryComp->AddItemByID(ItemID, MyActorGuid, Quantity))
		return true;

	// 빈 자리가 없다 → 아이템 높이만큼 줄을 늘리고(가로는 아이템이 들어갈 만큼) 다시 넣는다.
	const FItemTableRow* Data = InventoryComp->GetItemData(ItemID);
	if (!Data)
	{
		UE_LOG(LogTemp, Warning, TEXT("LootCrate: item %s not in item table"), *ItemID.ToString());
		return false;
	}
	FIntPoint Size = InventoryComp->GetInventorySizeByGuid(MyActorGuid);
	Size.X = FMath::Max(Size.X, Data->GridSize.X);
	Size.Y += FMath::Max(1, Data->GridSize.Y);
	InventoryComp->RegisterContainer(MyActorGuid, Size);
	if (InventoryComp->AddItemByID(ItemID, MyActorGuid, Quantity))
	{
		UE_LOG(LogTemp, Display, TEXT("LootCrate: %s grew to (%d,%d) to fit item=%s"), *GetName(), Size.X, Size.Y, *ItemID.ToString());
		return true;
	}
	UE_LOG(LogTemp, Warning, TEXT("LootCrate: AddItem failed item=%s size=(%d,%d)"),
		*ItemID.ToString(), Data->GridSize.X, Data->GridSize.Y);
	return false;
}

void ALootCrateActor::PlaceOnFloor(const FVector& FloorPoint)
{
	SetActorLocation(FloorPoint);
	// 지금 메시 아래면과 바닥의 높이 차이만큼 올린다(+1cm: 바닥과 같은 높이면 깜빡거림).
	const float BottomZ = MeshComp->Bounds.GetBox().Min.Z;
	AddActorWorldOffset(FVector(0.0f, 0.0f, FloorPoint.Z - BottomZ + 1.0f));
}

int32 ALootCrateActor::GetLootCount() const
{
	return InventoryComp && MyActorGuid.IsValid() ? InventoryComp->GetItems(MyActorGuid).Num() : 0;
}
