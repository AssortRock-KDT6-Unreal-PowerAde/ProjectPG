// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "LobbyUIFlowController.generated.h"

// 로비 화면 흐름 담당.
// 예전: 로그인 → 아이디 만들기 → 서버 인벤토리 받기 → 로비. 10/4 팀 합의로 웹 서버를 빼서
//       "시작 짐 넣기 → 로비 띄우기" 만 남았다(로그인·아이디 만들기 함수는 지움).
// 10/6: 로비 캐릭터 세우기·로비 카메라 바꾸기도 맡는다(둘 다 "로비 화면에 무엇이 보이나" 의 흐름).
UCLASS(Config = Game)
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

	// 로비 카메라 바꾸기. 레벨에 놓인 카메라 중 이 이름표(태그)를 가진 것으로 BlendSeconds 동안 부드럽게 옮긴다.
	// 게임에서: 캐릭터 화면을 열면 캐릭터 정면(LobbyCamera_Character), 뒤로가기면 메뉴 자리(LobbyCamera_Menu).
	// 카메라 자리·각도는 레벨(L_Title)에서 정한다 — 여기는 "그 이름의 카메라로 바꿔라" 만 한다. 없으면 아무것도 안 함.
	UFUNCTION(BlueprintCallable, Category = "Lobby")
	void FocusCamera(FName CameraTag, float BlendSeconds = 0.6f);

private:
	// 로비 캐릭터 세우기. 레벨의 자리 표시(CharacterSpotTag 태그를 가진 액터) 위치에,
	// 게임 맵 게임모드(CharacterSourceGameMode)의 기본 캐릭터 클래스를 세운다.
	// 왜: 캐릭터 담당 팀원이 게임 캐릭터를 바꾸면(BP_GameMode 의 Default Pawn Class) 로비 캐릭터도 따라 바뀌게 — 고칠 곳이 한 곳.
	void SpawnLobbyCharacter();

	// DefaultGame.ini [/Script/ProjectPG.LobbyUIFlowController] 에서 바꾼다.
	UPROPERTY(Config)
	FSoftClassPath CharacterSourceGameMode = FSoftClassPath(TEXT("/Game/PG/Blueprint/GameMode/BP_GameMode.BP_GameMode_C"));

	UPROPERTY(Config)
	FName CharacterSpotTag = TEXT("LobbyCharacterSpot");

	TWeakObjectPtr<APawn> LobbyCharacter;
};
