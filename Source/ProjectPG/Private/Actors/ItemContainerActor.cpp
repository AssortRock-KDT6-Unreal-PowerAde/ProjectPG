// Fill out your copyright notice in the Description page of Project Settings.


#include "Actors/ItemContainerActor.h"
#include "Common/PGSoundRouter.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Components/BoxComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGLockComponent.h"
#include "Objects/PGLootableComponent.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectTypes.h"

AItemContainerActor::AItemContainerActor()
{
	// 상자는 매 프레임 할 일이 없다. 맵 한 판에 수십 개가 깔리는데
	// 전부 Tick을 켜두면 아무것도 안 하는 함수 호출만 초당 수천 번이 된다.
	// 뚜껑이 움직이는 0.4초 동안만 Tick을 켠다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	// 몸통 → 경첩 → 뚜껑 순서로 붙인다. 뚜껑을 직접 돌리지 않고 경첩을 돌린다.
	LidHinge = CreateDefaultSubobject<USceneComponent>(TEXT("LidHinge"));
	LidHinge->SetupAttachment(MeshComponent);

	LidComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lid"));
	LidComponent->SetupAttachment(LidHinge);
	LidComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	Storage = CreateDefaultSubobject<UPGLootableComponent>(TEXT("Storage"));

	LockComponent = CreateDefaultSubobject<UPGLockComponent>(TEXT("Lock"));
	// 기본 상자는 잠기지 않는다. 잠긴 상자는 카탈로그 행이나 에디터에서 켠다.
	LockComponent->Configure(false, NAME_None);

	DisplayName = NSLOCTEXT("Container", "DefaultName", "상자");
}

void AItemContainerActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AItemContainerActor, bIsOpen);
	DOREPLIFETIME(AItemContainerActor, bKnockedLoose);
}

void AItemContainerActor::KnockLoose(const FVector& Velocity)
{
	if (!HasAuthority() || !IsValid(MeshComponent) || !MeshComponent->GetStaticMesh())
		return;
	// 너무 세게 맞으면 맵 밖·산 너머까지 날아가 루팅을 못 한다. 속도를 묶는다.
	const FVector Kick = Velocity.GetClampedToMaxSize(2500.0f);
	if (bKnockedLoose)
	{
		if (UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetRootComponent()); Body && Body->IsSimulatingPhysics())
			Body->AddImpulse(Kick, NAME_None, true);
		return;
	}
	bKnockedLoose = true; // 복제 — 클라이언트도 OnRep_KnockedLoose 에서 같은 몸통을 만든다
	UPrimitiveComponent* Body = MakeLooseBody();
	if (!Body)
		return;
	// 물리는 서버가 돌리고 자리만 복제한다(바닥 아이템과 같다).
	SetReplicateMovement(true);
	Body->SetSimulatePhysics(true);
	Body->AddImpulse(Kick, NAME_None, true);
	UE_LOG(LogPGObjects, Display, TEXT("Container %s: knocked loose at %s (kick %.0f cm/s, body %s)"),
		*GetName(), *GetActorLocation().ToCompactString(), Kick.Size(), *Body->GetName());
}

// 날아가는 몸통을 만든다(서버·클라이언트 같은 모양). 루트를 이 몸통으로 바꾸고 원래 루트·메시를 그 밑에 붙인다.
// 멀티(9/28): 전에는 서버에서만 루트를 바꿔서, 클라이언트의 상자는 루트가 빈 자리(물리 없음)라 서버가 보내는 물리 자리를
//   받을 몸이 없었다 — 서버에서만 날아가고 클라 화면에서는 제자리였다(두 사람 시험: 상자 246개 중 0개 움직임).
//   이제 클라이언트도 복제된 bKnockedLoose 를 받고 같은 몸통을 만든다. 물리 켜기·자리 맞추기는 엔진 복제가 한다.
// 메시가 "정밀 모양 그대로(ComplexAsSimple)" 이거나 단순 모양이 없으면 물리를 켤 수 없다 — 켜려 하면 "피직스 시뮬레이션이 켜져 있어야
//   충격량 추가 가능" 경고만 나고 제자리에 남았다(9/28 사용자 PIE, SM_box). 그런 상자는 메시 크기의 보이지 않는 상자 몸통(KnockBody)을 쓴다.
UPrimitiveComponent* AItemContainerActor::MakeLooseBody()
{
	if (!IsValid(MeshComponent) || !MeshComponent->GetStaticMesh())
		return nullptr;
	if (UPrimitiveComponent* Existing = Cast<UPrimitiveComponent>(GetRootComponent()); Existing && (Existing == MeshComponent || Existing->GetFName() == TEXT("KnockBody")))
		return Existing; // 이미 만들었다
	USceneComponent* OldRoot = GetRootComponent();
	const UBodySetup* Setup = MeshComponent->GetBodySetup();
	const bool bNeedsBox = !Setup || Setup->CollisionTraceFlag == CTF_UseComplexAsSimple || Setup->AggGeom.GetElementCount() == 0;
	UPrimitiveComponent* Body = MeshComponent;
	if (bNeedsBox)
	{
		const FBox LocalBox = MeshComponent->GetStaticMesh()->GetBoundingBox();
		UBoxComponent* Box = NewObject<UBoxComponent>(this, TEXT("KnockBody"));
		Box->SetBoxExtent(LocalBox.GetExtent() * MeshComponent->GetComponentScale().GetAbs());
		Box->SetWorldLocationAndRotation(MeshComponent->GetComponentTransform().TransformPosition(LocalBox.GetCenter()), MeshComponent->GetComponentQuat());
		Box->RegisterComponent();
		Body = Box;
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly); // F 로 겨누는 선은 그대로 메시에 맞게
	}
	else
	{
		// 바닥 아이템(APGFloorItemActor::MakeLoose)과 같은 요령: 메시를 루트로 올린다. 뚜껑(경첩)은 메시에 붙어 있어 같이 날아간다.
		MeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		MeshComponent->SetMobility(EComponentMobility::Movable);
	}
	SetRootComponent(Body);
	if (Body != MeshComponent)
		MeshComponent->AttachToComponent(Body, FAttachmentTransformRules::KeepWorldTransform);
	if (IsValid(OldRoot) && OldRoot != Body && OldRoot != MeshComponent)
		OldRoot->AttachToComponent(Body, FAttachmentTransformRules::KeepWorldTransform);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetCanEverAffectNavigation(false);
	Body->SetMassOverrideInKg(NAME_None, 40.0f, true);
	Body->SetLinearDamping(0.3f);
	Body->SetAngularDamping(0.6f);
	return Body;
}

