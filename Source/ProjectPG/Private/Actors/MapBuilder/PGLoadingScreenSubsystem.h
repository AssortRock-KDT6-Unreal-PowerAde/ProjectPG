#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "PGLoadingScreenSubsystem.generated.h"

class SWidget;
class UPGLoadingScreenWidget;

// 플레이어가 시작 구역 자리에 서기 전까지 화면을 가리는 로딩 창. 흐름 그림 → MapBuilder/README.md "로딩 창과 시작 무적"
// 누가 부르나: 띄우기 = 맵(AMapBuilder::BeginPlay, 화면 있는 쪽만).
//   닫기 = 두 가지가 다 됐을 때: 서버가 "자리에 섰다"(UMapSeatStatus) + 내 컴퓨터의 맵도 다 지어짐(끼임 정리 끝, UMapPointPlanner).
//   왜 둘 다: 들어온 사람은 맵을 자기 컴퓨터에서 따로 짓는다. 서버가 먼저 세워도 내 맵이 아직이면 빈 땅이 보인다(시험에서 약 4초 차이).
// 왜 게임 인스턴스 쪽 부품인가: 레벨보다 오래 살아서, 맵 액터가 없어져도 창을 확실히 치울 수 있다.
// ProjectTest2 의 같은 이름 파일에서 가져왔고, 시작 로고·안내 문구 부분은 뺐다(그쪽 다른 파일에 기대고 있어서).
UCLASS()
class UPGLoadingScreenSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	static UPGLoadingScreenSubsystem* Get(const UObject* WorldContext);

	virtual void Deinitialize() override;

	// 로딩 창을 띄운다. WidgetClass 는 DA_MapAssets 의 칸에서 온다(비었으면 Error 로그만 남기고 안 띄움).
	// 이 판에서 두 가지가 이미 다 됐으면 띄우지 않는다.
	void ShowLoading(UWorld* World, TSoftClassPtr<UUserWidget> WidgetClass, const FText& Message);
	// 서버가 자리에 세웠다 / 내 맵을 다 지었다. 둘 다 되면 0.6초 동안 흐려지며 닫힌다. 아직 안 띄웠으면 기억만 한다.
	void NotifySeated(UWorld* World);
	void NotifyMapReady(UWorld* World);

private:
	void HideIfReady();
	void HideLoading(const TCHAR* Why);
	void RemoveWidget();
	// 흐려지기·시간 제한 확인. 창이 떠 있는 동안만 등록한다(흐려지는 모양이 매 프레임 바뀌어야 해서).
	bool TickLoading(float DeltaTime);
	void StopTicker();
	// 창을 띄운 판(레벨)이 끝나면 창도 치운다(엔진이 화면 위젯을 다 떼지만, 우리 기록도 지워야 다음 판에 다시 뜬다).
	void HandleWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);

	UPROPERTY(Transient)
	TObjectPtr<UPGLoadingScreenWidget> Widget;
	TSharedPtr<SWidget> SlateRoot;

	FTSTicker::FDelegateHandle TickHandle;
	FDelegateHandle WorldCleanupHandle;

	// 창을 띄운 판, "섰다" 를 이미 받은 판, 맵을 다 지은 판.
	TWeakObjectPtr<UWorld> ShownWorld;
	TWeakObjectPtr<UWorld> SeatedWorld;
	TWeakObjectPtr<UWorld> MapReadyWorld;

	bool bLoading = false;
	bool bHiding = false;
	double LoadingShownAt = 0.0;
	double HideStartedAt = 0.0;
	// 서버 쪽 무적 시간 제한(60초)보다 조금 길게. 넘으면 창을 닫고 Error 로그(영영 검은 화면보다 낫다).
	float LoadingTimeoutSeconds = 75.0f;
	float HideFadeSeconds = 0.6f;
};
