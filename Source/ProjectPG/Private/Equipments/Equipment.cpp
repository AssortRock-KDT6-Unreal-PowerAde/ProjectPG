// Fill out your copyright notice in the Description page of Project Settings.


#include "Equipments/Equipment.h"

#include "Common/TableData.h"
#include "Common/GameData.h"
#include "Animations/CustomAnimInstance.h"
#include "Characters/CustomCharacter.h"
#include "Core/ItemSubSystem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

AEquipment::AEquipment()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	USceneComponent* root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(root);
}

void AEquipment::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEquipment, EquipmentState);
}

void AEquipment::InitializeEquipment(FName ItemID)
{
	UItemSubSystem* ItemSubsystem = UItemSubSystem::Get(this);
	const FEquipTableRow* Data = ItemSubsystem->GetEquip(ItemID);
	if (!Data)
		return;

	// Client reconstruction is local presentation work, not an unequip RPC.
	if (HasAuthority()) UnequipInternal();
	else Detach();

	for (auto& [Socket, MeshComp] : StaticMeshesBySocket)
		if (IsValid(MeshComp)) MeshComp->DestroyComponent();
	StaticMeshesBySocket.Reset();

	for (auto& [Socket, MeshComp] : SkeletalMeshesBySocket)
		if (IsValid(MeshComp)) MeshComp->DestroyComponent();
	SkeletalMeshesBySocket.Reset();

	for (auto& [Socket, Actor] : BlueprintsBySocket)
		if (IsValid(Actor)) Actor->Destroy();
	BlueprintsBySocket.Reset();

	SocketItemTypes.Reset();
	EquipmentItemID = ItemID.IsNone() ? FName(*FString::FromInt(Data->ItemID)) : ItemID;
	BuiltItemID = EquipmentItemID;
	LeftHandIKData = FWeaponHandIKData();

	for (auto& [Socket, MeshData] : Data->StaticMeshes)
	{
		if (!MeshData.Mesh
			|| Socket.IsNone()
			|| SocketItemTypes.Contains(Socket))
			continue;
		UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(this);

		StaticMeshesBySocket.Add(Socket, MeshComp);
		MeshComp->SetupAttachment(GetRootComponent());
		MeshComp->SetRelativeTransform(MeshData.RelativeTransform);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetStaticMesh(MeshData.Mesh);

		// Every peer builds these cosmetic parts from replicated equipment state.
		MeshComp->SetIsReplicated(false);

		MeshComp->RegisterComponent();
		MeshComp->SetVisibility(false);
		SocketItemTypes.Add(Socket, EEquipType::Static);
	}

	for (auto& [Socket, MeshData] : Data->SkeletalMeshes)
	{
		if (!MeshData.Mesh
			|| Socket.IsNone()
			|| SocketItemTypes.Contains(Socket))
			continue;
		USkeletalMeshComponent* MeshComp = NewObject<USkeletalMeshComponent>(this);

		SkeletalMeshesBySocket.Add(Socket, MeshComp);
		MeshComp->SetupAttachment(GetRootComponent());
		MeshComp->SetRelativeTransform(MeshData.RelativeTransform);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetSkeletalMesh(MeshData.Mesh);

		MeshComp->SetIsReplicated(false);

		MeshComp->RegisterComponent();
		MeshComp->SetVisibility(false);
		SocketItemTypes.Add(Socket, EEquipType::Skeletal);
		if (Socket == TEXT("Holster") && Data->HolsterPistolMaterial)
		{
			const int32 Slot = MeshComp->GetMaterialIndex(TEXT("M_Pistol"));
			if (Slot != INDEX_NONE) MeshComp->SetMaterial(Slot, Data->HolsterPistolMaterial);
		}
	}

	UWorld* World = GetWorld();
	if (!IsValid(World))
		return;

	for (auto& [Socket, ActorData] : Data->ActorEquipments)
	{
		if (!ActorData.Blueprint || Socket.IsNone() || SocketItemTypes.Contains(Socket)) continue;
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* Actor = World->SpawnActor<AActor>(ActorData.Blueprint, GetActorTransform(), Params);
		if (!IsValid(Actor)) continue;
		if (!Actor->GetRootComponent())
		{
			Actor->Destroy();
			continue;
		}
		Actor->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Actor->GetRootComponent()->SetRelativeTransform(ActorData.RelativeTransform);
		Actor->SetActorHiddenInGame(true);

		Actor->SetReplicates(false);

		BlueprintsBySocket.Add(Socket, Actor);

		SocketItemTypes.Add(Socket, EEquipType::Blueprint);
	}
}

