#include "Objects/PGInteractionComponent.h"
#include "Finale/PGAnnounceSubsystem.h"
#include "Objects/PGInteractableActorBase.h"

#include "Camera/CameraComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/Interactable.h"
#include "Interaction/PGRideable.h"
#include "Objects/PGInteractableActorBase.h"
#include "Objects/PGLootableComponent.h"
#include "Objects/PGObjectTypes.h"

UPGInteractionComponent::UPGInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

void UPGInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn))
		return;
	if (!Pawn->IsLocallyControlled())
	{
		RefreshRiderPrompt(Pawn);
		return;
	}

	RefreshAccumulated += DeltaTime;
	if (RefreshAccumulated >= RefreshInterval)
	{
		RefreshAccumulated = 0.0f;
		RefreshTarget();
	}

	if (!bHolding)
		return;

	// 유지 중 대상이 바뀌거나 사라지면 취소한다. 상자 앞에서 F를 누른 채 돌아서면 열리지 않는다.
	if (!HoldTarget.IsValid() || HoldTarget.Get() != CurrentTarget)
	{
		EndInteract();
		return;
	}

	HoldElapsed += DeltaTime;
	OnHoldProgress.Broadcast(GetHoldProgress());
	if (HoldElapsed >= HoldRequired)
	{
		AActor* Target = HoldTarget.Get();
		bHolding = false;
		HoldElapsed = 0.0f;
		HoldTarget = nullptr;
		OnHoldProgress.Broadcast(0.0f);
		if (GetOwner()->HasAuthority())
			InteractWith(Target);
		else
			ServerInteract(Target);
	}
}

void UPGInteractionComponent::RefreshRiderPrompt(const APawn* Pawn)
{
	// 차·로봇에 타면 컨트롤러가 탈것으로 넘어가 이 폰은 조종을 놓는다. 예전에는 여기서 그냥 멈춰서,
	// 타기 직전에 보던 "차량 탑승"이 탄 내내 화면에 남았다.
	// 이제는 붙어 있는 탈것이 내가 조종하는 것이면 그 탈것의 탄 사람 문구(멈추면 "차량 하차")를, 아니면 아무것도 안 띄운다.
	AActor* Ride = Pawn->GetAttachParentActor();
	const APawn* RidePawn = Cast<APawn>(Ride);
	const IPGRideable* Rideable = Cast<IPGRideable>(Ride);
	const bool bRidingMine = IsValid(RidePawn) && RidePawn->IsLocallyControlled() && Rideable != nullptr;
	// 조종도 안 하고 탈것도 아니면(서버의 남의 폰 등) 보여 줄 게 없다. 이미 비어 있으면 알릴 것도 없다.
	if (!bRidingMine && !IsValid(CurrentTarget) && Candidates.Num() == 0)
		return;

	AActor* NewTarget = bRidingMine ? Ride : nullptr;
	const FText Prompt = bRidingMine ? Rideable->GetRiderPrompt() : FText::GetEmpty();
	if (bHolding)
		EndInteract();
	if (Candidates.Num() > 0)
	{
		Candidates.Reset();
		SelectedIndex = INDEX_NONE;
		PreferredTarget = nullptr;
		OnCandidatesChanged.Broadcast(TArray<AActor*>(), SelectedIndex);
	}
	if (NewTarget != CurrentTarget || !Prompt.EqualTo(CurrentPrompt))
	{
		CurrentTarget = NewTarget;
		CurrentPrompt = Prompt;
		OnTargetChanged.Broadcast(CurrentTarget, CurrentPrompt);
	}
}

