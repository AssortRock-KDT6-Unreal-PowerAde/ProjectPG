#include "Objects/PGFloorItemActor.h"
#include "Common/PGSoundRouter.h"

#include "Common/PGVisualSettings.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGItemValue.h"
#include "Objects/PGWearableColors.h"
#include "Materials/MaterialInterface.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Perception/AISense_Hearing.h"
#include "TimerManager.h"

UClass* APGFloorItemActor::GetSpawnClass()
{
	return UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().FloorItemClass, APGFloorItemActor::StaticClass());
}

APGFloorItemActor::APGFloorItemActor()
{
	// 연료 폭발 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 에셋).
	FuelBlastDust = TSoftObjectPtr<UParticleSystem>(FSoftObjectPath(TEXT("/Game/ParagonRampage/FX/Particles/Abilities/RipNToss/FX/P_Rampage_Rock_HitWorld.P_Rampage_Rock_HitWorld")));
	FuelBlastMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/SM_MissileBlast.SM_MissileBlast")));
	FuelBlastMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/PG/Finale/Missile/MI_MissileBlast.MI_MissileBlast")));
	// 바닥 아이템은 작다. 걸어가다 걸리면 짜증나므로 폰은 통과시키고, 시선 트레이스에만 잡히게 한다.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	DisplayName = NSLOCTEXT("FloorItem", "DefaultName", "아이템");
}

void APGFloorItemActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(APGFloorItemActor, ItemId);
	DOREPLIFETIME(APGFloorItemActor, Count);
	DOREPLIFETIME(APGFloorItemActor, VisualVariant);
}

void APGFloorItemActor::ApplyCatalogRow(const FPGObjectCatalogRow& Row)
{
	Super::ApplyCatalogRow(Row);
	SetItem(Row.ItemId, Row.ItemCount);
}

void APGFloorItemActor::ApplyCatalogLook()
{
	Super::ApplyCatalogLook();
	// 서버 순서와 같게: 표 메시를 넣은 뒤 착장·색 변형 메시가 있으면 그게 이긴다(ApplyWearableVisual).
	// 클라이언트에서는 ItemId 와 이 겉모습 중 무엇이 먼저 올지 모르므로 여기서 한 번 더 맞춘다.
	ApplyWearableVisual();
}

void APGFloorItemActor::SetItem(FName InItemId, int32 InCount)
{
	ItemId = InItemId;
	Count = FMath::Max(1, InCount);
	if (DisplayName.IsEmpty() || DisplayName.EqualTo(NSLOCTEXT("FloorItem", "DefaultName", "아이템")))
		DisplayName = UPGWearableColorLibrary::GetItemDisplayName(ItemId); // 옷이면 "검정 바지" 같은 이름, 아니면 ItemId 그대로
	ApplyWearableVisual();
}

void APGFloorItemActor::SetVisualVariant(int32 InVariant)
{
	VisualVariant = static_cast<uint8>(FMath::Abs(InVariant) % 256);
	ApplyWearableVisual();
}

void APGFloorItemActor::OnRep_ItemId()
{
	ApplyWearableVisual();
}

void APGFloorItemActor::ApplyWearableVisual()
{
	if (!IsValid(MeshComponent))
		return;
	FPGWearableColor Color;
	if (UPGWearableColorLibrary::FindWearableColor(ItemId, Color))
	{
		// 옷은 착장 표가 메시의 기준이다. 카탈로그 행의 메시가 있어도 덮어쓴다 — 그래야 바닥에 있는 모양과 입었을 때 모양이 같은 팩에서 나온다.
		// SpawnDrop(버리기·몬스터 드롭)은 카탈로그 행을 안 거쳐 메시가 비어 있는데, 그 경우도 여기서 채워진다.
		ApplySoftMesh(MeshComponent, Color.FloorMesh);
		if (UMaterialInterface* Material = Color.Material.LoadSynchronous())
			MeshComponent->SetMaterial(0, Material);
	}
	else
	{
		// 색 변형이 있는 아이템(연료통)은 카탈로그 행의 메시보다 변형 메시가 우선이다.
		const TSoftObjectPtr<UStaticMesh> Variant = UPGWearableColorLibrary::FindItemFloorMeshVariant(ItemId, VisualVariant);
		if (!Variant.IsNull())
			ApplySoftMesh(MeshComponent, Variant);
		else
		{
			// 착장이 아닌 아이템: 카탈로그 행이 메시를 안 줬을 때(SpawnDrop)만 기본 바닥 메시(등급 표의 FloorMesh)를 채운다.
			const TSoftObjectPtr<UStaticMesh> Loose = UPGWearableColorLibrary::FindItemFloorMesh(ItemId);
			if (!MeshComponent->GetStaticMesh() && !Loose.IsNull())
				ApplySoftMesh(MeshComponent, Loose);
		}
	}
	ApplyGradeOverlay();
}