void AItemContainerActor::OnRep_KnockedLoose()
{
	if (!bKnockedLoose)
		return;
	if (UPrimitiveComponent* Body = MakeLooseBody())
	{
		// 엔진의 물리 복제는 "루트가 물리 몸일 때" 만 물리를 켜 준다. 복제 순서상 몸통이 생기기 전에 그 신호가 지나갔을 수 있어 직접 켠다
		//   — 클라는 함께 굴리고, 서버 자리가 오면 엔진이 맞춘다(바닥 아이템과 같은 방식).
		Body->SetSimulatePhysics(true);
		UE_LOG(LogPGObjects, Display, TEXT("Container %s: knocked loose on this screen (body %s)"), *GetName(), *Body->GetName());
	}
}

void AItemContainerActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	LootTableId = Row.LootTableId;
	LockComponent->Configure(Row.bLocked, Row.RequiredKeyId);
}

void AItemContainerActor::ApplyCatalogLook()
{
	Super::ApplyCatalogLook();
	ApplySoftMesh(LidComponent, CatalogLook.SecondaryMesh); // 뚜껑(클라이언트에도 — 없으면 여는 동작이 안 보였다)
}

bool AItemContainerActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	// "지금 만질 수 있는가"를 답하는 함수다. 여는 것은 HandleInteract가 한다.
	// 여기서 상태를 바꾸면 안 된다 - const 함수인 이유가 그것이다.
	// 열린 상자는 내용물이 남아 있는 동안 계속 뒤질 수 있다(타르코프식). 비면 끝.
	if (bIsOpen)
	{
		if (Storage->CanLoot(Interactor))
			return true;
		OutReason = NSLOCTEXT("Container", "Empty", "비어 있음");
		return false;
	}

	// 잠긴 상자는 열쇠를 가진 사람만 만질 수 있다. 열쇠 확인은 잠금 컴포넌트가 한다.
	if (LockComponent->IsLocked() && !LockComponent->CanUnlock(Interactor))
	{
		OutReason = LockComponent->GetLockedPrompt();
		return false;
	}

	return true;
}

void AItemContainerActor::HandleInteract(APawn* Interactor)
{
	// 실제로 여는 쪽. 서버에서만 불린다(부모가 보장).
	if (LockComponent->IsLocked() && !LockComponent->TryUnlock(Interactor))
		return;

	// 이미 열린 상자에 다시 F: 남은 내용물을 전부 가져간다. 하나씩 집는 건 Storage->TakeItem(UI·숫자키).
	if (bIsOpen)
	{
		Storage->Loot(Interactor);
		return;
	}

	// 처음 여는 순간 루팅 테이블을 굴려 칸(Storage)에 담는다. 바로 인벤토리로 넘기지 않는다 -
	// 타르코프·배그처럼 상자 칸을 보고 고르는 게 목표라, 내용물은 상자에 남고 플레이어가 꺼내 간다.
	const int64 Seed = LootSeed != 0 ? LootSeed : FDateTime::UtcNow().GetTicks();
	if (UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(this))
		LastLoot = Spawner->RollLoot(LootTableId, Seed);
	Storage->SetContents(LastLoot);
	Storage->ActivateLoot(Seed);
	UE_LOG(LogPGObjects, Display, TEXT("Container %s (%s) opened: %d stacks in storage"), *GetName(), *ObjectId.ToString(), LastLoot.Num());

	bIsOpen = true;
	OnRep_IsOpen();
}

