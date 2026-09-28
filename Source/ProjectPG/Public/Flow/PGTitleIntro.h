// 시작 로고와 타이틀 화면 사이의 짧은 인트로. (2026-09-22)
//
// 순서: 시작 로고(UPGLoadingScreenSubsystem) → 인트로(이 파일) → 타이틀 위젯이 서서히 나타난다.
//   게임을 켜고 처음 타이틀이 열릴 때 한 번만 튼다. 로비에서 "타이틀로" 돌아오면 바로 타이틀이다.
//
// 왜 시퀀서(레벨 시퀀스 에셋)가 아니라 코드인가: 타이틀 무대 자체가 코드로 지어진다(PGFlowStage). 무대 위의 캐릭터·카메라는
//   레벨에 저장된 액터가 아니라서 시퀀스가 붙잡을 대상이 없다. 그리고 에셋이면 팀 저장소로 옮길 때 .uasset 이 같이 가야 하고
//   머지 충돌이 난다. 그래서 "시간표(몇 초에 무엇을)" 를 코드로 적고, 무대에 있는 것(카메라·주인공)을 그대로 움직인다.
//
// 두 가지를 만들어 두고 설정(UPGFlowSettings::TitleIntroVariant)으로 고른다(사용자 9/22: "둘 다 만들어 줘, 골라 쓰겠다").
//   - ShipDragon: 하늘에서 시작 → 전함이 머리 위를 지나가고 드래곤이 쫓아간다 → 카메라가 내려와 캐릭터를 잡는다.
//   - Rescue:     주인공이 소총으로 작은 몹을 쓰러뜨림 → 장전하는 사이 뒤에서 두 마리가 덤벼 위기 → 날으는 변신 차가 빔으로 구함
//                 → 옆에 내려앉아 여고생으로 돌아옴 → 둘이 나란히 선 그림 → 까맣게 덮었다 걷히면 평소 타이틀(여고생은 인트로에만).
//
// 누가 돌리나: 타이틀 게임모드의 Tick 이 PGFlowStage::Animate(평소 타이틀 카메라) 다음에 Advance 를 부른다.
//   순서가 정해져 있어야 "평소 카메라 자리" 를 읽어 마지막 장면의 도착점으로 쓸 수 있다(두 액터가 따로 틱하면 순서가 보장되지 않는다).
// 건너뛰기: 아무 키나 마우스 클릭. 마지막 상태(인트로용 액터 치움, 카메라는 타이틀 자리, 위젯 표시)로 바로 간다.
#pragma once

#include "CoreMinimal.h"
#include "Engine/Scene.h" // FPostProcessSettings(습격 인트로가 화면 설정 원본을 들고 있다)
#include "GameFramework/Actor.h"
#include "Flow/PGFlowSettings.h"
#include "PGTitleIntro.generated.h"

class ACameraActor;
class APGTransformNPCActor;
class ASkeletalMeshActor;
class UAnimSequence;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMeshComponent;
class SWidget;
class FPGTitleIntroSkipInput;

// 카메라 한 컷. 위치와 "보는 점" 으로 적는다 — 회전 각도로 적으면 사람이 읽고 고치기 어렵다.
struct FPGIntroCamKey
{
	float Time = 0.0f;
	FVector Location = FVector::ZeroVector;
	FVector LookAt = FVector::ZeroVector;
	float Fov = 55.0f;
	float Roll = 0.0f; // 화면 기울기(도). 위기 장면에서 살짝 비튼다.
};

// 움직이는 것(전함·드래곤·차)의 길 한 점. 점들 사이는 부드러운 곡선(캣멀-롬)으로 잇는다.
struct FPGIntroPathKey
{
	float Time = 0.0f;
	FVector Location = FVector::ZeroVector;
};

enum class EPGTitleIntroPhase : uint8
{
	WaitingForSplash, // 로고가 떠 있는 동안 첫 컷에 멈춰 있는다
	Playing,
	Done,             // 끝났거나 건너뜀. 타이틀 위젯을 띄워도 된다
};

