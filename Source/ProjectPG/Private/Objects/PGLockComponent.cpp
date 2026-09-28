#include "Objects/PGLockComponent.h"

#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"

UPGLockComponent::UPGLockComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPGLockComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGLockComponent, bLocked);
	DOREPLIFETIME(UPGLockComponent, RequiredKeyId);
}

bool UPGLockComponent::CanUnlock(const APawn* Pawn) const
{
	if (!bLocked)
		return true;
	// 열쇠 이름이 없는 잠금은 장치(제어 패널·스위치)로만 풀린다.
	if (RequiredKeyId.IsNone())
		return false;
	return UPGItemReceiverLibrary::HasItem(Pawn, RequiredKeyId, 1);
}

bool UPGLockComponent::TryUnlock(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
		return false;
	if (!bLocked)
		return true;
	if (!CanUnlock(Pawn))
		return false;

	if (bConsumeKey && !UPGItemReceiverLibrary::ConsumeItem(Pawn, RequiredKeyId, 1))
		return false;

	bLocked = false;
	OnRep_Locked();
	OnLockStateChanged.Broadcast(false, Pawn);
	return true;
}

void UPGLockComponent::SetLocked(bool bNewLocked)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
		return;
	if (bLocked == bNewLocked)
		return;
	bLocked = bNewLocked;
	OnRep_Locked();
	OnLockStateChanged.Broadcast(bLocked, nullptr);
}

void UPGLockComponent::Configure(bool bNewLocked, FName NewRequiredKeyId)
{
	bLocked = bNewLocked;
	RequiredKeyId = NewRequiredKeyId;
}

FText UPGLockComponent::GetLockedPrompt() const
{
	if (RequiredKeyId.IsNone())
		return NSLOCTEXT("PGObject", "LockedNoKey", "잠김");
	return FText::Format(NSLOCTEXT("PGObject", "LockedNeedKey", "잠김 ({0} 필요)"), FText::FromName(RequiredKeyId));
}

void UPGLockComponent::OnRep_Locked()
{
	// 클라이언트 연출(자물쇠 메시 숨김 등)은 소유 액터가 델리게이트로 받아 처리한다.
}
