#include "Actors/MapBuilder/MapSeatStatus.h"

#include "Actors/MapBuilder/PGLoadingScreenSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

UMapSeatStatus::UMapSeatStatus()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UMapSeatStatus::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UMapSeatStatus, bSeated, COND_OwnerOnly);
}

void UMapSeatStatus::MarkSeated()
{
	if (bSeated)
		return;
	bSeated = true;
	const APlayerController* PlayerController = Cast<APlayerController>(GetOwner());
	if (PlayerController && PlayerController->IsLocalController())
		TellLoadingScreen();
}

void UMapSeatStatus::OnRep_Seated()
{
	if (bSeated)
		TellLoadingScreen();
}

void UMapSeatStatus::TellLoadingScreen()
{
	if (UPGLoadingScreenSubsystem* Loading = UPGLoadingScreenSubsystem::Get(this))
		Loading->NotifySeated(GetWorld());
}
