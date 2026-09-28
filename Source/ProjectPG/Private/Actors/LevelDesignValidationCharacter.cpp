// ProjectPG-only walking pawn used to validate generated level design.

#include "Actors/LevelDesignValidationCharacter.h"

#include "AIController.h"
#include "Camera/CameraComponent.h"
#include "Flow/PGRunSubsystem.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Blueprint/UserWidget.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGWearableComponent.h"
#include "UI/PGInteractionPromptWidget.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "InputCoreTypes.h"
#include "NavigationInvokerComponent.h"
#include "Objects/PGInteractionComponent.h"
#include "Common/PGKeyPolling.h"
#include "Objects/PGLootableComponent.h"
#include "Weapons/PGWeaponComponent.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

// PG.DebugHud 1/0 - 팀 인벤토리 UI 가 나오기 전까지 체력·무기·주운 것을 화면 구석에 글자로 보여준다.
static TAutoConsoleVariable<int32> CVarPGDebugHud(TEXT("PG.DebugHud"), 1, TEXT("Show health / weapon / inventory text on screen for the validation character."));
#include "UObject/ConstructorHelpers.h"

ALevelDesignValidationCharacter::ALevelDesignValidationCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;

	GetCapsuleComponent()->InitCapsuleSize(42.0f, 88.0f);
	GetCharacterMovement()->MaxWalkSpeed = 500.0f;
	GetCharacterMovement()->BrakingDecelerationWalking = 1800.0f;
	GetCharacterMovement()->bOrientRotationToMovement = true;
	bUseControllerRotationYaw = false;

	AIControllerClass = AAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	// 몸은 팀 플레이어 캐릭터와 같은 Quantum 모듈 캐릭터. 본체 메시(머리)·팔·옷은 UPGWearableComponent 가 BeginPlay 에서 조립한다.
	// 여기서는 자리만 잡는다: 캡슐 중심이 원점이라 발밑까지 내리고, 메시가 +Y 를 보고 있어 Yaw 를 돌린다(팩 데모 블루프린트와 같은 값).
	GetMesh()->SetRelativeLocation(FVector(0.0f, 0.0f, -88.0f));
	GetMesh()->SetRelativeRotation(FRotator(0.0f, -90.0f, 0.0f));
	// 애님 블루프린트 없이 PlayAnimation 으로 루프를 돌린다.
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationSingleNode);
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetMesh()->SetCanEverAffectNavigation(false);

	// 예전 큐브 표시용. 스켈레탈 메시로 대체했으므로 숨긴다(참조는 남겨 둔다).
	BodyVisual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyVisual"));
	BodyVisual->SetupAttachment(GetCapsuleComponent());
	BodyVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyVisual->SetCanEverAffectNavigation(false);
	BodyVisual->SetVisibility(false);

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(GetCapsuleComponent());
	// 3인칭이 기본: 옷을 갈아입는 게 보여야 한다. 어깨 너머로 살짝 비껴서 조준점이 몸에 안 가린다. V 키로 1인칭.
	CameraArm->TargetArmLength = 300.0f;
	CameraArm->SocketOffset = FVector(0.0f, 55.0f, 20.0f);
	CameraArm->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f));
	CameraArm->bUsePawnControlRotation = true;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->bUsePawnControlRotation = false;

	NavigationInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavigationInvoker"));
	NavigationInvoker->SetGenerationRadii(12000.0f, 15000.0f);

	Interaction = CreateDefaultSubobject<UPGInteractionComponent>(TEXT("Interaction"));
	Weapon = CreateDefaultSubobject<UPGWeaponComponent>(TEXT("Weapon"));
	Weapon->bFirstPersonView = false;
	Wearable = CreateDefaultSubobject<UPGWearableComponent>(TEXT("Wearable"));
	Health = MaxHealth;

	// 이동 동작 칸 기본값(9/23 블루프린트 분리 전 BeginPlay 에 적혀 있던 Quantum 데모 동작).
	const FString Anim = TEXT("/Game/QuantumCharacter/Demo/Animations/");
	IdleAnimAsset = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Anim + TEXT("A_MM_Idle.A_MM_Idle")));
	WalkAnimAsset = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Anim + TEXT("A_MM_Walk_Fwd.A_MM_Walk_Fwd")));
	RunAnimAsset = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Anim + TEXT("A_MM_Run_Fwd.A_MM_Run_Fwd")));
	FallAnimAsset = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(Anim + TEXT("A_MM_Fall_Loop.A_MM_Fall_Loop")));
}