bool UPGInteractionComponent::ResolveInteractable(const AActor* Target, const APawn* Pawn, FText& OutPrompt, float& OutHoldSeconds, bool& bOutCanInteract)
{
	OutPrompt = FText::GetEmpty();
	OutHoldSeconds = 0.0f;
	bOutCanInteract = false;
	if (!IsValid(Target))
		return false;

	if (Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
	{
		UObject* Mutable = const_cast<AActor*>(Target);
		OutPrompt = IInteractable::Execute_GetInteractionPrompt(Mutable);
		bOutCanInteract = IInteractable::Execute_CanInteract(Mutable, const_cast<APawn*>(Pawn));
		// 유지 시간도 약속(IInteractable)으로 묻는다 — 대상이 어떤 부모 클래스인지 모른다(블루프린트만으로 구현한 대상은 0).
		if (const IInteractable* Interactable = Cast<IInteractable>(Target))
			OutHoldSeconds = Interactable->GetHoldSeconds();
		return true;
	}

	// 시체처럼 IInteractable이 없는 액터는 루팅 컴포넌트로 대신 말한다.
	if (const UPGLootableComponent* Lootable = Target->FindComponentByClass<UPGLootableComponent>())
	{
		if (!Lootable->IsActivated())
			return false;
		OutPrompt = Lootable->GetPrompt();
		bOutCanInteract = Lootable->CanLoot(Pawn);
		return true;
	}
	return false;
}

void UPGInteractionComponent::GatherCandidates(const FVector& Start, const FVector& Direction, TArray<AActor*>& OutCandidates) const
{
	OutCandidates.Reset();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	UWorld* World = GetWorld();
	if (!IsValid(Pawn) || !IsValid(World))
		return;

	const FVector Dir = Direction.GetSafeNormal();
	const FVector End = Start + Dir * TraceDistance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGInteractionTrace), false, Pawn);
	FHitResult Hit;
	const bool bHit = World->SweepSingleByChannel(Hit, Start, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(TraceRadius), Params);

	FText Prompt;
	float HoldSeconds = 0.0f;
	bool bCan = false;
	// 0번: 시선이 직접 닿은 것. 휠을 안 돌리면 예전과 똑같이 이게 대상이다.
	AActor* Direct = bHit ? Hit.GetActor() : nullptr;
	if (IsValid(Direct) && ResolveInteractable(Direct, Pawn, Prompt, HoldSeconds, bCan))
		OutCandidates.Add(Direct);
	if (CandidateRadius <= 0.0f)
		return;

	// 내 몸 둘레도 훑는다. 시선이 조금 빗나가도 발치의 물건은 집힌다(사용자 9/20: "겹칠 쯤이면 그냥 먹어지게").
	if (BodyReachRadius > 0.0f)
	{
		TArray<FOverlapResult> Near;
		World->OverlapMultiByChannel(Near, Pawn->GetActorLocation(), FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(BodyReachRadius), Params);
		for (const FOverlapResult& Overlap : Near)
		{
			AActor* Actor = Overlap.GetActor();
			if (!IsValid(Actor) || OutCandidates.Contains(Actor))
				continue;
			if (ResolveInteractable(Actor, Pawn, Prompt, HoldSeconds, bCan))
				OutCandidates.Add(Actor);
		}
	}

	// 닿은 지점(허공이면 시선 끝) 둘레를 훑는다. 상자 위의 총, 뭉쳐 떨어진 아이템, 문 옆 스위치.
	const FVector Center = bHit ? Hit.ImpactPoint : End;
	TArray<FOverlapResult> Overlaps;
	World->OverlapMultiByChannel(Overlaps, Center, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(CandidateRadius), Params);
	TArray<AActor*> Around;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (!IsValid(Actor) || Actor == Direct || Around.Contains(Actor))
			continue;
		if (ResolveInteractable(Actor, Pawn, Prompt, HoldSeconds, bCan))
			Around.Add(Actor);
	}
	// 시선(직선)에서 가까운 순. 화면 가운데에 가까운 것이 먼저 온다.
	Around.Sort([&Start, &Dir](const AActor& A, const AActor& B)
	{
		return FMath::PointDistToLine(A.GetActorLocation(), Dir, Start) < FMath::PointDistToLine(B.GetActorLocation(), Dir, Start);
	});
	OutCandidates.Append(Around);
}

