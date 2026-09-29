// 화면 가운데 상호작용 안내 (배틀그라운드식). "[F] 갈색 신발 줍기" 한 줄 + 유지형 게이지 + 겹친 대상 목록.
//
// 위젯 블루프린트 없이 C++ 로 위젯 트리를 짠다. 왜: UI 담당이 진짜 UI 를 만들기 전까지 쓸 임시 화면인데, 에셋(.uasset)으로 만들면
// 팀 저장소로 옮길 때 같이 옮겨야 하고 머지 충돌도 난다. 코드면 파일 하나다.
// 이 위젯은 UPGInteractionComponent 의 델리게이트 셋(OnTargetChanged / OnCandidatesChanged / OnHoldProgress)만 듣는다.
// UI 담당은 자기 위젯에서 같은 셋을 들으면 되고, 이 파일은 그 예시다.
//
// 글씨체: PGUiFont (Content/PG/UI/Fonts 의 Pretendard). 다른 글꼴로 바꾸려면 그 폴더의 파일만 바꾸면 된다.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PGInteractionPromptWidget.generated.h"

class APlayerController;
class UBorder;
class UPGInteractionComponent;
class UProgressBar;
class UTextBlock;
class UVerticalBox;

UCLASS()
class PROJECTPG_API UPGInteractionPromptWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 안내 위젯을 만든다. 설정(ProjectPG Flow > Screens > Interaction Prompt Class)에 WBP 가 있으면 그것으로(9/23 블루프린트 분리).
	// 캐릭터마다 같은 줄을 반복하지 않게 여기 한 곳에서 고른다.
	static UPGInteractionPromptWidget* Create(APlayerController* Owner);

	// 이 컴포넌트의 상태를 보여 준다. 다시 부르면 이전 연결은 끊는다.
	UFUNCTION(BlueprintCallable, Category = "PG|UI")
	void BindTo(UPGInteractionComponent* InInteraction);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	UFUNCTION()
	void HandleTargetChanged(AActor* Target, const FText& Prompt);

	UFUNCTION()
	void HandleCandidatesChanged(const TArray<AActor*>& Candidates, int32 SelectedIndex);

	UFUNCTION()
	void HandleHoldProgress(float Progress01);

	void BuildTree();
	void RefreshList();
	UTextBlock* MakeText(const FName& Name, int32 Size, const FName& Typeface, const FLinearColor& Color);

	UPROPERTY(Transient)
	TObjectPtr<UPGInteractionComponent> Interaction;

	// ---- WBP 자리 (9/23 블루프린트 분리, WBP_PGInteractionPrompt) ----
	// 같은 이름으로 놓으면 연결된다. 없으면 BuildTree 가 코드로 같은 자리를 짓는다.
	// [F] 한 줄 판(대상이 있을 때만 보인다).
	UPROPERTY(BlueprintReadOnly, Category = "PG|UI", meta = (BindWidgetOptional))
	TObjectPtr<UBorder> PromptRow;

	// "갈색 신발 줍기" 같은 글자.
	UPROPERTY(BlueprintReadOnly, Category = "PG|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> PromptText;

	// 유지형(금고 등) 게이지. 누르고 있는 동안만 보인다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|UI", meta = (BindWidgetOptional))
	TObjectPtr<UProgressBar> HoldBar;

	// 겹친 대상 목록(C++ 이 줄을 채운다). 후보가 둘 이상일 때만 보인다.
	UPROPERTY(BlueprintReadOnly, Category = "PG|UI", meta = (BindWidgetOptional))
	TObjectPtr<UVerticalBox> ListBox;

	UPROPERTY(BlueprintReadOnly, Category = "PG|UI", meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> WheelHint;
};