void ALevelDesignValidationCharacter::BeginPlay()
{
	Super::BeginPlay();
	auto LoadAnim = [](const TSoftObjectPtr<UAnimSequence>& Slot) { return Slot.IsNull() ? nullptr : Slot.LoadSynchronous(); };
	AnimIdle = LoadAnim(IdleAnimAsset);
	AnimWalk = LoadAnim(WalkAnimAsset);
	AnimRun = LoadAnim(RunAnimAsset);
	AnimFall = LoadAnim(FallAnimAsset);
	SetFirstPerson(false);
	if (HasAuthority() && IsValid(Weapon))
		Weapon->OnEquipped.AddDynamic(this, &ALevelDesignValidationCharacter::HandleWeaponEquipped);
}

void ALevelDesignValidationCharacter::HandleWeaponEquipped(UPGWeaponComponent* InWeapon, FName ItemId)
{
	UpdateHolsteredPistol();
}

void ALevelDesignValidationCharacter::UpdateHolsteredPistol()
{
	if (!HasAuthority() || !IsValid(Weapon) || !IsValid(Wearable))
		return;
	static const FName Pistol(TEXT("Pistol"));
	Wearable->SetHolsteredPistolVisible(Weapon->GetOwnedWeapons().Contains(Pistol) && Weapon->GetEquippedItemId() != Pistol);
}

void ALevelDesignValidationCharacter::SetFirstPerson(bool bInFirstPerson)
{
	bFirstPerson = bInFirstPerson;
	CameraArm->TargetArmLength = bFirstPerson ? 0.0f : 300.0f;
	CameraArm->SocketOffset = bFirstPerson ? FVector::ZeroVector : FVector(0.0f, 55.0f, 20.0f);
	if (IsValid(Wearable))
		Wearable->SetOwnerNoSee(bFirstPerson); // 본체 + 파츠 전부
	if (IsValid(Weapon))
		Weapon->SetFirstPersonView(bFirstPerson);
}

void ALevelDesignValidationCharacter::UpdateLocomotionAnim()
{
	const float Speed = GetVelocity().Size2D();
	UAnimSequence* Wanted = GetCharacterMovement()->IsFalling() ? AnimFall : (Speed > 320.0f ? AnimRun : (Speed > 15.0f ? AnimWalk : AnimIdle));
	if (!Wanted || Wanted == CurrentAnim)
		return;
	CurrentAnim = Wanted;
	GetMesh()->PlayAnimation(Wanted, true); // 파츠들은 LeaderPose 로 따라온다
}

void ALevelDesignValidationCharacter::EnsurePromptWidget(APlayerController* PlayerController)
{
	if (IsValid(PromptWidget) || !IsValid(PlayerController))
		return;
	PromptWidget = UPGInteractionPromptWidget::Create(PlayerController);
	if (!IsValid(PromptWidget))
		return;
	PromptWidget->AddToViewport();
	PromptWidget->BindTo(Interaction);
}

bool ALevelDesignValidationCharacter::ReceiveItem_Implementation(FName ItemId, int32 Count)
{
	if (ItemId.IsNone() || Count <= 0)
		return false;
	// 옷이면 주운 즉시 입는다(배그식). 그 자리에 입고 있던 것은 발 앞에 떨어진다. 같은 옷을 또 주우면 입지 않고 아래 인벤토리로 간다.
	FName Previous;
	if (IsValid(Wearable) && Wearable->Equip(ItemId, Previous))
	{
		if (!Previous.IsNone())
			APGFloorItemActor::SpawnDrop(this, Previous, 1, FTransform(FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f),
				GetActorLocation() + GetActorForwardVector() * 70.0f - FVector(0.0f, 0.0f, GetSimpleCollisionHalfHeight())));
		if (--Count <= 0)
			return true;
	}
	DebugInventory.FindOrAdd(ItemId) += Count;
	UE_LOG(LogTemp, Display, TEXT("ValidationCharacter inventory: +%d %s (now %d)"), Count, *ItemId.ToString(), DebugInventory[ItemId]);
	// 무기면 무기 컴포넌트가 목록에 넣고, 빈손이면 바로 든다.
	if (IsValid(Weapon))
		Weapon->NotifyItemReceived(ItemId);
	UpdateHolsteredPistol(); // 권총을 주웠는데 다른 걸 들고 있으면 권총집에 꽂힌다
	return true;
}

