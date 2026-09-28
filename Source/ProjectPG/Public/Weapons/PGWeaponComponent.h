// 무기 컴포넌트. 폰에 붙여서 "주운 무기 중 하나를 들고, 공격 입력이 오면 트레이스 한 번" 을 담당한다.
//
// 인벤토리는 이 컴포넌트가 갖지 않는다. 탄약 확인·소모는 IPGItemReceiver(GiveItem/HasItem/ConsumeItem)로만
// 하므로 검증용 캐릭터의 TMap이든 팀의 UInventoryComponent든 그대로 붙는다.
// 공격은 서버에서만 판정한다(오브젝트 Interact와 같은 규칙). 클라이언트는 조준점만 보낸다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Weapons/PGWeaponTypes.h"
#include "PGWeaponComponent.generated.h"

class UDataTable;
class UStaticMeshComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGWeaponEquippedSignature, UPGWeaponComponent*, Weapon, FName, ItemId);

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGWeaponComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGWeaponComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ---- 데이터 ----
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	void RegisterWeapon(const FPGWeaponDef& Def);

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	void LoadFromDataTable(UDataTable* Table);

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	bool IsWeaponItem(FName ItemId) const { return Defs.Contains(ItemId); }

	const FPGWeaponDef* FindDef(FName ItemId) const { return Defs.Find(ItemId); }

	// 코드에 박아 둔 기본 무기 표. DataTable이 없을 때의 기본값(오브젝트 카탈로그와 같은 방식).
	static void RegisterDefaultWeapons(UPGWeaponComponent& Component);

	// 무기 표(데이터 테이블 DT_PGWeapons, 행 = FPGWeaponDef)가 설정(ProjectPG Visuals > Weapon Table)에 있으면
	// 게임 월드에서 이 부품이 붙을 때 기본 표 위에 덮어쓴다(같은 ItemId 는 표 값). 비어 있으면 코드 기본 표 그대로. (9/23 블루프린트 분리)
	virtual void OnRegister() override;

	// 도구용: 코드 기본 무기 표를 데이터 테이블에 행으로 적는다. DT_PGWeapons 를 처음 만들 때 한 번(Tools/wbp/make_weapon_table.py). 적은 행 수.
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon|Tools")
	static int32 WriteDefaultWeaponsToTable(UDataTable* Table);

	// 시험용: 지금 무기 표를 한 줄씩 로그로(PG.VisualProbe 가 표 적용 전·후를 비교할 때 쓴다).
	void LogWeaponDefs() const;

	// ---- 소유·장착 ----
	// 인벤토리에 아이템이 들어올 때 캐릭터가 불러 준다. 무기면 목록에 넣고, 든 게 없으면 바로 든다.
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	void NotifyItemReceived(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	bool Equip(FName ItemId);

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	void Unequip();

	// 주운 무기들 사이를 순서대로 넘긴다(Q 키).
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	bool CycleNext();

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	FName GetEquippedItemId() const { return EquippedItemId; }

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	const TArray<FName>& GetOwnedWeapons() const { return OwnedWeapons; }

	// ---- 공격 ----
	// 소유 클라이언트에서 부른다. 조준 방향을 구해 서버로 보내고, 서버가 트레이스·피해를 판정한다.
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	void Attack();

	UPROPERTY(BlueprintAssignable, Category = "PG|Weapon")
	FPGWeaponEquippedSignature OnEquipped;

	// 애니메이션·이펙트가 아직 없어서 트레이스를 선으로 그려 준다. 맞으면 빨강, 빗나가면 초록.
	UPROPERTY(EditAnywhere, Category = "PG|Weapon|Debug")
	bool bDrawDebugTrace = false;

	UPROPERTY(EditAnywhere, Category = "PG|Weapon|Debug")
	float DebugTraceSeconds = 1.0f;

	// 1인칭 카메라를 가진 로컬 플레이어는 손 소켓 대신 카메라 앞에 무기를 붙인다.
	// 몸이 OwnerNoSee라 손에 붙이면 본인 화면에 안 보이기 때문.
	UPROPERTY(EditAnywhere, Category = "PG|Weapon")
	FVector FirstPersonOffset = FVector(30.0f, 15.0f, -15.0f);

	// false(3인칭)면 본인 화면에서도 손 소켓에 붙인다. 카메라에 붙이면 3인칭에서는 총이 허공에 떠 보인다.
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon")
	void SetFirstPersonView(bool bInFirstPerson) { bFirstPersonView = bInFirstPerson; ApplyEquippedVisual(); }

	bool bFirstPersonView = true;

	// 3인칭에서 손에 든 무기 방향 = 몸 기준(true) / 손뼈 기준(false).
	// 왜 몸 기준이 기본인가: 무기 애니가 없어서 손은 걷기·서기 애니대로 흔들리고, 손뼈 축(X = 손가락 방향)에 붙이면 팔을 내린 자세에서
	// 총구가 땅이나 하늘을 보며 거꾸로 들렸다. 위치만 손을 따라가고 방향은 몸 정면으로 고정하면 애니와 상관없이 "들고 있는" 모양이 된다.
	// 무기 애니가 들어오면 false 로 바꾸고 손뼈 기준 AttachRotation 을 다시 맞춘다.
	UPROPERTY(EditAnywhere, Category = "PG|Weapon")
	bool bHoldInBodySpace = true;

	// 들고 있는 무기의 붙는 위치를 런타임에 바꿔 본다(PG.WeaponTune). 정답을 찾으면 기본 표에 적는다.
	UFUNCTION(BlueprintCallable, Category = "PG|Weapon|Debug")
	void TuneEquipped(bool bFirstPerson, FVector Location, FRotator Rotation, float Scale);

	UFUNCTION(BlueprintCallable, Category = "PG|Weapon|Debug")
	FString DescribeEquippedTuning() const;

protected:
	UFUNCTION(Server, Reliable)
	void ServerAttack(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction);

	UFUNCTION(Server, Reliable)
	void ServerEquip(FName ItemId);

	UFUNCTION()
	void OnRep_EquippedItemId();

	void ApplyEquippedVisual();
	bool GetAimOriginAndDirection(FVector& OutOrigin, FVector& OutDirection) const;
	bool PerformAttack(const FPGWeaponDef& Def, const FVector& Origin, const FVector& Direction);

	UPROPERTY()
	TMap<FName, FPGWeaponDef> Defs;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedItemId, VisibleInstanceOnly, Category = "PG|Weapon")
	FName EquippedItemId;

	UPROPERTY(VisibleInstanceOnly, Category = "PG|Weapon")
	TArray<FName> OwnedWeapons;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> HeldMesh;

	float LastAttackTime = -1000.0f;
	// 지금 몸 기준으로 손 위치를 따라가는 중인지(3인칭 + 손 소켓 있음).
	bool bHeldInHandBodySpace = false;
	// 무기 표를 이미 덮어썼나(부품이 다시 등록돼도 한 번만).
	bool bWeaponTableApplied = false;
};
