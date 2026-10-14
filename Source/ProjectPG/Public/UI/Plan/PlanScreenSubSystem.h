// 기획서 화면(설명 창·옵션·매칭)을 띄우고 닫는 곳.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PlanScreenSubSystem.generated.h"

class UUserWidget;

// 기획서 화면 담당.
// 왜 따로 있나: 형님 UI 관리자(UUIManagerSubSystem)는 화면 종류 목록(EUIType)과 BP_GameInstance 등록으로 화면을 연다.
//   거기에 우리 화면을 끼우려면 형님 파일(목록·등록 그래프)을 고쳐야 한다. 그래서 우리 화면은 여기서 따로 연다.
// 하는 일: WBP 종류마다 창을 하나만 만들어 두고(다시 열면 같은 창), 화면에 붙이고 떼기만 한다.
//   어떤 WBP 를 열지는 여는 쪽 WBP 의 칸(예: WBP_Lobby 의 Option Screen Class)에서 고른다.
UCLASS()
class PROJECTPG_API UPlanScreenSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPlanScreenSubSystem* Get(const UObject* WorldContext);

	// 이 WBP 창을 화면에 띄운다(없으면 만든다). ZOrder 가 클수록 위에 그려진다.
	UFUNCTION(BlueprintCallable, Category = "Plan Screen")
	UUserWidget* OpenScreen(TSubclassOf<UUserWidget> ScreenClass, int32 ZOrder = 500);

	// 화면에서 뗀다(창은 버리지 않고 다음에 다시 쓴다).
	UFUNCTION(BlueprintCallable, Category = "Plan Screen")
	void CloseScreen(TSubclassOf<UUserWidget> ScreenClass);

	// 이미 만든 창(없으면 null).
	UFUNCTION(BlueprintPure, Category = "Plan Screen")
	UUserWidget* FindScreen(TSubclassOf<UUserWidget> ScreenClass) const;

private:
	// 맵이 바뀌면 창도 그 맵과 함께 사라지므로, 주인 플레이어가 바뀐 창은 새로 만든다.
	UPROPERTY()
	TMap<TObjectPtr<UClass>, TObjectPtr<UUserWidget>> Screens;
};
