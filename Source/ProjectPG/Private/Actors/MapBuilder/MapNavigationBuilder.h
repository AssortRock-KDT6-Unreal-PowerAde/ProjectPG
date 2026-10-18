#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Actors/MapBuilder.h"
#include "MapNavigationBuilder.generated.h"

class ANavigationData;

// 길찾기 지도 담당 (서버만). 흐름 그림: 이 폴더 README.md
// 게임에서: 맵 짓기가 바닥판 크기를 정하면, 맵 전체 크기의 길찾기 영역을 넣고 지도를 뒤에서 굽는다.
//           다 구우면 맵의 OnNavigationReady 로 알린다. 몬스터를 내보내거나 움직이는 쪽은 그 뒤에.
// 왜 코드로: 맵이 판마다 새로 지어져서 지도를 미리 구울 수 없고, 레벨 파일(TestLevel_MapGenerating)은 팀원 것이라 영역 박스를 직접 놓지 않는다.
// 필요한 설정: DefaultEngine.ini 의 RuntimeGeneration=Dynamic(게임 중 굽기). 없으면 영역을 넣어도 지도가 안 생긴다.
// 조정 수치(영역 높이, 기다리는 시간)는 DA_MapAssets 의 "길찾기" 칸.
UCLASS(Transient)
class UMapNavigationBuilder : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 바닥판(FloorBounds) 크기만큼 길찾기 영역을 넣고 굽기를 시작한다. 판마다 한 번, 서버만.
	void StartBuild(const FBox& FloorBounds);
	bool IsReady() const { return bReady; }

private:
	// 영역 박스(NavMeshBoundsVolume)를 만든다. 코드로 만든 박스는 모양이 비어 있어서 상자 모양을 직접 넣는다.
	class ANavMeshBoundsVolume* SpawnBoundsVolume(const FVector& Center, const FVector& HalfSize) const;

	// 굽기가 끝날 때마다 엔진이 부른다(문 등이 움직여 일부만 다시 구울 때도). 엔진이 부르는 함수라서 UFUNCTION.
	UFUNCTION()
	void HandleGenerationFinished(ANavigationData* NavigationData);
	// 정한 시간 안에 굽기가 안 끝났을 때: Error 로그만 남기고 계속 기다린다.
	void HandleBuildTimeout();

	UPROPERTY(Transient)
	TObjectPtr<AMapBuilder> Map;

	double BuildStartSeconds = 0.0;
	bool bReady = false;
	FTimerHandle BuildTimeoutTimer;
};
