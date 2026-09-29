#include "Objects/PGExtractionZoneActor.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/StaticMesh.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGWearableColors.h"
#include "Flow/PGRunSubsystem.h"
#include "Common/PGPlayerMessageComponent.h"
#include "GameFramework/PlayerController.h"
#include "Interaction/PGRideable.h"
#include "Engine/OverlapResult.h"
#include "TimerManager.h"
#include "GameFramework/Character.h"
#include "EngineUtils.h"

APGExtractionZoneActor::APGExtractionZoneActor()
{
	Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
	Zone->SetupAttachment(RootScene);
	Zone->SetBoxExtent(FVector(200.0f, 200.0f, 150.0f));
	Zone->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Zone->SetCollisionResponseToAllChannels(ECR_Ignore);
	Zone->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	// 차·탱크도 잡는다. 차는 Vehicle 채널이라 Pawn 만 보던 예전에는 차를 타고 EXIT 를 지나가도 아무 일이 없었다(9/22).
	Zone->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);
	Zone->SetGenerateOverlapEvents(true);

	// 탈출 영역 자체는 막지 않는다. 헬기·배 같은 메시는 차량 담당의 액터가 따로 있다.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	DisplayName = NSLOCTEXT("Extraction", "DefaultName", "탈출구");
}

void APGExtractionZoneActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGExtractionZoneActor, UsesConsumed);
	DOREPLIFETIME(APGExtractionZoneActor, Trigger);
	DOREPLIFETIME(APGExtractionZoneActor, RequiredSeconds);
	DOREPLIFETIME(APGExtractionZoneActor, RequiredItemId);
}

void APGExtractionZoneActor::BeginPlay()
{
	Super::BeginPlay();
	SessionStartSeconds = IsValid(GetWorld()) ? GetWorld()->GetTimeSeconds() : 0.0;
	if (HasAuthority())
	{
		Zone->OnComponentBeginOverlap.AddDynamic(this, &APGExtractionZoneActor::OnZoneBeginOverlap);
		Zone->OnComponentEndOverlap.AddDynamic(this, &APGExtractionZoneActor::OnZoneEndOverlap);
		if (bPollOccupants)
			GetWorldTimerManager().SetTimer(PollTimer, this, &APGExtractionZoneActor::PollOccupants, 0.1f, true, 0.1f);
	}
}

APawn* APGExtractionZoneActor::ResolveCarrier(const APawn* Pawn)
{
	// 가방은 사람에게 있다. 차·로봇·탱크는 가방이 없으니 타고 있는 사람을 본다.
	if (!IsValid(Pawn))
		return nullptr;
	if (Pawn->GetClass()->ImplementsInterface(UPGItemReceiver::StaticClass()))
		return const_cast<APawn*>(Pawn);
	if (const IPGRideable* Rideable = Cast<IPGRideable>(Pawn))
		if (APawn* Rider = Rideable->GetRiderPawn(); IsValid(Rider))
			return Rider;
	return const_cast<APawn*>(Pawn);
}

