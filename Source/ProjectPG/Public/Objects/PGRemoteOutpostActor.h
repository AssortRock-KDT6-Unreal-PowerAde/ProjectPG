// 외진 보상 거점: 추락한 헬기 + 작은 통신 캠프 (2026-09-22).
//
// 왜 있나: 맵 생성기(팀원 코드)는 가장자리 방향 8개 중 3~5개를 매 판 버린다. 버려진 모서리에는 길도, 시작 지점도, 출구도 없어서
//   그냥 빈 땅이 된다(호수가 모서리 하나를 가져가고도 하나쯤 더 남는다). 사용자 9/22: "멀리까지 갈 이유가 있게 멋진 거 하나 + 좋은 상자 몇 개".
// 무엇을 세우나: 비스듬히 박힌 군용 헬기 잔해(연기·불), 떨어져 나간 주 회전날개·꼬리 날개·문짝, 흙더미,
//   그 옆에 통신 캠프(무전 안테나 차량·발전기·텐트·감시탑·험비·모래주머니·차단벽·탄약 상자 더미).
//   멀리서 보이는 미끼: 감시탑 꼭대기에서 깜빡이는 빛나는 공 + 붉은 등(헬기 옆 연기·불은 가까이서 보이는 분위기용).
// 상자: 이 액터는 "상자 놓을 자리" 만 준다(GetLootSlots). 실제 상자와 바닥 아이템은 월드 루팅(UPGWorldLootSpawner)이 기존 상자 카탈로그와
//   등급 표(PGItemValue)로 놓는다 — 상자·등급 시스템을 따로 만들지 않기 위해서다(사용자 요구: 평행 시스템 금지).
// 어디에 놓나: AWarZoneFootprintPreview::PlaceRemoteOutpost 가 빈 모서리를 고르고 SpawnOutpost 를 부른다. 이 액터는 모서리 판단을 하지 않는다.
// 성능(4060 8GB): 액터 하나에 스태틱 메시 컴포넌트 30개 안쪽, 그림자는 큰 것만, 나이아가라 4개(연기 2·불 2), 점광원 1개(그림자 없음), 빛나는 공 1개.
// 멀티 대비: 부품 목록(Pieces)을 서버가 채워 복제하고, 서버·클라 모두 같은 목록으로 컴포넌트를 만든다(탈출구 검문소와 같은 방식).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PGRemoteOutpostActor.generated.h"

class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UPointLightComponent;
class ACameraActor;

// 부품 하나. 서버가 채우고 복제한다.
USTRUCT()
struct FPGRemoteOutpostPiece
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> Mesh;

	// 액터 기준(원점 = 거점 가운데 바닥, +X = 맵 가운데 쪽).
	UPROPERTY()
	FTransform Relative;

	UPROPERTY()
	bool bCollide = false;

	UPROPERTY()
	bool bShadow = false;

	// 국방색 덧칠을 입힐 부품(험비·발전기). 예전에는 메시 이름으로 골랐는데, 블루프린트에서 모델을 바꾸면 이름이 달라져 덧칠이 빠진다.
	UPROPERTY()
	bool bTint = false;
};

// 상자 한 자리. 서버 전용(월드 루팅이 읽는다).
USTRUCT()
struct FPGRemoteOutpostLootSlot
{
	GENERATED_BODY()

	// 월드 좌표(바닥).
	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	// 이 자리에 어울리는 상자 카탈로그 ID(OBJ-004 무기 상자 등). 월드 루팅 설정의 목록이 우선이고, 이 값은 그 목록이 짧을 때 쓴다.
	UPROPERTY()
	FName PreferredObjectId;
};

UCLASS()
class PROJECTPG_API APGRemoteOutpostActor : public AActor
{
	GENERATED_BODY()

public:
	APGRemoteOutpostActor();

	// 서버 전용. Centre(바닥, 보통 Z=20)에 거점을 세운다. InwardYaw = 거점에서 맵 가운데를 보는 방향(도).
	// Seed 가 같으면 같은 모습. CornerLabel 은 로그용. 실패하면 nullptr.
	static APGRemoteOutpostActor* SpawnOutpost(UWorld* World, const FVector& Centre, float InwardYaw, int64 Seed, const FString& CornerLabel);

