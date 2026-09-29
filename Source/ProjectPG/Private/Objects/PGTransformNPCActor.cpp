#include "Objects/PGTransformNPCActor.h"

#include "Common/PGVisualSettings.h"

#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#if WITH_EDITOR
#include "SkinnedAssetCompiler.h"
#endif
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGCarRevertComponent.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGObjectTypes.h"
#include "Particles/ParticleSystem.h"
#include "TimerManager.h"
#include "Vehicle/PGVehiclePawn.h"
#include "Vehicle/PGFlightKitComponent.h"
#include "Net/UnrealNetwork.h"

UClass* APGTransformNPCActor::GetSpawnClass()
{
	return UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().TransformNPCClass, APGTransformNPCActor::StaticClass());
}

APGTransformNPCActor::APGTransformNPCActor()
{
	VehicleClass = APGVehiclePawn::StaticClass();
	// 모델링 세션이 만든 여고생(VRoid → Blender → FBX, Tools/import_school_girl.py 로 가져옴).
	BodyMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/SK_SchoolGirl.SK_SchoolGirl")));
	IdleAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/A_SchoolGirl_Idle.A_SchoolGirl_Idle")));
	TransformAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/A_SchoolGirl_Transform.A_SchoolGirl_Transform")));
	DrinkAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/A_SchoolGirl_Drink.A_SchoolGirl_Drink")));
	PanelsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/SportsCarPanels/SK_SportsCarPanels.SK_SportsCarPanels")));
	PanelsAnim = TSoftObjectPtr<UAnimSequence>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/SportsCarPanels/A_SportsCarPanels_Assemble.A_SportsCarPanels_Assemble")));
	VehicleBodyMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/Materials/M_SportsCar_Body_Navy.M_SportsCar_Body_Navy")));
	// 겉모습 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 에셋 그대로).
	PlaceholderMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/VehicleVarietyPack/Skeletons/SK_SportsCar.SK_SportsCar")));
	ShieldMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Engine/BasicShapes/Sphere.Sphere")));
	ShieldMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/M_PGEmissiveSoft.M_PGEmissiveSoft")));
	HeldCanMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Props/FuelCan/SM_PGFuelCan_Red.SM_PGFuelCan_Red")));
	NoAnimDustFx = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld")));

	// 사람 크기 캡슐: F 를 겨눌 때 맞고(Visibility), 걸어서 통과하지 못한다.
	Capsule = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	Capsule->SetupAttachment(RootScene);
	Capsule->InitCapsuleSize(40.0f, 85.0f);
	Capsule->SetRelativeLocation(FVector(0.0f, 0.0f, 85.0f));
	Capsule->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	Capsule->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Capsule->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// 울타리: 사람 키 두 배쯤 되는 원통. 차·탱크(ECC_Vehicle)와 몬스터·잔해를 막는다.
	// 겨냥선(Visibility)과 카메라는 통과시킨다 — 안 그러면 F 를 겨눌 수도, 뒤에서 볼 수도 없다.
	Barrier = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Barrier"));
	Barrier->SetupAttachment(RootScene);
	Barrier->InitCapsuleSize(260.0f, 300.0f);
	Barrier->SetRelativeLocation(FVector(0.0f, 0.0f, 300.0f));
	Barrier->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Barrier->SetCollisionResponseToAllChannels(ECR_Block);
	Barrier->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Barrier->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 변신 연출이 코앞에서 벌어지는데 카메라가 이 액터의 어느 부품에라도 걸리면 화면이 확 당겨진다.
	// 그래서 이 액터는 카메라를 통과시킨다(9/20 PIE: "변신할 때 시야 줌이 확 된다").
	Barrier->SetCanEverAffectNavigation(false);
	Barrier->bHiddenInGame = true;

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(RootScene);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 외곽선 껍데기. Body 에 붙여 키 보정 배율을 같이 받는다.
	OutlineMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Characters/SchoolGirl/Toon/M_GirlOutline.M_GirlOutline")));
	BodyOutline = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("BodyOutline"));
	BodyOutline->SetupAttachment(Body);
	BodyOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyOutline->SetCastShadow(false); // 부풀린 껍데기가 제 그림자를 만들면 캐릭터가 지저분해진다
	BodyOutline->bReceivesDecals = false;

	Panels = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Panels"));
	Panels->SetupAttachment(RootScene);
	Panels->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Panels->SetVisibility(false);
	// 조각들이 배율 0 에서 시작해 튀어나오므로 움직이는 뼈로 경계를 재면 처음에 화면에서 잘린다. 완성된 차 크기 경계를 고정으로 쓴다.
	Panels->bComponentUseFixedSkelBounds = true;

	// 부모의 스태틱 메시 자리는 안 쓴다(겉모습은 Body).
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 거대 로봇·드래곤이 스타터 지역을 밟아도 변신 NPC 는 남는다(시작 동선이 끊기지 않게). 부스들과 달리 이건 보호한다.
	Tags.Add(TEXT("PGProtected"));
}

void APGTransformNPCActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGTransformNPCActor, bAlreadyFueled);
	DOREPLIFETIME(APGTransformNPCActor, bPlayRevertIntro);
}