// 구출 인트로의 몹 한 마리. 애니 에셋은 인트로 액터의 KeepAlive 가 붙들고 있어서 여기서는 맨 포인터로 둔다.
struct FPGIntroMonster
{
	TWeakObjectPtr<ASkeletalMeshActor> Actor;
	UAnimSequence* Run = nullptr;
	UAnimSequence* Attack = nullptr;
	UAnimSequence* Die = nullptr;
	float MeshYaw = -90.0f;  // 팩 메시가 바라보는 방향 보정(프리셋의 MeshRotation)
	FVector From = FVector::ZeroVector;
	FVector To = FVector::ZeroVector;
	float RunStart = 0.0f;
	float RunEnd = 0.0f;
	float AttackAt = -1.0f;
	float HitAt = 0.0f;                  // 맞아 쓰러지는 시각
	FVector FlyTo = FVector::ZeroVector; // 맞고 날아가 떨어지는 자리
	float FlyHeight = 150.0f;
	float FlySpinYaw = 0.0f;             // 날아가며 도는 양(도)
	bool bAttacked = false;
	bool bHit = false;
	bool bRunning = false;
	bool bGone = false;
};

// 두 인트로가 같이 쓰는 뼈대: 로고 기다리기, 건너뛰기, 위아래 검은 띠, 카메라 컷 잇기, 시험용 스크린샷.
UCLASS(Abstract, NotBlueprintable, Transient)
class PROJECTPG_API APGTitleIntro : public AActor
{
	GENERATED_BODY()

public:
	APGTitleIntro();

	// 조건이 맞으면 인트로를 만들어 돌려준다. 이미 이 게임에서 봤거나, 화면이 없거나(헤드리스), 설정이 None 이면 nullptr.
	static APGTitleIntro* StartOnce(UWorld* World, ACameraActor* StageCamera);

	// 지금 쓸 인트로 종류: 명령줄 -PGIntro=<이름> > 콘솔 PG.Flow.Intro > 프로젝트 설정.
	static EPGTitleIntroVariant GetActiveVariant();

	// 게임모드가 매 틱 부른다(PGFlowStage::Animate 다음). 끝난 뒤에도 계속 불러도 된다 — 뒷정리(방어막 걷기 등)를 한다.
	void Advance(float DeltaSeconds);
	bool IsFinished() const { return Phase == EPGTitleIntroPhase::Done; }

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	// ---- 종류별로 채우는 곳 ----
	virtual float GetDuration() const { return 10.0f; }
	virtual const TCHAR* GetVariantName() const { return TEXT("?"); }
	// 로고가 떠 있는 동안(화면이 까만 동안) 액터를 만든다. 무거운 에셋 읽기도 여기서 끝낸다.
	virtual void BeginShots() {}
	// T = 인트로 시작 뒤 초. 액터를 움직이고, 카메라 흔들림을 돌려준다.
	virtual void TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation) {}
	// 끝(또는 건너뛰기). 인트로용 액터를 치우고 무대를 원래대로 돌린다.
	virtual void CleanupShots(bool bSkipped) {}
	// 끝난 뒤에도 매 틱.
	virtual void TickAfterFinish(float DeltaSeconds) {}

	// 카메라 컷 목록(시간순). 마지막에 "평소 타이틀 카메라" 가 GetDuration() 시각의 컷으로 자동으로 붙는다.
	TArray<FPGIntroCamKey> CamKeys;
	// 시험용 스크린샷을 찍을 인트로 시각들(-PGIntroShots 일 때만).
	TArray<float> ShotTimes;

	// ---- 같이 쓰는 도구 ----
	// 디스크에 없으면 경고 한 줄 남기고 nullptr(팩이 없는 PC 에서도 인트로가 멈추지 않는다).
	template <typename T>
	T* LoadIntroAsset(const TCHAR* ObjectPath)
	{
		T* Asset = Cast<T>(LoadIntroObject(ObjectPath, T::StaticClass()));
		if (Asset)
			KeepAlive.Add(Asset);
		return Asset;
	}
	static UObject* LoadIntroObject(const TCHAR* ObjectPath, UClass* Class);
	// 데이터 에셋(DA_PGTitleIntro·DA_PGFlowStage) 칸 → 경로 글자. 칸이 비면 빈 글자 = 그 조각을 조용히 뺀다(9/23 블루프린트 분리).
	template <typename TSlot>
	static FString IntroPath(const TSlot& Slot)
	{
		return Slot.ToSoftObjectPath().ToString();
	}
	ASkeletalMeshActor* SpawnSkeletal(USkeletalMesh* Mesh, const FTransform& Transform);
	UStaticMeshComponent* AddBeamComponent(AActor* BeamOwner);
	// 빔 메시(피벗에서 +X 로 100cm)를 From→To 로 늘여 보여 준다.
	static void ShowBeam(UStaticMeshComponent* Beam, const FVector& From, const FVector& To, float Thickness);
	void SpawnDust(const FVector& Location, float Scale);
	// 총알에 맞은 작은 효과(소총 명중용). 흙먼지 폭발(SpawnDust)은 빔 같은 큰 공격에만.
	void SpawnHitSpark(const FVector& Location, float Scale);
	// 네 점 캣멀-롬 곡선. U=0 이면 P1, U=1 이면 P2 를 지난다(점을 "지나가는" 곡선이라 길을 적기 쉽다).
	static FVector CatmullRom(const FVector& P0, const FVector& P1, const FVector& P2, const FVector& P3, float U);
	// 시간표 길에서 T 초의 자리. 처음보다 이르면 첫 점, 끝보다 늦으면 끝 점.
	static FVector EvalPath(const TArray<FPGIntroPathKey>& Keys, float T);
	// 길 위의 진행 방향(앞뒤 0.05초 차이). 몸을 진행 방향으로 돌릴 때 쓴다.
	static FVector PathVelocity(const TArray<FPGIntroPathKey>& Keys, float T);

	TWeakObjectPtr<ACameraActor> Camera;
	float TitleFov = 55.0f;
	// 시험 촬영(-PGIntroShots) 중인가. 이때만 자세한 로그를 남긴다.
	bool IsTestRun() const { return bTestShots; }
	// 끝에 화면을 까맣게 덮었다 걷는 시간(초). 0 이면 안 한다. 까만 동안 CleanupShots 가 불린다(구출 인트로: 여고생을 치우는 자리).
	virtual float GetEndFadeOutSeconds() const { return 0.0f; }
	virtual float GetEndFadeInSeconds() const { return 0.0f; }
	// 까맣게 된 채로 더 버티는 시간(초). 페이드 없이(GetEndFadeOutSeconds = 0) 이 값만 주면 "뚝 끊고 까만 화면" 이 된다(습격 인트로).
	virtual float GetEndHoldSeconds() const { return 0.0f; }
	// 마지막 컷을 평소 타이틀 카메라로 이을지. 끊어서 끝내는 인트로는 타이틀 구도로 흘러가면 안 된다(까만 화면 뒤에서 바뀐다).
	virtual bool EndsOnTitleShot() const { return true; }
	// 위아래 검은 띠를 끝까지 둘지(끊어서 끝내는 인트로). 아니면 끝나기 1초 전부터 걷는다.
	virtual bool KeepLetterboxToEnd() const { return false; }

	// 읽어 온 에셋(메시·애니)을 인트로가 끝날 때까지 붙들어 둔다. 재생 중인 애니가 가비지 수거에 걸리면 안 된다.
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> KeepAlive;