void APGFloorItemActor::ApplyGradeOverlay()
{
	// 등급 색 테두리(9/22). 레어 = 파랑, 에픽 = 보라, 노말 = 테두리 없음.
	// 원래 재질을 바꾸지 않고 위에 한 겹 덧그린다(오버레이) — 총마다 재질이 달라 "색 바꾸는 파라미터"가 제각각이라서.
	// 서버·클라 둘 다 여기로 온다(서버는 SetItem, 클라는 OnRep_ItemId). 등급은 ItemId 로 정해지므로 따로 복제할 값이 없다.
	const EPGItemGrade Grade = UPGItemValueLibrary::GetItemGrade(ItemId);
	UMaterialInterface* Overlay = UPGItemValueLibrary::GetGradeOverlayMaterial(Grade);
	MeshComponent->SetOverlayMaterial(Overlay);
	MeshComponent->SetForceDisableNanite(Overlay != nullptr);
	if (Overlay)
		if (const UPGItemGradeSettings* Settings = GetDefault<UPGItemGradeSettings>())
			MeshComponent->SetOverlayMaterialMaxDrawDistance(Settings->OverlayMaxDrawDistance);
}

APGFloorItemActor* APGFloorItemActor::SpawnDrop(UObject* WorldContextObject, FName InItemId, int32 InCount, const FTransform& Transform)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!IsValid(World) || World->GetNetMode() == NM_Client || InItemId.IsNone())
		return nullptr;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APGFloorItemActor* Item = World->SpawnActorDeferred<APGFloorItemActor>(GetSpawnClass(), Transform);
	if (!IsValid(Item))
		return nullptr;
	Item->SetItem(InItemId, InCount);
	Item->FinishSpawning(Transform);
	return Item;
}

bool APGFloorItemActor::IsLooseObject() const
{
	return ItemId == TEXT("Fuel");
}

void APGFloorItemActor::BeginPlay()
{
	Super::BeginPlay();
	if (IsLooseObject())
		MakeLoose();
}

void APGFloorItemActor::MakeLoose()
{
	if (!IsValid(MeshComponent) || !MeshComponent->GetStaticMesh() || GetRootComponent() == MeshComponent)
	{
		// 일찍 빠지는 길에도 남긴다 — 메시가 없으면 물리 몸도 없어 조용히 "통과하는 연료통" 으로 돌아간다.
		UE_LOG(LogPGObjects, Warning, TEXT("FloorItem %s: %s stays static (mesh=%s)"), *GetName(), *ItemId.ToString(),
			IsValid(MeshComponent) ? *GetNameSafe(MeshComponent->GetStaticMesh()) : TEXT("none"));
		return;
	}
	// 메시를 루트로 올린다. 빈 루트(RootScene)는 메시 밑으로 내려 따라다니게 한다.
	USceneComponent* OldRoot = GetRootComponent();
	MeshComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
	SetRootComponent(MeshComponent);
	if (IsValid(OldRoot) && OldRoot != MeshComponent)
		OldRoot->AttachToComponent(MeshComponent, FAttachmentTransformRules::KeepWorldTransform);

	// 차·땅·총알과 부딪힌다. 걷는 사람(폰)은 전처럼 통과 — 발에 걸려 넘어지는 연료통은 짜증만 난다.
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	MeshComponent->SetCanEverAffectNavigation(false);
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetMassOverrideInKg(NAME_None, 25.0f, true); // 기름 찬 20L 통
	MeshComponent->SetLinearDamping(0.2f);
	MeshComponent->SetAngularDamping(0.4f);
	// 물리는 서버가 돌리고 자리만 복제한다. 클라가 각자 굴리면 사람마다 통이 다른 데 있다.
	SetReplicateMovement(true);
	MeshComponent->SetSimulatePhysics(HasAuthority());
	UE_LOG(LogPGObjects, Display, TEXT("FloorItem %s: %s is a loose physics object (simulating=%d, body=%d)"),
		*GetName(), *ItemId.ToString(), MeshComponent->IsSimulatingPhysics() ? 1 : 0, MeshComponent->GetBodyInstance() && MeshComponent->GetBodyInstance()->IsValidBodyInstance() ? 1 : 0);
}

