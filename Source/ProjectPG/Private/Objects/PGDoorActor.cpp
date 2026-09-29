#include "Objects/PGDoorActor.h"
#include "Common/PGSoundRouter.h"
#include "Common/PGPhysicsUtil.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGLockComponent.h"
#include "TimerManager.h"

void APGDoorActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Tags.AddUnique(PGPhysicsUtil::ShatterWholeTag);
}

APGDoorActor::APGDoorActor()
{
	// 로봇·탱크·차가 박으면 문짝을 통째로 잔해로 날린다 — 물리 유틸은 문 클래스를 모르고 이 태그만 본다(PGPhysicsUtil::ShatterWholeTag).
	Tags.AddUnique(PGPhysicsUtil::ShatterWholeTag);

	// 문틀은 부모의 MeshComponent. 문짝은 힌지(Pivot)에 붙여서 Pivot만 돌리거나 옮긴다.
	PivotA = CreateDefaultSubobject<USceneComponent>(TEXT("PivotA"));
	PivotA->SetupAttachment(RootScene);

	LeafA = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafA"));
	LeafA->SetupAttachment(PivotA);
	LeafA->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	LeafA->SetCollisionResponseToAllChannels(ECR_Block);

	PivotB = CreateDefaultSubobject<USceneComponent>(TEXT("PivotB"));
	PivotB->SetupAttachment(RootScene);

	LeafB = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeafB"));
	LeafB->SetupAttachment(PivotB);
	LeafB->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	LeafB->SetCollisionResponseToAllChannels(ECR_Block);

	// 문틀 자체는 통과 판정에 끼지 않게 두고, 막는 것은 문짝이 한다.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

	LockComponent = CreateDefaultSubobject<UPGLockComponent>(TEXT("Lock"));
	LockComponent->Configure(false, NAME_None);

	DisplayName = NSLOCTEXT("Door", "DefaultName", "문");
}

void APGDoorActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGDoorActor, bIsOpen);
	DOREPLIFETIME(APGDoorActor, Motion);
}

void APGDoorActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	LockComponent->Configure(Row.bLocked, Row.RequiredKeyId);
	// 움직이는 방식은 카탈로그가 정한다. 예전에는 코드 기본값(SwingSingle)에 고정돼 있어서
	// 미닫이문·셔터도 전부 회전으로 열렸다. 문짝이 두 장인지로 추측하던 것도 여기서 없앤다.
	Motion = Row.Motion;
}

void APGDoorActor::ApplyCatalogLook()
{
	Super::ApplyCatalogLook();
	// 카탈로그의 Mesh는 문짝, SecondaryMesh는 두 번째 문짝(양문형)으로 해석한다.
	// 문틀 메시는 타일 쪽 정적 메시(벽 모듈)가 이미 갖고 있는 경우가 대부분이라 비워 둔다.
	if (!CatalogLook.Mesh.IsNull())
	{
		ApplySoftMesh(LeafA, CatalogLook.Mesh);
		MeshComponent->SetStaticMesh(nullptr);
	}
	ApplySoftMesh(LeafB, CatalogLook.SecondaryMesh);
}

bool APGDoorActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (LockComponent->IsLocked() && !LockComponent->CanUnlock(Interactor))
	{
		OutReason = LockComponent->GetLockedPrompt();
		return false;
	}
	if (bIsOpen && bCheckBlockedBeforeClose && IsBlockedForClosing())
	{
		OutReason = NSLOCTEXT("Door", "Blocked", "막혀 있음");
		return false;
	}
	return true;
}

void APGDoorActor::HandleInteract(APawn* Interactor)
{
	if (LockComponent->IsLocked() && !LockComponent->TryUnlock(Interactor))
		return;
	SetOpen(!bIsOpen, Interactor);
}

void APGDoorActor::SetOpen(bool bNewOpen, APawn* InstigatorPawn)
{
	if (!HasAuthority() || bIsOpen == bNewOpen)
		return;
	if (!bNewOpen && bCheckBlockedBeforeClose && IsBlockedForClosing())
		return;

	bIsOpen = bNewOpen;
	OnRep_IsOpen();
	OnDoorUsed.Broadcast(this, InstigatorPawn, bIsOpen);
	PGSound::PlayAll(this, bIsOpen ? FName(TEXT("Door_Open")) : FName(TEXT("Door_Close")), this, GetActorLocation()); // 서버 → 모두

	if (bIsOpen && AutoCloseSeconds > 0.0f)
	{
		// 약참조 람다: 문이 닫히기 전에 로봇이나 차가 부숴서 Destroy 되면(PGPhysicsUtil 이
		// 그렇게 한다) 날 [this] 람다는 죽은 포인터로 실행된다. 타이머 매니저는 UObject 에
		// 묶인 것만 자동으로 지운다.
		GetWorldTimerManager().SetTimer(AutoCloseTimer,
			FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				SetOpen(false, nullptr);
			}), AutoCloseSeconds, false);
	}
}

