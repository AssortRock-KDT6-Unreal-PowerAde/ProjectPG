#include "Objects/PGInteractableActorBase.h"

#include "Components/StaticMeshComponent.h"
#include "LevelDesign/PGMapVisualSet.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY(LogPGObjects);

APGInteractableActorBase::APGInteractableActorBase()
{
	// 오브젝트는 매 프레임 할 일이 없다. 문처럼 연출이 필요한 자식만 잠깐 Tick을 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 상태(열림·잠김·사용 중)는 서버가 정하고 모든 클라이언트에 복제한다.
	bReplicates = true;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(5.0f);
	// 멀티(9/27): 기본 복제 거리 150m 밖에서는 액터가 클라이언트에서 지워져, 멀리 있는 문(원래 문은 숨겼다)이 사라져
	//   건물에 구멍이 뚫려 보이고 상자가 튀어나왔다. 맵이 600m(대각 850m) 라 900m 로 늘린다. 값이 거의 안 바뀌어(갱신 5번/초) 싸다.
	SetNetCullDistanceSquared(FMath::Square(90000.0f));

	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(RootScene);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootScene);
	// 플레이어가 걸어와서 만져야 하므로 막아 세우는 충돌이 필요하다.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
}

void APGInteractableActorBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGInteractableActorBase, CurrentUser);
	DOREPLIFETIME(APGInteractableActorBase, ObjectId);
	DOREPLIFETIME(APGInteractableActorBase, DisplayName);
	DOREPLIFETIME(APGInteractableActorBase, QuestTag);
	DOREPLIFETIME(APGInteractableActorBase, InteractSeconds);
	DOREPLIFETIME(APGInteractableActorBase, CatalogLook);
}

bool APGInteractableActorBase::CanInteract_Implementation(APawn* Interactor) const
{
	if (!IsValid(Interactor))
		return false;

	// 다른 사람이 쓰는 중이면 못 만진다. 자기 자신은 예외(유지형 상호작용 중 재확인).
	if (IsInUse() && CurrentUser != Interactor)
		return false;

	FText Unused;
	return CanInteractInternal(Interactor, Unused);
}

void APGInteractableActorBase::Interact_Implementation(APawn* Interactor)
{
	// 상태 변경은 서버만 한다. 클라이언트에서 직접 불리면 무시하고 경고만 남긴다.
	// 정상 경로는 UPGInteractionComponent::ServerInteract → 여기다.
	if (!HasAuthority())
	{
		UE_LOG(LogPGObjects, Warning, TEXT("%s: Interact called without authority; use UPGInteractionComponent"), *GetName());
		return;
	}

	if (!CanInteract_Implementation(Interactor))
		return;

	HandleInteract(Interactor);
	OnInteracted.Broadcast(this, Interactor);
}

FText APGInteractableActorBase::GetInteractionPrompt_Implementation() const
{
	if (IsInUse())
		return NSLOCTEXT("PGObject", "InUse", "사용 중");
	return GetPromptInternal();
}

bool APGInteractableActorBase::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	return true;
}

FText APGInteractableActorBase::GetPromptInternal() const
{
	return DisplayName.IsEmpty()
		? NSLOCTEXT("PGObject", "DefaultPrompt", "상호작용")
		: DisplayName;
}

bool APGInteractableActorBase::TryBeginUse(APawn* Pawn)
{
	if (!HasAuthority() || !IsValid(Pawn))
		return false;
	if (IsInUse() && CurrentUser != Pawn)
		return false;
	if (CurrentUser != Pawn)
		UseStartedAt = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	CurrentUser = Pawn;
	OnRep_CurrentUser();
	return true;
}

void APGInteractableActorBase::EndUse(APawn* Pawn)
{
	if (!HasAuthority())
		return;
	// 남이 잡은 잠금은 풀 수 없다.
	if (CurrentUser != Pawn)
		return;
	CurrentUser = nullptr;
	OnRep_CurrentUser();
}

void APGInteractableActorBase::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	ObjectId = Row.ObjectId;
	if (!Row.DisplayName.IsEmpty())
		DisplayName = Row.DisplayName;
	QuestTag = Row.QuestTag;
	InteractSeconds = Row.InteractSeconds;
	// 겉모습은 복제 칸에 적어 두고 같은 함수로 입힌다 — 클라이언트도 OnRep 에서 같은 함수를 부른다.
	CatalogLook.Mesh = Row.Mesh;
	CatalogLook.SecondaryMesh = Row.SecondaryMesh;
	CatalogLook.SocketKind = Row.SocketKind;
	CatalogLook.bSet = true;
	ApplyCatalogLook();
}

void APGInteractableActorBase::ApplyCatalogLook()
{
	ApplySoftMesh(MeshComponent, CatalogLook.Mesh);
}

void APGInteractableActorBase::OnRep_CatalogLook()
{
	if (CatalogLook.bSet)
		ApplyCatalogLook();
}

void APGInteractableActorBase::ApplySoftMesh(UStaticMeshComponent* Component, const TSoftObjectPtr<UStaticMesh>& Mesh)
{
	if (!IsValid(Component) || Mesh.IsNull())
		return;
	// 오브젝트는 맵 생성 시 한꺼번에 만들어지므로 동기 로드로 충분하다.
	// 스트리밍이 필요해지면 여기만 비동기로 바꾸면 된다.
	if (UStaticMesh* Loaded = Mesh.LoadSynchronous())
	{
		Component->SetStaticMesh(Loaded);
		// 작은 것은 멀리서 안 그린다(9/28 4060 측정: 바닥 아이템 수천 개가 따로 그려져 CPU 그리기 준비를 먹었다). 거리는 DA_PGMapVisuals.
		// 문·출구 탈것처럼 큰 것(반지름 1.5m 이상)은 그대로 — 멀리서도 길잡이가 된다.
		const float Radius = Loaded->GetBounds().SphereRadius * Component->GetComponentScale().GetAbsMax();
		const UPGMapVisualSet* Visuals = UPGMapVisualSet::GetActive();
		const float Limit = Radius < 60.0f ? Visuals->SmallObjectDrawDistanceCm : Radius < 150.0f ? Visuals->MediumObjectDrawDistanceCm : 0.0f;
		if (Limit > 0.0f)
			Component->SetCachedMaxDrawDistance(Limit);
	}
}
