#include "Objects/PGWearableComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Character.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Objects/PGObjectTypes.h"

namespace
{
	constexpr int32 SlotCount = static_cast<int32>(EPGWearSlot::Shoes) + 1;

	// 몸을 대신하는 메시라 벗을 수 없는 슬롯.
	bool IsBodySlot(EPGWearSlot Slot) { return Slot == EPGWearSlot::Top || Slot == EPGWearSlot::Bottom; }
}

UPGWearableComponent::UPGWearableComponent()
{
	// 몸 칸 기본값(9/23 블루프린트 분리 전 코드에 적혀 있던 Quantum 머리·팔 모듈).
	BodyHeadMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/QuantumCharacter/Mesh/Modules/SKM_Head.SKM_Head")));
	BodyArmsMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Game/QuantumCharacter/Mesh/Modules/SKM_Arms.SKM_Arms")));
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPGWearableComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UPGWearableComponent, EquippedItems);
	DOREPLIFETIME(UPGWearableComponent, bHolsteredPistolVisible);
}

USkeletalMeshComponent* UPGWearableComponent::GetBodyMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	return IsValid(Character) ? Character->GetMesh() : nullptr;
}

template <typename T>
T* UPGWearableComponent::MakePart(const FName& Name)
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (!IsValid(Body))
		return nullptr;
	T* Part = NewObject<T>(GetOwner(), Name);
	Part->SetupAttachment(Body);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Part->SetCanEverAffectNavigation(false);
	Part->SetOwnerNoSee(bOwnerNoSee);
	Part->RegisterComponent();
	return Part;
}

void UPGWearableComponent::BeginPlay()
{
	Super::BeginPlay();
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (!IsValid(Body))
	{
		UE_LOG(LogPGObjects, Warning, TEXT("WearableComponent on %s: owner is not a character with a mesh"), *GetNameSafe(GetOwner()));
		return;
	}

	if (bBuildQuantumBody)
	{
		// 본체 = 머리. 팩 데모 블루프린트와 같은 구성이다. 나머지 파츠는 전부 이 메시의 포즈를 따라간다.
		if (USkeletalMesh* Head = BodyHeadMesh.IsNull() ? nullptr : BodyHeadMesh.LoadSynchronous())
			Body->SetSkeletalMeshAsset(Head);
		if (USkeletalMesh* Arms = BodyArmsMesh.IsNull() ? nullptr : BodyArmsMesh.LoadSynchronous())
		{
			ArmsPart = MakePart<USkeletalMeshComponent>(TEXT("Wear_Arms"));
			if (IsValid(ArmsPart))
			{
				ArmsPart->SetSkeletalMeshAsset(Arms);
				ArmsPart->SetLeaderPoseComponent(Body);
			}
		}
	}

	if (GetOwner()->HasAuthority())
	{
		EquippedItems.Init(NAME_None, SlotCount);
		for (const FName& ItemId : DefaultItems)
		{
			FName Previous;
			Equip(ItemId, Previous);
		}
	}
	RefreshVisuals();
}

bool UPGWearableComponent::Equip(FName ItemId, FName& OutPrevious)
{
	OutPrevious = NAME_None;
	FPGWearableColor Info;
	if (!GetOwner()->HasAuthority() || !UPGWearableColorLibrary::FindWearableColor(ItemId, Info) || Info.Slot == EPGWearSlot::None)
		return false;
	if (EquippedItems.Num() != SlotCount)
		EquippedItems.Init(NAME_None, SlotCount);

	const int32 Index = static_cast<int32>(Info.Slot);
	OutPrevious = EquippedItems[Index];
	if (OutPrevious == ItemId)
	{
		// 같은 것을 또 주웠다. 입은 건 그대로 두고 "입지 않았다"고 알려서 부르는 쪽이 인벤토리에만 넣게 한다.
		OutPrevious = NAME_None;
		return false;
	}
	EquippedItems[Index] = ItemId;
	ApplySlot(Info.Slot, ItemId);
	OnWearableChanged.Broadcast(Info.Slot, ItemId);
	UE_LOG(LogPGObjects, Display, TEXT("%s wears %s in slot %d (was %s)"), *GetNameSafe(GetOwner()), *ItemId.ToString(), Index, *OutPrevious.ToString());
	return true;
}

