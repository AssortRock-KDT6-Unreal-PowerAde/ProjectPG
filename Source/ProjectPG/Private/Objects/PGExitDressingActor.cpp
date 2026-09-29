#include "Objects/PGExitDressingActor.h"

#include "Common/PGPhysicsUtil.h"
#include "Common/PGVisualSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGExtractionZoneActor.h"
#include "Objects/PGObjectTypes.h"

namespace
{
	// 익명 namespace 상수는 파일 고유 접두어(ExitDressing_)를 붙인다 — 유니티 빌드에서 다른 파일의 같은 이름 상수와 충돌한 사고가 있었다.
	const TCHAR* const ExitDressing_FactoryFolder = TEXT("/Game/Factory_Pack_V1/Meshes/");
	const TCHAR* const ExitDressing_MilitaryBuildings = TEXT("/Game/Military_Free/Static_Meshes/Buildings/");
	const TCHAR* const ExitDressing_MilitaryDecor = TEXT("/Game/Military_Free/Static_Meshes/Decorations/");
	const TCHAR* const ExitDressing_TSLFolder = TEXT("/Game/TSL_CQBModularCore/Static_meshes/");
	// 검문소 전용 킷(9/22): 문틀·초소·제어함·모래주머니 벽. Tools/import_exit_kit.py 로 들어온다(README_ExitKit.md).
	const TCHAR* const ExitDressing_KitFolder = TEXT("/Game/PG/Props/ExitKit/");

	// 킷 문틀(SM_PGExit_Gate) 치수. 메시가 이 숫자로 만들어졌다 — 바꾸면 Blender 스크립트(build_procedural.py)도 같이.
	// 기둥 가운데 Y=±3.5m(안쪽 면 ±3.1m → 길 6m 에 양옆 10cm 여유), 기초 1.1m 네모.
	constexpr float ExitDressing_GatePillarYCm = 350.0f;
	constexpr float ExitDressing_GateFootHalfCm = 55.0f;
	// 간판 판: 앞면(-X)이 X=-21cm, 가운데 높이 549cm, 3m x 0.8m. 글자는 판 10cm 앞에 띄운다(판에 파묻혀 깜빡이지 않게).
	constexpr float ExitDressing_GateSignFrontXCm = -21.0f;
	constexpr float ExitDressing_GateSignCentreZCm = 549.0f;
	// 글자 크기 배율. 기본 글자 높이 1m(SetWorldSize 100) 를 0.6 으로 줄여야 0.8m 판 안에 들어간다.
	constexpr float ExitDressing_GateSignScale = 0.6f;

	// 길 반폭 3m(폭 6m): 험비(폭 2.8m)가 여유 있게 지나고, 탱크(폭 3.5m 안팎)도 지난다.
	constexpr float ExitDressing_LaneHalfCm = 300.0f;
	// 펜스 게이트(문짝 2.05m) 양옆 여유. 펜스 줄은 여기부터 시작한다.
	constexpr float ExitDressing_GateGapHalfCm = 120.0f;
	// 탈출 지점은 20m 타일 가운데에서 바깥쪽으로 5.5m 에 있다(WarZoneFootprintPreview::BuildGameplayPointMarkers).
	// → 안쪽 타일 끝은 -15.5m, 옆은 ±10m. 이 밖에는 세우지 않는다(옆 타일의 도로 소품과 겹친다).
	constexpr float ExitDressing_TileInnerEdgeCm = -1550.0f;
	constexpr float ExitDressing_TileSideCm = 990.0f;
	// 펜스 줄이 뻗는 길이(양쪽 각각). 타일 옆 끝(10m) 바로 안쪽까지.
	constexpr float ExitDressing_FenceReachCm = 950.0f;
	// 부품 자리의 땅이 탈출 지점보다 이만큼 높거나 낮으면(호수·산기슭) 그 부품은 건너뛴다.
	constexpr float ExitDressing_GroundToleranceCm = 120.0f;
	// 부품 하나가 이보다 크면(팩 부품 크기를 못 봤을 때의 안전장치 — 예: "Tower" 가 알고 보니 33m 공장 건물) 건너뛴다.
	constexpr float ExitDressing_MaxPieceRadiusCm = 700.0f;

	// 배치 도우미. 로컬 좌표: 원점 = 탈출 지점(바닥), +X = 맵 바깥쪽(길 방향), +Y = 오른쪽.
	struct FExitDressingBuilder
	{
		UWorld* World = nullptr;
		FTransform ActorToWorld;
		float GroundZ = 0.0f;
		// 비울 곳(로컬): 탈출 액터(차·헬기·게이트·영역) 둘레.
		FBox KeepClear = FBox(ForceInit);
		FRandomStream* Stream = nullptr;
		FCollisionQueryParams Query;

		TArray<FPGExitDressingPiece> Pieces;
		// 이미 놓은 부품의 발자국(로컬 XY). 부품끼리 겹치지 않게.
		TArray<FBox2D> Occupied;
		TArray<FString> Missing;
		int32 Skipped = 0;

		// 메시를 불러온다. 없으면 이름을 기록하고 nullptr — 경로가 틀려도 조용히 넘어가면 안 되니 로그 한 줄에 모아 찍는다.
		UStaticMesh* Load(const TSoftObjectPtr<UStaticMesh>& Slot, const TCHAR* Name)
		{
			UStaticMesh* Mesh = Slot.IsNull() ? nullptr : Slot.LoadSynchronous();
			if (!Mesh)
				Missing.AddUnique(Name);
			return Mesh;
		}

		// 검문소 킷 메시를 불러온다. 없으면(임포트 전 PC) 이름만 적어 두고 nullptr — 부르는 쪽이 예전 팩 부품으로 되돌아간다.
		// 왜 Missing 과 따로 적나: 킷이 없는 건 "경로가 틀림" 이 아니라 "아직 안 들여옴" 이다. 로그에서 둘을 구분해야 원인을 바로 안다.
		TArray<FString> KitFallbacks;
		UStaticMesh* LoadKit(const TSoftObjectPtr<UStaticMesh>& Slot, const TCHAR* Name)
		{
			UStaticMesh* Mesh = Slot.IsNull() ? nullptr : Slot.LoadSynchronous();
			if (!Mesh)
				KitFallbacks.AddUnique(Name);
			return Mesh;
		}

