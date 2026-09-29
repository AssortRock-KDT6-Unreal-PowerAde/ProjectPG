#include "Objects/PGRemoteOutpostActor.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Common/PGPhysicsUtil.h"
#include "Common/PGVisualSettings.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGObjectTypes.h"
#include "PhysicsEngine/BodySetup.h"
#include "UnrealClient.h"

namespace
{
	// 익명 namespace 상수·함수는 파일 고유 접두어(RemoteOutpost_)를 붙인다 — 유니티 빌드에서 다른 파일의 같은 이름과 부딪힌 사고가 있었다.
	// 아래 폴더는 "칸의 기본값" 을 적는 데만 쓴다(생성자). 실제로 쓰는 에셋은 액터 칸(BP_PGRemoteOutpost 에서 바꿀 수 있다)에서 읽는다.
	const TCHAR* const RemoteOutpost_MilitaryVehicles = TEXT("/Game/Military_Free/Static_Meshes/Vehicles/");
	const TCHAR* const RemoteOutpost_MilitaryBuildings = TEXT("/Game/Military_Free/Static_Meshes/Buildings/");
	const TCHAR* const RemoteOutpost_MilitaryDecor = TEXT("/Game/Military_Free/Static_Meshes/Decorations/");
	const TCHAR* const RemoteOutpost_ExitKit = TEXT("/Game/PG/Props/ExitKit/");
	const TCHAR* const RemoteOutpost_CraterFolder = TEXT("/Game/ParagonRampage/FX/Meshes/Debris/");

	// 폴더 + 이름 → 에셋 경로("/Game/.../SM_X.SM_X").
	FSoftObjectPath RemoteOutpost_Path(const TCHAR* Folder, const TCHAR* Name)
	{
		return FSoftObjectPath(FString::Printf(TEXT("%s%s.%s"), Folder, Name, Name));
	}

	// 칸의 메시를 불러온다. 비었거나 못 읽으면 칸 이름을 모아 로그 한 줄로 알린다 — 경로가 틀려도 조용히 넘어가면 안 된다.
	UStaticMesh* RemoteOutpost_Load(const TSoftObjectPtr<UStaticMesh>& Slot, const TCHAR* SlotName, TArray<FString>& Missing)
	{
		UStaticMesh* Mesh = Slot.IsNull() ? nullptr : Slot.LoadSynchronous();
		if (!Mesh)
			Missing.AddUnique(SlotName);
		return Mesh;
	}