void APGTransformNPCActor::BeginPlay()
{
	Super::BeginPlay();
	// 발밑 맞추기: 스포너는 땅 높이(Z=20)에 세우는데 길 위는 10cm 더 높다(도로면 Z=30).
	// 그래서 길 위에 서면 발이 박혀 보였다(9/20 PIE). 실제 표면을 다시 재서 올려놓는다.
	{
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGGirlGround), false, this);
		// 위로 100cm 만 올려서 잰다. 200cm 에서 재면 머리 높이 가지·소품을 먼저 맞아 그 위에 올라설 수 있다(9/22 공중 여고생).
		const FVector From = GetActorLocation() + FVector(0.0f, 0.0f, 100.0f);
		if (GetWorld()->LineTraceSingleByChannel(Ground, From, From - FVector(0.0f, 0.0f, 500.0f), ECC_Visibility, Params))
			SetActorLocation(FVector(GetActorLocation().X, GetActorLocation().Y, Ground.ImpactPoint.Z + 2.0f));
		UE_LOG(LogPGObjects, Display, TEXT("%s: feet at z=%.0f on %s/%s"), *GetName(), GetActorLocation().Z,
			*GetNameSafe(Ground.GetActor()), *GetNameSafe(Ground.GetComponent()));
	}
	if (USkeletalMesh* Mesh = BodyMesh.LoadSynchronous())
	{
		Body->SetSkeletalMeshAsset(Mesh);
		// 화면에서 사라졌다 나타났다 하던 문제: 애니가 뼈를 크게 움직이면 원래 경계 상자를 벗어나
		// "화면 밖"으로 판정된다. 경계를 넉넉히 잡아 둔다(9/20 PIE: 시야를 돌릴 때마다 깜빡임).
		Body->SetBoundsScale(4.0f);
		// 외곽선 껍데기: 같은 메시를 씌우고 Body 의 포즈를 그대로 따라간다.
		// 기본은 끔 — 앞면을 잘라내는 재질 노드가 이 빌드에서 안 먹어서 검은 껍데기가 캐릭터를 덮었다(9/20 PIE).
		// 재질이 고쳐지면 bUseOutline 을 켜면 된다.
		if (bUseOutline && IsValid(BodyOutline))
		{
			BodyOutline->SetSkeletalMeshAsset(Mesh);
			BodyOutline->SetLeaderPoseComponent(Body);
			BodyOutline->SetBoundsScale(4.0f);
			if (UMaterialInterface* Outline = OutlineMaterial.LoadSynchronous())
				for (int32 Index = 0; Index < Mesh->GetMaterials().Num(); ++Index)
					BodyOutline->SetMaterial(Index, Outline);
		}
#if WITH_EDITOR
		// 메시가 아직 구워지는 중이면 GetBounds() 가 제 크기를 모른다. 그 값으로 키를 맞추면 반대로 수십 배가 된다
		// (9/20 PIE: 에셋을 다시 가져온 직후 여고생이 40m 로 서 있었고, 컬링은 161cm 기준이라 깜빡였다).
		FSkinnedAssetCompilingManager::Get().FinishCompilation({ Mesh });
#endif
		// 키 맞추기: Blender 에서 cm/m 단위가 어긋나 1/100(1.6cm)로 들어온 적이 있다(9/20 가져오기 로그). 너무 작거나 크면 161cm 로 맞춘다.
		const float Height = Mesh->GetBounds().BoxExtent.Z * 2.0f;
		// 0.5cm 보다 작게 나오면 "단위가 틀린 것"이 아니라 "아직 크기를 모르는 것"이다. 그때는 건드리지 않는다(원래 크기가 맞을 확률이 높다).
		const bool bWrongUnit = Height > 0.5f && (Height < 100.0f || Height > 250.0f);
		if (bWrongUnit)
		{
			Body->SetRelativeScale3D(FVector(TargetHeightCm / Height));
			UE_LOG(LogPGObjects, Warning, TEXT("%s: school girl mesh is %.2f cm, scaling by %.2f to reach %.0f cm"),
				*GetName(), Height, TargetHeightCm / Height, TargetHeightCm);
		}
		else
		{
			UE_LOG(LogPGObjects, Display, TEXT("%s: school girl mesh height %.2f cm (no scaling)"), *GetName(), Height);
		}
		if (UAnimSequence* Idle = IdleAnim.LoadSynchronous())
		{
			Body->PlayAnimation(Idle, true);
			// 애니를 한 번 돌린 뒤 진짜 화면 크기를 다시 잰다(VerifyPlayedHeight 주석 참고).
			if (bPlayRevertIntro)
				BeginRevertIntro(); // 키 재측정은 연출이 끝난 뒤에 한다(변신 자세로 재면 엉뚱한 배율이 나온다)
			else
				GetWorldTimerManager().SetTimer(HeightCheckTimer, this, &APGTransformNPCActor::VerifyPlayedHeight, 0.15f, false);
			// 울타리는 플레이어가 생긴 뒤에 걸어야 한다(플레이어 캡슐에 "이 액터 무시"를 심는다).
			// 차에서 돌아오는 중이면 여기서 걸지 않는다 — 아직 차 조각이 분해되는 중인데 1초 만에
			// 방어막 반구가 떠 버리면 "차 위에 방어막"이 된다. 연출이 끝나는 EndRevertIntro 에서 건다
			// (사용자 9/20: "배리어 다시 생기면 자연스럽잖아" — 다시 생기는 시점이 사람으로 돌아온 순간이어야 한다).
			if (!bPlayRevertIntro)
			{
				FTimerHandle BarrierTimer;
				GetWorldTimerManager().SetTimer(BarrierTimer, this, &APGTransformNPCActor::SetupBarrier, 1.0f, false);
			}
			GetWorldTimerManager().SetTimer(FuelCheckTimer, this, &APGTransformNPCActor::EnsureFuelNearby, 8.0f, true, 8.0f);
		}
	}
	else if (USkeletalMesh* Car = PlaceholderMesh.IsNull() ? nullptr : PlaceholderMesh.LoadSynchronous())
	{
		// 여고생 모델이 오기 전 자리 표시: 변신 결과인 스포츠카를 세워 둔다. 캡슐도 차 크기로.
		Body->SetSkeletalMeshAsset(Car);
		Capsule->SetCapsuleSize(120.0f, 120.0f);
		Capsule->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	}
}