		// 로컬 XY 자리의 땅 높이(로컬 Z). 위에서 아래로 선을 쏜다. 땅이 없거나 탈출 지점과 너무 다르면 false.
		bool GroundAt(const FVector2D& Local, float& OutLocalZ) const
		{
			const FVector WorldXY = ActorToWorld.TransformPosition(FVector(Local.X, Local.Y, 0.0f));
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, WorldXY + FVector(0.0f, 0.0f, 300.0f), WorldXY - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, Query))
				return false;
			if (FMath::Abs(Hit.ImpactPoint.Z - GroundZ) > ExitDressing_GroundToleranceCm)
				return false;
			OutLocalZ = Hit.ImpactPoint.Z - GroundZ;
			return true;
		}

		// 발자국이 타일 안이고, 비울 곳·길·이미 놓은 부품과 안 겹치는가.
		bool FootprintFree(const FBox2D& Foot, bool bLaneMatters, float InnerLimit) const
		{
			if (Foot.Min.X < InnerLimit || Foot.Max.Y > ExitDressing_TileSideCm || Foot.Min.Y < -ExitDressing_TileSideCm)
				return false;
			if (FBox2D(FVector2D(KeepClear.Min.X, KeepClear.Min.Y), FVector2D(KeepClear.Max.X, KeepClear.Max.Y)).Intersect(Foot))
				return false;
			if (bLaneMatters && Foot.Min.Y < ExitDressing_LaneHalfCm && Foot.Max.Y > -ExitDressing_LaneHalfCm)
				return false;
			for (const FBox2D& Other : Occupied)
				if (Other.Intersect(Foot))
					return false;
			return true;
		}

		void Add(UStaticMesh* Mesh, const FTransform& Relative, bool bCollide, bool bShadow)
		{
			FPGExitDressingPiece& Piece = Pieces.AddDefaulted_GetRef();
			Piece.Mesh = Mesh;
			Piece.Relative = Relative;
			Piece.bCollide = bCollide;
			Piece.bShadow = bShadow;
		}

		// 부품을 "바운드 가운데가 Center, 바운드 밑면이 땅" 이 되게 놓는다.
		// 왜 바운드로 맞추나: 팩 부품은 피벗이 제각각이다(컨테이너는 모서리, 펜스는 한쪽 끝, 공장 울타리는 가운데 높이).
		//   피벗 대신 바운드로 자리를 잡으면 어떤 부품이든 같은 식으로 놓인다.
		// 검사 순서: 크기 → 땅 → 발자국(타일·길·비울 곳·다른 부품) → 월드 충돌(타일에 원래 있던 대피소·버려진 차·나무).
		bool Place(UStaticMesh* Mesh, const FVector2D& Center, float Yaw, bool bCollide, bool bShadow,
			bool bLaneMatters = true, bool bWorldTest = true, float InnerLimit = ExitDressing_TileInnerEdgeCm)
		{
			if (!Mesh)
				return false;
			const FBoxSphereBounds Bounds = Mesh->GetBounds();
			if (Bounds.SphereRadius > ExitDressing_MaxPieceRadiusCm)
			{
				++Skipped;
				return false;
			}
			float GroundLocalZ = 0.0f;
			if (!GroundAt(Center, GroundLocalZ))
			{
				++Skipped;
				return false;
			}
			const FRotator Rot(0.0f, Yaw, 0.0f);
			// 회전한 바운드 상자를 감싸는 축 정렬 발자국.
			const float Cos = FMath::Abs(FMath::Cos(FMath::DegreesToRadians(Yaw)));
			const float Sin = FMath::Abs(FMath::Sin(FMath::DegreesToRadians(Yaw)));
			const FVector2D HalfFoot(Bounds.BoxExtent.X * Cos + Bounds.BoxExtent.Y * Sin, Bounds.BoxExtent.X * Sin + Bounds.BoxExtent.Y * Cos);
			const FBox2D Foot(Center - HalfFoot, Center + HalfFoot);
			if (!FootprintFree(Foot, bLaneMatters, InnerLimit))
			{
				++Skipped;
				return false;
			}
			if (bWorldTest)
			{
				// 상자를 땅에서 20cm 띄우고 옆으로 10cm 줄여 검사한다 — 땅·도로 판(10cm 차이)에 걸리지 않게.
				const FVector WorldCentre = ActorToWorld.TransformPosition(FVector(Center.X, Center.Y, GroundLocalZ + Bounds.BoxExtent.Z + 20.0f));
				const FQuat WorldRot = ActorToWorld.GetRotation() * Rot.Quaternion();
				const FVector Extent = (Bounds.BoxExtent - FVector(10.0f, 10.0f, 20.0f)).ComponentMax(FVector(2.0f));
				if (World->OverlapBlockingTestByChannel(WorldCentre, WorldRot, ECC_Pawn, FCollisionShape::MakeBox(Extent), Query))
				{
					++Skipped;
					return false;
				}
				// 한 번 더: 탈출 지점 바닥 높이(로컬 0) 기준으로, 사람 채널이 아니라 "물건 종류" 로 겹침을 본다.
				// 왜: 타일에 원래 있던 폐차는 사람 채널 검사에 안 걸렸고, 땅 찾기 선이 차 지붕에 맞아 그 높이를 땅으로 읽어
				//   초소가 차 위에 얹혔다(9/22 PIE: "검문소가 공중에 떠 있길래 봤더니 차 위에 스폰"). 바닥 높이로 다시 재면 차 몸통과 겹친다.
				FCollisionObjectQueryParams Objects;
				Objects.AddObjectTypesToQuery(ECC_WorldStatic);
				Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
				Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
				Objects.AddObjectTypesToQuery(ECC_Vehicle);
				const FVector AtLaneGround = ActorToWorld.TransformPosition(FVector(Center.X, Center.Y, Bounds.BoxExtent.Z + 30.0f));
				if (FMath::Abs(GroundLocalZ) > 40.0f || World->OverlapAnyTestByObjectType(AtLaneGround, WorldRot, Objects, FCollisionShape::MakeBox(Extent), Query))
				{
					++Skipped;
					return false;
				}
			}
			// 피벗 자리 = 원하는 바운드 가운데 - (회전한) 피벗→바운드 가운데 벡터. 높이는 바운드 밑면이 땅에 닿게.
			const FVector Offset = Rot.RotateVector(Bounds.Origin);
			const FVector Pivot(Center.X - Offset.X, Center.Y - Offset.Y, GroundLocalZ - (Bounds.Origin.Z - Bounds.BoxExtent.Z));
			Add(Mesh, FTransform(Rot, Pivot), bCollide, bShadow);
			Occupied.Add(Foot);
			return true;
		}
	};

	// 수평 방향을 가장 가까운 격자 축(±X·±Y)으로 붙인다.
	FVector ExitDressing_SnapToAxis(const FVector& Direction)
	{
		const FVector Flat = Direction.GetSafeNormal2D();
		if (Flat.IsNearlyZero())
			return FVector::ForwardVector;
		return FMath::Abs(Flat.X) >= FMath::Abs(Flat.Y)
			? FVector(FMath::Sign(Flat.X), 0.0f, 0.0f)
			: FVector(0.0f, FMath::Sign(Flat.Y), 0.0f);
	}
}

