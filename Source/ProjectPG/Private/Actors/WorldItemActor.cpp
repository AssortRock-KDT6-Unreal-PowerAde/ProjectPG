#include "Actors/WorldItemActor.h"

#include "Components/StaticMeshComponent.h"
#include "Core/ItemSubSystem.h"
#include "Engine/StaticMesh.h"
#include "Net/UnrealNetwork.h"

AWorldItemActor::AWorldItemActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// 부딪힘: 사람(Pawn)은 그냥 지나가고(바닥 물건에 걸려 넘어지지 않게), 눈 선(Visibility)만 막는다.
	// 왜 Visibility 를 막나: 나중에 "바라보는 물건 줍기" 가 보통 Visibility 로 쏘는 선으로 물건을 찾아서.
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Mesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Mesh->SetGenerateOverlapEvents(false);
	Mesh->SetCanEverAffectNavigation(false);
	Tags.Add(TEXT("WorldItem"));
}

void AWorldItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWorldItemActor, ItemID);
	DOREPLIFETIME(AWorldItemActor, Quantity);
}

// 들어온 사람 쪽: 번호·개수가 오면 서버와 같은 모양·크기로 바꾼다(위치는 서버가 이미 바닥에 맞춰 둔 것이 함께 온다).
void AWorldItemActor::OnRep_Item()
{
	SetItem(ItemID, Quantity);
}

void AWorldItemActor::SetItem(FName InItemID, int32 InQuantity)
{
	ItemID = InItemID;
	Quantity = FMath::Max(1, InQuantity);

	UStaticMesh* ItemMesh = nullptr;
	if (UItemSubSystem* Items = UItemSubSystem::Get(this))
		if (const FItemTableRow* Row = Items->GetItem(ItemID))
			ItemMesh = Row->WorldMesh;
	Mesh->SetStaticMesh(ItemMesh ? ItemMesh : FallbackMesh.Get());

	// 너무 큰 메시는 가장 긴 변이 MaxSizeCm 이 되게 줄인다(늘리지는 않는다).
	// 대신 모양(표에 메시 없음)은 FallbackSizeCm 크기의 작은 상자로 — 엔진 큐브는 1m 라 그대로 두면 너무 크다.
	if (const UStaticMesh* Shown = Mesh->GetStaticMesh())
	{
		const float LimitCm = ItemMesh ? MaxSizeCm : FallbackSizeCm;
		const float LongestCm = Shown->GetBounds().BoxExtent.GetMax() * 2.0f;
		if (LongestCm > LimitCm)
			Mesh->SetRelativeScale3D(FVector(LimitCm / LongestCm));
	}

	OnItemSet();
}

void AWorldItemActor::PlaceOnFloor(const FVector& FloorPoint)
{
	SetActorLocation(FloorPoint);
	// 지금 메시 아래면과 바닥의 높이 차이만큼 올린다(+1cm: 바닥과 같은 높이면 깜빡거림).
	const float BottomZ = Mesh->Bounds.GetBox().Min.Z;
	AddActorWorldOffset(FVector(0.0f, 0.0f, FloorPoint.Z - BottomZ + 1.0f));
}