// 메시 크기가 맞아도 "애니메이션이 뼈를 키워 놓은" 경우가 있다.
// 9/20 PIE: SK_SchoolGirl 은 161cm 인데 A_SchoolGirl_Idle 의 root 뼈에 배율이 박혀 있어 재생하는 순간 수십 배가 됐다.
//
// 왜 경계 상자(Bounds)가 아니라 뼈 위치로 재나:
//   스켈레탈 메시의 경계 상자는 여유분이 붙어 있어서 실제 몸 크기와 다르다. 그걸 기준으로 맞췄더니
//   화면에서는 여전히 사람보다 컸다(사용자: "아직도 좀 큰 것 같은데"). 뼈는 거짓말을 하지 않는다.
// 목표 키도 고정값이 아니라 "플레이어 캐릭터와 같은 키"다(사용자 요구 9/20). 변신 애니는 이 배율 위에서
//   그대로 돌아가므로 차는 여전히 제 크기로 나온다.
// 플레이어만 통과시키는 방법: 채널로는 "사람"과 "몬스터"를 구분할 수 없다(둘 다 Pawn).
// 그래서 울타리를 다 막아 두고, 플레이어 캡슐 쪽에서 이 액터를 "움직일 때 무시"하게 한다.
void APGTransformNPCActor::SetupBarrier()
{
	if (!IsValid(Barrier))
		return;
	// 두 번 부르지 않는다. 아래에서 "Shield" 라는 고정 이름으로 컴포넌트를 만드는데,
	// 같은 이름이 이미 있으면 이름 충돌로 죽는다(이 프로젝트에서 승강기 난간으로 한 번 겪었다).
	if (IsValid(Shield))
		return;
	// 예전에는 콘크리트 방벽 6개를 둘러쳤는데, 판이 시작되고 1초 뒤에 생기다 보니
	// 그 자리에 있던 차를 튕겨냈다(9/20 PIE). 어차피 아래 방어막 반구가 "지키고 있다"를 보여 주므로
	// 물리적으로 막는 일은 보이지 않는 울타리(Barrier 캡슐)에 맡기고 방벽 프롭은 놓지 않는다.
	{
		// 콘크리트 블록만으로는 "보호받고 있다"가 안 읽힌다(맵에 널린 블록과 똑같이 생겼다).
		// 그 위에 빛나는 방어막 반구를 씌운다 — 눈에 바로 보이고, 변신할 때 같이 사라져 연출도 된다.
		if (UStaticMesh* Dome = ShieldMesh.IsNull() ? nullptr : ShieldMesh.LoadSynchronous())
		{
			Shield = NewObject<UStaticMeshComponent>(this, TEXT("Shield"));
			Shield->SetStaticMesh(Dome);
			Shield->SetupAttachment(RootScene);
			Shield->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
			Shield->SetRelativeScale3D(FVector(6.4f, 6.4f, 4.4f)); // 반지름 320cm, 높이 220cm 쯤의 반구형
			Shield->SetCollisionEnabled(ECollisionEnabled::NoCollision); // 막는 건 울타리 캡슐이 한다
			Shield->SetCastShadow(false);
			Shield->SetCanEverAffectNavigation(false);
			Shield->RegisterComponent();
			if (UMaterialInterface* Soft = ShieldMaterial.IsNull() ? nullptr : ShieldMaterial.LoadSynchronous())
			{
				UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Soft, this);
				// 가산 재질이라 어두울수록 투명하다. 옅은 하늘색으로 은은하게.
				Mid->SetScalarParameterValue(TEXT("Brightness"), ShieldBrightness);
				Shield->SetMaterial(0, Mid);
			}
			UE_LOG(LogPGObjects, Display, TEXT("%s: shield dome up"), *GetName());
		}
	}
	// 멀티(9/27): 한 번이 아니라 1초마다. 예전에는 판 시작 1초 뒤 "그때 있던" 캐릭터만 통과시켜서, 나중에 생긴 캐릭터
	//   (늦게 들어온 사람, 두 번째 사람, 클라이언트 쪽 캐릭터)는 울타리에 막혔다(9/27 PIE: "베리어에 막혀 접근도 안 된다").
	RefreshBarrierIgnores();
	GetWorldTimerManager().SetTimer(BarrierIgnoreTimer, this, &APGTransformNPCActor::RefreshBarrierIgnores, 1.0f, true);
}

void APGTransformNPCActor::RefreshBarrierIgnores()
{
	// 서버는 모든 사람의 캐릭터를, 클라이언트는 자기 캐릭터를(클라에는 자기 컨트롤러만 있다) — 양쪽이 같은 판정을 한다.
	// 플레이어만 통과시키는 이유는 SetupBarrier 위 주석(채널로는 사람과 몬스터를 못 가른다).
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		APawn* Pawn = IsValid(PC) ? PC->GetPawn() : nullptr;
		if (!IsValid(Pawn))
			continue;
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Pawn->GetRootComponent()))
			Root->IgnoreActorWhenMoving(this, true);
	}
}

