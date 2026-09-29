// ProjectPG-only walking pawn used to validate generated level design.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Objects/PGItemReceiverInterface.h"
#include "LevelDesignValidationCharacter.generated.h"

class UCameraComponent;
class UNavigationInvokerComponent;
class UPGInteractionComponent;
class USpringArmComponent;
class UStaticMeshComponent;
class UPGWeaponComponent;
class UPGWearableComponent;
class UPGInteractionPromptWidget;
class UAnimSequence;

UCLASS()
class PROJECTPG_API ALevelDesignValidationCharacter : public ACharacter, public IPGItemReceiver
{
	GENERATED_BODY()

public:
	ALevelDesignValidationCharacter();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- IPGItemReceiver ----
	// 검증용 폰은 진짜 인벤토리가 없다. 이름→수량 맵 하나로 오브젝트 계약만 확인한다.
	// 팀 캐릭터(ACustomPlayerCharacter)에 붙일 때는 UInventoryComponent::AddItemByID 로 바꾼다.
	virtual bool ReceiveItem_Implementation(FName ItemId, int32 Count) override;
	virtual bool HasItem_Implementation(FName ItemId, int32 Count) const override;
	virtual bool ConsumeItem_Implementation(FName ItemId, int32 Count) override;
	virtual FName GetPlayerKey_Implementation() const override;

	// 몬스터가 때리면 여기로 온다. 테스트용 폰이라 죽지 않고 로그만 남기고 체력을 되돌린다.
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintCallable, Category = "Validation")
	UPGWeaponComponent* GetWeapon() const { return Weapon; }

	UPROPERTY(EditAnywhere, Category = "Validation")
	float MaxHealth = 100.0f;

	UFUNCTION(BlueprintCallable, Category = "Validation")
	int32 GetDebugItemCount(FName ItemId) const;

	// 가진 것 전부(이름→개수). 판이 끝날 때 스코어보드(PGRunSubsystem)가 "들고 나온 아이템" 을 읽는다.
	const TMap<FName, int32>& GetDebugInventory() const { return DebugInventory; }

	UFUNCTION(BlueprintCallable, Category = "Validation")
	UPGInteractionComponent* GetInteraction() const { return Interaction; }

	UFUNCTION(BlueprintCallable, Category = "Validation")
	UPGWearableComponent* GetWearable() const { return Wearable; }

	// V 키. 3인칭(기본)에서는 입은 옷이 보이고, 1인칭에서는 자기 몸을 숨긴다.
	UFUNCTION(BlueprintCallable, Category = "Validation")
	void SetFirstPerson(bool bInFirstPerson);

private:
	// Tick 을 역할별로 나눈 것. 전부 로컬 플레이어 전용.
	void PollInteraction(APlayerController* PlayerController);
	void PollLootSlots(APlayerController* PlayerController);
	void PollWeapon(APlayerController* PlayerController);
	void DrawDebugHud() const;
	void UpdateLocomotionAnim();
	void EnsurePromptWidget(APlayerController* PlayerController);
	// 권총을 갖고 있고 손에 안 들었으면 권총집에 꽂혀 보이게. 서버에서 소유·장착이 바뀔 때마다.
	void UpdateHolsteredPistol();

	UFUNCTION()
	void HandleWeaponEquipped(UPGWeaponComponent* InWeapon, FName ItemId);
	const class UPGLootableComponent* GetLookedAtStorage() const;

	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<UPGInteractionComponent> Interaction;

	UPROPERTY(VisibleInstanceOnly, Category = "Validation")
	TMap<FName, int32> DebugInventory;

	bool bInteractKeyWasDown = false;
	bool bAttackKeyWasDown = false;
	bool bCycleWeaponKeyWasDown = false;
	bool bTakeKeyWasDown[9] = {};
	bool bViewKeyWasDown = false;
	bool bFirstPerson = false;

	// 착장 겉모습(Quantum 모듈 캐릭터). 팀 캐릭터에도 이 컴포넌트 하나만 붙이면 된다.
	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<UPGWearableComponent> Wearable;

	UPROPERTY(Transient)
	TObjectPtr<UPGInteractionPromptWidget> PromptWidget;

	// 애님 블루프린트 없이 속도로 고르는 루프 네 개(몬스터와 같은 방식). 팀 캐릭터는 자기 ABP 를 쓴다.
	// 9/23 블루프린트 분리: 어떤 동작을 쓸지는 이 칸(캐릭터 블루프린트에서 바꾼다). 기본값 = Quantum 데모 동작.
	UPROPERTY(EditDefaultsOnly, Category = "Validation|Visual")
	TSoftObjectPtr<UAnimSequence> IdleAnimAsset;
	UPROPERTY(EditDefaultsOnly, Category = "Validation|Visual")
	TSoftObjectPtr<UAnimSequence> WalkAnimAsset;
	UPROPERTY(EditDefaultsOnly, Category = "Validation|Visual")
	TSoftObjectPtr<UAnimSequence> RunAnimAsset;
	UPROPERTY(EditDefaultsOnly, Category = "Validation|Visual")
	TSoftObjectPtr<UAnimSequence> FallAnimAsset;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AnimIdle;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AnimWalk;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AnimRun;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> AnimFall;
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> CurrentAnim;

	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<UPGWeaponComponent> Weapon;

	UPROPERTY(VisibleInstanceOnly, Category = "Validation")
	float Health = 0.0f;

	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<UStaticMeshComponent> BodyVisual;

	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<USpringArmComponent> CameraArm;

	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY(VisibleAnywhere, Category = "Validation")
	TObjectPtr<UNavigationInvokerComponent> NavigationInvoker;
};