void AEquipment::Attach()
{
	if (!IsValid(OwnerMesh) || !OwnerMesh->GetSkeletalMeshAsset())
		return;

	USceneComponent* RootComp = GetRootComponent();
	RootComp->AttachToComponent(OwnerMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	bIsEquipped = true;
	if (HasAuthority())
		if (auto* Character = Cast<ACustomCharacter>(OwnerCharacter))
			if (auto* Items = UItemSubSystem::Get(this))
				if (const FEquipTableRow* Row = Items->GetEquip(EquipmentItemID))
					Character->SetAmmoCapacityEquipment(this, Row->bDoublesAmmoCapacity);

	for (const auto& [Socket, EquipType] : SocketItemTypes)
	{
		if (!Socket.IsNone()
			&& !OwnerMesh->DoesSocketExist(Socket))
			continue;


		USceneComponent* Part = nullptr;
		switch (EquipType)
		{
		case EEquipType::Static:
			{
				auto MeshComp = StaticMeshesBySocket.Find(Socket);
				if (MeshComp)
					Part = MeshComp->Get();
			}
			break;
		case EEquipType::Skeletal:
			{
				auto MeshComp = SkeletalMeshesBySocket.Find(Socket);
				if (MeshComp)
					Part = MeshComp->Get();
			}
			break;
		case EEquipType::Blueprint:
			{
				auto ActorPtr = BlueprintsBySocket.Find(Socket);
				if (!ActorPtr)
					break;

				auto* Actor = ActorPtr->Get();
				if (!IsValid(Actor))
					break;

				Actor->SetActorHiddenInGame(false);
				Part = Actor->GetRootComponent();
			}
			break;
		}

		if (!IsValid(Part))
			continue;

		Part->AttachToComponent(OwnerMesh, FAttachmentTransformRules::KeepRelativeTransform, Socket);
		Part->SetVisibility(true, true);

		if (auto* Mesh = Cast<USkeletalMeshComponent>(Part))
		{
			bool bCompatible = Socket.IsNone() || OwnerMesh->GetSocketBoneName(Socket) == TEXT("root");
			if (bCompatible)
				Mesh->SetLeaderPoseComponent(OwnerMesh);
		}
	}

	LeftHandIKData = FWeaponHandIKData();
	if (auto* Items = UItemSubSystem::Get(this))
		if (const auto* Row = Items->GetWeapon(EquipmentItemID))
		{
			if (Row->AnimBlueprint)
			{
				if (OwnerMesh->GetAnimClass() != Row->AnimBlueprint.Get())
					OwnerMesh->SetAnimInstanceClass(Row->AnimBlueprint);
				if (auto* Character = Cast<ACustomCharacter>(OwnerCharacter))
					Character->ApplyWeaponAnimation(Row->WeaponType, Row->AnimBlueprint);
			}
			else if (auto* Character = Cast<ACustomCharacter>(OwnerCharacter))
				Character->ApplyWeaponAnimation(Row->WeaponType);
			LeftHandIKData = Row->LeftHandIK;
		}
	if (LeftHandIKData.bEnabled)
	{
		BoundAnimInstance = Cast<UCustomAnimInstance>(OwnerMesh->GetAnimInstance());
		if (BoundAnimInstance.IsValid())
			BoundAnimInstance->SetLeftHandIKEquipment(this);
	}
	UE_LOG(LogTemp, Display, TEXT("[EquipmentAnimation] ItemID=%s Character=%s AnimClass=%s Authority=%d"),
	       *EquipmentItemID.ToString(), *GetNameSafe(OwnerCharacter), *GetNameSafe(OwnerMesh->GetAnimClass()),
	       HasAuthority());
}

void AEquipment::Detach()
{
	if (auto* Character = Cast<ACustomCharacter>(OwnerCharacter))
		Character->SetAmmoCapacityEquipment(this, false);
	bIsEquipped = false;
	if (BoundAnimInstance.IsValid()) BoundAnimInstance->ClearLeftHandIKEquipment(this);
	if (IsValid(OwnerMesh))
		if (auto* Anim = Cast<UCustomAnimInstance>(OwnerMesh->GetAnimInstance())) Anim->ClearLeftHandIKEquipment(this);
	BoundAnimInstance.Reset();
	for (const auto& Entry : SocketItemTypes)
	{
		USceneComponent* Part = FindEquipmentPart(Entry.Key);
		if (!IsValid(Part)) continue;
		if (auto* Mesh = Cast<USkeletalMeshComponent>(Part)) Mesh->SetLeaderPoseComponent(nullptr);
		Part->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Part->SetVisibility(false, true);
		if (const auto* Actor = BlueprintsBySocket.Find(Entry.Key))
			if (IsValid(Actor->Get())) (*Actor)->SetActorHiddenInGame(true);
	}
	GetRootComponent()->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
}

void AEquipment::Equip(ACharacter* Character)
{
	if (!IsValid(Character)
		|| !IsValid(Character->GetMesh())
		|| !Character->GetMesh()->GetSkeletalMeshAsset())
		return;

	if (!HasAuthority())
	{
		if (HasLocalNetOwner()) Req_Equip(Character);
		else UE_LOG(LogTemp, Warning,
		            TEXT("Equip: server must assign this equipment's Owner to the requesting player before the RPC."));
		return;
	}
	EquipInternal(Character);
}

void AEquipment::EquipInternal(ACharacter* Character)
{
	if (!HasAuthority()
		|| !IsValid(Character)
		|| !IsValid(Character->GetMesh())
		|| !Character->GetMesh()->GetSkeletalMeshAsset())
		return;

	UnequipInternal();
	OwnerCharacter = Character;
	OwnerMesh = Character->GetMesh();
	// Attach 관계와 별개로 RPC를 보낼 플레이어의 네트워크 소유권을 설정합니다.
	SetOwner(Character);
	Attach();
	EquipmentState.ItemID = EquipmentItemID;
	EquipmentState.Character = Character;
	EquipmentState.bEquipped = true;
	ForceNetUpdate();
}

void AEquipment::Unequip()
{
	if (!HasAuthority())
	{
		if (HasLocalNetOwner()) Req_Unequip();
		return;
	}
	UnequipInternal();
}

void AEquipment::UnequipInternal()
{
	if (!HasAuthority()) return;
	Detach();
	OwnerCharacter = nullptr;
	OwnerMesh = nullptr;
	EquipmentState.Character = nullptr;
	EquipmentState.bEquipped = false;
	ForceNetUpdate();
}

void AEquipment::Req_Equip_Implementation(ACharacter* Character)
{
	if (!HasAuthority()) return;

	ACharacter* OwningCharacter = Cast<ACharacter>(GetOwner());
	if (const APlayerController* Controller = Cast<APlayerController>(GetOwner()))
		OwningCharacter = Cast<ACharacter>(Controller->GetPawn());
	else if (const APlayerState* PlayerState = Cast<APlayerState>(GetOwner()))
		OwningCharacter = Cast<ACharacter>(PlayerState->GetPawn());

	if (!IsValid(Character) || Character != OwningCharacter)
	{
		UE_LOG(LogTemp, Warning, TEXT("Req_Equip: requested character must match the equipment's owning player."));
		return;
	}

	EquipInternal(Character);
}

void AEquipment::Req_Unequip_Implementation()
{
	if (!HasAuthority()) return;
	UnequipInternal();
	// Owner는 유지: 해제 후에도 같은 플레이어가 Req_Equip을 호출할 수 있습니다.
}

void AEquipment::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) OnRep_EquipmentState();
}

