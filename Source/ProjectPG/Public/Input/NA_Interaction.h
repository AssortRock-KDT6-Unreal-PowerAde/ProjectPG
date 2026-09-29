// F 상호작용(상자 열기·줍기·문·탈것 타기)을 팀 입력 구조에 붙이는 네이티브 액션. (2026-09-19)
//
// 왜: 팀 캐릭터의 입력은 전부 "IMC_Player(키) → PlayerDefaultActionTable(입력 동작 → 처리 클래스) → NA_* C++" 로 흐른다.
//     상호작용도 같은 길로 넣어야 팀원이 봤을 때 자연스럽고, 키는 엔진(Enhanced Input)이 처리한다.
//     IA_Interaction 과 IMC_Player 의 F 연결은 팀이 이미 만들어 뒀고, 처리 클래스만 없었다.
// 동작은 기존 상호작용 컴포넌트(UPGInteractionComponent)에 맡긴다: 누르는 순간 BeginInteract, 떼는 순간 EndInteract.
//   (유지형 상호작용 — 문 열기처럼 몇 초 누르고 있어야 하는 것 — 이 떼는 순간 취소되게 하려면 두 이벤트가 다 필요하다.)
#pragma once

#include "CoreMinimal.h"
#include "Input/NativeAction.h"
#include "NA_Interaction.generated.h"

UCLASS()
class PROJECTPG_API UNA_Interaction : public UNativeAction
{
	GENERATED_BODY()

public:
	virtual bool ShouldRegisterTriggerEvent(ETriggerEvent TriggerEvent) const override;
	virtual void Started(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
	virtual void Completed(const FInputActionValue& InputActionValue, ACustomPlayerCharacter* PlayerCharacter) override;
};
