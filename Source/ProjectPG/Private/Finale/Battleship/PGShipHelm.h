// 전함 조종 — 조종석 앉기/일어나기, 조종(이동·상승), 함교 시점 카메라, 조준선.
// 2026-09-26 SOLID(한 책임): 전함 액터(APGBattleshipActor)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 전함 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 전함 액터에 그대로 두고 Ship-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 전함 액터의 비공개 멤버를 읽어야 해서 전함 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGBattleshipActor.h"
#include "Finale/PGHelmControlComponent.h"
#include "PGShipHelm.generated.h"

UCLASS(Transient)
class UPGShipHelm : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGBattleshipActor* InShip) { Ship = InShip; }
	virtual UWorld* GetWorld() const override { return Ship ? Ship->GetWorld() : nullptr; }

	FVector GetBridgeWorld() const;
	void DrawCrosshair(UCanvas* Canvas, APlayerController* PC);
	// 함교 자리를 갑판보다 먼저 정한다(.cpp 주석 참고).
	void ComputeBridgeLocal(float FrontX);
	// ---- 조종석(멀티 9/27: 서버가 정하는 일 / 앉은 사람 컴퓨터가 하는 일로 나눔) ----
	// 서버: 앉기·일어서기 요청 판단, 조종 입력 받기, 앉은 사람을 의자에 붙여 두기.
	void ServerSeatRequest(APlayerController* PC, bool bSit);
	void ServerHelmInput(APlayerController* PC, const FPGHelmInput& InInput);
	void TickSeatServer(float DeltaSeconds);
	// 모든 컴퓨터: 이 컴퓨터 사람의 F·W/S/A/D·마우스를 읽어 서버로 보낸다(TickLocalPilot), 앉은 상태가 바뀌면
	//   그 사람 화면에서 카메라·걷기 막기를 켜고 끈다(OnSeatChangedLocal — 전함의 OnRep_Seated 가 부른다).
	void TickLocalPilot(float DeltaSeconds);
	void OnSeatChangedLocal();
	// 주포 빛줄기를 가늘게 줄이다 끈다(모든 컴퓨터).
	void TickBeamFade(float DeltaSeconds);
	FVector GetSeatWorld() const;
	void BuildBridge(float FrontX, float HalfY);   // 조종석·계기 글자·옆 화면
	void TickBridge(float DeltaSeconds);           // 계기 글자 갱신 + 옆 화면 촬영 조절
	void ApplyBridgeViewCamera();                  // 위 View* 값을 카메라에 먹인다(만들 때 + 앉을 때마다)
	void TickBridgeView(float DeltaSeconds);       // 마우스로 배 주위를 둘러본다(.cpp 주석 참고)
	void PlaceOrbitCamera(const FRotator& Look);   // 바라보는 방향 반대쪽으로 물러나 배를 담는다
	// 카메라가 도는 중심(월드)·반지름·처음 각도를 ViewBackRatio/ViewUpRatio 에서 뽑는다.
	void GetOrbitPivotAndDistance(FVector& OutPivotWorld, float& OutDistance, float& OutStartPitchDeg) const;
	void TickHelm(float DeltaSeconds);             // 서버: 받은 입력으로 주포를 쏜다
	// 앉은 사람을 일으킨다. F 로 일어서기와 추락 때의 강제 기립이 같은 길을 쓴다(.cpp 주석 참고).
	void StandUpFromSeat(APawn* Pawn, const TCHAR* Why);
	void TickHelmClimb(float DeltaSeconds);        // 조종석 오른쪽 마우스 — 배가 위로 오른다(.cpp 주석 참고)
	void TickHelmDrive(float DeltaSeconds);        // 조종석에 앉은 사람의 W/S/A/D — 배를 몬다(.cpp 주석 참고)

private:
	UPROPERTY()
	TObjectPtr<APGBattleshipActor> Ship;

	float HelmClimbFloorZ = 0.0f; // 처음 앉았을 때의 흔들림 기준 높이 — 오르기 한도의 바닥
	// 함교(뱃머리 유리창 안쪽)
	UPROPERTY(Transient)
	TArray<TObjectPtr<UTextRenderComponent>> BridgeReadouts;
	UPROPERTY(Transient)
	TObjectPtr<USceneCaptureComponent2D> BridgeCamera;
	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> BridgeScreenTarget;
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BridgeScreenMaterial;
	float BridgeUpdateTimer = 0.0f;
	float ScreenCaptureTimer = 0.0f;
	bool bSeatKeyDown = false;  // F 가 눌린 "순간" 만 잡기 위한 직전 상태
	double LastSeatToggle = 0.0;
	// 앉았을 때 쓰는 카메라. 배 뒤 위쪽에서 뱃머리를 본다(전함 3인칭).
	UPROPERTY()
	TObjectPtr<UCameraComponent> BridgeViewCamera;
	float HelmViewYaw = 0.0f;   // 앉은 동안 쌓아 가는 시야 각도
	float HelmViewPitch = 0.0f;
	bool bAimHasTarget = false; // 조준선에 미사일이 쫓아갈 것이 걸려 있나(십자 색이 바뀐다) — 이 컴퓨터 화면용
	float AimScanTime = 0.0f;
	FVector LocalAim = FVector::ZeroVector; // 이 컴퓨터 사람이 겨눈 곳(마지막 조준 검사 결과)

	// 멀티(9/27)
	FString LastLoggedReadout; // 클라 계기판 글자 로그(바뀔 때만)
	bool bLocalSeated = false;          // 이 컴퓨터 사람이 지금 앉아 있다고 화면 쪽을 켜 두었나
	FPGHelmInput Input;                 // 서버: 마지막으로 받은 조종 입력
	TWeakObjectPtr<APlayerController> InputFrom; // 서버: 그 입력을 보낸 사람
	double InputAt = -100.0;            // 서버: 받은 시각(오래되면 버린다)
	FPGHelmInput LastSent;              // 이 컴퓨터: 마지막으로 보낸 입력(바뀔 때만 보낸다)
	double LastSentAt = -100.0;
	// 서버: 이 사람의 폰이 지금 함교에서 쏠 수 있는 자리인가(앉았거나 함교 둘레).
	bool IsAtHelm(const APawn* Pawn) const;
	APlayerController* GetLocalPC() const;
};