void AEquipment::OnRep_EquipmentState()
{
	EquipmentStateRetryCount = 0;
	ApplyReplicatedEquipmentState();
}

void AEquipment::ApplyReplicatedEquipmentState()
{
	if (HasAuthority()) return;
	if (!EquipmentState.bEquipped)
	{
		GetWorldTimerManager().ClearTimer(EquipmentStateRetryTimer);
		Detach();
		OwnerCharacter = nullptr;
		OwnerMesh = nullptr;
		return;
	}
	ACharacter* Character = EquipmentState.Character;
	UItemSubSystem* Items = UItemSubSystem::Get(this);
	const FEquipTableRow* Data = Items && !EquipmentState.ItemID.IsNone()
		                             ? Items->GetEquip(EquipmentState.ItemID)
		                             : nullptr;
	if (!IsValid(Character) || !IsValid(Character->GetMesh()) || !Character->GetMesh()->GetSkeletalMeshAsset() || !Data)
	{
		if (++EquipmentStateRetryCount <= 100)
			GetWorldTimerManager().SetTimer(EquipmentStateRetryTimer, this, &AEquipment::ApplyReplicatedEquipmentState,
			                                0.1f, false);
		else
			UE_LOG(LogTemp, Warning, TEXT("[EquipmentReplication] Presentation not ready: ItemID=%s Character=%s"),
		       *EquipmentState.ItemID.ToString(), *GetNameSafe(Character));
		return;
	}
	GetWorldTimerManager().ClearTimer(EquipmentStateRetryTimer);
	if (BuiltItemID != EquipmentState.ItemID) InitializeEquipment(EquipmentState.ItemID);
	OwnerCharacter = Character;
	OwnerMesh = Character->GetMesh();
	Attach();
	UE_LOG(LogTemp, Display, TEXT("[EquipmentReplication] Applied ItemID=%s Character=%s AnimClass=%s Parts=%d"),
	       *EquipmentItemID.ToString(), *Character->GetName(), *GetNameSafe(OwnerMesh->GetAnimClass()),
	       SocketItemTypes.Num());
}

