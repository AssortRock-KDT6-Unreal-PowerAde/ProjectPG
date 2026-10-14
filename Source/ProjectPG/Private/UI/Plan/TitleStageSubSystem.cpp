#include "UI/Plan/TitleStageSubSystem.h"

#include "Camera/CameraActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

UTitleStageSubSystem* UTitleStageSubSystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UTitleStageSubSystem>() : nullptr;
}

bool UTitleStageSubSystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	// 게임(혼자 실행·에디터 플레이)에서만. 에디터에서 레벨을 열어 볼 때는 캐릭터를 세우지 않는다.
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

void UTitleStageSubSystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	// 전용 서버에는 화면이 없다.
	if (InWorld.GetNetMode() == NM_DedicatedServer)
		return;
	SetupStage();
}

void UTitleStageSubSystem::SetupStage()
{
	UWorld* World = GetWorld();
	AActor* Spot = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CharacterSpotTag))
		{
			Spot = *It;
			break;
		}
	}
	bIsTitleStage = Spot != nullptr;
	if (!bIsTitleStage)
		return;

	SpawnTitleCharacter(Spot);
	// 플레이어가 자기 몸(로비 기본 몸)에 붙은 뒤에 옮기고 카메라를 바꿔야 덮어쓰이지 않는다 → 한 틱 뒤.
	TWeakObjectPtr<UTitleStageSubSystem> WeakThis(this);
	TWeakObjectPtr<AActor> WeakSpot(Spot);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis, WeakSpot]()
	{
		if (WeakThis.IsValid())
		{
			WeakThis->SpawnTitleCharacter(WeakSpot.Get());
			WeakThis->FocusCamera(WeakThis->MenuCameraTag, 0.0f);
		}
	}));
}
void UTitleStageSubSystem::SpawnTitleCharacter(AActor* Spot)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !Spot)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TitleStage] no player character to place"));
		return;
	}
	// 캐릭터는 로비 게임모드(GM_InLobby)가 세운다. 여기서는 타이틀 자리로 옮기기만 한다.
	Pawn->SetActorTransform(Spot->GetActorTransform(), false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(Spot->GetActorRotation());
	TitleCharacter = Pawn;
	UE_LOG(LogTemp, Display, TEXT("[TitleStage] character %s at %s"), *Pawn->GetClass()->GetName(), *Spot->GetActorLocation().ToCompactString());
}



void UTitleStageSubSystem::FocusCamera(FName CameraTag, float BlendSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !bIsTitleStage)
		return;
	for (TActorIterator<ACameraActor> It(World); It; ++It)
	{
		if (It->ActorHasTag(CameraTag))
		{
			PC->SetViewTargetWithBlend(*It, BlendSeconds, VTBlend_EaseInOut, 2.0f);
			return;
		}
	}
}
