#include "Server/AuthSubSystem.h"
#include "Server/WebSocketSubSystem.h"
#include "Dom/JsonObject.h"

UAuthSubSystem* UAuthSubSystem::Get(UWorld* World)
{
	if (!World) return nullptr;
	if (UGameInstance* GI = World->GetGameInstance())
	{
		return GI->GetSubsystem<UAuthSubSystem>();
	}
	return nullptr;
}

void UAuthSubSystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAuthSubSystem::Deinitialize()
{
	Super::Deinitialize();
}

void UAuthSubSystem::HandleAuthMessage(const FString& MessageType, TSharedPtr<FJsonObject> PayloadObject)
{
	if (!PayloadObject.IsValid()) return;
	UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld());

	if (MessageType == TEXT("CREATE_ACCOUNT_SUCCESS") || MessageType == TEXT("CREATE_ID_SUCCESS"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("계정 생성 성공");
		FString UserId = WS ? WS->GetCurrentUserID() : TEXT("");

		OnCreateIDStatusChanged.Broadcast(true, UserId, Message);
	}
	else if (MessageType == TEXT("CREATE_ACCOUNT_FAIL") || MessageType == TEXT("CREATE_ID_FAILURE"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("계정 생성 실패");

		OnCreateIDStatusChanged.Broadcast(false, TEXT(""), Message);
	}
	else if (MessageType == TEXT("LOGIN_SUCCESS"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("로그인 성공");

		OnLoginStatusChanged.Broadcast(true, true, Message);
	}
	else if (MessageType == TEXT("LOGIN_FAIL") || MessageType == TEXT("LOGIN_FAILURE"))
	{
		FString Message = PayloadObject->HasField(TEXT("message")) ? PayloadObject->GetStringField(TEXT("message")) : TEXT("로그인 실패");

		OnLoginStatusChanged.Broadcast(false, false, Message);
	}
}

void UAuthSubSystem::RequestLogin(const FString& UserID)
{
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		WS->SetCurrentUserID(UserID);
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("userId"), UserID);
		WS->SendJsonMessage(TEXT("LOGIN"), Payload);
	}
}

void UAuthSubSystem::RequestCreateID(const FString& UserId)
{
	if (UWebSocketSubSystem* WS = UWebSocketSubSystem::Get(GetWorld()))
	{
		WS->SetCurrentUserID(UserId);

		FGuid NewPocketGUID = FGuid::NewGuid();
		FGuid NewStashGUID = FGuid::NewGuid();
		FGuid NewMainWeapon = FGuid::NewGuid();
		FGuid NewSubWeapon = FGuid::NewGuid();
		FGuid NewHelMet = FGuid::NewGuid();
		FGuid NewCloth = FGuid::NewGuid();
		FGuid NewPants = FGuid::NewGuid();
		FGuid NewShose = FGuid::NewGuid();
		FGuid NewBackPack = FGuid::NewGuid();
		FGuid NewAccuracy1 = FGuid::NewGuid();
		FGuid NewAccuracy2 = FGuid::NewGuid();

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("userId"), UserId);
		Payload->SetStringField(TEXT("pocketGuid"), NewPocketGUID.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("stashGuid"), NewStashGUID.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("MainWeapon"), NewMainWeapon.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("SubWeapon"), NewSubWeapon.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("HelMet"), NewHelMet.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("Cloth"), NewCloth.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("Pants"), NewPants.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("Shose"), NewShose.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("BackPack"), NewBackPack.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("Accuracy1"), NewAccuracy1.ToString(EGuidFormats::DigitsWithHyphens));
		Payload->SetStringField(TEXT("Accuracy2"), NewAccuracy2.ToString(EGuidFormats::DigitsWithHyphens));

		WS->SendJsonMessage(TEXT("Create_ID"), Payload);
	}
}