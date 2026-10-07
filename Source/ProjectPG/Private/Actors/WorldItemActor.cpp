#include "Actors/WorldItemActor.h"

#include "Common/TableData.h"
#include "Components/InventoryComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/ItemSubSystem.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AWorldItemActor::AWorldItemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// 모양은 부모(AInteractActor)의 MeshComp 를 그대로 쓴다(따로 메시 부품을 두지 않는다).
	// 부딪힘: 사람(Pawn)은 그냥 지나가고(바닥 물건에 걸려 넘어지지 않게), 눈 선(Visibility)만 막는다.
	// 왜 Visibility 를 막나: 형님 상호작용(E 키)이 Visibility 로 쏘는 선·구로 바라보는 물건을 찾아서.
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MeshComp->SetGenerateOverlapEvents(false);
	MeshComp->SetCanEverAffectNavigation(false);
	Tags.Add(TEXT("WorldItem"));
}

void AWorldItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWorldItemActor, ItemID);
	DOREPLIFETIME(AWorldItemActor, Quantity);
}

void AWorldItemActor::BeginPlay()
{
	Super::BeginPlay();
	// 서버만 지운다(지우면 복제로 들어온 사람 쪽에서도 사라진다).
	if (HasAuthority() && InventoryComp)
		InventoryComp->OnInventoryUpdated.AddDynamic(this, &AWorldItemActor::HandleInventoryUpdated);
}

// 들어온 사람 쪽: 번호·개수가 오면 서버와 같은 모양·크기로 바꾼다(위치는 서버가 이미 바닥에 맞춰 둔 것이 함께 온다).
// 상자 칸·내용은 건드리지 않는다(들어온 사람은 못 바꾼다 — 서버가 보낸 것을 형님 인벤토리 복제가 받는다).
void AWorldItemActor::OnRep_Item()
{
	ApplyItemLook();
}

bool AWorldItemActor::SetItem(FName InItemID, int32 InQuantity)
{
	// 형님 규칙(InventoryDedicatedServerSync.md): 상자 내용은 서버에서, BeginPlay 뒤에만 채운다.
	// 그 전에는 인벤토리가 "준비됨" 표시가 없어 복제(내용 보내기)가 안 된다.
	if (!HasAuthority() || !HasActorBegunPlay() || !InventoryComp || !MyActorGuid.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldItem: SetItem skipped (authority=%d begun=%d guid=%d) item=%s"),
			HasAuthority(), HasActorBegunPlay(), MyActorGuid.IsValid(), *InItemID.ToString());
		return false;
	}

	ItemID = InItemID;
	Quantity = FMath::Max(1, InQuantity);
	ApplyItemLook();

	// 상자 칸 = 아이템 크기(예: 권총 2×1). 부모가 만든 10×10 은 아이템 하나에 너무 넓다.
	const FItemTableRow* Data = InventoryComp->GetItemData(ItemID);
	if (!Data)
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldItem: item %s not in item table - nothing placed"), *ItemID.ToString());
		return false;
	}
	InventoryComp->RegisterContainer(MyActorGuid, FIntPoint(FMath::Max(1, Data->GridSize.X), FMath::Max(1, Data->GridSize.Y)));
	if (!InventoryComp->AddItemByID(ItemID, MyActorGuid, Quantity))
	{
		UE_LOG(LogTemp, Warning, TEXT("WorldItem: AddItem failed item=%s size=(%d,%d)"),
			*ItemID.ToString(), Data->GridSize.X, Data->GridSize.Y);
		return false;
	}
	bHasHeldItem = true;
	return true;
}

void AWorldItemActor::ApplyItemLook()
{
	UStaticMesh* ItemMesh = nullptr;
	if (UItemSubSystem* Items = UItemSubSystem::Get(this))
		if (const FItemTableRow* Row = Items->GetItem(ItemID))
			ItemMesh = Row->WorldMesh;
	MeshComp->SetStaticMesh(ItemMesh ? ItemMesh : FallbackMesh.Get());

	// 너무 큰 메시는 가장 긴 변이 MaxSizeCm 이 되게 줄인다(늘리지는 않는다).
	// 대신 모양(표에 메시 없음)은 FallbackSizeCm 크기의 작은 상자로 — 엔진 큐브는 1m 라 그대로 두면 너무 크다.
	if (const UStaticMesh* Shown = MeshComp->GetStaticMesh())
	{
		const float LimitCm = ItemMesh ? MaxSizeCm : FallbackSizeCm;
		const float LongestCm = Shown->GetBounds().BoxExtent.GetMax() * 2.0f;
		if (LongestCm > LimitCm)
			MeshComp->SetRelativeScale3D(FVector(LimitCm / LongestCm));
	}

	OnItemSet();
}

void AWorldItemActor::PlaceOnFloor(const FVector& FloorPoint)
{
	SetActorLocation(FloorPoint);
	// 지금 메시 아래면과 바닥의 높이 차이만큼 올린다(+1cm: 바닥과 같은 높이면 깜빡거림).
	const float BottomZ = MeshComp->Bounds.GetBox().Min.Z;
	AddActorWorldOffset(FVector(0.0f, 0.0f, FloorPoint.Z - BottomZ + 1.0f));
}

// 아이템을 가방으로 끌어 가면(형님 서버 이동) 이 상자가 비게 된다 → 바닥에 빈 껍데기가 남지 않게 지운다.
// 왜 다음 틱에 지우나: 지금은 형님 이동 함수가 알림을 돌리는 도중이라, 그 한가운데서 지우지 않으려고.
void AWorldItemActor::HandleInventoryUpdated()
{
	if (!bHasHeldItem || bDestroyQueued || !InventoryComp || !HasAuthority())
		return;
	if (InventoryComp->GetItems(MyActorGuid).Num() > 0)
		return;
	bDestroyQueued = true;
	UE_LOG(LogTemp, Display, TEXT("WorldItem: %s emptied (item=%s taken) - removing"), *GetName(), *ItemID.ToString());
	GetWorldTimerManager().SetTimerForNextTick(FTimerDelegate::CreateWeakLambda(this, [this]()
	{
		Destroy();
	}));
}
