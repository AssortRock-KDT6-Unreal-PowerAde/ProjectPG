#include "Flow/PGFlowStage.h"
#include "Flow/PGFlowStageSet.h"

#include "Animation/AnimSequence.h"
#include "Animation/SkeletalMeshActor.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PointLight.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "UObject/UnrealType.h"

// 유니티 빌드에서 다른 cpp 의 상수와 이름이 겹치지 않게 파일 고유 이름 공간 + Stage 접두어를 쓴다.
namespace PGFlowStageLocal
{
	// ---- 에셋: 무엇을 놓나는 데이터 에셋(DA_PGFlowStage, 없으면 원래 코드 에셋)이 정한다(9/23 블루프린트 분리) ----
	// 어디에 몇 개, 얼마나 크게는 아래 코드가 그대로 정한다. 원래 경로와 설명은 UPGFlowStageSet 생성자에 옮겨 두었다.
	const UPGFlowStageSet& StageSet()
	{
		const UPGFlowStageSet* Set = UPGFlowStageSet::GetActive();
		return Set ? *Set : *GetDefault<UPGFlowStageSet>();
	}

	template <typename T>
	FString StagePath(const TSoftObjectPtr<T>& Slot)
	{
		return Slot.ToSoftObjectPath().ToString();
	}

	// 목록 칸(풀·소나무)은 차례로 돌려 쓴다. 비어 있으면 빈 경로 = 그 조각을 빼고 짓는다.
	template <typename T>
	FString StagePathAt(const TArray<TSoftObjectPtr<T>>& Slots, int32 Index)
	{
		return Slots.IsEmpty() ? FString() : StagePath(Slots[Index % Slots.Num()]);
	}

	// 타이틀 영상. 파일이 생기면 3D 무대 대신 이걸 깐다.
	const TCHAR* const StageTitleVideoRelative = TEXT("Movies/PG_TitleLoop.mp4");

	// ---- 에셋 읽기: 없으면 경고 한 줄 남기고 nullptr ----
	template <typename T>
	T* LoadStageAsset(const TCHAR* ObjectPath)
	{
		// 데이터 에셋에서 칸을 비웠으면 조용히 뺀다(일부러 뺀 것이다).
		if (!ObjectPath || !*ObjectPath)
			return nullptr;
		const FString Package = FPackageName::ObjectPathToPackageName(FString(ObjectPath));
		if (!FPackageName::DoesPackageExist(Package))
		{
			UE_LOG(LogTemp, Warning, TEXT("PGFlowStage: %s not on disk — skipped"), ObjectPath);
			return nullptr;
		}
		T* Asset = LoadObject<T>(nullptr, ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Asset)
			UE_LOG(LogTemp, Warning, TEXT("PGFlowStage: %s exists but is not a %s — skipped"), ObjectPath, *T::StaticClass()->GetName());
		return Asset;
	}

	// 메시 크기를 어떻게 맞추나. 팩마다 원본 크기가 제각각이라(소나무가 5m 인 팩도 20m 인 팩도 있다) "원하는 크기"로 맞춘다.
	enum class EStageFit : uint8
	{
		Scale,  // Size 를 그대로 배율로
		Height, // 높이가 Size(cm)가 되게
		Length, // 가로(긴 쪽)가 Size(cm)가 되게
	};

