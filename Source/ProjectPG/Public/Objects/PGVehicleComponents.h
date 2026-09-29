// 차량 요소 컴포넌트 (노션 "차량 요소", OBJ-072 ~ OBJ-077).
//
// 차량(헬기·선박·자동차)의 이동·물리는 차량 담당이다. 여기서는 차량 액터에 붙이는
// 두 가지 부품만 제공한다.
//  - UPGSeatComponent: 운전석/탑승 좌석/탑승 위치/하차 위치 (OBJ-072~075). 점유와 안전한 하차 지점.
//  - UPGFuelComponent: 연료 주입 지점 (OBJ-076). 연료통 아이템을 소모해 연료량을 올린다.
// 차량 출발·탈출 지점(OBJ-077)은 APGExtractionZoneActor(Trigger=Interact)를 차량에 붙여 쓴다.
//
// 노션 기준 좌석은 "검토 필요 / P3", 연료는 "확정 / P2"다. 카탈로그에는 좌석 항목을 bIncluded=false로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PGVehicleComponents.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGSeatSignature, UPGSeatComponent*, Seat, APawn*, Pawn);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGFuelSignature, UPGFuelComponent*, Fuel, float, Amount);

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGSeatComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPGSeatComponent();

	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	bool IsOccupied() const { return Occupant != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	APawn* GetOccupant() const { return Occupant; }

	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	bool IsDriverSeat() const { return bDriverSeat; }

	// 서버 전용. 좌석에 앉힌다. 이미 누가 있으면 false. 실제 부착·입력 전환은 차량 담당이 OnEntered에서 한다.
	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	bool TryEnter(APawn* Pawn);

	// 서버 전용. 내린다. 안전한 하차 위치를 OutExitLocation에 돌려준다.
	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	bool Exit(APawn* Pawn, FVector& OutExitLocation);

	// 탑승 접근 지점에서 얼마나 가까워야 탈 수 있는지.
	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	bool IsPawnAtEntryPoint(const APawn* Pawn) const;

	// 하차 후보 중 막히지 않은 곳을 고른다. 전부 막히면 좌석 위치.
	UFUNCTION(BlueprintCallable, Category = "PG|Seat")
	FVector FindSafeExitLocation() const;

	UPROPERTY(BlueprintAssignable, Category = "PG|Seat")
	FPGSeatSignature OnEntered;

	UPROPERTY(BlueprintAssignable, Category = "PG|Seat")
	FPGSeatSignature OnExited;

	// 운전석이면 차량 제어권을 준다 (OBJ-072). 아니면 탑승 좌석 (OBJ-073).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Seat")
	bool bDriverSeat = false;

	// 탑승 위치 (OBJ-074): 좌석 기준 상대 오프셋. 플레이어가 여기서 F를 누른다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Seat")
	FVector EntryOffset = FVector(0.0f, -120.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Seat", meta = (ClampMin = "0.0"))
	float EntryRadius = 200.0f;

	// 하차 위치 후보 (OBJ-075): 좌석 기준 상대 오프셋들. 순서대로 충돌 검사한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Seat")
	TArray<FVector> ExitOffsets;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|Seat")
	TObjectPtr<APawn> Occupant;
};

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGFuelComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UPGFuelComponent();

	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	float GetFuel() const { return CurrentFuel; }

	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	float GetCapacity() const { return Capacity; }

	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	bool HasEnoughToDepart() const { return CurrentFuel >= RequiredToDepart; }

	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	bool CanRefuel(const APawn* Pawn) const;

	// 서버 전용. 연료통 하나를 소모해 FuelPerItem만큼 채운다.
	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	bool TryRefuel(APawn* Pawn);

	// 서버 전용. 주행 중 소모 등 차량 담당이 부른다.
	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	void ConsumeFuel(float Amount);

	UFUNCTION(BlueprintCallable, Category = "PG|Fuel")
	FText GetPrompt() const;

	UPROPERTY(BlueprintAssignable, Category = "PG|Fuel")
	FPGFuelSignature OnFuelChanged;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Fuel")
	FName FuelItemId = TEXT("Fuel");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Fuel", meta = (ClampMin = "0.0"))
	float Capacity = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Fuel", meta = (ClampMin = "0.0"))
	float FuelPerItem = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Fuel", meta = (ClampMin = "0.0"))
	float RequiredToDepart = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Fuel", meta = (ClampMin = "0.0"))
	float UseRadius = 200.0f;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated, EditAnywhere, BlueprintReadOnly, Category = "PG|Fuel", meta = (ClampMin = "0.0"))
	float CurrentFuel = 0.0f;
};
