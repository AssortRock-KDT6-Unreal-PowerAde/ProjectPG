// 거래소 부스(상점·택배·제조·교환소) 건물. 부품(바닥·벽·지붕·간판·유리·창구 서랍…)을 조립하고, 창구 앞에 서비스 상호작용을 붙인다. (2026-09-20)
//
// 왜 건물과 상호작용을 나눴나:
//  - 부서지기: PGPhysicsUtil::TryKnockProp 은 "상호작용 액터(IInteractable)의 부품"은 루팅·퀘스트 보호 때문에 떼지 않는다.
//    건물은 평범한 액터로 두어야 드래곤·전함·로봇이 부딪힌 부품만 하나씩 날아간다(사용자 결정 9/20: 방어막 없이 같이 부서진다).
//  - 거래: 창구 앞에 APGServiceInteractionActor(상점/택배/제조/교환소)를 따로 세워 F 를 받는다. 창구 부품(카운터)이 부서지면 거래도 닫는다.
// 부품 경로·소켓은 모델링 세션의 키트 v2(Tools/booth_kit_parts.json, import_shop_booth.py). 모든 부품의 피벗 = 부스 원점(바닥 가운데),
// 정면 +X. 그래서 부품은 전부 (0,0,0) 에 붙이면 조립된다. 창구 모듈(창틀·유리·서랍)은 바닥 부품의 Window 소켓에 붙는다.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Objects/PGObjectTypes.h"
#include "PGBoothActor.generated.h"

class APGServiceInteractionActor;
class UBoxComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EPGBoothKind : uint8
{
	Shop     UMETA(DisplayName = "상점"),
	Parcel   UMETA(DisplayName = "택배"),
	Craft    UMETA(DisplayName = "제조"),
	Exchange UMETA(DisplayName = "교환소"),
};

UCLASS()
class PROJECTPG_API APGBoothActor : public AActor
{
	GENERATED_BODY()

public:
	APGBoothActor();

	// 스폰 직후(BeginPlay 전)에 정한다. 스포너가 SpawnActorDeferred 로 만들고 이걸 부른 뒤 FinishSpawning 한다.
	void SetBoothKind(EPGBoothKind InKind) { Kind = InKind; }
	EPGBoothKind GetBoothKind() const { return Kind; }

	// 이 종류 부스의 바닥 부품 반폭(cm, 로컬 Y). 스포너가 부스를 나란히 세울 간격을 잴 때 쓴다(메시를 안 불러왔으면 기본값).
	static float GetHalfWidth(EPGBoothKind InKind);
	static EPGServiceKind ToServiceKind(EPGBoothKind InKind);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

protected:
	void BuildParts();
	void SpawnService();
	void CheckStillStanding();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|Booth")
	TObjectPtr<USceneComponent> RootScene;

	// 멀티(9/27): 복제 — 스포너가 세우기 전에 정하므로 클라이언트 BeginPlay(부품 짓기) 때 이미 와 있다.
	//   전에는 클라에서 네 부스가 모두 "상점" 모양으로 지어져 벽 위치가 서버와 달랐다(되돌림).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "PG|Booth")
	EPGBoothKind Kind = EPGBoothKind::Shop;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	// 창구 부품(Counter). 이게 부서지면(숨겨지면) 거래를 닫는다.
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> CounterPart;

	UPROPERTY(Transient)
	TObjectPtr<APGServiceInteractionActor> Service;

	FTimerHandle StandingTimer;

public:
	// 부스 부품 킷 폴더(부품 이름은 코드가 종류별로 고른다). 9/23 블루프린트 분리 — BP_PGBooth 에서 바꾼다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Visual", meta = (ContentDir, LongPackageName))
	FDirectoryPath BoothKitFolder;

	// 세울 클래스: 설정(ProjectPG Visuals > Booth Class)에 블루프린트가 있으면 그것, 없으면 C++.
	static UClass* GetSpawnClass();

	// ---- 보호막(9/23 사용자: "배리어 투명하게 둘러서 플레이어만 통하게, 파괴·몬스터로부터 보호") ----
	// 9/20 에는 "방어막 없이 같이 부서진다" 로 정했었다. 부스가 부서지면 거래가 끊기므로 이제는 지킨다:
	//   부스 액터에 보호 표(ProtectedTag)를 붙여 차·로봇·폭발에 안 부서지고, 둘레에 보이는 막을 쳐 몬스터·탈것을 막는다.
	//   걸어 다니는 플레이어만 막을 지나간다. 드래곤 등장 붕괴에서는 예외로 같이 사라진다(WarZoneFootprintPreview::CollapseRegion).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Barrier")
	bool bBarrier = true;

	// 막 모양(기본 엔진 원통 — 부스 크기에 맞춰 타원으로 늘인다)과 반투명 재질(Tools/make_booth_barrier.py 가 만든다).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Barrier")
	TSoftObjectPtr<UStaticMesh> BarrierMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Barrier")
	TSoftObjectPtr<UMaterialInterface> BarrierMaterial;

	// 부스 부품 둘레에서 막까지 여유(cm). 창구 앞 손님 자리가 막 안에 들어와야 한다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Barrier", meta = (ClampMin = "0.0"))
	float BarrierMarginCm = 300.0f;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Barrier", meta = (ClampMin = "100.0"))
	float BarrierHeightCm = 600.0f;

	// ---- 들어오면 띄우는 안내(9/23 사용자: "뭐가 교환소인지 모르겠다") ----
	// 부스 막 안에 들어온 플레이어 화면에 안내 줄(WBP_PGAnnounceLine)로 두 줄: 이름, 하는 일. 교환소는 무엇을 무엇으로 바꾸는지 자동으로 붙는다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Sign")
	TMap<EPGBoothKind, FText> BoothNames;

	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Sign")
	TMap<EPGBoothKind, FText> BoothDescriptions;

	// 같은 사람이 막 경계를 들락날락할 때 안내가 계속 쌓이지 않게(초).
	UPROPERTY(EditDefaultsOnly, Category = "PG|Booth|Sign", meta = (ClampMin = "0.0"))
	float EnterMessageCooldownSeconds = 8.0f;

protected:
	void BuildEnterZone(const FBox& LocalParts);

	UFUNCTION()
	void OnEnterZoneBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> EnterZone;

	double LastEnterMessageTime = -1000.0;

	void BuildBarrier();
	// 걸어 다니는 플레이어(캐릭터, 로봇 제외)는 막을 무시하고 지나가게 한다. 플레이어가 새로 생기거나 차에서 내릴 수 있어 1초마다 다시 건다.
	void LetPlayersThroughBarrier();

	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BarrierPart;

	FTimerHandle BarrierTimer;
};