bool ALevelDesignValidationCharacter::HasItem_Implementation(FName ItemId, int32 Count) const
{
	const int32* Found = DebugInventory.Find(ItemId);
	return Found && *Found >= Count;
}

bool ALevelDesignValidationCharacter::ConsumeItem_Implementation(FName ItemId, int32 Count)
{
	int32* Found = DebugInventory.Find(ItemId);
	if (!Found || *Found < Count)
		return false;
	*Found -= Count;
	if (*Found <= 0)
		DebugInventory.Remove(ItemId);
	return true;
}

FName ALevelDesignValidationCharacter::GetPlayerKey_Implementation() const
{
	return GetFName();
}

int32 ALevelDesignValidationCharacter::GetDebugItemCount(FName ItemId) const
{
	const int32* Found = DebugInventory.Find(ItemId);
	return Found ? *Found : 0;
}

void ALevelDesignValidationCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateLocomotionAnim(); // 남이 봐도 움직여야 하니 로컬 여부와 상관없이

	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!IsValid(PlayerController) || !IsLocallyControlled())
		return;
	EnsurePromptWidget(PlayerController);
	if (PGKeyPolling::WasPressed(PlayerController, EKeys::V, bViewKeyWasDown))
		SetFirstPerson(!bFirstPerson);

	// 키를 직접 읽는 임시 입력(PGKeyPolling). 팀 캐릭터는 Enhanced Input 으로 같은 함수들을 부르면 된다.
	PGKeyPolling::ApplyWasdMovement(PlayerController, this);
	PGKeyPolling::ApplyMouseLook(PlayerController, this);
	PollInteraction(PlayerController);
	PollLootSlots(PlayerController);
	PollWeapon(PlayerController);
	DrawDebugHud();
}

// F: 상호작용. 눌리는 순간 시작, 떼는 순간 취소(유지형).
void ALevelDesignValidationCharacter::PollInteraction(APlayerController* PlayerController)
{
	const bool bWasDown = bInteractKeyWasDown;
	const bool bDown = PlayerController->IsInputKeyDown(EKeys::F);
	if (bDown && !bWasDown)
		Interaction->BeginInteract();
	else if (!bDown && bWasDown)
		Interaction->EndInteract();
	bInteractKeyWasDown = bDown;

	// 겹친 대상 고르기: 마우스 휠. 휠은 "눌려 있는" 상태가 없어서 이번 프레임에 돌았는지로 읽는다.
	if (PlayerController->WasInputKeyJustPressed(EKeys::MouseScrollUp))
		Interaction->CycleTarget(-1);
	else if (PlayerController->WasInputKeyJustPressed(EKeys::MouseScrollDown))
		Interaction->CycleTarget(+1);
}

// 숫자키 1~9: 보고 있는 상자·시체의 그 칸 하나만 집기. 루팅 UI 가 생기기 전까지의 대용.
void ALevelDesignValidationCharacter::PollLootSlots(APlayerController* PlayerController)
{
	static const FKey DigitKeys[9] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	const UPGLootableComponent* Storage = GetLookedAtStorage();
	for (int32 Slot = 0; Slot < 9; ++Slot)
	{
		if (PGKeyPolling::WasPressed(PlayerController, DigitKeys[Slot], bTakeKeyWasDown[Slot]) && IsValid(Storage))
			Interaction->TakeItemFromTarget(Slot);
	}
}

// 마우스 왼쪽: 공격(누르는 순간 한 번). Q: 주운 무기 바꾸기.
void ALevelDesignValidationCharacter::PollWeapon(APlayerController* PlayerController)
{
	if (!IsValid(Weapon))
		return;
	if (PGKeyPolling::WasPressed(PlayerController, EKeys::LeftMouseButton, bAttackKeyWasDown))
		Weapon->Attack();
	if (PGKeyPolling::WasPressed(PlayerController, EKeys::Q, bCycleWeaponKeyWasDown))
		Weapon->CycleNext();
}