	AStaticMeshActor* PlaceMesh(UWorld* World, const TCHAR* Path, const FVector& Location, float Yaw, EStageFit Fit, float Size,
		UMaterialInterface* Material = nullptr)
	{
		UStaticMesh* Mesh = LoadStageAsset<UStaticMesh>(Path);
		if (!Mesh)
			return nullptr;
		const FBox Box = Mesh->GetBoundingBox();
		const FVector Extent = Box.GetSize();
		float Scale = Size;
		if (Fit == EStageFit::Height)
			Scale = static_cast<float>(Size / FMath::Max(Extent.Z, 1.0));
		else if (Fit == EStageFit::Length)
			Scale = static_cast<float>(Size / FMath::Max(FMath::Max(Extent.X, Extent.Y), 1.0));
		// 피벗이 메시 가운데인 팩이 있다. 밑면이 땅(Location.Z)에 닿게 올린다.
		const FVector Grounded = Location - FVector(0.0f, 0.0f, Box.Min.Z * Scale);
		const FTransform Transform(FRotator(0.0f, Yaw, 0.0f), Grounded, FVector(Scale));

		// 지연 스폰: 등록 전에 Movable 로 바꿔야 "정적 메시에 런타임에 메시를 바꿨다" 경고 없이 깔끔하다. 이 레벨엔 구운 빛이 없어 Movable 이 맞다.
		AStaticMeshActor* Actor = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Actor)
			return nullptr;
		UStaticMeshComponent* Component = Actor->GetStaticMeshComponent();
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetStaticMesh(Mesh);
		// 보기만 하는 무대다. 충돌을 끄면 관전 폰이 원점에 생길 때 밀려나지 않고, 물리 비용도 없다.
		Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Component->SetCanEverAffectNavigation(false);
		if (Material)
			Component->SetMaterial(0, Material);
		Actor->FinishSpawning(Transform);
		return Actor;
	}

	template <typename T>
	bool LevelHas(UWorld* World)
	{
		TActorIterator<T> It(World);
		return static_cast<bool>(It);
	}

	// 분위기 한 벌. 화면마다 값만 다르다.
	struct FStageMood
	{
		FRotator SunRotation;
		FLinearColor SunColor;
		float SunLux = 5.0f;
		float SkyIntensity = 1.0f;
		FLinearColor FogColor;
		float FogDensity = 0.02f;
	};

	FStageMood MoodFor(EPGFlowScreen Screen)
	{
		FStageMood Mood;
		// 해는 화면 왼쪽 뒤, 지평선 바로 위. 캐릭터 어깨에 따뜻한 테두리 빛이 걸리고 앞은 하늘빛이 채운다(배그 로비 느낌).
		//   카메라는 -X 를 본다 → 화면 왼쪽은 +Y. 해가 (-X,+Y) 쪽에 있으니 빛은 (+X,-Y) 로 간다 = Yaw -60.
		if (Screen == EPGFlowScreen::Title)
		{
			// 타이틀은 더 낮고 붉게. 불 잔해의 주황빛이 주인공이 되게 해를 약하게 둔다.
			Mood.SunRotation = FRotator(-4.0f, -60.0f, 0.0f);
			Mood.SunColor = FLinearColor(1.0f, 0.42f, 0.24f);
			Mood.SunLux = 2.5f;
			Mood.SkyIntensity = 0.6f;
			Mood.FogColor = FLinearColor(0.32f, 0.16f, 0.1f);
			Mood.FogDensity = 0.045f;
		}
		else
		{
			Mood.SunRotation = FRotator(-9.0f, -60.0f, 0.0f);
			Mood.SunColor = FLinearColor(1.0f, 0.64f, 0.42f);
			Mood.SunLux = 5.0f;
			Mood.SkyIntensity = 1.0f;
			Mood.FogColor = FLinearColor(0.42f, 0.3f, 0.3f);
			Mood.FogDensity = 0.025f;
		}
		return Mood;
	}

	// 해·하늘빛·대기·안개. 레벨에 있는 것은 그대로 두고 없는 것만 만든다.
	void BuildSky(UWorld* World, EPGFlowScreen Screen)
	{
		const FStageMood Mood = MoodFor(Screen);
		if (!LevelHas<ADirectionalLight>(World))
		{
			const FTransform SunTransform(Mood.SunRotation);
			if (ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), SunTransform))
			{
				if (UDirectionalLightComponent* Light = Sun->FindComponentByClass<UDirectionalLightComponent>())
				{
					Light->SetMobility(EComponentMobility::Movable);
					Light->SetIntensity(Mood.SunLux);
					Light->SetLightColor(Mood.SunColor);
					// 하늘 대기가 이 빛을 해로 삼아 노을색을 만든다. 끄면 하늘이 해와 따로 논다.
					Light->SetAtmosphereSunLight(true);
					Light->SetDynamicShadowDistanceMovableLight(5000.0f);
				}
				Sun->FinishSpawning(SunTransform);
			}
		}
		if (!LevelHas<ASkyAtmosphere>(World))
			World->SpawnActor<ASkyAtmosphere>(ASkyAtmosphere::StaticClass(), FTransform::Identity);
		if (!LevelHas<ASkyLight>(World))
		{
			if (ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity))
			{
				if (USkyLightComponent* Light = Sky->FindComponentByClass<USkyLightComponent>())
				{
					Light->SetMobility(EComponentMobility::Movable);
					// 실시간 캡처: 구운 큐브맵이 없는 빈 레벨이라 하늘 대기를 그대로 떠서 그림자 쪽을 채운다.
					Light->SetRealTimeCapture(true);
					Light->SetIntensity(Mood.SkyIntensity);
				}
				Sky->FinishSpawning(FTransform::Identity);
			}
		}
		if (!LevelHas<AExponentialHeightFog>(World))
		{
			if (AExponentialHeightFog* Fog = World->SpawnActorDeferred<AExponentialHeightFog>(AExponentialHeightFog::StaticClass(), FTransform::Identity))
			{
				if (UExponentialHeightFogComponent* FogComponent = Fog->FindComponentByClass<UExponentialHeightFogComponent>())
				{
					// 안개가 무대 끝(바닥 평면 가장자리)과 멀리 빈 곳을 가린다. 가까운 캐릭터는 안 흐리게 8m 부터.
					FogComponent->SetFogDensity(Mood.FogDensity);
					FogComponent->SetFogHeightFalloff(0.25f);
					FogComponent->SetFogInscatteringColor(Mood.FogColor);
					FogComponent->SetStartDistance(800.0f);
				}
				Fog->FinishSpawning(FTransform::Identity);
			}
		}
	}

	APointLight* SpawnPointLight(UWorld* World, const FVector& Location, const FLinearColor& Color, float Candelas, float Radius, bool bShadows)
	{
		const FTransform Transform(Location);
		APointLight* Actor = World->SpawnActorDeferred<APointLight>(APointLight::StaticClass(), Transform);
		if (!Actor)
			return nullptr;
		if (UPointLightComponent* Light = Actor->FindComponentByClass<UPointLightComponent>())
		{
			Light->SetMobility(EComponentMobility::Movable);
			// 단위를 칸델라로 못 박는다. 기본(단위 없음)은 엔진 설정에 따라 밝기가 달라진다.
			Light->SetIntensityUnits(ELightUnits::Candelas);
			Light->SetIntensity(Candelas);
			Light->SetLightColor(Color);
			Light->SetAttenuationRadius(Radius);
			Light->SetCastShadows(bShadows);
		}
		Actor->FinishSpawning(Transform);
		return Actor;
	}

	// 나이아가라 시스템을 리플렉션으로 붙인다.
	// 왜 이렇게: UNiagaraComponent 를 직접 쓰려면 Build.cs 에 Niagara(플러그인 모듈) 의존을 걸어야 한다. 그러면 플러그인이 꺼진 PC 에서는
	//   우리 모듈 자체가 안 올라온다 — 배경 연기 하나 때문에 게임이 안 뜨면 안 된다. 그래서 클래스를 이름으로 찾고, 컴포넌트의 "Asset"
	//   속성에 시스템을 넣은 뒤 등록한다(자동 재생이 기본값). 클래스·속성이 없으면 경고만 남기고 건너뛴다.
	void SpawnNiagaraByName(UWorld* World, const TCHAR* SystemPath, const FVector& Location, float Scale)
	{
		UClass* ComponentClass = FindObject<UClass>(nullptr, TEXT("/Script/Niagara.NiagaraComponent"));
		if (!ComponentClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("PGFlowStage: Niagara plugin not loaded — %s skipped"), SystemPath);
			return;
		}
		UObject* System = LoadStageAsset<UObject>(SystemPath);
		FObjectPropertyBase* AssetProperty = FindFProperty<FObjectPropertyBase>(ComponentClass, TEXT("Asset"));
		if (!System || !AssetProperty || !System->IsA(AssetProperty->PropertyClass))
			return;

		AActor* Holder = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Location));
		if (!Holder)
			return;
		USceneComponent* Component = NewObject<USceneComponent>(Holder, ComponentClass, TEXT("StageNiagara"));
		AssetProperty->SetObjectPropertyValue_InContainer(Component, System);
		Holder->SetRootComponent(Component);
		Component->SetWorldLocationAndRotation(Location, FRotator::ZeroRotator);
		Component->SetWorldScale3D(FVector(Scale));
		Holder->AddInstanceComponent(Component);
		Component->RegisterComponent();
		Component->Activate(true);
	}

	void SpawnCascade(UWorld* World, const TCHAR* Path, const FVector& Location, float Scale)
	{
		if (UParticleSystem* System = LoadStageAsset<UParticleSystem>(Path))
			UGameplayStatics::SpawnEmitterAtLocation(World, System, Location, FRotator::ZeroRotator, FVector(Scale), false);
	}

	// ---- 무대 조각 ----

	void BuildGround(UWorld* World)
	{
		// 평면(1m x 1m)을 800 배 = 800m. 안개가 가장자리를 먹어서 끝이 안 보인다.
		const UPGFlowStageSet& Set = StageSet();
		UMaterialInterface* GroundMaterial = LoadStageAsset<UMaterialInterface>(*StagePath(Set.GroundMaterial));
		PlaceMesh(World, *StagePath(Set.GroundMesh), FVector::ZeroVector, 0.0f, EStageFit::Scale, 800.0f, GroundMaterial);

		// 풀 덤불을 흩뿌린다. 시드를 고정해 매번 같은 그림(스크린샷·포트폴리오 영상이 매번 같다).
		FRandomStream Random(20260922);
		for (int32 Index = 0; Index < 90; ++Index)
		{
			const FVector Spot(Random.FRandRange(-3200.0f, 260.0f), Random.FRandRange(-2400.0f, 2400.0f), 0.0f);
			// 캐릭터 발 바로 앞은 비운다. 풀 조각이 정강이를 가리면 "전신" 이 안 보인다.
			if (Spot.X > -120.0f && FMath::Abs(Spot.Y) < 140.0f)
				continue;
			const FString Path = StagePathAt(Set.GrassPatches, Index);
			PlaceMesh(World, *Path, Spot, Random.FRandRange(0.0f, 360.0f), EStageFit::Length, Random.FRandRange(140.0f, 280.0f));
		}
	}

	void BuildTrees(UWorld* World)
	{
		struct FTreeSpot { float X, Y, Height; };
		// 캐릭터 뒤(-X)에 부채꼴로. 가까운 것은 낮게, 먼 것은 높게 — 겹겹이 보여 숲 깊이가 생긴다.
		static const FTreeSpot Trees[] = {
			{ -1500.0f, -900.0f, 1300.0f }, { -1900.0f, 300.0f, 1500.0f }, { -2400.0f, -2000.0f, 1600.0f },
			{ -2800.0f, 1400.0f, 1400.0f }, { -1300.0f, 1500.0f, 1200.0f }, { -3500.0f, -300.0f, 1700.0f },
			{ -1700.0f, -2600.0f, 1500.0f }, { -3200.0f, 2600.0f, 1600.0f }, { -4200.0f, 900.0f, 1800.0f },
			{ -4600.0f, -1800.0f, 1800.0f }, { -5200.0f, 200.0f, 2000.0f }, { -2200.0f, 2300.0f, 1500.0f },
		};
		const UPGFlowStageSet& Set = StageSet();
		for (int32 Index = 0; Index < static_cast<int32>(UE_ARRAY_COUNT(Trees)); ++Index)
		{
			const FTreeSpot& Tree = Trees[Index];
			PlaceMesh(World, *StagePathAt(Set.PineTrees, Index), FVector(Tree.X, Tree.Y, 0.0f),
				Index * 47.0f, EStageFit::Height, Tree.Height);
		}
		PlaceMesh(World, *StagePath(Set.BushTree), FVector(-1100.0f, -700.0f, 0.0f), 30.0f, EStageFit::Height, 420.0f);
		PlaceMesh(World, *StagePath(Set.Bush), FVector(-380.0f, -260.0f, 0.0f), 10.0f, EStageFit::Height, 80.0f);
		PlaceMesh(World, *StagePath(Set.Bush), FVector(-420.0f, 560.0f, 0.0f), 120.0f, EStageFit::Height, 70.0f);
		PlaceMesh(World, *StagePath(Set.Rock), FVector(120.0f, -210.0f, 0.0f), 40.0f, EStageFit::Height, 35.0f);
		PlaceMesh(World, *StagePath(Set.Rock), FVector(-900.0f, -500.0f, 0.0f), 200.0f, EStageFit::Height, 140.0f);
		PlaceMesh(World, *StagePath(Set.Stones), FVector(-200.0f, -380.0f, 0.0f), 75.0f, EStageFit::Length, 160.0f);
	}

	void BuildHero(UWorld* World)
	{
		const UPGFlowStageSet& Set = StageSet();
		USkeletalMesh* Mesh = LoadStageAsset<USkeletalMesh>(*StagePath(Set.HeroMesh));
		if (!Mesh)
			return;
		// 메시는 +Y 를 보고 있다(팩 데모·게임 캐릭터가 메시를 Yaw -90 돌리는 이유). -90 이면 +X = 카메라 쪽을 본다.
		const FTransform Transform(FRotator(0.0f, -90.0f, 0.0f), FVector::ZeroVector);
		ASkeletalMeshActor* Hero = World->SpawnActorDeferred<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Hero)
			return;
		USkeletalMeshComponent* Body = Hero->GetSkeletalMeshComponent();
		Body->SetSkeletalMeshAsset(Mesh);
		// 애님 블루프린트 없이 대기 동작 하나만 돈다. 게임 캐릭터의 ABP 는 이동 속도·GAS 를 읽어서 폰 없이 쓰면 안전하지 않다.
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		// 화면 밖으로 나가도 포즈를 계속 갱신(카메라가 흘러갈 때 한 프레임 T 포즈로 튀지 않게).
		Body->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Hero->FinishSpawning(Transform);

		if (UAnimSequence* Idle = LoadStageAsset<UAnimSequence>(*StagePath(Set.HeroIdle)))
			Body->PlayAnimation(Idle, true);
		else
			UE_LOG(LogTemp, Warning, TEXT("PGFlowStage: no idle animation — the character stands in its reference pose"));
	}

	void BuildParkedCar(UWorld* World)
	{
		// 화면 왼쪽 끝에 반쯤 걸치게(배그 로비의 차처럼). 코를 카메라 쪽으로 비스듬히.
		PlaceMesh(World, *StagePath(StageSet().ParkedCar), FVector(-260.0f, 330.0f, 0.0f), 200.0f, EStageFit::Length, 480.0f);
	}

	void BuildBurningWreck(UWorld* World, FPGFlowStageHandles& Stage)
	{
		// 캐릭터 뒤 오른쪽(-Y = 화면 오른쪽)에서 불타는 폐차. 불꽃 위치는 잔해 바운드 윗면에 맞춘다.
		const UPGFlowStageSet& Set = StageSet();
		const FVector WreckSpot(-650.0f, -300.0f, 0.0f);
		FVector FireTop = WreckSpot + FVector(0.0f, 0.0f, 120.0f);
		if (AStaticMeshActor* Wreck = PlaceMesh(World, *StagePath(Set.BurningWreck), WreckSpot, 35.0f, EStageFit::Length, 430.0f))
		{
			FVector Origin, Extent;
			Wreck->GetActorBounds(false, Origin, Extent);
			FireTop = FVector(Origin.X, Origin.Y, Origin.Z + Extent.Z * 0.6f);
		}
		SpawnCascade(World, *StagePath(Set.FireEffect), FireTop, 3.0f);
		SpawnCascade(World, *StagePath(Set.FireEffect), FireTop + FVector(80.0f, 90.0f, -30.0f), 2.2f);
		SpawnCascade(World, *StagePath(Set.FireEffect), FVector(-950.0f, 380.0f, 0.0f), 1.8f);
		SpawnCascade(World, *StagePath(Set.EmbersEffect), FireTop + FVector(200.0f, 0.0f, 50.0f), 1.5f);
		SpawnNiagaraByName(World, *StagePath(Set.BigFireEffect), FireTop, 2.0f);
		SpawnNiagaraByName(World, *StagePath(Set.SmokeEffect), FireTop + FVector(0.0f, 0.0f, 80.0f), 4.0f);

		// 불빛: 캐릭터 등과 풀에 주황빛이 번지게. Animate 가 불꽃처럼 흔든다.
		if (APointLight* Glow = SpawnPointLight(World, FireTop + FVector(0.0f, 0.0f, 60.0f), FLinearColor(1.0f, 0.42f, 0.14f), 900.0f, 3000.0f, true))
		{
			Stage.FireLight = Glow->FindComponentByClass<UPointLightComponent>();
			Stage.FireBaseIntensity = 900.0f;
		}
	}

	struct FStageShot
	{
		FVector Location;
		FVector LookAt;
		float Fov = 50.0f;
	};

	FStageShot ShotFor(EPGFlowScreen Screen)
	{
		// 가로 화각 50° · 16:9 → 세로 반각 약 14.7°. 4.7m 에서 위아래 ±1.2m 가 보여 1.8m 캐릭터 전신이 머리 위 여유와 함께 들어온다.
		switch (Screen)
		{
		case EPGFlowScreen::Title:
			// 조금 낮게 올려다봐서 영웅처럼. 로고가 아래쪽에 깔리니 캐릭터는 화면 위쪽 2/3 에.
			return { FVector(430.0f, 0.0f, 80.0f), FVector(0.0f, 0.0f, 70.0f), 55.0f };
		case EPGFlowScreen::Scoreboard:
			// 위에 제목·띠가 들어가니 캐릭터를 화면 아래 2/3 로 내린다(카메라를 멀리, 높이 본다).
			return { FVector(620.0f, 0.0f, 150.0f), FVector(0.0f, 0.0f, 140.0f), 48.0f };
		default:
			return { FVector(470.0f, 0.0f, 115.0f), FVector(0.0f, 0.0f, 95.0f), 50.0f };
		}
	}

	ACameraActor* BuildCamera(UWorld* World, EPGFlowScreen Screen, FPGFlowStageHandles& Stage, bool bBlackBackdrop)
	{
		const FStageShot Shot = ShotFor(Screen);
		const FTransform Transform((Shot.LookAt - Shot.Location).Rotation(), Shot.Location);
		ACameraActor* Camera = World->SpawnActorDeferred<ACameraActor>(ACameraActor::StaticClass(), Transform);
		if (!Camera)
			return nullptr;
		UCameraComponent* Lens = Camera->GetCameraComponent();
		Lens->SetFieldOfView(Shot.Fov);
		// 화면비를 강제하지 않는다. 21:9·4:3 모니터에서도 검은 띠 없이 옆만 더 보인다.
		Lens->SetConstraintAspectRatio(false);
		Lens->PostProcessBlendWeight = 1.0f;
		FPostProcessSettings& Post = Lens->PostProcessSettings;
		Post.bOverride_VignetteIntensity = true;
		Post.VignetteIntensity = 0.55f;
		if (Screen == EPGFlowScreen::Scoreboard)
		{
			// 타르코프 결과 화면처럼: 캐릭터만 또렷하고 뒤는 흐리고, 색이 빠져 가라앉은 느낌.
			Post.bOverride_DepthOfFieldFocalDistance = true;
			Post.DepthOfFieldFocalDistance = FVector::Dist(Shot.Location, Shot.LookAt);
			Post.bOverride_DepthOfFieldFstop = true;
			Post.DepthOfFieldFstop = 1.2f;
			Post.bOverride_ColorSaturation = true;
			Post.ColorSaturation = FVector4(0.55f, 0.55f, 0.55f, 1.0f);
			Post.bOverride_SceneColorTint = true;
			Post.SceneColorTint = FLinearColor(0.72f, 0.72f, 0.78f);
		}
		else if (Screen == EPGFlowScreen::Title)
		{
			Post.bOverride_FilmGrainIntensity = true;
			Post.FilmGrainIntensity = 0.2f;
			Post.bOverride_BloomIntensity = true;
			Post.BloomIntensity = 1.2f;
			Post.VignetteIntensity = 0.75f;
		}
		if (bBlackBackdrop)
		{
			// 영상이 깔릴 때: 3D 무대를 안 짓고 화면을 검게만(영상이 불투명하게 덮는다).
			Post.bOverride_SceneColorTint = true;
			Post.SceneColorTint = FLinearColor::Black;
		}
		Camera->FinishSpawning(Transform);

		Stage.Camera = Camera;
		Stage.CameraBase = Shot.Location;
		Stage.LookAt = Shot.LookAt;
		return Camera;
	}
}

