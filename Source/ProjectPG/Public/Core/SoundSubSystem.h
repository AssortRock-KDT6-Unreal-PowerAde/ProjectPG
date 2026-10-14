// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "SoundSubSystem.generated.h"

/**
 * 
 */
class UAkAudioEvent;

UCLASS()
class PROJECTPG_API USoundSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
	
public:
	static USoundSubSystem* Get(const UObject* worldContext);

	// 다른 코드에서 한 줄로 UI 소리를 낼 때 쓴다. 창구를 못 찾으면 조용히 넘어간다.
	static void PlayUISound(const UObject* worldContext, FName soundName);

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	
	// 화면소리(UI). 위치가 없어서 어디서 듣든 똑같이 들린다. 
	UFUNCTION(BlueprintCallable,Category = "Sound")
	void PlaySound2D(FName soundName);
	
	// 월드의 한 지점에서 나는 소리. 한 번 나고 끝나는 소리용.
	UFUNCTION(BlueprintCallable,Category = "Sound")
	void PlaySoundAtLocation(FName soundName, const FVector& location);
	
	// 액터에 붙어서 따라다니는 소리.
	UFUNCTION(BlueprintCallable, Category = "Sound")
	void PlaySoundOnActor(FName soundName, AActor* actor);
private:
	// 소리 이름으로 표에서 Wwise 이벤트를 찾는다. 없거나 전용 서버면 nullptr.
	UAkAudioEvent* FindEvent(FName soundName) const;
	// 마우스 클릭을 지켜보다가 버튼 위에서 눌리면 클릭 소리를 내는 감시자.
	TSharedPtr<class FUIClickSoundProcessor> _clickSoundProcessor;
};