FText AItemContainerActor::GetPromptInternal() const
{
	// 화면에 뜰 안내 문구. 상태마다 다른 말이 나와야 플레이어가
	// "왜 안 열리지"를 헤매지 않는다.
	if (bIsOpen)
	{
		const int32 Stacks = Storage->GetContents().Num();
		if (Stacks == 0)
			return FText::Format(NSLOCTEXT("Container", "EmptyName", "{0} (비어 있음)"), DisplayName);
		return FText::Format(NSLOCTEXT("Container", "TakeAll", "{0} 전부 집기 ({1}칸, 숫자키로 하나씩)"), DisplayName, FText::AsNumber(Stacks));
	}

	if (LockComponent->IsLocked())
		return LockComponent->GetLockedPrompt();

	if (InteractSeconds > 0.0f)
		return FText::Format(NSLOCTEXT("Container", "OpenHold", "{0} 열기 ({1}초 유지)"), DisplayName, FText::AsNumber(InteractSeconds));

	return FText::Format(NSLOCTEXT("Container", "Open", "{0} 열기"), DisplayName);
}

void AItemContainerActor::OnRep_IsOpen()
{
	// 열림 연출은 서버·클라이언트 모두에서 돈다. 뚜껑이 없으면 아무것도 하지 않는다.
	if (bIsOpen)
		PGSound::PlayLocal(this, FName(TEXT("Container_Open")), this, GetActorLocation()); // 모든 기계에서 도는 곳 — PlayLocal
	if (!IsValid(LidComponent) || !IsValid(LidComponent->GetStaticMesh()))
		return;
	// 여는 순간에 맞춘다. 카탈로그로 넣은 메시든 BP에서 넣은 메시든, 클라이언트든 같은 결과가 나온다.
	// 닫힌 상태에서는 경첩 위치와 뚜껑 위치가 서로 상쇄되므로 미리 맞출 필요가 없다.
	FitLidHinge();
	LidAnimElapsed = 0.0f;
	SetActorTickEnabled(true);
}

void AItemContainerActor::FitLidHinge()
{
	if (!IsValid(LidComponent) || !IsValid(LidComponent->GetStaticMesh()) || !IsValid(LidHinge))
		return;

	// 뚜껑 메시의 크기 상자(메시 자기 좌표). 팩마다 원점 위치가 달라서 원점을 믿지 않고 크기로 계산한다.
	const FBox Box = LidComponent->GetStaticMesh()->GetBoundingBox();
	const FVector Center = Box.GetCenter();
	const FVector Size = Box.GetSize();

	// 경첩은 긴 변 쪽 뒷모서리, 뚜껑 밑면 높이에 둔다. 실제 상자 뚜껑이 대부분 긴 변에 경첩이 있다.
	// Far = 경첩 반대편 모서리. 열었을 때 이 점이 위로 올라가야 "제대로 열린 것"이다.
	const bool bHingeAlongY = Size.Y >= Size.X;
	const FVector Hinge = bHingeAlongY
		? FVector(Box.Max.X, Center.Y, Box.Min.Z)
		: FVector(Center.X, Box.Max.Y, Box.Min.Z);
	const FVector Far = bHingeAlongY
		? FVector(Box.Min.X, Center.Y, Box.Min.Z)
		: FVector(Center.X, Box.Min.Y, Box.Min.Z);

	// Y축 경첩은 Pitch, X축 경첩은 Roll로 돈다. 부호(+/-)는 외우지 않고
	// 두 방향을 다 돌려 보고 반대편 모서리가 더 높이 올라가는 쪽을 고른다.
	const float Angle = FMath::Abs(LidOpenAngle);
	const FRotator Plus = bHingeAlongY ? FRotator(Angle, 0.0f, 0.0f) : FRotator(0.0f, 0.0f, Angle);
	const FRotator Minus = bHingeAlongY ? FRotator(-Angle, 0.0f, 0.0f) : FRotator(0.0f, 0.0f, -Angle);
	const FVector Arm = Far - Hinge;
	LidOpenRotation = Plus.RotateVector(Arm).Z >= Minus.RotateVector(Arm).Z ? Plus : Minus;

	// 경첩을 경첩 자리로 옮기고, 뚜껑은 그만큼 반대로 당겨 닫힌 모습은 그대로 둔다.
	LidHinge->SetRelativeLocation(Hinge);
	LidComponent->SetRelativeLocation(-Hinge);
}

void AItemContainerActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	LidAnimElapsed += DeltaTime;
	const float Alpha = LidAnimSeconds > 0.0f ? FMath::Clamp(LidAnimElapsed / LidAnimSeconds, 0.0f, 1.0f) : 1.0f;
	const float Openness = bIsOpen ? Alpha : 1.0f - Alpha;
	LidHinge->SetRelativeRotation(LidOpenRotation * Openness);

	if (Alpha >= 1.0f)
		SetActorTickEnabled(false);
}
