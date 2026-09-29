#include "Common/PGPlayerMessageComponent.h"

#include "Finale/PGAnnounceSubsystem.h"
#include "Flow/PGRunSubsystem.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Objects/PGObjectTypes.h"

namespace
{
	// 막 붙인 부품의 RPC 를 이만큼 미룬다. 부품이 클라이언트에 생기는 데 한두 번의 네트워크 갱신이면 충분하다.
	constexpr double FreshComponentDelaySeconds = 0.5;
}

UPGPlayerMessageComponent::UPGPlayerMessageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

UPGPlayerMessageComponent* UPGPlayerMessageComponent::Ensure(APlayerController* PlayerController)
{
	if (!IsValid(PlayerController))
		return nullptr;
	if (UPGPlayerMessageComponent* Existing = PlayerController->FindComponentByClass<UPGPlayerMessageComponent>())
		return Existing;
	if (!PlayerController->HasAuthority())
		return nullptr;
	UPGPlayerMessageComponent* Created = NewObject<UPGPlayerMessageComponent>(PlayerController, TEXT("PGPlayerMessages"));
	Created->CreatedAt = PlayerController->GetWorld() ? PlayerController->GetWorld()->GetTimeSeconds() : 0.0;
	Created->RegisterComponent();
	return Created;
}

APlayerController* UPGPlayerMessageComponent::ResolvePlayer(const APawn* Pawn)
{
	if (!IsValid(Pawn))
		return nullptr;
	if (APlayerController* PC = Cast<APlayerController>(Pawn->GetController()))
		return PC;
	// 탈것에 탄 사람(조종이 풀린 캐릭터): 붙어 있는 탈것을 조종하는 사람.
	if (const AActor* Parent = Pawn->GetAttachParentActor())
		if (const APawn* Ride = Cast<APawn>(Parent))
			return Cast<APlayerController>(Ride->GetController());
	return nullptr;
}

void UPGPlayerMessageComponent::AnnounceTo(AController* Controller, const TArray<FText>& Lines)
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!IsValid(PC) || Lines.IsEmpty())
		return;
	if (PC->IsLocalController()) // 혼자 하는 판·듣기 서버 방장: 이 컴퓨터 화면에 바로
	{
		if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(PC))
			Announcer->Announce(Lines);
		return;
	}
	if (UPGPlayerMessageComponent* Messages = Ensure(PC))
		Messages->SendAnnounce(Lines);
}

void UPGPlayerMessageComponent::CountdownTo(AController* Controller, const FText& Line)
{
	APlayerController* PC = Cast<APlayerController>(Controller);
	if (!IsValid(PC))
		return;
	if (PC->IsLocalController())
	{
		if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(PC))
			Announcer->SetCountdown(Line);
		return;
	}
	if (UPGPlayerMessageComponent* Messages = Ensure(PC))
		Messages->SendCountdown(Line);
}

void UPGPlayerMessageComponent::AnnounceToAll(const UObject* WorldContext, const TArray<FText>& Lines)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!IsValid(World))
		return;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		AnnounceTo(It->Get(), Lines);
}

void UPGPlayerMessageComponent::SendAnnounce(const TArray<FText>& Lines)
{
	UWorld* World = GetWorld();
	const double Wait = World ? FreshComponentDelaySeconds - (World->GetTimeSeconds() - CreatedAt) : 0.0;
	if (Wait <= 0.0 || !World)
	{
		ClientAnnounce(Lines);
		return;
	}
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Lines]() { ClientAnnounce(Lines); }),
		static_cast<float>(Wait), false);
}

void UPGPlayerMessageComponent::SendCountdown(const FText& Line)
{
	UWorld* World = GetWorld();
	const double Wait = World ? FreshComponentDelaySeconds - (World->GetTimeSeconds() - CreatedAt) : 0.0;
	if (Wait <= 0.0 || !World)
	{
		ClientCountdown(Line);
		return;
	}
	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [this, Line]() { ClientCountdown(Line); }),
		static_cast<float>(Wait), false);
}

void UPGPlayerMessageComponent::SendRunResult(APlayerController* PC, const FPGRunRecord& Record)
{
	if (!IsValid(PC))
		return;
	if (PC->IsLocalController())
	{
		if (UPGRunSubsystem* Run = UPGRunSubsystem::Get(PC))
			Run->ReceiveRunResultFromServer(Record);
		return;
	}
	if (UPGPlayerMessageComponent* Messages = Ensure(PC))
		Messages->ClientRunFinished(Record);
}

void UPGPlayerMessageComponent::ClientRunFinished_Implementation(const FPGRunRecord& Record)
{
	if (UPGRunSubsystem* Run = UPGRunSubsystem::Get(this))
		Run->ReceiveRunResultFromServer(Record);
}

void UPGPlayerMessageComponent::ClientAnnounce_Implementation(const TArray<FText>& Lines)
{
	if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(this))
		Announcer->Announce(Lines);
}

void UPGPlayerMessageComponent::ClientCountdown_Implementation(const FText& Line)
{
	// 1초에 한 줄 — 멀티 시험에서 접속자 화면에 어떤 숫자가 어떤 순서로 떴는지 로그로 본다(9/28 카운트다운 겹침).
	UE_LOG(LogPGObjects, Display, TEXT("PGCountdown: \"%s\""), *Line.ToString().Replace(TEXT("\n"), TEXT(" / ")));
	if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(this))
		Announcer->SetCountdown(Line);
}
