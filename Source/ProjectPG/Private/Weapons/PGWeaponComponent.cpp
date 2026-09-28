#include "Weapons/PGWeaponComponent.h"
#include "Common/PGSoundRouter.h"

#include "Camera/CameraComponent.h"
#include "Common/PGVisualSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/DataTable.h"
#include "Flow/PGRunSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Objects/PGItemReceiverInterface.h"
#include "Objects/PGObjectTypes.h"
#include "Perception/AISense_Hearing.h"

UPGWeaponComponent::UPGWeaponComponent()
{
	// 몸 기준으로 들 때 매 프레임 손 위치를 따라가야 한다. 애니 포즈가 끝난 뒤에 읽어야 손보다 한 프레임 늦지 않다.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetIsReplicatedByDefault(true);
	RegisterDefaultWeapons(*this);
}

void UPGWeaponComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGWeaponComponent, EquippedItemId);
}

void UPGWeaponComponent::BeginPlay()
{
	Super::BeginPlay();

	// 손에 드는 메시는 무기가 바뀔 때마다 갈아끼우므로 컴포넌트 하나를 재사용한다.
	if (AActor* Owner = GetOwner())
	{
		HeldMesh = NewObject<UStaticMeshComponent>(Owner, TEXT("HeldWeaponMesh"));
		HeldMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		HeldMesh->SetCanEverAffectNavigation(false);
		HeldMesh->SetIsReplicated(false);
		HeldMesh->RegisterComponent();
		HeldMesh->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}
	ApplyEquippedVisual();
}

void UPGWeaponComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	// 몸 기준 들기: 위치 = 손 소켓 + 보정(몸 방향으로 돌림), 방향 = 몸 정면 + 무기별 회전.
	if (!bHoldInBodySpace || !bHeldInHandBodySpace || !IsValid(HeldMesh) || !HeldMesh->IsVisible())
		return;
	const FPGWeaponDef* Def = FindDef(EquippedItemId);
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	const USkeletalMeshComponent* Body = IsValid(Character) ? Character->GetMesh() : nullptr;
	if (!Def || !IsValid(Body) || !Body->DoesSocketExist(Def->AttachSocket))
		return;
	const FRotator BodyYaw(0.0f, Character->GetActorRotation().Yaw, 0.0f);
	const FVector Hand = Body->GetSocketLocation(Def->AttachSocket);
	HeldMesh->SetWorldLocationAndRotation(Hand + BodyYaw.RotateVector(Def->AttachLocation), (BodyYaw.Quaternion() * Def->AttachRotation.Quaternion()));
}

// ---- 데이터 ----

void UPGWeaponComponent::RegisterWeapon(const FPGWeaponDef& Def)
{
	if (Def.ItemId.IsNone())
		return;
	Defs.Add(Def.ItemId, Def);
}

void UPGWeaponComponent::LoadFromDataTable(UDataTable* Table)
{
	if (!IsValid(Table))
		return;
	Table->ForeachRow<FPGWeaponDef>(TEXT("PGWeaponComponent::LoadFromDataTable"), [this](const FName& RowName, const FPGWeaponDef& Row)
	{
		FPGWeaponDef Copy = Row;
		if (Copy.ItemId.IsNone())
			Copy.ItemId = RowName;
		RegisterWeapon(Copy);
	});
}

void UPGWeaponComponent::OnRegister()
{
	Super::OnRegister();
	// 왜 BeginPlay 가 아니라 여기인가: 월드 전리품(UPGWorldLootSpawner)이 "액터가 스폰됐다" 알림을 받자마자 등급 변형 총을
	//   원래 총 값에서 복사한다. 그 알림은 BeginPlay 보다 먼저 온다 — 표가 먼저 들어가 있어야 변형 총도 표 값을 따른다.
	UWorld* World = GetWorld();
	if (bWeaponTableApplied || !World || !World->IsGameWorld())
		return;
	bWeaponTableApplied = true;
	const TSoftObjectPtr<UDataTable>& Designed = UPGVisualSettings::Get().WeaponTable;
	if (Designed.IsNull())
		return;
	if (UDataTable* Table = Designed.LoadSynchronous())
	{
		LoadFromDataTable(Table);
		UE_LOG(LogPGObjects, Verbose, TEXT("PGWeapon: %s applied (%d rows)"), *Table->GetName(), Table->GetRowMap().Num());
	}
	else
	{
		UE_LOG(LogPGObjects, Warning, TEXT("PGWeapon: weapon table %s not found — using the code table"), *Designed.ToString());
	}
}