	// 부품 자리 계산: "바운드 가운데가 XY, 회전한 바운드의 가장 낮은 점이 BottomZ" 가 되는 피벗 트랜스폼.
	// 왜 바운드로 맞추나: 팩 메시는 피벗이 제각각이다(무전 차량은 한쪽 끝, 텐트는 가운데). 기울인 헬기도 이 식이면 한 번에 땅에 닿는다.
	FTransform RemoteOutpost_FitOnGround(const UStaticMesh* Mesh, const FVector2D& XY, const FRotator& Rotation, float BottomZ, float Scale)
	{
		const FBoxSphereBounds Bounds = Mesh->GetBounds();
		const FQuat Q = Rotation.Quaternion();
		const FVector Origin = Bounds.Origin * Scale;
		const FVector Extent = Bounds.BoxExtent * Scale;
		float MinZ = TNumericLimits<float>::Max();
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector Local(
				Origin.X + ((Corner & 1) ? Extent.X : -Extent.X),
				Origin.Y + ((Corner & 2) ? Extent.Y : -Extent.Y),
				Origin.Z + ((Corner & 4) ? Extent.Z : -Extent.Z));
			MinZ = FMath::Min(MinZ, static_cast<float>(Q.RotateVector(Local).Z));
		}
		const FVector CentreOffset = Q.RotateVector(Origin);
		const FVector Pivot(XY.X - CentreOffset.X, XY.Y - CentreOffset.Y, BottomZ - MinZ);
		return FTransform(Q, Pivot, FVector(Scale));
	}

	// 나이아가라 시스템을 리플렉션으로 붙인다(PGFlowStage 의 SpawnNiagaraByName 과 같은 이유).
	// 왜: UNiagaraComponent 를 직접 쓰려면 Build.cs 에 Niagara 모듈 의존을 걸어야 하고, 플러그인이 꺼진 PC 에서는 모듈이 안 올라온다.
	//   연기 하나 때문에 게임이 안 뜨면 안 된다 — 클래스를 이름으로 찾고, 없으면 경고만 남기고 건너뛴다.
	USceneComponent* RemoteOutpost_AddNiagara(AActor* Owner, USceneComponent* Parent, const TSoftObjectPtr<UObject>& SystemSlot, const FVector& Relative, float Scale)
	{
		UClass* ComponentClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraComponent"));
		if (!ComponentClass)
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PGRemoteOutpost: Niagara plugin not loaded — %s skipped"), *SystemSlot.ToString());
			return nullptr;
		}
		UObject* System = SystemSlot.IsNull() ? nullptr : SystemSlot.LoadSynchronous();
		FObjectPropertyBase* AssetProperty = FindFProperty<FObjectPropertyBase>(ComponentClass, TEXT("Asset"));
		if (!System || !AssetProperty || !System->IsA(AssetProperty->PropertyClass))
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PGRemoteOutpost: niagara system %s missing"), *SystemSlot.ToString());
			return nullptr;
		}
		USceneComponent* Component = NewObject<USceneComponent>(Owner, ComponentClass);
		AssetProperty->SetObjectPropertyValue_InContainer(Component, System);
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetupAttachment(Parent);
		Component->SetRelativeLocation(Relative);
		Component->SetRelativeScale3D(FVector(Scale));
		Owner->AddInstanceComponent(Component);
		Component->RegisterComponent();
		Component->Activate(true);
		return Component;
	}

	// 배치 도우미. 로컬 좌표: 원점 = 거점 가운데 바닥, +X = 맵 가운데 쪽.
	struct FRemoteOutpostBuilder
	{
		TArray<FPGRemoteOutpostPiece> Pieces;
		// 막는 부품의 발자국(로컬 XY, 회전 바운드를 감싼 상자). 상자 자리가 부품에 파묻히지 않았는지 마지막에 검사한다.
		TArray<FBox2D> SolidFeet;
		TArray<FString> Missing;

		void Add(UStaticMesh* Mesh, const FTransform& Relative, bool bCollide, bool bShadow)
		{
			if (!Mesh)
				return;
			FPGRemoteOutpostPiece& Piece = Pieces.AddDefaulted_GetRef();
			Piece.Mesh = Mesh;
			Piece.Relative = Relative;
			Piece.bCollide = bCollide;
			Piece.bShadow = bShadow;
			if (bCollide)
			{
				const FBox Local = Mesh->GetBoundingBox().TransformBy(Relative);
				SolidFeet.Add(FBox2D(FVector2D(Local.Min.X, Local.Min.Y), FVector2D(Local.Max.X, Local.Max.Y)));
			}
		}

		// 바운드 가운데를 XY 에, 가장 낮은 점을 BottomZ 에(음수면 땅에 박힌다).
		void Place(UStaticMesh* Mesh, const FVector2D& XY, const FRotator& Rotation, bool bCollide, bool bShadow, float BottomZ = 0.0f, float Scale = 1.0f)
		{
			if (Mesh)
				Add(Mesh, RemoteOutpost_FitOnGround(Mesh, XY, Rotation, BottomZ, Scale), bCollide, bShadow);
		}
	};
}

APGRemoteOutpostActor::APGRemoteOutpostActor()
{
	// 틱은 붉은 등 깜빡임(0.1초 간격)과 시험 스크린샷만. 컴포넌트 수십 개를 매 프레임 건드리지 않는다.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	bAlwaysRelevant = true; // 멀티(9/27): 큰 건물이라 150m(기본 복제 거리) 밖에서 사라졌다 나타났다. 맵이 600m 라 늘 보낸다
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// 한 번 세우면 안 움직인다. Static 이면 그림자 캐시(VSM)가 매 프레임 다시 그리지 않는다.
	RootScene->SetMobility(EComponentMobility::Static);
	SetRootComponent(RootScene);
	// 차·로봇이 쳐도 잔해가 날아가지 않게(PGPhysicsUtil::TryKnockProp 이 이 표를 보고 거부). 보상 거점은 늘 같은 모양이어야 한다.
	// 단 피날레 붕괴에는 같이 꺼진다 — WarZoneFootprintPreview::CollapseRegion 이 PGRemoteOutpost 표를 예외로 둔다(구덩이 위에 떠 있지 않게).
	Tags.AddUnique(PGPhysicsUtil::ProtectedTag);
	Tags.AddUnique(TEXT("PGRemoteOutpost"));

	// 겉모습 칸 기본값: 9/22 에 코드에 적어 두었던 에셋 그대로(블루프린트가 없어도 모습이 같다).
	HelicopterMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Helicopter_001")));
	CraterMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_CraterFolder, TEXT("SM_Rampage_Rock_Rip_Crater")));
	RotorMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Blade_001")));
	TailRotorMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Blade_002")));
	DoorMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Door_003")));
	LowWallMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Barrier_007")));
	TiresMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Tires_001")));
	TowerMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryBuildings, TEXT("SM_Tower_003")));
	RadioMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryBuildings, TEXT("SM_Radiostation_001")));
	GeneratorMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Generator_004")));
	TableMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Table_002")));
	TentMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryBuildings, TEXT("SM_Tent_002")));
	HummerMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Hummer_003")));
	SandbagMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_ExitKit, TEXT("SM_PGExit_SandbagCurve")));
	MachineGunMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryVehicles, TEXT("SM_Machine_gun_004")));
	HedgehogMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Hedgehog_001")));
	CrateMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Box_022")));
	SmallBoxMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Box_003")));
	DrumMesh = TSoftObjectPtr<UStaticMesh>(RemoteOutpost_Path(RemoteOutpost_MilitaryDecor, TEXT("SM_Barrel_005")));
	TintMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Props/Outpost/Materials/M_PGMilitaryTint.M_PGMilitaryTint")));
	// 연기·불: 타이틀 무대(PGFlowStage)가 쓰는 것과 같은 나이아가라. 이미 프로젝트에 있고 그림체가 맞는다.
	SmokeFx = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Fishermans_Cabin/VFX/VFX_Niagara/NS_Smoke.NS_Smoke")));
	FireFx = TSoftObjectPtr<UObject>(FSoftObjectPath(TEXT("/Game/Fishermans_Cabin/VFX/VFX_Niagara/NS_Fireplace.NS_Fireplace")));
	// 등 자체가 보이게 하는 빛나는 공(AK-47 팩 총구 불꽃용 구·재질).
	GlowMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/AK-47/FX/Meshes/St_Sphere_01.St_Sphere_01")));
	GlowMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/AK-47/FX/Materials/M_GlowSphere_01.M_GlowSphere_01")));
}

void APGRemoteOutpostActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGRemoteOutpostActor, Pieces);
	DOREPLIFETIME(APGRemoteOutpostActor, SmokeRelative);
	DOREPLIFETIME(APGRemoteOutpostActor, FireRelative);
	DOREPLIFETIME(APGRemoteOutpostActor, BeaconRelative);
}

APGRemoteOutpostActor* APGRemoteOutpostActor::SpawnOutpost(UWorld* World, const FVector& Centre, float InwardYaw, int64 Seed, const FString& InCornerLabel)
{
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
		return nullptr;

	const FTransform ActorToWorld(FRotator(0.0f, InwardYaw, 0.0f), Centre);
	// 설정(ProjectPG Visuals > Remote Outpost Class)에 블루프린트가 있으면 그것으로. 부품 에셋은 그 클래스의 칸에서 읽는다.
	UClass* OutpostClass = UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().RemoteOutpostClass, APGRemoteOutpostActor::StaticClass());
	APGRemoteOutpostActor* Actor = World->SpawnActorDeferred<APGRemoteOutpostActor>(OutpostClass, ActorToWorld, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Actor))
		return nullptr;
	Actor->CornerLabel = InCornerLabel;

	// 시드는 모양을 "조금씩" 흔드는 데만 쓴다(기울기·흩어진 조각 각도). 큰 배치는 고정 — 손으로 짠 배치가 가장 보기 좋고,
	// 무작위로 흩으면 텐트가 헬기에 박히는 식의 사고가 난다.
	FRandomStream Stream(static_cast<int32>(Seed ^ (Seed >> 32)) ^ 0x0C0FFEE);
	auto Jitter = [&Stream](float Range) { return Stream.FRandRange(-Range, Range); };

	FRemoteOutpostBuilder B;
	TArray<FString>& Missing = B.Missing;

	// ---- 1) 추락한 헬기 (서쪽 = 맵 가장자리 쪽 절반) ----
	// 몸통은 긴 쪽이 메시 Y(14.7m). 옆으로 13도 눕히고(Pitch = Y축 회전 = 옆으로 기움) 앞을 6도 박는다(Roll).
	// 40cm 땅에 묻는다 — 추락한 것이 땅 위에 얌전히 얹혀 있으면 주차된 헬기로 보인다.
	const FVector2D HeliXY(-400.0f, -100.0f);
	const float HeliYaw = 20.0f + Jitter(6.0f);
	if (UStaticMesh* Heli = RemoteOutpost_Load(Actor->HelicopterMesh, TEXT("Helicopter"), Missing))
		B.Place(Heli, HeliXY, FRotator(-13.0f + Jitter(2.0f), HeliYaw, 6.0f), true, true, -40.0f);

	// 헬기 양 끝에 파헤쳐진 흙·바위(Paragon 팩 바닥 찢김 조각). 피벗이 원래 땅 높이에 맞춰져 있어 피벗을 땅에 둔다.
	if (UStaticMesh* Crater = RemoteOutpost_Load(Actor->CraterMesh, TEXT("Crater"), Missing))
	{
		const FVector2D Axis(-FMath::Sin(FMath::DegreesToRadians(HeliYaw)), FMath::Cos(FMath::DegreesToRadians(HeliYaw)));
		const FVector2D Ends[] = { HeliXY + Axis * 820.0f, HeliXY - Axis * 800.0f, HeliXY + FVector2D(Axis.Y, -Axis.X) * 420.0f };
		// 9/22 첫 스크린샷: 1.5배는 헬기 옆에 회색 바위 덩어리가 서 있는 것처럼 보였다. 작게, 더 깊이 묻는다.
		const float Scales[] = { 1.1f, 0.95f, 0.75f };
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Ends)); ++Index)
			B.Add(Crater, FTransform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), FVector(Ends[Index].X, Ends[Index].Y, -45.0f), FVector(Scales[Index])), false, true);
	}

	// 떨어져 나간 주 회전날개(11.5m 원판). 한쪽 날이 땅에 박히게 살짝 기울인다. 얇아서 밟고 지나가게 충돌은 끈다.
	if (UStaticMesh* Rotor = RemoteOutpost_Load(Actor->RotorMesh, TEXT("Rotor"), Missing))
		B.Place(Rotor, FVector2D(-1000.0f, -820.0f), FRotator(5.0f, Stream.FRandRange(0.0f, 90.0f), -7.0f), false, true, -30.0f);
	// 꼬리 날개: 세워진 원판이라 90도 눕힌다.
	if (UStaticMesh* TailRotor = RemoteOutpost_Load(Actor->TailRotorMesh, TEXT("TailRotor"), Missing))
		B.Place(TailRotor, FVector2D(-1320.0f, 250.0f), FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 88.0f), false, false, -5.0f);
	// 튕겨 나간 문짝.
	if (UStaticMesh* Door = RemoteOutpost_Load(Actor->DoorMesh, TEXT("Door"), Missing))
		B.Place(Door, FVector2D(-30.0f, 380.0f), FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 4.0f), false, false, 0.0f);
	// 가장자리 쪽 낮은 콘크리트 차단벽 하나(맵 바깥 쪽 등 뒤를 막는 느낌). 사람 허리 높이 — 엄폐물.
	if (UStaticMesh* LowWall = RemoteOutpost_Load(Actor->LowWallMesh, TEXT("LowWall"), Missing))
		B.Place(LowWall, FVector2D(-1620.0f, -150.0f), FRotator(0.0f, 90.0f + Jitter(4.0f), 0.0f), true, false);
	// 타이어 더미(헬기 옆 쓰레기).
	if (UStaticMesh* Tires = RemoteOutpost_Load(Actor->TiresMesh, TEXT("Tires"), Missing))
		B.Place(Tires, FVector2D(-120.0f, 1050.0f), FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), true, false);

	// ---- 2) 감시탑 (서북쪽 구석). 꼭대기에 붉은 등 = 멀리서 보이는 미끼 ----
	const FVector2D TowerXY(-1150.0f, 1000.0f);
	float TowerTopZ = 1230.0f;
	if (UStaticMesh* Tower = RemoteOutpost_Load(Actor->TowerMesh, TEXT("Tower"), Missing))
	{
		B.Place(Tower, TowerXY, FRotator(0.0f, 45.0f, 0.0f), true, true);
		TowerTopZ = Tower->GetBounds().Origin.Z + Tower->GetBounds().BoxExtent.Z;
	}

	// ---- 3) 통신 캠프 (동쪽 = 맵 가운데 쪽 절반). 들어오는 사람이 먼저 보는 쪽 ----
	// 무전 안테나 차량(6.7m 안테나) — "통신 거점" 으로 읽히게 하는 중심 부품.
	if (UStaticMesh* Radio = RemoteOutpost_Load(Actor->RadioMesh, TEXT("Radio"), Missing))
		B.Place(Radio, FVector2D(1150.0f, -700.0f), FRotator(0.0f, 0.0f, 0.0f), true, true);
	if (UStaticMesh* Generator = RemoteOutpost_Load(Actor->GeneratorMesh, TEXT("Generator"), Missing))
	{
		B.Place(Generator, FVector2D(1150.0f, 150.0f), FRotator(0.0f, Jitter(5.0f), 0.0f), true, true);
		B.Pieces.Last().bTint = true;
	}
	if (UStaticMesh* Table = RemoteOutpost_Load(Actor->TableMesh, TEXT("Table"), Missing))
		B.Place(Table, FVector2D(650.0f, -700.0f), FRotator(0.0f, 90.0f + Jitter(5.0f), 0.0f), true, false);
	// 텐트(입구 방향을 모르니 긴 쪽을 X 로). 들어갈 수는 없다 — 충돌 상자로 막는다.
	if (UStaticMesh* Tent = RemoteOutpost_Load(Actor->TentMesh, TEXT("Tent"), Missing))
		B.Place(Tent, FVector2D(600.0f, 1100.0f), FRotator(0.0f, 90.0f, 0.0f), true, true);
	// 세워 둔 험비(탈 수는 없는 잔해). 차가 있으면 "사람이 있던 곳" 으로 읽힌다.
	if (UStaticMesh* Hummer = RemoteOutpost_Load(Actor->HummerMesh, TEXT("Hummer"), Missing))
	{
		B.Place(Hummer, FVector2D(1500.0f, 650.0f), FRotator(0.0f, 10.0f + Jitter(6.0f), 0.0f), true, true);
		B.Pieces.Last().bTint = true;
	}
	// 입구 모래주머니 둥지 + 기관총. 굽은 벽은 오목한 쪽이 -X(캠프 안, 지키는 사람 쪽)를 보게 180도(검문소와 같은 규칙).
	if (UStaticMesh* Sandbag = RemoteOutpost_Load(Actor->SandbagMesh, TEXT("Sandbag"), Missing))
		B.Place(Sandbag, FVector2D(1650.0f, -150.0f), FRotator(0.0f, 180.0f, 0.0f), true, true);
	// 기관총은 삼각대째 땅에 세운다. 모래주머니 위(98cm)에 올렸더니 둥지와 자리가 어긋나 공중에 뜬 총으로 보였다(9/22 스크린샷).
	if (UStaticMesh* Gun = RemoteOutpost_Load(Actor->MachineGunMesh, TEXT("MachineGun"), Missing))
		B.Place(Gun, FVector2D(1510.0f, -150.0f), FRotator(0.0f, -90.0f, 0.0f), false, false, 0.0f);
	// 대전차 고슴도치: 무전 차량 바깥 모서리.
	if (UStaticMesh* Hedgehog = RemoteOutpost_Load(Actor->HedgehogMesh, TEXT("Hedgehog"), Missing))
		B.Place(Hedgehog, FVector2D(1650.0f, -680.0f), FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), true, false);
	// 보급품 더미: 무전 차량 옆. 큰 궤짝 + 작은 상자.
	if (UStaticMesh* Crate = RemoteOutpost_Load(Actor->CrateMesh, TEXT("Crate"), Missing))
		B.Place(Crate, FVector2D(640.0f, -1200.0f), FRotator(0.0f, Jitter(8.0f), 0.0f), true, true);
	if (UStaticMesh* SmallBox = RemoteOutpost_Load(Actor->SmallBoxMesh, TEXT("SmallBox"), Missing))
		B.Place(SmallBox, FVector2D(860.0f, -1330.0f), FRotator(0.0f, 20.0f + Jitter(8.0f), 0.0f), true, false);
	if (UStaticMesh* Drum = RemoteOutpost_Load(Actor->DrumMesh, TEXT("Drum"), Missing))
	{
		B.Place(Drum, FVector2D(1000.0f, 430.0f), FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), false, false);
		B.Place(Drum, FVector2D(1075.0f, 500.0f), FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), false, false);
	}

	// ---- 4) 상자 자리(좋은 상자 4개). 전부 하늘이 트인 곳 — 월드 루팅이 "머리 위 2m 가 비었나" 를 검사한다. ----
	// 어떤 상자인지는 월드 루팅 설정(RemoteOutpostBoxes)이 정한다. 여기 적은 것은 자리에 어울리는 기본값.
	struct FSlotDef { FVector2D XY; const TCHAR* ObjectId; };
	const FSlotDef SlotDefs[] = {
		{ FVector2D(90.0f, 80.0f),     TEXT("OBJ-004") }, // 헬기 옆 — 헬기에 실려 있던 무기 상자
		{ FVector2D(600.0f, 560.0f),   TEXT("OBJ-010") }, // 텐트 앞 — 금고(5초 붙잡고 열기)
		{ FVector2D(650.0f, -380.0f),  TEXT("OBJ-002") }, // 무전 탁자 앞 — 군용 보급 상자
		{ FVector2D(-800.0f, 1250.0f), TEXT("OBJ-004") }, // 감시탑 발치 — 두 번째 무기 상자(탄약 상자는 내용물이 약해 뺐다)
	};
	int32 BuriedSlots = 0;
	for (const FSlotDef& Def : SlotDefs)
	{
		// 부품 발자국에서 60cm 안이면 파묻힌 자리다 — 배치를 고쳤는데 자리를 안 옮긴 실수를 로그로 잡는다.
		const FBox2D SlotFoot(Def.XY - FVector2D(60.0f), Def.XY + FVector2D(60.0f));
		if (B.SolidFeet.ContainsByPredicate([&SlotFoot](const FBox2D& Foot) { return Foot.Intersect(SlotFoot); }))
			++BuriedSlots;
		FPGRemoteOutpostLootSlot& Slot = Actor->LootSlots.AddDefaulted_GetRef();
		Slot.Location = ActorToWorld.TransformPosition(FVector(Def.XY.X, Def.XY.Y, 0.0f));
		Slot.PreferredObjectId = Def.ObjectId;
	}
	// 바닥 아이템: 거점 가운데 둘레 26m 네모. 부품 위·속 자리는 월드 루팅의 바닥·캡슐 검사가 거른다.
	Actor->FloorLootCentre = Centre;
	Actor->FloorLootHalfExtent = FVector2D(1300.0f, 1300.0f);

	// ---- 5) 미끼: 연기(헬기 엔진) + 작은 불(헬기 옆구리) + 감시탑 꼭대기 빛나는 공·붉은 등 ----
	Actor->SmokeRelative = FVector(HeliXY.X - 20.0f, HeliXY.Y - 20.0f, 260.0f);
	Actor->FireRelative = FVector(HeliXY.X + 150.0f, HeliXY.Y + 40.0f, 30.0f);
	Actor->BeaconRelative = FVector(TowerXY.X, TowerXY.Y, TowerTopZ + 40.0f);

	Actor->Pieces = MoveTemp(B.Pieces);
	Actor->FinishSpawning(ActorToWorld);

	UE_LOG(LogPGObjects, Display, TEXT("PGRemoteOutpost: built class=%s corner=%s at %s yaw=%.0f pieces=%d loot_slots=%d buried_slots=%d missing=%s"),
		*OutpostClass->GetName(), *InCornerLabel, *Centre.ToCompactString(), InwardYaw, Actor->Pieces.Num(), Actor->LootSlots.Num(), BuriedSlots,
		Missing.Num() > 0 ? *FString::Join(Missing, TEXT(",")) : TEXT("none"));
	return Actor;
}

