#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/WarZoneFootprintPreview.h"
#include "MapGroundBuilder.generated.h"

// 바닥 담당.
// 게임에서: 타일 밑에 깔리는 땅판(들판·워존·경계 흙), 도로 아스팔트 판, 호숫가 비탈·호수 바닥·물,
//           맵 바깥을 둘러싼 산, 타일 사이 이음매를 가리는 풀·덤불을 깐다.
// 깔 위치는 칸 모양 담당이 만든 설계 카드(TileDesignPlacements)를 보고 정한다.
// 그리는 그릇(HISM 컴포넌트)은 맵 액터에 붙어 있어서 Map-> 로 쓴다.
UCLASS(Transient)
class UMapGroundBuilder : public UObject
{
	GENERATED_BODY()
public:
	void Init(AWarZoneFootprintPreview* InMap);
	virtual UWorld* GetWorld() const override;

	// 땅판·도로 판·호수를 깐다. 게임에서: 걸어 다니는 바닥과 호수.
	void BuildLightweightWorldVisuals();
	// 맵 바깥 산을 둥글게 두른다. 게임에서: 맵 끝이 허공으로 안 보이게.
	void BuildBorderMountains();
	// 풀·덤불을 뿌린다(PCG). 게임에서: 20m 타일 이음매가 줄처럼 안 보이게.
	void BuildPCGDressingGraph();

private:
	UPROPERTY()
	TObjectPtr<AWarZoneFootprintPreview> Map;
};