void APGTransformNPCActor::EnsureFuelNearby()
{
	UWorld* World = GetWorld();
	// 멀티(9/27): 연료통(복제되는 게임 물건)은 서버만 놓는다. 클라이언트가 놓으면 그 사람 화면에만 있는 가짜 통이 생긴다.
	if (!IsValid(World) || !HasAuthority())
		return;
	// 이미 연료를 받았으면 그만 본다.
	if (bTransforming || bAlreadyFueled)
	{
		World->GetTimerManager().ClearTimer(FuelCheckTimer);
		return;
	}
	// 15m 안에 연료통이 있나. 차가 치고 지나가 사라졌으면 다시 놓는다(9/20 PIE: 차가 벽을 부수며 지나가 깔렸다).
	TArray<AActor*> Found;
	UGameplayStatics::GetAllActorsOfClass(World, APGFloorItemActor::StaticClass(), Found);
	for (const AActor* Actor : Found)
		if (const APGFloorItemActor* Item = Cast<APGFloorItemActor>(Actor);
			Item && Item->GetItemId() == TEXT("Fuel") && FVector::Dist(Item->GetActorLocation(), GetActorLocation()) < 1500.0f)
			return;
	// 빈 자리를 찾아 놓는다. 앞쪽 한 자리에 그냥 놓았더니 벽에 낀 적이 있다(9/20 PIE).
	// 둘레를 45도씩 돌며 "사람 무릎 높이 공이 들어갈 만한 곳 + 그 밑에 땅이 있는 곳"을 고른다.
	FVector Spot = FVector::ZeroVector;
	bool bFound = false;
	for (int32 Step = 0; Step < 8 && !bFound; ++Step)
	{
		const float Angle = GetActorRotation().Yaw + (Step % 2 == 0 ? 1.0f : -1.0f) * (Step / 2) * 45.0f;
		const FVector Try = GetActorLocation() + FRotator(0.0f, Angle, 0.0f).Vector() * 320.0f + FVector(0.0f, 0.0f, 60.0f);
		FCollisionQueryParams Params(SCENE_QUERY_STAT(PGFuelSpot), false, this);
		// 막힌 자리인가
		if (World->OverlapAnyTestByChannel(Try, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(45.0f), Params))
			continue;
		// 그 밑에 땅이 있나
		FHitResult Ground;
		if (!World->LineTraceSingleByChannel(Ground, Try, Try - FVector(0.0f, 0.0f, 400.0f), ECC_Visibility, Params))
			continue;
		Spot = Ground.ImpactPoint + FVector(0.0f, 0.0f, 20.0f);
		bFound = true;
	}
	if (!bFound)
	{
		UE_LOG(LogPGObjects, Warning, TEXT("%s: no clear spot for the fuel can, will try again"), *GetName());
		return;
	}
	APGFloorItemActor::SpawnDrop(this, TEXT("Fuel"), 1, FTransform(FRotator(0.0f, GetActorRotation().Yaw, 0.0f), Spot));
	UE_LOG(LogPGObjects, Display, TEXT("%s: fuel can was gone — put a new one at %s"), *GetName(), *Spot.ToCompactString());
}

void APGTransformNPCActor::VerifyPlayedHeight()
{
	if (!IsValid(Body) || !Body->GetSkeletalMeshAsset())
		return;
	// 배율을 1 로 되돌리고 다시 잰다. 애니마다 뼈 배율이 달라서(대기 애니는 100배가 박혀 있고 마시는 애니는 정상),
	// 이전 보정값 위에 또 보정하면 애니가 바뀔 때 1.6cm 로 쪼그라든다(9/20 PIE: "그냥 사라지는데").
	Body->SetRelativeScale3D(FVector::OneVector);
	Body->RefreshBoneTransforms();

	// 1) 지금 그려지는 키: 뼈 중 가장 높은 것의 발밑 기준 높이. 정수리는 뼈보다 조금 위라 6% 를 더한다.
	const FTransform ToLocal = Body->GetComponentTransform().Inverse();
	float TopZ = 0.0f;
	const int32 BoneCount = Body->GetNumBones();
	for (int32 Index = 0; Index < BoneCount; ++Index)
		TopZ = FMath::Max(TopZ, static_cast<float>(ToLocal.TransformPosition(Body->GetBoneTransform(Index).GetLocation()).Z));
	const float Drawn = TopZ * 1.06f;
	if (Drawn < KINDA_SMALL_NUMBER)
		return;

	// 2) 목표 키: 플레이어 캐릭터와 같게. 못 찾으면 설정값(161cm).
	float Target = TargetHeightCm;
	if (const APlayerController* PC = GetWorld()->GetFirstPlayerController())
		if (const ACharacter* Player = Cast<ACharacter>(PC->GetPawn()))
			if (const UCapsuleComponent* PlayerCapsule = Player->GetCapsuleComponent())
				Target = PlayerCapsule->GetScaledCapsuleHalfHeight() * 2.0f;

	const float Correction = Target / Drawn;
	if (FMath::IsNearlyEqual(Correction, 1.0f, 0.05f))
	{
		UE_LOG(LogPGObjects, Display, TEXT("%s: school girl draws %.0f cm, target %.0f cm — no scaling"), *GetName(), Drawn, Target);
		return;
	}
	Body->SetRelativeScale3D(FVector(Correction)); // 절대값으로 정한다(위에서 1 로 되돌렸다)
	// 캡슐(F 를 겨누는 몸)도 같이 맞춘다. 안 그러면 몸은 작은데 겨냥 판정만 크게 남는다.
	if (IsValid(Capsule))
	{
		const float Half = Target * 0.5f;
		Capsule->SetCapsuleSize(Capsule->GetUnscaledCapsuleRadius(), Half);
		Capsule->SetRelativeLocation(FVector(0.0f, 0.0f, Half));
	}
	UE_LOG(LogPGObjects, Warning,
		TEXT("%s: school girl drew %.0f cm (target %.0f cm) — scaling by %.4f. An animation bone is scaled; re-export with root scale 1."),
		*GetName(), Drawn, Target, Correction);
}

bool APGTransformNPCActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (!Super::CanInteractInternal(Interactor, OutReason))
		return false;
	// 변신 중에는 조용히 거절한다(문구 없음 — 빈 이유는 화면에 안 띄운다, PGInteractionComponent).
	// 왜(9/28 사용자 PIE: "변신 중 글자가 너무 늦게 떠오르는 것 같은데… 변신 순식간에 되는데 굳이 있을 필요 없을 것 같아"):
	//   변신이 금방 끝나서 문구가 뜰 즈음엔 이미 차가 되어 있었다. 늦게 뜬 안내가 오히려 헷갈렸다.
	if (bTransforming)
		return false;
	// 차에서 사람으로 돌아오는 연출 중에는 못 건다. 이미 연료를 받은 상태(bAlreadyFueled)라
	// F 가 바로 먹히는데, 그러면 거꾸로 돌던 애니 위에 변신 애니가 덮여 그림이 엉킨다.
	if (bPlayRevertIntro)
	{
		OutReason = NSLOCTEXT("TransformNPC", "Reverting", "돌아오는 중");
		return false;
	}
	// 이미 한 번 받았으면 또 달라고 하지 않는다(차에서 돌아온 여고생).
	if (!bAlreadyFueled && !UPGItemReceiverLibrary::HasItem(Interactor, RequiredItemId, 1))
	{
		OutReason = NSLOCTEXT("TransformNPC", "NeedFuel", "연료통이 필요합니다");
		return false;
	}
	return true;
}

