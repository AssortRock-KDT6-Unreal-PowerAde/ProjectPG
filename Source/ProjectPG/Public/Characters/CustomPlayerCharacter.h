// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CustomGameplayTags.h"
#include "InputActionValue.h"
#include "Characters/CustomCharacter.h"
#include "Objects/PGItemReceiverInterface.h"
#include "CustomPlayerCharacter.generated.h"

class UCustomAbilitySystemComponent;
class UInventoryComponent;
class UPGInteractionComponent;
class UPGInteractionPromptWidget;
/**
 * 
 */
// IPGItemReceiver: 상자·바닥 아이템·시체·문 열쇠 같은 오브젝트(내 담당)가 "이 플레이어에게 아이템을 준다/있나/뺀다" 를 부르는 창구.
// 팀 인벤토리(UInventoryComponent, PlayerState 에 붙어 있음)로 이어 준다. (2026-09-19 팀 코드 합치기)
UCLASS()
class PROJECTPG_API ACustomPlayerCharacter : public ACustomCharacter, public IPGItemReceiver
{
	GENERATED_BODY()

public:
	ACustomPlayerCharacter();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UCameraComponent> CameraComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class USpringArmComponent> CameraArmComp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TObjectPtr<class UNativeActionComponent> NativeActionComp;

public:
	virtual void Tick(float DeltaTime) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UCustomAbilitySystemComponent* GetCustomAbilitySystemComponent() const;
	USpringArmComponent* GetCameraArm() const;

protected:
	virtual void BeginPlay() override;

	// ---- 상호작용·인벤토리 연결 (2026-09-19) ----
public:
	// NA_Interaction(F 키)이 이걸 꺼내 BeginInteract/EndInteract 를 부른다.
	UPGInteractionComponent* GetInteraction() const { return Interaction; }
	class UPGWigBeamComponent* GetWigBeam() const { return WigBeam; }

	// IPGItemReceiver
	virtual bool ReceiveItem_Implementation(FName ItemId, int32 Count) override;
	virtual bool HasItem_Implementation(FName ItemId, int32 Count) const override;
	virtual bool ConsumeItem_Implementation(FName ItemId, int32 Count) override;
	virtual FName GetPlayerKey_Implementation() const override;

protected:
	virtual void PossessedBy(AController* NewController) override;

	// 인벤토리는 캐릭터가 아니라 PlayerState 에 붙어 있다(팀 구조). 죽거나 리스폰해도 인벤토리가 남게 하려는 설계.
	UInventoryComponent* GetInventory() const;

	// 서버(로비 WebSocket)가 없을 때 주머니를 만들어 준다. 서버가 인벤토리를 보내 주면 그게 이걸 덮어쓴다.
	void EnsureOfflinePocket();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly)
	TObjectPtr<UPGInteractionComponent> Interaction;

	// 분홍 가발 광선(가발을 가지고 있으면 머리에 보이고 오른쪽 클릭으로 쏜다). 오브젝트 담당 추가(9/20).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG")
	TObjectPtr<class UPGWigBeamComponent> WigBeam;

	// "F 차량 탑승" 같은 안내 문구 위젯. 내 화면(로컬 플레이어)에서만 만든다.
	UPROPERTY(Transient)
	TObjectPtr<UPGInteractionPromptWidget> PromptWidget;

	// 화면 가운데 조준점 + 가발 광선 고리(9/23, WBP_PGCrosshair). 내 화면에서 조종할 때 한 번 만든다.
	UPROPERTY(Transient)
	TObjectPtr<class UPGCrosshairWidget> CrosshairWidget;
};
