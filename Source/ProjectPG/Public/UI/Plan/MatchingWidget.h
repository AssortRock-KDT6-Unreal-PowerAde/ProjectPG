// 매칭 화면(기획서 1.3.1): "매칭중..." 글자, 취소 버튼, 진행 막대.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MatchingWidget.generated.h"

class UButton;
class UProgressBar;
class UTextBlock;

// 매칭 화면.
// 게임에서: 로비 메뉴의 "게임 시작"을 누르면 뜬다. 가운데 "매칭중... (2/4)" 같은 글자, 사람이 모이는 만큼 차는 막대, 취소 버튼.
// 매칭 자체는 형님 웹 서버 매칭(UMatchmakingSubSystem)이 한다. 이 화면은 그걸 보여 주기만 한다:
//   - 형님 매칭이 알려 주는 상태(OnMatchStatusChanged: WAITING / SERVER_STARTING / CancelMatch / Match_CANCELLED)를 글자로,
//   - WAITING 글자 끝의 "(지금 인원/필요 인원)"을 막대로(인원을 모르면 막대가 왔다 갔다 한다),
//   - 취소 버튼 → 형님 RequestCancleMatch.
//   서버 준비가 끝나면(JOIN_SERVER) 형님 코드가 게임 서버로 이동하고, 맵이 바뀌며 이 화면도 같이 사라진다.
// 웹 서버에 연결되어 있지 않으면 "서버에 연결되어 있지 않습니다"를 띄우고, 취소를 누르면 바로 닫힌다.
// C++ 은 동작만. 모양(배치·색·글꼴)은 WBP_Matching 에서. 칸 이름이 같으면 붙는다(없으면 그 부분만 안 보임).
UCLASS()
class PROJECTPG_API UMatchingWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> StatusText;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> CancelButton;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> MatchProgress;

	// 형님 매칭 상태 이름 → 띄울 글자. WBP_Matching 에서 고친다(문구를 바꿔도 빌드 필요 없음).
	// 이름은 형님 UMatchmakingSubSystem 이 보내는 그대로다. "Start" 는 화면을 막 열었을 때, "NotConnected" 는 웹 서버 연결이 없을 때.
	UPROPERTY(EditAnywhere, Category = "Matching")
	TMap<FString, FText> StatusTexts = {
		{ TEXT("Start"),           INVTEXT("매칭중...") },
		{ TEXT("WAITING"),         INVTEXT("매칭중...") },
		{ TEXT("SERVER_STARTING"), INVTEXT("게임 서버를 여는 중...") },
		{ TEXT("CancelMatch"),     INVTEXT("매칭을 취소하는 중...") },
		{ TEXT("Match_CANCELLED"), INVTEXT("매칭을 취소했습니다") },
		{ TEXT("NotConnected"),    INVTEXT("서버에 연결되어 있지 않습니다") } };

	// 인원 수를 글자 뒤에 붙이는 모양. {Text} = 위 글자, {Current} = 지금 인원, {Target} = 필요 인원.
	UPROPERTY(EditAnywhere, Category = "Matching")
	FText CountFormat = INVTEXT("{Text} ({Current}/{Target})");

	// 취소된 뒤 창을 닫기까지(초).
	UPROPERTY(EditAnywhere, Category = "Matching", meta = (ClampMin = "0"))
	float CloseDelaySeconds = 1.0f;

	// 취소를 보냈는데 서버 답이 이만큼(초) 없으면 그냥 닫는다.
	UPROPERTY(EditAnywhere, Category = "Matching", meta = (ClampMin = "0"))
	float CancelTimeoutSeconds = 5.0f;

private:
	UFUNCTION()
	void HandleStatusChanged(const FString& StatusType, const FString& Message);

	UFUNCTION()
	void HandleCancelClicked();

	void SetStatus(const FString& Key, int32 Current = -1, int32 Target = -1);
	void CloseAfter(float Seconds);

	FTimerHandle CloseTimer;
};