void APGExtractionZoneActor::PollOccupants()
{
	// 겹침 이벤트를 믿지 않고 직접 묻는다. 왜: 차의 몸체(스켈레탈 메시)는 기본으로 겹침 이벤트를 안 내서
	// 차로 지나가면 BeginOverlap 이 안 온다. 상자 모양 그대로 Pawn·Vehicle 채널을 0.1초마다 물어본다(탈출구 몇 개뿐이라 싸다).
	UWorld* World = GetWorld();
	if (!World)
		return;
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_Pawn);
	Objects.AddObjectTypesToQuery(ECC_Vehicle);
	TArray<FOverlapResult> Hits;
	World->OverlapMultiByObjectType(Hits, Zone->GetComponentLocation(), Zone->GetComponentQuat(), Objects,
		FCollisionShape::MakeBox(Zone->GetScaledBoxExtent()), FCollisionQueryParams(SCENE_QUERY_STAT(PGExtractionPoll), false, this));

	// 차에 탄 사람과 차가 둘 다 잡히면 한 번만 센다(가방 주인 기준).
	TSet<APawn*> Carriers;
	TSet<TWeakObjectPtr<APawn>> Found;
	for (const FOverlapResult& Hit : Hits)
	{
		APawn* Pawn = Cast<APawn>(Hit.GetActor());
		if (!IsValid(Pawn) || Found.Contains(Pawn))
			continue;
		APawn* Carrier = ResolveCarrier(Pawn);
		bool bAlreadyCounted = false;
		Carriers.Add(Carrier, &bAlreadyCounted);
		if (bAlreadyCounted)
			continue;
		Found.Add(Pawn);
		// 움직여서 취소된 뒤 영역 안에서 다시 멈추면 카운트다운을 다시 시작한다(나갔다 들어올 필요 없게).
		if (bHoldStill && Trigger == EPGExtractionTrigger::Overlap && PawnsInside.Contains(Pawn) && !Progress.Contains(Pawn)
			&& Pawn->GetVelocity().SizeSquared() < FMath::Square(30.0f))
			StartPawn(Pawn);
		if (!PawnsInside.Contains(Pawn))
		{
			// 드나듦 기록. "지나갔는데 아무 일 없다" 를 다시 들으면 여기서 들어왔는지·연료통이 있었는지부터 본다.
			if (Pawn->IsPlayerControlled())
				UE_LOG(LogPGObjects, Display, TEXT("Extraction %s: %s entered (carrier %s, has %s=%d)"), *GetName(), *GetNameSafe(Pawn),
					*GetNameSafe(Carrier), *RequiredItemId.ToString(),
					RequiredItemId.IsNone() ? 1 : (UPGItemReceiverLibrary::HasItem(Carrier, RequiredItemId, 1) ? 1 : 0));
			PolledInside.Add(Pawn);
			NotifyPawnEntered(Pawn);
		}
	}
	// 이 검사로 들어온 폰만 이 검사로 내보낸다. 테스트가 NotifyPawnEntered 로 직접 넣은 폰은 건드리지 않는다.
	for (auto It = PolledInside.CreateIterator(); It; ++It)
	{
		if (!Found.Contains(*It))
		{
			if (APawn* Pawn = It->Get())
				NotifyPawnLeft(Pawn);
			It.RemoveCurrent();
		}
	}
}

void APGExtractionZoneActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	// 카탈로그의 InteractSeconds는 탈출구에서는 "서 있어야 하는 시간"으로 쓴다.
	RequiredSeconds = Row.InteractSeconds;
	RequiredItemId = Row.ItemId;
	InteractSeconds = 0.0f;

	// 차·헬기 탈출구(소켓 종류 Vehicle): F 로 타고 출발한다. 충돌·영역은 ApplyCatalogLook(클라이언트도 같은 모양이어야 한다).
	if (Row.SocketKind == EPGSpawnSocketKind::Vehicle)
		Trigger = EPGExtractionTrigger::Interact;
}

void APGExtractionZoneActor::ApplyCatalogLook()
{
	Super::ApplyCatalogLook();
	// 메시(차·헬기)는 실제로 사람을 막고, 영역은 메시 둘레 1.5m 로 넓힌다 — 차 옆에 서서 F 를 누르고 출발할 때까지 그 둘레에 있으면 된다.
	// 멀티(9/27): 클라이언트에도 같은 충돌 — 서버에만 있으면 클라에선 보이지 않는 벽이고 F 로 겨눌 수도 없었다.
	if (CatalogLook.SocketKind == EPGSpawnSocketKind::Vehicle)
	{
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		if (const UStaticMesh* VehicleMesh = MeshComponent->GetStaticMesh())
		{
			const FBox Bounds = VehicleMesh->GetBoundingBox();
			SetZoneBox(Bounds.GetCenter(), Bounds.GetExtent() + FVector(150.0f, 150.0f, 50.0f));
		}
	}
}

void APGExtractionZoneActor::SetZoneBox(const FVector& LocalCenter, const FVector& Extent)
{
	Zone->SetRelativeLocation(LocalCenter);
	Zone->SetBoxExtent(Extent.ComponentMax(FVector(50.0f)));
}

void APGExtractionZoneActor::Configure(EPGExtractionTrigger InTrigger, float InRequiredSeconds, FName InRequiredItemId, bool bInConsume, int32 InMaxUses)
{
	Trigger = InTrigger;
	RequiredSeconds = FMath::Max(0.0f, InRequiredSeconds);
	RequiredItemId = InRequiredItemId;
	bConsumeRequiredItem = bInConsume;
	MaxUses = FMath::Max(0, InMaxUses);
}

