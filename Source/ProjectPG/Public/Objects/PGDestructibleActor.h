// 파괴물 원형 (노션 "파괴물 원형", OBJ-025, OBJ-108 ~ OBJ-115).
//
// 깨지는 유리창·판자문·바리케이드·약한 엄폐물·폭발 제거 장애물·도구 제거 장애물·
// 퀘스트 파괴 대상·폭발성 연료통이 전부 이 클래스다.
//  - Condition = AnyDamage:     체력이 0이 되면 부서진다
//  - Condition = ExplosiveOnly: 폭발 피해 타입(RequiredDamageType)만 체력을 깎는다
//  - Condition = ToolOnly:      피해로는 안 부서지고, RequiredToolItemId를 가진 플레이어가 F로 제거한다
//  - ExplosionRadius > 0:       부서질 때 주변에 방사 피해 (폭발성 연료통)
// 피해 계산(무기 데미지)은 전투 담당이고, 여기서는 "받은 피해로 언제 부서지는가"만 책임진다.
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "PGDestructibleActor.generated.h"

class UDamageType;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGDestroyedSignature, APGDestructibleActor*, Destructible, AActor*, Causer);

UCLASS()
class PROJECTPG_API APGDestructibleActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	APGDestructibleActor();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;
	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;

	UFUNCTION(BlueprintCallable, Category = "PG|Destructible")
	bool IsDestroyed() const { return bDestroyed; }

	UFUNCTION(BlueprintCallable, Category = "PG|Destructible")
	float GetHealth() const { return Health; }

	UFUNCTION(BlueprintCallable, Category = "PG|Destructible")
	void Configure(EPGDestroyCondition InCondition, float InMaxHealth, FName InToolItemId = NAME_None);

	UFUNCTION(BlueprintCallable, Category = "PG|Destructible")
	void SetExplosion(float Radius, float Damage) { ExplosionRadius = Radius; ExplosionDamage = Damage; }

	// 서버 전용. 조건과 상관없이 즉시 부순다 (퀘스트·디버그).
	UFUNCTION(BlueprintCallable, Category = "PG|Destructible")
	void ForceDestroy(AActor* Causer);

	UPROPERTY(BlueprintAssignable, Category = "PG|Destructible")
	FPGDestroyedSignature OnDestroyedByDamage;

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Destroyed();

	void Break(AActor* Causer);

protected:
	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다. 도구 전용 바리케이드 안내·부서진 모양.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Destructible")
	EPGDestroyCondition Condition = EPGDestroyCondition::AnyDamage;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Destructible", meta = (ClampMin = "1.0"))
	float MaxHealth = 100.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Destructible")
	float Health = 100.0f;

	// ExplosiveOnly에서 인정하는 피해 타입. 비어 있으면 FRadialDamageEvent만 인정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Destructible")
	TSubclassOf<UDamageType> RequiredDamageType;

	// ToolOnly에서 필요한 아이템 (절단기, 곡괭이 등).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Destructible")
	FName RequiredToolItemId;

	// 부서진 뒤 보여줄 메시. 비어 있으면 숨긴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Destructible")
	TSoftObjectPtr<UStaticMesh> DestroyedMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Destructible", meta = (ClampMin = "0.0"))
	float ExplosionRadius = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Destructible", meta = (ClampMin = "0.0"))
	float ExplosionDamage = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_Destroyed, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Destructible")
	bool bDestroyed = false;
};