int32 UPGWeaponComponent::WriteDefaultWeaponsToTable(UDataTable* Table)
{
	if (!IsValid(Table) || Table->GetRowStruct() != FPGWeaponDef::StaticStruct())
		return 0;
	const UPGWeaponComponent* Defaults = GetDefault<UPGWeaponComponent>();
	for (const TPair<FName, FPGWeaponDef>& Pair : Defaults->Defs)
		Table->AddRow(Pair.Key, Pair.Value);
	return Defaults->Defs.Num();
}

void UPGWeaponComponent::LogWeaponDefs() const
{
	TArray<FName> Ids;
	Defs.GetKeys(Ids);
	Ids.Sort(FNameLexicalLess());
	for (const FName& Id : Ids)
	{
		const FPGWeaponDef& D = Defs[Id];
		UE_LOG(LogPGObjects, Display, TEXT("PGWeapon: %s name=%s mesh=%s range=%.0f dmg=%.1f every=%.2f r=%.1f ammo=%s x%d pellets=%d spread=%.1f hold=%s/%s fp=%s/%s scale=%.2f socket=%s"),
			*Id.ToString(), *D.DisplayName.ToString(), *D.Mesh.ToSoftObjectPath().ToString(), D.Range, D.Damage, D.AttackInterval, D.TraceRadius,
			*D.AmmoItemId.ToString(), D.AmmoPerShot, D.PelletCount, D.SpreadDegrees, *D.AttachLocation.ToCompactString(), *D.AttachRotation.ToCompactString(),
			*D.FirstPersonLocation.ToCompactString(), *D.FirstPersonRotation.ToCompactString(), D.Scale, *D.AttachSocket.ToString());
	}
}