APGExitDressingActor::APGExitDressingActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true; // 멀티(9/27): 큰 건물이라 150m(기본 복제 거리) 밖에서 사라졌다 나타났다. 맵이 600m 라 늘 보낸다
	RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	// 한 번 세우면 안 움직인다. Static 이면 그림자 캐시(VSM)가 매 프레임 다시 그리지 않는다.
	RootScene->SetMobility(EComponentMobility::Static);
	SetRootComponent(RootScene);
	// 차·로봇·드래곤·전함이 쳐도 안 부서진다(PGPhysicsUtil::TryKnockProp 이 이 표를 보고 거부). 탈출구는 늘 같은 자리에 같은 모양으로.
	Tags.AddUnique(PGPhysicsUtil::ProtectedTag);
	Tags.AddUnique(TEXT("PGExitDressing"));

	// 겉모습 칸 기본값: 9/22 에 코드에 적어 두었던 에셋 그대로(블루프린트가 없어도 모습이 같다).
	auto Path = [](const TCHAR* Folder, const TCHAR* Name) { return FSoftObjectPath(FString::Printf(TEXT("%s%s.%s"), Folder, Name, Name)); };
	FencePanelMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_Fence_3_a")));
	FencePanelFallbackMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_Fence_1")));
	BoomArmMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_Barriere_7_b")));
	GateFrameMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_KitFolder, TEXT("SM_PGExit_Gate")));
	GatePostMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_TSLFolder, TEXT("SM_Board")));
	BoomPedestalMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_Barriere_3")));
	ControlCabinetMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_KitFolder, TEXT("SM_PGExit_Cabinet")));
	GuardBoothMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_KitFolder, TEXT("SM_PGExit_Booth")));
	ConcreteBarrierMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_barrier")));
	HedgehogMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_MilitaryDecor, TEXT("SM_Hedgehog_001")));
	ContainerMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM__Container")));
	WatchTowerMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_MilitaryBuildings, TEXT("SM_Tower_003")));
	TentMeshA = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_MilitaryBuildings, TEXT("SM_Tent_002")));
	TentMeshB = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_MilitaryBuildings, TEXT("SM_Tent_010")));
	LanternMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_Lantern")));
	SandbagCurveMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_KitFolder, TEXT("SM_PGExit_SandbagCurve")));
	SandbagStraightMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_KitFolder, TEXT("SM_PGExit_SandbagStraight")));
	DrumMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_barrel_1")));
	GasBottleMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_gas_bottle")));
	PalletMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_pallet")));
	WoodPileMesh = TSoftObjectPtr<UStaticMesh>(Path(ExitDressing_FactoryFolder, TEXT("SM_wood_pile")));
	SignText = FText::FromString(TEXT("EXIT"));
}

FVector APGExitDressingActor::ResolveLaneAxis(const FVector& Ground, const FVector& CellCentre, const FVector& Outward)
{
	// 탈출 지점은 탈출 타일 가운데에서 길 방향으로 5.5m 떨어져 있다(BuildGameplayPointMarkers: Exit 는 회전한 (550,0)).
	// 그래서 "타일 가운데 → 탈출 지점" 이 곧 길 축이다. 자리가 막혀 3.2m 씩 옮겨진 포인트도 있어서, 그 축 성분이 4m 넘을 때만 믿고
	// 아니면 맵 가운데에서 본 방향(Outward)에 가장 가까운 축으로 돌아간다.
	const FVector Delta = (Ground - CellCentre) * FVector(1.0f, 1.0f, 0.0f);
	FVector FromCell = ExitDressing_SnapToAxis(Delta);
	if (FMath::Abs(FVector::DotProduct(Delta, FromCell)) >= 400.0f)
	{
		// 축은 맞아도 부호가 안쪽일 수 있다 — 탈출 지점이 타일 가운데에서 맵 안쪽으로 5.5m 떨어진 칸이 있다.
		// 그러면 +X(바깥쪽)가 맵 안쪽을 가리켜 펜스·문틀·EXIT 글자가 통째로 뒤집혔다(9/22 PIE: "출구판이 왜 반대로 돼 있냐", 로그 lane=V(Y=1) 인데 맵 남쪽 끝).
		// 맵 가운데에서 본 방향(Outward)과 반대면 뒤집는다.
		if (FVector::DotProduct(FromCell, Outward) < 0.0f)
			FromCell = -FromCell;
		return FromCell;
	}
	return ExitDressing_SnapToAxis(Outward);
}

void APGExitDressingActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGExitDressingActor, Pieces);
	DOREPLIFETIME(APGExitDressingActor, SignRelative);
	DOREPLIFETIME(APGExitDressingActor, bHasSign);
}

