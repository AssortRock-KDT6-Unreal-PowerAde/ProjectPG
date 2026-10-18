// 몬스터 AI 전체 흐름 그림: Source/ProjectPG/Private/AI/README.md

#include "AI/EnemyVoiceComponent.h"
#include "AI/EnemyAIConfig.h"
#include "AI/EnemyAILog.h"
#include "Characters/CustomEnemyCharacter.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Engine/World.h"

namespace
{
	// 목소리를 붙일 뼈 이름. 마네킹 계열 뼈대에 있는 머리 뼈. 없으면 엔진이 메시 원점에 붙인다.
	const FName VoiceSocketName(TEXT("head"));
}

UEnemyVoiceComponent::UEnemyVoiceComponent()
{
	// 서버가 정한 "목소리 틀어" 를 클라이언트로 보내야 해서 복제를 켠다. 매 프레임 할 일은 없다.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

const UEnemyAIConfig* UEnemyVoiceComponent::GetConfig() const
{
	const ACustomEnemyCharacter* Enemy = Cast<ACustomEnemyCharacter>(GetOwner());
	return Enemy ? Enemy->GetAIConfig() : nullptr;
}

const TArray<TObjectPtr<USoundBase>>* UEnemyVoiceComponent::GetSounds(const UEnemyAIConfig& Config, EEnemyVoiceLine Line) const
{
	switch (Line)
	{
	case EEnemyVoiceLine::TargetSpotted: return &Config.TargetSpottedSounds;
	case EEnemyVoiceLine::Hurt:          return &Config.HurtSounds;
	case EEnemyVoiceLine::Death:         return &Config.DeathSounds;
	}
	return nullptr;
}

float UEnemyVoiceComponent::GetCooldown(const UEnemyAIConfig& Config, EEnemyVoiceLine Line) const
{
	switch (Line)
	{
	case EEnemyVoiceLine::TargetSpotted: return Config.TargetSpottedCooldown;
	case EEnemyVoiceLine::Hurt:          return Config.HurtCooldown;
	default:                             return 0.f;
	}
}

bool UEnemyVoiceComponent::TryPlayLine(EEnemyVoiceLine Line)
{
	const UEnemyAIConfig* Config = GetConfig();
	if (!GetOwner() || !GetOwner()->HasAuthority() || !Config || bDeathPlayed) return false;

	const int32 Slot = static_cast<int32>(Line);
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextAllowedTime[Slot]) return false;
	NextAllowedTime[Slot] = Now + GetCooldown(*Config, Line);
	if (Line == EEnemyVoiceLine::Death) bDeathPlayed = true;

	// 소리 파일이 아직 없으면 조용히 넘어가지만, "외칠 때가 됐다"는 true 로 알려 동료 부르기는 계속 돈다.
	const TArray<TObjectPtr<USoundBase>>* Sounds = GetSounds(*Config, Line);
	if (Sounds && Sounds->Num() > 0)
		MulticastPlayLine(Line, static_cast<uint8>(FMath::RandHelper(FMath::Min(Sounds->Num(), 255))));
	return true;
}

void UEnemyVoiceComponent::MulticastPlayLine_Implementation(EEnemyVoiceLine Line, uint8 Index)
{
	// 전용 서버는 소리를 낼 스피커가 없다.
	if (GetNetMode() == NM_DedicatedServer) return;
	const UEnemyAIConfig* Config = GetConfig();
	const ACharacter* Enemy = Cast<ACharacter>(GetOwner());
	const TArray<TObjectPtr<USoundBase>>* Sounds = Config ? GetSounds(*Config, Line) : nullptr;
	if (!Enemy || !Sounds || !Sounds->IsValidIndex(Index) || !(*Sounds)[Index])
	{
		// 서버와 클라이언트의 AIConfig 목록이 다르면(다른 버전 접속 등) 여기로 온다.
		UE_LOG(LogEnemyAI, Warning, TEXT("[EnemyAI] %s: 목소리 %d번째 소리를 못 찾아 안 틂"), *GetNameSafe(GetOwner()), Index);
		return;
	}
	UE_LOG(LogEnemyAI, Log, TEXT("[EnemyAI] %s: 목소리 틂 %s"), *GetNameSafe(GetOwner()), *GetNameSafe((*Sounds)[Index]));

	// 앞 목소리가 아직 나오는 중이면 끊고 새 것을 튼다(맞으면 외치다가도 "악!" 으로 바뀌는 게 자연스러움).
	if (UAudioComponent* Previous = CurrentVoice.Get()) Previous->Stop();
	const float Pitch = FMath::FRandRange(Config->VoicePitchRange.X, Config->VoicePitchRange.Y);
	CurrentVoice = UGameplayStatics::SpawnSoundAttached((*Sounds)[Index], Enemy->GetMesh(), VoiceSocketName,
		FVector::ZeroVector, EAttachLocation::SnapToTarget, true, 1.f, Pitch, 0.f, Config->VoiceAttenuation);
}
