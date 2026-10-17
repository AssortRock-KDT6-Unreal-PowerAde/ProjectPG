#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PGLoadingScreenWidget.generated.h"

class UTextBlock;

// 로딩 창 위젯의 부모 클래스 (모양은 WBP_PGLoadingScreen 이 맡는다). 흐름 그림 → MapBuilder/README.md "로딩 창과 시작 무적"
// ProjectTest2 에서 가져왔다. 이름을 그대로 둔 이유: WBP 가 부모를 "PGLoadingScreenWidget" 이라는 이름으로 기억해서,
//   DefaultEngine.ini 의 ProjectPGTest2 → ProjectPG 이름 바꿈 줄만으로 이 클래스에 붙는다.
// 원래 있던 "C++ 로 모양 짓기"는 뺐다(보이는 건 WBP 로). WBP 칸이 비면 로딩 창 없이 시작한다(로그 남김).
UCLASS()
class UPGLoadingScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetLoadingText(const FText& Text);

protected:
	// 처음 화면에 붙을 때: 시작 로고(ProjectTest2 첫 화면용)는 숨기고, 오른쪽 아래 "불러오는 중" 묶음만 보인다.
	virtual void NativeConstruct() override;

	// ---- WBP 안 위젯 이름과 같으면 자동으로 이어진다. 없어도 된다. ----
	// 시작 로고 묶음(이 프로젝트에선 안 씀, 숨김).
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> SplashLogo;
	// 오른쪽 아래 글자 + 도는 표시.
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UWidget> LoadingCorner;
	UPROPERTY(BlueprintReadOnly, Category = "PG|Loading", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> LoadingText;
};