private:
	void Begin(ACameraActor* StageCamera);
	void Finish(bool bSkipped);
	FPGIntroCamKey EvaluateCamera(float T, const FPGIntroCamKey& TitleKey) const;
	void ApplyCamera(const FPGIntroCamKey& Pose, const FVector& ShakeLocation, const FRotator& ShakeRotation);
	void BuildLetterbox();
	void SetLetterboxOpacity(float Opacity);
	void RemoveLetterbox();
	void RegisterSkipInput();
	void UnregisterSkipInput();
	void TickTestShots(float DeltaSeconds);

	EPGTitleIntroPhase Phase = EPGTitleIntroPhase::WaitingForSplash;
	float Clock = 0.0f;
	// 인트로 동안 움직임 흐림(모션 블러)을 줄였다가 끝나면 타이틀 카메라 값으로 돌려놓는다.
	bool bSavedBlurOverride = false;
	float SavedBlurAmount = 0.0f;
	// 끝 페이드(GetEndFadeOutSeconds). 까맣게 덮는 중이면 경과 초, 아니면 -1.
	float EndFadeClock = -1.0f;
	void StartCameraFade(float From, float To, float Seconds, bool bHold);
	TSharedPtr<SWidget> Letterbox;
	TSharedPtr<FPGTitleIntroSkipInput> SkipInput;

	// 시험용(-PGIntroShots, -PGIntroSkipAt=초)
	bool bTestShots = false;
	float TestSkipAt = -1.0f;
	int32 NextShot = 0;
	float AfterFinishClock = 0.0f;
	bool bFinalShotTaken = false;
};

// ---- 하늘: 전함과 드래곤 ----
UCLASS(NotBlueprintable, Transient)
class PROJECTPG_API APGTitleIntroShipDragon : public APGTitleIntro
{
	GENERATED_BODY()

public:
	APGTitleIntroShipDragon();

protected:
	virtual float GetDuration() const override;
	virtual const TCHAR* GetVariantName() const override { return TEXT("ShipDragon"); }
	virtual void BeginShots() override;
	virtual void TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation) override;
	virtual void CleanupShots(bool bSkipped) override;