AActor* UPGInteractionComponent::RefreshTarget()
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn))
		return nullptr;
	// 카메라가 있으면 카메라 기준, 없으면 눈높이 기준으로 본다.
	FVector Start;
	FRotator ViewRotation;
	if (const UCameraComponent* Camera = Pawn->FindComponentByClass<UCameraComponent>())
	{
		Start = Camera->GetComponentLocation();
		ViewRotation = Camera->GetComponentRotation();
	}
	else
	{
		Pawn->GetActorEyesViewPoint(Start, ViewRotation);
	}
	// 3인칭이면 카메라가 몸 뒤 몇 m 에 있다. 거기서부터 TraceDistance 를 재면 발 앞의 아이템에도 안 닿는다.
	// 시선 방향은 그대로 두고 출발점만 몸 옆까지 당긴다(카메라→몸 거리를 시선에 투영한 만큼).
	const FVector ViewDir = ViewRotation.Vector();
	const float Ahead = FVector::DotProduct(Pawn->GetPawnViewLocation() - Start, ViewDir);
	if (Ahead > 0.0f)
		Start += ViewDir * Ahead;
	return RefreshTargetFromView(Start, ViewDir);
}

AActor* UPGInteractionComponent::RefreshTargetFromView(const FVector& Start, const FVector& Direction)
{
	TArray<AActor*> Found;
	GatherCandidates(Start, Direction, Found);

	// 휠로 골라 둔 대상이 아직 후보에 있으면 그걸 유지한다. 없어졌으면 잊고 0번(직접 닿은 것)으로 돌아간다.
	int32 NewIndex = Found.Num() > 0 ? 0 : INDEX_NONE;
	if (PreferredTarget.IsValid())
	{
		const int32 Kept = Found.IndexOfByKey(PreferredTarget.Get());
		if (Kept != INDEX_NONE)
			NewIndex = Kept;
		else
			PreferredTarget = nullptr;
	}

	bool bListChanged = Found.Num() != Candidates.Num() || NewIndex != SelectedIndex;
	for (int32 I = 0; !bListChanged && I < Found.Num(); ++I)
		bListChanged = Found[I] != Candidates[I];
	if (bListChanged)
	{
		Candidates.Reset();
		for (AActor* Actor : Found)
			Candidates.Add(Actor);
		SelectedIndex = NewIndex;
		OnCandidatesChanged.Broadcast(Found, SelectedIndex);
	}

	AActor* NewTarget = Found.IsValidIndex(NewIndex) ? Found[NewIndex] : nullptr;
	FText Prompt;
	float HoldSeconds = 0.0f;
	bool bCan = false;
	if (IsValid(NewTarget))
		ResolveInteractable(NewTarget, Cast<APawn>(GetOwner()), Prompt, HoldSeconds, bCan);
	if (NewTarget != CurrentTarget || !Prompt.EqualTo(CurrentPrompt))
	{
		CurrentTarget = NewTarget;
		CurrentPrompt = Prompt;
		OnTargetChanged.Broadcast(CurrentTarget, CurrentPrompt);
	}
	return CurrentTarget;
}

void UPGInteractionComponent::CycleTarget(int32 Direction)
{
	if (Candidates.Num() < 2 || Direction == 0)
		return;
	const int32 Count = Candidates.Num();
	const int32 Next = ((FMath::Max(SelectedIndex, 0) + (Direction > 0 ? 1 : -1)) % Count + Count) % Count;
	PreferredTarget = Candidates[Next];
	SelectedIndex = Next;
	OnCandidatesChanged.Broadcast(GetCandidates(), SelectedIndex);

	FText Prompt;
	float HoldSeconds = 0.0f;
	bool bCan = false;
	ResolveInteractable(Candidates[Next], Cast<APawn>(GetOwner()), Prompt, HoldSeconds, bCan);
	CurrentTarget = Candidates[Next];
	CurrentPrompt = Prompt;
	OnTargetChanged.Broadcast(CurrentTarget, CurrentPrompt);
}