float APGExtractionZoneActor::GetSessionSeconds() const
{
	return IsValid(GetWorld()) ? static_cast<float>(GetWorld()->GetTimeSeconds() - SessionStartSeconds) : 0.0f;
}

bool APGExtractionZoneActor::IsActiveNow() const
{
	const float Now = GetSessionSeconds();
	if (ActiveFromSeconds >= 0.0f && Now < ActiveFromSeconds)
		return false;
	if (ActiveUntilSeconds >= 0.0f && Now > ActiveUntilSeconds)
		return false;
	if (MaxUses > 0 && UsesConsumed >= MaxUses)
		return false;
	return true;
}

bool APGExtractionZoneActor::IsAvailableFor(const APawn* Pawn, FText& OutReason) const
{
	if (!IsValid(Pawn))
		return false;
	if (MaxUses > 0 && UsesConsumed >= MaxUses)
	{
		OutReason = NSLOCTEXT("Extraction", "Closed", "닫힌 탈출구");
		return false;
	}
	if (!IsActiveNow())
	{
		OutReason = NSLOCTEXT("Extraction", "Inactive", "지금은 사용 불가");
		return false;
	}
	// 차에 탄 채 들어왔으면 운전자의 가방을 본다.
	const APawn* Carrier = ResolveCarrier(Pawn);
	if (AllowedPlayerKeys.Num() > 0 && !AllowedPlayerKeys.Contains(UPGItemReceiverLibrary::GetPlayerKey(Carrier)))
	{
		OutReason = NSLOCTEXT("Extraction", "NotYours", "내 탈출구가 아닙니다");
		return false;
	}
	if (!RequiredItemId.IsNone() && !UPGItemReceiverLibrary::HasItem(Carrier, RequiredItemId, 1))
	{
		OutReason = FText::Format(NSLOCTEXT("Extraction", "NeedItem", "{0} 필요"), UPGWearableColorLibrary::GetItemDisplayName(RequiredItemId));
		return false;
	}
	return true;
}

bool APGExtractionZoneActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (Trigger != EPGExtractionTrigger::Interact)
	{
		OutReason = NSLOCTEXT("Extraction", "StandHere", "영역 안에 서 있으세요");
		return false;
	}
	return IsAvailableFor(Interactor, OutReason);
}

void APGExtractionZoneActor::HandleInteract(APawn* Interactor)
{
	// F로 시작하는 탈출구(헬기·선박). 시작만 하고 진행은 Tick이 이어 간다.
	StartPawn(Interactor);
}

FText APGExtractionZoneActor::GetPromptInternal() const
{
	// 필요한 아이템은 문구에 같이 적는다. 문구 함수는 누가 보는지 모르므로(폰 인자 없음) 가졌는지와 상관없이 항상 적는다.
	// 안 적으면 연료 없이 F 를 눌러도 아무 일이 없어서 왜 안 되는지 알 수 없었다.
	const FText Need = RequiredItemId.IsNone() ? FText::GetEmpty()
		: FText::Format(NSLOCTEXT("Extraction", "NeedSuffix", " ({0} 필요)"), UPGWearableColorLibrary::GetItemDisplayName(RequiredItemId));
	if (Trigger == EPGExtractionTrigger::Interact)
		return FText::Format(NSLOCTEXT("Extraction", "Board", "{0} 탑승·출발{1}"), DisplayName, Need);
	if (!Need.IsEmpty())
		return FText::Format(NSLOCTEXT("Extraction", "WaitNeed", "{0}{1}"), DisplayName, Need);
	if (RequiredSeconds > 0.0f)
		return FText::Format(NSLOCTEXT("Extraction", "Wait", "{0} ({1}초 대기)"), DisplayName, FText::AsNumber(RequiredSeconds));
	return DisplayName;
}

void APGExtractionZoneActor::OnZoneBeginOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	NotifyPawnEntered(Cast<APawn>(OtherActor));
}

void APGExtractionZoneActor::OnZoneEndOverlap(UPrimitiveComponent*, AActor* OtherActor, UPrimitiveComponent*, int32)
{
	// 차는 부품(바퀴·몸체)마다 따로 겹친다. 한 부품이 빠져도 다른 부품이 아직 안에 있으면 나간 게 아니다.
	if (IsValid(OtherActor) && Zone->IsOverlappingActor(OtherActor))
		return;
	NotifyPawnLeft(Cast<APawn>(OtherActor));
}