// 지금 보고 있는 대상이 열린 상자·시체면 그 내용물 칸. 아니면 nullptr.
const UPGLootableComponent* ALevelDesignValidationCharacter::GetLookedAtStorage() const
{
	const AActor* Target = Interaction->GetCurrentTarget();
	const UPGLootableComponent* Storage = IsValid(Target) ? Target->FindComponentByClass<UPGLootableComponent>() : nullptr;
	return (IsValid(Storage) && Storage->IsActivated()) ? Storage : nullptr;
}

// 임시 HUD. 진짜 UI 는 팀원이 만드는 중이라, 시스템 검증용으로 글자만 띄운다. 키 번호로 같은 줄을 덮어써 깜빡이지 않는다.
void ALevelDesignValidationCharacter::DrawDebugHud() const
{
	if (!GEngine || CVarPGDebugHud.GetValueOnGameThread() == 0)
		return;
	GEngine->AddOnScreenDebugMessage(9001, 0.0f, FColor::White, FString::Printf(TEXT("HP %.0f / %.0f"), Health, MaxHealth));
	const FName Equipped = IsValid(Weapon) ? Weapon->GetEquippedItemId() : NAME_None;
	GEngine->AddOnScreenDebugMessage(9002, 0.0f, FColor::Yellow, FString::Printf(TEXT("WEAPON %s   (Q switch / LMB attack / F interact)"), Equipped.IsNone() ? TEXT("-") : *Equipped.ToString()));

	FString Items;
	int32 Shown = 0;
	for (const TPair<FName, int32>& Pair : DebugInventory)
	{
		Items += FString::Printf(TEXT("%s x%d   "), *Pair.Key.ToString(), Pair.Value);
		if (++Shown >= 10) { Items += TEXT("..."); break; }
	}
	GEngine->AddOnScreenDebugMessage(9003, 0.0f, FColor::Green, FString::Printf(TEXT("INVENTORY (%d): %s"), DebugInventory.Num(), Items.IsEmpty() ? TEXT("empty") : *Items));

	FString Slots;
	if (const UPGLootableComponent* Storage = GetLookedAtStorage())
	{
		const TArray<FPGItemStack>& Contents = Storage->GetContents();
		for (int32 Slot = 0; Slot < Contents.Num() && Slot < 9; ++Slot)
			Slots += FString::Printf(TEXT("[%d] %s x%d   "), Slot + 1, *Contents[Slot].ItemId.ToString(), Contents[Slot].Count);
		if (Slots.IsEmpty())
			Slots = TEXT("empty");
	}
	else
	{
		Slots = IsValid(Interaction->GetCurrentTarget()) ? Interaction->GetCurrentPrompt().ToString() : TEXT("-");
	}
	// 겹친 대상이 있으면 몇 번째를 보고 있는지 알려 준다(임시 HUD. 진짜 목록 UI 는 OnCandidatesChanged 로).
	const int32 CandidateCount = Interaction->GetCandidates().Num();
	if (CandidateCount > 1)
		Slots += FString::Printf(TEXT("   [%d/%d  wheel]"), Interaction->GetSelectedIndex() + 1, CandidateCount);
	GEngine->AddOnScreenDebugMessage(9004, 0.0f, FColor::Cyan, FString::Printf(TEXT("LOOK: %s"), *Slots));
}

float ALevelDesignValidationCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!HasAuthority() || Applied <= 0.0f)
		return Applied;
	Health = FMath::Max(0.0f, Health - Applied);
	UE_LOG(LogTemp, Display, TEXT("ValidationCharacter took %.0f from %s (hp %.0f/%.0f)"),
		Applied, *GetNameSafe(DamageCauser), Health, MaxHealth);
	if (Health <= 0.0f)
	{
		// 검증용 폰은 사망 처리가 없다. 죽었다는 사실만 남기고 바로 회복시켜 계속 걸어 다니게 한다.
		UE_LOG(LogTemp, Warning, TEXT("ValidationCharacter died (test pawn: instantly restored)"));
		// 판 기록에는 알린다(판이 열려 있을 때만 뜻이 있다) — 팀 캐릭터 없이도 "사망 → 스코어보드" 흐름을 PIE 에서 볼 수 있게.
		UPGRunSubsystem::NotifyPlayerDied(this, DamageCauser);
		Health = MaxHealth;
	}
	return Applied;
}
