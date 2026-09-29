// 탈것(차·로봇). 타고 있는 사람에게 보여 줄 문구만 알려 준다.
//
// 왜 IInteractable 과 따로인가: GetInteractionPrompt 는 "밖에서 보는 사람" 문구("차량 탑승")다.
// 타고 있는 사람의 문구("차량 하차")를 거기 섞으면, 남이 탄 차를 바라본 다른 플레이어에게도 "차량 하차"가 떴다.
// 탄 사람의 화면(UPGInteractionComponent)만 이 함수를 부른다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PGRideable.generated.h"

class APawn;

UINTERFACE(MinimalAPI)
class UPGRideable : public UInterface
{
	GENERATED_BODY()
};

class PROJECTPG_API IPGRideable
{
	GENERATED_BODY()

public:
	// 비어 있으면 아무것도 안 띄운다(달리는 중 등).
	virtual FText GetRiderPrompt() const = 0;

	// 지금 타고 있는 사람(없으면 nullptr). 탈출구가 "차에 탄 채 지나가도 운전자의 연료통을 본다" 에 쓴다(9/22).
	virtual APawn* GetRiderPawn() const { return nullptr; }
};