void APGExtractionZoneActor::NotifyPawnEntered(APawn* Pawn)
{
	if (!HasAuthority() || !IsValid(Pawn))
		return;
	PawnsInside.Add(Pawn);
	// 서 있으면 진행되는 방식은 들어오는 순간 시작한다. F 방식은 HandleInteract가 시작한다.
	if (Trigger == EPGExtractionTrigger::Overlap)
	{
		// 못 쓰면 왜 못 쓰는지 화면에 알린다. 예전에는 연료 없이 EXIT 를 지나가면 아무 일도 없어서 고장으로 보였다(9/22).
		// 플레이어가 조종하는 폰만, 4초에 한 번만 — 문 앞에서 서성이면 같은 줄이 계속 쌓인다.
		FText Reason;
		APlayerController* Player = UPGPlayerMessageComponent::ResolvePlayer(Pawn);
		if (!IsAvailableFor(Pawn, Reason) && IsValid(Player) && !Reason.IsEmpty())
		{
			const double Now = GetWorld()->GetTimeSeconds();
			double& LastAt = LastRefusalAnnounceAt.FindOrAdd(Pawn, -100.0);
			if (Now - LastAt > 4.0)
			{
				LastAt = Now;
				UPGPlayerMessageComponent::AnnounceTo(Player, { FText::Format(NSLOCTEXT("Extraction", "RefusedAt", "{0}: {1}"), DisplayName, Reason) });
				UE_LOG(LogPGObjects, Display, TEXT("Extraction %s: %s refused - %s"), *GetName(), *GetNameSafe(Pawn), *Reason.ToString());
			}
		}
		// 버티기 방식은 멈춘 뒤에 시작한다. 달려 들어오는 순간 시작하면 바로 "움직여서 취소" 가 떴다. 멈추면 PollOccupants 가 시작한다.
		if (!bHoldStill || Pawn->GetVelocity().SizeSquared() < FMath::Square(30.0f))
			StartPawn(Pawn);
	}
}

void APGExtractionZoneActor::NotifyPawnLeft(APawn* Pawn)
{
	if (!HasAuthority() || !IsValid(Pawn))
		return;
	PawnsInside.Remove(Pawn);
	// 영역을 벗어나면 진행이 취소된다. 헬기도 타기 전에 내리면 취소다.
	CancelPawn(Pawn);
}

void APGExtractionZoneActor::StartPawn(APawn* Pawn)
{
	FText Reason;
	if (!IsAvailableFor(Pawn, Reason))
		return;
	if (Progress.Contains(Pawn))
		return;
	// 이 사람이 벌써 다른 탈출구에서 초를 세고 있으면 새로 시작하지 않는다(먼저 시작한 쪽 하나만).
	// 왜(9/28 사용자 PIE: "헬기 탈출까지 5,4,3,2,1 순서대로 나오면 되는데 그 사이에 자꾸 글자가 잠깐잠깐 뜬다"):
	//   헬기 출구는 검문소 영역(검문소 출구, 들어가서 멈추면 시작) 안에 헬기(F 로 시작)가 서 있다. 둘 다 연료통 + 5초라
	//   영역 안에서 F 를 누르면 두 곳이 같이 세면서 "검문소 출구 탈출까지 N" 과 "헬기 탈출까지 N" 을 번갈아 띄웠다.
	//   탈출구는 맵에 열 개 남짓이라 시작할 때 한 번 훑는 값은 무시할 만하다.
	for (TActorIterator<APGExtractionZoneActor> It(GetWorld()); It; ++It)
	{
		if (*It != this && It->Progress.Contains(Pawn))
		{
			UE_LOG(LogPGObjects, Display, TEXT("Extraction %s: %s is already counting down at %s — not starting a second one"),
				*GetName(), *GetNameSafe(Pawn), *It->GetName());
			return;
		}
	}

	Progress.Add(Pawn, 0.0f);
	OnExtractionStarted.Broadcast(this, Pawn);

	if (RequiredSeconds <= 0.0f)
		CompletePawn(Pawn);
	else
	{
		if (bHoldStill)
		{
			HoldStartSpot.Add(Pawn, Pawn->GetActorLocation());
			LastShownSecond.Remove(Pawn);
			ShowCountdown(Pawn, RequiredSeconds);
			UE_LOG(LogPGObjects, Display, TEXT("Extraction %s: %s holding still for %.0fs"), *GetName(), *GetNameSafe(Pawn), RequiredSeconds);
		}
		SetActorTickEnabled(true);
	}
}

