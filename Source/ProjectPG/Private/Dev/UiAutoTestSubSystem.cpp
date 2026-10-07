#include "Dev/UiAutoTestSubSystem.h"

#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/ComboBoxString.h"
#include "Components/InventoryComponent.h"
#include "Core/UIManagerSubSystem.h"
#include "Dom/JsonObject.h"
#include "Server/MatchmakingSubSystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "HAL/FileManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UI/FitIconItemWidget.h"
#include "UnrealClient.h"
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
			Part.Split(TEXT("@"), &Name, &Time, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			Steps.Add({ FCString::Atod(*Time), Action, Name });
		}
	};
	AddSteps(TEXT("PGStep="), TEXT("Step"));
	AddSteps(TEXT("PGShot="), TEXT("Shot"));
	AddSteps(TEXT("PGQuitAt="), TEXT("Quit"));
	if (Steps.Num() > 0)
	{
		Steps.StableSort([](const FStep& A, const FStep& B) { return A.AtSeconds < B.AtSeconds; });
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
		if (Step.Action == TEXT("Step"))
		{
			RunStep(Step.Argument);
		}
		else if (Step.Action == TEXT("Shot"))
		{
			// UI 까지 포함해 찍는다.
			const FString Dir = FPaths::ProjectSavedDir() / TEXT("UiShots");
			IFileManager::Get().MakeDirectory(*Dir, true);
			FScreenshotRequest::RequestScreenshot(Dir / (Step.Argument.IsEmpty() ? FString(TEXT("shot")) : Step.Argument) + TEXT(".png"), true, false);
		}
		else if (Step.Action == TEXT("Quit") && World)
		{
			UKismetSystemLibrary::QuitGame(World, nullptr, EQuitPreference::Quit, false);
		}
	}
	return Steps.Num() > 0;
}

