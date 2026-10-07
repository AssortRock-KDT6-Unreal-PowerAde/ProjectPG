// 화면 없이(헤드리스) UI 흐름을 시험하는 도구. 개발 빌드에서만 일한다(출시 빌드에서는 아무것도 안 함).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "UiAutoTestSubSystem.generated.h"

// UI 자동 시험 담당.
// 왜 있나: 로비 버튼·인벤토리 창·매칭은 사람이 눌러야 돈다. 명령줄로 "몇 초에 무슨 버튼을 눌러라 / 사진 찍어라" 를 주면
//          사람 없이 같은 흐름을 매번 똑같이 돌려 보고 스크린샷·로그로 확인할 수 있다.
// 명령줄(전부 선택, 초 = 프로그램 시작부터):
//   -PGClick=Character@3+GameStart@8   로비 버튼 누르기(Character / GameStart / Option / Exit)
//   -PGShot=4+6.5                      그 시각에 스크린샷(UI 포함, Saved/Screenshots)
//   -PGQuitAt=12                       그 시각에 끄기
UCLASS()
class UUiAutoTestSubSystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

private:
	bool Tick(float DeltaTime);
	void Click(const FString& ButtonName);
	void DragItem(class UItemWidget* Target);

	struct FStep
	{
		double AtSeconds = 0.0;
		FString Action;   // Click / Shot / Quit
		FString Argument; // 버튼 이름
	};
	TArray<FStep> Steps;
	double StartSeconds = 0.0;
	FTSTicker::FDelegateHandle TickHandle;
};