USceneComponent* AEquipment::FindEquipmentPart(FName PartName) const
{
	if (const auto* Part = StaticMeshesBySocket.Find(PartName)) return Part->Get();
	if (const auto* Part = SkeletalMeshesBySocket.Find(PartName)) return Part->Get();
	if (const auto* Actor = BlueprintsBySocket.Find(PartName))
		return IsValid(Actor->Get()) ? (*Actor)->GetRootComponent() : nullptr;
	return nullptr;
}

bool AEquipment::GetLeftHandIKTarget(FTransform& TargetTransform, float& Alpha, FName& ReferenceBoneName) const
{
	TargetTransform = FTransform::Identity;
	Alpha = 0.f;
	ReferenceBoneName = LeftHandIKData.ReferenceBoneName;
	if (!bIsEquipped || !LeftHandIKData.bEnabled || !IsValid(OwnerMesh) || OwnerMesh->GetBoneIndex(ReferenceBoneName) ==
		INDEX_NONE)
		return false;
	USceneComponent* Part = FindEquipmentPart(LeftHandIKData.EquipmentPartName);
	// 단일 파츠만 이전 데이터 자동 호환. Map 순서에 의존하지 않습니다.
	if (LeftHandIKData.EquipmentPartName.IsNone() && SocketItemTypes.Num() == 1)
		for (const auto& Entry : SocketItemTypes) Part = FindEquipmentPart(Entry.Key);
	if (!IsValid(Part) || Part->GetAttachParent() != OwnerMesh) return false;
	if (!LeftHandIKData.GripSocketName.IsNone() && !Part->DoesSocketExist(LeftHandIKData.GripSocketName)) return false;
	const FTransform GripWorld = LeftHandIKData.GripTransform * Part->GetSocketTransform(
		LeftHandIKData.GripSocketName, RTS_World);
	TargetTransform = GripWorld.GetRelativeTransform(OwnerMesh->GetSocketTransform(ReferenceBoneName, RTS_World));
	Alpha = FMath::Clamp(LeftHandIKData.Alpha, 0.f, 1.f);
	return true;
}

void AEquipment::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(EquipmentStateRetryTimer);
	// 종료 시 로컬 정리만 수행하고 서버에 장착 해제 요청을 보내지 않습니다.
	Detach();
	OwnerCharacter = nullptr;
	OwnerMesh = nullptr;
	for (const auto& Entry : BlueprintsBySocket)
		if (IsValid(Entry.Value)) Entry.Value->Destroy();
	Super::EndPlay(Reason);
}
