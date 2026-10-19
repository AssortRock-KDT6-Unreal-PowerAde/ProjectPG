#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PGLoadingScreenWidget.generated.h"

class UWidgetAnimation;

// 로딩 창 위젯의 부모 클래스. 글자·모양·사라지는 효과는 전부 WBP_PGLoadingScreen 이 맡고, 여기엔 칸만 있다.
// 흐름 그림 → MapBuilder/README.md "로딩 창과 시작 무적"
// ProjectTest2 에서 가져왔다. 이름을 그대로 둔 이유: WBP 가 부모를 "PGLoadingScreenWidget" 이라는 이름으로 기억해서,
//   DefaultEngine.ini 의 ProjectPGTest2 → ProjectPG 이름 바꿈 줄만으로 이 클래스에 붙는다.
UCLASS()
class UPGLoadingScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 닫기 시작: WBP 에 HideAnim 애니메이션이 있으면 틀고, 끝날 때까지 몇 초인지 돌려준다(없으면 0 → 바로 닫음).
	float PlayHide();

	// "섰다" 가 끝내 안 오면 몇 초 뒤 그냥 닫을지(WBP 의 클래스 기본값에서 고친다).
	// 서버 쪽 무적 시간 제한(AMapBuilder 의 SpawnProtectionTimeoutSeconds)보다 조금 길게 둔다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Loading", meta = (ClampMin = "5"))
	float WaitLimitSeconds = 75.0f;

protected:
	// 처음 화면에 붙을 때: 시작 로고(ProjectTest2 첫 화면용)는 숨기고, 오른쪽 아래 "불러오는 중" 묶음만 보인다.
	virtual void NativeConstruct() override;

	// ---- WBP 안 위젯·애니메이션 이름과 같으면 자동으로 이어진다. 없어도 된다. ----
	// 시작 로고 묶음(이 프로젝트에선 안 씀, 숨김).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SplashLogo;
	// 오른쪽 아래 글자 + 도는 표시.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LoadingCorner;
	// 닫힐 때 트는 애니메이션(예: 0.6초 동안 흐려지기).
	UPROPERTY(Transient, meta = (BindWidgetAnimOptional))
	TObjectPtr<UWidgetAnimation> HideAnim;
};
