// Fill out your copyright notice in the Description page of Project Settings.


#include "GameMode/CustomPlayerController.h"
#include "Characters/CustomPlayerCharacter.h"
#include "Objects/PGInteractionComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Core/UIManagerSubSystem.h"
#include "UI/ItemDragDropOperation.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include <UI/Controller/LobbyUIFlowController.h>
#include "UI/InventoryWindow.h"
#include "GameMode/CustomPlayerState.h"
#include "Components/InventoryComponent.h"
#include "Components/EquipComponent.h"
#include "Objects/PGWigBeamComponent.h"

void ACustomPlayerController::BeginPlay()
{
	Super::BeginPlay();

}
void ACustomPlayerController::ToggleInventory()
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UUIManagerSubSystem* UIMgr = GI->GetSubsystem<UUIManagerSubSystem>())
		{
			// PlayerController는 InventoryWidget의 존재를 몰라도 됨!
			// 열거형(EUIType)만 넘겨서 UIManager에게 처리를 위임함.
			// (2026-09-19 인게임 연결) EUIType::Inventory 에 등록된 건 격자 조각(WBP_InventoryGrid)이라 혼자 띄우면 빈 화면이었다.
			// 실제 창은 EUIType::Character(WBP_CharacterWidget = UInventoryWindow). 로비(LobbyWidget::OnClickedCharacterButton)와
			// 같은 순서로 창을 열고 연결한다. 판 안에서는 창고(Stash)가 없으니 주머니·가방만 붙인다.
			UUserWidget* Opened = UIMgr->ToggleUI(EUIType::Character);
			UInventoryWindow* Window = Cast<UInventoryWindow>(Opened);
			ACustomPlayerState* MyPS = GetPlayerState<ACustomPlayerState>();
			if (Window && MyPS)
			{
				UInventoryComponent* InvenComp = MyPS->GetComponentByClass<UInventoryComponent>();
				Window->InitWidget(InvenComp, MyPS->GetComponentByClass<UEquipComponent>());
				const TSubclassOf<UUserWidget> GridClass = UIMgr->GetUIClass(EUIType::Inventory);
				Window->SetupPocketInventoryWidget(GridClass);
				Window->SetupBackPackInventoryWidget(GridClass);
				// 서버 없이도 칸이 바로 그려지게: 창은 OnInventoryUpdated 를 들어 RefreshAllGrids 를 한다.
				if (InvenComp)
					InvenComp->OnInventoryUpdated.Broadcast();
			}
		}
	}
}


void ACustomPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::I, IE_Pressed, this, &ACustomPlayerController::ToggleInventory);
		InputComponent->BindKey(EKeys::R, IE_Pressed, this, &ACustomPlayerController::OnRotateKey);
		// 가발 광선: 왼쪽·오른쪽 클릭을 둘 다 가발 부품에 넘기고, 어느 버튼으로 쏠지는 부품의 FireKey 가 정한다
		// (9/23 사용자: "왼쪽 클릭으로" — 기본 왼쪽, 캐릭터 블루프린트에서 바꾼다. 9/20 에는 오른쪽이었다).
		// 누름을 가로채지 않는다(bConsumeInput=false) — 같은 왼쪽 클릭을 쓰는 총 공격(팀 입력 표)도 그대로 받는다.
		InputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ACustomPlayerController::OnWigBeamLeftKey).bConsumeInput = false;
		InputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &ACustomPlayerController::OnWigBeamKey).bConsumeInput = false;
		InputComponent->BindKey(EKeys::V, IE_Pressed, this, &ACustomPlayerController::OnToggleViewKey);
		// F 는 팀 입력 표(NA_Interaction)에도 있을 수 있다. 두 번 들어와도
		// UPGInteractionComponent 가 0.2초 디바운스로 한 번만 처리한다.
		InputComponent->BindKey(EKeys::F, IE_Pressed, this, &ACustomPlayerController::OnInteractKey);
	}
}


void ACustomPlayerController::OnInteractKey()
{
	// 줍기·대화·문 열기. 팀 입력 표에 F 가 매핑돼 있지 않아도 동작하게 하는 예비 경로다
	// (9/20 PIE: 연료통을 주울 수 없었고 로그에 시도 자체가 없었다).
	if (APawn* ControlledPawn = GetPawn())
		if (UPGInteractionComponent* Interaction = ControlledPawn->FindComponentByClass<UPGInteractionComponent>())
			Interaction->BeginInteract();
}

void ACustomPlayerController::OnToggleViewKey()
{
	// 1인칭 = 카메라 팔 길이 0. 팀 캐릭터의 카메라 구조를 바꾸지 않고 길이만 줄였다 늘린다
	// (검증 캐릭터 ALevelDesignValidationCharacter::SetFirstPerson 과 같은 방식).
	// 왜 필요한가: 전함 함교에서 앞유리 밖을 보려면 3인칭 카메라가 배 안쪽 벽에 파묻혀 쓸 수 없다.
	APawn* ControlledPawn = GetPawn();
	ACustomPlayerCharacter* PlayerCharacter = Cast<ACustomPlayerCharacter>(ControlledPawn);
	USpringArmComponent* Arm = PlayerCharacter ? PlayerCharacter->GetCameraArm() : nullptr;
	if (!Arm)
		return; // 탈것에 타고 있으면 탈것 카메라가 따로 있다
	if (ThirdPersonArmLength <= 0.0f)
		ThirdPersonArmLength = FMath::Max(Arm->TargetArmLength, 100.0f);
	bFirstPersonView = !bFirstPersonView;
	Arm->TargetArmLength = bFirstPersonView ? 0.0f : ThirdPersonArmLength;
	// 1인칭에서는 어깨너머 치우침을 없애고 눈높이로 올린다.
	Arm->SocketOffset = bFirstPersonView ? FVector(0.0f, 0.0f, 60.0f) : FVector(0.0f, 0.0f, 0.0f);
	// 제 머리가 화면을 가리지 않게.
	if (USkeletalMeshComponent* Mesh = PlayerCharacter->GetMesh())
		Mesh->SetOwnerNoSee(bFirstPersonView);
	UE_LOG(LogTemp, Display, TEXT("View: %s"), bFirstPersonView ? TEXT("first person") : TEXT("third person"));
}

void ACustomPlayerController::OnWigBeamKey()
{
	// 차·로봇에 타고 있으면 조종 대상이 탈것이라 컴포넌트가 없다 → 아무 일도 없다.
	if (APawn* ControlledPawn = GetPawn())
		if (UPGWigBeamComponent* Beam = ControlledPawn->FindComponentByClass<UPGWigBeamComponent>())
			Beam->RequestFireWithKey(EKeys::RightMouseButton);
}

void ACustomPlayerController::OnWigBeamLeftKey()
{
	if (APawn* ControlledPawn = GetPawn())
		if (UPGWigBeamComponent* Beam = ControlledPawn->FindComponentByClass<UPGWigBeamComponent>())
			Beam->RequestFireWithKey(EKeys::LeftMouseButton);
}

void ACustomPlayerController::OnRotateKey()
{
	// 현재 마우스에 들려있는 DragDropOp 가져오기
	if (UItemDragDropOperation* DragOp = Cast<UItemDragDropOperation>(UWidgetBlueprintLibrary::GetDragDroppingContent()))
	{
		DragOp->RotateItem();
	}
}