	// 거점이 차지하는 반경(cm). 프리뷰가 이 안의 나무·바위·담을 걷어 낸다. 2x2 칸(40m) 안에 들어가게 잡았다.
	static constexpr float LayoutRadiusCm = 1850.0f;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	const TArray<FPGRemoteOutpostLootSlot>& GetLootSlots() const { return LootSlots; }
	// 바닥 아이템을 흩뿌릴 네모(월드 가운데, 반폭, 회전). 헬기 잔해 둘레.
	FVector GetFloorLootCentre() const { return FloorLootCentre; }
	FVector2D GetFloorLootHalfExtent() const { return FloorLootHalfExtent; }
	float GetFloorLootYaw() const { return GetActorRotation().Yaw; }
	int32 GetPieceCount() const { return Pieces.Num(); }
	const FString& GetCornerLabel() const { return CornerLabel; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Pieces 목록대로 컴포넌트를 만든다. 서버는 BeginPlay, 클라는 OnRep. 두 번째 호출은 무시.
	void BuildFromPieces();
	// 연기·불(나이아가라)과 붉은 등. 순수 연출이라 서버·클라가 각자 만든다.
	void BuildLure();

	UFUNCTION()
	void OnRep_Pieces();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|RemoteOutpost")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(ReplicatedUsing = OnRep_Pieces)
	TArray<FPGRemoteOutpostPiece> Pieces;

	// 연기 자리 / 불 자리 / 붉은 등 자리(액터 기준). 복제해서 클라도 같은 자리에 만든다.
	UPROPERTY(Replicated)
	FVector SmokeRelative = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector FireRelative = FVector::ZeroVector;

	UPROPERTY(Replicated)
	FVector BeaconRelative = FVector::ZeroVector;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UPrimitiveComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UPointLightComponent> Beacon;

	// 등 자체가 멀리서 보이게 하는 빛나는 공(점광원은 광원 자체가 안 보인다).
	UPROPERTY(Transient)
	TObjectPtr<UStaticMeshComponent> BeaconGlow;

	// 서버 전용 데이터(복제 안 함).
	TArray<FPGRemoteOutpostLootSlot> LootSlots;
	FVector FloorLootCentre = FVector::ZeroVector;
	FVector2D FloorLootHalfExtent = FVector2D(600.0f, 600.0f);
	FString CornerLabel;

	bool bBuilt = false;
	bool bLureBuilt = false;
	float BeaconClock = 0.0f;

	// ---- 시험용 스크린샷(-PGOutpostShot): 화면 없는 PC 에서 거점 모습을 눈으로 확인하려고 둔다. 찍고 나면 게임을 끈다. ----
	void TickOutpostShots(float DeltaSeconds);
	bool bOutpostShots = false;
	float ShotClock = 0.0f;
	int32 ShotIndex = 0;
	bool bShotPosed = false;

	UPROPERTY(Transient)
	TObjectPtr<ACameraActor> ShotCamera;

public:
	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGRemoteOutpost 에서 바꾼다) ----
	// 기본값은 원래 코드에 적혀 있던 에셋 그대로(생성자). 칸을 비우면 그 부품만 빠진다(로그 missing= 에 이름이 찍힌다).
	// 배치(자리·기울기)는 코드에 남긴다 — 부품끼리 안 겹치게 손으로 맞춘 자리라서, 모델을 바꾸면 자리도 같이 봐야 한다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> HelicopterMesh;

	// 헬기 양 끝·옆의 파헤쳐진 흙더미.
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> CraterMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> RotorMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> TailRotorMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> DoorMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> LowWallMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Wreck")
	TSoftObjectPtr<UStaticMesh> TiresMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> TowerMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> RadioMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> GeneratorMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> TableMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> TentMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> HummerMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> SandbagMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> MachineGunMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> HedgehogMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> CrateMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> SmallBoxMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UStaticMesh> DrumMesh;

	// 험비·발전기에 입히는 "색 견본 x Tint" 재질과 색(Tools/make_military_tint.py).
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	TSoftObjectPtr<UMaterialInterface> TintMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Camp")
	FLinearColor OliveTint = FLinearColor(0.30f, 0.36f, 0.20f);

	// 연기·불(나이아가라 시스템). 나이아가라 모듈에 직접 묶이지 않으려고 UObject 로 받는다(PGFlowStage 와 같은 이유).
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	TSoftObjectPtr<UObject> SmokeFx;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure", meta = (AllowedClasses = "/Script/Niagara.NiagaraSystem"))
	TSoftObjectPtr<UObject> FireFx;

	// 연기 배율. 원래 굴뚝 연기라 가늘다(BuildLure 주석 참고).
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	float SmokeScale = 7.0f;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	float FireScale = 1.6f;

	// 감시탑 꼭대기 빛나는 공(점광원은 멀리서 광원 자체가 안 보여서).
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	TSoftObjectPtr<UStaticMesh> GlowMesh;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	TSoftObjectPtr<UMaterialInterface> GlowMaterial;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	float GlowDiameterCm = 100.0f;

	// 붉은 등. 9/22 첫 스크린샷: 1800cd·30m 는 그림자 진 땅을 빨갛게 물들여 핏자국처럼 보였다. 탑 꼭대기만 비추게 줄였다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	float BeaconCandelas = 900.0f;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	float BeaconRadiusCm = 1000.0f;

	UPROPERTY(EditDefaultsOnly, Category = "PG|RemoteOutpost|Lure")
	FLinearColor BeaconColor = FLinearColor(1.0f, 0.08f, 0.04f);
};
