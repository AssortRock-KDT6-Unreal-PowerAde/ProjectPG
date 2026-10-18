#pragma once

#include "Characters/CustomCharacter.h"
#include "Common/TableData.h"
#include "CustomEnemyCharacter.generated.h"

UCLASS()
class PROJECTPG_API ACustomEnemyCharacter : public ACustomCharacter
{
	GENERATED_BODY()
public:
	ACustomEnemyCharacter(const FObjectInitializer& ObjectInitializer);
private:
	// AI 행동 습관과 상태 트리·EQS 에셋. BP(CBP_CustomEnemyCharacter)에서 DA_EnemyAI_Rifle 같은 것을 넣는다.
	// C++ 기본값으로 에셋 경로를 박지 않으려고 BP 칸으로 둠(쿠킹이 연결을 따라가게).
	UPROPERTY(EditDefaultsOnly, Category="AI")
	TObjectPtr<class UEnemyAIConfig> AIConfig;
	UPROPERTY(Transient) FEnemyTableRow EnemyData;
	// EnemyTable 에서 이 몬스터 행을 찾았는지. 못 찾았으면 AI 를 켜지 않는다.
	bool bHasEnemyData = false;
	// 전투 중에만 조준 각도를 매 프레임 갱신한다(AI 컨트롤러가 켜고 끔).
	bool bAimTracking = false;
	UPROPERTY(Replicated) FVector2D ReplicatedAimOffset = FVector2D::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<class AFirearmEquipment> EnemyWeapon;
	// 외침·비명을 내는 부품. 틀 소리는 AIConfig 의 Voice 칸에 있다.
	UPROPERTY(VisibleAnywhere, Category="AI")
	TObjectPtr<class UEnemyVoiceComponent> Voice;
public:
	class UEnemyVoiceComponent* GetVoice() const { return Voice; }
	const FEnemyTableRow& GetEnemyData() const { return EnemyData; }
	bool HasEnemyData() const { return bHasEnemyData; }
	FName GetCharacterID() const { return CharacterID; }
	const class UEnemyAIConfig* GetAIConfig() const { return AIConfig; }
	void SetAimTracking(bool bEnabled);
	FVector2D GetEnemyAimOffset() const { return ReplicatedAimOffset; }
	virtual bool HasInfiniteReserveAmmo() const override { return true; }
	virtual void GetCombatViewPoint(FVector& Location, FRotator& Rotation) const override;
	virtual void Hit_Implementation(float Damage, AActor* DamageCauser, const FHitResult& HitResult) override;
	virtual void Tick(float DeltaTime) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
};
