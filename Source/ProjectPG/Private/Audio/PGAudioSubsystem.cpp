// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
#include "Audio/PGAudioSubsystem.h"

#include "Audio/PGAudioSettings.h"
#include "Audio/PGCharacterSoundComponent.h"
#include "Audio/PGSoundSet.h"
#include "AudioDevice.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"
#include "LandscapeProxy.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundWave.h"

DEFINE_LOG_CATEGORY_STATIC(LogPGAudio, Log, All);

// UI 버튼 소리: 팀원 위젯 18개를 하나씩 고치지 않고, 마우스 입력을 먼저 엿봐서
// 커서 밑에 버튼이 있으면 소리만 내고 입력은 그대로 흘려보낸다(먹지 않음).
// - 버튼 위에서 누름 → UI.Click
// - 커서가 새 버튼 위로 올라감 → UI.Hover (같은 버튼 위에서 움직이면 다시 안 냄)
// 부담: 마우스가 움직일 때만 커서 밑 위젯을 찾음. 게임 중 마우스가 숨겨져 있으면 거의 안 돎.
class FPGUISoundInput : public IInputProcessor
{
public:
	explicit FPGUISoundInput(UPGAudioSubsystem* InOwner) : Owner(InOwner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (FindButtonUnderCursor(SlateApp, MouseEvent).IsValid()) Play(TEXT("UI.Click"));
		return false;
	}

	virtual bool HandleMouseMoveEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		if (!SlateApp.IsUsingHighPrecisionMouseMovment() && SlateApp.GetPlatformCursor().IsValid()
			&& SlateApp.GetPlatformCursor()->GetType() != EMouseCursor::None)
		{
			const TSharedPtr<SWidget> Button = FindButtonUnderCursor(SlateApp, MouseEvent);
			if (Button.IsValid() && Button != LastHovered.Pin()) Play(TEXT("UI.Hover"));
			LastHovered = Button;
		}
		return false;
	}

	virtual const TCHAR* GetDebugName() const override { return TEXT("PGUISoundInput"); }

private:
	TSharedPtr<SWidget> FindButtonUnderCursor(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) const
	{
		const FWidgetPath Path = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
		// 가장 안쪽(맨 위)에서 바깥쪽으로 보며 처음 만나는 켜진 버튼.
		for (int32 Index = Path.Widgets.Num() - 1; Index >= 0; --Index)
		{
			const TSharedRef<SWidget>& Widget = Path.Widgets[Index].Widget;
			const FName Type = Widget->GetType();
			if ((Type == TEXT("SButton") || Type == TEXT("SCheckBox")) && Widget->IsEnabled()) return Widget;
		}
		return nullptr;
	}

	void Play(FName Cue) const
	{
		if (UPGAudioSubsystem* Audio = Owner.Get()) Audio->PlayCueByName(Cue, FVector::ZeroVector);
	}

	TWeakObjectPtr<UPGAudioSubsystem> Owner;
	TWeakPtr<SWidget> LastHovered;
};

UPGAudioSubsystem* UPGAudioSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UPGAudioSubsystem>() : nullptr;
}

bool UPGAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 게임 월드(PIE·패키지)만. 에디터 편집 화면·애셋 미리보기에는 안 만든다.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && !IsRunningDedicatedServer();
}

void UPGAudioSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	// PIE 에서 데디 서버를 같은 프로세스로 띄우면 IsRunningDedicatedServer 가 false 라 여기서 한 번 더 거른다.
	if (InWorld.GetNetMode() == NM_DedicatedServer) return;

	const UPGAudioSettings* Settings = GetDefault<UPGAudioSettings>();
	SoundSet = Settings->SoundSet.LoadSynchronous();
	if (!SoundSet)
	{
		UE_LOG(LogPGAudio, Error, TEXT("소리 표가 없음: 프로젝트 설정 → Game → PG Audio → SoundSet 이 비었거나 '%s' 를 못 읽음. 게임 소리가 전부 꺼짐."),
			*Settings->SoundSet.ToString());
		return;
	}

	// 부품 클래스를 BP 로 바꿔 끼웠으면 그걸 씀. 못 읽으면 C++ 기본으로 계속(소리는 나야 하니까) + 로그.
	if (!Settings->CharacterSoundClass.IsNull())
	{
		CharacterSoundClass = Settings->CharacterSoundClass.LoadSynchronous();
		if (!CharacterSoundClass)
			UE_LOG(LogPGAudio, Error, TEXT("캐릭터 소리 부품 '%s' 를 못 읽음 → C++ 기본 부품으로 대신함."), *Settings->CharacterSoundClass.ToString());
	}

	// 캐릭터는 대부분 나중에 생긴다(접속·복제·몬스터 스폰). 생길 때마다 소리 부품을 붙이고, 이미 있는 것도 붙인다.
	ActorSpawnedHandle = InWorld.AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UPGAudioSubsystem::HandleActorSpawned));
	for (TActorIterator<ACharacter> It(&InWorld); It; ++It) AttachCharacterSounds(*It);

	if (FSlateApplication::IsInitialized() && !UIInput.IsValid())
	{
		UIInput = MakeShared<FPGUISoundInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(UIInput);
	}
}

void UPGAudioSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld()) World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
	if (UIInput.IsValid() && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(UIInput);
	UIInput.Reset();
	Super::Deinitialize();
}

void UPGAudioSubsystem::HandleActorSpawned(AActor* Actor)
{
	if (ACharacter* Character = Cast<ACharacter>(Actor)) AttachCharacterSounds(Character);
}

void UPGAudioSubsystem::AttachCharacterSounds(ACharacter* Character)
{
	if (!IsValid(Character) || Character->FindComponentByClass<UPGCharacterSoundComponent>()) return;
	UClass* ComponentClass = CharacterSoundClass ? CharacterSoundClass.Get() : UPGCharacterSoundComponent::StaticClass();
	UPGCharacterSoundComponent* Component = NewObject<UPGCharacterSoundComponent>(Character, ComponentClass, TEXT("PGCharacterSound"));
	Component->RegisterComponent();
}

int32 UPGAudioSubsystem::PickIndex(const void* Key, int32 Count)
{
	if (Count <= 1) return 0;
	int32& Last = LastPicked.FindOrAdd(Key, INDEX_NONE);
	int32 Pick = FMath::RandRange(0, Count - 2);
	if (Last != INDEX_NONE && Pick >= Last) ++Pick; // 바로 전 것만 건너뜀
	Last = Pick;
	return Pick;
}

UAudioComponent* UPGAudioSubsystem::PlayCue(const FPGSoundCue& Cue, const FVector& Location, float VolumeScale)
{
	UWorld* World = GetWorld();
	if (!World || !Cue.Sound || VolumeScale <= 0.f) return nullptr;

	USoundWave* Wave = Cue.Waves.Num() > 0 ? Cue.Waves[PickIndex(&Cue.Waves, Cue.Waves.Num())].Get() : nullptr;
	USoundWave* FarWave = Cue.FarWaves.Num() > 0 ? Cue.FarWaves[PickIndex(&Cue.FarWaves, Cue.FarWaves.Num())].Get() : nullptr;
	// 시험용 기록: -LogCmds="LogPGAudio Verbose" 로 켜면 소리 장치 없이(-nosound) 띄운 클라에서도 어떤 소리가 언제 났는지 보임.
	UE_LOG(LogPGAudio, Verbose, TEXT("소리 %s / %s at %s"), *GetNameSafe(Cue.Sound), *GetNameSafe(Wave), *Location.ToCompactString());

	FAudioDevice::FCreateComponentParams Params(World);
	Params.bAutoDestroy = true;
	Params.bPlay = false;
	if (!Cue.b2D)
	{
		Params.SetLocation(Location);
		Params.AttenuationSettings = Cue.Attenuation;
	}
	UAudioComponent* Audio = FAudioDevice::CreateComponent(Cue.Sound, Params);
	if (!Audio) return nullptr; // 소리 장치가 없을 때(-nosound 등)

	if (Cue.b2D)
	{
		Audio->bAllowSpatialization = false;
		Audio->bIsUISound = true;
	}
	Audio->SetVolumeMultiplier(Cue.Volume * VolumeScale);
	Audio->SetPitchMultiplier(1.f + FMath::FRandRange(-Cue.PitchJitter, Cue.PitchJitter));

	// 메타사운드 입력 이름은 MS_OneShot / MS_Gunshot 그래프의 입력과 같아야 한다(README 참고).
	if (Wave) Audio->SetObjectParameter(TEXT("Wave"), Wave);
	if (FarWave) Audio->SetObjectParameter(TEXT("FarWave"), FarWave);
	// 0/1 숫자로 넘김: 메타사운드 안에서 메아리 크기에 바로 곱하려고(참/거짓을 숫자로 바꾸는 노드가 따로 없음).
	if (Cue.bCheckIndoor) Audio->SetFloatParameter(TEXT("Indoor"), IsIndoors(Location) ? 1.f : 0.f);

	Audio->Play();
	return Audio;
}

