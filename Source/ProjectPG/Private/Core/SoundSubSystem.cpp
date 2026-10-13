// Fill out your copyright notice in the Description page of Project Settings.


#include "Core/SoundSubSystem.h"
#include "Kismet/GameplayStatics.h"
#include "AkAudioEvent.h"
#include "Core/TableSubSystem.h"
#include "Common/SoundData.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Engine/GameViewportClient.h"
#include "Widgets/SViewport.h"


// 마우스 왼쪽 버튼이 눌릴 때마다, 마우스 밑에 게임 화면의 버튼이 있으면 클릭 소리를 낸다.
class FUIClickSoundProcessor : public IInputProcessor
{
public:
	explicit FUIClickSoundProcessor(USoundSubSystem* owner) : _owner(owner) {}

	virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override {}

	virtual bool HandleMouseButtonDownEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		PlayIfOnButton(SlateApp, MouseEvent);
		return false; // false = 클릭을 가로채지 않고 원래 가던 곳으로 그대로 보낸다.
	}

	// 빠르게 두 번 누르면 두 번째는 "더블클릭"으로 들어오므로 같이 받는다.
	virtual bool HandleMouseButtonDoubleClickEvent(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent) override
	{
		PlayIfOnButton(SlateApp, MouseEvent);
		return false;
	}

private:
	void PlayIfOnButton(FSlateApplication& SlateApp, const FPointerEvent& MouseEvent)
	{
		if (MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton) return;
		if (false == _owner.IsValid()) return;

		// 게임 화면 위젯. 에디터 툴바 같은 게임 밖 버튼을 걸러 내는 데 쓴다.
		UGameViewportClient* viewportClient = _owner->GetGameInstance()->GetGameViewportClient();
		TSharedPtr<SViewport> gameViewport = viewportClient ? viewportClient->GetGameViewportWidget() : nullptr;
		if (false == gameViewport.IsValid()) return;

		// 마우스 밑에 깔린 위젯들을 바깥(창)부터 안쪽 순서로 받아 온다.
		FWidgetPath path = SlateApp.LocateWindowUnderMouse(MouseEvent.GetScreenSpacePosition(), SlateApp.GetInteractiveTopLevelWindows());
		if (false == path.IsValid() || false == path.ContainsWidget(gameViewport.Get())) return;

		static const FName buttonType(TEXT("SButton"));
		for (int32 i = path.Widgets.Num() - 1; i >= 0; --i)
		{
			const TSharedRef<SWidget>& widget = path.Widgets[i].Widget;
			if (widget->GetType() == buttonType)
			{
				// 꺼진(회색) 버튼은 눌러도 소리가 안 난다.
				if (widget->IsEnabled())
				{
					_owner->PlaySound2D(TEXT("UI_Click"));
				}
				return;
			}
		}
	}

	TWeakObjectPtr<USoundSubSystem> _owner;
};

USoundSubSystem* USoundSubSystem::Get(const UObject* worldContext)
{
	if (nullptr == worldContext) return nullptr;
	//"지금 나를 부른 애가 누구냐"
	UGameInstance* inst = UGameplayStatics::GetGameInstance(worldContext);
	if (nullptr == inst) return nullptr;
	return inst ->GetSubsystem<USoundSubSystem>();
}
void USoundSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// 전용 서버에는 화면(Slate)이 없으므로 건너뛴다.
	if (FSlateApplication::IsInitialized())
	{
		_clickSoundProcessor = MakeShared<FUIClickSoundProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(_clickSoundProcessor);
	}
}

void USoundSubSystem::Deinitialize()
{
	if (_clickSoundProcessor.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(_clickSoundProcessor);
	}
	_clickSoundProcessor.Reset();

	Super::Deinitialize();
}
UAkAudioEvent* USoundSubSystem::FindEvent(FName soundName) const
{
	// 전용 서버는 듣는 사람이 없으니 소리를 내지 않는다.
	if (IsRunningDedicatedServer()) return nullptr;

	const UTableSubSystem* tableSubSystem = GetGameInstance()->GetSubsystem<UTableSubSystem>();
	if (nullptr == tableSubSystem) return nullptr;

	const FSoundTableRow* row = tableSubSystem->FindTableRow<FSoundTableRow>(TEXT("SoundTable"), soundName);
	if (nullptr == row || nullptr == row->Event)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Sound] SoundTable 에 '%s' 가 없거나 Event 칸이 비어 있음"), *soundName.ToString());
		return nullptr;
	}

	return row->Event;
}

void USoundSubSystem::PlaySound2D(FName soundName)
{
	if (UAkAudioEvent* event = FindEvent(soundName))
	{
		// 액터 자리에 nullptr 을 주면 Wwise 가 위치 없는 소리로 재생한다.
		event->PostOnActor(nullptr, FOnAkPostEventCallback(), 0, false);
	}
}

void USoundSubSystem::PlaySoundAtLocation(FName soundName, const FVector& location)
{
	if (UAkAudioEvent* event = FindEvent(soundName))
	{
		event->PostAtLocation(location, FRotator::ZeroRotator, FOnAkPostEventCallback(), 0, GetGameInstance());
	}
}

void USoundSubSystem::PlaySoundOnActor(FName soundName, AActor* actor)
{
	// 여기서 actor 가 nullptr 이면 2D 로 새어 나가므로 막는다.
	if (nullptr == actor) return;

	if (UAkAudioEvent* event = FindEvent(soundName))
	{
		event->PostOnActor(actor, FOnAkPostEventCallback(), 0, false);
	}
}