void UUiAutoTestSubSystem::RunStep(const FString& Step)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (Step == TEXT("Lobby"))
	{
		if (UUIManagerSubSystem* UI = UUIManagerSubSystem::Get(World))
		{
			// 시험 전용: BP_GameInstance 가 화면을 등록하지 못한 경우(Init 이벤트 선이 끊김) 같은 WBP 를 여기서 등록해 둔다.
			// 게임 자체의 등록은 형님 BP_GameInstance 몫이라 고치지 않는다(출시 빌드에는 이 코드가 없다).
			if (!UI->GetUIClass(EUIType::Lobby))
			{
				const TPair<EUIType, const TCHAR*> Screens[] = {
					{ EUIType::Inventory, TEXT("WBP_InventoryGrid") }, { EUIType::MessagePopup, TEXT("WBP_MessagePopup") },
					{ EUIType::Lobby, TEXT("WBP_Lobby") }, { EUIType::Character, TEXT("WBP_CharacterWidget") },
					{ EUIType::ItemContext, TEXT("WBP_ItemContextWidget") }, { EUIType::BackPackPopup, TEXT("WBP_BagPopupWindow") } };
				for (const TPair<EUIType, const TCHAR*>& Screen : Screens)
				{
					const FString Path = FString::Printf(TEXT("/Game/PG/Blueprint/UI/%s.%s_C"), Screen.Value, Screen.Value);
					UI->RegisterUIClass(Screen.Key, LoadClass<UUserWidget>(nullptr, *Path));
				}
				UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] screens were not registered by BP_GameInstance - registered for this test only"));
			}
			UI->CloseAllUI();
			UUserWidget* Lobby = UI->OpenUI(EUIType::Lobby);
			UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] lobby=%s class=%s in_viewport=%d"), Lobby ? *Lobby->GetName() : TEXT("null"),
				UI->GetUIClass(EUIType::Lobby) ? *UI->GetUIClass(EUIType::Lobby)->GetName() : TEXT("not registered"), Lobby && Lobby->IsInViewport() ? 1 : 0);
		}
	}
	else if (Step == TEXT("FakeItems"))
	{
		FakeItems();
	}
	else if (Step.StartsWith(TEXT("Btn:")))
	{
		PressButton(Step.Mid(4));
	}
	else if (Step.StartsWith(TEXT("FakeMatch:")))
	{
		// 웹 서버가 보내는 매칭 메시지를 흉내 내 형님 매칭 처리 함수(HandleMatchMessage)에 그대로 넣는다.
		// 예: FakeMatch:2/4 = "WAITING_FOR_MATCH"(2명/4명), FakeMatch:Starting = "SERVER_STARTING", FakeMatch:Cancelled = "MATCH_CANCELLED"
		const FString Arg = Step.Mid(10);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		FString Type;
		FString Current, Target;
		if (Arg.Split(TEXT("/"), &Current, &Target))
		{
			Type = TEXT("WAITING_FOR_MATCH");
			Payload->SetStringField(TEXT("message"), TEXT("waiting"));
			Payload->SetNumberField(TEXT("currentQueueCount"), FCString::Atoi(*Current));
			Payload->SetNumberField(TEXT("targetCount"), FCString::Atoi(*Target));
		}
		else
		{
			Type = Arg == TEXT("Starting") ? TEXT("SERVER_STARTING") : TEXT("MATCH_CANCELLED");
			Payload->SetStringField(TEXT("message"), Arg);
		}
		if (UMatchmakingSubSystem* Match = UMatchmakingSubSystem::Get(World))
			Match->HandleMatchMessage(Type, Payload);
	}
	else if (Step.StartsWith(TEXT("Combo:")))
	{
		FString Name, Index;
		Step.Mid(6).Split(TEXT("="), &Name, &Index);
		PickCombo(Name, FCString::Atoi(*Index));
	}
	else if (Step == TEXT("Tooltip") || Step == TEXT("Context"))
	{
		UFitIconItemWidget* Target = FindBiggestItem();
		if (!Target)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] no item on screen - %s skipped"), *Step);
			return;
		}
		// 마우스를 아이템 가운데로 옮겨 두고(설명 창·메뉴가 그 옆에 뜨게) 진짜 처리 함수를 부른다.
		const FGeometry Geometry = Target->GetCachedGeometry();
		const FVector2D Center = Geometry.GetAbsolutePosition() + Geometry.GetAbsoluteSize() * 0.5f;
		if (APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr)
			PC->SetMouseLocation(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
		if (Step == TEXT("Tooltip"))
		{
			Target->ShowTooltip();
		}
		else
		{
			const FPointerEvent Event(0, Center, Center, TSet<FKey>({ EKeys::RightMouseButton }), EKeys::RightMouseButton, 0.0f, FModifierKeysState());
			Target->NativeOnMouseButtonDown(Geometry, Event);
		}
		UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] %s on item %s"), *Step, *Target->ItemInstance.ItemID.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] unknown step %s"), *Step);
	}
}

// 화면에 붙어 있는 위젯 중 이 이름의 버튼을 찾아 누른다(사람이 누른 것과 같은 길: 버튼의 OnClicked).
void UUiAutoTestSubSystem::PressButton(const FString& Name)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || !Widget->IsInViewport() || Widget->GetWorld() != World)
			continue;
		if (UButton* Button = Cast<UButton>(Widget->GetWidgetFromName(FName(*Name))))
		{
			if (!Button->IsVisible() || !Button->GetIsEnabled())
			{
				UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] button %s in %s is hidden/disabled"), *Name, *Widget->GetClass()->GetName());
				continue;
			}
			UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] press %s in %s"), *Name, *Widget->GetClass()->GetName());
			Button->OnClicked.Broadcast();
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] button %s not on screen"), *Name);
}

void UUiAutoTestSubSystem::PickCombo(const FString& Name, int32 Index)
{
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (!IsValid(Widget) || !Widget->IsInViewport())
			continue;
		if (UComboBoxString* Combo = Cast<UComboBoxString>(Widget->GetWidgetFromName(FName(*Name))))
		{
			Combo->SetSelectedIndex(Index); // 고르기 바뀜 알림이 함께 간다.
			UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] combo %s = %d (%s)"), *Name, Index, *Combo->GetSelectedOption());
			return;
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] combo %s not on screen"), *Name);
}