FPGFlowStageHandles PGFlowStage::Build(UWorld* World, EPGFlowScreen Screen)
{
	using namespace PGFlowStageLocal;
	FPGFlowStageHandles Stage;
	if (!IsValid(World))
		return Stage;

	FString VideoPath;
	if (Screen == EPGFlowScreen::Title && FindTitleVideo(VideoPath))
	{
		// 영상이 있으면 3D 를 안 짓는다(보이지도 않는데 그리면 GPU 만 쓴다). 카메라만 둔다.
		BuildCamera(World, Screen, Stage, true);
		UE_LOG(LogTemp, Display, TEXT("PGFlowStage: title video %s found — 3D stage skipped"), *VideoPath);
		return Stage;
	}

	BuildSky(World, Screen);
	BuildGround(World);
	BuildTrees(World);
	BuildHero(World);
	if (Screen == EPGFlowScreen::Title)
		BuildBurningWreck(World, Stage);
	else
		BuildParkedCar(World);
	// 얼굴을 살짝 밝히는 채움 빛. 해가 뒤에 있어 앞은 하늘빛뿐이라 얼굴이 어둡게 뭉개진다. 그림자는 안 만든다(비용·이중 그림자).
	SpawnPointLight(World, FVector(280.0f, -140.0f, 200.0f), FLinearColor(1.0f, 0.86f, 0.74f),
		Screen == EPGFlowScreen::Title ? 20.0f : 40.0f, 1200.0f, false);
	BuildCamera(World, Screen, Stage, false);
	UE_LOG(LogTemp, Display, TEXT("PGFlowStage: stage built for screen %d"), static_cast<int32>(Screen));
	return Stage;
}