FText APGTransformNPCActor::GetPromptInternal() const
{
	return NSLOCTEXT("TransformNPC", "Prompt", "연료통 건네기 (변신)");
}

// 마시는 동작이 끝나면 여기로 온다. bDrinking 이 켜져 있으므로 위의 "먼저 마시기" 분기를 건너뛰고 변신이 진행된다.
// 차 → 여고생: 변신 애니를 거꾸로 돌린다. 차가 소리 없이 사라지고 사람이 서 있으면 버그로 보인다
// (사용자 9/20: "여고생으로 다시 변신해서 돌아와 달라는 말이었지").
// 재생 속도를 -1 로 주고 마지막 프레임에서 시작하면 조립이 분해로 보인다.
// 차 → 여고생: 변신 애니를 거꾸로 돌린다. 차가 소리 없이 사라지고 사람이 서 있으면 버그로 보인다
// (사용자 9/20: "여고생으로 다시 변신해서 돌아와달라는 말이였지. 그리고 배리어 다시 생기면 자연스럽잖아").
// 어떻게: 재생 속도를 -1 로 주고 마지막 프레임에서 시작한다. 조립 애니가 그대로 분해로 보인다.
// 방어막은 따로 손대지 않는다 — BeginPlay 의 1초 타이머가 SetupBarrier 를 부르므로, 연출이 끝나는
// 무렵에 울타리와 반구가 저절로 다시 생긴다.
void APGTransformNPCActor::BeginRevertIntro()
{
	float Duration = TransformSeconds;
	if (UAnimSequence* Anim = TransformAnim.LoadSynchronous(); Anim && IsValid(Body) && Body->GetSkeletalMeshAsset())
	{
		Body->PlayAnimation(Anim, false);
		Body->SetPosition(Anim->GetPlayLength(), false);
		Body->SetPlayRate(-1.0f);
		Duration = FMath::Max(0.2f, Anim->GetPlayLength());
	}
	// 차 조각도 같이 분해된다. 이때 패널 메시를 붙여 줘야 한다 — 막 생긴 액터라 아직 비어 있다.
	if (USkeletalMesh* PanelMesh = PanelsMesh.LoadSynchronous(); PanelMesh && IsValid(Panels))
	{
		Panels->SetSkeletalMeshAsset(PanelMesh);
		Panels->SetRelativeScale3D(FVector::OneVector); // 차 조각은 진짜 차 크기 그대로
		if (UMaterialInterface* Navy = VehicleBodyMaterial.LoadSynchronous())
			Panels->SetMaterialByName(TEXT("M_SportsCar_Body"), Navy);
		Panels->SetVisibility(true);
		if (UAnimSequence* Assemble = PanelsAnim.LoadSynchronous())
		{
			Panels->PlayAnimation(Assemble, false);
			Panels->SetPosition(Assemble->GetPlayLength(), false);
			Panels->SetPlayRate(-1.0f);
			Duration = FMath::Max(Duration, Assemble->GetPlayLength());
		}
	}
	GetWorldTimerManager().SetTimer(RevertIntroTimer, this, &APGTransformNPCActor::EndRevertIntro, Duration, false);
	UE_LOG(LogPGObjects, Display, TEXT("%s: turning back from the car (%.1fs)"), *GetName(), Duration);
}

// 연출이 끝나면 차 조각을 치우고 평소 대기 자세로 돌아간다.
void APGTransformNPCActor::EndRevertIntro()
{
	bPlayRevertIntro = false;
	if (IsValid(Panels))
	{
		Panels->SetPlayRate(1.0f);
		Panels->SetVisibility(false);
	}
	if (IsValid(Body))
	{
		Body->SetPlayRate(1.0f);
		if (UAnimSequence* Idle = IdleAnim.LoadSynchronous())
			Body->PlayAnimation(Idle, true);
	}
	// 자세가 바뀌었으니 화면에 보이는 키를 다시 잰다.
	GetWorldTimerManager().SetTimer(HeightCheckTimer, this, &APGTransformNPCActor::VerifyPlayedHeight, 0.15f, false);
	// 사람으로 다 돌아온 지금 방어막을 세운다(BeginPlay 는 이 경우 일부러 걸지 않았다).
	SetupBarrier();
}

namespace
{
	// 후보 이름 중 이 메시에 실제로 있는 첫 번째 뼈(또는 소켓)를 고른다.
	FName FindFirstBone(const USkeletalMeshComponent& Mesh, const TArray<FName>& Candidates)
	{
		for (const FName& Name : Candidates)
			if (Mesh.GetBoneIndex(Name) != INDEX_NONE || Mesh.DoesSocketExist(Name))
				return Name;
		return NAME_None;
	}
}

