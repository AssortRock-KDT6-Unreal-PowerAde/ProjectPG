#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/DataTable.h"
#include "Actors/MapBuilder.h"
#include "MapPointPlanner.generated.h"

// 시설 지점 한 줄이 "언제" 들어가나. 대부분 Always.
UENUM(BlueprintType)
enum class EFacilityPointCondition : uint8
{
	Always,
	// 시설 씨앗(LocalSeed)이 짝수인 판에만. 막사 퀘스트 자리처럼 "있을 때도 없을 때도 있는" 자리.
	EvenSeedOnly,
	// 호수가 없는 판에만. 호숫가 마을은 호수가 있으면 보트 시작 자리가 생겨서 몬스터 자리를 뺀다.
	NoLakeOnly
};

// 시설 안 지점 한 줄 (데이터 테이블 DT_FacilityPoints 의 한 줄).
// 게임에서: "창고 2층 구석에 상자 자리", "공장 사무실 앞에 몬스터 자리" 같은 것.
// 왜 표로 뺐나: 예전엔 이 좌표 30여 개가 C++ 에 FVector 로 박혀 있어서 상자 하나 옮기려 해도 빌드해야 했다.
//              이제 에디터에서 표를 열어 숫자만 고치면 다음 판부터 바뀐다.
// 표의 줄 순서 = 지점이 만들어지는 순서다(지점 이름 번호·지점 지문이 이 순서로 정해진다). 줄 순서를 바꾸면 지문이 바뀐다.
USTRUCT(BlueprintType)
struct FFacilityPointRow : public FTableRowBase
{
	GENERATED_BODY()

	// 어느 시설의 지점인가.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EFacilityVisualSet Facility = EFacilityVisualSet::Warehouse;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ELevelDesignPointType Type = ELevelDesignPointType::Loot;

	// 시설 가운데 기준 위치(cm). 시설이 돌아가 있으면 같이 돌린다. Z 120 = 바닥 위 사람 키 절반쯤.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FVector LocalSocket = FVector::ZeroVector;

	// 무엇을 놓을 자리인가(이름표). 아이템 담당·몬스터 담당이 읽는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName Archetype = NAME_None;

	// 1~3. 높을수록 좋은 자리(아이템이 더 많이·더 좋게 나온다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "1", ClampMax = "3"))
	int32 Tier = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	float RadiusCm = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Capacity = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	EFacilityPointCondition Condition = EFacilityPointCondition::Always;
};

// 지점 담당.
// 게임에서: 맵이 다 지어진 뒤 "시작 자리·상자 자리·몬스터 자리·출구·퀘스트 자리" 를 찍는다.
//           그리고 시설 레벨이 다 불러와지면, 벽 속에 박힌 자리를 근처 빈 곳으로 옮긴다(끼임 정리).
// 결과는 맵의 LevelDesignPoints 목록. 아이템 담당·시작 구역 담당·검사기가 그 목록을 읽는다.
// 예전엔 맵 클래스(MapBuilder.cpp) 안에 있던 함수 3개(BuildGameplayPointMarkers 등)를 그대로 옮긴 것.
UCLASS(Transient)
class UMapPointPlanner : public UObject
{
	GENERATED_BODY()
public:
	void Init(AMapBuilder* InMap);
	virtual UWorld* GetWorld() const override;

	// 지점 찍기. 타일을 다 세운 직후(시설 레벨은 아직 불러오는 중) 한 번 부른다.
	void BuildPoints();

	// 끼임 정리. 맵 Tick 이 매번 부르지만, 시설 레벨이 다 보일 때 딱 한 번만 일한다.
	void ResolveSafety();

private:
	// 지점 지문 다시 계산. 같은 시드면 같은 값이어야 한다(검사 기준).
	void RebuildHash();

	// 사람 한 명이 이 바닥에 설 수 있나(벽·소품에 막혔나). 지점 찍기·끼임 정리가 같이 쓴다.
	bool IsSpotBlocked(const FVector& Location) const;

	// 시설 지점 표(DT_FacilityPoints). 없으면 시설 안 지점은 0개가 된다(에러 로그).
	const UDataTable* LoadFacilityPointTable() const;

	UPROPERTY()
	TObjectPtr<AMapBuilder> Map;
};