void PGFlowStage::Animate(FPGFlowStageHandles& Stage, EPGFlowScreen Screen, float Seconds)
{
	if (ACameraActor* Camera = Stage.Camera.Get())
	{
		FVector Offset = FVector::ZeroVector;
		if (Screen == EPGFlowScreen::Title)
		{
			// 느린 좌우 흐름 + 살짝 오르내림. 주기가 서로 달라(약 52초·31초) 같은 궤적이 금방 되풀이되지 않는다.
			Offset = FVector(20.0f * FMath::Sin(Seconds * 0.09f), 70.0f * FMath::Sin(Seconds * 0.12f), 12.0f * FMath::Sin(Seconds * 0.2f));
		}
		else if (Screen == EPGFlowScreen::Lobby)
		{
			// 로비는 숨 쉬듯 아주 조금. 멈춘 사진처럼 보이지 않을 만큼만.
			Offset = FVector(0.0f, 12.0f * FMath::Sin(Seconds * 0.15f), 4.0f * FMath::Sin(Seconds * 0.23f));
		}
		if (!Offset.IsNearlyZero())
		{
			const FVector Location = Stage.CameraBase + Offset;
			Camera->SetActorLocationAndRotation(Location, (Stage.LookAt - Location).Rotation());
		}
	}
	if (UPointLightComponent* Fire = Stage.FireLight.Get())
	{
		// 불꽃 깜빡임: 주기가 다른 사인 셋을 더해 규칙이 안 보이게. 60~130% 사이.
		const float Flicker = 0.95f + 0.2f * FMath::Sin(Seconds * 11.0f) + 0.12f * FMath::Sin(Seconds * 17.3f) + 0.08f * FMath::Sin(Seconds * 5.1f);
		Fire->SetIntensity(Stage.FireBaseIntensity * FMath::Clamp(Flicker, 0.6f, 1.3f));
	}
}

bool PGFlowStage::FindTitleVideo(FString& OutFullPath)
{
	OutFullPath = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir() / PGFlowStageLocal::StageTitleVideoRelative);
	return IFileManager::Get().FileExists(*OutFullPath);
}
