// Fill out your copyright notice in the Description page of Project Settings.


#include "Characters/CustomPlayerCharacter.h"

#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/NativeActionComponent.h"
#include "Core/TableSubSystem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameplayAbilities/CustomAbilitySystemComponent.h"
#include "Blueprint/UserWidget.h"
#include "Components/InventoryComponent.h"
#include "GameMode/CustomPlayerState.h"
#include "Objects/PGInteractionComponent.h"
#include "UI/PGInteractionPromptWidget.h"
#include "UI/PGCrosshairWidget.h"
#include "Objects/PGWigBeamComponent.h"
#include "Common/PGCameraUtil.h"

ACustomPlayerCharacter::ACustomPlayerCharacter()
{
	USceneComponent* rootComp = GetRootComponent();
	if (!IsValid(rootComp))
		return;

	CameraArmComp = CreateDefaultSubobject<USpringArmComponent>("CameraArm");
	if (!IsValid(CameraArmComp))
		return;

	CameraArmComp->SetupAttachment(rootComp);

	FVector cameraArmAdditiveLocation = FVector::ZeroVector;
	cameraArmAdditiveLocation.Z += 50.;
	CameraArmComp->AddRelativeLocation(cameraArmAdditiveLocation);
	CameraArmComp->TargetArmLength = 200.f;

	CameraComp = CreateDefaultSubobject<UCameraComponent>("Camera");
	if (!IsValid(CameraComp))
		return;

	CameraComp->SetupAttachment(CameraArmComp);

	FVector cameraAdditiveLocation = FVector::ZeroVector;
	cameraAdditiveLocation.Y += 30.;
	CameraComp->AddRelativeLocation(cameraAdditiveLocation);

	NativeActionComp = CreateDefaultSubobject<UNativeActionComponent>(TEXT("NativeAction"));
	if (!IsValid(NativeActionComp))
		return;

	AbilitySystemComp = CreateDefaultSubobject<UCustomAbilitySystemComponent>(TEXT("AbilitySystem"));
	if (!IsValid(AbilitySystemComp))
		return;

	AbilitySystemComp->SetIsReplicated(true);
	AbilitySystemComp->SetReplicationMode(EGameplayEffectReplicationMode::Mixed);

	// 상호작용: 카메라가 보는 방향의 상자·문·탈것을 골라 두고(컴포넌트 Tick), F 가 눌리면 NA_Interaction 이 실행시킨다.
	Interaction = CreateDefaultSubobject<UPGInteractionComponent>(TEXT("Interaction"));
	WigBeam = CreateDefaultSubobject<UPGWigBeamComponent>(TEXT("WigBeam"));
}

void ACustomPlayerCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	// 탈것에서 내린 뒤: 탈것이 줄여 둔 위쪽 보기 각도를 원래대로(탈것 카메라가 땅 밑으로 안 가게 제한했던 것 — PGCameraUtil.h).
	PGCameraUtil::RestoreCameraPitch(this);

	// 안내 문구 위젯은 내 화면에서 조종할 때 한 번만 만든다. BeginPlay 때는 아직 컨트롤러가 없을 수 있어 Tick 에서 확인.
	if (!IsValid(PromptWidget) && IsLocallyControlled() && IsValid(Interaction))
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			PromptWidget = UPGInteractionPromptWidget::Create(PC);
			if (IsValid(PromptWidget))
			{
				PromptWidget->AddToViewport();
				PromptWidget->BindTo(Interaction);
			}
		}
	}
	// 조준점(총·가발 광선 공용, 9/23)도 같은 때 한 번 만든다. 차·로봇·전함 조종석에서는 위젯이 스스로 숨는다.
	if (!IsValid(CrosshairWidget) && IsLocallyControlled())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetController()))
		{
			CrosshairWidget = UPGCrosshairWidget::Create(PC);
			if (IsValid(CrosshairWidget) && !CrosshairWidget->IsInViewport())
				CrosshairWidget->AddToViewport(-10); // 다른 화면(안내·메뉴)보다 아래
		}
	}
}

void ACustomPlayerCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	APlayerController* controller = Cast<APlayerController>(GetController());
	if (!IsValid(controller))
		return;

	UEnhancedInputLocalPlayerSubsystem* inputSubsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(
		controller->GetLocalPlayer());
	if (!IsValid(inputSubsystem))
		return;

	UEnhancedInputComponent* inputComp = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!IsValid(inputComp))
		return;

	UTableSubSystem* tableSubSystem = UTableSubSystem::Get(this);
	if (!IsValid(tableSubSystem))
		return;

	const FPlayerDefaultActionTableRow* playerDefaultActionRow = tableSubSystem->FindTableRow<
		FPlayerDefaultActionTableRow>(
		"PlayerDefaultActionTable", "PlayerDefault");
	if (nullptr == playerDefaultActionRow)
		return;

	inputSubsystem->AddMappingContext(playerDefaultActionRow->InputMappingContext.Get(), 0);

	if (!IsValid(NativeActionComp))
		return;

	for (auto& TaggedNativeAction : playerDefaultActionRow->TaggedNativeActions)
	{
		UNativeAction* nativeAction = NativeActionComp->RegisterNativeAction(TaggedNativeAction.NativeActionClass);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Started))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Started,
			                      nativeAction, &UNativeAction::Started, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Triggered))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Triggered,
			                      nativeAction, &UNativeAction::Triggered, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Ongoing))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Ongoing,
			                      nativeAction, &UNativeAction::Ongoing, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Completed))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Completed,
			                      nativeAction, &UNativeAction::Completed, this);

		if (nativeAction->ShouldRegisterTriggerEvent(ETriggerEvent::Canceled))
			inputComp->BindAction(TaggedNativeAction.InputAction, ETriggerEvent::Canceled,
			                      nativeAction, &UNativeAction::Canceled, this);
	}

	UCustomAbilitySystemComponent* abilitySystemComp = GetCustomAbilitySystemComponent();
	if (!IsValid(abilitySystemComp))
		return;

	for (auto& TaggedInputAction : playerDefaultActionRow->TaggedAbilities)
	{
		inputComp->BindAction(TaggedInputAction.InputAction, ETriggerEvent::Started, abilitySystemComp,
		                      &UCustomAbilitySystemComponent::AbilityInputPressed, TaggedInputAction.Tag);
		inputComp->BindAction(TaggedInputAction.InputAction, ETriggerEvent::Completed, abilitySystemComp,
		                      &UCustomAbilitySystemComponent::AbilityInputReleased, TaggedInputAction.Tag);
	}
}

UCustomAbilitySystemComponent* ACustomPlayerCharacter::GetCustomAbilitySystemComponent() const
{
	return Cast<UCustomAbilitySystemComponent>(AbilitySystemComp);
}

USpringArmComponent* ACustomPlayerCharacter::GetCameraArm() const
{
	return CameraArmComp;
}

void ACustomPlayerCharacter::BeginPlay()
{
	Super::BeginPlay();

	AbilitySystemComp->InitAbilityActorInfo(this, this);

	// [멀티 임시수정 2026-09-27 — 형님께 전달] 시작 (Docs/TeamHandoff_2026-09-27_PlayerMultiplayer.md)
	// 무엇: 최고 걷기 속도(MaxWalkSpeed)를 서버·클라이언트 모두에서 넣는다(아래 HasAuthority 안의 같은 줄은 그대로 둔다).
	// 왜: 그 줄이 서버에서만 돌아 클라이언트는 기본값 600 으로 걸었다. 서버는 300 이라, 걸을 때마다 서버가 위치를 뒤로 되돌렸다.
	if (UCharacterMovementComponent* sharedMovementComp = GetCharacterMovement())
		sharedMovementComp->MaxWalkSpeed = 300.f;
	// [멀티 임시수정] 끝

	if (HasAuthority())
	{
		if (!IsValid(CharacterAttributeSet))
			return;

		// TODO: Table로 옮기기
		CharacterAttributeSet->InitHealth(100.f);
		CharacterAttributeSet->InitMaxHealth(100.f);
		CharacterAttributeSet->InitStamina(100.f);
		CharacterAttributeSet->InitMaxStamina(100.f);
		CharacterAttributeSet->InitWalkSpeed(300.f);
		CharacterAttributeSet->InitSprintSpeed(700.f);

		UCharacterMovementComponent* movementComp = GetCharacterMovement();
		if (!IsValid(movementComp))
			return;

		movementComp->MaxWalkSpeed = 300.f;
		// ~TODO: Table로 옮기기

		UTableSubSystem* tableSubSystem = UTableSubSystem::Get(this);
		if (!IsValid(tableSubSystem))
			return;

		const FPlayerDefaultActionTableRow* playerDefaultActionRow = tableSubSystem->FindTableRow<
			FPlayerDefaultActionTableRow>(
			"PlayerDefaultActionTable", "PlayerDefault");
		if (nullptr == playerDefaultActionRow)
			return;

		if (!IsValid(AbilitySystemComp))
			return;

		for (auto& TaggedInputAction : playerDefaultActionRow->TaggedAbilities)
		{
			FGameplayAbilitySpec spec(TaggedInputAction.GameAbilityClass);
			spec.GetDynamicSpecSourceTags().AddTag(TaggedInputAction.Tag);

			AbilitySystemComp->GiveAbility(spec);
		}
	}
}