// 마시는 동안 두 손에 연료통을 쥐여 준다.
//
// 왜: 애니메이션은 "두 손으로 받아 마시는" 동작인데 손이 비어 있어서, 허공에 대고 마시는 것으로 보였다
//   (9/20 PIE: "허공에 뭘 들고 마시는 거야").
// 왜 눈대중이 아니라 실측인가 — 9/20 헤드리스로 A_SchoolGirl_Drink 의 뼈를 직접 재서 얻은 값:
//   - 손 뼈 이름이 프로젝트 표준(hand_r)이 아니라 **VRoid 이름 `J_Bip_R_Hand`** 였다. 예전 후보 목록에는
//     그 이름이 없어서 통이 늘 메시 원점(발밑)에 붙었다. 그래서 VRoid 이름을 맨 앞에 둔다.
//   - 통 크기 38.8 x 18.4 x 47.0 cm, 피벗은 바닥 가운데. 두 손 간격은 받는 순간 17.2cm, 마실 때 13.7cm.
//     통 두께(18.4)가 손 간격과 거의 같다 — 예전처럼 배율 0.6 으로 줄이면 손이 허공을 쥔 것처럼 보인다.
// 어떻게: 받는 순간의 두 손 가운데에 통 무게중심을 놓고, 통의 얇은 축(Y)을 두 손을 잇는 선에 맞춘다.
//   그 상대 위치 하나면 나머지 구간은 손이 알아서 한다 — 20~60프레임에 손목이 71도 기울며 통을 입으로
//   가져가기 때문이다(계산: 45프레임에 주둥이가 입에서 10.3cm. 통 바닥을 손에 맞추면 30.8cm 로 멀어진다).
// 좌표를 전부 컴포넌트 공간으로 재는 이유: 월드로 재면 VerifyPlayedHeight 가 넣은 키 보정 배율이
//   한 번 더 곱해져 통 크기와 위치가 같이 틀어진다.
void APGTransformNPCActor::AttachFuelCan()
{
	if (IsValid(HeldCan) || !IsValid(Body) || !Body->GetSkeletalMeshAsset())
		return;
	UStaticMesh* Can = HeldCanMesh.IsNull() ? nullptr : HeldCanMesh.LoadSynchronous();
	if (!Can)
		return;

	const FName RightBone = FindFirstBone(*Body,
		{ TEXT("J_Bip_R_Hand"), TEXT("hand_r"), TEXT("Hand_R"), TEXT("RightHand"), TEXT("mixamorig:RightHand") });
	const FName LeftBone = FindFirstBone(*Body,
		{ TEXT("J_Bip_L_Hand"), TEXT("hand_l"), TEXT("Hand_L"), TEXT("LeftHand"), TEXT("mixamorig:LeftHand") });
	if (RightBone.IsNone())
	{
		// 붙일 손을 못 찾으면 아예 안 붙인다. 발밑에 통이 떠 있는 편이 더 이상해 보인다.
		UE_LOG(LogPGObjects, Warning, TEXT("%s: no hand bone on %s — not giving her a fuel can"),
			*GetName(), *GetNameSafe(Body->GetSkeletalMeshAsset()));
		return;
	}

	// 지금 자세를 실제로 읽는다(마시기 21프레임 무렵 = 두 손이 통을 받는 순간).
	Body->RefreshBoneTransforms();
	const FTransform RightHand = Body->GetSocketTransform(RightBone, RTS_Component);
	const FTransform LeftHand = LeftBone.IsNone() ? RightHand : Body->GetSocketTransform(LeftBone, RTS_Component);
	const FVector Middle = (RightHand.GetLocation() + LeftHand.GetLocation()) * 0.5f;
	const FVector Across = RightHand.GetLocation() - LeftHand.GetLocation();
	const float Gap = Across.Size();

	// 통 두께를 두 손 간격에 맞춘다. 통이 더 두꺼우면 손이 통에 파묻히고, 얇으면 허공을 쥔 것처럼 보인다.
	const FVector CanSize = Can->GetBoundingBox().GetSize();
	const float Scale = (Gap > 1.0f && CanSize.Y > 1.0f) ? FMath::Clamp(Gap / CanSize.Y, 0.5f, 1.2f) : 1.0f;

	// 세우는 방향: 통의 얇은 축(Y)을 두 손을 잇는 선에, 통의 위(Z)를 하늘로.
	FVector Sideways = Across.GetSafeNormal2D();
	if (Sideways.IsNearlyZero())
		Sideways = FVector::RightVector;
	const FQuat Upright = FRotationMatrix::MakeFromYZ(Sideways, FVector::UpVector).ToQuat();
	// 피벗이 바닥이라 높이의 절반을 빼야 통 가운데가 두 손 가운데에 온다. FuelCanDropCm 은 눈으로 맞출 여유.
	const FVector Where = Middle - FVector(0.0f, 0.0f, CanSize.Z * Scale * 0.5f + FuelCanDropCm);

	HeldCan = NewObject<UStaticMeshComponent>(this, TEXT("HeldFuelCan"));
	HeldCan->SetStaticMesh(Can);
	HeldCan->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	HeldCan->SetCanEverAffectNavigation(false);
	HeldCan->SetupAttachment(Body, RightBone);
	HeldCan->RegisterComponent();
	// 붙인 뒤에 정한다 — 붙은 컴포넌트의 상대 트랜스폼은 "붙은 뼈 기준"이다.
	HeldCan->SetRelativeTransform(FTransform(Upright, Where, FVector(Scale)).GetRelativeTransform(RightHand));
	UE_LOG(LogPGObjects, Display,
		TEXT("%s: holding a fuel can (bone=%s/%s, hands %.1fcm apart, can %.0fx%.0fx%.0fcm scaled %.2f)"),
		*GetName(), *RightBone.ToString(), *LeftBone.ToString(), Gap, CanSize.X, CanSize.Y, CanSize.Z, Scale);
}

void APGTransformNPCActor::DetachFuelCan()
{
	// 아직 안 붙었으면 붙지도 않게 한다. 변신이 마시기 중간에 끼어들면 통만 뒤늦게 생긴다.
	GetWorldTimerManager().ClearTimer(FuelCanTimer);
	if (!IsValid(HeldCan))
		return;
	HeldCan->DestroyComponent();
	HeldCan = nullptr;
}

// 마시는 동작이 "끝나면" 여기로 온다. 통을 붙이는 자리가 아니다 — 그건 AttachFuelCan 타이머가 한다.
void APGTransformNPCActor::FinishDrink()
{
	HandleInteract(nullptr);
}

