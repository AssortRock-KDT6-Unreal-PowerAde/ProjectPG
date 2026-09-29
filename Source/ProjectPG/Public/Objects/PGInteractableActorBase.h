// 모든 상호작용 오브젝트 원형의 공통 부모.
//
// 공통으로 갖는 것:
//  - ObjectId / 표시 이름 / 퀘스트 태그 (카탈로그 행에서 채움)
//  - 상호작용 시간(InteractSeconds): 0이면 즉시, 아니면 F 유지
//  - 사용 중 잠금(CurrentUser): 두 플레이어가 같은 상자를 동시에 열지 못하게 서버가 잠근다
//  - 서버 권한: 상태를 바꾸는 코드는 전부 HasAuthority()일 때만 돈다.
//    클라이언트는 UPGInteractionComponent의 Server RPC를 거쳐 여기 도착한다.
//
// 자식은 HandleInteract / CanInteractInternal / GetPromptInternal 세 개만 채운다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/Interactable.h"
#include "Objects/PGObjectTypes.h"
#include "PGInteractableActorBase.generated.h"

class UStaticMeshComponent;

// 멀티(9/27): 표(카탈로그) 줄에서 온 겉모습. 서버가 채워 복제하면 클라이언트가 같은 메시를 입힌다(ApplyCatalogLook).
// 왜: 전에는 ApplyCatalogRow 가 서버에서만 메시를 넣어서, 클라이언트 화면에는 상자·문·차/헬기 출구·퀘스트 물건이 안 보였고
//   F 로 잡을 수도 없었다(서버에는 충돌이 있어 보이지 않는 벽). 메시 컴포넌트를 통째로 복제하는 대신 "무슨 메시인가" 만 보낸다 —
//   바닥 아이템이 2천 개 넘게 깔려서 컴포넌트마다 복제하면 서버가 무거워진다.
USTRUCT()
struct FPGCatalogLook
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> SecondaryMesh;
	UPROPERTY()
	EPGSpawnSocketKind SocketKind = EPGSpawnSocketKind::Generic;
	UPROPERTY()
	bool bSet = false;
};

UCLASS(Abstract)
class PROJECTPG_API APGInteractableActorBase : public AActor, public IInteractable
{
	GENERATED_BODY()

public:
	APGInteractableActorBase();

	// ---- IInteractable ----
	virtual void Interact_Implementation(APawn* Interactor) override;
	virtual bool CanInteract_Implementation(APawn* Interactor) const override;
	virtual FText GetInteractionPrompt_Implementation() const override;
	virtual float GetHoldSeconds() const override { return InteractSeconds; }

	// F를 몇 초 유지해야 하는지. 상호작용 컴포넌트가 진행 바를 그릴 때 읽는다.
	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	float GetInteractSeconds() const { return InteractSeconds; }

	// 사용 중 잠금. 유지형 상호작용을 시작할 때 서버가 부른다. 이미 다른 사람이 쓰고 있으면 false.
	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	bool TryBeginUse(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	void EndUse(APawn* Pawn);

	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	bool IsInUse() const { return CurrentUser != nullptr; }

	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	APawn* GetCurrentUser() const { return CurrentUser; }

	// 서버: 지금 사용자가 잠금을 잡은 월드 시각. 누르고 있기(InteractSeconds)를 서버가 다시 재는 데 쓴다(9/27 멀티).
	double GetUseStartedAt() const { return UseStartedAt; }

	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	FName GetObjectId() const { return ObjectId; }

	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	FName GetQuestTag() const { return QuestTag; }

	// 카탈로그 행 하나를 이 액터에 적용한다. 자식은 Super를 부른 뒤 자기 값만 더 읽는다.
	UFUNCTION(BlueprintCallable, Category = "PG|Object")
	virtual void ApplyCatalogRow(const FPGObjectCatalogRow& Row);

	// 퀘스트·통계 담당이 구독한다. 서버에서만 브로드캐스트된다.
	UPROPERTY(BlueprintAssignable, Category = "PG|Object")
	FPGObjectInteractedSignature OnInteracted;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 자식이 채우는 세 함수. 서버에서만 불린다(HandleInteract).
	virtual bool CanInteractInternal(APawn* Interactor, FText& OutReason) const;
public:
	// 지금 못 쓰는 이유("연료통 필요" 등). 쓸 수 있으면 빈 글자. F 를 눌렀는데 막혔을 때 화면에 알려 주려고 연다(PGInteractionComponent).
	FText GetBlockedReason(APawn* Interactor) const { FText Reason; return CanInteractInternal(Interactor, Reason) ? FText::GetEmpty() : Reason; }
protected:
	virtual void HandleInteract(APawn* Interactor) {}
	virtual FText GetPromptInternal() const;

	// 소프트 참조 메시를 동기 로드해서 컴포넌트에 넣는다. 비어 있으면 아무것도 하지 않는다.
	static void ApplySoftMesh(UStaticMeshComponent* Component, const TSoftObjectPtr<UStaticMesh>& Mesh);

	UFUNCTION()
	virtual void OnRep_CurrentUser() {}

	// 표 줄의 겉모습을 컴포넌트에 입힌다 — 서버는 ApplyCatalogRow 에서, 클라이언트는 복제가 도착했을 때(OnRep_CatalogLook).
	// 자식은 뚜껑·문짝·출구 충돌처럼 "보이는 것·부딪히는 것" 만 여기서 더 한다(값은 ApplyCatalogRow + 복제 칸).
	virtual void ApplyCatalogLook();
	UFUNCTION()
	void OnRep_CatalogLook();
	UPROPERTY(ReplicatedUsing = OnRep_CatalogLook)
	FPGCatalogLook CatalogLook;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Object")
	TObjectPtr<USceneComponent> RootScene;

	// 겉모습. 자식 원형이 필요하면 뚜껑·문짝 같은 보조 메시를 더 붙인다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Object")
	TObjectPtr<UStaticMeshComponent> MeshComponent;

	// 아래 네 칸은 복제한다(9/27 멀티) — 클라이언트의 안내 문구("상자 열기 (5초 유지)")·누르고 있기 시간이 여기서 나온다.
	// 노션 오브젝트 ID (OBJ-001 등).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Object")
	FName ObjectId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Object")
	FText DisplayName;

	// 퀘스트 담당이 이 오브젝트의 이벤트를 구분하는 이름. 비어 있으면 퀘스트와 무관.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Object")
	FName QuestTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Object", meta = (ClampMin = "0.0"))
	float InteractSeconds = 0.0f;

	// 지금 이 오브젝트를 쓰고 있는 폰. 서버가 정하고 클라이언트로 복제된다.
	UPROPERTY(ReplicatedUsing = OnRep_CurrentUser, VisibleInstanceOnly, Category = "PG|Object")
	TObjectPtr<APawn> CurrentUser;
	double UseStartedAt = 0.0;
};
