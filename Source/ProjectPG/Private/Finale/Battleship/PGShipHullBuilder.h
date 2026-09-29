// 전함 선체 조립·측정 — 팩 부품으로 몸통·실내·조명을 짓고 크기를 잰다.
// 2026-09-26 SOLID(한 책임): 전함 액터(APGBattleshipActor)에서 이 책임의 "하는 일"과 "그 일에만 쓰는 상태"를 떼어 낸 협력 객체.
// 전함 액터는 이 객체를 들고 부르기만 한다. 레벨에 저장되는 설정 칸과 부품(HISM 등)은 전함 액터에 그대로 두고 Ship-> 으로 읽는다
// (옮기면 레벨에 저장된 값이 풀린다). 전함 액터의 비공개 멤버를 읽어야 해서 전함 액터가 이 클래스를 friend 로 둔다.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Finale/PGBattleshipActor.h"
#include "PGShipHullBuilder.generated.h"

UCLASS(Transient)
class UPGShipHullBuilder : public UObject
{
	GENERATED_BODY()

public:
	void Init(APGBattleshipActor* InShip) { Ship = InShip; }
	virtual UWorld* GetWorld() const override { return Ship ? Ship->GetWorld() : nullptr; }

	void BuildHull();
	void MeasureHull();      // 부품이 다 붙은 뒤(1초 뒤) 크기를 잰다
	void BuildInterior();    // 껍데기 크기를 안 뒤에 사람 크기 속을 깐다
	UStaticMeshComponent* AddPart(const TCHAR* MeshName, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator, float Scale = 1.0f);
	// 팩 폴더 밖(우리가 만든 함교 키트 등)의 메시를 전체 경로로 붙인다.
	UStaticMeshComponent* AddPartByPath(const TCHAR* AssetPath, const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator, float Scale = 1.0f);
	// 밟는 상자에 보이는 몸을 붙인다(.cpp 주석 참고).
	UStaticMeshComponent* AddSolidBox(const TCHAR* Name, USceneComponent* Parent, const FVector& Centre, const FVector& Extent, const FRotator& Rotation, const TCHAR* MaterialPath);
	UBoxComponent* AddWalkBox(const TCHAR* Name, const FVector& Center, const FVector& Extent, const FRotator& Rotation = FRotator::ZeroRotator);
	// 배 안 조명(.cpp 주석 참고). 갑판 두 줄 + 함교 + 승강기 출입구.
	void BuildInteriorLights(float RearX, float DeckFrontX, float HalfY, float HangarLandingX);
	UPointLightComponent* AddInteriorLight(const FVector& Local, float Radius);   // 작은 점광원(함교·출입구)
	URectLightComponent* AddDeckLight(const FVector& Local);                        // 갑판을 덮는 넓은 면광원
	// 껍데기 부품 하나를 이름으로 찾아 "배 기준" 상자를 돌려준다(뱃머리 유리창 자리를 알아내려고).
	bool FindHullPartLocal(const TCHAR* MeshNameContains, FBox& OutLocal) const;
	// 껍데기의 "어느 X 구간"만 재서 그 자리의 실제 폭·높이를 돌려준다(선미 입구 크기를 정하려고 — .cpp 주석 참고).
	bool MeasureHullSliceLocal(float SliceMinX, float SliceMaxX, FBox& OutLocal) const;
	// 껍데기 부품 전체를 이름·좌표·보임 여부로 찍는다(.cpp 주석 참고).
	void LogHullInventory() const;
	// 이륙해서 뒤쪽 선반이 다 들어간 뒤, 배 본체 밖으로 튀어나와 보이는 것이 남았는지 재서 찍는다(.cpp 주석 참고).
	void LogPartsOutsideHull() const;
	FVector RandomHullLocal() const;                  // 선체 상자 위쪽 절반의 무작위 한 점(배 기준)
	// 몸통 가운데 토막(_A_BodyMiddle)의 배 기준 상자. 그 앞끝이 갑판을 깔 수 있는 마지막 X 다 — 그 앞은 좁은 뱃머리뿐이라
	// 갑판판이 껍데기 밖으로 나온다(9/21 "검은 사각 판"). MeasureHull 이 재고 BuildInterior 와 after-sealing 검사가 쓴다.
	FBox BodyMiddleLocal = FBox(ForceInit);
	// 몸통(_A_Body*, 해치 제외) 전체의 배 기준 상자. 뒤끝(Min.X)이 "껍데기 몸통이 끝나는 X" 다 — 문 평면(RearX)은 그보다
	// 21m 뒤라, 거기 세운 판은 전부 허공에 뜬다(9/21 뒷문 제거의 근거, BuildInterior 로그).
	FBox BodyCoreLocal = FBox(ForceInit);

private:
	UPROPERTY()
	TObjectPtr<APGBattleshipActor> Ship;

	APGBattleshipActor::FPGShipSignature OnAssembled;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> InteriorParts;
	UPROPERTY(Transient)
	TArray<TObjectPtr<UBoxComponent>> WalkBoxes;
	// 배 안 조명. 지붕(DeckRoof)이 햇빛을 막고 껍데기가 하늘빛을 막아, 문이 닫히면 안이 칠흑이 된다
	// (9/20 PIE: "전함을 타면 검은색으로 변하네"). 그림자 없는 Movable 점광원이라 개수만큼의 비용이 거의 없다.
	// 갑판 등은 점광원 24개가 아니라 넓은 면광원(RectLight) 몇 개다. 점광원을 반경 50m·간격 40m 로 두 줄 깔았더니
	// 한 픽셀의 조명 격자 칸에 등이 16개 넘게 겹쳐 VSM 라이트 오버플로 경고가 화면에 계속 떴다(9/20 PIE). 클러스터 셰이딩은
	// "겹치는 등 개수"가 비용이라, 개수를 줄이는 게 답이다 — 전역 CVar 를 올리는 건 배 하나 때문에 프로젝트 설정을 건드리는 것.
	UPROPERTY(Transient)
	TArray<TObjectPtr<ULocalLightComponent>> InteriorLights;
	FTimerHandle MeasureTimer;
};
