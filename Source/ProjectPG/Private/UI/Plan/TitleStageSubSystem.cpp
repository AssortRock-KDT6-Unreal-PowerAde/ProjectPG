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
	// 플레이어가 자기 몸(로비 기본 몸)에 붙은 뒤에 카메라를 바꿔야 덮어쓰이지 않는다 → 한 틱 뒤.
	TWeakObjectPtr<UTitleStageSubSystem> WeakThis(this);
	World->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateLambda([WeakThis]()
	{
		if (WeakThis.IsValid())
			WeakThis->FocusCamera(WeakThis->MenuCameraTag, 0.0f);
	}));
}

void UTitleStageSubSystem::SpawnTitleCharacter(AActor* Spot)
{
	UWorld* World = GetWorld();
	if (!World || TitleCharacter.IsValid())
		return;
	const UClass* GameModeClass = CharacterSourceGameMode.TryLoadClass<AGameModeBase>();
	const AGameModeBase* GameModeDefaults = GameModeClass ? GameModeClass->GetDefaultObject<AGameModeBase>() : nullptr;
	UClass* PawnClass = GameModeDefaults ? GameModeDefaults->DefaultPawnClass.Get() : nullptr;
	if (!PawnClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TitleStage] no character class from %s"), *CharacterSourceGameMode.ToString());
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* Pawn = World->SpawnActor<APawn>(PawnClass, Spot->GetActorTransform(), Params);
	if (!Pawn)
		return;
	// 조종하는 사람은 없지만 AI 조종기를 붙여야 이동 부품이 돌아 바닥에 내려서고 서 있는 동작이 나온다(레벨에 놓았을 때와 같게).
	Pawn->SpawnDefaultController();
	TitleCharacter = Pawn;
	UE_LOG(LogTemp, Display, TEXT("[TitleStage] character %s at %s"), *PawnClass->GetName(), *Spot->GetActorLocation().ToCompactString());
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