private:
	void SpawnShip();
	void SpawnDragon();

	UPROPERTY(Transient)
	TObjectPtr<AActor> Ship;

	UPROPERTY(Transient)
	TObjectPtr<ASkeletalMeshActor> Dragon;

	// 하늘에서 내려다보면 무대 바닥(800m)의 끝이 지평선에 네모나게 보인다. 이 인트로 동안만 그 밑에 넓은 바닥을 한 장 더 깐다.
	UPROPERTY(Transient)
	TObjectPtr<AActor> WideGround;

	// 길(월드 좌표). BeginShots 에서 "하늘 카메라 기준 앞·오른쪽·위(m)" 로 적은 표를 월드로 바꿔 채운다.
	TArray<FPGIntroPathKey> ShipPath;
	TArray<FPGIntroPathKey> DragonPath;
	float LastDragonYaw = 0.0f;
	float DragonRoll = 0.0f;
	bool bDragonYawValid = false;

	// 드래곤이 선체를 뚫고 지나가지 않게(9/22 사용자). 선체 상자는 "배의 방향·자리 기준, cm" 로 한 번 잰다.
	void KeepDragonOffShip(float T);
	FBox ShipLocalBox = FBox(ForceInit);
	FBox DragonMeshBox = FBox(ForceInit); // 드래곤 메시 에셋의 상자(메시 좌표)
	float MinDragonGap = TNumericLimits<float>::Max(); // 밀어낸 뒤 실제로 그려진 가장 가까운 틈(cm)
	int32 DragonPushFrames = 0;
	float NextGapLogAt = 0.0f;
};

