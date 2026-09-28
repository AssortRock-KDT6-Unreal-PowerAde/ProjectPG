// 화면 알림 문구: 여러 줄을 순서대로 페이드인 → 머묾 → 페이드아웃으로 보여 준다. (2026-09-22)
//
// 왜 월드 서브시스템인가: 누가 불러도(디렉터·드래곤·탈출구) 같은 자리에 한 줄씩 차례로 떠야 한다. 부르는 쪽이 위젯을 들고 있으면
//   두 곳에서 동시에 부를 때 문구가 겹친다. 월드마다 하나만 있으면 줄이 알아서 뒤에 선다.
// 왜 Slate 인가(UMG 위젯 에셋이 아니라): 한글 글꼴이 기본으로 붙어 있고, 에셋 없이 코드만으로 뜬다.
//   UI 담당 형님이 위젯을 만들면 Announce 를 부르는 쪽은 그대로 두고 여기 그리는 부분만 바꾸면 된다.
// 화면이 있는 쪽(클라이언트·단일 플레이)에서만 그린다. 전용 서버에는 화면이 없어 조용히 넘어간다.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "PGAnnounceSubsystem.generated.h"

class SWidget;
class UPGScreenLineWidget;
class UUserWidget;

UCLASS()
class PROJECTPG_API UPGAnnounceSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UPGAnnounceSubsystem* Get(const UObject* WorldContext);

	// 문구들을 줄 세운다. 이미 보이는 줄이 있으면 그 뒤에 이어진다.
	void Announce(const TArray<FText>& Lines);

	// 화면 가운데 큰 카운트다운 한 줄("탈출까지 3"). 흐리게 나타나고 사라지는 안내 줄과 달리 바로 바뀐다 — 1초마다 숫자가 바뀌어야 해서.
	// 빈 글자를 주면 지운다. 탈출구(APGExtractionZoneActor)가 부른다.
	void SetCountdown(const FText& Line);

	// 한 줄의 시간(초). 에디터에서 바꿀 일이 드물어 상수 대신 값으로만 둔다.
	// 9/22 "너무 갑작스럽다, 조금 천천히" 로 0.8 → 1.8초 등장, 1.4초 퇴장으로 늘렸다.
	float FadeInSeconds = 1.8f;
	float HoldSeconds = 2.8f;
	float FadeOutSeconds = 1.4f;
	float GapSeconds = 0.5f;

	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;

private:
	void EnsureWidget();
	void RemoveWidget();

	TArray<FText> Queue;
	double CurrentStart = -1.0; // 지금 줄이 시작한 월드 시각(음수 = 비어 있음)
	// 그리는 위젯(9/23: Slate → UMG 부모 UPGAnnounceLineWidget / UPGCountdownLineWidget, 모양은 WBP).
	// 화면에는 Slate 조각으로 붙인다 — 예전과 같은 층(안내 50, 카운트다운 51)·같은 방식.
	UPROPERTY(Transient)
	TObjectPtr<UPGScreenLineWidget> LineWidget;
	TSharedPtr<SWidget> LineSlate;

	UPROPERTY(Transient)
	TObjectPtr<UPGScreenLineWidget> CountdownWidget;
	TSharedPtr<SWidget> CountdownSlate;

	UPGScreenLineWidget* CreateLine(const TSoftClassPtr<UUserWidget>& Designed, UClass* Fallback);
};