void UPGWeaponComponent::RegisterDefaultWeapons(UPGWeaponComponent& Component)
{
	auto Make = [](const TCHAR* Id, const TCHAR* Name, EPGWeaponKind Kind, const TCHAR* MeshPath,
		float Range, float Damage, float Interval, float Radius, const TCHAR* Ammo)
	{
		FPGWeaponDef D;
		D.ItemId = Id;
		D.DisplayName = FText::FromString(Name);
		D.Kind = Kind;
		if (MeshPath && *MeshPath)
			D.Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(MeshPath));
		D.Range = Range;
		D.Damage = Damage;
		D.AttackInterval = Interval;
		D.TraceRadius = Radius;
		if (Ammo && *Ammo)
			D.AmmoItemId = Ammo;
		return D;
	};

	// ItemId·메시는 오브젝트 카탈로그(OBJ-027 ~ 031)와 같은 값을 쓴다. 바닥에서 주운 것이 그대로 손에 들린다.
	Component.RegisterWeapon(Make(TEXT("Rifle_AR70"), TEXT("소총 AR70"), EPGWeaponKind::Gun,
		TEXT("/Game/AR70/Models/Ar70_PBR.Ar70_PBR"), 10000.0f, 35.0f, 0.15f, 5.0f, TEXT("Ammo_Rifle")));
	// 권총 메시는 Quantum 권총집(SKM_Holster_Hard_Bege)에 꽂혀 있던 권총을 잘라낸 것(Tools/make_quantum_wearables.py 6번).
	// 캐릭터와 같은 팩이라 질감이 맞고, 안 뽑았을 때는 권총집 안의 같은 권총이 보인다(UPGWearableComponent::SetHolsteredPistolVisible).
	// 방향: 총구 +X, 손잡이 -Z, 피벗 = 손잡이 쥐는 자리.
	Component.RegisterWeapon(Make(TEXT("Pistol"), TEXT("권총"), EPGWeaponKind::Gun,
		TEXT("/Game/PG/Characters/Quantum/Weapons/SM_PGQ_Pistol.SM_PGQ_Pistol"), 6000.0f, 20.0f, 0.3f, 5.0f, TEXT("Ammo_Pistol")));
	// AK 는 Fab "AK-47 with animations"(CC BY 4.0, 출처는 Docs/Credits.md)의 실사 메시를 그대로 쓴다.
	// 로우폴리 AK 에 실사 질감을 덧대 봤지만 모양(각진 개머리판·검은 손잡이)이 한계라 모델 자체를 바꿨다.
	Component.RegisterWeapon(Make(TEXT("Rifle_AK"), TEXT("소총 AK"), EPGWeaponKind::Gun,
		TEXT("/Game/AK-47/Mesh/SM_AK-47.SM_AK-47"), 9000.0f, 32.0f, 0.12f, 5.0f, TEXT("Ammo_Rifle")));
	// 샷건·리볼버는 로우폴리 무기 팩(ithappy Weapons FREE)에 실사 질감을 덧댄 것(Tools/make_realistic_weapons.py). 부품(탄창·펌프)을 합치고 실제 크기로 줄였다.
	Component.RegisterWeapon(Make(TEXT("Shotgun"), TEXT("샷건"), EPGWeaponKind::Gun,
		TEXT("/Game/PG/Weapons/SM_PGW_Shotgun.SM_PGW_Shotgun"), 2500.0f, 14.0f, 0.9f, 4.0f, TEXT("Ammo_Shotgun")));
	Component.RegisterWeapon(Make(TEXT("Revolver"), TEXT("리볼버"), EPGWeaponKind::Gun,
		TEXT("/Game/PG/Weapons/SM_PGW_Pistol.SM_PGW_Pistol"), 6000.0f, 34.0f, 0.55f, 5.0f, TEXT("Ammo_Pistol")));
	if (FPGWeaponDef* Shotgun = Component.Defs.Find(TEXT("Shotgun")))
	{
		// 8발 × 14 = 붙어서 다 맞으면 112. 퍼짐 5도라 10m 에서 지름 약 1.7m — 멀면 몇 발만 맞는다.
		Shotgun->PelletCount = 8;
		Shotgun->SpreadDegrees = 5.0f;
	}
	// 활·투척용 칼·도끼·야구방망이는 뺐다(9/17 무기 범위: 권총·샷건·라이플 세 종류). 되살리려면 이 자리에 행을 다시 넣는다.

	// 붙는 방향·크기. 메시마다 축이 달라 PIE에서 PG.WeaponTune 으로 본 값을 옮겨 적는다.
	// AR70: 실제보다 약 3배 크고 총구가 -X 를 향해 들어온다 → 180도 돌리고 0.35배.
	auto Tune = [&Component](const TCHAR* Id, const FRotator& Rotation, float Scale)
	{
		if (FPGWeaponDef* Def = Component.Defs.Find(FName(Id)))
		{
			Def->FirstPersonRotation = Rotation;
			Def->AttachRotation = Rotation;
			Def->Scale = Scale;
		}
	};
	Tune(TEXT("Rifle_AR70"), FRotator(0.0f, 180.0f, 0.0f), 0.35f);
	// AK-47: 총구가 +Y 를 향하고 길이 112cm 로 들어온다 → yaw -90 으로 총구를 +X 로 돌리고 0.8배(실제 AK 약 88cm).
	Tune(TEXT("Rifle_AK"), FRotator(0.0f, -90.0f, 0.0f), 0.8f);
	// 몸 기준 손 위치: 피벗에서 손잡이까지의 거리를 반대로 준다(눈대중 값. PG.WeaponTune hand 로 고친 뒤 여기 옮겨 적는다).
	//  로우폴리 총은 피벗이 메시 가운데 — 손잡이가 가운데보다 뒤·아래라 앞·위로 민다. Quantum 권총은 피벗이 손잡이라 0.
	//  AK-47 은 피벗이 손잡이 윗부분이라 거의 0(손잡이 중간까지 조금 올린다).
	auto HoldAt = [&Component](const TCHAR* Id, const FVector& Location)
	{
		if (FPGWeaponDef* Def = Component.Defs.Find(FName(Id)))
			Def->AttachLocation = Location;
	};
	HoldAt(TEXT("Rifle_AK"), FVector(2.0f, 0.0f, 3.0f));
	HoldAt(TEXT("Shotgun"), FVector(18.0f, 0.0f, 6.0f));
	HoldAt(TEXT("Revolver"), FVector(6.0f, 0.0f, 4.0f));
}