TArray<AActor*> UPGInteractionComponent::GetCandidates() const
{
	TArray<AActor*> Out;
	for (const TObjectPtr<AActor>& Actor : Candidates)
		Out.Add(Actor);
	return Out;
}

FText UPGInteractionComponent::GetCandidatePrompt(int32 Index) const
{
	FText Prompt;
	float HoldSeconds = 0.0f;
	bool bCan = false;
	if (Candidates.IsValidIndex(Index))
		ResolveInteractable(Candidates[Index], Cast<APawn>(GetOwner()), Prompt, HoldSeconds, bCan);
	return Prompt;
}

void UPGInteractionComponent::BeginInteract()
{
	// F 가 두 경로로 들어올 수 있다(팀 입력 표의 NA_Interaction, 그리고 표에 F 가 없을 때를 대비한
	// ACustomPlayerController 의 예비 바인딩). 0.2초 안의 두 번째 호출은 같은 누름으로 본다 — 문이 열리자마자 닫히지 않게.
	const UWorld* World = GetWorld();
	const double Now = IsValid(World) ? World->GetTimeSeconds() : 0.0;
	if (Now - LastBeginInteractTime < 0.2)
		return;
	LastBeginInteractTime = Now;

	AActor* Target = RefreshTarget();
	if (!IsValid(Target))
	{
		// 아무것도 못 찾았을 때도 로그를 남긴다. "F 를 눌러도 반응이 없다"의 원인이
		// 키가 안 눌린 것인지 대상이 없는 것인지 구분해야 한다(9/20 PIE: 연료통을 못 주움).
		UE_LOG(LogPGObjects, Verbose, TEXT("%s: interact pressed, no target in reach"), *GetNameSafe(GetOwner()));
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	FText Prompt;
	float HoldSeconds = 0.0f;
	bool bCan = false;
	ResolveInteractable(Target, Pawn, Prompt, HoldSeconds, bCan);
	// 멀티(9/27): 클라이언트의 "못 한다" 는 믿지 않고 서버에 물어본다. 서버가 다시 검사하고(InteractWith), 안 되면 이유를 돌려준다.
	// 왜: 가방 내용(팀 인벤토리)은 서버에만 있고 클라이언트로 오지 않는다. 그래서 연료통을 들고 있어도 클라 쪽 검사(HasItem)는
	//   "연료통이 필요하다" 가 되어 요청을 아예 안 보냈다 — 두 번째 사람이 여고생에게 연료통을 못 건넸다(9/27 PIE).
	// 혼자 하는 판(서버 = 내 컴퓨터)은 예전 그대로: 여기서 거른다.
	if (!bCan && GetOwner()->HasAuthority())
		return;

	if (HoldSeconds <= 0.0f)
	{
		if (GetOwner()->HasAuthority())
			InteractWith(Target);
		else
			ServerInteract(Target);
		return;
	}

	// 유지형: 서버에 "쓰기 시작"을 알려 다른 사람이 못 끼어들게 하고, 로컬에서 시간을 잰다.
	bHolding = true;
	HoldElapsed = 0.0f;
	HoldRequired = HoldSeconds;
	HoldTarget = Target;
	if (GetOwner()->HasAuthority())
	{
		if (APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target))
			Base->TryBeginUse(Cast<APawn>(GetOwner()));
	}
	else
	{
		ServerBeginUse(Target);
	}
}

void UPGInteractionComponent::EndInteract()
{
	if (!bHolding)
		return;
	AActor* Target = HoldTarget.Get();
	bHolding = false;
	HoldElapsed = 0.0f;
	HoldTarget = nullptr;
	OnHoldProgress.Broadcast(0.0f);

	if (GetOwner()->HasAuthority())
	{
		if (APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target))
			Base->EndUse(Cast<APawn>(GetOwner()));
	}
	else
	{
		ServerEndUse(Target);
	}
}