void APGExtractionZoneActor::CancelPawn(APawn* Pawn)
{
	if (Progress.Remove(Pawn) > 0)
	{
		OnExtractionCancelled.Broadcast(this, Pawn);
		if (bHoldStill)
			ClearCountdown(Pawn, FText::GetEmpty());
	}
	HoldStartSpot.Remove(Pawn);
	if (Progress.Num() == 0)
		SetActorTickEnabled(false);
}

void APGExtractionZoneActor::ShowCountdown(APawn* Pawn, float Remaining)
{
	// 그 사람 화면에(혼자 하는 판·듣기 서버 방장은 바로, 원격 클라이언트는 Client RPC). 플레이어가 조종하는 폰이거나 그 사람이 탄 탈것.
	APlayerController* Player = UPGPlayerMessageComponent::ResolvePlayer(Pawn);
	if (!IsValid(Player))
		Player = UPGPlayerMessageComponent::ResolvePlayer(ResolveCarrier(Pawn));
	if (!IsValid(Player))
		return;
	const int32 Second = FMath::Max(1, FMath::CeilToInt(Remaining));
	int32& Shown = LastShownSecond.FindOrAdd(Pawn, -1);
	if (Second == Shown)
		return;
	Shown = Second;
	UPGPlayerMessageComponent::CountdownTo(Player, FText::Format(NSLOCTEXT("Extraction", "Countdown", "{0} 탈출까지 {1}\n움직이면 취소됩니다"),
		DisplayName, FText::AsNumber(Second)));
}

void APGExtractionZoneActor::ClearCountdown(APawn* Pawn, const FText& Why)
{
	LastShownSecond.Remove(Pawn);
	APlayerController* Player = UPGPlayerMessageComponent::ResolvePlayer(Pawn);
	if (!IsValid(Player))
		Player = UPGPlayerMessageComponent::ResolvePlayer(ResolveCarrier(Pawn));
	if (!IsValid(Player))
		return;
	// 취소 이유("움직여서 취소")는 카운트다운 자리에 바로 띄우고, 다시 멈추면 바로 지운다.
	// 예전에는 흐리게 나타났다 사라지는 안내 줄로 띄워서, 이미 멈췄는데도 몇 초 동안 "취소됐다" 가 남아 있었다(9/22 사용자).
	if (!Why.IsEmpty())
	{
		UPGPlayerMessageComponent::CountdownTo(Player, Why);
		CancelNoticePawns.Add(Pawn);
		GetWorldTimerManager().SetTimer(CancelNoticeTimer, this, &APGExtractionZoneActor::TickCancelNotice, 0.1f, true);
		return;
	}
	UPGPlayerMessageComponent::CountdownTo(Player, FText::GetEmpty());
}

void APGExtractionZoneActor::TickCancelNotice()
{
	// 사람마다: 멈췄거나(초속 20cm 미만) 폰이 없어졌으면 그 사람 문구를 지운다. 그사이 카운트다운이 다시 시작됐으면 그 숫자가 이미 자리를 덮었으니 건드리지 않는다.
	for (auto It = CancelNoticePawns.CreateIterator(); It; ++It)
	{
		APawn* Pawn = It->Get();
		if (IsValid(Pawn) && Pawn->GetVelocity().SizeSquared() >= FMath::Square(20.0f))
			continue;
		It.RemoveCurrent();
		if (!IsValid(Pawn) || Progress.Contains(Pawn))
			continue;
		APlayerController* Player = UPGPlayerMessageComponent::ResolvePlayer(Pawn);
		if (!IsValid(Player))
			Player = UPGPlayerMessageComponent::ResolvePlayer(ResolveCarrier(Pawn));
		UPGPlayerMessageComponent::CountdownTo(Player, FText::GetEmpty());
	}
	if (CancelNoticePawns.IsEmpty())
		GetWorldTimerManager().ClearTimer(CancelNoticeTimer);
}