APGExitDressingActor* APGExitDressingActor::SpawnCheckpoint(UWorld* World, const FVector& Ground, const FVector& Outward, EPGExitDressingKind Kind,
	const FBox& KeepClear, FRandomStream& Stream, int32 ExitIndex)
{
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
		return nullptr;

	// 스포너가 ResolveLaneAxis 로 바로잡은 축을 준다. 혹시 비스듬한 값이 와도 가장 가까운 축에 붙인다.
	const FVector LaneAxis = ExitDressing_SnapToAxis(Outward);
	const FTransform ActorToWorld(FRotator(0.0f, LaneAxis.Rotation().Yaw, 0.0f), Ground);
	// 설정(ProjectPG Visuals > Exit Dressing Class)에 블루프린트가 있으면 그것으로. 부품 에셋은 그 클래스의 칸에서 읽는다.
	UClass* DressingClass = UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().ExitDressingClass, APGExitDressingActor::StaticClass());
	APGExitDressingActor* Actor = World->SpawnActorDeferred<APGExitDressingActor>(DressingClass, ActorToWorld, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!IsValid(Actor))
		return nullptr;

	const bool bGate = Kind == EPGExitDressingKind::FenceGate;
	FExitDressingBuilder B;
	B.World = World;
	B.ActorToWorld = ActorToWorld;
	B.GroundZ = Ground.Z;
	B.Stream = &Stream;
	B.Query = FCollisionQueryParams(SCENE_QUERY_STAT(PGExitDressing), false, Actor);
	// 비울 곳을 로컬로: 월드 상자의 여덟 모서리를 로컬로 옮겨 다시 감싼다(액터가 돌아가 있어서).
	// 탈것은 둘레 60cm 를 더 비운다(문 여닫고 타는 자리). 게이트는 10cm 만 — 펜스 줄이 게이트 바로 옆에서 이어져야 "담" 이 된다.
	if (KeepClear.IsValid)
	{
		FBox Local(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector WorldCorner((Corner & 1) ? KeepClear.Max.X : KeepClear.Min.X, (Corner & 2) ? KeepClear.Max.Y : KeepClear.Min.Y, (Corner & 4) ? KeepClear.Max.Z : KeepClear.Min.Z);
			Local += ActorToWorld.InverseTransformPosition(WorldCorner);
		}
		B.KeepClear = Local.ExpandBy(FVector(bGate ? 10.0f : 60.0f, bGate ? 10.0f : 60.0f, 0.0f));
	}

	// ---- 자리 잡기 ----
	// 펜스 줄: 게이트 세트는 게이트와 같은 줄(X=0). 탈것 세트는 탈것 둘레 바로 안쪽 — 헬기는 날개폭 15m 라 줄이 9m 넘게 안으로 들어간다.
	const float Gap = bGate ? ExitDressing_GateGapHalfCm : ExitDressing_LaneHalfCm;
	const float FenceX = bGate ? 0.0f : FMath::Min(-100.0f, (B.KeepClear.IsValid ? B.KeepClear.Min.X : 0.0f) - 40.0f);
	// 문틀(기둥·가로대·EXIT 글자)과 차단봉: 펜스 줄에서 4.5m 안쪽. 타일 안쪽 끝은 넘지 않는다.
	const float GantryX = FMath::Max(FenceX - 450.0f, ExitDressing_TileInnerEdgeCm + 140.0f);
	// 컨테이너가 서는 쪽(+1 = 오른쪽). 반대쪽에 차단봉·감시탑(또는 텐트)·연료통. 시드로 좌우가 바뀐다.
	const float Side = Stream.RandRange(0, 1) == 0 ? 1.0f : -1.0f;
	const float BoomSide = -Side;
	const float LaneHalf = ExitDressing_LaneHalfCm;

	// 1) 철조망 펜스 줄(공장 팩 2m 판). 가운데(길·게이트)는 비우고 양옆으로 이어 붙인다. 판은 메시 +X 로 길어서 90도 돌려 Y 방향으로.
	UStaticMesh* FencePanel = B.Load(Actor->FencePanelMesh, TEXT("FencePanel"));
	if (!FencePanel)
		FencePanel = B.Load(Actor->FencePanelFallbackMesh, TEXT("FencePanelFallback"));
	if (FencePanel)
	{
		const float PanelLen = FencePanel->GetBounds().BoxExtent.X * 2.0f;
		for (const float S : { 1.0f, -1.0f })
			for (float Y0 = Gap; Y0 + PanelLen <= ExitDressing_FenceReachCm; Y0 += PanelLen)
				B.Place(FencePanel, FVector2D(FenceX, S * (Y0 + PanelLen * 0.5f)), 90.0f, true, false, false, true);
	}

	// 2) 문틀. 먼저 검문소 킷의 콘크리트 문틀(SM_PGExit_Gate: 기둥 둘 + 철골 보 + 빈 간판 + 경광등·투광등)을 세운다.
	//    킷이 없으면(임포트 전) 예전처럼 3m 널판 기둥 둘(TSL 팩) + 5.8m 차단봉 팔(공장 팩)을 가로대로 쓴다.
	//    어느 쪽이든 빨간 "EXIT" 글자는 UTextRenderComponent — 간판에 글자를 굽지 않고 게임이 그린다(글자·언어를 바꾸기 쉽다).
	UStaticMesh* Bar = B.Load(Actor->BoomArmMesh, TEXT("BoomArm"));
	float GantryGroundZ = 0.0f;
	const bool bGantryGround = B.GroundAt(FVector2D(GantryX, 0.0f), GantryGroundZ);
	UStaticMesh* KitGate = B.LoadKit(Actor->GateFrameMesh, TEXT("GateFrame"));
	if (KitGate && bGantryGround)
	{
		// 문틀은 길을 가로지르니 Place(길 겹침 거부)를 못 쓴다. 땅 높이만 보고 바로 놓고, 기둥 두 자리만 "찼음" 으로 적는다.
		// 피벗 = 두 기둥 사이 바닥 가운데, 메시 +X = 액터 +X(맵 바깥쪽). 간판은 -X(맵 안쪽)를 본다.
		// 충돌은 기둥·보의 UCX 상자뿐이라 보 밑(4.4m 아래)은 비어 있다 — 차·탱크가 지나간다. 그림자는 켠다(큰 구조물).
		B.Add(KitGate, FTransform(FRotator::ZeroRotator, FVector(GantryX, 0.0f, GantryGroundZ)), true, true);
		for (const float S : { 1.0f, -1.0f })
		{
			const FVector2D PillarAt(GantryX, S * ExitDressing_GatePillarYCm);
			B.Occupied.Add(FBox2D(PillarAt - FVector2D(ExitDressing_GateFootHalfCm), PillarAt + FVector2D(ExitDressing_GateFootHalfCm)));
		}
		// 글자: 간판 판 앞 10cm, 판 가운데 높이. 맵 안쪽(-X)에서 다가오는 사람이 읽게 180도(글자는 기본으로 +X 쪽에서 읽힌다).
		Actor->SignRelative = FTransform(FRotator(0.0f, 180.0f, 0.0f),
			FVector(GantryX + ExitDressing_GateSignFrontXCm - 10.0f, 0.0f, GantryGroundZ + ExitDressing_GateSignCentreZCm),
			FVector(ExitDressing_GateSignScale));
	}
	else
	{
		UStaticMesh* Post = B.Load(Actor->GatePostMesh, TEXT("GatePost"));
		float BarTopZ = GantryGroundZ + 300.0f;
		if (Post)
			for (const float S : { 1.0f, -1.0f })
				B.Place(Post, FVector2D(GantryX, S * (LaneHalf + 12.0f)), 0.0f, true, false, true, true);
		if (Bar && bGantryGround)
		{
			// 팔은 메시 +Y 로 뻗고 피벗이 한쪽 끝(Y≈-18cm)이다. 가운데가 길 가운데(Y=0)에 오게 피벗을 -바운드중심 만큼 옮긴다.
			const FBoxSphereBounds BarBounds = Bar->GetBounds();
			const float PivotZ = GantryGroundZ + 300.0f - (BarBounds.Origin.Z - BarBounds.BoxExtent.Z);
			B.Add(Bar, FTransform(FRotator::ZeroRotator, FVector(GantryX, -BarBounds.Origin.Y, PivotZ)), false, false);
			BarTopZ = PivotZ + BarBounds.Origin.Z + BarBounds.BoxExtent.Z;
		}
		// 글자: 가로대 위, 맵 안쪽(-X)에서 다가오는 사람이 읽게 180도. 글자는 기본으로 +X 쪽에서 읽도록 서 있다(전함 계기와 같다).
		Actor->SignRelative = FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(GantryX - 10.0f, 0.0f, BarTopZ + 65.0f));
	}
	Actor->bHasSign = true;

	// 3) 차단봉(붐 게이트) — 받침(공장 팩 52cm 블록) 위에 팔을 세워 "올라가 있는(열린)" 모양. 길은 막지 않는다.
	UStaticMesh* Pedestal = B.Load(Actor->BoomPedestalMesh, TEXT("BoomPedestal"));
	const FVector2D PedestalAt(GantryX + 130.0f, BoomSide * (LaneHalf + 70.0f));
	float PedestalGroundZ = 0.0f;
	if (Pedestal && B.Place(Pedestal, PedestalAt, 0.0f, true, false, true, true) && Bar && B.GroundAt(PedestalAt, PedestalGroundZ))
	{
		// 메시 +Y(팔 방향)를 위로 보내되 길 쪽으로 살짝(약 11도) 기울인다 — 똑바로 서면 기둥처럼 보인다.
		const FVector ArmDir = FVector(0.0f, -BoomSide * 0.2f, 1.0f).GetSafeNormal();
		const FQuat ArmRot = FQuat::FindBetweenNormals(FVector::RightVector, ArmDir);
		B.Add(Bar, FTransform(ArmRot, FVector(PedestalAt.X, PedestalAt.Y, PedestalGroundZ + 64.0f)), false, false);
	}

	// 킷 부품이 길을 볼 때의 회전: 킷 메시는 앞(창구·문)이 +X 다. 길 가운데(Y=0) 쪽을 보게 BoomSide 반대쪽으로 돌린다.
	const float FaceLaneYaw = BoomSide > 0.0f ? -90.0f : 90.0f;

	// 3-1) 차단봉 제어함(킷, 경광등 달린 철제 함) — 받침 바로 바깥, 앞면이 길을 본다. 차단봉을 "누가 올리나" 가 보이게.
	//      자리: 받침(길에서 0.7m) 바깥쪽에 붙이고, X 는 받침과 같게 — 연료통 자리(문틀 +2.4m)와 겹치지 않는다.
	//      사람 키 높이 쇠함이라 차가 뚫고 들어가지 않게 충돌은 막는다. 작아서 그림자는 끈다.
	if (UStaticMesh* Cabinet = B.LoadKit(Actor->ControlCabinetMesh, TEXT("ControlCabinet")))
	{
		const FBoxSphereBounds CabB = Cabinet->GetBounds();
		const float CabY = BoomSide * (LaneHalf + 110.0f + CabB.BoxExtent.X);
		if (!B.Place(Cabinet, FVector2D(PedestalAt.X, CabY), FaceLaneYaw, true, false, true, true))
			B.Place(Cabinet, FVector2D(PedestalAt.X - 90.0f, CabY), FaceLaneYaw, true, false, true, true);
	}

	// 3-2) 초소(킷, 경비 부스) — 차단봉 쪽 길가, 창구가 길을 본다. 검문소에 "지키는 사람 자리" 가 있어야 검문소로 읽힌다.
	//      문틀 바깥(+X, 받침 너머) → 안 되면 문틀 안쪽(-X) 순서로 본다. 감시탑·텐트(7)보다 먼저 자리를 잡는다.
	if (UStaticMesh* Booth = B.LoadKit(Actor->GuardBoothMesh, TEXT("GuardBooth")))
	{
		const FBoxSphereBounds BoothB = Booth->GetBounds();
		// 90도 돌리면 메시 Y 폭이 길 방향(X) 길이, 메시 X 깊이가 길에서 멀어지는 쪽(Y) 길이가 된다.
		const float BoothY = BoomSide * (LaneHalf + 150.0f + BoothB.BoxExtent.X);
		const float AlongHalf = BoothB.BoxExtent.Y;
		if (!B.Place(Booth, FVector2D(GantryX + 250.0f + AlongHalf, BoothY), FaceLaneYaw, true, true, true, true))
			B.Place(Booth, FVector2D(GantryX - 160.0f - AlongHalf, BoothY), FaceLaneYaw, true, true, true, true);
	}

	// 4) 입구 양옆 콘크리트 차단벽(공장 팩 2m). 시드로 2개 또는 4개. 살짝 비뚤게 — 자로 잰 듯 놓이면 가짜 같다.
	//    헬기 탈출구는 문틀이 타일 안쪽 끝 가까이 밀려서 차단벽이 옆 도로 타일까지 나간다 — 길가라 괜찮고, 그 타일 소품과 겹치면 월드 충돌 검사가 거른다.
	const float ApproachLimit = ExitDressing_TileInnerEdgeCm - 500.0f;
	UStaticMesh* Barrier = B.Load(Actor->ConcreteBarrierMesh, TEXT("ConcreteBarrier"));
	const int32 BarrierRows = 1 + Stream.RandRange(0, 1);
	for (int32 Row = 0; Row < BarrierRows; ++Row)
		for (const float S : { 1.0f, -1.0f })
			B.Place(Barrier, FVector2D(GantryX - 280.0f - Row * 260.0f, S * (LaneHalf + 60.0f)), Stream.FRandRange(-6.0f, 6.0f), true, false, true, true, ApproachLimit);

	// 5) 그 앞에 대전차 장애물(군용 팩 고슴도치) 한 쌍. 크기를 파일로 못 봤으니 Place 의 크기 검사에 맡긴다.
	UStaticMesh* Hedgehog = B.Load(Actor->HedgehogMesh, TEXT("Hedgehog"));
	if (Hedgehog && Hedgehog->GetBounds().SphereRadius <= 220.0f)
		for (const float S : { 1.0f, -1.0f })
			B.Place(Hedgehog, FVector2D(GantryX - 280.0f - BarrierRows * 260.0f - 40.0f, S * (LaneHalf + 170.0f)), Stream.FRandRange(0.0f, 360.0f), true, false, true, true, ApproachLimit);

	// 6) 컨테이너(공장 팩 12m). 메시 +Y 로 길어서 90도 돌려 길과 나란히, 펜스 줄에서 안쪽으로. 12m 라 타일 안쪽 끝을 5m 까지 넘도록 허용하되
	//    옆 타일 소품과 겹치면(월드 충돌 검사) 건너뛴다. 이쪽이 막히면 반대쪽을 한 번 더 본다.
	UStaticMesh* Container = B.Load(Actor->ContainerMesh, TEXT("Container"));
	if (Container)
	{
		const FBoxSphereBounds CB = Container->GetBounds();
		const FVector2D At(FenceX - 120.0f - CB.BoxExtent.Y, Side * (LaneHalf + 160.0f + CB.BoxExtent.X));
		if (!B.Place(Container, At, 90.0f, true, true, true, true, ExitDressing_TileInnerEdgeCm - 500.0f))
			B.Place(Container, FVector2D(At.X, -At.Y), 90.0f, true, true, true, true, ExitDressing_TileInnerEdgeCm - 500.0f);
	}

	// 7) 감시탑 또는 텐트 하나(군용 팩), 차단봉 쪽. 시드로 고르고, 없거나 안 들어가면 다른 것을 시도한다.
	{
		const bool bTowerFirst = Stream.RandRange(0, 1) == 0;
		const bool bTentA = Stream.RandRange(0, 1) == 0;
		auto TryBig = [&B, FenceX, GantryX, BoomSide, LaneHalf](UStaticMesh* Mesh, float YawOffset) -> bool
		{
			if (!Mesh)
				return false;
			const FBoxSphereBounds MB = Mesh->GetBounds();
			const float Y = BoomSide * (LaneHalf + 140.0f + FMath::Max(MB.BoxExtent.X, MB.BoxExtent.Y));
			// 펜스 줄 바로 안쪽 → 안 되면 문틀 쪽.
			return B.Place(Mesh, FVector2D(FenceX - 160.0f - MB.BoxExtent.X, Y), YawOffset, true, true, true, true)
				|| B.Place(Mesh, FVector2D(GantryX + 230.0f + MB.BoxExtent.X, Y), YawOffset, true, true, true, true);
		};
		UStaticMesh* Tower = B.Load(Actor->WatchTowerMesh, TEXT("WatchTower"));
		UStaticMesh* Tent = bTentA ? B.Load(Actor->TentMeshA, TEXT("TentA")) : B.Load(Actor->TentMeshB, TEXT("TentB"));
		// 텐트 입구 방향은 모르니 길 쪽(BoomSide 반대)을 보게 돌린다.
		const float TentYaw = BoomSide > 0.0f ? -90.0f : 90.0f;
		if (bTowerFirst)
		{
			if (!TryBig(Tower, 0.0f))
				TryBig(Tent, TentYaw);
		}
		else if (!TryBig(Tent, TentYaw))
			TryBig(Tower, 0.0f);
	}

	// 8) 투광등(공장 팩 6.4m 등). 펜스 줄 안쪽, 컨테이너 쪽 길가. 소품이라 충돌 없음, 높아서 그림자는 켠다.
	if (UStaticMesh* Lantern = B.Load(Actor->LanternMesh, TEXT("Lantern")))
		B.Place(Lantern, FVector2D(FenceX - 200.0f, Side * (LaneHalf + 110.0f)), Stream.RandRange(0, 1) == 0 ? 0.0f : 180.0f, false, true, true, true);

	// 8-1) 모래주머니 벽(킷, 3m x 1m). 펜스 줄 바로 안쪽에 — 검문소를 "지키는 자리" 로 보이게 하고, 걸어서 오는 사람에게 엄폐물이 된다.
	//      굽은 벽은 오목한 쪽이 맵 안쪽(-X, 지키는 사람 쪽)을 보게 180도. 곧은 벽은 펜스와 나란히(메시가 Y 로 길다).
	//      자리는 몇 군데를 차례로 보고 먼저 맞는 곳 하나만 — 컨테이너·탑이 이미 차지했으면 다음 자리.
	//      사람 허리 높이 둑이라 차를 막는다(충돌 켬). 그림자는 켠다 — 없으면 바닥에 떠 보인다.
	{
		UStaticMesh* SandbagCurve = B.LoadKit(Actor->SandbagCurveMesh, TEXT("SandbagCurve"));
		UStaticMesh* SandbagStraight = B.LoadKit(Actor->SandbagStraightMesh, TEXT("SandbagStraight"));
		auto PlaceWall = [&B, FenceX, GantryX, LaneHalf](UStaticMesh* Mesh, float WallSide, float Yaw) -> bool
		{
			if (!Mesh)
				return false;
			const FBoxSphereBounds WB = Mesh->GetBounds();
			const float Y = WallSide * (LaneHalf + 90.0f + WB.BoxExtent.Y);
			const float Xs[] = { FenceX - 70.0f - WB.BoxExtent.X, FenceX - 260.0f - WB.BoxExtent.X, GantryX - 150.0f - WB.BoxExtent.X };
			for (const float X : Xs)
				if (B.Place(Mesh, FVector2D(X, Y), Yaw, true, true, true, true))
					return true;
			return false;
		};
		PlaceWall(SandbagCurve, BoomSide, 180.0f);
		PlaceWall(SandbagStraight, Side, 0.0f);
	}

	// 9) 소품(충돌·그림자 없음): 드럼통 둘, 가스통, 팔레트, 장작 더미. 자리는 시드로 조금씩 흔든다.
	auto Jitter = [&Stream]() { return Stream.FRandRange(-30.0f, 30.0f); };
	if (UStaticMesh* Drum = B.Load(Actor->DrumMesh, TEXT("Drum")))
	{
		B.Place(Drum, FVector2D(GantryX - 120.0f + Jitter(), BoomSide * (LaneHalf + 100.0f) + Jitter()), Stream.FRandRange(0.0f, 360.0f), false, false, true, true);
		B.Place(Drum, FVector2D(GantryX - 190.0f + Jitter(), BoomSide * (LaneHalf + 150.0f) + Jitter()), Stream.FRandRange(0.0f, 360.0f), false, false, true, true);
	}
	if (UStaticMesh* GasBottle = B.Load(Actor->GasBottleMesh, TEXT("GasBottle")))
		B.Place(GasBottle, FVector2D(GantryX + 60.0f + Jitter(), BoomSide * (LaneHalf + 120.0f) + Jitter()), Stream.FRandRange(0.0f, 360.0f), false, false, true, true);
	if (UStaticMesh* Pallet = B.Load(Actor->PalletMesh, TEXT("Pallet")))
		B.Place(Pallet, FVector2D(FenceX - 320.0f + Jitter(), BoomSide * (LaneHalf + 120.0f) + Jitter()), Stream.FRandRange(-20.0f, 20.0f), false, false, true, true);
	if (UStaticMesh* WoodPile = B.Load(Actor->WoodPileMesh, TEXT("WoodPile")))
		B.Place(WoodPile, FVector2D(FenceX - 560.0f + Jitter(), BoomSide * (LaneHalf + 110.0f) + Jitter()), Stream.FRandRange(-20.0f, 20.0f), false, false, true, true);

	Actor->Pieces = MoveTemp(B.Pieces);
	Actor->FinishSpawning(ActorToWorld);

	// 문틀(EXIT 글자) 아래를 실제 탈출구로 만든다. 연료통 1개를 갖고 걸어서든 차로든 지나가면 바로 탈출.
	// 왜: 예전에는 문틀이 꾸미기일 뿐이고 진짜 탈출은 옆에 세운 차·헬기에 F 였다. EXIT 글자 밑을 지나가도 아무 일이 없어서
	//   "EXIT 자체가 작동을 안 한다" 로 보였다(사용자 9/22). 글자가 있는 곳이 탈출하는 곳이어야 한다.
	// 왜 연료통인가: 차·헬기·배 탈출구와 같은 조건 — 탈출 조건은 모두에게 공평하게(9/22). 검문소마다 연료통 하나를 놓는다(아래 10).
	// 왜 기다리지 않나(0초): 차는 멈추지 않고 지나간다. 상자는 길 방향으로 6m 두께 — 0.1초마다 묻는 사이에 시속 100km 차도 3m 밖에 못 간다.
	{
		// 9/22 두 번째 PIE: 문틀 밑 6m 줄로는 영역에 들어간 기록조차 없었다(옆으로 돌아 지나가도 안 잡힌다).
		// 그래서 문틀 앞 3m 부터 펜스 줄 너머 2m 까지, 길 양옆 3m 씩 더 — 검문소 안에 들어오면 잡힌다.
		const float ZoneMinX = GantryX - 300.0f;
		// 게이트 검문소는 펜스 문 너머 5m 까지 — 문을 열고 지나가서 서 있어도 잡히게(9/22 "문을 통과해서 가만히 있으면 나갈 수 있게").
		const float ZoneMaxX = FenceX + (bGate ? 500.0f : 200.0f);
		const float ZoneHalfX = FMath::Max((ZoneMaxX - ZoneMinX) * 0.5f, 300.0f);
		const FVector ZoneWorld = ActorToWorld.TransformPosition(FVector((ZoneMinX + ZoneMaxX) * 0.5f, 0.0f, bGantryGround ? GantryGroundZ : 0.0f));
		FActorSpawnParameters ZoneParams;
		ZoneParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ZoneParams.bDeferConstruction = true;
		if (APGExtractionZoneActor* Gate = World->SpawnActor<APGExtractionZoneActor>(APGExtractionZoneActor::StaticClass(),
			FTransform(ActorToWorld.GetRotation(), ZoneWorld), ZoneParams))
		{
			// 규칙(9/22 사용자): 탈것(차·헬기·배)이 있는 출구는 연료통, 탈것이 없는 출구(잠긴 펜스 문)는 열쇠.
			// 예전에는 문 영역이 늘 연료통을 요구해서, 바로 옆 펜스 영역은 "열쇠 필요", 문은 "연료통 필요" 로 서로 다른 말을 했다.
			// 열쇠는 쓰지 않는다(소모 안 함) — 일반 열쇠는 여러 문에 쓰는 물건이다. 연료통은 탈것에 넣으니 쓴다.
			if (bGate)
				Gate->Configure(EPGExtractionTrigger::Overlap, 5.0f, TEXT("Key_Common"), false, 0);
			else
				Gate->Configure(EPGExtractionTrigger::Overlap, 5.0f, TEXT("Fuel"), true, 0);
			Gate->SetHoldStill(true); // 9/22: 조건을 갖추고 멈춰 5초 버티면 탈출(움직이면 취소)
			Gate->SetZoneBox(FVector(0.0f, 0.0f, 250.0f), FVector(ZoneHalfX, LaneHalf + 300.0f, 300.0f));
			Gate->SetExitDisplayName(NSLOCTEXT("Extraction", "CheckpointExit", "검문소 출구"));
			Gate->SetPollOccupants(true);
			Gate->FinishSpawning(FTransform(ActorToWorld.GetRotation(), ZoneWorld));
			UE_LOG(LogPGObjects, Display, TEXT("PGExitDressing: exit %d gate zone %s at %s — hold still 5s with %s"),
				ExitIndex, *GetNameSafe(Gate), *ZoneWorld.ToCompactString(), bGate ? TEXT("Key_Common") : TEXT("Fuel"));
		}
	}

	// 10) 연료통 하나. 차·헬기·배 탈출구가 연료통을 요구하는데 맵에는 시작 지점 것 하나뿐이었다(사용자 9/22 "출구 근처에도 연료통 놔둬줘").
	//     차단봉 받침 옆, 길 밖. 연료통은 굴러다니는 물건이라(APGFloorItemActor::MakeLoose) 차에 치이면 날아가도 그대로 둔다.
	bool bFuel = false;
	{
		const FVector2D FuelSpots[] = { FVector2D(GantryX + 240.0f, BoomSide * (LaneHalf + 120.0f)), FVector2D(FenceX - 150.0f, BoomSide * (LaneHalf + 120.0f)) };
		for (const FVector2D& Spot : FuelSpots)
		{
			float SpotZ = 0.0f;
			if (!B.GroundAt(Spot, SpotZ))
				continue;
			const FVector FuelWorld = ActorToWorld.TransformPosition(FVector(Spot.X, Spot.Y, SpotZ + 30.0f));
			bFuel = IsValid(APGFloorItemActor::SpawnDrop(World, TEXT("Fuel"), 1, FTransform(FRotator(0.0f, Stream.FRandRange(0.0f, 360.0f), 0.0f), FuelWorld)));
			if (bFuel)
				break;
		}
	}

	UE_LOG(LogPGObjects, Display, TEXT("PGExitDressing: exit %d at (%.0f,%.0f) — %d pieces (missing: %s) kind=%s lane=%s skipped=%d fuel=%s class=%s"),
		ExitIndex, Ground.X, Ground.Y, Actor->Pieces.Num(),
		B.Missing.Num() > 0 ? *FString::Join(B.Missing, TEXT(", ")) : TEXT("none"),
		bGate ? TEXT("gate") : TEXT("vehicle"), *LaneAxis.ToCompactString(), B.Skipped, bFuel ? TEXT("yes") : TEXT("no"), *DressingClass->GetName());
	// 킷이 없어서 예전 팩 부품으로 대신한 것(또는 뺀 것). 경고로 찍어야 "왜 예전 모습이지?" 를 로그에서 바로 찾는다.
	if (B.KitFallbacks.Num() > 0)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGExitDressing: exit %d — exit kit assets missing (%s), pack-mesh fallback used. Run Tools/import_exit_kit.py or set the slots on %s"),
			ExitIndex, *FString::Join(B.KitFallbacks, TEXT(", ")), *DressingClass->GetName());
	}
	return Actor;
}

