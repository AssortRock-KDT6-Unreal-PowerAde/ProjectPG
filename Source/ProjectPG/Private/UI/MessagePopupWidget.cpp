// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MessagePopupWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"

#include "Core/UIManagerSubSystem.h"
void UMessagePopupWidget::NativeConstruct()
{
	UUIManagerSubSystem* subsystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(subsystem)) return;

	subsystem->OnMessagePopupEvent.RemoveDynamic(this, &UMessagePopupWidget::SetMessageText);
	subsystem->OnMessagePopupEvent.AddDynamic(this, &UMessagePopupWidget::SetMessageText);
	if (false == IsValid(OkButton))
	{
		OkButton->OnClicked.RemoveDynamic(this, &UMessagePopupWidget::OnClickeOkbutton);
		OkButton->OnClicked.AddDynamic(this, &UMessagePopupWidget::OnClickeOkbutton);
	}
}

void UMessagePopupWidget::SetMessageText(const FString& msg, int32 num)
{
	if(IsValid(MessageText)) MessageText->SetText(FText::FromString(msg));

	if (num == 1)
	{
		OkButton->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		OkButton->SetVisibility(ESlateVisibility::Hidden);

		float LifeTime = 3.0f;
		GetWorld()->GetTimerManager().SetTimer(
			DestroyTimerHandle,
			this,
			&UMessagePopupWidget::OnLifetimeExpired,
			LifeTime,
			false
		);
	}
}

void UMessagePopupWidget::OnClickeOkbutton()
{
	this->SetVisibility(ESlateVisibility::Collapsed);
}
void UMessagePopupWidget::OnLifetimeExpired()
{
	UUIManagerSubSystem* subsystem = UUIManagerSubSystem::Get(GetWorld());
	if (false == IsValid(subsystem)) return;
	subsystem->OnPopupClosed.Broadcast();

	RemoveFromParent();



}