void APGRemoteOutpostActor::BeginPlay()
{
	Super::BeginPlay();
	BuildFromPieces();
	bOutpostShots = HasAuthority() && FParse::Param(FCommandLine::Get(), TEXT("PGOutpostShot"));
}

void APGRemoteOutpostActor::OnRep_Pieces()
{
	BuildFromPieces();
}

void APGRemoteOutpostActor::BuildFromPieces()
{
	// 클라는 BeginPlay 와 OnRep 이 둘 다 부를 수 있다. 목록이 아직 안 왔으면 기다리고, 한 번 지었으면 다시 안 짓는다.
	if (bBuilt || Pieces.Num() == 0)
		return;
	bBuilt = true;
	int32 FallbackBoxes = 0;
	TArray<FString> FallbackNames;
	for (const FPGRemoteOutpostPiece& Piece : Pieces)
	{
		UStaticMesh* Mesh = Piece.Mesh.LoadSynchronous();
		if (!Mesh)
			continue;
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(RootScene);
		Component->SetRelativeTransform(Piece.Relative);
		Component->SetCastShadow(Piece.bShadow);
		if (Piece.bTint)
		{
			// 재질이 없으면(에셋을 안 만든 PC) 원래 모래색 그대로 둔다 — 색 때문에 부품이 빠지면 안 된다.
			if (UMaterialInterface* TintBase = TintMaterial.IsNull() ? nullptr : TintMaterial.LoadSynchronous())
			{
				UMaterialInstanceDynamic* Olive = UMaterialInstanceDynamic::Create(TintBase, Component);
				Olive->SetVectorParameterValue(TEXT("Tint"), OliveTint);
				for (int32 Slot = 0; Slot < Mesh->GetStaticMaterials().Num(); ++Slot)
					Component->SetMaterial(Slot, Olive);
			}
			else
			{
				UE_LOG(LogPGObjects, Warning, TEXT("PGRemoteOutpost: tint material missing — %s keeps the pack colour"), *Mesh->GetName());
			}
		}
		// 길찾기 바닥은 WarZoneFootprintPreview 의 NavigationFloor 가 맡는다. 부품마다 내비 다시 굽기를 시키지 않는다.
		Component->SetCanEverAffectNavigation(false);
		const UBodySetup* Body = Mesh->GetBodySetup();
		// "복잡한 모양을 단순 충돌로 쓰기" 로 설정된 메시는 단순 도형이 0개여도 막는다.
		const bool bHasSimple = Body && (Body->AggGeom.GetElementCount() > 0 || Body->CollisionTraceFlag == CTF_UseComplexAsSimple);
		if (Piece.bCollide && bHasSimple)
			Component->SetCollisionProfileName(TEXT("BlockAll"));
		else
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->RegisterComponent();
		Parts.Add(Component);

		// 막아야 하는데 메시에 단순 충돌이 없으면(팩 메시 중 일부) 사람이 헬기·텐트를 뚫고 지나간다.
		// 바운드 크기의 보이지 않는 상자를 대신 세운다. 기울인 헬기는 상자도 같이 기운다.
		if (Piece.bCollide && !bHasSimple)
		{
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			UBoxComponent* Box = NewObject<UBoxComponent>(this);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetupAttachment(RootScene);
			Box->SetRelativeLocation(Piece.Relative.TransformPosition(Bounds.Origin));
			Box->SetRelativeRotation(Piece.Relative.GetRotation());
			Box->SetBoxExtent(Bounds.BoxExtent * Piece.Relative.GetScale3D() * FVector(0.92f, 0.92f, 1.0f));
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			Box->SetHiddenInGame(true);
			Box->SetCanEverAffectNavigation(false);
			Box->RegisterComponent();
			Parts.Add(Box);
			++FallbackBoxes;
			FallbackNames.AddUnique(Mesh->GetName());
		}
	}
	UE_LOG(LogPGObjects, Display, TEXT("PGRemoteOutpost: %d parts built, %d collision boxes stood in for meshes without simple collision (%s)"),
		Parts.Num() - FallbackBoxes, FallbackBoxes, FallbackNames.Num() > 0 ? *FString::Join(FallbackNames, TEXT(",")) : TEXT("none"));
	BuildLure();
}

