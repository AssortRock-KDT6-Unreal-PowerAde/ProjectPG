// 공통 잠금 컴포넌트.
//
// 잠긴 상자(OBJ-009), 잠긴 금고(OBJ-011), 잠긴 문(OBJ-017), 특수 열쇠 문(OBJ-018),
// 잠긴 펜스 게이트(OBJ-020)는 전부 "원형 + 이 컴포넌트"다. 잠금 자체를 상자·문 클래스에
// 넣지 않는 이유는, 같은 규칙(열쇠 확인 → 소모 여부 → 상태 복제)을 한 곳에서만 고치기 위해서다.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PGLockComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPGLockStateChanged, bool, bLocked, APawn*, Pawn);

UCLASS(ClassGroup = (PG), meta = (BlueprintSpawnableComponent))
class PROJECTPG_API UPGLockComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPGLockComponent();

	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	bool IsLocked() const { return bLocked; }

	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	FName GetRequiredKeyId() const { return RequiredKeyId; }

	// Pawn이 열쇠를 가졌는지만 본다. 상태를 바꾸지 않는다.
	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	bool CanUnlock(const APawn* Pawn) const;

	// 서버 전용. 열쇠가 있으면 잠금을 풀고(필요하면 열쇠를 소모하고) true.
	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	bool TryUnlock(APawn* Pawn);

	// 서버 전용. 다시 잠근다 (퀘스트·장치 연동용).
	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	void SetLocked(bool bNewLocked);

	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	void Configure(bool bNewLocked, FName NewRequiredKeyId);

	UFUNCTION(BlueprintCallable, Category = "PG|Lock")
	FText GetLockedPrompt() const;

	UPROPERTY(BlueprintAssignable, Category = "PG|Lock")
	FPGLockStateChanged OnLockStateChanged;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Locked();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_Locked, Category = "PG|Lock")
	bool bLocked = true;

	// 어떤 열쇠로 열리는지. 비어 있으면 열쇠 없이도 잠겨 있는 것(장치로만 열림).
	// 멀티(9/27): 복제 — 클라이언트 안내 문구·판정에 쓴다. "잠김 (열쇠 필요)" 에 열쇠 이름이 나오게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Lock")
	FName RequiredKeyId;

	// 열쇠를 쓰면 없어지는지. 노션 "잠금 오브젝트의 열쇠 소모 여부"는 팀 협의 항목이라 기본 false.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Lock")
	bool bConsumeKey = false;
};
