#include "Objects/PGDeviceActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"
#include "TimerManager.h"

APGDeviceActor::APGDeviceActor()
{
	PlateTrigger = CreateDefaultSubobject<UBoxComponent>(TEXT("PlateTrigger"));
	PlateTrigger->SetupAttachment(RootScene);
	PlateTrigger->SetBoxExtent(FVector(60.0f, 60.0f, 30.0f));
	PlateTrigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PlateTrigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	PlateTrigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	DisplayName = NSLOCTEXT("Device", "DefaultName", "장치");
}

void APGDeviceActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGDeviceActor, bIsOn);
	DOREPLIFETIME(APGDeviceActor, Mode);
	DOREPLIFETIME(APGDeviceActor, RequiredItemId);
	DOREPLIFETIME(APGDeviceActor, bRequiredItemSatisfied);
}

void APGDeviceActor::BeginPlay()
{
	Super::BeginPlay();
	if (Mode == EPGDeviceMode::PressurePlate)
	{
		PlateTrigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PlateTrigger->SetGenerateOverlapEvents(true);
		if (HasAuthority())
		{
			PlateTrigger->OnComponentBeginOverlap.AddDynamic(this, &APGDeviceActor::OnPlateBeginOverlap);
			PlateTrigger->OnComponentEndOverlap.AddDynamic(this, &APGDeviceActor::OnPlateEndOverlap);
		}
	}
}

void APGDeviceActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	RequiredItemId = Row.ItemId;
}

void APGDeviceActor::AddLinkedTarget(AActor* Target)
{
	if (IsValid(Target) && Target != this)
		LinkedTargets.AddUnique(Target);
}

void APGDeviceActor::SetRequiredItem(FName ItemId, bool bConsume)
{
	RequiredItemId = ItemId;
	bConsumeRequiredItem = bConsume;
}

bool APGDeviceActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (Mode == EPGDeviceMode::PressurePlate)
	{
		OutReason = NSLOCTEXT("Device", "StepOn", "밟아서 작동");
		return false;
	}
	if (!RequiredItemId.IsNone() && !bRequiredItemSatisfied && !UPGItemReceiverLibrary::HasItem(Interactor, RequiredItemId, 1))
	{
		OutReason = FText::Format(NSLOCTEXT("Device", "NeedItem", "{0} 필요"), FText::FromName(RequiredItemId));
		return false;
	}
	return true;
}

void APGDeviceActor::HandleInteract(APawn* Interactor)
{
	// 필요 아이템이 있으면 먼저 처리한다. 소모형이면 여기서 사라진다.
	if (!RequiredItemId.IsNone() && !bRequiredItemSatisfied)
	{
		if (bConsumeRequiredItem && !UPGItemReceiverLibrary::ConsumeItem(Interactor, RequiredItemId, 1))
			return;
		if (bRequiredItemOnce)
			bRequiredItemSatisfied = true;
	}

	switch (Mode)
	{
	case EPGDeviceMode::Toggle:
		SetOn(!bIsOn, Interactor);
		break;
	case EPGDeviceMode::Momentary:
		// 버튼: 켜졌다가 다음 프레임에 꺼진다. 받는 쪽은 "켜짐" 신호만 쓰면 된다.
		SetOn(true, Interactor);
		// 약참조 람다 — 붕괴 구역 안의 장치가 타이머보다 먼저 파괴될 수 있다.
		GetWorldTimerManager().SetTimerForNextTick(
			FTimerDelegate::CreateWeakLambda(this, [this]() { SetOn(false, nullptr); }));
		break;
	case EPGDeviceMode::Timed:
		SetOn(true, Interactor);
		GetWorldTimerManager().SetTimer(TimedHandle,
			FTimerDelegate::CreateWeakLambda(this, [this]() { SetOn(false, nullptr); }),
			FMath::Max(0.01f, TimedSeconds), false);
		break;
	default:
		break;
	}
}

void APGDeviceActor::SetOn(bool bNewOn, APawn* InstigatorPawn)
{
	if (!HasAuthority() || bIsOn == bNewOn)
		return;
	bIsOn = bNewOn;
	OnRep_IsOn();
	BroadcastSignal(InstigatorPawn);
}

void APGDeviceActor::BroadcastSignal(APawn* InstigatorPawn)
{
	UE_LOG(LogPGObjects, Display, TEXT("Device %s (%s): on=%s targets=%d"),
		*GetName(), *ObjectId.ToString(), bIsOn ? TEXT("true") : TEXT("false"), LinkedTargets.Num());
	for (AActor* Target : LinkedTargets)
	{
		if (!IsValid(Target))
			continue;
		if (Target->GetClass()->ImplementsInterface(UPGDeviceSignalTarget::StaticClass()))
			IPGDeviceSignalTarget::Execute_OnDeviceSignal(Target, bIsOn, this);
	}
	OnDeviceStateChanged.Broadcast(this, bIsOn, InstigatorPawn);
}

void APGDeviceActor::OnDeviceSignal_Implementation(bool bOn, AActor* Source)
{
	// 중계. 자기 자신에게 되돌아오는 루프는 LinkedTargets에 자신을 넣지 않는 것으로 막는다.
	SetOn(bOn, nullptr);
}

void APGDeviceActor::OnPlateBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!OtherActor || !OtherActor->IsA<APawn>())
		return;
	if (++PlateOccupants == 1)
		SetOn(true, Cast<APawn>(OtherActor));
}

void APGDeviceActor::OnPlateEndOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32)
{
	if (!OtherActor || !OtherActor->IsA<APawn>())
		return;
	PlateOccupants = FMath::Max(0, PlateOccupants - 1);
	if (PlateOccupants == 0)
		SetOn(false, Cast<APawn>(OtherActor));
}

void APGDeviceActor::OnRep_IsOn()
{
	// 클라이언트 연출(램프 색, 레버 각도)은 BP 자식이나 머티리얼 파라미터로 처리한다.
}

FText APGDeviceActor::GetPromptInternal() const
{
	if (!RequiredItemId.IsNone() && !bRequiredItemSatisfied)
		return FText::Format(NSLOCTEXT("Device", "Insert", "{0} 넣기"), FText::FromName(RequiredItemId));
	return bIsOn
		? FText::Format(NSLOCTEXT("Device", "TurnOff", "{0} 끄기"), DisplayName)
		: FText::Format(NSLOCTEXT("Device", "TurnOn", "{0} 켜기"), DisplayName);
}
