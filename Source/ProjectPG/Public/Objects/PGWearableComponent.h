// 착장 겉모습 컴포넌트 (캐릭터에 붙인다). "무엇을 입고 있나"를 ItemId 로 들고 있다가 파츠 메시·머티리얼로 보여 준다.
//
// 몸은 Quantum 모듈 캐릭터 방식 그대로다: 머리 메시가 캐릭터의 본체(GetMesh)이고, 팔·셔츠·바지·조끼 등은 각자 스켈레탈 메시
// 컴포넌트로 붙어서 본체의 포즈를 그대로 따라간다(SetLeaderPoseComponent). 애니메이션은 본체 하나만 돌리면 된다.
// 모자·배낭처럼 뼈대가 없는 것은 스태틱 메시를 뼈에 붙인다.
//
// 팀 장비 시스템(UEquipComponent)과의 관계: 그쪽은 "무엇을 장착했나(데이터·능력치)"를 맡고, 이 컴포넌트는 "어떻게 보이나"만 맡는다.
// 장착이 바뀔 때 Equip(ItemId) / Unequip(Slot) 만 불러 주면 된다. 옷은 스태틱 메시를 소켓에 붙이는 방식으로는 몸을 못 따라가서
// (팔을 들면 소매가 따로 논다) 스켈레탈 + LeaderPose 가 필요하다.
//
// 네트워크: EquippedItems 가 리플리케이트된다. 서버가 Equip 하면 모든 클라이언트가 OnRep 에서 같은 겉모습을 만든다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Objects/PGWearableColors.h"
#include "PGWearableComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGWearableChanged, EPGWearSlot, Slot, FName, ItemId);

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGWearableComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGWearableComponent();

	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 서버 전용. ItemId 가 옷이면 그 슬롯에 입히고 true. 입고 있던 것이 있으면 OutPrevious 로 돌려준다(바닥에 떨굴지는 부르는 쪽이 정한다).
	UFUNCTION(BlueprintCallable, Category = "PG|Wearable")
	bool Equip(FName ItemId, FName& OutPrevious);

	// 서버 전용. 슬롯을 비우고 입고 있던 ItemId 를 돌려준다. 상·하의는 몸을 대신하는 메시라 비울 수 없다(None 반환).
	UFUNCTION(BlueprintCallable, Category = "PG|Wearable")
	FName Unequip(EPGWearSlot Slot);

	UFUNCTION(BlueprintCallable, Category = "PG|Wearable")
	FName GetEquipped(EPGWearSlot Slot) const;

	// 1인칭일 때 자기 몸을 숨기는 것처럼, 본체 메시의 "주인에게 안 보임"을 파츠 전부에 똑같이 적용한다.
	UFUNCTION(BlueprintCallable, Category = "PG|Wearable")
	void SetOwnerNoSee(bool bNoSee);

	UPROPERTY(BlueprintAssignable, Category = "PG|Wearable")
	FPGWearableChanged OnWearableChanged;

	// 뼈에 붙는 파츠(모자·배낭)의 위치를 런타임에 바꿔 본다(PG.WearTune). 표의 AttachOffset 대신 이 값을 쓴다. 로그에 표에 옮겨 적을 값이 찍힌다.
	UFUNCTION(BlueprintCallable, Category = "PG|Wearable|Debug")
	void TuneSlot(EPGWearSlot Slot, FTransform Offset);

	// 서버 전용. 권총집 메시 안에 꽂힌 권총(M_Pistol 섹션)을 보일지. 권총을 갖고 있고 손에 안 들었을 때 true.
	// 권총집이 없으면 아무 일도 없다. 캐릭터(무기 담당)가 권총 소유·장착이 바뀔 때 불러 준다.
	UFUNCTION(BlueprintCallable, Category = "PG|Wearable")
	void SetHolsteredPistolVisible(bool bVisible);

	// true 면 BeginPlay 에서 주인 캐릭터의 본체 메시를 Quantum 머리로 바꾸고 팔을 붙인다. 팀 캐릭터가 이미 그렇게 돼 있으면 끈다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	bool bBuildQuantumBody = true;

	// 몸 메시(머리 = 본체, 팔 = 본체 포즈를 따라가는 부품). bBuildQuantumBody 일 때만 쓴다.
	// 9/23 블루프린트 분리 — 캐릭터 블루프린트의 이 부품에서 바꾼다. 기본값 = Quantum 머리·팔 모듈.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable|Visual")
	TSoftObjectPtr<USkeletalMesh> BodyHeadMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable|Visual")
	TSoftObjectPtr<USkeletalMesh> BodyArmsMesh;

	// 처음부터 입고 있는 것. 상·하의는 반드시 있어야 몸이 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Wearable")
	TArray<FName> DefaultItems = { FName(TEXT("Shirt")), FName(TEXT("Pants")) };

protected:
	UFUNCTION()
	void OnRep_EquippedItems();

	UFUNCTION()
	void OnRep_HolsteredPistol();

	// 권총집 파츠의 M_Pistol 섹션을 bHolsteredPistolVisible 에 맞춘다.
	void ApplyHolsteredPistol();

	// 처음엔 빈 권총집(권총을 주워야 꽂힌다).
	UPROPERTY(ReplicatedUsing = OnRep_HolsteredPistol, VisibleInstanceOnly, Category = "PG|Wearable")
	bool bHolsteredPistolVisible = false;

	// EquippedItems 와 화면을 맞춘다. 서버·클라이언트 공통.
	void RefreshVisuals();
	void ApplySlot(EPGWearSlot Slot, FName ItemId);
	USkeletalMeshComponent* GetBodyMesh() const;
	template <typename T> T* MakePart(const FName& Name);

	// 슬롯 번호 = 배열 번호. 비어 있으면 NAME_None.
	UPROPERTY(ReplicatedUsing = OnRep_EquippedItems, VisibleInstanceOnly, Category = "PG|Wearable")
	TArray<FName> EquippedItems;

	UPROPERTY(Transient)
	TMap<EPGWearSlot, TObjectPtr<USkeletalMeshComponent>> SkeletalParts;

	UPROPERTY(Transient)
	TMap<EPGWearSlot, TObjectPtr<UStaticMeshComponent>> StaticParts;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> ArmsPart;

	bool bOwnerNoSee = false;

	// PG.WearTune 로 넣은 값. 로컬 확인용이라 리플리케이트하지 않는다.
	TMap<EPGWearSlot, FTransform> TuneOverrides;
};
