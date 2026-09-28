// APGBattleshipActor 를 나눈 .cpp 들이 함께 쓰는 인클루드·상수·도우미(2026-09-26 책임별 나누기). 이 폴더 밖에서는 인클루드하지 않는다.
// 원래 한 파일 맨 위에 있던 것이라, 파일을 나눈 뒤에도 값이 두 벌이 되지 않게 한곳에 둔다.
#pragma once

#include "Finale/PGBattleshipActor.h"
#include "Common/PGVisualSettings.h"
#include "Finale/PGFinaleDirector.h"
#include "Finale/PGMissileSubsystem.h"
#include "Common/PGPhysicsUtil.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Perception/AISense_Hearing.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/StaticMesh.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Objects/PGObjectTypes.h"
#include "Flow/PGRunSubsystem.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#include "EngineUtils.h"
#include "Vehicle/PGVehiclePawn.h"
#include "Finale/PGDragonBoss.h"
#include "Vehicle/PGFlightKitComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/RectLightComponent.h"

namespace
{
	// Minerva 팩 화물선. 부품 31개가 자식 액터로 붙어 있는 부모 블루프린트.
	const TCHAR* const MinervaCargoShip =
		TEXT("/Game/kb3d_missiontominerva/Blueprints/Vehicles/BP_KB3D_MTM_VehicleCargoShip_A.BP_KB3D_MTM_VehicleCargoShip_A_C");

	const TCHAR* const MinervaMeshDir = TEXT("/Game/kb3d_missiontominerva/StaticMeshes/");

	// 추락 연출 에셋. 둘 다 이미 다른 코드가 쓰는 경로다 — 추측한 경로가 아니다.
	//   불: 드래곤 브레스가 쓰는 Weapon_Pack 횃불 불꽃(PGDragonBoss.cpp FireFxPath 와 같은 것).
	//   흙먼지: 주포 탄착·드래곤 추락이 쓰는 Paragon 바위 먼지. 코드에서 확인된 "연기" 전용 에셋이 없어 연기 자리도 이걸로 쓴다.
	// 팩이 없는 PC 에서는 이펙트 없이 떨어지기만 한다(BeginWreck 로그에 fire=no 로 남긴다).
	// (경로는 이제 액터 칸 WreckFireEffect / WreckDustEffect 의 기본값 — 생성자.)
	// 이 프로젝트의 땅 높이 기준(타일 윗면 z=20). 산과 둘레 바닥판은 충돌이 없어 선 검사가 뚫고 지나가므로(9/21 조사)
	//   땅을 못 찾은 자리는 이 높이로 친다 — 드래곤(PGDragonBoss.cpp GroundDatumZ)과 같은 규칙.
	// 이름을 ShipGroundDatumZ 로 둔다 — 유니티 빌드는 cpp 여럿을 한 덩어리로 묶어서, 드래곤 cpp 의 GroundDatumZ 와 이름이 겹치면 "재정의" 로 멈춘다.
	constexpr float ShipGroundDatumZ = 20.0f;

	// 팩 부품은 전부 Static 으로 들어와 있다. 그대로 두면 움직이는 배에 붙지도(AttachTo 거부) 따라오지도 못한다
	// (9/20 스모크 로그: "is not static, cannot attach ... which is static to it. Aborting.").
	inline void MakeHierarchyMovable(AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TArray<AActor*> Actors;
		Actor->GetAttachedActors(Actors, true, true);
		Actors.Add(Actor);
		for (AActor* Each : Actors)
			for (UActorComponent* Component : Each->GetComponents())
				if (USceneComponent* Scene = Cast<USceneComponent>(Component); Scene && Scene->Mobility != EComponentMobility::Movable)
					Scene->SetMobility(EComponentMobility::Movable);
	}

}

// ---- 배 안 조명 ----
//
// 왜 필요한가: 지붕(DeckRoof)은 "보이지 않지만 그림자는 만드는" 상자라 햇빛을 막고, 껍데기 31개는 보이는 메시라 Lumen 이
//   하늘빛을 막는다. 격납고 문까지 닫히면 안에 빛이 하나도 없다 — 배에 타면 화면이 새까맸다(9/20 PIE).
//   지붕을 없애면 드래곤 그림자가 다시 배 안으로 들어오니(9/20 사용자 지적) 지붕은 두고 등을 단다.
// 비용: 전부 Movable 이지만 그림자를 안 만들고 간접광(Lumen)에도 안 끼어들게 한다. 밝기는 물리 단위(칸델라)라 자동 노출이 그대로 받는다.
// 왜 면광원(RectLight)인가: 처음엔 점광원 24개(반경 50m, 40m 간격 두 줄)였다. 그러자 조명 격자 한 칸에 등이 16개를 넘게 겹쳐
//   "[VSM] One Pass Projection max lights overflow" 경고가 화면에 계속 떴다(9/20 PIE). 클러스터 셰이딩은 픽셀마다 "닿는 등 개수"
//   만큼 일하니, 겹침이 곧 비용이다. 80m 폭 면광원 하나가 갑판 폭 107m 를 고르게 덮으니 70m 마다 하나면 되고,
//   어느 자리든 닿는 등은 2~3개다. 개수가 5개쯤이라 오버플로도 없다. 전역 CVar 를 올리는 쪽은 배 하나 때문에 프로젝트 설정을 건드리는 것이라 안 한다.
static inline void ConfigureShipLight(ULocalLightComponent* Light, USceneComponent* Parent, const FVector& Local, float Intensity, float Radius)
{
	Light->SetupAttachment(Parent);
	Light->SetMobility(EComponentMobility::Movable); // 배가 움직인다. Stationary 는 움직이는 액터에서 깨진다
	Light->SetRelativeLocation(Local);
	Light->SetIntensityUnits(ELightUnits::Candelas);
	Light->SetIntensity(Intensity);
	Light->SetAttenuationRadius(Radius);
	Light->SetLightColor(FLinearColor(0.85f, 0.92f, 1.0f)); // 살짝 차가운 흰색 — 함선 실내
	Light->SetCastShadows(false);
	Light->SetAffectGlobalIllumination(false); // Lumen 간접광 계산에서 뺀다 — 직접광만으로 충분하다
	Light->SetCastVolumetricShadow(false);
}

