// 탈출구 검문소 꾸미기 (2026-09-22).
//
// 왜 있나: 맵 가장자리 탈출구에는 빨간 EXIT 표시와 펜스 한 칸이 달랑 서 있었고, 그마저 차에 치이면 쓰러져 나뒹굴었다
//   (사용자 9/22 스크린샷: "탈출구 시설 좀 만들까?"). 새 모델 대신 이미 있는 팩 부품(공장 팩 차단벽·철조망·컨테이너,
//   군용 팩 감시탑·텐트)으로 "검문소" 처럼 꾸며 그림체를 맞춘다.
// 무엇을 세우나(탈출구마다 하나): 입구 양옆 콘크리트 차단벽, 열려 있는 차단봉(붐 게이트), 3m 기둥 둘 + 가로대 + 빨간 "EXIT" 글자의
//   문틀, 탈출 자리 앞을 가로지르는 철조망 펜스(가운데는 길), 한쪽에 감시탑·텐트, 반대쪽에 컨테이너, 투광등, 드럼통 같은 소품.
//   그리고 연료통 하나(차·헬기·배 탈출구가 연료통을 요구하는데 맵에 시작 지점 것 하나뿐이었다 — 사용자 9/22).
// 성능(4060 8GB): 탈출구당 액터 하나에 스태틱 메시 컴포넌트 20개 안쪽. 그림자는 큰 것(컨테이너·탑·텐트·투광등)만.
//   충돌은 차단벽·펜스·기둥·컨테이너만 막고(차가 뚫고 들어가지 않게), 소품은 없음. 탈출 자리와 길은 비워 둔다(차로 들어갈 수 있게).
// 부서지지 않게: 액터에 PGPhysicsUtil::ProtectedTag 를 붙인다 — 차·로봇·드래곤이 쳐도 TryKnockProp 이 거부한다.
// 배치는 서버가 시드로 정해 Pieces 에 담고, 서버·클라 모두 BeginPlay/OnRep 에서 같은 목록으로 부품을 만든다(멀티 대비, 같은 시드 = 같은 모습).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PGExitDressingActor.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

// 탈출구 종류. 길을 얼마나 비워 둘지, 펜스 줄을 어디에 세울지가 다르다.
UENUM()
enum class EPGExitDressingKind : uint8
{
	// 세워 둔 차·헬기(연료통 + F). 탈것 둘레(KeepClear)를 통째로 비우고 펜스 줄은 그 안쪽에.
	Vehicle,
	// 잠긴 펜스 게이트 + 바깥 탈출 영역. 펜스 줄이 게이트 양옆으로 이어져 "문이 있는 담" 이 된다.
	FenceGate,
};

// 부품 하나. 서버가 채우고 복제한다.
USTRUCT()
struct FPGExitDressingPiece
{
	GENERATED_BODY()

	UPROPERTY()
	TSoftObjectPtr<UStaticMesh> Mesh;

	// 액터(탈출 지점, +X = 맵 바깥쪽) 기준.
	UPROPERTY()
	FTransform Relative;

	UPROPERTY()
	bool bCollide = false;

	UPROPERTY()
	bool bShadow = false;
};

UCLASS()
class PROJECTPG_API APGExitDressingActor : public AActor
{
	GENERATED_BODY()

public:
	APGExitDressingActor();

	// 서버 전용. 탈출구 하나 둘레에 검문소를 세우고 연료통을 놓는다. 실패하면 nullptr.
	//  Ground: 탈출 지점(바닥 위). Outward: 맵 바깥쪽(수평). KeepClear: 탈출 액터(차·헬기·게이트·영역)의 월드 바운드 — 여기는 비운다.
	//  Stream: 시드 스트림(같은 시드면 같은 모습). ExitIndex: 로그용 번호.
	static APGExitDressingActor* SpawnCheckpoint(UWorld* World, const FVector& Ground, const FVector& Outward, EPGExitDressingKind Kind,
		const FBox& KeepClear, FRandomStream& Stream, int32 ExitIndex);

