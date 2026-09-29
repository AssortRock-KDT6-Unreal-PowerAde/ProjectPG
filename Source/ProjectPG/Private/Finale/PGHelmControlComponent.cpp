#include "Finale/PGHelmControlComponent.h"

#include "Finale/PGBattleshipActor.h"
#include "GameFramework/PlayerController.h"

UPGHelmControlComponent::UPGHelmControlComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UPGHelmControlComponent* UPGHelmControlComponent::Ensure(APlayerController* PlayerController)
{
	if (!IsValid(PlayerController))
		return nullptr;
	if (UPGHelmControlComponent* Existing = PlayerController->FindComponentByClass<UPGHelmControlComponent>())
		return Existing;
	if (!PlayerController->HasAuthority())
		return nullptr;
	UPGHelmControlComponent* Created = NewObject<UPGHelmControlComponent>(PlayerController, TEXT("PGHelmControl"));
	Created->RegisterComponent();
	return Created;
}

void UPGHelmControlComponent::RequestSeat(APlayerController* PC, APGBattleshipActor* Ship, bool bSit)
{
	if (!IsValid(PC) || !IsValid(Ship))
		return;
	if (PC->HasAuthority())
	{
		Ship->ServerSeatRequest(PC, bSit);
		return;
	}
	if (UPGHelmControlComponent* Control = PC->FindComponentByClass<UPGHelmControlComponent>())
		Control->ServerRequestSeat(Ship, bSit);
}

void UPGHelmControlComponent::SendInput(APlayerController* PC, APGBattleshipActor* Ship, const FPGHelmInput& Input)
{
	if (!IsValid(PC) || !IsValid(Ship))
		return;
	if (PC->HasAuthority())
	{
		Ship->ServerHelmInput(PC, Input);
		return;
	}
	if (UPGHelmControlComponent* Control = PC->FindComponentByClass<UPGHelmControlComponent>())
		Control->ServerSendInput(Ship, Input);
}

void UPGHelmControlComponent::ServerRequestSeat_Implementation(APGBattleshipActor* Ship, bool bSit)
{
	if (IsValid(Ship))
		Ship->ServerSeatRequest(Cast<APlayerController>(GetOwner()), bSit);
}

void UPGHelmControlComponent::ServerSendInput_Implementation(APGBattleshipActor* Ship, const FPGHelmInput& Input)
{
	if (IsValid(Ship))
		Ship->ServerHelmInput(Cast<APlayerController>(GetOwner()), Input);
}
