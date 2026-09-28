#include "Combat/PGMonsterLookComponent.h"

#include "Monster/PGMonsterCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "TimerManager.h"

UPGMonsterLookComponent::UPGMonsterLookComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPGMonsterLookComponent::Attach(AActor* Monster, FName InPreset)
{
	if (!IsValid(Monster) || !Monster->HasAuthority() || InPreset.IsNone())
		return;
	UPGMonsterLookComponent* Look = Monster->FindComponentByClass<UPGMonsterLookComponent>();
	if (!Look)
	{
		Look = NewObject<UPGMonsterLookComponent>(Monster, TEXT("PGMonsterLook"));
		Look->RegisterComponent();
	}
	Look->Preset = InPreset;
}

void UPGMonsterLookComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGMonsterLookComponent, Preset);
}

void UPGMonsterLookComponent::BeginPlay()
{
	Super::BeginPlay();
	// 몬스터 BeginPlay 는 이 부품 BeginPlay 뒤에 자기 기본 모양을 다시 입힌다 — 다음 틱에 덮는다.
	if (GetOwner() && !GetOwner()->HasAuthority())
		GetWorld()->GetTimerManager().SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UPGMonsterLookComponent::ApplyOnClient));
}

void UPGMonsterLookComponent::OnRep_Preset()
{
	ApplyOnClient();
}

void UPGMonsterLookComponent::ApplyOnClient()
{
	APGMonsterCharacter* Monster = Cast<APGMonsterCharacter>(GetOwner());
	if (!IsValid(Monster) || Monster->HasAuthority() || Preset.IsNone() || !Monster->HasActorBegunPlay())
		return;
	FPGMonsterVisuals Visuals;
	if (APGMonsterCharacter::GetPresetVisuals(Preset, Visuals))
	{
		Monster->SetVisuals(Visuals);
		// 멀티(9/28): 옮긴 메시 자리를 기준으로 다시 기억시킨다 — 안 하면 엔진의 "부드럽게 하기" 가 매 프레임 처음 자리로
		//   되돌려 몬스터가 떠 있거나 땅에 묻혀 보인다(로봇과 같은 문제, PGRobotCharacter::BeginPlay 주석).
		if (USkeletalMeshComponent* Body = Monster->GetMesh())
			Monster->CacheInitialMeshOffset(Body->GetRelativeLocation(), Body->GetRelativeRotation());
	}
}
