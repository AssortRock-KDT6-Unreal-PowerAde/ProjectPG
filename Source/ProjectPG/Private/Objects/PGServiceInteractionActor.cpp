#include "Objects/PGServiceInteractionActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGWearableColors.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

APGServiceInteractionActor::APGServiceInteractionActor()
{
	UsePoint = CreateDefaultSubobject<USceneComponent>(TEXT("UsePoint"));
	UsePoint->SetupAttachment(RootScene);
	// 기본값: 액터 정면 1.5m 앞이 사용 위치.
	UsePoint->SetRelativeLocation(FVector(150.0f, 0.0f, 0.0f));
	UsePoint->SetRelativeRotation(FRotator(0.0f, 180.0f, 0.0f));

	DisplayName = NSLOCTEXT("Service", "DefaultName", "상점");

	InteractBox = CreateDefaultSubobject<UBoxComponent>(TEXT("InteractBox"));
	InteractBox->SetupAttachment(RootScene);
	InteractBox->SetBoxExtent(FVector(40.0f, 70.0f, 70.0f));
	InteractBox->SetRelativeLocation(FVector(0.0f, 0.0f, 100.0f));
	InteractBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	InteractBox->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	InteractBox->SetCanEverAffectNavigation(false);
}

void APGServiceInteractionActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGServiceInteractionActor, ServiceKind);
	DOREPLIFETIME(APGServiceInteractionActor, ReplicatedUseWorld);
	DOREPLIFETIME(APGServiceInteractionActor, ReplicatedTargetWorld);
	DOREPLIFETIME(APGServiceInteractionActor, bUseAreaSet);
}

void APGServiceInteractionActor::OnRep_UseArea()
{
	if (bUseAreaSet)
		ConfigureUseArea(ReplicatedUseWorld, ReplicatedTargetWorld);
}

void APGServiceInteractionActor::ConfigureUseArea(const FVector& UseWorld, const FVector& TargetWorld)
{
	if (HasAuthority())
	{
		ReplicatedUseWorld = UseWorld;
		ReplicatedTargetWorld = TargetWorld;
		bUseAreaSet = true;
	}
	InteractBox->SetWorldLocation(TargetWorld);
	UsePoint->SetWorldLocation(UseWorld);
	// UsePoint 정면이 창구를 본다(IsPawnAtUsePoint 의 각도 기준).
	UsePoint->SetWorldRotation((TargetWorld - UseWorld).GetSafeNormal2D().Rotation());
}

void APGServiceInteractionActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	// 카탈로그의 SocketKind가 곧 서비스 종류다. 택배 소켓이면 택배, 아니면 상점.
	if (Row.SocketKind == EPGSpawnSocketKind::Courier)
		ServiceKind = EPGServiceKind::Courier;
}

bool APGServiceInteractionActor::IsPawnAtUsePoint(const APawn* Pawn) const
{
	if (!IsValid(Pawn))
		return false;
	const FVector PawnLocation = Pawn->GetActorLocation();
	const FVector UseLocation = UsePoint->GetComponentLocation();
	if (FVector::DistSquared2D(PawnLocation, UseLocation) > FMath::Square(MaxUseDistance))
		return false;
	if (MaxUseAngleDeg >= 180.0f)
		return true;
	// UsePoint의 정면이 상점을 바라본다. 플레이어가 그 방향으로 서 있어야 한다.
	const FVector ToShop = (GetActorLocation() - PawnLocation).GetSafeNormal2D();
	const FVector PawnForward = Pawn->GetActorForwardVector().GetSafeNormal2D();
	const float CosAngle = FVector::DotProduct(ToShop, PawnForward);
	return CosAngle >= FMath::Cos(FMath::DegreesToRadians(MaxUseAngleDeg));
}

bool APGServiceInteractionActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (!IsPawnAtUsePoint(Interactor))
	{
		OutReason = NSLOCTEXT("Service", "MoveToUsePoint", "창구 앞으로 이동");
		return false;
	}
	if (ServiceKind == EPGServiceKind::Exchange && !UPGItemReceiverLibrary::HasItem(Interactor, ExchangeCostItemId, ExchangeCostCount))
	{
		OutReason = FText::Format(NSLOCTEXT("Service", "ExchangeNeed", "{0} {1}개 필요"),
			UPGWearableColorLibrary::GetItemDisplayName(ExchangeCostItemId), FText::AsNumber(ExchangeCostCount));
		return false;
	}
	return true;
}

