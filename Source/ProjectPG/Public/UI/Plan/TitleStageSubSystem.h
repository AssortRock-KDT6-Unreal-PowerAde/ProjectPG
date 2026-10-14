// 타이틀(로비) 장면: 배경 마을에 팀 캐릭터를 세우고 카메라를 정한다.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TitleStageSubSystem.generated.h"

class APawn;

// 타이틀 장면 담당.
// 게임에서: L_Title(호숫가 마을 배경)을 열면 마을 흙길에 게임 캐릭터가 서 있고, 화면은 메뉴 카메라로 본다.
//   로비 메뉴의 "캐릭터"를 누르면 캐릭터 앞 카메라로 부드럽게 옮겨 가고, 캐릭터 창을 닫으면 메뉴 카메라로 돌아온다.
// 레벨에 이름표가 붙은 것만 쓴다(L_Title):
//   - 캐릭터 설 자리: 태그 LobbyCharacterSpot (Title_CharacterSpot)
//   - 카메라: 태그 LobbyCamera_Menu, LobbyCamera_Character
//   이름표가 없는 레벨(게임 맵·시험 맵)에서는 아무것도 안 한다.
// 왜 형님 로비 코드에 안 넣었나: 로비 흐름(로그인·웹 서버)은 형님 몫이라 손대지 않고, 보이는 장면만 따로 둔다.
// 캐릭터 종류: 게임 캐릭터와 같게, 게임모드(기본 GM_InGame)의 Default Pawn Class 를 그대로 쓴다.
//   바꾸려면 DefaultGame.ini [/Script/ProjectPG.TitleStageSubSystem] CharacterSourceGameMode=.
UCLASS(Config = Game)
class PROJECTPG_API UTitleStageSubSystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UTitleStageSubSystem* Get(const UObject* WorldContext);

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	// 이 이름표가 붙은 카메라로 화면을 옮긴다(BlendSeconds 동안 부드럽게). 없으면 아무것도 안 한다.
	UFUNCTION(BlueprintCallable, Category = "Title Stage")
	void FocusCamera(FName CameraTag, float BlendSeconds = 0.6f);

	// 이 레벨이 타이틀 장면인가(캐릭터 자리 이름표가 있나).
	bool IsTitleStage() const { return bIsTitleStage; }

private:
	void SetupStage();
	void SpawnTitleCharacter(AActor* Spot);

	UPROPERTY(Config)
	FSoftClassPath CharacterSourceGameMode = FSoftClassPath(TEXT("/Game/PG/Blueprint/GM_InGame.GM_InGame_C"));

	UPROPERTY(Config)
	FName CharacterSpotTag = TEXT("LobbyCharacterSpot");

	UPROPERTY(Config)
	FName MenuCameraTag = TEXT("LobbyCamera_Menu");

	TWeakObjectPtr<APawn> TitleCharacter;
	bool bIsTitleStage = false;
};
