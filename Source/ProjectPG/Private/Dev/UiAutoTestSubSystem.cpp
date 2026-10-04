#include "Dev/UiAutoTestSubSystem.h"

#include "UI/LobbyWidget.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectIterator.h"

void UUiAutoTestSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	StartSeconds = FPlatformTime::Seconds();
	auto AddSteps = [this](const TCHAR* Key, const TCHAR* Action)
	{
		FString Value;
		if (!FParse::Value(FCommandLine::Get(), Key, Value, false))
			return;
		TArray<FString> Parts;
		Value.ParseIntoArray(Parts, TEXT("+"));
		for (const FString& Part : Parts)
		{
			FString Name, Time = Part;
			Part.Split(TEXT("@"), &Name, &Time);
			Steps.Add({ FCString::Atod(*Time), Action, Name });
		}
	};
	AddSteps(TEXT("PGClick="), TEXT("Click"));
	AddSteps(TEXT("PGShot="), TEXT("Shot"));
	AddSteps(TEXT("PGQuitAt="), TEXT("Quit"));
	if (Steps.Num() > 0)
	{
		Steps.Sort([](const FStep& A, const FStep& B) { return A.AtSeconds < B.AtSeconds; });
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UUiAutoTestSubSystem::Tick), 0.1f);
		UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] %d steps"), Steps.Num());
	}
#endif
}

void UUiAutoTestSubSystem::Deinitialize()
{
	if (TickHandle.IsValid())
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	Super::Deinitialize();
}

bool UUiAutoTestSubSystem::Tick(float DeltaTime)
{
	const double Now = FPlatformTime::Seconds() - StartSeconds;
	while (Steps.Num() > 0 && Steps[0].AtSeconds <= Now)
	{
		const FStep Step = Steps[0];
		Steps.RemoveAt(0);
		UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] t=%.1f %s %s"), Now, *Step.Action, *Step.Argument);
		UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		if (Step.Action == TEXT("Click"))
			Click(Step.Argument);
		else if (Step.Action == TEXT("Shot"))
			FScreenshotRequest::RequestScreenshot(true); // UI 까지 포함해 찍는다(Saved/Screenshots/<플랫폼>/ScreenShot*.png)
		else if (Step.Action == TEXT("Quit") && World)
			UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
	}
	return Steps.Num() > 0;
}

// 화면에 떠 있는 로비 위젯을 찾아 그 버튼 함수를 부른다(사람이 누른 것과 같은 길).
void UUiAutoTestSubSystem::Click(const FString& ButtonName)
{
	for (TObjectIterator<ULobbyWidget> It; It; ++It)
	{
		ULobbyWidget* Lobby = *It;
		if (!IsValid(Lobby) || !Lobby->IsInViewport())
			continue;
		if (ButtonName == TEXT("Character"))      Lobby->OnClickedCharacterButton();
		else if (ButtonName == TEXT("GameStart")) Lobby->OnClickedGameStartButton();
		else if (ButtonName == TEXT("Option"))    Lobby->OnClickedOptionButton();
		else if (ButtonName == TEXT("Exit"))      Lobby->OnClickedExitButton();
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] lobby widget not on screen - %s skipped"), *ButtonName);
}
