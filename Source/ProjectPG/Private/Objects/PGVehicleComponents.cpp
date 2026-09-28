#include "Objects/PGVehicleComponents.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"

// ---------------------------------------------------------------------------
// Seat

UPGSeatComponent::UPGSeatComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	// 기본 하차 후보: 좌우, 뒤.
	ExitOffsets = { FVector(0.0f, -150.0f, 0.0f), FVector(0.0f, 150.0f, 0.0f), FVector(-200.0f, 0.0f, 0.0f) };
}

void UPGSeatComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGSeatComponent, Occupant);
}

bool UPGSeatComponent::IsPawnAtEntryPoint(const APawn* Pawn) const
{
	if (!IsValid(Pawn))
		return false;
	const FVector Entry = GetComponentTransform().TransformPosition(EntryOffset);
	return FVector::DistSquared(Pawn->GetActorLocation(), Entry) <= FMath::Square(EntryRadius);
}

bool UPGSeatComponent::TryEnter(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !IsValid(Pawn) || IsOccupied())
		return false;
	Occupant = Pawn;
	OnEntered.Broadcast(this, Pawn);
	return true;
}

FVector UPGSeatComponent::FindSafeExitLocation() const
{
	const UWorld* World = GetWorld();
	const FTransform SeatTransform = GetComponentTransform();
	for (const FVector& Offset : ExitOffsets)
	{
		const FVector Candidate = SeatTransform.TransformPosition(Offset);
		if (!IsValid(World))
			return Candidate;
		// 캡슐 크기의 상자로 겹침 검사. 차량 자신은 제외한다.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGSeatExit), false, GetOwner());
		const bool bBlocked = World->OverlapBlockingTestByChannel(Candidate + FVector(0.0f, 0.0f, 90.0f), FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(42.0f, 88.0f), Params);
		if (!bBlocked)
			return Candidate;
	}
	return SeatTransform.GetLocation();
}

bool UPGSeatComponent::Exit(APawn* Pawn, FVector& OutExitLocation)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || Occupant != Pawn)
		return false;
	OutExitLocation = FindSafeExitLocation();
	Occupant = nullptr;
	OnExited.Broadcast(this, Pawn);
	return true;
}

// ---------------------------------------------------------------------------
// Fuel

UPGFuelComponent::UPGFuelComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPGFuelComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGFuelComponent, CurrentFuel);
}

bool UPGFuelComponent::CanRefuel(const APawn* Pawn) const
{
	if (!IsValid(Pawn) || CurrentFuel >= Capacity)
		return false;
	if (FVector::DistSquared(Pawn->GetActorLocation(), GetComponentLocation()) > FMath::Square(UseRadius))
		return false;
	return UPGItemReceiverLibrary::HasItem(Pawn, FuelItemId, 1);
}

bool UPGFuelComponent::TryRefuel(APawn* Pawn)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || !CanRefuel(Pawn))
		return false;
	if (!UPGItemReceiverLibrary::ConsumeItem(Pawn, FuelItemId, 1))
		return false;
	CurrentFuel = FMath::Min(Capacity, CurrentFuel + FuelPerItem);
	OnFuelChanged.Broadcast(this, CurrentFuel);
	return true;
}

void UPGFuelComponent::ConsumeFuel(float Amount)
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority() || Amount <= 0.0f)
		return;
	CurrentFuel = FMath::Max(0.0f, CurrentFuel - Amount);
	OnFuelChanged.Broadcast(this, CurrentFuel);
}

FText UPGFuelComponent::GetPrompt() const
{
	if (CurrentFuel >= Capacity)
		return NSLOCTEXT("Fuel", "Full", "연료 가득");
	return FText::Format(NSLOCTEXT("Fuel", "Refuel", "연료 넣기 ({0}/{1})"), FText::AsNumber(FMath::RoundToInt(CurrentFuel)), FText::AsNumber(FMath::RoundToInt(Capacity)));
}