// 웹 서버가 없으면 로비 짐이 비어 있다. 내 창고(10x10)·주머니(5x4)를 만들고 시험용 아이템을 넣는다.
// 형님 인벤토리의 보통 함수(AddItemByID)로만 넣는다. 로비(서버 관리가 아닌 곳)에서만 들어간다.
void UUiAutoTestSubSystem::FakeItems()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	UInventoryComponent* Inventory = PC && PC->PlayerState ? PC->PlayerState->FindComponentByClass<UInventoryComponent>() : nullptr;
	if (!Inventory)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UiAutoTest] FakeItems: no inventory"));
		return;
	}
	if (!Inventory->GetStashInventoryID().IsValid())
	{
		const FGuid Stash = FGuid::NewGuid();
		Inventory->RegisterContainer(Stash, FIntPoint(10, 10));
		Inventory->SetStashInventoryID(Stash);
	}
	if (!Inventory->GetPocketInventoryID().IsValid())
	{
		const FGuid Pocket = FGuid::NewGuid();
		Inventory->RegisterContainer(Pocket, FIntPoint(5, 4));
		Inventory->SetPocketInventoryID(Pocket);
	}
	struct FFake { const TCHAR* Id; int32 Count; bool bPocket; };
	const FFake Fakes[] = {
		{ TEXT("1010"), 1, false }, { TEXT("1007"), 1, false }, { TEXT("1001"), 1, false }, { TEXT("2003"), 1, false },
		{ TEXT("2001"), 1, false }, { TEXT("3011"), 1, false }, { TEXT("3002"), 60, false }, { TEXT("3001"), 45, false },
		{ TEXT("3007"), 5, false }, { TEXT("3005"), 1, true }, { TEXT("1017"), 1, false },
		{ TEXT("3012"), 1, true }, { TEXT("3008"), 4, true } };
	int32 Added = 0;
	for (const FFake& Fake : Fakes)
	{
		const FGuid Target = Fake.bPocket ? Inventory->GetPocketInventoryID() : Inventory->GetStashInventoryID();
		Added += Inventory->AddItemByID(FName(Fake.Id), Target, Fake.Count) ? 1 : 0;
	}
	UE_LOG(LogTemp, Display, TEXT("[UiAutoTest] FakeItems added %d/%d"), Added, static_cast<int32>(UE_ARRAY_COUNT(Fakes)));
}

UFitIconItemWidget* UUiAutoTestSubSystem::FindBiggestItem() const
{
	UFitIconItemWidget* Best = nullptr;
	int32 BestArea = -1;
	for (TObjectIterator<UFitIconItemWidget> It; It; ++It)
	{
		UFitIconItemWidget* Item = *It;
		if (!IsValid(Item) || !Item->IsVisible() || !Item->GetCachedItemData() || Item->GetCachedGeometry().GetLocalSize().X <= 0)
			continue;
		const FIntPoint Size = Item->ItemInstance.GetCurrentGridSize(Item->GetCachedItemData());
		// 그 자리에서 돌릴 수 있는(정사각형이 아니고 돌린 모양이 들어갈 자리가 있는) 아이템을 먼저 고른다(돌리기 시험용).
		const UInventoryComponent* Owner = Item->OwnerInventoryComp.Get();
		const bool bRotatable = Size.X != Size.Y && Owner && const_cast<UInventoryComponent*>(Owner)->CanPlaceItemByGuid(
			Item->OwnerInventoryGUID, Item->ItemInstance.ItemID, Item->ItemInstance.Position, !Item->ItemInstance.bIsRotated, Item->ItemInstance.GUID);
		const int32 Area = Size.X * Size.Y + (Size.X != Size.Y ? 100 : 0) + (bRotatable ? 1000 : 0);
		if (Area > BestArea)
		{
			BestArea = Area;
			Best = Item;
		}
	}
	return Best;
}