// ---- 땅: 주인공 구출 ----
UCLASS(NotBlueprintable, Transient)
class PROJECTPG_API APGTitleIntroRescue : public APGTitleIntro
{
	GENERATED_BODY()

public:
	APGTitleIntroRescue();

protected:
	virtual float GetDuration() const override;
	virtual const TCHAR* GetVariantName() const override { return TEXT("Rescue"); }
	// 둘이 나란히 선 그림 → 0.4초에 까맣게 → (여고생·소총 치움) → 0.6초에 걷히며 평소 타이틀(주인공 혼자).
	virtual float GetEndFadeOutSeconds() const override { return 0.4f; }
	virtual float GetEndFadeInSeconds() const override { return 0.6f; }
	virtual void BeginShots() override;
	virtual void TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation) override;
	virtual void CleanupShots(bool bSkipped) override;
	virtual void TickAfterFinish(float DeltaSeconds) override;

	// 아래는 습격 인트로(APGTitleIntroAmbush)가 물려받아 같이 쓴다 — 소총 든 주인공·몹 인형·뒷정리가 같다.
	void TickHero(float T);
	void TickMonster(FPGIntroMonster& Monster, float T);
	// 몹 프리셋(게임 A 세력) 하나를 인형으로 세운다. Plan 의 시간표·자리를 그대로 쓴다. 못 세우면 false.
	bool SpawnIntroMonster(const TCHAR* Preset, const FPGIntroMonster& Plan);
	void FindStageHero();
	void TickCar(float T, float DeltaSeconds);
	void SpawnCar();
	void SpawnGirl(bool bRevert);
	void PlayHero(UAnimSequence* Anim, bool bLoop);
	void PlayRifle(UAnimSequence* Anim, bool bLoop, float T);
	void SetHeroPose(const FVector& Location, float FacingYaw);
	void SetupRifleHero();
	void UpdateRifle();
	void FireRifle(float T, int32 TargetIndex = 0);
	void TickShieldFade(float DeltaSeconds);

	// 무대(PGFlowStage)가 세운 주인공. 빌려 쓰고 끝나면 제자리(HeroHome)로 돌려놓는다.
	TWeakObjectPtr<ASkeletalMeshActor> Hero;
	FTransform HeroHome;
	float HeroFacing = 0.0f;            // 지금 바라보는 방향(월드 요)
	UAnimSequence* HeroIdle = nullptr;  // 에셋은 KeepAlive 가 붙들고 있다
	UAnimSequence* HeroRun = nullptr;
	UAnimSequence* HeroPlaying = nullptr;

	// 소총 동작(UE5 마네킹 뼈대)을 주인공에게 입히는 보이지 않는 마네킹. 주인공 몸이 이 포즈를 뼈 이름으로 따라간다(LeaderPose).
	UPROPERTY(Transient)
	TObjectPtr<ASkeletalMeshActor> RifleDriver;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Rifle;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> Tracer;

	UAnimSequence* RifleJog = nullptr;
	UAnimSequence* RifleIdle = nullptr;
	UAnimSequence* RifleFire = nullptr;
	UAnimSequence* RifleReload = nullptr;
	UAnimSequence* RiflePlaying = nullptr;
	float RifleAnimEnd = 0.0f;          // 한 번짜리 동작(쏘기·장전)이 끝나는 인트로 시각
	int32 ShotsFired = 0;
	bool bReloadStarted = false;
	float TracerHideAt = -1.0f;
	FVector RifleMuzzleLocal = FVector::ZeroVector; // 총 메시 안에서 총구 자리

	TArray<FPGIntroMonster> Monsters;

	// 끝나면 치울 것(몹·날으는 차).
	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> SpawnedActors;

	UPROPERTY(Transient)
	TObjectPtr<ASkeletalMeshActor> Car;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> Booster;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Flames;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CarBeam;

	UAnimSequence* BoosterRetract = nullptr;
	bool bBoosterRetracted = false;
	float BeamHideAt = -1.0f;
	int32 BeamsFired = 0;
	float CarRoll = 0.0f;
	float LastCarYaw = 0.0f;
	bool bCarYawValid = false;

	// 차가 내려앉아 돌아온 여고생. 인트로에만 나오고, 끝의 까만 화면 동안 치운다(9/22 사용자).
	UPROPERTY(Transient)
	TObjectPtr<APGTransformNPCActor> Girl;

	// 여고생이 사람으로 돌아오면 방어막 반구가 생긴다(게임 안 규칙). 타이틀에서는 번쩍 한 번 보이고 사라지게 걷는다.
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ShieldMaterial;

	float ShieldFade = -1.0f;
	bool bShieldHandled = false;
	bool bShieldInstantRemove = false; // 건너뛰었을 때는 번쩍임 없이 바로 치운다

	TArray<FPGIntroPathKey> CarPath;
};
// ---- 땅: 습격(소총으로 싸우다 뒤에서 덮치는 순간 느려지고 뚝 끊겨 타이틀로) ----
// 구출 인트로의 주인공·몹 인형·뒷정리를 그대로 물려받고, 시간표와 카메라만 다르다.
UCLASS(NotBlueprintable, Transient)
class PROJECTPG_API APGTitleIntroAmbush : public APGTitleIntroRescue
{
	GENERATED_BODY()

public:
	APGTitleIntroAmbush();

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

protected:
	virtual float GetDuration() const override;
	virtual const TCHAR* GetVariantName() const override { return TEXT("Ambush"); }
	// 페이드 없이 뚝 끊고(0), 0.4초 까맣게 버틴 뒤, 0.8초에 걸쳐 평소 타이틀이 밝아진다.
	virtual float GetEndFadeOutSeconds() const override { return 0.0f; }
	virtual float GetEndHoldSeconds() const override { return 0.4f; }
	virtual float GetEndFadeInSeconds() const override { return 0.8f; }
	virtual bool EndsOnTitleShot() const override { return false; }
	virtual bool KeepLetterboxToEnd() const override { return true; }
	virtual void BeginShots() override;
	virtual void TickShots(float T, float DeltaSeconds, FVector& OutShakeLocation, FRotator& OutShakeRotation) override;
	virtual void CleanupShots(bool bSkipped) override;

private:
	void TickAmbushHero(float S);
	void TickLeaper(FPGIntroMonster& Monster, float S);
	void ApplySlowMoLook(float Amount, float T);
	// 세상 시간 느리게 한 것·화면 색 바꾼 것을 원래대로. 끝·건너뛰기·레벨 내려감 어디서든 불러도 된다(두 번 불러도 안전).
	void RestoreTimeAndLook();

	// 장면 시각(초). 느려지는 동안은 실제 시간보다 천천히 간다. 사람·몹의 자리는 이 시각으로, 카메라는 실제 시각으로 움직인다.
	float SceneClock = 0.0f;
	bool bTimeDilated = false;
	bool bLookChanged = false;
	int32 FirstLeaper = 0; // Monsters 에서 덮치는 두 마리가 시작하는 번호(앞은 총에 맞는 몹)

	// 카메라 화면 설정(색·비네트·초점) 원본. 느린 순간에만 바꾸고 되돌린다.
	UPROPERTY(Transient)
	FPostProcessSettings SavedPost;
};