// ---- 상호작용·인벤토리 연결 (2026-09-19) ----

void ACustomPlayerCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	// PossessedBy 에서 하는 이유: 이 시점에 PlayerState 가 확실히 붙어 있다. BeginPlay 는 빙의 전일 수 있어 PlayerState 가 없을 수 있다.
	EnsureOfflinePocket();
}

UInventoryComponent* ACustomPlayerCharacter::GetInventory() const
{
	const ACustomPlayerState* State = GetPlayerState<ACustomPlayerState>();
	return IsValid(State) ? State->InvenComp.Get() : nullptr;
}

void ACustomPlayerCharacter::EnsureOfflinePocket()
{
	UInventoryComponent* Inventory = GetInventory();
	// 이미 주머니가 있으면(서버가 보내 준 진짜 데이터) 건드리지 않는다.
	if (!IsValid(Inventory) || Inventory->GetPocketInventoryID().IsValid())
		return;
	// 서버 없이 PIE 를 돌리면 주머니 ID·격자 크기가 비어 있어 AddItemByID 가 항상 실패했다(격자 순회가 0번).
	// 테스트용 주머니: 가로 10 × 세로 6 칸. 나중에 서버가 인벤토리를 보내면 HandleInventoryReceived 가 통째로 다시 채운다.
	const FGuid Pocket = FGuid::NewGuid();
	Inventory->SetPocketInventoryID(Pocket);
	// RegisterContainer: 크기 + 빈 칸 격자 + 아이템 목록 자리를 한 번에 만든다(가방 창이 빈 칸을 바로 그릴 수 있게).
	Inventory->RegisterContainer(Pocket, FIntPoint(10, 6));
	UE_LOG(LogTemp, Display, TEXT("%s: offline pocket created (10x6) — no inventory from server"), *GetName());
}

bool ACustomPlayerCharacter::ReceiveItem_Implementation(FName ItemId, int32 Count)
{
	UInventoryComponent* Inventory = GetInventory();
	if (!IsValid(Inventory) || Count <= 0)
		return false;
	// 결과를 그대로 돌려준다: false(가방 꽉 참·표에 없는 아이템)면 상자가 내용물을 그대로 유지한다(오브젝트 쪽 규칙).
	return Inventory->AddItemByID(ItemId, Inventory->GetPocketInventoryID(), Count);
}

bool ACustomPlayerCharacter::HasItem_Implementation(FName ItemId, int32 Count) const
{
	const UInventoryComponent* Inventory = GetInventory();
	if (!IsValid(Inventory))
		return false;
	// 모든 가방(주머니·배낭 등)을 돌며 같은 아이템 개수를 더한다. GetItemInstance 는 첫 하나만 돌려줘서 개수 확인에 부족하다.
	// 스택이 아닌 아이템은 StackCount 가 0 으로 올 수 있어 "0 이면 1개"로 센다.
	int32 Total = 0;
	for (const TPair<FGuid, FItemArrayWrapper>& Bag : Inventory->GetItemsMap())
		for (const FItemInstance& Item : Bag.Value.Items)
			if (Item.ItemID == ItemId)
				Total += FMath::Max(1, Item.StackCount);
	return Total >= Count;
}

bool ACustomPlayerCharacter::ConsumeItem_Implementation(FName ItemId, int32 Count)
{
	UInventoryComponent* Inventory = GetInventory();
	// 부르는 쪽(열쇠 문·연료 주입·퀘스트 납품)이 HasItem 으로 먼저 확인하고 서버에서만 부른다(인터페이스 규칙).
	return IsValid(Inventory) && Inventory->RemoveItemByID(ItemId, Count);
}

FName ACustomPlayerCharacter::GetPlayerKey_Implementation() const
{
	// 플레이어별 탈출구·1회 퀘스트를 구분하는 키. 폰 이름은 리스폰하면 바뀌어서 PlayerState 의 PlayerId 를 쓴다(세션 안에서 안정).
	const APlayerState* State = GetPlayerState();
	return IsValid(State) ? FName(*FString::Printf(TEXT("Player_%d"), State->GetPlayerId())) : NAME_None;
}
