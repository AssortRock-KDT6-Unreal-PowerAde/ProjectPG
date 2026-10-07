#include "Dev/UiAutoTestSubSystem.h"

#include "UI/LobbyWidget.h"
#include "UI/ItemWidget.h"
#include "UI/ItemTooltipWidget.h"
#include "UI/ItemContextWidget.h"
#include "Core/UIManagerSubSystem.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "UObject/UObjectIterator.h"
#include "Framework/Application/SlateApplication.h"

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
// Tooltip / Context = 화면에 보이는 아이템 중 가장 큰 것에 설명 창 / 우클릭 메뉴(마우스가 없어서 같은 함수를 직접 부름).
void UUiAutoTestSubSystem::Click(const FString& ButtonName)
{
	if (ButtonName == TEXT("Tooltip") || ButtonName == TEXT("Context") || ButtonName == TEXT("Drag"))
	{
		UItemWidget* Target = nullptr;
		int32 BestArea = -1;
		for (TObjectIterator<UItemWidget> It; It; ++It)
		{
			UItemWidget* Item = *It;
			if (!IsValid(Item) || !Item->IsVisible() || !Item->GetCachedItemData() || Item->GetCachedGeometry().GetLocalSize().X <= 0)
				continue;
			const FIntPoint Size = Item->ItemInstance.GetCurrentGridSize(Item->GetCachedItemData());
			if (Size.X * Size.Y > BestArea)
			{
				BestArea = Size.X * Size.Y;
				Target = Item;
			}
		}
		if (!Target)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] no item widget on screen - %s skipped"), *ButtonName);
			return;
		}
		if (ButtonName == TEXT("Drag"))
		{
			DragItem(Target);
			return;
		}
		if (ButtonName == TEXT("Tooltip"))
		{
			UItemTooltipWidget::ShowFor(Target, Target->ItemInstance, *Target->GetCachedItemData());
		}
		else if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(Target))
		{
			if (UItemContextWidget* Menu = Cast<UItemContextWidget>(UI->OpenUI(EUIType::ItemContext)))
			{
				Menu->SetItem(Target->ItemInstance);
				Menu->UpdateButtonState(Target->ItemInstance.type);
				Menu->SetVisibility(ESlateVisibility::Visible);
				Menu->SetPositionInViewport(Target->GetCachedGeometry().GetAbsolutePosition(), true);
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] %s on item %s"), *ButtonName, *Target->ItemInstance.ItemID.ToString());
		return;
	}

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

// Drag = 화면에 보이는 가장 큰 아이템을 진짜 마우스 입력(Slate)으로 잡아 아래로 3칸 끌어 놓는다.
// 함수를 직접 부르지 않고 마우스 누름·이동·뗌을 보내므로, 마우스가 위젯에 안 닿는 문제(10/7 아이콘 틀)까지 잡힌다.
// 결과: 로그 "[UiAutoTest] Drag ... moved=1" 이면 옮겨졌다.
void UUiAutoTestSubSystem::DragItem(UItemWidget* Target)
{
	FSlateApplication& Slate = FSlateApplication::Get();
	const FGeometry Geometry = Target->GetCachedGeometry();
	const FVector2D TopLeft = Geometry.GetAbsolutePosition();
	const FVector2D Size = Geometry.GetAbsoluteSize();
	const FIntPoint Cells = Target->ItemInstance.GetCurrentGridSize(Target->GetCachedItemData());
	const float Cell = Size.Y / FMath::Max(1, Cells.Y);
	const FVector2D From = TopLeft + Size * 0.5f;
	const FVector2D To = From + FVector2D(0.0f, Cell * (Cells.Y + 2));
	const FGuid Guid = Target->ItemInstance.GUID;
	const uint32 User = Slate.GetUserIndexForMouse();
	const FModifierKeysState Keys;
	FVector2D Last = From;

	Slate.SetCursorPos(From);
	Slate.ProcessMouseMoveEvent(FPointerEvent(User, FSlateApplication::CursorPointerIndex, From, From, TSet<FKey>(), EKeys::Invalid, 0, Keys));
	Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(User, FSlateApplication::CursorPointerIndex, From, From,
		TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::LeftMouseButton, 0, Keys));
	for (int32 Step = 1; Step <= 12; ++Step)
	{
		const FVector2D Now = FMath::Lerp(From, To, Step / 12.0f);
		Slate.SetCursorPos(Now);
		Slate.ProcessMouseMoveEvent(FPointerEvent(User, FSlateApplication::CursorPointerIndex, Now, Last,
			TSet<FKey>({ EKeys::LeftMouseButton }), EKeys::Invalid, 0, Keys));
		Last = Now;
	}
	const bool bDragging = Slate.IsDragDropping();
	Slate.ProcessMouseButtonUpEvent(FPointerEvent(User, FSlateApplication::CursorPointerIndex, To, To,
		TSet<FKey>(), EKeys::LeftMouseButton, 0, Keys));

	// 놓은 뒤 같은 아이템(GUID)의 위젯 자리를 다시 찾는다(격자가 위젯을 새로 만들 수 있어서). 다음 그리기 뒤에 확인.
	TWeakObjectPtr<UUiAutoTestSubSystem> WeakThis(this);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakThis, Guid, TopLeft, bDragging](float) -> bool
	{
		for (TObjectIterator<UItemWidget> It; It; ++It)
		{
			if (!IsValid(*It) || !It->IsVisible() || It->ItemInstance.GUID != Guid || It->GetCachedGeometry().GetLocalSize().X <= 0)
				continue;
			const FVector2D NewTopLeft = It->GetCachedGeometry().GetAbsolutePosition();
			UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] Drag item %s dragStarted=%d moved=%d (%.0f,%.0f)->(%.0f,%.0f)"),
				*It->ItemInstance.ItemID.ToString(), bDragging ? 1 : 0, NewTopLeft.Equals(TopLeft, 2.0) ? 0 : 1,
				TopLeft.X, TopLeft.Y, NewTopLeft.X, NewTopLeft.Y);
			return false;
		}
		UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] Drag: item not found after drop dragStarted=%d"), bDragging ? 1 : 0);
		return false;
	}), 0.5f);
}