bool UPGInteractionComponent::IsTargetInRange(const AActor* Target) const
{
	const AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !IsValid(Target))
		return false;
	// 가운데가 아니라 가장 가까운 충돌면까지 잰다(9/27 멀티). 헬기 출구(약 15m)는 꼬리·코 쪽에서 F 를 누르면 가운데까지 450cm 가
	//   넘어 서버가 거절했다 — 방장(서버=내 컴퓨터)은 이 검사를 안 거쳐서 혼자 할 때는 안 보였다.
	const FVector From = Owner->GetActorLocation();
	float DistSq = FVector::DistSquared(From, Target->GetActorLocation());
	const FBox Bounds = Target->GetComponentsBoundingBox(false);
	if (Bounds.IsValid)
		DistSq = FMath::Min(DistSq, static_cast<float>(Bounds.ComputeSquaredDistanceToPoint(From)));
	return DistSq <= FMath::Square(ServerAcceptDistance);
}

bool UPGInteractionComponent::InteractWith(AActor* Target)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn) || !Pawn->HasAuthority() || !IsValid(Target))
		return false;

	if (Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
	{
		if (!IInteractable::Execute_CanInteract(Target, Pawn))
		{
			// 막힌 이유를 화면과 로그에 남긴다. 전에는 조용히 돌아가서 "탈출구에서 F 를 눌러도 아무것도 안 된다" 로만 보였다
			//   (9/22 사용자 — 헬기 탈출구는 연료통이 있어야 한다는 걸 알 길이 없었다).
			APGInteractableActorBase* Blocked = Cast<APGInteractableActorBase>(Target);
			FText Reason = Blocked ? Blocked->GetBlockedReason(Pawn) : FText::GetEmpty();
			if (Reason.IsEmpty() && Blocked && Blocked->IsInUse() && Blocked->GetCurrentUser() != Pawn)
				Reason = NSLOCTEXT("PGObject", "InUse", "사용 중");
			// 멀티(9/27): 거절돼도 이 사람이 잡은 "사용 중" 잠금은 푼다 — 안 풀면 모두에게 "사용 중" 이 판 끝까지 남는다.
			if (Blocked)
				Blocked->EndUse(Pawn);
			UE_LOG(LogPGObjects, Display, TEXT("PGInteract: %s refused %s — %s"), *GetNameSafe(Pawn), *GetNameSafe(Target),
				Reason.IsEmpty() ? TEXT("(no reason given)") : *Reason.ToString());
			if (!Reason.IsEmpty() && Pawn->IsLocallyControlled())
			{
				if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(Pawn))
					Announcer->Announce({ Reason });
			}
			else if (!Reason.IsEmpty())
			{
				ClientInteractRefused(Reason); // 원격 클라이언트: 그 사람 화면에 띄운다
			}
			return false;
		}
		IInteractable::Execute_Interact(Target, Pawn);
		// 유지형이었으면 사용 중 잠금을 푼다. 즉시형은 TryBeginUse를 안 했으므로 EndUse가 무시된다.
		if (APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target))
			Base->EndUse(Pawn);
		return true;
	}
	if (UPGLootableComponent* Lootable = Target->FindComponentByClass<UPGLootableComponent>())
	{
		const bool bLooted = Lootable->Loot(Pawn);
		Lootable->EndUse(Pawn);
		return bLooted;
	}
	return false;
}

void UPGInteractionComponent::ReleaseUseAfterReject(AActor* Target)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target))
		Base->EndUse(Pawn);
	else if (UPGLootableComponent* Lootable = IsValid(Target) ? Target->FindComponentByClass<UPGLootableComponent>() : nullptr)
		Lootable->EndUse(Pawn);
}

