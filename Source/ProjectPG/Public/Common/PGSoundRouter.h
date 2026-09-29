// 소리 신호 담당. (2026-09-23, 와이즈 도입 준비)
//
// 왜: 와이즈 플러그인이 아직 없다. 코드가 와이즈를 직접 부르면 설치 전에는 빌드도 안 된다.
//   그래서 코드는 "지금 이 이름의 소리를 내라"(Cue 이름) 신호만 보내고, 실제로 와이즈 이벤트를 내는 것은
//   이 클래스의 블루프린트 자식(BP_PGSoundRouter)이 한다 — 와이즈를 깔고 나서 C++ 를 한 줄도 안 고쳐도 된다.
// 어떻게 부르나(코드): PGSound::PlayAll(서버에서만 도는 곳 — 모두에게 한 번씩), PGSound::PlayLocal(이미 모두에게서 도는 곳·내 화면 UI).
// 블루프린트에서 할 일: 이벤트 PlayCue 를 구현 → CueTable(DT_PGSounds, 행 이름 = Cue 이름)에서 행을 찾아 → 와이즈 Post Event.
//   붙일 신호 목록과 순서: Docs/WwiseSoundGuide_2026-09-23.md.
// 멀티: 서버가 이 액터를 하나 만들고(항상 복제), PlayAll 은 이 액터의 Multicast 로 모든 사람에게 간다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "PGSoundRouter.generated.h"

// 소리 표 한 줄(DT_PGSounds). 행 이름 = Cue 이름(예: Weapon_Fire).
USTRUCT(BlueprintType)
struct PROJECTPG_API FPGSoundCueRow : public FTableRowBase
{
	GENERATED_BODY()

	// 낼 소리 — 와이즈 이벤트 에셋(AkAudioEvent)을 끼운다. 와이즈가 없는 지금은 어떤 에셋이든 들어가게 UObject 로 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	TSoftObjectPtr<UObject> Sound;

	// true = 소리 낸 액터에 붙여서(따라 움직임: 엔진·날갯짓), false = 그 자리에서 한 번(폭발·부딪힘).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sound")
	bool bAttachToSource = false;
};

UCLASS(Blueprintable)
class PROJECTPG_API APGSoundRouter : public AActor
{
	GENERATED_BODY()

public:
	APGSoundRouter();

	// 블루프린트가 구현: Cue 이름으로 표를 찾아 와이즈 이벤트를 낸다. Source 는 없을 수 있다(그때는 Location 에서).
	UFUNCTION(BlueprintImplementableEvent, Category = "PG|Sound")
	void PlayCue(FName Cue, AActor* Source, FVector Location);

	// 블루프린트가 쓰는 소리 표(DT_PGSounds). 코드는 읽지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "PG|Sound")
	TObjectPtr<UDataTable> CueTable;

	// 서버에서 불러 모든 사람에게(혼자 할 때는 나에게) 한 번씩 PlayCue.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastCue(FName Cue, AActor* Source, FVector_NetQuantize Location);

	// 이 기계에서만 PlayCue(+ 로그).
	void PlayHere(FName Cue, AActor* Source, const FVector& Location);
};

namespace PGSound
{
	// 서버에서만 도는 곳(총 쏘기·문 열기·몬스터 공격 등)에서 부른다. 모든 사람이 한 번씩 듣는다. 클라이언트에서 부르면 나만 듣는다.
	PROJECTPG_API void PlayAll(const UObject* WorldContext, FName Cue, AActor* Source, const FVector& Location);
	// 이미 모든 기계에서 도는 곳(Multicast·OnRep)이나 내 화면 UI 에서 부른다. 이 기계에서만 듣는다.
	PROJECTPG_API void PlayLocal(const UObject* WorldContext, FName Cue, AActor* Source, const FVector& Location);
}
