#pragma once

#include "Characters/CustomCharacter.h"
#include "Common/TableData.h"
#include "CustomEnemyCharacter.generated.h"

UCLASS()
class PROJECTPG_API ACustomEnemyCharacter : public ACustomCharacter
{
	GENERATED_BODY()
public:
	ACustomEnemyCharacter(const FObjectInitializer& ObjectInitializer);
private:
	UPROPERTY(Transient) FEnemyTableRow EnemyData;
	UPROPERTY(Replicated) FVector2D ReplicatedAimOffset = FVector2D::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<class AFirearmEquipment> EnemyWeapon;
public:
	const FEnemyTableRow& GetEnemyData() const { return EnemyData; }
	FVector2D GetEnemyAimOffset() const { return ReplicatedAimOffset; }
	virtual bool HasInfiniteReserveAmmo() const override { return true; }
	virtual void GetCombatViewPoint(FVector& Location, FRotator& Rotation) const override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
