// 바닥 아이템 원형 (노션 "바닥 아이템 원형", OBJ-027 ~ OBJ-052).
//
// 총기·근접무기·탄약·회복제·장비·열쇠·연료통·재화·퀘스트 아이템이 전부 이 클래스다.
// 무엇인지는 ItemId 하나로 정해지고, 겉모습은 카탈로그 행의 Mesh를 쓴다.
// 전투 로직(총기 발사 등)은 무기 담당이며, 여기서는 "월드에 놓여 있고 F로 줍는다"만 책임진다.
//
// 동시 획득 방지: 두 클라이언트가 같은 프레임에 주워도 서버의 HandleInteract는
// 한 번만 성공한다(첫 호출에서 Destroy → 두 번째 호출은 IsValid 실패).
#pragma once

#include "CoreMinimal.h"
#include "Objects/PGInteractableActorBase.h"
#include "Engine/NetSerialization.h"
#include "PGFloorItemActor.generated.h"

class UMaterialInterface;
class UParticleSystem;
class UStaticMesh;

UCLASS()
class PROJECTPG_API APGFloorItemActor : public APGInteractableActorBase
{
	GENERATED_BODY()

public:
	APGFloorItemActor();

	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row) override;
	// 멀티(9/27): 표 줄 겉모습을 서버·클라 모두에서 입힌다(APGInteractableActorBase::ApplyCatalogLook).
	virtual void ApplyCatalogLook() override;

	UFUNCTION(BlueprintCallable, Category = "PG|FloorItem")
	void SetItem(FName InItemId, int32 InCount);

	UFUNCTION(BlueprintCallable, Category = "PG|FloorItem")
	FName GetItemId() const { return ItemId; }

	UFUNCTION(BlueprintCallable, Category = "PG|FloorItem")
	int32 GetCount() const { return Count; }

	// 서버 전용. 겉모습 변형 번호(연료통 색 등). 스포너가 시드로 정한다.
	void SetVisualVariant(int32 InVariant);

	// 서버 전용. 몬스터 드롭·인벤토리에서 버리기가 쓰는 생성 도우미.
	UFUNCTION(BlueprintCallable, Category = "PG|FloorItem", meta = (WorldContext = "WorldContextObject"))
	static APGFloorItemActor* SpawnDrop(UObject* WorldContextObject, FName ItemId, int32 Count, const FTransform& Transform);

	// 연료통은 "굴러다니는 물건" 이다(9/21 사용자). 차에 치이거나 총에 맞으면 날아가고, 너무 많이 맞으면 터진다.
	// 다른 바닥 아이템은 전처럼 제자리에 가만히 있다 — 탄약·열쇠가 굴러다니면 줍기만 번거롭다.
	bool IsLooseObject() const;

	virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// 연료통이 버티는 피해량. 소총 몇 발이면 터진다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|FloorItem|Fuel", meta = (ClampMin = "1.0"))
	float FuelHealth = 100.0f;
	// 터질 때 주변에 주는 피해와 반경(cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|FloorItem|Fuel")
	float FuelBlastDamage = 80.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|FloorItem|Fuel")
	float FuelBlastRadius = 600.0f;

protected:
	virtual void BeginPlay() override;
	// 루트를 메시로 바꾸고 물리를 켠다. 루트가 빈 씬이면 메시만 굴러가고 액터 자리는 남아, 줍기·거리 검사가 엉뚱한 곳을 본다.
	void MakeLoose();
	void Explode(AController* EventInstigator);
	// 멀티(9/27): 폭발의 그림(흙먼지 + 부푸는 구)을 모든 화면에. 전에는 서버에서만 그려 전용 서버 판에서는 아무도 못 봤다.
	//   믿을 수 있는 방송(Reliable): 바로 뒤에 액터를 지우므로 신뢰 안 함으로 보내면 빠질 수 있다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFuelBlastFx(FVector_NetQuantize Where);
	void PlayFuelBlastFx(const FVector& Where);
	float FuelDamageTaken = 0.0f;
	bool bExploded = false;

	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const override;
	virtual void HandleInteract(APawn* Interactor) override;
	virtual FText GetPromptInternal() const override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 입는 장비(하의·신발·가방 색)면 색 표의 메시·머티리얼을 입힌다. 서버는 SetItem 에서, 클라는 OnRep 에서.
	void ApplyWearableVisual();

	// 등급 색 테두리(레어 파랑·에픽 보라)를 덧그린다. 노말이거나 재질 애셋이 없으면 지운다.
	void ApplyGradeOverlay();

	UFUNCTION()
	void OnRep_ItemId();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ItemId, Category = "PG|FloorItem")
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|FloorItem", meta = (ClampMin = "1"))
	int32 Count = 1;

	// 겉모습 변형 번호. ItemId 와 같이 처음 복제될 때 오므로, 클라의 OnRep_ItemId 에서 이미 값이 들어 있다.
	UPROPERTY(Replicated, VisibleInstanceOnly, Category = "PG|FloorItem")
	uint8 VisualVariant = 0;

public:
	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGFloorItem 에서 바꾼다. 기본값 = 원래 코드 에셋) ----
	// 연료통이 터질 때: 흙먼지와 빛나는 폭발 공(0.5초 동안 커지며 사라진다).
	UPROPERTY(EditDefaultsOnly, Category = "PG|FloorItem|Visual")
	TSoftObjectPtr<UParticleSystem> FuelBlastDust;

	UPROPERTY(EditDefaultsOnly, Category = "PG|FloorItem|Visual")
	TSoftObjectPtr<UStaticMesh> FuelBlastMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|FloorItem|Visual")
	TSoftObjectPtr<UMaterialInterface> FuelBlastMaterial;

	// 세울 클래스: 설정(ProjectPG Visuals > Floor Item Class)에 블루프린트가 있으면 그것, 없으면 C++.
	static UClass* GetSpawnClass();
};