UAudioComponent* UPGAudioSubsystem::PlayCueByName(FName CueName, const FVector& Location, float VolumeScale)
{
	if (!SoundSet) return nullptr;
	const FPGSoundCue* Cue = SoundSet->Cues.Find(CueName);
	return Cue ? PlayCue(*Cue, Location, VolumeScale) : nullptr;
}

EPhysicalSurface UPGAudioSubsystem::SurfaceOf(const FHitResult& Hit)
{
	const EPhysicalSurface Surface = UPhysicalMaterial::DetermineSurfaceType(Hit.PhysMaterial.Get());
	if (Surface != SurfaceType_Default) return Surface;
	// 캐릭터·몬스터 몸에는 물리 재질을 따로 안 붙여 둠 → 맞은 게 폰이면 살(Flesh)로 봄.
	if (Cast<APawn>(Hit.GetActor())) return SurfaceType6; // DefaultEngine.ini: SurfaceType6 = Flesh
	if (!SoundSet) return Surface;
	if (Cast<ALandscapeProxy>(Hit.GetActor())) return SoundSet->LandscapeSurface;
	return GuessSurfaceByName(Hit.GetComponent());
}

EPhysicalSurface UPGAudioSubsystem::GuessSurfaceByName(const UPrimitiveComponent* Component)
{
	if (!Component || SoundSet->SurfaceKeywords.Num() == 0) return SurfaceType_Default;
	// 머티리얼 이름이 재질을 제일 잘 말해 줌(M_Metal_Rusty 등). 없으면 메시 이름, 그다음 액터 이름.
	const UObject* Material = Component->GetMaterial(0);
	const UObject* Key = Material ? Material : static_cast<const UObject*>(Component);
	if (const TEnumAsByte<EPhysicalSurface>* Known = GuessedSurfaces.Find(Key)) return *Known;

	FString Names = GetNameSafe(Material);
	if (const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component)) Names += TEXT(" ") + GetNameSafe(Mesh->GetStaticMesh());
	Names += TEXT(" ") + GetNameSafe(Component->GetOwner());

	EPhysicalSurface Found = SurfaceType_Default;
	for (const FPGSurfaceKeyword& Rule : SoundSet->SurfaceKeywords)
		if (!Rule.Keyword.IsEmpty() && Names.Contains(Rule.Keyword, ESearchCase::IgnoreCase)) { Found = Rule.Surface; break; }
	UE_LOG(LogPGAudio, Verbose, TEXT("재질 짐작: '%s' → %d"), *Names, static_cast<int32>(Found));
	GuessedSurfaces.Add(Key, Found);
	return Found;
}

bool UPGAudioSubsystem::IsIndoors(const FVector& Location) const
{
	const UWorld* World = GetWorld();
	if (!World) return false;
	const float Height = GetDefault<UPGAudioSettings>()->IndoorCeilingDistance;
	// 바로 위 하나 + 비스듬히 네 방향. 창문·뚫린 지붕 한두 개는 봐주려고 5개 중 4개 이상 막히면 실내.
	static const FVector Directions[] = {
		FVector(0, 0, 1), FVector(0.5f, 0, 1), FVector(-0.5f, 0, 1), FVector(0, 0.5f, 1), FVector(0, -0.5f, 1) };
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGIndoor), false);
	const FVector Start = Location + FVector(0, 0, 50);
	int32 Blocked = 0;
	for (const FVector& Direction : Directions)
	{
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Start, Start + Direction.GetSafeNormal() * Height, ECC_Visibility, Params)) ++Blocked;
	}
	return Blocked >= 4;
}