FName UPGWearableComponent::Unequip(EPGWearSlot Slot)
{
	const int32 Index = static_cast<int32>(Slot);
	if (!GetOwner()->HasAuthority() || IsBodySlot(Slot) || !EquippedItems.IsValidIndex(Index))
		return NAME_None;
	const FName Previous = EquippedItems[Index];
	EquippedItems[Index] = NAME_None;
	ApplySlot(Slot, NAME_None);
	OnWearableChanged.Broadcast(Slot, NAME_None);
	return Previous;
}

FName UPGWearableComponent::GetEquipped(EPGWearSlot Slot) const
{
	const int32 Index = static_cast<int32>(Slot);
	return EquippedItems.IsValidIndex(Index) ? EquippedItems[Index] : NAME_None;
}

void UPGWearableComponent::OnRep_EquippedItems()
{
	RefreshVisuals();
}

void UPGWearableComponent::RefreshVisuals()
{
	for (int32 Index = 1; Index < SlotCount; ++Index)
		ApplySlot(static_cast<EPGWearSlot>(Index), EquippedItems.IsValidIndex(Index) ? EquippedItems[Index] : NAME_None);
}

void UPGWearableComponent::ApplySlot(EPGWearSlot Slot, FName ItemId)
{
	USkeletalMeshComponent* Body = GetBodyMesh();
	if (!IsValid(Body))
		return;

	FPGWearableColor Info;
	const bool bHasItem = !ItemId.IsNone() && UPGWearableColorLibrary::FindWearableColor(ItemId, Info);
	USkeletalMesh* Skeletal = bHasItem && !Info.WornSkeletalMesh.IsNull() ? Info.WornSkeletalMesh.LoadSynchronous() : nullptr;
	UStaticMesh* Static = bHasItem && !Info.WornStaticMesh.IsNull() ? Info.WornStaticMesh.LoadSynchronous() : nullptr;
	UMaterialInterface* Material = bHasItem && !Info.Material.IsNull() ? Info.Material.LoadSynchronous() : nullptr;

	// 스켈레탈 파츠: 본체 포즈를 따라간다. 같은 슬롯에 같은 메시면 머티리얼만 갈아 끼운다(색만 다른 옷).
	TObjectPtr<USkeletalMeshComponent>& SkeletalPart = SkeletalParts.FindOrAdd(Slot);
	if (Skeletal)
	{
		if (!IsValid(SkeletalPart))
		{
			SkeletalPart = MakePart<USkeletalMeshComponent>(*FString::Printf(TEXT("Wear_Skeletal_%d"), static_cast<int32>(Slot)));
			if (IsValid(SkeletalPart))
				SkeletalPart->SetLeaderPoseComponent(Body);
		}
		if (IsValid(SkeletalPart))
		{
			if (SkeletalPart->GetSkeletalMeshAsset() != Skeletal)
				SkeletalPart->SetSkeletalMeshAsset(Skeletal);
			if (Material)
				SkeletalPart->SetMaterial(0, Material);
			SkeletalPart->SetVisibility(true);
			if (Slot == EPGWearSlot::Holster)
				ApplyHolsteredPistol(); // 메시를 새로 넣으면 섹션 표시가 초기화된다
		}
	}
	else if (IsValid(SkeletalPart))
	{
		SkeletalPart->SetVisibility(false);
	}

	// 스태틱 파츠: 뼈에 붙인다(모자, 배낭).
	TObjectPtr<UStaticMeshComponent>& StaticPart = StaticParts.FindOrAdd(Slot);
	if (Static)
	{
		if (!IsValid(StaticPart))
			StaticPart = MakePart<UStaticMeshComponent>(*FString::Printf(TEXT("Wear_Static_%d"), static_cast<int32>(Slot)));
		if (IsValid(StaticPart))
		{
			StaticPart->SetStaticMesh(Static);
			StaticPart->AttachToComponent(Body, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Info.AttachBone);
			const FTransform* Tuned = TuneOverrides.Find(Slot);
			const FTransform Offset = Tuned ? *Tuned : Info.AttachOffset;
			// 몸 공간으로 적은 오프셋은 레퍼런스 포즈(T/A 자세)의 뼈 위치를 빼서 뼈 기준으로 바꾼다.
			// 지금 프레임의 포즈를 쓰면 걷는 도중 입었을 때 그 순간의 흔들림이 오프셋에 굳어 버린다.
			const bool bBodySpace = Tuned ? true : Info.bOffsetInBodySpace;
			FTransform Relative = Offset;
			if (bBodySpace && Body->GetSkeletalMeshAsset())
			{
				const FReferenceSkeleton& Ref = Body->GetSkeletalMeshAsset()->GetRefSkeleton();
				FTransform BoneInBody = FTransform::Identity;
				for (int32 Bone = Ref.FindBoneIndex(Info.AttachBone); Bone != INDEX_NONE; Bone = Ref.GetParentIndex(Bone))
					BoneInBody = BoneInBody * Ref.GetRefBonePose()[Bone]; // 자식 → 부모 순으로 곱하면 뼈의 몸 공간 위치
				Relative = Offset.GetRelativeTransform(BoneInBody);
			}
			StaticPart->SetRelativeTransform(Relative);
			if (Material)
				StaticPart->SetMaterial(0, Material);
			StaticPart->SetVisibility(true);
		}
	}
	else if (IsValid(StaticPart))
	{
		StaticPart->SetVisibility(false);
	}
}