void APGTransformNPCActor::HandleInteract(APawn* Interactor)
{
	if (!HasAuthority() || (bTransforming && !bDrinking))
		return;
	// 먼저 건네받는다. 인벤토리에서 못 빼면(동시에 다른 데서 썼다 등) 변신하지 않는다.
	// 마시는 동작이 끝나 스스로 다시 불린 것이면(bDrinking) 연료는 이미 받았다. 또 빼앗지 않는다.
	if (!bAlreadyFueled && !bDrinking && !UPGItemReceiverLibrary::ConsumeItem(Interactor, RequiredItemId, 1))
		return;
	bTransforming = true;
	// 연료통을 받으면 먼저 마신다. 그 동작이 끝나야 변신이 시작된다(사용자 9/20: 건네자마자 변신하면 급작스럽다).
	// 마시는 동작이 없으면(에셋 미도착) 예전처럼 바로 변신한다.
	if (!bDrinking)
	{
		if (UAnimSequence* Drink = DrinkAnim.LoadSynchronous(); Drink && IsValid(Body) && Body->GetSkeletalMeshAsset())
		{
			const float Wait = FMath::Max(0.2f, Drink->GetPlayLength());
			MulticastPlayDrink(); // 연출은 모두의 화면에서(서버도 여기서 같이 튼다 — bDrinking 도 거기서 켜진다)
			GetWorldTimerManager().SetTimer(DrinkTimer, this, &APGTransformNPCActor::FinishDrink, Wait, false); // 판정 타이머는 서버만
			UE_LOG(LogPGObjects, Display, TEXT("%s: drinking (%.1fs), then transforming"), *GetName(), Wait);
			return;
		}
	}
	const float Duration = GetTransformDuration();
	MulticastPlayTransform();
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, this, &APGTransformNPCActor::FinishTransform, Duration, false); // 차 만들기는 서버만
	UE_LOG(LogPGObjects, Display, TEXT("%s: %s handed over %s, transforming (%.1fs)"), *GetName(), *GetNameSafe(Interactor), *RequiredItemId.ToString(), Duration);
}

float APGTransformNPCActor::GetTransformDuration() const
{
	float Duration = TransformSeconds;
	if (const UAnimSequence* Anim = TransformAnim.LoadSynchronous(); Anim && IsValid(Body) && Body->GetSkeletalMeshAsset())
		Duration = FMath::Max(0.1f, Anim->GetPlayLength());
	if (!PanelsMesh.IsNull())
		if (const UAnimSequence* Assemble = PanelsAnim.LoadSynchronous())
			Duration = FMath::Max(Duration, Assemble->GetPlayLength());
	return Duration;
}

void APGTransformNPCActor::MulticastPlayDrink_Implementation()
{
	UAnimSequence* Drink = DrinkAnim.LoadSynchronous();
	if (!Drink || !IsValid(Body) || !Body->GetSkeletalMeshAsset())
		return;
	bTransforming = true; // 클라이언트에서도 변신 중에는 F 를 조용히 거절
	bDrinking = true;
	Body->PlayAnimation(Drink, false);
	// 애니가 바뀌면 크기가 달라질 수 있다. 한 틱 뒤 다시 재서 맞춘다.
	GetWorldTimerManager().SetTimer(HeightCheckTimer, this, &APGTransformNPCActor::VerifyPlayedHeight, 0.1f, false);
	const float Wait = FMath::Max(0.2f, Drink->GetPlayLength());
	// 통은 "받는 순간"(21프레임 = 0.7초)에 손에 생긴다. 0~20프레임은 아직 두 손을 내미는 중이라
	// 그전에 붙이면 통이 팔을 따라다니는 것으로 보이고, 무엇보다 그 자세를 재야 쥐는 위치가 맞는다.
	const float Receive = FMath::Clamp(FuelCanReceiveSeconds, 0.05f, Wait * 0.5f);
	GetWorldTimerManager().SetTimer(FuelCanTimer, this, &APGTransformNPCActor::AttachFuelCan, Receive, false);
}

void APGTransformNPCActor::MulticastPlayTransform_Implementation()
{
	bTransforming = true;
	// 울타리를 푼다. 변신이 시작되면 더는 지킬 것이 없고, 차가 울타리 안에서 태어나면 갇힌다(사용자 9/20).
	GetWorldTimerManager().ClearTimer(BarrierIgnoreTimer);
	if (IsValid(Barrier))
		Barrier->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (HasAuthority()) // 딸린 액터는 서버가 치운다(복제되어 모두에게서 사라진다)
	{
		for (AActor* Prop : BarrierProps)
			if (IsValid(Prop))
				Prop->Destroy();
		BarrierProps.Reset();
	}
	if (IsValid(Shield))
	{
		Shield->DestroyComponent();
		Shield = nullptr;
	}
	DetachFuelCan(); // 차가 되는데 손에 통이 남아 있으면 안 된다
	bool bHasAnim = false;
	// 여고생은 웅크려 운전석 쪽으로 작아지고,
	if (UAnimSequence* Anim = TransformAnim.LoadSynchronous(); Anim && Body->GetSkeletalMeshAsset())
	{
		Body->PlayAnimation(Anim, false);
		bHasAnim = true;
	}
	// 같은 자리에서 차 조각이 튀어나와 조립된다. 둘은 같은 2초짜리라 같이 시작하면 맞물린다.
	if (USkeletalMesh* PanelMesh = PanelsMesh.LoadSynchronous())
	{
		Panels->SetSkeletalMeshAsset(PanelMesh);
		// 여고생 키를 보정했더라도 차 조각은 진짜 차 크기 그대로(배율 1)여야 바뀌는 순간 어긋나지 않는다.
		Panels->SetRelativeScale3D(FVector::OneVector);
		if (UMaterialInterface* Navy = VehicleBodyMaterial.LoadSynchronous())
			Panels->SetMaterialByName(TEXT("M_SportsCar_Body"), Navy);
		Panels->SetVisibility(true);
		if (UAnimSequence* Assemble = PanelsAnim.LoadSynchronous())
		{
			Panels->PlayAnimation(Assemble, false);
			bHasAnim = true;
			// 38프레임(1.27초, 착지 바운스)에 전조등·후미등이 켜진다(모델링 README: 렌즈 재질 Emissive Intensity 0 → 20).
			// 9/20 애니 재작업으로 착지가 48 → 38 프레임으로 당겨졌다(펼쳐지는 방식으로 바뀜).
			TWeakObjectPtr<USkeletalMeshComponent> WeakPanels(Panels);
			FTimerHandle LightsHandle;
			GetWorldTimerManager().SetTimer(LightsHandle, FTimerDelegate::CreateLambda([WeakPanels]()
			{
				USkeletalMeshComponent* P = WeakPanels.Get();
				if (!IsValid(P))
					return;
				for (const TCHAR* Slot : { TEXT("M_SportsCarPanels_Headlight"), TEXT("M_SportsCarPanels_Taillight") })
				{
					const int32 Index = P->GetMaterialIndex(Slot);
					if (Index != INDEX_NONE)
						if (UMaterialInstanceDynamic* Lens = P->CreateDynamicMaterialInstance(Index))
							Lens->SetScalarParameterValue(TEXT("Emissive Intensity"), 20.0f);
				}
			}), 38.0f / 30.0f, false);
		}
	}
	// 캡슐은 이제 차 크기(조립되는 동안 사람이 차 자리로 걸어 들어오지 않게).
	Capsule->SetCapsuleSize(120.0f, 120.0f);
	Capsule->SetRelativeLocation(FVector(0.0f, 0.0f, 120.0f));
	// 애니메이션이 없으면 먼지 한 번으로 "무언가 일어났다"를 보여 준다(잔해와 같은 이펙트).
	if (!bHasAnim)
	{
		if (UParticleSystem* Dust = NoAnimDustFx.IsNull() ? nullptr : NoAnimDustFx.LoadSynchronous())
			UGameplayStatics::SpawnEmitterAtLocation(GetWorld(), Dust, GetActorLocation(), FRotator::ZeroRotator, FVector(1.2f), true, EPSCPoolMethod::AutoRelease);
	}
}

