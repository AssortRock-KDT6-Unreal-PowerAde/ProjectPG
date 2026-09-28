#include "Objects/PGQuestObjectActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Pawn.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Net/UnrealNetwork.h"

APGQuestObjectActor::APGQuestObjectActor()
{
	DisplayName = NSLOCTEXT("Quest", "DefaultName", "조사 대상");
}

void APGQuestObjectActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGQuestObjectActor, Mode);
	DOREPLIFETIME(APGQuestObjectActor, RequiredItemId);
	DOREPLIFETIME(APGQuestObjectActor, RequiredCount);
	DOREPLIFETIME(APGQuestObjectActor, bOperated);
}

void APGQuestObjectActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	RequiredItemId = Row.ItemId;
	RequiredCount = FMath::Max(1, Row.ItemCount);
	if (!RequiredItemId.IsNone())
		Mode = EPGQuestObjectMode::DeliverItem;
	else if (Row.InteractSeconds <= 0.0f)
		Mode = EPGQuestObjectMode::Operate;
}

void APGQuestObjectActor::Configure(EPGQuestObjectMode InMode, FName InQuestTag, FName InRequiredItemId, int32 InRequiredCount, bool bInOncePerPlayer)
{
	Mode = InMode;
	QuestTag = InQuestTag;
	RequiredItemId = InRequiredItemId;
	RequiredCount = FMath::Max(1, InRequiredCount);
	bOncePerPlayer = bInOncePerPlayer;
}

bool APGQuestObjectActor::HasCompleted(const APawn* Pawn) const
{
	return CompletedPlayerKeys.Contains(UPGItemReceiverLibrary::GetPlayerKey(Pawn));
}

bool APGQuestObjectActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (bOncePerPlayer && Mode != EPGQuestObjectMode::Operate && HasCompleted(Interactor))
	{
		OutReason = NSLOCTEXT("Quest", "Done", "완료됨");
		return false;
	}
	if (Mode == EPGQuestObjectMode::DeliverItem && !UPGItemReceiverLibrary::HasItem(Interactor, RequiredItemId, RequiredCount))
	{
		OutReason = FText::Format(NSLOCTEXT("Quest", "NeedItems", "{0} x{1} 필요"), FText::FromName(RequiredItemId), FText::AsNumber(RequiredCount));
		return false;
	}
	return true;
}

void APGQuestObjectActor::HandleInteract(APawn* Interactor)
{
	switch (Mode)
	{
	case EPGQuestObjectMode::DeliverItem:
		// 원자적 전달: 확인(CanInteract) 직후 소모한다. 소모가 실패하면 완료 처리하지 않는다.
		if (!UPGItemReceiverLibrary::ConsumeItem(Interactor, RequiredItemId, RequiredCount))
			return;
		break;
	case EPGQuestObjectMode::Operate:
		bOperated = !bOperated;
		break;
	default:
		break;
	}

	CompletedPlayerKeys.Add(UPGItemReceiverLibrary::GetPlayerKey(Interactor));
	UE_LOG(LogPGObjects, Display, TEXT("QuestObject %s (%s): tag=%s by %s mode=%d"),
		*GetName(), *ObjectId.ToString(), *QuestTag.ToString(), *GetNameSafe(Interactor), static_cast<int32>(Mode));
	OnQuestEvent.Broadcast(QuestTag, this, Interactor);
}

FText APGQuestObjectActor::GetPromptInternal() const
{
	switch (Mode)
	{
	case EPGQuestObjectMode::Investigate:
		return FText::Format(NSLOCTEXT("Quest", "Investigate", "{0} 조사"), DisplayName);
	case EPGQuestObjectMode::DeliverItem:
		return FText::Format(NSLOCTEXT("Quest", "Deliver", "{0} 납품 (x{1})"), FText::FromName(RequiredItemId), FText::AsNumber(RequiredCount));
	case EPGQuestObjectMode::Operate:
		return bOperated
			? FText::Format(NSLOCTEXT("Quest", "OperateOff", "{0} 끄기"), DisplayName)
			: FText::Format(NSLOCTEXT("Quest", "OperateOn", "{0} 작동"), DisplayName);
	}
	return DisplayName;
}

// ---------------------------------------------------------------------------

APGQuestTriggerVolume::APGQuestTriggerVolume()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // 서버에서만 판정하고, 결과는 퀘스트 담당이 복제한다.

	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("Volume"));
	SetRootComponent(Volume);
	Volume->SetBoxExtent(FVector(300.0f, 300.0f, 200.0f));
	Volume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Volume->SetCollisionResponseToAllChannels(ECR_Ignore);
	Volume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Volume->SetGenerateOverlapEvents(true);
}

void APGQuestTriggerVolume::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
		Volume->OnComponentBeginOverlap.AddDynamic(this, &APGQuestTriggerVolume::OnVolumeBeginOverlap);
}

void APGQuestTriggerVolume::OnVolumeBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	NotifyPawnEntered(Cast<APawn>(OtherActor));
}

bool APGQuestTriggerVolume::NotifyPawnEntered(APawn* Pawn)
{
	if (!HasAuthority() || !IsValid(Pawn))
		return false;
	const FName Key = UPGItemReceiverLibrary::GetPlayerKey(Pawn);
	if (bOncePerPlayer && EnteredPlayerKeys.Contains(Key))
		return false;
	EnteredPlayerKeys.Add(Key);
	OnQuestEvent.Broadcast(QuestTag, this, Pawn);
	return true;
}