// ---- 소유·장착 ----

void UPGWeaponComponent::NotifyItemReceived(FName ItemId)
{
	if (!IsWeaponItem(ItemId))
		return;
	OwnedWeapons.AddUnique(ItemId);
	if (EquippedItemId.IsNone())
		Equip(ItemId);
}

bool UPGWeaponComponent::Equip(FName ItemId)
{
	if (!IsWeaponItem(ItemId))
		return false;
	AActor* Owner = GetOwner();
	if (!IsValid(Owner))
		return false;
	if (!Owner->HasAuthority())
	{
		ServerEquip(ItemId);
		return true;
	}
	EquippedItemId = ItemId;
	ApplyEquippedVisual();
	OnEquipped.Broadcast(this, ItemId);
	UE_LOG(LogPGObjects, Display, TEXT("%s equipped %s"), *GetNameSafe(Owner), *ItemId.ToString());
	return true;
}

void UPGWeaponComponent::ServerEquip_Implementation(FName ItemId)
{
	Equip(ItemId);
}

void UPGWeaponComponent::Unequip()
{
	AActor* Owner = GetOwner();
	if (!IsValid(Owner) || !Owner->HasAuthority())
		return;
	EquippedItemId = NAME_None;
	ApplyEquippedVisual();
	OnEquipped.Broadcast(this, NAME_None);
}

bool UPGWeaponComponent::CycleNext()
{
	if (OwnedWeapons.Num() == 0)
		return false;
	const int32 Current = OwnedWeapons.IndexOfByKey(EquippedItemId);
	const int32 Next = (Current + 1) % OwnedWeapons.Num();
	return Equip(OwnedWeapons[Next]);
}

void UPGWeaponComponent::OnRep_EquippedItemId()
{
	ApplyEquippedVisual();
}

