// 시체 루팅 컴포넌트 (노션 "시체" 대분류, OBJ-053 ~ OBJ-056).
//
// 몬스터·플레이어 시체는 별도 Actor를 만들지 않고, 기존 캐릭터/사체 Actor에 이 컴포넌트를
// 붙인다. AI와 사망 처리는 캐릭터 담당이며, 이 컴포넌트는
//  - 사망 후 ActivateLoot()가 불리면 상호작용 가능 상태가 되고
//  - 루팅 테이블(또는 지정 아이템)을 굴려 보관하며
//  - 한 명이 루팅하는 동안 다른 사람은 못 만지게 잠그고
//  - 다 털리면 OnEmptied로 소멸 처리를 캐릭터 쪽에 넘긴다.
// UPGInteractionComponent는 IInteractable이 없는 액터에서 이 컴포넌트를 찾아 대신 호출한다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Objects/PGObjectTypes.h"
#include "PGLootableComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGLootableSignature, UPGLootableComponent*, Lootable, APawn*, Pawn);

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGLootableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGLootableComponent();

	// 서버 전용. 사망 시 캐릭터 담당이 부른다. 루팅 테이블을 굴려 내용물을 확정한다.
	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	void ActivateLoot(int64 Seed = 0);

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	bool IsActivated() const { return bActivated; }

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	bool CanLoot(const APawn* Pawn) const;

	// 서버 전용. 내용물을 전부 Pawn에게 준다. 못 받은 묶음은 남는다. 성공한 묶음이 하나라도 있으면 true.
	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	bool Loot(APawn* Pawn);

	// 서버 전용. Index 번째 묶음 하나만 Pawn에게 준다(타르코프식 "칸에서 하나 집기"). UI 는 이걸 칸마다 부른다.
	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	bool TakeItem(APawn* Pawn, int32 Index);

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	FText GetPrompt() const;

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	const TArray<FPGItemStack>& GetContents() const { return Contents; }

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	void SetContents(const TArray<FPGItemStack>& InContents) { Contents = InContents; }

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	void SetLootTableId(FName InTableId) { LootTableId = InTableId; }

	// 사용 중 잠금.
	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	bool TryBeginUse(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "PG|Lootable")
	void EndUse(APawn* Pawn);

	UPROPERTY(BlueprintAssignable, Category = "PG|Lootable")
	FPGLootableSignature OnLooted;

	// 내용물이 비었을 때. 시체 소멸 타이머는 캐릭터 담당이 여기서 시작한다.
	UPROPERTY(BlueprintAssignable, Category = "PG|Lootable")
	FPGLootableSignature OnEmptied;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Lootable")
	FName LootTableId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Lootable")
	FText DisplayName;

	// 살아 있는 동안은 false. 캐릭터 담당이 사망 시 ActivateLoot로 켠다.
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Lootable")
	bool bActivated = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "PG|Lootable")
	TArray<FPGItemStack> Contents;

	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|Lootable")
	TObjectPtr<APawn> CurrentUser;
};
