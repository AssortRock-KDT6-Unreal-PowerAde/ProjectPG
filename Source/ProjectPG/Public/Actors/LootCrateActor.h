// 맵에 놓이는 아이템 상자 하나.

#pragma once

#include "CoreMinimal.h"
#include "Actor/InteractActor.h"
#include "LootCrateActor.generated.h"

// 아이템 상자 (E 로 열어서 끌어 가는 상자).
// 게임에서: 창고 구석의 나무 상자. 열면 형님 인벤토리 창이 상자 칸과 같이 뜨고, 안의 물건을 가방으로 끌어 간다.
// 팀 방식 그대로: 형님 AInteractActor(상자 칸 + E 열기 + 서버 동기화)를 물려받고, 여기서는 칸 크기와 "아이템 넣기" 만 더한다.
// 보이는 것(어떤 상자 메시인지)은 BP_LootCrate 에서 고른다. C++ 에는 동작과 칸만.
// 리슨 서버: 서버가 놓고 서버가 채운다. 들어온 사람은 복제로 상자와 내용을 받는다.
UCLASS()
class PROJECTPG_API ALootCrateActor : public AInteractActor
{
	GENERATED_BODY()

public:
	ALootCrateActor();

	// 부모가 만든 10×10 칸을 이 상자 크기(CrateSize)로 바꾼다(서버만).
	virtual void PostInitializeComponents() override;

	// 서버만: 아이템 하나를 상자 빈 자리에 넣는다. 자리가 모자라면 상자를 아래로 늘려서라도 넣는다.
	// 왜 늘리나: 좋은 자리는 큰 물건(4×2 소총 등)이 3개까지 나와 6×4 에 다 안 들어갈 수 있다 — 뽑힌 것이 사라지면 안 된다.
	bool AddLoot(FName ItemID, int32 Quantity);

	// 메시 아래면이 FloorPoint 높이에 닿게 내려놓는다(메시마다 기준점이 달라서 크기를 재서 맞춘다).
	void PlaceOnFloor(const FVector& FloorPoint);

	// 지금 상자 안 아이템 수(확인 로그용).
	int32 GetLootCount() const;

protected:
	// 상자 칸 크기(가로×세로). 예: 6×4 = 권총(2×1) 여러 개, 소총(4×2) 하나 반쯤.
	UPROPERTY(EditDefaultsOnly, Category = "Crate", meta = (ClampMin = "1"))
	FIntPoint CrateSize = FIntPoint(6, 4);
};
