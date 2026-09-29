#include "Flow/PGFlowSettings.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"

UPGFlowSettings::UPGFlowSettings()
{
	// 게임 맵 기본값: 지금 PIE 로 쓰는 절차 생성 시험 맵(DefaultEngine.ini 의 GameDefaultMap 과 같다).
	// 타이틀·로비·스코어보드는 비워 둔다 — 사용자가 에디터에서 레벨을 만들면 프로젝트 설정에서 넣는다. 그 전에는 Entry 맵으로 돈다.
	GameLevel = TSoftObjectPtr<UWorld>(FSoftObjectPath(TEXT("/Game/PG/LevelDesign/Tests/LD_MetaballGenerationTest.LD_MetaballGenerationTest")));
}

UClass* UPGFlowSettings::ResolveWidgetClass(const TSoftClassPtr<UUserWidget>& Designed, UClass* Fallback)
{
	if (Designed.IsNull())
		return Fallback;
	UClass* Loaded = Designed.LoadSynchronous();
	// 부모가 다른 WBP 를 잘못 넣으면 CreateWidget<부모형> 이 null 을 준다 — 차라리 코드 화면을 띄운다.
	if (!Loaded || (Fallback && !Loaded->IsChildOf(Fallback)))
	{
		UE_LOG(LogTemp, Warning, TEXT("PGFlow: widget %s not usable (missing or not a %s) — using the code-built one"),
			*Designed.ToString(), *GetNameSafe(Fallback));
		return Fallback;
	}
	return Loaded;
}