void UPGWearableComponent::TuneSlot(EPGWearSlot Slot, FTransform Offset)
{
	TuneOverrides.Add(Slot, Offset);
	ApplySlot(Slot, GetEquipped(Slot));
	const FRotator R = Offset.Rotator();
	const FVector L = Offset.GetLocation();
	UE_LOG(LogPGObjects, Display, TEXT("WearTune slot %d: FTransform(FRotator(%.1ff, %.1ff, %.1ff), FVector(%.1ff, %.1ff, %.1ff), FVector(%.2ff)), true  (body space)"),
		static_cast<int32>(Slot), R.Pitch, R.Yaw, R.Roll, L.X, L.Y, L.Z, Offset.GetScale3D().X);
}

void UPGWearableComponent::SetHolsteredPistolVisible(bool bVisible)
{
	if (!GetOwner()->HasAuthority() || bHolsteredPistolVisible == bVisible)
		return;
	bHolsteredPistolVisible = bVisible;
	ApplyHolsteredPistol();
}

void UPGWearableComponent::OnRep_HolsteredPistol()
{
	ApplyHolsteredPistol();
}

void UPGWearableComponent::ApplyHolsteredPistol()
{
	const TObjectPtr<USkeletalMeshComponent>* Found = SkeletalParts.Find(EPGWearSlot::Holster);
	USkeletalMeshComponent* Holster = Found ? Found->Get() : nullptr;
	const USkeletalMesh* Mesh = IsValid(Holster) ? Holster->GetSkeletalMeshAsset() : nullptr;
	const FSkeletalMeshRenderData* RenderData = Mesh ? Mesh->GetResourceForRendering() : nullptr;
	if (!RenderData)
		return;
	// 권총은 메시 한 덩어리 안의 머티리얼 섹션이다. 메시를 둘로 나누지 않고 그 섹션만 켜고 끈다(모든 LOD).
	const int32 PistolMaterial = Mesh->GetMaterials().IndexOfByPredicate([](const FSkeletalMaterial& M) { return M.MaterialSlotName == TEXT("M_Pistol"); });
	if (PistolMaterial == INDEX_NONE)
		return;
	for (int32 Lod = 0; Lod < RenderData->LODRenderData.Num(); ++Lod)
	{
		const TArray<FSkelMeshRenderSection>& Sections = RenderData->LODRenderData[Lod].RenderSections;
		for (int32 Section = 0; Section < Sections.Num(); ++Section)
			if (Sections[Section].MaterialIndex == PistolMaterial)
				Holster->ShowMaterialSection(PistolMaterial, Section, bHolsteredPistolVisible, Lod);
	}
}

void UPGWearableComponent::SetOwnerNoSee(bool bNoSee)
{
	bOwnerNoSee = bNoSee;
	if (USkeletalMeshComponent* Body = GetBodyMesh())
		Body->SetOwnerNoSee(bNoSee);
	if (IsValid(ArmsPart))
		ArmsPart->SetOwnerNoSee(bNoSee);
	for (const TPair<EPGWearSlot, TObjectPtr<USkeletalMeshComponent>>& Pair : SkeletalParts)
		if (IsValid(Pair.Value))
			Pair.Value->SetOwnerNoSee(bNoSee);
	for (const TPair<EPGWearSlot, TObjectPtr<UStaticMeshComponent>>& Pair : StaticParts)
		if (IsValid(Pair.Value))
			Pair.Value->SetOwnerNoSee(bNoSee);
}