void UPGInteractionComponent::ServerInteract_Implementation(AActor* Target)
{
	// 클라이언트가 보낸 대상은 믿지 않는다. 거리를 서버에서 다시 잰다.
	if (!IsTargetInRange(Target))
	{
		UE_LOG(LogPGObjects, Warning, TEXT("%s: ServerInteract rejected (out of range) target=%s"), *GetNameSafe(GetOwner()), *GetNameSafe(Target));
		ReleaseUseAfterReject(Target);
		ClientInteractRefused(NSLOCTEXT("PGObject", "TooFar", "너무 멉니다"));
		return;
	}
	// 누르고 있기(금고 5초 등)는 서버가 다시 잰다(9/27 멀티). 클라이언트가 "다 눌렀다" 고 보내도, 서버가 잠금을 준 시각부터
	//   정한 시간(80% — 네트워크 지연 여유)이 안 지났으면 받지 않는다. 전에는 클라에서 유지 시간이 0 으로 와서 금고가 바로 열렸다.
	if (const APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target); Base && Base->GetInteractSeconds() > 0.0f)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		const double Held = GetWorld()->GetTimeSeconds() - Base->GetUseStartedAt();
		if (Base->GetCurrentUser() != Pawn || Held < Base->GetInteractSeconds() * 0.8f)
		{
			UE_LOG(LogPGObjects, Display, TEXT("%s: ServerInteract rejected (held %.1fs of %.1fs, user=%s) target=%s"), *GetNameSafe(GetOwner()),
				Held, Base->GetInteractSeconds(), *GetNameSafe(Base->GetCurrentUser()), *GetNameSafe(Target));
			ReleaseUseAfterReject(Target);
			if (Base->GetCurrentUser() != nullptr && Base->GetCurrentUser() != Pawn)
				ClientInteractRefused(NSLOCTEXT("PGObject", "InUse", "사용 중"));
			return;
		}
	}
	InteractWith(Target);
}

void UPGInteractionComponent::ClientInteractRefused_Implementation(const FText& Reason)
{
	if (UPGAnnounceSubsystem* Announcer = UPGAnnounceSubsystem::Get(GetOwner()))
		Announcer->Announce({ Reason });
}

void UPGInteractionComponent::ServerBeginUse_Implementation(AActor* Target)
{
	if (!IsTargetInRange(Target))
		return;
	if (APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target))
		Base->TryBeginUse(Cast<APawn>(GetOwner()));
	else if (UPGLootableComponent* Lootable = IsValid(Target) ? Target->FindComponentByClass<UPGLootableComponent>() : nullptr)
		Lootable->TryBeginUse(Cast<APawn>(GetOwner()));
}

void UPGInteractionComponent::TakeItemFromTarget(int32 Index)
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn) || !IsValid(CurrentTarget))
		return;
	if (Pawn->HasAuthority())
		ServerTakeItem_Implementation(CurrentTarget, Index);
	else
		ServerTakeItem(CurrentTarget, Index);
}

void UPGInteractionComponent::ServerTakeItem_Implementation(AActor* Target, int32 Index)
{
	// 클라이언트가 보낸 대상·칸 번호는 믿지 않는다. 거리와 칸 유효성은 서버가 다시 본다.
	if (!IsTargetInRange(Target))
		return;
	if (UPGLootableComponent* Lootable = Target->FindComponentByClass<UPGLootableComponent>())
		Lootable->TakeItem(Cast<APawn>(GetOwner()), Index);
}

void UPGInteractionComponent::ServerEndUse_Implementation(AActor* Target)
{
	if (APGInteractableActorBase* Base = Cast<APGInteractableActorBase>(Target))
		Base->EndUse(Cast<APawn>(GetOwner()));
	else if (UPGLootableComponent* Lootable = IsValid(Target) ? Target->FindComponentByClass<UPGLootableComponent>() : nullptr)
		Lootable->EndUse(Cast<APawn>(GetOwner()));
}
