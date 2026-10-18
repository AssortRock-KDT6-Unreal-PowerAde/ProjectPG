#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PGLoadingScreenSubsystem.generated.h"

class SWidget;
class UPGLoadingScreenWidget;
class UUserWidget;

// 로딩 창을 "언제 띄우고 언제 닫는지" 만 정한다. 모양·글자·사라지는 효과는 WBP_PGLoadingScreen 이 맡는다.
// 흐름 그림 → MapBuilder/README.md "로딩 창과 시작 무적"
// 띄우기 = 맵(AMapBuilder::BeginPlay, 화면 있는 쪽만).
// 닫기 = 두 가지가 다 됐을 때: 서버가 "자리에 섰다"(UMapSeatStatus) + 내 컴퓨터의 맵도 다 지어짐(끼임 정리 끝, UMapPointPlanner).
//   왜 둘 다: 들어온 사람은 맵을 자기 컴퓨터에서 따로 짓는다. 서버가 먼저 세워도 내 맵이 아직이면 빈 땅이 보인다(시험에서 약 4초 차이).
// 왜 게임 인스턴스 쪽 부품인가: 레벨보다 오래 살아서, 맵 액터가 없어져도 창을 확실히 치울 수 있다.
// ProjectTest2 의 같은 이름 파일에서 가져왔고, 시작 로고·안내 문구·C++ 흐려지기는 뺐다.
UCLASS()
class UPGLoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPGLoadingScreenSubsystem* Get(const UObject* WorldContext);

	virtual void Deinitialize() override;

	// 로딩 창을 띄운다. WidgetClass 는 DA_MapAssets 의 칸에서 온다(비었으면 Error 로그만 남기고 안 띄움).
	// 이 판에서 두 가지가 이미 다 됐으면 띄우지 않는다.
	void ShowLoading(UWorld* World, TSoftClassPtr<UUserWidget> WidgetClass);
	// 서버가 자리에 세웠다 / 내 맵을 다 지었다. 둘 다 되면 닫는다. 아직 안 띄웠으면 기억만 한다.
	void NotifySeated(UWorld* World);
	void NotifyMapReady(UWorld* World);

private:
	void HideIfReady();
	// 닫기 시작: WBP 의 HideAnim 이 끝나면 떼어 낸다(애니메이션이 없으면 바로).
	void HideLoading(const TCHAR* Why);
	void RemoveWidget();
	// 한 번만 부르는 예약(매 프레임 확인 아님). Delay 초 뒤 Action 을 한 번 부른다.
	void Schedule(FTSTicker::FDelegateHandle& Handle, float Delay, TFunction<void()> Action);
	void CancelSchedules();
	// 창을 띄운 판(레벨)이 끝나면 창도 치운다(엔진이 화면 위젯을 다 떼지만, 우리 기록도 지워야 다음 판에 다시 뜬다).
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	UPROPERTY(Transient)
	TObjectPtr<UPGLoadingScreenWidget> Widget;
	TSharedPtr<SWidget> SlateRoot;

	// "섰다" 가 끝내 안 올 때 닫는 예약, 사라지는 애니메이션이 끝나면 떼는 예약.
	FTSTicker::FDelegateHandle WaitLimitHandle;
	FTSTicker::FDelegateHandle RemoveHandle;
	FDelegateHandle WorldCleanupHandle;

	// 창을 띄운 판, "섰다" 를 이미 받은 판, 맵을 다 지은 판.
	TWeakObjectPtr<UWorld> ShownWorld;
	TWeakObjectPtr<UWorld> SeatedWorld;
	TWeakObjectPtr<UWorld> MapReadyWorld;

	bool bHiding = false;
	double LoadingShownAt = 0.0;
};