void UPGWeaponComponent::ApplyEquippedVisual()
{
	if (!IsValid(HeldMesh))
		return;
	AActor* Owner = GetOwner();
	const FPGWeaponDef* Def = FindDef(EquippedItemId);
	if (!IsValid(Owner) || !Def || Def->Mesh.IsNull())
	{
		HeldMesh->SetStaticMesh(nullptr);
		HeldMesh->SetVisibility(false);
		return;
	}

	bHeldInHandBodySpace = false;
	HeldMesh->SetStaticMesh(Def->Mesh.LoadSynchronous());
	HeldMesh->SetVisibility(true);
	HeldMesh->SetRelativeScale3D(FVector(Def->Scale));

	const APawn* Pawn = Cast<APawn>(Owner);
	UCameraComponent* Camera = Owner->FindComponentByClass<UCameraComponent>();
	USkeletalMeshComponent* Skeletal = Owner->FindComponentByClass<USkeletalMeshComponent>();
	if (const ACharacter* Character = Cast<ACharacter>(Owner))
		Skeletal = Character->GetMesh();

	if (bFirstPersonView && Pawn && Pawn->IsLocallyControlled() && IsValid(Camera))
	{
		// 1인칭: 카메라 앞에 붙여야 본인 눈에 보인다(몸은 OwnerNoSee).
		HeldMesh->AttachToComponent(Camera, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		HeldMesh->SetRelativeLocationAndRotation(FirstPersonOffset + Def->FirstPersonLocation, Def->FirstPersonRotation);
		HeldMesh->SetRelativeScale3D(FVector(Def->Scale));
	}
	else if (bHoldInBodySpace && IsValid(Skeletal) && Skeletal->DoesSocketExist(Def->AttachSocket))
	{
		// 몸 기준: 소켓에 붙이지 않고 캐릭터 루트에 두고 TickComponent 가 매 프레임 손 위치로 옮긴다.
		HeldMesh->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		bHeldInHandBodySpace = true;
	}
	else if (IsValid(Skeletal) && Skeletal->DoesSocketExist(Def->AttachSocket))
	{
		// 다른 사람 눈에는 손에 들려 있어야 한다. 소켓이 없으면 본 이름으로도 붙는다.
		HeldMesh->AttachToComponent(Skeletal, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Def->AttachSocket);
		HeldMesh->SetRelativeLocationAndRotation(Def->AttachLocation, Def->AttachRotation);
	}
	else
	{
		HeldMesh->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		HeldMesh->SetRelativeLocationAndRotation(FVector(60.0f, 25.0f, 30.0f) + Def->AttachLocation, Def->AttachRotation);
	}
}

void UPGWeaponComponent::TuneEquipped(bool bFirstPerson, FVector Location, FRotator Rotation, float Scale)
{
	FPGWeaponDef* Def = Defs.Find(EquippedItemId);
	if (!Def)
		return;
	if (bFirstPerson)
	{
		Def->FirstPersonLocation = Location;
		Def->FirstPersonRotation = Rotation;
	}
	else
	{
		Def->AttachLocation = Location;
		Def->AttachRotation = Rotation;
	}
	Def->Scale = FMath::Max(Scale, 0.01f);
	ApplyEquippedVisual();
	UE_LOG(LogPGObjects, Display, TEXT("WeaponTune: %s"), *DescribeEquippedTuning());
}

FString UPGWeaponComponent::DescribeEquippedTuning() const
{
	const FPGWeaponDef* Def = FindDef(EquippedItemId);
	if (!Def)
		return TEXT("(no weapon equipped)");
	return FString::Printf(TEXT("%s fp loc=%s rot=%s | hand loc=%s rot=%s | scale=%.2f"),
		*Def->ItemId.ToString(),
		*Def->FirstPersonLocation.ToCompactString(), *Def->FirstPersonRotation.ToCompactString(),
		*Def->AttachLocation.ToCompactString(), *Def->AttachRotation.ToCompactString(), Def->Scale);
}

// ---- 공격 ----

bool UPGWeaponComponent::GetAimOriginAndDirection(FVector& OutOrigin, FVector& OutDirection) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (!IsValid(Pawn))
		return false;
	FRotator ViewRotation;
	if (const AController* Controller = Pawn->GetController())
		Controller->GetPlayerViewPoint(OutOrigin, ViewRotation);
	else
		Pawn->GetActorEyesViewPoint(OutOrigin, ViewRotation);
	OutDirection = ViewRotation.Vector();
	return true;
}

void UPGWeaponComponent::Attack()
{
	const FPGWeaponDef* Def = FindDef(EquippedItemId);
	if (!Def)
		return;
	FVector Origin, Direction;
	if (!GetAimOriginAndDirection(Origin, Direction))
		return;

	AActor* Owner = GetOwner();
	if (Owner->HasAuthority())
		PerformAttack(*Def, Origin, Direction);
	else
		ServerAttack(Origin, Direction);
}

void UPGWeaponComponent::ServerAttack_Implementation(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Direction)
{
	const FPGWeaponDef* Def = FindDef(EquippedItemId);
	if (Def)
		PerformAttack(*Def, Origin, Direction);
}