void APGServiceInteractionActor::HandleInteract(APawn* Interactor)
{
	// 교환소는 화면 없이 바로 바꾼다: 먼저 받고(모자라면 아무것도 안 함), 그다음 준다. 주기가 실패하면(가방 꽉 참) 받은 것을 돌려준다.
	if (ServiceKind == EPGServiceKind::Exchange)
	{
		if (!UPGItemReceiverLibrary::ConsumeItem(Interactor, ExchangeCostItemId, ExchangeCostCount))
			return;
		if (!UPGItemReceiverLibrary::GiveItem(Interactor, ExchangeRewardItemId, 1))
		{
			UPGItemReceiverLibrary::GiveItem(Interactor, ExchangeCostItemId, ExchangeCostCount);
			UE_LOG(LogPGObjects, Warning, TEXT("Exchange %s: %s has no room for %s, refunded"), *GetName(), *GetNameSafe(Interactor), *ExchangeRewardItemId.ToString());
			return;
		}
		UE_LOG(LogPGObjects, Display, TEXT("Exchange %s: %s traded %dx %s for %s"), *GetName(), *GetNameSafe(Interactor),
			ExchangeCostCount, *ExchangeCostItemId.ToString(), *ExchangeRewardItemId.ToString());
		OnServiceRequested.Broadcast(this, Interactor, ServiceKind);
		return;
	}
	// 화면이 열려 있는 동안 다른 사람이 끼어들지 못하게 잠근다. EndService에서 푼다.
	if (!TryBeginUse(Interactor))
		return;
	GetWorldTimerManager().SetTimer(ServiceUserCheckTimer, this, &APGServiceInteractionActor::CheckServiceUserStillHere, 1.0f, true);
	UE_LOG(LogPGObjects, Display, TEXT("Service %s (%s): %s requested kind=%d"),
		*GetName(), *ObjectId.ToString(), *GetNameSafe(Interactor), static_cast<int32>(ServiceKind));
	OnServiceRequested.Broadcast(this, Interactor, ServiceKind);
}

void APGServiceInteractionActor::CheckServiceUserStillHere()
{
	APawn* User = GetCurrentUser();
	// 사용 자리에서 두 배 거리까지는 머무는 것으로 본다(창구 앞에서 몸을 조금 움직여도 풀리지 않게).
	if (IsValid(User) && FVector::Dist2D(User->GetActorLocation(), UsePoint->GetComponentLocation()) <= MaxUseDistance * 2.0f)
		return;
	GetWorldTimerManager().ClearTimer(ServiceUserCheckTimer);
	if (IsValid(User))
		EndService(User);
	else
		EndUse(GetCurrentUser()); // 사용자가 사라졌으면(나감·죽음) 그냥 푼다
}

void APGServiceInteractionActor::EndService(APawn* Pawn)
{
	if (!HasAuthority() || GetCurrentUser() != Pawn)
		return;
	EndUse(Pawn);
	OnServiceEnded.Broadcast(this, Pawn, ServiceKind);
}

FText APGServiceInteractionActor::GetExchangeSummary() const
{
	return FText::Format(NSLOCTEXT("Service", "ExchangeSummary", "{0} {1}개 → {2}"),
		UPGWearableColorLibrary::GetItemDisplayName(ExchangeCostItemId), FText::AsNumber(ExchangeCostCount),
		UPGWearableColorLibrary::GetItemDisplayName(ExchangeRewardItemId));
}

FText APGServiceInteractionActor::GetPromptInternal() const
{
	switch (ServiceKind)
	{
	case EPGServiceKind::Shop:    return NSLOCTEXT("Service", "OpenShop", "거래하기");
	case EPGServiceKind::Courier: return NSLOCTEXT("Service", "OpenCourier", "배송 접수");
	case EPGServiceKind::Craft:   return NSLOCTEXT("Service", "OpenCraft", "제작하기");
	case EPGServiceKind::Exchange:
		return FText::Format(NSLOCTEXT("Service", "OpenExchange", "교환하기 ({0} {1}개 → {2})"),
			UPGWearableColorLibrary::GetItemDisplayName(ExchangeCostItemId), FText::AsNumber(ExchangeCostCount),
			UPGWearableColorLibrary::GetItemDisplayName(ExchangeRewardItemId));
	default:                      return NSLOCTEXT("Service", "Disabled", "사용 불가");
	}
}
