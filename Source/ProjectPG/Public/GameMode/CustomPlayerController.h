// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "CustomPlayerController.generated.h"

/**
 * 
 */
UCLASS()
class PROJECTPG_API ACustomPlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	virtual void BeginPlay() override;
	void ToggleInventory();
	void SetupInputComponent() override;
	void OnRotateKey();
	// 오른쪽 클릭 = 분홍 가발 광선(가발이 없으면 아무 일도 없다). I 키와 같은 방식으로 컨트롤러에 키를 묶었다.
	void OnWigBeamKey();
	// 왼쪽 클릭도 가발 부품에 넘긴다 — 어느 버튼으로 쏠지는 부품의 FireKey(기본 왼쪽, 9/23).
	void OnWigBeamLeftKey();
	// V: 1인칭 / 3인칭 전환. 전함 함교에서 창밖을 보려면 1인칭이 필요하다(9/20 사용자 요구: 둘 다 되게).
	void OnToggleViewKey();
	// F: 상호작용 예비 바인딩(팀 입력 표에 F 가 없어도 줍기·대화가 되게).
	void OnInteractKey();
	bool bFirstPersonView = false;
	float ThirdPersonArmLength = 0.0f; // 처음 본 3인칭 팔 길이. 되돌릴 때 쓴다.
};