bool UPGWeaponComponent::PerformAttack(const FPGWeaponDef& Def, const FVector& Origin, const FVector& Direction)
{
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!IsValid(Owner) || !IsValid(World) || !Owner->HasAuthority())
		return false;

	// 연사 제한은 서버 시계로만 잰다. 클라이언트가 보낸 횟수는 믿지 않는다.
	const float Now = World->GetTimeSeconds();
	if (Now - LastAttackTime < Def.AttackInterval)
		return false;

	// 탄약: 있으면 먼저 빼고 쏜다. 없으면 빈 방아쇠.
	if (!Def.AmmoItemId.IsNone())
	{
		if (!UPGItemReceiverLibrary::HasItem(Owner, Def.AmmoItemId, Def.AmmoPerShot))
		{
			UE_LOG(LogPGObjects, Display, TEXT("%s: %s 탄약 없음 (%s)"), *GetNameSafe(Owner), *Def.ItemId.ToString(), *Def.AmmoItemId.ToString());
			return false;
		}
		UPGItemReceiverLibrary::ConsumeItem(Owner, Def.AmmoItemId, Def.AmmoPerShot);
	}
	LastAttackTime = Now;
	// 판 기록(사격 수). 플레이어 폰이 아니면 서브시스템이 무시한다.
	UPGRunSubsystem::NotifyShotFired(Owner);
	// 소리 신호(와이즈는 BP_PGSoundRouter 가 낸다). 이 함수는 서버에서만 돌아서 PlayAll — 모두에게 한 번씩.
	PGSound::PlayAll(this, FName(TEXT("Weapon_Fire")), Owner, Origin);
	// 총소리: 몬스터 AI 청각(반경 45m)에 알린다. 이게 없으면 청각 설정만 있고 소리 낼 곳이 없어서, 크리처 등 뒤에서 쏴도 못 알아챘다.
	if (Def.Kind == EPGWeaponKind::Gun)
		UAISense_Hearing::ReportNoiseEvent(World, Origin, 1.0f, Owner, 0.0f, TEXT("Gunshot"));

	// 탄 한 발 = 트레이스 한 번. 샷건은 같은 방식을 여러 번(산탄) 원뿔 안에서 흩어 쏜다 — 탄약은 위에서 한 번만 뺐다.
	// 퍼짐 난수는 서버에서만 굴린다(판정이 서버라 클라이언트와 맞출 필요가 없다).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PGWeaponAttack), false, Owner);
	static const IConsoleVariable* DebugCombat = IConsoleManager::Get().FindConsoleVariable(TEXT("PG.DebugCombat"));
	const bool bDraw = bDrawDebugTrace || (DebugCombat && DebugCombat->GetInt() != 0);
	const APawn* Pawn = Cast<APawn>(Owner);
	TMap<AActor*, int32> Hits; // 로그용: 누가 몇 발 맞았나
	const int32 Pellets = FMath::Max(1, Def.PelletCount);
	for (int32 Pellet = 0; Pellet < Pellets; ++Pellet)
	{
		const FVector PelletDir = Def.SpreadDegrees > 0.0f ? FMath::VRandCone(Direction, FMath::DegreesToRadians(Def.SpreadDegrees)) : Direction;
		const FVector End = Origin + PelletDir * Def.Range;
		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, Origin, End, FQuat::Identity, ECC_Visibility,
			FCollisionShape::MakeSphere(FMath::Max(Def.TraceRadius, 0.1f)), Params);
		AActor* HitActor = bHit ? Hit.GetActor() : nullptr;
		if (IsValid(HitActor))
		{
			UGameplayStatics::ApplyPointDamage(HitActor, Def.Damage, PelletDir, Hit,
				Pawn ? Pawn->GetController() : nullptr, Owner, UDamageType::StaticClass());
			Hits.FindOrAdd(HitActor) += 1;
		}
		// 기본은 안 그린다. 콘솔 PG.DebugCombat 1 (로봇 공격 캡슐과 같은 스위치) 또는 컴포넌트의 bDrawDebugTrace.
		if (bDraw)
		{
			DrawDebugLine(World, Origin, bHit ? Hit.ImpactPoint : End, bHit ? FColor::Red : FColor::Green, false, DebugTraceSeconds, 0, 1.0f);
			if (bHit)
				DrawDebugSphere(World, Hit.ImpactPoint, FMath::Max(Def.TraceRadius, 6.0f), 6, FColor::Red, false, DebugTraceSeconds);
		}
	}

	FString HitText;
	for (const TPair<AActor*, int32>& Pair : Hits)
		HitText += FString::Printf(TEXT("%s x%d  "), *GetNameSafe(Pair.Key), Pair.Value);
	UE_LOG(LogPGObjects, Display, TEXT("%s attack %s pellets=%d hits=%s"),
		*GetNameSafe(Owner), *Def.ItemId.ToString(), Pellets, HitText.IsEmpty() ? TEXT("none") : *HitText);

	// 투척 무기는 마지막 하나를 던지면 손에서 없어진다.
	if (Def.AmmoItemId == Def.ItemId && !UPGItemReceiverLibrary::HasItem(Owner, Def.ItemId, 1))
	{
		OwnedWeapons.Remove(Def.ItemId);
		if (OwnedWeapons.Num() > 0)
			Equip(OwnedWeapons[0]);
		else
			Unequip();
	}
	return true;
}
