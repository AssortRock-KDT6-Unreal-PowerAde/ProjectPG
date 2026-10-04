// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LobbyUIFlowController.generated.h"

// 로비 화면 흐름 담당.
// 예전: 로그인 → 아이디 만들기 → 서버 인벤토리 받기 → 로비. 10/4 팀 합의로 웹 서버를 빼서
//       "시작 짐 넣기 → 로비 띄우기" 만 남았다(로그인·아이디 만들기 함수는 지움).
UCLASS()
class PROJECTPG_API ULobbyUIFlowController : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static ULobbyUIFlowController* Get(const UObject* worldContext);
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION()
	void BeginSetting();//해당 로비씬으로 왔을시 최초 호출

	UFUNCTION()
	void ShowLobby();
};