void APGExtractionZoneActor::CompletePawn(APawn* Pawn)
{
	// 완료 직전에 다시 확인한다. 대기 중 열쇠를 버렸거나 시간이 지났을 수 있다.
	FText Reason;
	if (!IsAvailableFor(Pawn, Reason))
	{
		CancelPawn(Pawn);
		return;
	}
	if (!RequiredItemId.IsNone() && bConsumeRequiredItem && !UPGItemReceiverLibrary::ConsumeItem(ResolveCarrier(Pawn), RequiredItemId, 1))
	{
		CancelPawn(Pawn);
		return;
	}

	Progress.Remove(Pawn);
	HoldStartSpot.Remove(Pawn);
	if (bHoldStill)
		ClearCountdown(Pawn, FText::GetEmpty());
	++UsesConsumed;
	UE_LOG(LogPGObjects, Display, TEXT("Extraction %s (%s): %s completed uses=%d/%d"),
		*GetName(), *ObjectId.ToString(), *GetNameSafe(Pawn), UsesConsumed, MaxUses);
	OnExtractionCompleted.Broadcast(this, Pawn);
	// 판 기록(탈출 → 스코어보드). 플레이어 폰이 아니거나(스모크 테스트 폰) 판이 없으면 아무 일도 안 한다.
	UPGRunSubsystem::NotifyExtraction(Pawn, DisplayName);

	if (Progress.Num() == 0)
		SetActorTickEnabled(false);
}

bool APGExtractionZoneActor::AdvancePawn(APawn* Pawn, float DeltaSeconds)
{
	if (!HasAuthority() || !IsValid(Pawn))
		return false;
	float* Value = Progress.Find(Pawn);
	if (!Value)
		return false;

	// 가만히 버티기: 시작한 자리에서 1m(탈것은 2m) 넘게 벗어나면 취소. 숨 쉬는 몸짓·서 있는 차의 흔들림은 넘지 않는다.
	if (bHoldStill)
	{
		const FVector* Start = HoldStartSpot.Find(Pawn);
		const float Allowed = Pawn->IsA<ACharacter>() ? 100.0f : 200.0f;
		if (Start && (FVector::DistSquared2D(*Start, Pawn->GetActorLocation()) > FMath::Square(Allowed)
			|| FMath::Abs(Start->Z - Pawn->GetActorLocation().Z) > 150.0f))
		{
			UE_LOG(LogPGObjects, Display, TEXT("Extraction %s: %s moved — cancelled"), *GetName(), *GetNameSafe(Pawn));
			Progress.Remove(Pawn);
			HoldStartSpot.Remove(Pawn);
			OnExtractionCancelled.Broadcast(this, Pawn);
			ClearCountdown(Pawn, NSLOCTEXT("Extraction", "MovedCancel", "움직여서 탈출이 취소됐습니다"));
			return false;
		}
	}
	*Value += DeltaSeconds;
	const float Ratio = RequiredSeconds > 0.0f ? FMath::Clamp(*Value / RequiredSeconds, 0.0f, 1.0f) : 1.0f;
	if (bHoldStill && Ratio < 1.0f)
		ShowCountdown(Pawn, RequiredSeconds - *Value);
	OnExtractionProgress.Broadcast(this, Pawn, Ratio);

	if (Ratio >= 1.0f)
	{
		CompletePawn(Pawn);
		return true;
	}
	return false;
}

float APGExtractionZoneActor::GetProgress(const APawn* Pawn) const
{
	const float* Value = Progress.Find(const_cast<APawn*>(Pawn));
	if (!Value || RequiredSeconds <= 0.0f)
		return 0.0f;
	return FMath::Clamp(*Value / RequiredSeconds, 0.0f, 1.0f);
}

void APGExtractionZoneActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (!HasAuthority())
		return;

	TArray<TWeakObjectPtr<APawn>> Keys;
	Progress.GetKeys(Keys);
	for (const TWeakObjectPtr<APawn>& Key : Keys)
	{
		APawn* Pawn = Key.Get();
		if (!IsValid(Pawn))
		{
			Progress.Remove(Key);
			continue;
		}
		AdvancePawn(Pawn, DeltaTime);
	}
	if (Progress.Num() == 0)
		SetActorTickEnabled(false);
}
