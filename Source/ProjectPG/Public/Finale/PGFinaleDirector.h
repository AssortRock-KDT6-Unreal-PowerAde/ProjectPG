// 피날레 진행 담당. 보스가 죽으면 경고 → 전함 등장 → 탑승 → 이륙 순서를 한 군데서 굴린다. (2026-09-20)
//
// 왜 액터 하나인가: 상태가 하나뿐이고 서버가 정한다. 월드 서브시스템으로 해도 되지만, 진행 상황을 클라이언트에 복제해야 해서
//   복제 붙이기 쉬운 액터로 뒀다(서브시스템은 복제가 안 된다).
//
// 기존 코드와의 접점은 딱 한 줄이다 — APGRobotCharacter::Die 안의 NotifyBossDefeated(this).
//   기획서 §5 "기존 코드에서 피날레 코드로 들어가는 호출은 정확히 하나". 나머지는 이 파일 안에서 끝난다.
//   그래서 피날레를 통째로 꺼도(UPGObjectSettings::bEnableFinale = false) 다른 시스템이 아무것도 모른다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PGFinaleDirector.generated.h"

class APGBattleshipActor;
class APGDragonBoss;

UENUM(BlueprintType)
enum class EPGFinaleState : uint8
{
	Idle     UMETA(DisplayName = "대기"),
	Warning  UMETA(DisplayName = "경고"),   // 보스가 죽고, 하늘이 울리는 시간
	Arrival  UMETA(DisplayName = "등장"),   // 산 너머에서 넘어온다
	Hover    UMETA(DisplayName = "정박"),   // 맵 위에 낮게 떠서 기다린다
	Launch   UMETA(DisplayName = "이륙"),   // 사람이 타면 외곽 타일을 부수며 날아오른다
	Dragon   UMETA(DisplayName = "공중전"), // 산에서 드래곤이 솟아오른다
	Victory  UMETA(DisplayName = "격추"),
};

UCLASS()
class PROJECTPG_API APGFinaleDirector : public AActor
{
	GENERATED_BODY()

public:
	APGFinaleDirector();

	// 기존 코드에서 들어오는 유일한 문. 서버에서만 뜻이 있고, 이미 시작했으면 아무것도 안 한다.
	// 디렉터가 월드에 없으면 여기서 만든다(레벨에 미리 놓을 필요 없음).
	static void NotifyBossDefeated(AActor* Boss);

	// 월드에 있는 디렉터를 찾는다(없으면 nullptr). bCreate 면 없을 때 만든다.
	static APGFinaleDirector* Get(const UWorld* World, bool bCreate = false);

	// 판이 만들어질 때(보스를 놓을 때) 한 번 부른다. 전함을 미리 만들어 산 너머에 숨겨 둔다.
	// 왜: 보스가 죽은 뒤에 만들면 부품 31개 + 재질·텍스처 읽기가 그 순간에 몰려 화면이 한 번 끊긴다(9/20 PIE).
	//     검은 로딩 화면 동안 미리 치러 두면 등장 순간은 "이미 있는 것을 움직이기"뿐이다.
	static void Prewarm(const UWorld* World, const FVector& BossLocation);

	// 콘솔·테스트에서 보스 없이 바로 시작. Focus 는 전함이 올 자리를 정할 기준점(보통 플레이어).
	void StartFinale(const FVector& Focus);

	EPGFinaleState GetState() const { return State; }
	APGBattleshipActor* GetShip() const { return Ship; }
	APGDragonBoss* GetDragon() const { return Dragon; }
	// 콘솔·테스트에서 바로 이륙시킨다(갑판에 사람이 없어도).
	void ForceLaunch();

	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;

	// 보스가 죽고 전함이 나타나기까지. 기획서는 30초지만 확인하기 답답해서 15초로 뒀다(설정에서 바꾼다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale", meta = (ClampMin = "0.0"))
	float WarningSeconds = 15.0f;

	// 전함 밑바닥이 떠 있을 높이(땅에서 cm). 날으는 차의 상승 한계가 200m 라서 그보다 낮아야 타러 올라갈 수 있다
	// (PGFlightKitComponent::MaxAltitude). 승강 발판도 이 높이만큼 오르내린다 — 높이면 타는 데 오래 걸린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float HoverAltitude = 8000.0f; // 80m. 더 높으면 승강 발판이 "한참 기다리는 것"이 된다(9/20 사용자)

	// 산줄기 바깥 이만큼 더 먼 곳에서 출발한다(cm). 전함 길이(436m)보다 넉넉히 멀어야 "산 너머에서 넘어온다"로 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float ApproachMargin = 50000.0f;

