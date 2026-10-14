// 사람 없이 UI 흐름을 시험하는 도구. 개발 빌드에서만 일한다(출시 빌드에서는 아무것도 안 함).

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "UiAutoTestSubSystem.generated.h"

class UFitIconItemWidget;

// UI 자동 시험 담당.
// 왜 있나: 로비 버튼·인벤토리 창·매칭은 사람이 눌러야 돈다. 이 PC 에는 웹 서버(로그인)가 없어서 로비까지 갈 수도 없다.
//   명령줄로 "몇 초에 무엇을 하라 / 사진 찍어라" 를 주면 사람 없이 같은 흐름을 매번 똑같이 돌려 보고 사진·로그로 확인한다.
// 명령줄(전부 선택, 초 = 프로그램 시작부터, 여러 개는 + 로 잇는다):
//   -PGStep=Lobby@4+FakeItems@4.5+Btn:CharacterBtn@5+Tooltip@8+Context@10+Btn:RotateButton@11
//       Lobby          = 로그인 창을 닫고 로비 메뉴를 바로 띄운다(웹 서버 로그인 대신)
//       FakeItems      = 내 창고·주머니에 시험용 아이템을 넣는다(웹 서버가 없어 짐이 비어 있으므로, 로비에서만)
//       Btn:이름       = 화면에 떠 있는 위젯 안의 그 이름 버튼을 누른다(CharacterBtn, GameStartBtn, OptionBtn, BackBtn, CancelButton, RotateButton 등)
//       Combo:이름=번호 = 그 이름 고르기 칸에서 번호째를 고른다(FilterCombo=1 이면 무기)
//       Tooltip / Context = 화면에서 가장 큰 아이템에 설명 창 / 우클릭(진짜 마우스 처리 함수로)
//       Context:2003   = 그 번호 아이템에 우클릭(그 뒤 Btn:EquipButton 이면 사람이 "장착" 누른 것과 같다)
//       Count          = 짐 데이터(칸별 아이템)와 화면에 그려진 아이템 칸, 장착 칸을 로그로 남긴다
//       Equip:2003 / EquipVest = 창고의 그 아이템을 우클릭 "장착" 과 같은 함수로 장착하고 앞뒤 Count 를 남긴다
//       FakeMatch:2/4 / FakeMatch:Starting / FakeMatch:Cancelled = 웹 서버 매칭 메시지를 흉내 내 형님 매칭 처리에 넣는다
//   -PGShot=lobby@6+inventory@9        그 시각에 사진(UI 포함) -> Saved/UiShots/이름.png
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
	void RunStep(const FString& Step);
	void PressButton(const FString& Name);
	void PickCombo(const FString& Name, int32 Index);
	void FakeItems();
	UFitIconItemWidget* FindBiggestItem() const;
	UFitIconItemWidget* FindItemOnScreen(FName ItemID) const;
	void LogInventoryState(const TCHAR* Label) const;
	void EquipFromStash(const FString& ItemID);

	struct FStep
	{
		double AtSeconds = 0.0;
		FString Action;   // Step / Shot / Quit
		FString Argument;
	};
	TArray<FStep> Steps;
	double StartSeconds = 0.0;
	FTSTicker::FDelegateHandle TickHandle;
};