float APGFloorItemActor::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (!IsLooseObject() || bExploded || !HasAuthority() || DamageAmount <= 0.0f)
		return Applied;

	// 맞은 쪽 반대로 튕긴다. 총알(점 피해)은 날아온 방향, 그 밖(폭발·몸통 박치기)은 때린 쪽에서 멀어지는 방향.
	FVector Push = FVector::ZeroVector;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
		Push = static_cast<const FPointDamageEvent&>(DamageEvent).ShotDirection;
	else if (IsValid(DamageCauser))
		Push = GetActorLocation() - DamageCauser->GetActorLocation();
	Push = Push.GetSafeNormal();
	if (MeshComponent->IsSimulatingPhysics() && !Push.IsNearlyZero())
		MeshComponent->AddImpulse((Push + FVector(0.0f, 0.0f, 0.35f)).GetSafeNormal() * FMath::Clamp(DamageAmount * 12.0f, 250.0f, 900.0f), NAME_None, true);

	FuelDamageTaken += DamageAmount;
	UE_LOG(LogPGObjects, Display, TEXT("FloorItem %s: fuel hit for %.0f (%.0f / %.0f) by %s"),
		*GetName(), DamageAmount, FuelDamageTaken, FuelHealth, *GetNameSafe(DamageCauser));
	if (FuelDamageTaken >= FuelHealth)
		Explode(EventInstigator);
	return Applied;
}

void APGFloorItemActor::Explode(AController* EventInstigator)
{
	if (bExploded)
		return;
	bExploded = true;
	PGSound::PlayAll(this, FName(TEXT("Explosion_Barrel")), nullptr, GetActorLocation());
	UWorld* World = GetWorld();
	const FVector Where = GetActorLocation();
	UE_LOG(LogPGObjects, Display, TEXT("FloorItem %s: fuel exploded at %s (radius %.0fm, damage %.0f)"),
		*GetName(), *Where.ToCompactString(), FuelBlastRadius * 0.01f, FuelBlastDamage);

	// 주변에 피해 — 자기 자신은 빼고(이미 터지는 중이다).
	TArray<AActor*> Ignore = { this };
	UGameplayStatics::ApplyRadialDamageWithFalloff(World, FuelBlastDamage, FuelBlastDamage * 0.2f, Where,
		FuelBlastRadius * 0.3f, FuelBlastRadius, 1.0f, UDamageType::StaticClass(), Ignore, this, EventInstigator, ECC_Visibility);
	UAISense_Hearing::ReportNoiseEvent(World, Where, 1.0f, this, 0.0f, TEXT("FuelExplosion"));

	MulticastFuelBlastFx(Where);
	Destroy();
}

void APGFloorItemActor::MulticastFuelBlastFx_Implementation(FVector_NetQuantize Where)
{
	if (GetNetMode() != NM_DedicatedServer)
		PlayFuelBlastFx(Where);
}

