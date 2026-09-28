// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Interactable.generated.h"

// This class does not need to be modified.
UINTERFACE(MinimalAPI)
class UInteractable : public UInterface
{
	GENERATED_BODY()
};

/**
 * 
 */
class PROJECTPG_API IInteractable
{
	GENERATED_BODY()

	// Add interface functions to this class. This is the class that will be inherited to implement this interface.
public:
	// 상호작용
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
	void Interact(APawn* Interactor);
	// bIsOpen(열림 상태)이 아니라 "지금 만질 수 있는가" (잠김·사용중 등)
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
	bool CanInteract(APawn* Interactor) const;
	// 상호작용 화면에 보여줄 안내 문구 ("문 열기", "무기 줍기" 등)
	UFUNCTION(BlueprintNativeEvent, Category = "Interaction")
	FText GetInteractionPrompt() const;

	// F 를 몇 초 눌러야 하는가(0 = 바로). 상호작용 컴포넌트가 진행 바를 그릴 때 읽는다.
	// 왜 여기 있나(2026-09-26 SOLID — 다형성): 전에는 컴포넌트가 Cast<APGInteractableActorBase> 로 부모 클래스를 뚫고 읽어서,
	//   그 부모를 안 쓰는 대상(탈것 등)은 유지 시간을 가질 수 없었다. 이제 "상호작용 약속" 의 일부라 누구든 답할 수 있다.
	// 일반 가상 함수(블루프린트 이벤트 아님)라 기존 구현(팀 코드 포함)은 고치지 않아도 기본값 0 을 쓴다.
	virtual float GetHoldSeconds() const { return 0.0f; }
};