void APGExitDressingActor::BeginPlay()
{
	Super::BeginPlay();
	BuildFromPieces();
}

void APGExitDressingActor::OnRep_Pieces()
{
	BuildFromPieces();
}

void APGExitDressingActor::BuildFromPieces()
{
	// 클라는 BeginPlay 와 OnRep 이 둘 다 부를 수 있다. 목록이 아직 안 왔으면 기다리고, 한 번 지었으면 다시 안 짓는다.
	if (bBuilt || (Pieces.Num() == 0 && !bHasSign))
		return;
	bBuilt = true;
	for (const FPGExitDressingPiece& Piece : Pieces)
	{
		UStaticMesh* Mesh = Piece.Mesh.LoadSynchronous();
		if (!Mesh)
			continue;
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetStaticMesh(Mesh);
		Component->SetupAttachment(RootScene);
		Component->SetRelativeTransform(Piece.Relative);
		if (Piece.bCollide)
			Component->SetCollisionProfileName(TEXT("BlockAll"));
		else
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCastShadow(Piece.bShadow);
		// 길찾기 바닥은 WarZoneFootprintPreview 의 전용 NavigationFloor 가 맡는다. 부품마다 내비 다시 굽기를 시키지 않는다.
		Component->SetCanEverAffectNavigation(false);
		Component->RegisterComponent();
		Parts.Add(Component);
	}
	if (bHasSign)
	{
		Sign = NewObject<UTextRenderComponent>(this);
		Sign->SetMobility(EComponentMobility::Static);
		Sign->SetupAttachment(RootScene);
		Sign->SetRelativeTransform(SignRelative);
		Sign->SetText(SignText);
		Sign->SetHorizontalAlignment(EHTA_Center);
		Sign->SetVerticalAlignment(EVRTA_TextCenter);
		Sign->SetWorldSize(SignWorldSize);
		Sign->SetTextRenderColor(SignColor);
		Sign->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sign->SetCastShadow(false);
		Sign->SetCanEverAffectNavigation(false);
		Sign->RegisterComponent();
	}
}