void APGDoorActor::OnDeviceSignal_Implementation(bool bOn, AActor* Source)
{
	// 장치가 켜지면 열리고, 꺼지면 닫힌다. 장치는 잠금을 무시한다(제어 패널이 곧 열쇠).
	SetOpen(bOn, nullptr);
}

FText APGDoorActor::GetPromptInternal() const
{
	if (LockComponent->IsLocked())
		return LockComponent->GetLockedPrompt();
	return bIsOpen
		? FText::Format(NSLOCTEXT("Door", "Close", "{0} 닫기"), DisplayName)
		: FText::Format(NSLOCTEXT("Door", "Open", "{0} 열기"), DisplayName);
}

bool APGDoorActor::IsBlockedForClosing() const
{
	// 문짝이 닫힌 자리(문틀 중앙)에 폰이 겹쳐 있으면 막힌 것으로 본다.
	const UWorld* World = GetWorld();
	if (!IsValid(World))
		return false;

	const FVector Center = GetActorLocation() + GetActorUpVector() * 100.0f;
	const FCollisionShape Shape = FCollisionShape::MakeBox(FVector(FMath::Max(SlideDistance, 100.0f) * 0.5f, 30.0f, 100.0f));
	TArray<FOverlapResult> Overlaps;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGDoorBlocked), false, this);
	World->OverlapMultiByObjectType(Overlaps, Center, GetActorQuat(), FCollisionObjectQueryParams(ECC_Pawn), Shape, Params);
	return Overlaps.ContainsByPredicate([](const FOverlapResult& R) { return IsValid(R.GetActor()) && R.GetActor()->IsA<APawn>(); });
}

void APGDoorActor::OnRep_IsOpen()
{
	AnimFrom = bIsOpen ? 0.0f : 1.0f;
	AnimTo = bIsOpen ? 1.0f : 0.0f;
	AnimElapsed = 0.0f;
	// 열린 문짝은 통로를 막지 않아야 한다. 연출과 무관하게 즉시 충돌을 바꾼다.
	const ECollisionEnabled::Type Collision = bIsOpen ? ECollisionEnabled::QueryOnly : ECollisionEnabled::QueryAndPhysics;
	LeafA->SetCollisionEnabled(Collision);
	LeafB->SetCollisionEnabled(Collision);
	SetActorTickEnabled(true);
}

void APGDoorActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	AnimElapsed += DeltaTime;
	const float T = AnimSeconds > 0.0f ? FMath::Clamp(AnimElapsed / AnimSeconds, 0.0f, 1.0f) : 1.0f;
	// 끝에서 살짝 느려지는 곡선. 선형이면 셔터가 기계처럼 뚝 멈춘다.
	const float Eased = FMath::InterpEaseOut(AnimFrom, AnimTo, T, 2.0f);
	ApplyLeafTransform(Eased);

	if (T >= 1.0f)
		SetActorTickEnabled(false);
}

void APGDoorActor::ApplyLeafTransform(float Alpha)
{
	switch (Motion)
	{
	case EPGDoorMotion::SwingSingle:
		PivotA->SetRelativeRotation(FRotator(0.0f, SwingAngle * Alpha, 0.0f));
		break;
	case EPGDoorMotion::SwingDouble:
		PivotA->SetRelativeRotation(FRotator(0.0f, SwingAngle * Alpha, 0.0f));
		PivotB->SetRelativeRotation(FRotator(0.0f, -SwingAngle * Alpha, 0.0f));
		break;
	case EPGDoorMotion::Slide:
		PivotA->SetRelativeLocation(FVector(0.0f, SlideDistance * Alpha, 0.0f));
		break;
	case EPGDoorMotion::Vertical:
		PivotA->SetRelativeLocation(FVector(0.0f, 0.0f, VerticalDistance * Alpha));
		break;
	case EPGDoorMotion::Hatch:
		PivotA->SetRelativeRotation(FRotator(SwingAngle * Alpha, 0.0f, 0.0f));
		break;
	}
}