	// 출발 높이(땅에서 cm). 산마루보다 높은 데서 내려온다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float ApproachAltitude = 40000.0f;

protected:
	void EnterState(EPGFinaleState NewState);
	// ---- 상태 표 (.cpp FindStateHandlers). 상태마다 들어갈 때 / 매 틱 처리 한 쌍 ----
	struct FStateHandlers
	{
		void (APGFinaleDirector::*Enter)() = nullptr;
		void (APGFinaleDirector::*Tick)(float) = nullptr;
	};
	static const FStateHandlers* FindStateHandlers(EPGFinaleState InState);
	void EnterWarningState();
	void TickWarningState(float DeltaSeconds);
	void EnterArrivalState();
	void TickArrivalState(float DeltaSeconds);
	void EnterHoverState();
	void TickHoverState(float DeltaSeconds);
	void EnterLaunchState();
	void TickLaunchState(float DeltaSeconds);
	void TickDragonState(float DeltaSeconds);
	void EnterVictoryState();
	void SpawnShip();
	// 전함을 미리 만들어 접근 지점에 숨겨 둔다(이미 있으면 아무것도 안 한다).
	void PrepareShip(const FVector& Focus);
	// 접근 지점(산 너머)과 정박 지점(밑바닥 높이 기준)을 구한다.
	void ComputeApproach(const FVector& Focus, FVector& OutStart, FVector& OutHover);
	// 맵 한가운데와 반지름, 산줄기 바깥 반지름을 잰다. 맵 생성기를 못 찾으면 Focus 기준으로 대충 잡는다.
	void ResolveMapExtent(const FVector& Focus, FVector& OutCentre, float& OutMountainRadius) const;
	// 공중전의 축(단위 벡터, 맵 중심 기준). 배는 이 방향 끝에, 드래곤은 반대쪽 끝에 선다.
	// 둘이 같은 축을 써야 간격이 벌어지므로 한 곳에서만 만든다.
	FVector2D ResolveFinaleAxis(FVector& OutCentre, float& OutMapRadiusCm) const;
	void HandleShipArrived(APGBattleshipActor* InShip);
	// 갑판에 사람이 서 있나(이륙 조건).
	bool IsAnyoneAboard() const;
	// 이륙하며 배 밑 외곽 타일·소품을 날린다. 한 프레임에 몇 개씩만(프레임 방어).
	void KnockGroundUnderShip();
	void SpawnDragon();

	UPROPERTY(ReplicatedUsing = OnRep_State)
	EPGFinaleState State = EPGFinaleState::Idle;

	UFUNCTION()
	void OnRep_State();

	UPROPERTY(Replicated)
	TObjectPtr<APGBattleshipActor> Ship;

	// 보스가 죽은 자리. 전함이 올 곳을 여기 기준으로 정한다(플레이어가 거기 있으니까).
	UPROPERTY(Replicated)
	FVector FocusLocation = FVector::ZeroVector;

	// 미리 만들어 둔 전함의 출발·정박 자리.
	FVector PreparedStart = FVector::ZeroVector;
	FVector PreparedHover = FVector::ZeroVector;
	bool bShipRevealed = false;

	float StateTimer = 0.0f;
	// 보스가 죽은 자리의 땅 높이. 정박 높이와 승강 발판이 닿을 곳을 여기서 잰다.
	float GroundZ = 20.0f;

	UPROPERTY(Replicated)
	TObjectPtr<APGDragonBoss> Dragon;

	// 이륙: 갑판에 사람이 이만큼 서 있으면 출발한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float BoardedSeconds = 3.0f;
	// 이륙해서 올라갈 높이(정박 높이 위로, cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float LaunchClimb = 0.0f; // 9/21 사용자: "굳이 떠오르게 하지 마" — 0 이면 자동 상승 없음, 조종석으로만 움직인다
	// 조종석에 앉은 뒤 드래곤이 솟기까지의 텀.
	//
	// 9/21 에 기준을 바꿨다. 사용자 요구: "전함 조종석에 앉으면, 시야가 전함 밖으로 보이면
	// 몇 초 뒤에 드래곤 등장." 즉 세는 시작점은 이륙이 아니라 **앉은 순간**이다.
	//
	// 왜 전에는 안 맞았나(같은 증상을 세 번 고쳤는데 세 번 다 빗나갔다):
	//   전에는 이륙 시작부터 셌다. 그런데 이륙은 "갑판에 3초 서 있으면" 시작되고, 격납고 입구에서
	//   의자까지가 409m 다. 3 + 4 = 7초 안에 그 거리를 걸어가 앉는 것은 불가능하다. 그래서 사용자
	//   눈에는 언제나 "그냥 바로 나온다" 였다. 값을 키우는 문제가 아니라 세는 대상이 틀렸다.
	//
	// 배가 뒤로 물러나며 오르는 동안 드래곤이 뱃머리 정면 끝에서 솟으므로, 앉아서 앞을 보면 그게 보인다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	float DragonDelaySeconds = 4.0f;
	// 끝내 아무도 안 앉을 때를 위한 보험 — 이륙하고 이만큼 지나면 앉든 말든 드래곤을 낸다.
	//
	// 없으면 피날레가 통째로 멈춘다. 사용자가 조종석을 못 찾거나, 앉기 전에 죽거나, 차를 타고 있으면
	// (차에 탄 채로는 앉는 것이 막혀 있다) 영영 다음으로 안 넘어간다.
	//
	// 처음에 16초로 잡았다가 실제로는 이 보험이 늘 먼저 터졌다(9/21 PIE: "nobody sat down, falling
	// back (waited 16.0s)" — 사용자 "조종석에 앉기도 전에 왜 용이 나와?"). 문에서 의자까지 409m 를
	// 차로 가서 내려 걸어가 앉는 데 16초로는 모자란다. 보험은 "정말 안 앉는 경우" 만 잡아야 하므로
	// 넉넉히 둔다. 세는 시작점은 이륙 시점(Launch 진입)이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "PG|Finale")
	// 9/21: 0(끔). 사용자: "용은 함선 조종석 앉지도 않았는데 왜 튀어나와" — 90초 보험이 또 먼저 터졌다(배에서 떨어져 407m 아래에 있을 때).
	// 이제 배는 앉아야 출발하므로 드래곤도 앉아야만 나온다. 0 보다 크게 주면 보험이 다시 켜진다.
	float DragonFallbackSeconds = 0.0f;
	float AboardTimer = 0.0f;
	// 조종석에 앉아 있은 시간. 일어나면 0 으로 돌아간다 — 잠깐 앉았다 일어난 것으로 세면 안 된다.
	float SeatedTimer = 0.0f;
};