void APGTransformNPCActor::FinishTransform()
{
	UWorld* World = GetWorld();
	if (!IsValid(World))
		return;
	// 차는 조립된 조각과 같은 자리·방향(조각의 쉬는 자세 = 진짜 차). 바퀴 서스펜션이 자리 잡도록 아주 조금만 띄운다.
	// 조립 애니가 없을 때(자리 표시)는 예전처럼 80cm 위에서 떨어뜨린다.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	const float Lift = Panels->IsVisible() ? 5.0f : 80.0f;
	const FTransform SpawnTransform(FRotator(0.0f, GetActorRotation().Yaw, 0.0f), GetActorLocation() + FVector(0.0f, 0.0f, Lift));
	// 차를 놓기 전에 여고생 쪽 충돌을 전부 끈다.
	//
	// 왜: 차는 여고생이 서 있던 바로 그 자리에 생긴다. 그런데 이 액터에는 방어막 캡슐(Barrier)과 몸 캡슐이
	//   아직 살아 있어서, 차가 그 안에 겹친 채로 태어난다. Chaos 는 겹친 물체를 밀어내려고 큰 힘을 주고,
	//   그래서 차가 하늘로 솟구쳤다(9/20 PIE: "스폰될 때 변신자동차 베리어 쪽에 스폰돼서 날아가나 보네").
	//   이 액터는 몇 줄 아래에서 어차피 Destroy() 되므로 여기서 충돌을 꺼도 잃는 것이 없다.
	for (UActorComponent* Component : GetComponents())
		if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	// 방어막에 딸린 액터들도 같이 치운다(차가 그 사이에 끼면 똑같이 튕긴다).
	for (AActor* Prop : BarrierProps)
		if (IsValid(Prop))
			Prop->Destroy();
	BarrierProps.Reset();
	// 탈것 칸이 C++ 기본 차면 설정의 차 블루프린트(ProjectPG Visuals > Vehicle Class)를 쓴다. 다른 탈것을 골라 두었으면 그대로.
	UClass* CarClass = VehicleClass.Get() == APGVehiclePawn::StaticClass() ? UPGVisualSettings::VehicleSpawnClass() : VehicleClass.Get();
	APawn* Vehicle = IsValid(CarClass) ? World->SpawnActor<APawn>(CarClass, SpawnTransform, Params) : nullptr;
	// 변신해서 나온 차만 로켓 부스터 비행(마우스 오른쪽). 다른 차에는 없다.
	if (IsValid(Vehicle))
	{
		UPGFlightKitComponent* Kit = NewObject<UPGFlightKitComponent>(Vehicle, UPGFlightKitComponent::GetSpawnClass(), TEXT("FlightKit"));
		Kit->RegisterComponent();
		// 한참 버려져 있으면 도로 여고생으로. 그래야 차를 어디 두고 와도 시작 동선이 안 끊긴다.
		UPGCarRevertComponent* Revert = NewObject<UPGCarRevertComponent>(Vehicle, TEXT("CarRevert"));
		Revert->RegisterComponent();
	}
	// 변신 순간 3인칭 카메라가 바로 앞에 생긴 차에 걸려 확 당겨졌다(9/20 PIE: "시야가 확 줌 된다").
	// 차는 카메라 채널을 무시하게 한다 — 타면 탈것 카메라가 따로 쓰이므로 잃는 것이 없다.
	if (IsValid(Vehicle))
		for (UActorComponent* Component : Vehicle->GetComponents())
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Component))
				Primitive->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	// 변신해서 나온 차만 남색. 차체 슬롯 하나만 바꾼다.
	// 멀티(9/27): 우리 차(APGVehiclePawn)는 SetBodyPaint 로 칠한다 — 복제되어 클라이언트 차도 남색·카메라 통과가 된다.
	//   직접 칠하면 서버 화면에만 칠해져 클라에서는 원래 빨간 차였다("내가 알던 변신카가 아니다").
	if (IsValid(Vehicle))
		if (UMaterialInterface* Navy = VehicleBodyMaterial.LoadSynchronous())
		{
			if (APGVehiclePawn* Car = Cast<APGVehiclePawn>(Vehicle))
				Car->SetBodyPaint(Navy, VehicleBodySlot);
			else if (USkeletalMeshComponent* VehicleMesh = Vehicle->FindComponentByClass<USkeletalMeshComponent>())
				VehicleMesh->SetMaterialByName(VehicleBodySlot, Navy);
		}
	UE_LOG(LogPGObjects, Display, TEXT("%s: transformed into %s"), *GetName(), *GetNameSafe(Vehicle));
	Destroy();
}