	// 탈출 타일의 길 축(격자 축 하나, 수평 단위 벡터, 맵 바깥쪽). 맵 가운데에서 본 방향(Outward)은 모서리 근처에서 비스듬해서,
	// 스포너가 차·게이트를 놓기 전에 이걸로 바로잡아야 검문소와 탈출 액터가 같은 방향을 본다. CellCentre = 탈출 타일 가운데(월드).
	static FVector ResolveLaneAxis(const FVector& Ground, const FVector& CellCentre, const FVector& Outward);

	virtual void BeginPlay() override;

	int32 GetPieceCount() const { return Pieces.Num(); }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// Pieces 목록대로 컴포넌트를 만든다. 서버는 BeginPlay 에서, 클라는 목록이 도착했을 때(OnRep). 두 번 부르면 두 번째는 무시.
	void BuildFromPieces();

	UFUNCTION()
	void OnRep_Pieces();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PG|ExitDressing")
	TObjectPtr<USceneComponent> RootScene;

	UPROPERTY(ReplicatedUsing = OnRep_Pieces)
	TArray<FPGExitDressingPiece> Pieces;

	// "EXIT" 글자 자리(액터 기준). bHasSign 이 false 면 글자 없음(기둥·가로대 부품이 없어도 글자는 띄운다).
	UPROPERTY(Replicated)
	FTransform SignRelative;

	UPROPERTY(Replicated)
	bool bHasSign = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> Sign;

	bool bBuilt = false;

public:
	// ---- 겉모습 칸 (9/23 블루프린트 분리: BP_PGExitDressing 에서 바꾼다) ----
	// 기본값은 원래 코드에 적혀 있던 에셋 그대로(생성자). 칸을 비우면 그 부품만 빠진다(로그 missing= 에 이름이 찍힌다).
	// 자리 잡기(길·타일·다른 부품과 안 겹치게)는 코드에 남긴다 — 부품 크기(바운드)를 보고 자리를 잡으므로 모델을 바꿔도 알아서 비켜 선다.
	// 철조망 펜스 판(2m). 줄지어 이어 붙인다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> FencePanelMesh;

	// 펜스 판이 없을 때 대신 쓰는 판.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> FencePanelFallbackMesh;

	// 차단봉 팔(5.8m). 킷 문틀이 없을 때는 가로대로도 쓴다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> BoomArmMesh;

	// 검문소 킷 콘크리트 문틀(기둥 둘 + 보 + 빈 간판). 치수는 GatePillarY 등 코드 상수와 맞춰져 있다.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> GateFrameMesh;

	// 킷 문틀이 없을 때 세우는 3m 널판 기둥.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> GatePostMesh;

	// 차단봉 받침(52cm 블록).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> BoomPedestalMesh;

	// 차단봉 제어함(킷).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> ControlCabinetMesh;

	// 초소(킷 경비 부스).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> GuardBoothMesh;

	// 입구 양옆 콘크리트 차단벽(2m).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> ConcreteBarrierMesh;

	// 대전차 장애물(고슴도치).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> HedgehogMesh;

	// 컨테이너(12m).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> ContainerMesh;

	// 감시탑.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> WatchTowerMesh;

	// 텐트(시드로 A/B 중 하나).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> TentMeshA;

	// 텐트(시드로 A/B 중 하나).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> TentMeshB;

	// 투광등(6.4m).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> LanternMesh;

	// 굽은 모래주머니 벽(킷).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> SandbagCurveMesh;

	// 곧은 모래주머니 벽(킷).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> SandbagStraightMesh;

	// 드럼통.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> DrumMesh;

	// 가스통.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> GasBottleMesh;

	// 팔레트.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> PalletMesh;

	// 장작 더미.
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Meshes")
	TSoftObjectPtr<UStaticMesh> WoodPileMesh;

	// 문틀 간판의 글자·색·크기(글자는 게임이 그린다 — 간판에 굽지 않아 바꾸기 쉽다).
	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Sign")
	FText SignText;

	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Sign")
	FColor SignColor = FColor(255, 40, 40);

	UPROPERTY(EditDefaultsOnly, Category = "PG|ExitDressing|Sign")
	float SignWorldSize = 100.0f;
};
