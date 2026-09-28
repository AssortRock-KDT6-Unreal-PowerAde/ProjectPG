// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "ItemContainerActor.generated.h"

class UPGLockComponent;
class UPGLootableComponent;

/**
 * 아이템 상자 원형 (노션 "아이템 상자 원형", OBJ-001 ~ OBJ-011).
 *
 * 노션 "오브젝트 관리 목록"의 규칙대로, 상자 종류마다 클래스를 새로 만들지 않는다.
 * 나무 상자 / 군용 상자 / 탄약 상자 / 무기 상자 / 의료품 상자 / 금고는 전부 이 한 클래스이고,
 * 차이는 아래 UPROPERTY 값(메시, 잠금, 열쇠, 루팅 테이블, 여는 시간)으로만 준다.
 * 새 클래스가 필요한 경우는 "데이터로 표현할 수 없는 새 행동"이 생겼을 때뿐이다.
 *
 * 책임 경계:
 *  - 이 클래스: 열림 상태, 뚜껑 연출, 루팅 테이블 굴리기, 사용 중 잠금
 *  - 상호작용 담당: F 대상 선택과 Interact 호출 (UPGInteractionComponent)
 *  - 인벤토리 담당: 실제 아이템 보관 (IPGItemReceiver)
 *  - 잠금: UPGLockComponent
 */
UCLASS()
class PROJECTPG_API AItemContainerActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	AItemContainerActor();

	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;
	// 멀티(9/27): 표 줄 겉모습을 서버·클라 모두에서 입힌다(APGInteractableActorBase::ApplyCatalogLook).
	virtual void ApplyCatalogLook() override;
	virtual void Tick(float DeltaTime) override;

	UFUNCTION(BlueprintCallable, Category = "Container")
	bool IsOpen() const { return bIsOpen; }

	UFUNCTION(BlueprintCallable, Category = "Container")
	UPGLockComponent* GetLock() const { return LockComponent; }

	// 서버 전용. 스포너가 소켓 시드를 넘겨 준다. 0이면 열 때 임의 시드를 쓴다.
	UFUNCTION(BlueprintCallable, Category = "Container")
	void SetLootSeed(int64 InSeed) { LootSeed = InSeed; }

	UFUNCTION(BlueprintCallable, Category = "Container")
	void SetLootTableId(FName InTableId) { LootTableId = InTableId; }

	// 마지막으로 굴린 결과. 스모크 테스트와 UI(루팅 창)가 읽는다.
	UFUNCTION(BlueprintCallable, Category = "Container")
	const TArray<FPGItemStack>& GetLastLoot() const { return LastLoot; }

	// 스모크 테스트가 뚜껑이 제자리에서 위로 열리는지 재 본다.
	UStaticMeshComponent* GetLid() const { return LidComponent; }

	// 열린 뒤 내용물이 들어 있는 칸. UI 는 GetContents 로 칸을 그리고 TakeItem 으로 하나씩 옮긴다.
	UFUNCTION(BlueprintCallable, Category = "Container")
	UPGLootableComponent* GetStorage() const { return Storage; }

	// 서버 전용. 로봇·탱크가 상자를 치거나 상자가 얹힌 건물 조각이 날아가면, 상자를 물리 물체로 풀어 같이 튕겨 보낸다(9/23 사용자:
	// "건물을 날려도 상자가 허공에 떠 있다 — 같이 날아가게"). 부수지 않는다: 떨어진 자리에서 그대로 열어 루팅한다.
	// Velocity 는 속도 변화(cm/s). 이미 풀린 상자면 속도만 더한다.
	void KnockLoose(const FVector& Velocity);
	bool IsKnockedLoose() const { return bKnockedLoose; }

protected:
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_IsOpen();
	// 멀티(9/28): 날아간 상자는 클라이언트도 같은 물리 몸통을 만든다(.cpp MakeLooseBody 주석).
	UFUNCTION()
	void OnRep_KnockedLoose();
	UPrimitiveComponent* MakeLooseBody();

protected:
	// 경첩. 뚜껑은 이 점을 축으로 돈다. 위치는 뚜껑 메시 크기를 보고 FitLidHinge가 정한다.
	// 경첩이 없으면 뚜껑이 메시 원점(보통 상자 바닥 가운데)을 축으로 돌아 허공으로 날아간다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<USceneComponent> LidHinge;

	// 내용물 칸. 여는 순간 루팅 테이블을 굴려 여기에 담고, 플레이어가 하나씩(숫자키/UI) 또는 전부(F) 가져간다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<UPGLootableComponent> Storage;

	// 뚜껑. 없는 상자(금고 문 포함)는 비워 둔다. 열리면 경첩을 축으로 LidOpenAngle 만큼 회전한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<UStaticMeshComponent> LidComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Container")
	TObjectPtr<UPGLockComponent> LockComponent;

	// 안에 무엇이 드는지. 탄약 상자와 무기 상자의 차이가 여기다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Container")
	FName LootTableId;

	// 한 번 열면 다시 열 수 없다. 런타임 상태라 에디터에서 편집하지 않는다.
	UPROPERTY(ReplicatedUsing = OnRep_IsOpen, VisibleInstanceOnly, BlueprintReadOnly, Category = "Container")
	bool bIsOpen = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Container")
	// 열리는 각도의 크기(도). 어느 쪽으로 돌지는 FitLidHinge가 "뚜껑 끝이 위로 올라가는 쪽"으로 고른다.
	float LidOpenAngle = 110.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Container", meta = (ClampMin = "0.0"))
	float LidAnimSeconds = 0.4f;

	UPROPERTY(VisibleInstanceOnly, Category = "Container")
	int64 LootSeed = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "Container")
	TArray<FPGItemStack> LastLoot;

	float LidAnimElapsed = 0.0f;
	UPROPERTY(ReplicatedUsing = OnRep_KnockedLoose)
	bool bKnockedLoose = false;

	// 다 열렸을 때 경첩의 회전. FitLidHinge가 채운다.
	FRotator LidOpenRotation = FRotator::ZeroRotator;

	// 뚜껑 메시 크기로 경첩 위치와 여는 방향을 정한다. 메시가 바뀌어도 다시 부르면 된다.
	void FitLidHinge();
};