void APGRemoteOutpostActor::BuildLure()
{
	if (bLureBuilt || GetNetMode() == NM_DedicatedServer)
		return;
	bLureBuilt = true;
	// 연기는 굴뚝 연기(NS_Smoke)라 가늘다 — 입자 크기가 컴포넌트 배율을 따르지 않는다. 9/22 스크린샷에서 3줄기·5줄기 모두
	//   낮에는 거의 안 보였다. 그래서 멀리서 보이는 미끼는 감시탑 꼭대기의 빛나는 공 + 붉은 등이 맡고, 연기는 헬기 옆 분위기용 두 줄기만 둔다.
	//   (제대로 된 연기 기둥이 필요하면 전용 나이아가라 에셋을 만들어야 한다 — 이 파일에서 경로만 바꾸면 된다.)
	int32 SmokeCount = 0;
	const FVector SmokeOffsets[] = { FVector::ZeroVector, FVector(130.0f, 70.0f, -60.0f) };
	for (const FVector& Offset : SmokeOffsets)
		SmokeCount += RemoteOutpost_AddNiagara(this, RootScene, SmokeFx, SmokeRelative + Offset, SmokeScale) != nullptr ? 1 : 0;
	const bool bFire = RemoteOutpost_AddNiagara(this, RootScene, FireFx, FireRelative, FireScale) != nullptr;
	RemoteOutpost_AddNiagara(this, RootScene, FireFx, FireRelative + FVector(-160.0f, -60.0f, 60.0f), FireScale * 0.8f);
	// 빛나는 공. 충돌·그림자 없음. 등과 같이 깜빡인다(Tick).
	UStaticMesh* GlowMeshAsset = GlowMesh.IsNull() ? nullptr : GlowMesh.LoadSynchronous();
	UMaterialInterface* GlowMaterialAsset = GlowMaterial.IsNull() ? nullptr : GlowMaterial.LoadSynchronous();
	if (GlowMeshAsset && GlowMaterialAsset)
	{
		BeaconGlow = NewObject<UStaticMeshComponent>(this);
		BeaconGlow->SetMobility(EComponentMobility::Movable);
		BeaconGlow->SetStaticMesh(GlowMeshAsset);
		BeaconGlow->SetMaterial(0, GlowMaterialAsset);
		BeaconGlow->SetupAttachment(RootScene);
		BeaconGlow->SetRelativeLocation(BeaconRelative);
		BeaconGlow->SetRelativeScale3D(FVector(GlowDiameterCm / FMath::Max(1.0f, static_cast<float>(GlowMeshAsset->GetBounds().BoxExtent.X) * 2.0f)));
		BeaconGlow->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		BeaconGlow->SetCastShadow(false);
		BeaconGlow->SetCanEverAffectNavigation(false);
		BeaconGlow->RegisterComponent();
	}
	const bool bSmoke = SmokeCount > 0;
	// 붉은 등: 그림자 없는 점광원 하나. Tick 에서 천천히 깜빡인다(항공 장애등처럼).
	Beacon = NewObject<UPointLightComponent>(this);
	Beacon->SetMobility(EComponentMobility::Movable);
	Beacon->SetupAttachment(RootScene);
	Beacon->SetRelativeLocation(BeaconRelative);
	Beacon->SetIntensityUnits(ELightUnits::Candelas);
	Beacon->SetIntensity(BeaconCandelas);
	Beacon->SetLightColor(BeaconColor);
	Beacon->SetAttenuationRadius(BeaconRadiusCm);
	Beacon->SetCastShadows(false);
	Beacon->RegisterComponent();
	UE_LOG(LogPGObjects, Display, TEXT("PGRemoteOutpost: lure smoke=%s(%d) fire=%s beacon=yes glow=%s"), bSmoke ? TEXT("yes") : TEXT("no"), SmokeCount,
		bFire ? TEXT("yes") : TEXT("no"), IsValid(BeaconGlow) ? TEXT("yes") : TEXT("no"));
}

void APGRemoteOutpostActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 1.4초 주기로 0.35초 켜짐 — 항공 장애등 리듬. 켜고 끄기만 한다(밝기를 매 틱 바꾸면 렌더 상태가 계속 더러워진다).
	if (IsValid(Beacon))
	{
		BeaconClock = FMath::Fmod(BeaconClock + DeltaSeconds, 1.4f);
		const bool bOn = BeaconClock < 0.35f;
		if (Beacon->IsVisible() != bOn)
		{
			Beacon->SetVisibility(bOn);
			if (IsValid(BeaconGlow))
				BeaconGlow->SetVisibility(bOn);
		}
	}
	if (bOutpostShots)
		TickOutpostShots(DeltaSeconds);
}

// ---- 시험용 스크린샷(-PGOutpostShot) ----
// 가까이 두 장, 위에서 한 장, 맵 가운데 쪽 멀리서 한 장(미끼가 보이는지). 찍고 나면 게임을 끈다.
void APGRemoteOutpostActor::TickOutpostShots(float DeltaSeconds)
{
	ShotClock += DeltaSeconds;
	// 시설 레벨·텍스처가 뜰 시간을 준다.
	constexpr float FirstShotAt = 14.0f;
	constexpr float ShotSpacing = 4.0f;
	struct FShot { FVector Eye; FVector Look; const TCHAR* Name; };
	const FShot Shots[] = {
		{ FVector(2900.0f, 1700.0f, 900.0f),   FVector(-100.0f, 0.0f, 150.0f),  TEXT("01_camp") },
		{ FVector(-600.0f, -2700.0f, 700.0f),  FVector(-400.0f, 0.0f, 200.0f),  TEXT("02_wreck") },
		{ FVector(900.0f, 0.0f, 5200.0f),      FVector(0.0f, 0.0f, 0.0f),       TEXT("03_top") },
		{ FVector(14000.0f, 2500.0f, 1400.0f), FVector(0.0f, 0.0f, 600.0f),     TEXT("04_far") },
		{ FVector(700.0f, -1100.0f, 350.0f),   FVector(-400.0f, -100.0f, 350.0f), TEXT("05_smoke") },
	};
	if (ShotIndex >= static_cast<int32>(UE_ARRAY_COUNT(Shots)))
	{
		if (ShotClock >= FirstShotAt + ShotSpacing * ShotIndex + 1.5f)
		{
			bOutpostShots = false;
			UE_LOG(LogPGObjects, Display, TEXT("PGRemoteOutpost: test shots done — quitting"));
			FPlatformMisc::RequestExit(false);
		}
		return;
	}
	const float PoseAt = FirstShotAt + ShotSpacing * ShotIndex - 2.5f;
	if (ShotClock < PoseAt)
		return;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (!IsValid(PC))
		return;
	const FShot& Shot = Shots[ShotIndex];
	const FVector Eye = GetActorTransform().TransformPosition(Shot.Eye);
	const FVector Look = GetActorTransform().TransformPosition(Shot.Look);
	if (!IsValid(ShotCamera))
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ShotCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(Eye), Params);
		if (IsValid(ShotCamera) && ShotCamera->GetCameraComponent())
		{
			ShotCamera->GetCameraComponent()->SetConstraintAspectRatio(false);
			ShotCamera->GetCameraComponent()->SetFieldOfView(75.0f);
		}
	}
	if (!IsValid(ShotCamera))
		return;
	// 폰 빙의가 시점을 되돌릴 수 있어 매 틱 다시 건다(싸다).
	ShotCamera->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
	if (PC->GetViewTarget() != ShotCamera)
		PC->SetViewTarget(ShotCamera);
	if (ShotClock >= FirstShotAt + ShotSpacing * ShotIndex)
	{
		const FString File = FPaths::ScreenShotDir() / FString::Printf(TEXT("PGOutpost_%s.png"), Shot.Name);
		FScreenshotRequest::RequestScreenshot(File, false, false);
		UE_LOG(LogPGObjects, Display, TEXT("PGRemoteOutpost: screenshot %s eye=%s"), *File, *Eye.ToCompactString());
		++ShotIndex;
	}
}
