// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MessagePopupWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Server/WebSocketSubSystem.h"
#include "Server/MatchmakingSubSystem.h"
#include "Core/UIManagerSubSystem.h"
void UMessagePopupWidget::NativeConstruct()
{
	UUIManagerSubSystem* subsystem = UUIManagerSubSystem::Get(GetWorld());
	UWebSocketSubSystem* server = UWebSocketSubSystem::Get(GetWorld());
	if (false == IsValid(subsystem)) return;

	subsystem->OnMessagePopupEvent.RemoveDynamic(this, &UMessagePopupWidget::SetMessageText);
	subsystem->OnMessagePopupEvent.AddDynamic(this, &UMessagePopupWidget::SetMessageText);	

	if (IsValid(OkButton))
	{
		OkButton->OnClicked.RemoveDynamic(this, &UMessagePopupWidget::OnClickeOkbutton);
		OkButton->OnClicked.AddDynamic(this, &UMessagePopupWidget::OnClickeOkbutton);
	}
	UMatchmakingSubSystem* matchSubSystem = UMatchmakingSubSystem::Get(GetWorld());
	if (!IsValid(matchSubSystem)) return;
	matchSubSystem->OnMatchStatusChanged.RemoveDynamic(this,&UMessagePopupWidget::SetMessageWebsocket);
	matchSubSystem->OnMatchStatusChanged.AddDynamic(this,&UMessagePopupWidget::SetMessageWebsocket);

}

void UMessagePopupWidget::SetMessageText(const FString& msg, int32 num)
{
	if(IsValid(MessageText)) MessageText->SetText(FText::FromString(msg));

	EMessageBoxType Type = static_cast<EMessageBoxType>(num);


	if (Type == EMessageBoxType::None)
	{
		OkButton->SetVisibility(ESlateVisibility::Hidden);
		MessageType = EMessageBoxType::Timer;

		GetWorld()->GetTimerManager().SetTimer(
			DestroyTimerHandle,
			this,
			&UMessagePopupWidget::OnLifetimeExpired,
			LifeTime,
			false
		);

	}
	else if (Type == EMessageBoxType::OneButton)
	{
		OkButton->SetVisibility(ESlateVisibility::Visible);
	}
	else if (Type == EMessageBoxType::ServerClose)
	{
		MessageType = EMessageBoxType::ServerClose;
	}
	else if (Type == EMessageBoxType::ServerWating)
	{
		MessageType = EMessageBoxType::ServerWating;
	}
	MessageType = Type;

}

void UMessagePopupWidget::SetMessageWebsocket(const FString& StatusType, const FString& Message)
{

	if (StatusType == "WAITING")
	{
		MessageType = EMessageBoxType::ServerWating;
	}
	if (StatusType == "CancelMatch")
	{
		MessageType = EMessageBoxType::ServerClose;
	}
	if (StatusType == "Match_CANCELLED")
	{
		MessageType = EMessageBoxType::None;
	}
	int8 Type = static_cast<int8>(MessageType);
	SetMessageText(Message, Type);


}



void UMessagePopupWidget::OnClickeOkbutton()
{
	int32 Type = static_cast<int32>(MessageType);

	UE_LOG(LogTemp, Warning, TEXT("CancleButton %d"), Type);
	switch (MessageType)
	{
	case EMessageBoxType::OneButton:
		//this->SetVisibility(ESlateVisibility::Collapsed);
		break;
	case EMessageBoxType::Timer:
		break;
	case EMessageBoxType::ServerClose:
		break;
		
	case EMessageBoxType::ServerWating: 
	{
		UMatchmakingSubSystem* subsystem = UMatchmakingSubSystem::Get(GetWorld());
		if (!IsValid(subsystem)) return;
		subsystem->RequestCancleMatch();
		UE_LOG(LogTemp, Warning, TEXT("Cancle Button"));

		break;
	}
	case EMessageBoxType::None:
		//this->SetVisibility(ESlateVisibility::Collapsed);
		break;
	default:
		break;
	}

}
void UMessagePopupWidget::OnLifetimeExpired()
{
	UUIManagerSubSystem* subsystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(subsystem)) return;
	subsystem->OnPopupClosed.Broadcast();
	RemoveFromParent();



}