void APGFloorItemActor::PlayFuelBlastFx(const FVector& Where)
{
	UWorld* World = GetWorld();
	if (!World)
		return;
	// 보이는 폭발: 흙먼지 + 미사일 폭발 구(Content/PG 에 있어 어느 PC 에서나 뜬다)를 0.5초 동안 키우며 지운다.
	if (UParticleSystem* Dust = FuelBlastDust.IsNull() ? nullptr : FuelBlastDust.LoadSynchronous())
		UGameplayStatics::SpawnEmitterAtLocation(World, Dust, Where, FRotator::ZeroRotator, FVector(2.0f), true, EPSCPoolMethod::AutoRelease);
	if (UStaticMesh* BlastMesh = FuelBlastMesh.IsNull() ? nullptr : FuelBlastMesh.LoadSynchronous())
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AActor* Flash = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform(Where), Params))
		{
			UStaticMeshComponent* Ball = NewObject<UStaticMeshComponent>(Flash, TEXT("FuelBlast"));
			Ball->SetMobility(EComponentMobility::Movable);
			Ball->SetStaticMesh(BlastMesh);
			if (UMaterialInterface* BlastMaterial = FuelBlastMaterial.IsNull() ? nullptr : FuelBlastMaterial.LoadSynchronous())
				Ball->SetMaterial(0, BlastMaterial);
			Ball->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ball->SetCastShadow(false);
			Flash->SetRootComponent(Ball);
			Ball->RegisterComponent();
			Ball->SetWorldLocation(Where);
			const double Start = World->GetTimeSeconds();
			const float MaxScale = FuelBlastRadius * 0.6f / 100.0f; // SM_MissileBlast 는 반지름 1m 구
			TWeakObjectPtr<UStaticMeshComponent> WeakBall(Ball);
			TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
			World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Flash, [WeakBall, Start, MaxScale, Handle]()
			{
				UStaticMeshComponent* B = WeakBall.Get();
				if (!IsValid(B))
					return;
				const float T = FMath::Clamp(static_cast<float>(B->GetWorld()->GetTimeSeconds() - Start) / 0.5f, 0.0f, 1.0f);
				B->SetWorldScale3D(FVector(FMath::Lerp(0.3f, MaxScale, FMath::Sqrt(T))));
				if (T >= 1.0f)
				{
					B->GetWorld()->GetTimerManager().ClearTimer(*Handle);
					B->GetOwner()->Destroy();
				}
			}), 1.0f / 30.0f, true);
			Flash->SetLifeSpan(2.0f); // 타이머가 어떤 이유로 못 치워도 남지 않게
		}
	}
	if (GetNetMode() == NM_Client)
		UE_LOG(LogPGObjects, Display, TEXT("FloorItem %s: fuel blast on this screen at %s"), *GetName(), *Where.ToCompactString());
}

bool APGFloorItemActor::CanInteractInternal(APawn* Interactor, FText& OutReason) const
{
	if (ItemId.IsNone())
	{
		OutReason = NSLOCTEXT("FloorItem", "Empty", "빈 아이템");
		return false;
	}
	return true;
}

void APGFloorItemActor::HandleInteract(APawn* Interactor)
{
	// 인벤토리가 받아 줘야만 사라진다. 가방이 꽉 차면 그대로 바닥에 남는다.
	if (!UPGItemReceiverLibrary::GiveItem(Interactor, ItemId, Count))
	{
		UE_LOG(LogPGObjects, Display, TEXT("FloorItem %s: %s x%d refused by %s"), *GetName(), *ItemId.ToString(), Count, *GetNameSafe(Interactor));
		return;
	}
	UE_LOG(LogPGObjects, Display, TEXT("FloorItem %s: %s x%d picked by %s"), *GetName(), *ItemId.ToString(), Count, *GetNameSafe(Interactor));
	Destroy();
}

FText APGFloorItemActor::GetPromptInternal() const
{
	if (Count > 1)
		return FText::Format(NSLOCTEXT("FloorItem", "PickupMany", "{0} 줍기 (x{1})"), DisplayName, FText::AsNumber(Count));
	return FText::Format(NSLOCTEXT("FloorItem", "Pickup", "{0} 줍기"), DisplayName);
}
