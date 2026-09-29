#include "Objects/PGObjectSmokeTest.h"

#include "Actors/ItemContainerActor.h"
#if WITH_EDITOR
#include "AssetCompilingManager.h"
#endif
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Interaction/Interactable.h"
#include "Misc/FileHelper.h"
#include "Objects/PGDestructibleActor.h"
#include "Objects/PGDeviceActor.h"
#include "Objects/PGDoorActor.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Vehicle/PGTankPawn.h"
#include "Finale/PGBattleshipActor.h"
#include "Finale/PGFinaleDirector.h"
#include "Objects/PGExtractionZoneActor.h"
#include "Objects/PGFloorItemActor.h"
#include "Objects/PGInteractionComponent.h"
#include "Objects/PGLockComponent.h"
#include "Objects/PGLootableComponent.h"
#include "Objects/PGItemValue.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGQuestObjectActor.h"
#include "Objects/PGServiceInteractionActor.h"
#include "Objects/PGSpawnSocketComponent.h"
#include "Objects/PGVehicleComponents.h"
#include "Objects/PGWearableColors.h"
#include "Materials/MaterialInterface.h"

// ---------------------------------------------------------------------------
// Test pawn

APGObjectTestPawn::APGObjectTestPawn()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Interaction = CreateDefaultSubobject<UPGInteractionComponent>(TEXT("Interaction"));
}

bool APGObjectTestPawn::ReceiveItem_Implementation(FName ItemId, int32 Count)
{
	if (bRefuseItems || ItemId.IsNone() || Count <= 0)
		return false;
	Items.FindOrAdd(ItemId) += Count;
	return true;
}

bool APGObjectTestPawn::HasItem_Implementation(FName ItemId, int32 Count) const
{
	const int32* Found = Items.Find(ItemId);
	return Found && *Found >= Count;
}

bool APGObjectTestPawn::ConsumeItem_Implementation(FName ItemId, int32 Count)
{
	int32* Found = Items.Find(ItemId);
	if (!Found || *Found < Count)
		return false;
	*Found -= Count;
	if (*Found <= 0)
		Items.Remove(ItemId);
	return true;
}

FName APGObjectTestPawn::GetPlayerKey_Implementation() const
{
	return GetFName();
}

int32 APGObjectTestPawn::GetCount(FName ItemId) const
{
	const int32* Found = Items.Find(ItemId);
	return Found ? *Found : 0;
}

// ---------------------------------------------------------------------------
// Default catalog (노션 "ProjectPG 오브젝트 관리 목록" 확정 항목의 코드 미러)

namespace
{
	FPGLootTableRow MakeLoot(int32 RollCount, std::initializer_list<FPGLootEntry> Entries)
	{
		FPGLootTableRow Row;
		Row.RollCount = RollCount;
		for (const FPGLootEntry& Entry : Entries)
			Row.Entries.Add(Entry);
		return Row;
	}

	FPGLootEntry Entry(const TCHAR* ItemId, float Weight, int32 Min, int32 Max)
	{
		FPGLootEntry E;
		E.ItemId = ItemId;
		E.Weight = Weight;
		E.MinCount = Min;
		E.MaxCount = Max;
		return E;
	}

	FPGObjectCatalogRow MakeRow(const TCHAR* ObjectId, const TCHAR* Name, EPGObjectArchetype Archetype, EPGSpawnSocketKind Socket,
		EPGObjectPriority Priority, bool bMVP, bool bIncluded = true)
	{
		FPGObjectCatalogRow Row;
		Row.ObjectId = ObjectId;
		Row.DisplayName = FText::FromString(Name);
		Row.Archetype = Archetype;
		Row.SocketKind = Socket;
		Row.Priority = Priority;
		Row.bMVP = bMVP;
		Row.bIncluded = bIncluded;
		return Row;
	}

	TSoftObjectPtr<UStaticMesh> Mesh(const TCHAR* Path)
	{
		return TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(Path));
	}
}

void PGObjectSmokeTest::RegisterDefaultCatalog(UPGObjectSpawnerSubsystem& Spawner)
{
	using A = EPGObjectArchetype;
	using S = EPGSpawnSocketKind;
	using P = EPGObjectPriority;

	// ---- 루팅 테이블. ItemId는 팀 아이템 테이블 이름과 맞춘다 (미확정이면 임시 이름).
	Spawner.RegisterLootTable(TEXT("LT_Wood"),     MakeLoot(2, { Entry(TEXT("Bandage"), 3, 1, 2), Entry(TEXT("Ammo_Rifle"), 2, 10, 30), Entry(TEXT("Money"), 2, 50, 200), Entry(TEXT("Scrap"), 1, 1, 3) }));
	// 무기 등급(9/22): 상자 안에서도 노말·레어·에픽 변형이 나온다. 레어는 노말의 0.4배, 에픽은 0.1배 비중(무기 상자 기준 약 67% / 27% / 7%).
	// 에픽 AR70 은 보스(세력 C) 시체에서만 확정적으로 노릴 수 있다 — 보스가 워존에 있으므로 "에픽은 주로 워존" 규칙과 맞는다.
	Spawner.RegisterLootTable(TEXT("LT_Military"), MakeLoot(3, { Entry(TEXT("Ammo_Rifle"), 3, 20, 60), Entry(TEXT("Grenade"), 1, 1, 1), Entry(TEXT("Rifle_AR70"), 0.7f, 1, 1), Entry(TEXT("Rifle_AR70_Rare"), 0.3f, 1, 1), Entry(TEXT("Armor_Vest"), 1, 1, 1), Entry(TEXT("Backpack"), 1, 1, 1), Entry(TEXT("Pants"), 1, 1, 1), Entry(TEXT("Shoes"), 1, 1, 1), Entry(TEXT("Helmet"), 1, 1, 1), Entry(TEXT("ChestPouch"), 1, 1, 1), Entry(TEXT("Holster"), 1, 1, 1) }));
	Spawner.RegisterLootTable(TEXT("LT_Ammo"),     MakeLoot(2, { Entry(TEXT("Ammo_Rifle"), 3, 30, 60), Entry(TEXT("Ammo_Pistol"), 2, 20, 40), Entry(TEXT("Ammo_Shotgun"), 2, 8, 16), Entry(TEXT("Magazine_Rifle"), 1, 1, 2) }));
	Spawner.RegisterLootTable(TEXT("LT_Weapon"),   MakeLoot(1, {
		Entry(TEXT("Rifle_AR70"), 1, 1, 1), Entry(TEXT("Rifle_AK"), 2, 1, 1), Entry(TEXT("Shotgun"), 2, 1, 1), Entry(TEXT("Pistol"), 3, 1, 1), Entry(TEXT("Revolver"), 1, 1, 1),
		Entry(TEXT("Rifle_AR70_Rare"), 0.4f, 1, 1), Entry(TEXT("Rifle_AK_Rare"), 0.8f, 1, 1), Entry(TEXT("Shotgun_Rare"), 0.8f, 1, 1), Entry(TEXT("Pistol_Rare"), 1.2f, 1, 1), Entry(TEXT("Revolver_Rare"), 0.4f, 1, 1),
		Entry(TEXT("Rifle_AR70_Epic"), 0.1f, 1, 1), Entry(TEXT("Rifle_AK_Epic"), 0.2f, 1, 1), Entry(TEXT("Shotgun_Epic"), 0.2f, 1, 1), Entry(TEXT("Pistol_Epic"), 0.3f, 1, 1), Entry(TEXT("Revolver_Epic"), 0.1f, 1, 1) }));
	Spawner.RegisterLootTable(TEXT("LT_Medical"),  MakeLoot(2, { Entry(TEXT("Bandage"), 3, 1, 3), Entry(TEXT("Medkit"), 1, 1, 1), Entry(TEXT("Tourniquet"), 1, 1, 1), Entry(TEXT("Splint"), 1, 1, 1) }));
	Spawner.RegisterLootTable(TEXT("LT_Food"),     MakeLoot(2, { Entry(TEXT("Ration"), 3, 1, 2), Entry(TEXT("Water"), 3, 1, 2) }));
	Spawner.RegisterLootTable(TEXT("LT_Tools"),    MakeLoot(2, { Entry(TEXT("Scrap"), 3, 1, 4), Entry(TEXT("Tool_Cutter"), 1, 1, 1), Entry(TEXT("Fuel"), 1, 1, 1) }));
	// 금고 재화 800~2500: 아이템 가치표(PGItemValue)로 재 보니 300~800 일 때 금고 기대가치 4,200 < 무기 상자 4,967 이었다.
	// 금고가 무기 상자보다 싸면 "잠긴 상자 + 열쇠"(기획 37쪽)를 지나치게 되므로 금고가 최상위가 되도록 올렸다(기대 5,500).
	Spawner.RegisterLootTable(TEXT("LT_Safe"),     MakeLoot(2, { Entry(TEXT("Money"), 2, 800, 2500), Entry(TEXT("RareMaterial"), 1, 1, 1), Entry(TEXT("Key_Special"), 1, 1, 1) }));
	Spawner.RegisterLootTable(TEXT("LT_CorpseA"),  MakeLoot(1, { Entry(TEXT("Scrap"), 3, 1, 2), Entry(TEXT("Ammo_Pistol"), 2, 5, 15), Entry(TEXT("Bandage"), 1, 1, 1) }));
	Spawner.RegisterLootTable(TEXT("LT_CorpseB"),  MakeLoot(2, { Entry(TEXT("Ammo_Rifle"), 3, 10, 30), Entry(TEXT("Medkit"), 1, 1, 1), Entry(TEXT("Money"), 2, 50, 150), Entry(TEXT("Backpack"), 1, 1, 1), Entry(TEXT("Shoes"), 1, 1, 1), Entry(TEXT("Shirt"), 1, 1, 1), Entry(TEXT("Pants"), 1, 1, 1) }));
	Spawner.RegisterLootTable(TEXT("LT_CorpseC"),  MakeLoot(2, { Entry(TEXT("RareMaterial"), 3, 1, 2), Entry(TEXT("Rifle_AR70_Epic"), 1, 1, 1) }));

	// ---- 상자·보관함 (OBJ-001 ~ 011): 전부 AItemContainerActor + 데이터
	{
		FPGObjectCatalogRow R = MakeRow(TEXT("OBJ-001"), TEXT("일반 나무 상자"), A::Container, S::Container, P::P0_Common, true);
		R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Wooden_Crate.Wooden_Crate")); R.LootTableId = TEXT("LT_Wood"); R.SpawnWeight = 4.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-002"), TEXT("군용 보급 상자"), A::Container, S::Container, P::P1_MVP, true);
		R.Mesh = Mesh(TEXT("/Game/Factory_Pack_V1/Meshes/SM_box.SM_box")); R.LootTableId = TEXT("LT_Military"); R.Tier = 2; R.SpawnWeight = 2.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-003"), TEXT("탄약 상자"), A::Container, S::Container, P::P1_MVP, true);
		R.Mesh = Mesh(TEXT("/Game/AE_BR_Props/Models/Ammunition_Box_01_Sm.Ammunition_Box_01_Sm")); R.SecondaryMesh = Mesh(TEXT("/Game/AE_BR_Props/Models/Ammunition_Box_01_Top_SM.Ammunition_Box_01_Top_SM")); R.LootTableId = TEXT("LT_Ammo"); R.SpawnWeight = 3.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-004"), TEXT("무기 상자"), A::Container, S::Container, P::P1_MVP, true);
		R.Mesh = Mesh(TEXT("/Game/AE_BR_Props/Models/Ammunition_Box_04_SM.Ammunition_Box_04_SM")); R.SecondaryMesh = Mesh(TEXT("/Game/AE_BR_Props/Models/Ammunition_Box_04_Top_SM.Ammunition_Box_04_Top_SM")); R.LootTableId = TEXT("LT_Weapon"); R.Tier = 2; R.SpawnWeight = 1.5f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-005"), TEXT("의료품 상자"), A::Container, S::Container, P::P1_MVP, true);
		R.Mesh = Mesh(TEXT("/Game/AE_BR_Props/Models/Medical_Box_SM.Medical_Box_SM")); R.SecondaryMesh = Mesh(TEXT("/Game/AE_BR_Props/Models/Medical_Box_Top_SM.Medical_Box_Top_SM")); R.LootTableId = TEXT("LT_Medical"); R.SpawnWeight = 2.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-006"), TEXT("식량·소비품 상자"), A::Container, S::Container, P::P2_Main, false);
		R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Cardboard_Box_1.Cardboard_Box_1")); R.LootTableId = TEXT("LT_Food"); R.SpawnWeight = 2.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-007"), TEXT("재료·공구 상자"), A::Container, S::Container, P::P2_Main, false);
		R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Utility_Box_1a.Utility_Box_1a")); R.LootTableId = TEXT("LT_Tools"); R.SpawnWeight = 1.5f; Spawner.RegisterCatalogRow(R);
		// OBJ-008 퀘스트 전용 상자: OBJ-087과 중복, 팀 결정 전 검토 필요 → 포함 안 함
		R = MakeRow(TEXT("OBJ-008"), TEXT("퀘스트 전용 상자"), A::Container, S::QuestItem, P::P2_Main, false, false);
		R.LootTableId = TEXT("LT_Wood"); R.bUniquePerMap = true; R.Notes = TEXT("OBJ-087과 중복 등록. 검토 필요"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-009"), TEXT("잠긴 상자"), A::Container, S::Container, P::P0_Common, true);
		R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Wooden_Crate_2.Wooden_Crate_2")); R.LootTableId = TEXT("LT_Military"); R.bLocked = true; R.RequiredKeyId = TEXT("Key_Common"); R.Tier = 2; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-010"), TEXT("금고"), A::Container, S::Safe, P::P1_MVP, true);
		R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Utility_Box_2a.Utility_Box_2a")); R.LootTableId = TEXT("LT_Safe"); R.InteractSeconds = 5.0f; R.Tier = 3; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-011"), TEXT("잠긴 금고"), A::Container, S::Safe, P::P2_Main, false);
		R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Utility_Box_2b.Utility_Box_2b")); R.LootTableId = TEXT("LT_Safe"); R.InteractSeconds = 5.0f; R.bLocked = true; R.RequiredKeyId = TEXT("Key_Special"); R.Tier = 3; Spawner.RegisterCatalogRow(R);
	}

	// ---- 문·통로 (OBJ-012 ~ 026): APGDoorActor + 데이터. 창문(024~026)은 검토 필요 → 포함 안 함
	{
		const TCHAR* FactoryDoor = TEXT("/Game/Factory_Pack_V1/Meshes/SM_Door.SM_Door");
		const TCHAR* CabinDoor = TEXT("/Game/Modular_Rural_Cabin/Meshes/Modular/Door_01.Door_01");
		const TCHAR* Fence = TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Fence_Old_1_2m.Fence_Old_1_2m");
		FPGObjectCatalogRow R = MakeRow(TEXT("OBJ-012"), TEXT("일반 외여닫이문"), A::Door, S::Generic, P::P0_Common, true);
		R.Mesh = Mesh(CabinDoor); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-013"), TEXT("양문형 문"), A::Door, S::Generic, P::P2_Main, false);
		R.Mesh = Mesh(FactoryDoor); R.SecondaryMesh = Mesh(FactoryDoor); R.Motion = EPGDoorMotion::SwingDouble; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-014"), TEXT("미닫이문"), A::Door, S::Generic, P::P2_Main, false);
		R.Mesh = Mesh(FactoryDoor); R.Motion = EPGDoorMotion::Slide; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-015"), TEXT("롤업 셔터"), A::Door, S::Generic, P::P2_Main, false, false); R.Mesh = Mesh(FactoryDoor); R.Motion = EPGDoorMotion::Vertical; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-016"), TEXT("차고 문"), A::Door, S::Generic, P::P2_Main, false, false); R.Mesh = Mesh(FactoryDoor); R.Motion = EPGDoorMotion::Vertical; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-017"), TEXT("잠긴 문"), A::Door, S::Generic, P::P0_Common, true);
		R.Mesh = Mesh(CabinDoor); R.bLocked = true; R.RequiredKeyId = TEXT("Key_Common"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-018"), TEXT("특수 열쇠 문"), A::Door, S::Generic, P::P2_Main, false);
		R.Mesh = Mesh(FactoryDoor); R.bLocked = true; R.RequiredKeyId = TEXT("Key_Special"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-019"), TEXT("일반 펜스 게이트"), A::Door, S::Generic, P::P1_MVP, true);
		R.Mesh = Mesh(Fence); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-020"), TEXT("잠긴 펜스 게이트"), A::Door, S::Generic, P::P1_MVP, true);
		R.Mesh = Mesh(Fence); R.bLocked = true; R.RequiredKeyId = TEXT("Key_Common"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-021"), TEXT("탈출용 문"), A::Door, S::Extraction, P::P1_MVP, true);
		R.Mesh = Mesh(CabinDoor); R.QuestTag = TEXT("Exit_Door"); R.Notes = TEXT("문 뒤에 APGExtractionZoneActor 배치"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-022"), TEXT("탈출용 펜스 게이트"), A::Door, S::Extraction, P::P1_MVP, true);
		R.Mesh = Mesh(Fence); R.QuestTag = TEXT("Exit_Gate"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-023"), TEXT("맨홀·해치"), A::Door, S::Generic, P::P2_Main, false, false); R.Motion = EPGDoorMotion::Hatch; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-024"), TEXT("열리는 창문"), A::Door, S::Generic, P::P3_Extend, false, false); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-025"), TEXT("깨지는 창문"), A::Destructible, S::Generic, P::P3_Extend, false, false); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-026"), TEXT("열고 깨뜨릴 수 있는 창문"), A::Door, S::Generic, P::P3_Extend, false, false); Spawner.RegisterCatalogRow(R);
	}

	// ---- 바닥 아이템 (OBJ-027 ~ 052): APGFloorItemActor + ItemId
	{
		struct FItemSpec { const TCHAR* Id; const TCHAR* Name; const TCHAR* ItemId; int32 Count; S Socket; P Priority; bool bMVP; bool bIncluded; const TCHAR* Mesh; };
		const FItemSpec Specs[] = {
			{ TEXT("OBJ-027"), TEXT("총기"),            TEXT("Rifle_AR70"),      1,  S::Weapon,     P::P1_MVP,  true,  true,  TEXT("/Game/PG/Weapons/SM_PGWFloor_AR70.SM_PGWFloor_AR70") },
			{ TEXT("OBJ-028"), TEXT("활·새총"),         TEXT("Bow"),             1,  S::Weapon,     P::P2_Main, false, false, nullptr }, // 9/17 무기 범위에서 뺌
			{ TEXT("OBJ-029"), TEXT("날붙이 근접무기"), TEXT("Axe"),             1,  S::Weapon,     P::P1_MVP,  true,  false, TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Axe_1.Axe_1") }, // 9/17 무기 범위에서 뺌
			{ TEXT("OBJ-030"), TEXT("둔기 근접무기"),   TEXT("Bat"),             1,  S::Weapon,     P::P1_MVP,  true,  false, TEXT("/Game/Survival_Character/Meshes/Modular_Parts/SM_Survival_Character_Bat.SM_Survival_Character_Bat") }, // 9/17 무기 범위에서 뺌
			{ TEXT("OBJ-031"), TEXT("투척용 칼"),       TEXT("ThrowingKnife"),   3,  S::Weapon,     P::P2_Main, false, false, TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Knive.Knive") }, // 9/17 무기 범위에서 뺌
			{ TEXT("OBJ-032"), TEXT("수류탄"),          TEXT("Grenade"),         1,  S::Consumable, P::P1_MVP,  true,  true,  TEXT("/Game/AE_BR_Props/Models/Dynamite_SM.Dynamite_SM") },
			{ TEXT("OBJ-033"), TEXT("탄약"),            TEXT("Ammo_Rifle"),      30, S::Ammo,       P::P1_MVP,  true,  true,  TEXT("/Game/AE_BR_Props/Models/Cartridges_Arms_01_SM.Cartridges_Arms_01_SM") },
			{ TEXT("OBJ-034"), TEXT("탄창"),            TEXT("Magazine_Rifle"),  1,  S::Ammo,       P::P2_Main, false, true,  TEXT("/Game/AE_BR_Props/Models/Rifles_Ammo_SM.Rifles_Ammo_SM") },
			{ TEXT("OBJ-035"), TEXT("일반 회복 아이템"),TEXT("Bandage"),         2,  S::Consumable, P::P1_MVP,  true,  true,  TEXT("/Game/AE_BR_Props/Models/Medical_Supplies_01_SM.Medical_Supplies_01_SM") },
			{ TEXT("OBJ-036"), TEXT("가벼운 출혈 치료제"), TEXT("Bandage"),      1,  S::Consumable, P::P2_Main, false, true,  TEXT("/Game/AE_BR_Props/Models/Medical_Supplies_02_SM.Medical_Supplies_02_SM") },
			{ TEXT("OBJ-037"), TEXT("과다 출혈 치료제"), TEXT("Tourniquet"),     1,  S::Consumable, P::P2_Main, false, true,  TEXT("/Game/AE_BR_Props/Models/Medical_Supplies_03_SM.Medical_Supplies_03_SM") },
			{ TEXT("OBJ-038"), TEXT("골절 치료제"),     TEXT("Splint"),          1,  S::Consumable, P::P2_Main, false, true,  TEXT("/Game/AE_BR_Props/Models/Medical_Supplies_04_SM.Medical_Supplies_04_SM") },
			{ TEXT("OBJ-039"), TEXT("버프·일반 소비 아이템"), TEXT("Painkiller"), 1, S::Consumable, P::P3_Extend, false, false, TEXT("/Game/AE_BR_Props/Models/Syringe_SM.Syringe_SM") },
			{ TEXT("OBJ-040"), TEXT("모자"),            TEXT("Helmet"),          1,  S::Item,       P::P1_MVP,  true,  true,  TEXT("/Game/PG/Characters/Quantum/FloorMeshes/SM_PGQFloor_Cap.SM_PGQFloor_Cap") },
			{ TEXT("OBJ-041"), TEXT("상의"),            TEXT("Armor_Vest"),      1,  S::Item,       P::P1_MVP,  true,  true,  TEXT("/Game/PG/Characters/Quantum/FloorMeshes/SM_PGQFloor_Vest.SM_PGQFloor_Vest") },
			// 모자·상의(조끼)·하의는 팀 플레이어 캐릭터(Quantum 모듈 캐릭터)의 파츠를 Tools/make_quantum_wearables.py 로 스태틱으로 구운 것 — 주운 것과 입은 모습이 같다.
			// 신발·가방은 그 팩에 없어서 Survival_Character 부품을 구운 것(Tools/make_floor_item_meshes.py). 실제 메시·색은 착장 표(PGWearableColors)가 정한다.
			{ TEXT("OBJ-042"), TEXT("하의"),            TEXT("Pants"),           1,  S::Item,       P::P1_MVP,  true,  true,  TEXT("/Game/PG/Characters/Quantum/FloorMeshes/SM_PGQFloor_Jeans.SM_PGQFloor_Jeans") },
			{ TEXT("OBJ-043"), TEXT("신발"),            TEXT("Shoes"),           1,  S::Item,       P::P1_MVP,  true,  true,  TEXT("/Game/PG/Props/FloorItems/SM_PGFloor_Shoes.SM_PGFloor_Shoes") },
			{ TEXT("OBJ-044"), TEXT("가방"),            TEXT("Backpack"),        1,  S::Item,       P::P1_MVP,  true,  true,  TEXT("/Game/PG/Props/FloorItems/SM_PGFloor_Backpack.SM_PGFloor_Backpack") },
			{ TEXT("OBJ-045"), TEXT("일반 열쇠"),       TEXT("Key_Common"),      1,  S::Key,        P::P1_MVP,  true,  true,  TEXT("/Game/Fab/Old_Rusty_Key/old_rusty_key/StaticMeshes/old_rusty_key.old_rusty_key") },
			{ TEXT("OBJ-046"), TEXT("특수 탈출 아이템"),TEXT("Key_Special"),     1,  S::Key,        P::P2_Main, false, true,  TEXT("/Game/Fab/Old_Rusty_Key/old_rusty_key/StaticMeshes/old_rusty_key.old_rusty_key") },
			{ TEXT("OBJ-047"), TEXT("연료통"),          TEXT("Fuel"),            1,  S::Fuel,       P::P2_Main, false, true,  TEXT("/Game/PG/Props/FuelCan/SM_PGFuelCan_Red.SM_PGFuelCan_Red") },
			{ TEXT("OBJ-048"), TEXT("재화"),            TEXT("Money"),           100,S::Item,       P::P2_Main, false, true,  TEXT("/Game/AE_BR_Props/Models/Money_SM.Money_SM") },
			{ TEXT("OBJ-049"), TEXT("퀘스트 아이템"),   TEXT("QuestItem_Doc"),   1,  S::QuestItem,  P::P2_Main, false, true,  nullptr },
			{ TEXT("OBJ-050"), TEXT("일반 재료"),       TEXT("Scrap"),           2,  S::Item,       P::P2_Main, false, true,  TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Metal_Sheet_1.Metal_Sheet_1") },
			{ TEXT("OBJ-051"), TEXT("희귀 핵심 재료"),  TEXT("RareMaterial"),    1,  S::Item,       P::P2_Main, false, true,  nullptr },
			{ TEXT("OBJ-052"), TEXT("제조 레시피"),     TEXT("Recipe"),          1,  S::Item,       P::Hold,    false, false, nullptr },
		};
		for (const FItemSpec& Spec : Specs)
		{
			FPGObjectCatalogRow R = MakeRow(Spec.Id, Spec.Name, A::FloorItem, Spec.Socket, Spec.Priority, Spec.bMVP, Spec.bIncluded);
			R.ItemId = Spec.ItemId;
			R.ItemCount = Spec.Count;
			if (Spec.Mesh)
				R.Mesh = Mesh(Spec.Mesh);
			if (Spec.Socket == S::QuestItem)
			{
				R.QuestTag = TEXT("Quest_Pickup");
				R.bUniquePerMap = true;
			}
			Spawner.RegisterCatalogRow(R);
		}
	}

	// ---- 시체 (OBJ-053 ~ 056): 컴포넌트 부착이라 스폰 대상이 아니다. 루팅 테이블 이름만 기록.
	{
		FPGObjectCatalogRow R = MakeRow(TEXT("OBJ-053"), TEXT("세력 A 몬스터 시체"), A::Corpse, S::MonsterA, P::P1_MVP, true); R.LootTableId = TEXT("LT_CorpseA"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-054"), TEXT("세력 B 몬스터 시체"), A::Corpse, S::MonsterB, P::P1_MVP, true); R.LootTableId = TEXT("LT_CorpseB"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-055"), TEXT("세력 C 몬스터 시체"), A::Corpse, S::MonsterC, P::P1_MVP, true); R.LootTableId = TEXT("LT_CorpseC"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-056"), TEXT("플레이어 시체"), A::Corpse, S::Generic, P::P3_Extend, false, false); R.Notes = TEXT("PvP/사망 아이템 정책 확정 필요"); Spawner.RegisterCatalogRow(R);
	}

	// ---- 탈출구 (OBJ-057 ~ 068): APGExtractionZoneActor + 데이터
	{
		FPGObjectCatalogRow R = MakeRow(TEXT("OBJ-057"), TEXT("일반 탈출 영역"), A::Extraction, S::Extraction, P::P1_MVP, true); R.SpawnWeight = 3.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-058"), TEXT("대기 시간형 탈출구"), A::Extraction, S::Extraction, P::P1_MVP, true); R.InteractSeconds = 8.0f; R.SpawnWeight = 3.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-059"), TEXT("아이템 요구 탈출구"), A::Extraction, S::Extraction, P::P2_Main, false); R.ItemId = TEXT("Key_Special"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-060"), TEXT("플레이어별 탈출구"), A::Extraction, S::Extraction, P::P1_MVP, true); R.InteractSeconds = 5.0f; R.Notes = TEXT("게임플레이 담당이 SetAllowedPlayerKeys 로 배정"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-061"), TEXT("제한시간 탈출구"), A::Extraction, S::Extraction, P::P3_Extend, false, false); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-062"), TEXT("1회 사용 탈출구"), A::Extraction, S::Extraction, P::P3_Extend, false, false); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-063"), TEXT("열쇠 탈출문"), A::Extraction, S::Extraction, P::P1_MVP, true); R.ItemId = TEXT("Key_Common"); R.Notes = TEXT("OBJ-017 잠긴 문과 조합"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-064"), TEXT("철조망 펜스 탈출구"), A::Extraction, S::Extraction, P::P1_MVP, true); R.Notes = TEXT("OBJ-022 게이트와 조합"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-065"), TEXT("맨홀·해치 탈출구"), A::Extraction, S::Extraction, P::P3_Extend, false, false); Spawner.RegisterCatalogRow(R);
		// 헬기·자동차 탈출구: 메시가 곧 탈것 모양이다(움직이지 않는 세워 둔 헬기·차). 연료통을 갖고 F → 10초/5초 뒤 탈출, 연료통 1개 소모.
		// 맵의 탈출 지점에는 UPGObjectSpawnerSubsystem::SpawnExitSet 이 차·헬기·잠긴 펜스 중 하나를 놓는다(기획서 3.3.2 "시작점/탈출구").
		// 헬기 메시는 Military_Free 로우폴리 헬기에 실사 질감을 입힌 것(Tools/make_realistic_heli.py, 동체 14.7m, 콜리전 상자 7개).
		R = MakeRow(TEXT("OBJ-066"), TEXT("헬기"), A::Extraction, S::Vehicle, P::P2_Main, false);
		R.Mesh = Mesh(TEXT("/Game/PG/Vehicles/Heli/SM_PGV_Helicopter.SM_PGV_Helicopter")); R.ItemId = TEXT("Fuel"); R.InteractSeconds = 10.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-067"), TEXT("선박 탈출구"), A::Extraction, S::Vehicle, P::P2_Main, false); R.ItemId = TEXT("Fuel"); R.InteractSeconds = 10.0f; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-068"), TEXT("자동차"), A::Extraction, S::Vehicle, P::P2_Main, false);
		// 자동차 = Military_Free 험비에 실사 질감(Tools/make_realistic_humvee.py, 4.35m, 사막색). 올리브 인스턴스 MI_PGV_Humvee_Olive 도 있다.
		R.Mesh = Mesh(TEXT("/Game/PG/Vehicles/Humvee/SM_PGV_Humvee.SM_PGV_Humvee")); R.ItemId = TEXT("Fuel"); R.InteractSeconds = 5.0f; Spawner.RegisterCatalogRow(R);
	}

	// ---- NPC·서비스 (OBJ-078 ~ 085)
	{
		FPGObjectCatalogRow R = MakeRow(TEXT("OBJ-078"), TEXT("상점 NPC 상호작용"), A::Service, S::Shop, P::P1_MVP, true); R.bUniquePerMap = true; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-079"), TEXT("상점 창구"), A::Service, S::Shop, P::P1_MVP, true); R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Mailbox_Pole.Mailbox_Pole")); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-081"), TEXT("택배 NPC 상호작용"), A::Service, S::Courier, P::P2_Main, false); R.bUniquePerMap = true; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-082"), TEXT("택배 접수 창구"), A::Service, S::Courier, P::P2_Main, false); R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Mailbox_Pole.Mailbox_Pole")); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-084"), TEXT("제조 NPC 상호작용"), A::Service, S::Shop, P::Hold, false, false); R.Notes = TEXT("재승인 전 구현 금지"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-085"), TEXT("제조 창구"), A::Service, S::Shop, P::Hold, false, false); Spawner.RegisterCatalogRow(R);
	}

	// ---- 퀘스트 (OBJ-086 ~ 095)
	{
		FPGObjectCatalogRow R = MakeRow(TEXT("OBJ-086"), TEXT("퀘스트 아이템"), A::FloorItem, S::QuestItem, P::P2_Main, false); R.ItemId = TEXT("QuestItem_Doc"); R.QuestTag = TEXT("Quest_Pickup"); R.bUniquePerMap = true; Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-087"), TEXT("퀘스트 전용 상자"), A::Container, S::QuestItem, P::P2_Main, false, false); R.Notes = TEXT("OBJ-008과 중복. 팀 결정 대기"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-088"), TEXT("조사 대상 물체"), A::QuestObject, S::QuestItem, P::P3_Extend, false, false); R.InteractSeconds = 3.0f; R.QuestTag = TEXT("Quest_Investigate"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-089"), TEXT("작동 장치"), A::QuestObject, S::QuestItem, P::P2_Main, false); R.QuestTag = TEXT("Quest_Operate"); R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Utility_Box_1b.Utility_Box_1b")); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-090"), TEXT("아이템 납품함"), A::QuestObject, S::QuestItem, P::P2_Main, false); R.ItemId = TEXT("QuestItem_Doc"); R.ItemCount = 1; R.QuestTag = TEXT("Quest_Deliver"); R.Mesh = Mesh(TEXT("/Game/Modular_Rural_Cabin/Meshes/Props/Mailbox_Pole.Mailbox_Pole")); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-091"), TEXT("NPC 전달 지점"), A::QuestObject, S::QuestItem, P::P2_Main, false); R.ItemId = TEXT("QuestItem_Doc"); R.QuestTag = TEXT("Quest_DeliverNPC"); R.Notes = TEXT("NPC Actor 옆에 배치"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-092"), TEXT("위치 방문 Trigger"), A::QuestObject, S::QuestItem, P::P3_Extend, false, false); R.Notes = TEXT("APGQuestTriggerVolume"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-093"), TEXT("특정 문 이용 목표"), A::Door, S::Generic, P::P2_Main, false, false); R.Notes = TEXT("문의 QuestTag + OnDoorUsed 로 처리, 별도 스폰 없음"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-094"), TEXT("특정 차량 이용 목표"), A::Extraction, S::Vehicle, P::P2_Main, false, false); R.Notes = TEXT("탈출구의 QuestTag + OnExtractionCompleted 로 처리"); Spawner.RegisterCatalogRow(R);
		R = MakeRow(TEXT("OBJ-095"), TEXT("퀘스트 목표 표시기"), A::None, S::Generic, P::P2_Main, false, false); R.Notes = TEXT("UI 담당. 오브젝트는 위치·QuestTag만 제공"); Spawner.RegisterCatalogRow(R);
	}

	// ---- 작동 장치 (OBJ-096 ~ 107): 전부 검토 필요 → 원형은 있지만 포함 안 함
	{
		const TCHAR* Ids[] = { TEXT("OBJ-096"), TEXT("OBJ-097"), TEXT("OBJ-098"), TEXT("OBJ-099"), TEXT("OBJ-100"), TEXT("OBJ-101"), TEXT("OBJ-102"), TEXT("OBJ-103"), TEXT("OBJ-104"), TEXT("OBJ-105"), TEXT("OBJ-106"), TEXT("OBJ-107") };
		const TCHAR* Names[] = { TEXT("스위치"), TEXT("레버"), TEXT("버튼"), TEXT("발전기"), TEXT("차단기"), TEXT("퓨즈 박스"), TEXT("키패드"), TEXT("카드 리더기"), TEXT("문 제어 패널"), TEXT("경보 장치"), TEXT("시간 제한 스위치"), TEXT("압력판") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index)
		{
			FPGObjectCatalogRow R = MakeRow(Ids[Index], Names[Index], A::Device, S::Generic, P::P3_Extend, false, false);
			R.Notes = TEXT("현재 MVP 제외, 팀 승인 후 bIncluded 켜기");
			Spawner.RegisterCatalogRow(R);
		}
	}

	// ---- 파괴 오브젝트 (OBJ-108 ~ 115): 검토 필요 → 포함 안 함
	{
		const TCHAR* Ids[] = { TEXT("OBJ-108"), TEXT("OBJ-109"), TEXT("OBJ-110"), TEXT("OBJ-111"), TEXT("OBJ-112"), TEXT("OBJ-113"), TEXT("OBJ-114"), TEXT("OBJ-115") };
		const TCHAR* Names[] = { TEXT("깨지는 유리창"), TEXT("파괴 가능한 판자문"), TEXT("파괴 가능한 바리케이드"), TEXT("약한 엄폐물"), TEXT("폭발로 제거하는 장애물"), TEXT("도구로 제거하는 장애물"), TEXT("퀘스트 파괴 대상"), TEXT("폭발성 연료통") };
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Ids); ++Index)
		{
			FPGObjectCatalogRow R = MakeRow(Ids[Index], Names[Index], A::Destructible, S::Generic, P::P3_Extend, false, false);
			if (Index == 5) R.ItemId = TEXT("Tool_Cutter");
			R.Notes = TEXT("현재 MVP 제외, 팀 승인 후 bIncluded 켜기");
			Spawner.RegisterCatalogRow(R);
		}
	}

	UE_LOG(LogPGObjects, Display, TEXT("Default object catalog registered: rows=%d"), Spawner.GetCatalogCount());
}

// ---------------------------------------------------------------------------
// Smoke test

namespace
{
	struct FSmokeContext
	{
		UWorld* World = nullptr;
		UPGObjectSpawnerSubsystem* Spawner = nullptr;
		UPGSmokeTestListener* Listener = nullptr;
		APGObjectTestPawn* PawnA = nullptr;
		APGObjectTestPawn* PawnB = nullptr;
		TArray<AActor*> Cleanup;
		int32 Passed = 0;
		int32 Failed = 0;

		void Check(const TCHAR* Name, bool bCondition, const FString& Detail = FString())
		{
			if (bCondition) ++Passed; else ++Failed;
			UE_LOG(LogPGObjects, Display, TEXT("PGObjectSmokeTest: %s pass=%s %s"), Name, bCondition ? TEXT("true") : TEXT("false"), *Detail);
		}

		template <typename T>
		T* Spawn(const FVector& Location)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			T* Actor = World->SpawnActor<T>(T::StaticClass(), Location, FRotator::ZeroRotator, Params);
			if (Actor) Cleanup.Add(Actor);
			return Actor;
		}

		AActor* SpawnCatalog(const TCHAR* ObjectId, const FVector& Location, int64 Seed = 1)
		{
			AActor* Actor = Spawner->SpawnFromCatalog(ObjectId, FTransform(Location), Seed);
			if (Actor) Cleanup.Add(Actor);
			return Actor;
		}

		bool Interact(AActor* Target, APawn* Pawn)
		{
			if (!IsValid(Target) || !Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()))
				return false;
			if (!IInteractable::Execute_CanInteract(Target, Pawn))
				return false;
			IInteractable::Execute_Interact(Target, Pawn);
			return true;
		}

		bool CanInteract(AActor* Target, APawn* Pawn)
		{
			return IsValid(Target) && Target->GetClass()->ImplementsInterface(UInteractable::StaticClass()) && IInteractable::Execute_CanInteract(Target, Pawn);
		}
	};

	// 아이템 가치표(PGItemValue) 검사. 액터를 안 쓰는 순수 검사라 헤드리스에서도 그대로 돈다.
	void TestItemValues(FSmokeContext& C)
	{
		// 기준점. 이 한 줄이 무너지면 표 전체의 단위가 무너진다(기획 21쪽: 재화도 아이템).
		C.Check(TEXT("item_value_money_anchor"), UPGItemValueLibrary::GetItemValue(TEXT("Money")) == 1);

		// 표를 어디서 읽었나. 예비 표(Fallback)로 돌고 있으면 CSV 가 깨졌다는 뜻이다 — CSV 를 고쳐도 게임에 반영이 안 되는 상태.
		const FString TableSource = UPGItemValueLibrary::GetTableSource();
		C.Check(TEXT("item_table_loaded_from_data"), TableSource != TEXT("Fallback"), TableSource);

		// 값과 등급을 따로 적어 두었으니 서로 어긋나지 않는지 기계가 본다.
		// 무기가 아닌 아이템은 값 구간(노말 1~999 / 레어 1000~2999 / 에픽 3000~)을 따른다.
		// 무기는 따로 본다(아래 item_grade_weapon_tiers) — 노말 AR70 이 에픽 권총보다 비쌀 수 있어서 구간이 안 맞는 게 정상이다.
		FName BandOffender;
		int32 BandOffenderValue = 0;
		for (const FPGItemValueRow& Row : UPGItemValueLibrary::GetAllRows())
		{
			if (!Row.bTradable || Row.Category == EPGItemCategory::Weapon)
				continue; // 거래 금지 행은 값이 0이라 구간 밖이 정상이다
			int32 Min = 0, Max = 0;
			UPGItemValueLibrary::GetGradeRange(Row.Grade, Min, Max);
			if (Row.BaseValue < Min || Row.BaseValue > Max)
			{
				BandOffender = Row.ItemId;
				BandOffenderValue = Row.BaseValue;
				break;
			}
		}
		C.Check(TEXT("item_value_grade_bands"), BandOffender.IsNone(),
			FString::Printf(TEXT("offender=%s value=%d"), *BandOffender.ToString(), BandOffenderValue));

		// 총기 사다리. 순서가 뒤집히면 "왜 이 총이 저 총보다 비싼가"라는 설명이 거짓이 된다.
		const int32 Ar70 = UPGItemValueLibrary::GetItemValue(TEXT("Rifle_AR70"));
		const int32 Ak = UPGItemValueLibrary::GetItemValue(TEXT("Rifle_AK"));
		const int32 Shotgun = UPGItemValueLibrary::GetItemValue(TEXT("Shotgun"));
		const int32 Revolver = UPGItemValueLibrary::GetItemValue(TEXT("Revolver"));
		const int32 Pistol = UPGItemValueLibrary::GetItemValue(TEXT("Pistol"));
		C.Check(TEXT("item_value_weapon_ladder"), Ar70 > Ak && Ak > Shotgun && Shotgun > Revolver && Revolver > Pistol,
			FString::Printf(TEXT("ar70=%d ak=%d shotgun=%d revolver=%d pistol=%d"), Ar70, Ak, Shotgun, Revolver, Pistol));

		// 무기 등급: 같은 총이면 노말 < 레어 < 에픽 값이고, 레어·에픽도 바닥 메시·이름·탄이 있어야 한다.
		// 하나라도 빠지면 "주웠는데 안 보인다 / ItemId 가 그대로 보인다 / 총 옆에 탄이 안 나온다" 가 된다.
		FString TierProblem;
		for (const FPGItemValueRow& Row : UPGItemValueLibrary::GetAllRows())
		{
			if (Row.Category != EPGItemCategory::Weapon || Row.BaseItemId == Row.ItemId || Row.BaseItemId.IsNone())
				continue;
			const FPGItemValueRow* Base = UPGItemValueLibrary::FindRow(Row.BaseItemId);
			if (!Base || Base->Grade != EPGItemGrade::Normal || Row.Grade == EPGItemGrade::Normal || Row.BaseValue <= Base->BaseValue)
				TierProblem = FString::Printf(TEXT("%s value/grade vs base %s"), *Row.ItemId.ToString(), *Row.BaseItemId.ToString());
			else if (UPGWearableColorLibrary::FindItemFloorMesh(Row.ItemId).IsNull())
				TierProblem = FString::Printf(TEXT("%s has no floor mesh"), *Row.ItemId.ToString());
			else if (UPGWearableColorLibrary::GetItemDisplayName(Row.ItemId).ToString() == Row.ItemId.ToString())
				TierProblem = FString::Printf(TEXT("%s has no display name"), *Row.ItemId.ToString());
			else if (Row.AmmoItemId != Base->AmmoItemId)
				TierProblem = FString::Printf(TEXT("%s ammo %s != base %s"), *Row.ItemId.ToString(), *Row.AmmoItemId.ToString(), *Base->AmmoItemId.ToString());
			if (!TierProblem.IsEmpty())
				break;
		}
		const int32 PistolEpic = UPGItemValueLibrary::GetItemValue(TEXT("Pistol_Epic"));
		const int32 PistolRare = UPGItemValueLibrary::GetItemValue(TEXT("Pistol_Rare"));
		C.Check(TEXT("item_grade_weapon_tiers"), TierProblem.IsEmpty() && PistolEpic > PistolRare && PistolRare > Pistol
			&& UPGItemValueLibrary::GetItemGrade(TEXT("Pistol_Epic")) == EPGItemGrade::Epic
			&& UPGItemValueLibrary::GetBaseItemId(TEXT("Pistol_Epic")) == FName(TEXT("Pistol")),
			TierProblem.IsEmpty() ? FString::Printf(TEXT("pistol %d < %d < %d"), Pistol, PistolRare, PistolEpic) : TierProblem);

		// 색 변형은 행이 없다. 원래색 값을 물려받아야 상점이 검정 바지를 공짜로 팔지 않는다.
		C.Check(TEXT("item_value_color_variant_inherits"),
			UPGItemValueLibrary::GetItemValue(TEXT("Pants_Black")) == UPGItemValueLibrary::GetItemValue(TEXT("Pants"))
			&& UPGItemValueLibrary::GetItemValue(TEXT("Pants")) > 0);

		// 루팅 테이블에 값 없는 아이템이 있으면 상자에서 나온 것이 상점에서 0원이 된다. 새 아이템을 넣고 표를 빠뜨리면 여기서 걸린다.
		TArray<FName> TableIds;
		C.Spawner->GetAllLootTableIds(TableIds);
		FName MissingValue;
		for (const FName& TableId : TableIds)
		{
			const FPGLootTableRow* Table = C.Spawner->FindLootTable(TableId);
			if (!Table)
				continue;
			for (const FPGLootEntry& Entry : Table->Entries)
				if (!Entry.ItemId.IsNone() && UPGItemValueLibrary::FindRow(Entry.ItemId) == nullptr)
				{
					MissingValue = Entry.ItemId;
					break;
				}
			if (!MissingValue.IsNone())
				break;
		}
		C.Check(TEXT("item_value_covers_loot_tables"), MissingValue.IsNone(),
			FString::Printf(TEXT("tables=%d missing=%s"), TableIds.Num(), *MissingValue.ToString()));

		// 값은 시드로 고정된다. 서버가 같은 시드로 다시 계산해 클라이언트가 보낸 거래를 검증할 수 있어야 한다.
		const int64 ShopSeed = 987654321LL;
		const int32 Buy = UPGItemValueLibrary::GetBuyPrice(TEXT("Medkit"), ShopSeed);
		const int32 BuyAgain = UPGItemValueLibrary::GetBuyPrice(TEXT("Medkit"), ShopSeed);
		const int32 Sell = UPGItemValueLibrary::GetSellPrice(TEXT("Medkit"), ShopSeed);
		const int32 Base = UPGItemValueLibrary::GetItemValue(TEXT("Medkit"));
		C.Check(TEXT("item_price_deterministic"), Buy == BuyAgain && Buy != 0,
			FString::Printf(TEXT("buy=%d again=%d"), Buy, BuyAgain));
		// 기획 14쪽: 구매가 x1.1~1.5 / 판매가 x0.6~0.8. 상점은 항상 싸게 사서 비싸게 판다.
		C.Check(TEXT("item_price_spread"), Sell < Base && Base < Buy,
			FString::Printf(TEXT("sell=%d base=%d buy=%d"), Sell, Base, Buy));
		// 다른 상점(다른 시드)이면 값이 달라야 플레이어가 상점을 고를 이유가 생긴다.
		C.Check(TEXT("item_price_varies_by_shop"),
			UPGItemValueLibrary::GetBuyPrice(TEXT("Medkit"), ShopSeed + 1) != Buy,
			FString::Printf(TEXT("other=%d"), UPGItemValueLibrary::GetBuyPrice(TEXT("Medkit"), ShopSeed + 1)));

		// 거래 판정. 판 가치 > 산 가치일 때만 성립하고 차액이 재화로 돌아온다(기획 14쪽).
		TArray<FPGItemStack> Sold;
		Sold.Add({ FName(TEXT("Rifle_AR70")), 1 });
		TArray<FPGItemStack> Bought;
		Bought.Add({ FName(TEXT("Bandage")), 3 });
		const FPGTradeResult Good = UPGItemValueLibrary::EvaluateTrade(Sold, Bought, ShopSeed);
		C.Check(TEXT("item_trade_surplus_valid"),
			Good.bValid && Good.Difference == Good.SoldValue - Good.BoughtValue && Good.Difference > 0,
			FString::Printf(TEXT("sold=%d bought=%d diff=%d"), Good.SoldValue, Good.BoughtValue, Good.Difference));

		// 판 것보다 비싼 것을 사려 하면 거래가 성립하지 않는다(외상 금지).
		const FPGTradeResult Deficit = UPGItemValueLibrary::EvaluateTrade(Bought, Sold, ShopSeed);
		C.Check(TEXT("item_trade_deficit_invalid"), !Deficit.bValid && Deficit.Difference < 0,
			FString::Printf(TEXT("diff=%d"), Deficit.Difference));

		// 퀘스트 아이템이 섞이면 어느 쪽이든 거래 전체가 막힌다. 팔리면 퀘스트가 진행 불가가 된다.
		TArray<FPGItemStack> WithQuest;
		WithQuest.Add({ FName(TEXT("QuestItem_Doc")), 1 });
		const FPGTradeResult Rejected = UPGItemValueLibrary::EvaluateTrade(WithQuest, Bought, ShopSeed);
		C.Check(TEXT("item_trade_rejects_quest_item"),
			!Rejected.bValid && Rejected.RejectedItemId == FName(TEXT("QuestItem_Doc")));

		// 소지품 전체 가치. 상점이 무엇을 진열할지 정하는 입력이다(기획 30쪽).
		TArray<FPGItemStack> Bag;
		Bag.Add({ FName(TEXT("Money")), 500 });
		Bag.Add({ FName(TEXT("Ammo_Rifle")), 30 });
		const int32 Expected = 500 * 1 + 30 * UPGItemValueLibrary::GetItemValue(TEXT("Ammo_Rifle"));
		C.Check(TEXT("item_value_total_of_bag"), UPGItemValueLibrary::GetTotalValue(Bag) == Expected,
			FString::Printf(TEXT("total=%d expected=%d"), UPGItemValueLibrary::GetTotalValue(Bag), Expected));
	}

	void TestLootDeterminism(FSmokeContext& C)
	{
		const TArray<FPGItemStack> First = C.Spawner->RollLoot(TEXT("LT_Wood"), 1234);
		const TArray<FPGItemStack> Second = C.Spawner->RollLoot(TEXT("LT_Wood"), 1234);
		bool bSame = First.Num() == Second.Num() && First.Num() > 0;
		for (int32 Index = 0; bSame && Index < First.Num(); ++Index)
			bSame = First[Index].ItemId == Second[Index].ItemId && First[Index].Count == Second[Index].Count;
		C.Check(TEXT("loot_roll_deterministic"), bSame, FString::Printf(TEXT("stacks=%d"), First.Num()));
	}

	void TestContainer(FSmokeContext& C)
	{
		AItemContainerActor* Box = Cast<AItemContainerActor>(C.SpawnCatalog(TEXT("OBJ-001"), FVector(0, 0, 0), 99));
		if (!Box) { C.Check(TEXT("container_spawn"), false); return; }
		C.Check(TEXT("container_spawn"), Box->GetObjectId() == TEXT("OBJ-001"));
		C.Check(TEXT("container_can_open"), C.CanInteract(Box, C.PawnA));
		// 1) 열기: 내용물은 인벤토리가 아니라 상자 칸(Storage)에 남는다.
		C.Interact(Box, C.PawnA);
		const int32 Rolled = Box->GetLastLoot().Num();
		C.Check(TEXT("container_opened_into_storage"), Box->IsOpen() && Rolled > 0 && Box->GetStorage()->GetContents().Num() == Rolled, FString::Printf(TEXT("stacks=%d"), Rolled));
		// 2) 첫 칸 하나만 집기(숫자키/UI 경로).
		const FPGItemStack First = Box->GetStorage()->GetContents()[0];
		C.Check(TEXT("container_take_one"), Box->GetStorage()->TakeItem(C.PawnA, 0) && C.PawnA->GetCount(First.ItemId) >= First.Count && Box->GetStorage()->GetContents().Num() == Rolled - 1);
		// 3) 다시 F: 남은 것 전부.
		if (Box->GetStorage()->GetContents().Num() > 0)
			C.Interact(Box, C.PawnA);
		bool bAllGiven = true;
		for (const FPGItemStack& Stack : Box->GetLastLoot())
			bAllGiven = bAllGiven && C.PawnA->GetCount(Stack.ItemId) >= Stack.Count;
		C.Check(TEXT("container_take_all_rest"), bAllGiven && Box->GetStorage()->GetContents().Num() == 0);
		C.Check(TEXT("container_empty_not_interactable"), !C.CanInteract(Box, C.PawnB));

		// 가방이 꽉 찬 플레이어: 상자는 열리되 아이템은 안 넘어간다 (로그만). 상태 변화는 서버 규칙대로.
		AItemContainerActor* Box2 = Cast<AItemContainerActor>(C.SpawnCatalog(TEXT("OBJ-003"), FVector(200, 0, 0), 5));
		UStaticMeshComponent* Lid = Box2 ? Box2->GetLid() : nullptr;
		const bool bHasLid = IsValid(Lid) && IsValid(Lid->GetStaticMesh());
		const FBoxSphereBounds ClosedLid = bHasLid ? Lid->Bounds : FBoxSphereBounds();
		C.PawnB->bRefuseItems = true;
		C.Interact(Box2, C.PawnB);
		C.Check(TEXT("container_refused_inventory_keeps_pawn_empty"), Box2 && Box2->IsOpen() && C.PawnB->GetCount(TEXT("Ammo_Rifle")) == 0);
		C.PawnB->bRefuseItems = false;

		// 뚜껑은 경첩을 축으로 위로 열려야 한다: 중심이 올라가고, 상자 옆을 크게 벗어나 허공으로 날아가지 않는다.
		if (bHasLid)
		{
			Box2->Tick(1.0f);
			const FBoxSphereBounds OpenLid = Lid->Bounds;
			const double Rise = OpenLid.Origin.Z - ClosedLid.Origin.Z;
			const double Drift2D = FVector::Dist2D(OpenLid.Origin, ClosedLid.Origin);
			const double MaxDrift = ClosedLid.BoxExtent.GetMax() * 1.5;
			C.Check(TEXT("container_lid_opens_up_on_hinge"), Rise > 1.0 && Drift2D <= MaxDrift,
				FString::Printf(TEXT("rise=%.1f drift=%.1f max=%.1f"), Rise, Drift2D, MaxDrift));
		}
		else
			C.Check(TEXT("container_lid_opens_up_on_hinge"), false, TEXT("OBJ-003 lid mesh missing"));
	}

	void TestLockedContainer(FSmokeContext& C)
	{
		AItemContainerActor* Box = Cast<AItemContainerActor>(C.SpawnCatalog(TEXT("OBJ-009"), FVector(400, 0, 0), 7));
		if (!Box) { C.Check(TEXT("locked_container_spawn"), false); return; }
		C.Check(TEXT("locked_container_blocks_without_key"), !C.CanInteract(Box, C.PawnA) && Box->GetLock()->IsLocked());
		IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("Key_Common"), 1);
		C.Check(TEXT("locked_container_allows_with_key"), C.CanInteract(Box, C.PawnA));
		C.Interact(Box, C.PawnA);
		C.Check(TEXT("locked_container_opened_key_kept"), Box->IsOpen() && !Box->GetLock()->IsLocked() && C.PawnA->GetCount(TEXT("Key_Common")) == 1);
	}

	void TestUseLock(FSmokeContext& C)
	{
		AItemContainerActor* Safe = Cast<AItemContainerActor>(C.SpawnCatalog(TEXT("OBJ-010"), FVector(600, 0, 0), 3));
		if (!Safe) { C.Check(TEXT("safe_spawn"), false); return; }
		C.Check(TEXT("safe_requires_hold"), Safe->GetInteractSeconds() >= 5.0f);
		C.Check(TEXT("use_lock_acquire"), Safe->TryBeginUse(C.PawnA) && Safe->IsInUse());
		C.Check(TEXT("use_lock_blocks_other"), !C.CanInteract(Safe, C.PawnB) && !Safe->TryBeginUse(C.PawnB));
		Safe->EndUse(C.PawnB);
		C.Check(TEXT("use_lock_not_released_by_other"), Safe->IsInUse());
		Safe->EndUse(C.PawnA);
		C.Check(TEXT("use_lock_released"), !Safe->IsInUse() && C.CanInteract(Safe, C.PawnB));
	}

	void TestDoor(FSmokeContext& C)
	{
		APGDoorActor* Door = Cast<APGDoorActor>(C.SpawnCatalog(TEXT("OBJ-012"), FVector(0, 400, 0)));
		if (!Door) { C.Check(TEXT("door_spawn"), false); return; }
		Door->OnDoorUsed.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleDoorUsed);
		C.Interact(Door, C.PawnA);
		C.Check(TEXT("door_opens"), Door->IsOpen());
		C.Interact(Door, C.PawnA);
		C.Check(TEXT("door_closes"), !Door->IsOpen() && C.Listener->DoorUsed == 2);

		APGDoorActor* Locked = Cast<APGDoorActor>(C.SpawnCatalog(TEXT("OBJ-017"), FVector(200, 400, 0)));
		C.Check(TEXT("locked_door_blocks"), Locked && !C.CanInteract(Locked, C.PawnB));
		IPGItemReceiver::Execute_ReceiveItem(C.PawnB, TEXT("Key_Common"), 1);
		C.Interact(Locked, C.PawnB);
		C.Check(TEXT("locked_door_opens_with_key"), Locked && Locked->IsOpen());

		// 장치 → 문. 제어 패널이 잠긴 문도 연다.
		APGDoorActor* Remote = Cast<APGDoorActor>(C.SpawnCatalog(TEXT("OBJ-018"), FVector(400, 400, 0)));
		APGDeviceActor* Panel = C.Spawn<APGDeviceActor>(FVector(400, 600, 0));
		if (Remote && Panel)
		{
			Panel->AddLinkedTarget(Remote);
			C.Interact(Panel, C.PawnA);
			const bool bOpened = Panel->IsOn() && Remote->IsOpen();
			C.Interact(Panel, C.PawnA);
			C.Check(TEXT("device_toggles_linked_door"), bOpened && !Panel->IsOn() && !Remote->IsOpen());
		}
		else
			C.Check(TEXT("device_toggles_linked_door"), false);

		// 아이템이 필요한 장치 (퓨즈 박스): 아이템 없으면 거부, 넣으면 소모 후 작동.
		APGDeviceActor* Fuse = C.Spawn<APGDeviceActor>(FVector(600, 600, 0));
		if (Fuse)
		{
			Fuse->SetRequiredItem(TEXT("Fuse"), true);
			const bool bBlocked = !C.CanInteract(Fuse, C.PawnA);
			IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("Fuse"), 1);
			C.Interact(Fuse, C.PawnA);
			C.Check(TEXT("device_required_item_consumed"), bBlocked && Fuse->IsOn() && C.PawnA->GetCount(TEXT("Fuse")) == 0);
		}
	}

	void TestFloorItem(FSmokeContext& C)
	{
		APGFloorItemActor* Ammo = Cast<APGFloorItemActor>(C.SpawnCatalog(TEXT("OBJ-033"), FVector(0, 800, 0)));
		if (!Ammo) { C.Check(TEXT("floor_item_spawn"), false); return; }
		const int32 Before = C.PawnA->GetCount(TEXT("Ammo_Rifle"));
		C.Interact(Ammo, C.PawnA);
		C.Check(TEXT("floor_item_picked_and_removed"), C.PawnA->GetCount(TEXT("Ammo_Rifle")) == Before + 30 && (!IsValid(Ammo) || Ammo->IsActorBeingDestroyed()));

		APGFloorItemActor* Drop = APGFloorItemActor::SpawnDrop(C.World, TEXT("Money"), 120, FTransform(FVector(200, 800, 0)));
		if (Drop) C.Cleanup.Add(Drop);
		C.Check(TEXT("floor_item_spawn_drop"), Drop && Drop->GetItemId() == TEXT("Money") && Drop->GetCount() == 120);

		// 가방이 꽉 차면 바닥에 남는다.
		APGFloorItemActor* Stuck = APGFloorItemActor::SpawnDrop(C.World, TEXT("Helmet"), 1, FTransform(FVector(400, 800, 0)));
		if (Stuck) C.Cleanup.Add(Stuck);
		C.PawnB->bRefuseItems = true;
		C.Interact(Stuck, C.PawnB);
		C.PawnB->bRefuseItems = false;
		C.Check(TEXT("floor_item_stays_when_refused"), Stuck && IsValid(Stuck) && !Stuck->IsActorBeingDestroyed());
	}

	// 겹친 상호작용 대상(기획 3.3.7): 붙어 있는 두 아이템이 둘 다 후보로 잡히는지, 휠로 대상이 바뀌고 한 바퀴 돌면 돌아오는지.
	void TestInteractionCandidates(FSmokeContext& C)
	{
		AActor* Pants = C.SpawnCatalog(TEXT("OBJ-042"), FVector(0, 5000, 0));
		AActor* Shoes = C.SpawnCatalog(TEXT("OBJ-043"), FVector(70, 5000, 0));
		UPGInteractionComponent* Interaction = NewObject<UPGInteractionComponent>(C.PawnA);
		if (!Pants || !Shoes || !Interaction) { C.Check(TEXT("interaction_candidates_setup"), false); return; }
		Interaction->RegisterComponent();
#if WITH_EDITOR
		// 에디터 빌드는 스태틱 메시를 백그라운드에서 컴파일하고, 끝나기 전에는 컴포넌트의 물리 몸(충돌)이 안 만들어진다.
		// 이 테스트만 실제 트레이스를 쏘므로 방금 로드한 메시의 컴파일을 기다린다. (PIE 는 시작할 때 엔진이 알아서 기다린다.)
		FAssetCompilingManager::Get().FinishAllCompilation();
#endif

		// 바지를 비스듬히 내려다본다. 신발은 시선이 닿은 지점에서 60cm 쯤 옆.
		const FVector Eye(-200, 5000, 120);
		const FVector Look = (FVector(0, 5000, 10) - Eye).GetSafeNormal();
		AActor* First = Interaction->RefreshTargetFromView(Eye, Look);
		const TArray<AActor*> Found = Interaction->GetCandidates();
		FHitResult Probe;
		C.World->LineTraceSingleByChannel(Probe, Eye, Eye + Look * 300.0f, ECC_Visibility);
		const UStaticMeshComponent* PantsMesh = Pants->FindComponentByClass<UStaticMeshComponent>();
		C.Check(TEXT("interaction_candidates_found"), First == Pants && Found.Contains(Pants) && Found.Contains(Shoes) && Interaction->GetSelectedIndex() == 0,
			FString::Printf(TEXT("n=%d first=%s probe=%s mesh=%s collision=%d bounds=%s"), Found.Num(), *GetNameSafe(First), *GetNameSafe(Probe.GetActor()),
				*GetNameSafe(PantsMesh ? PantsMesh->GetStaticMesh() : nullptr), PantsMesh ? static_cast<int32>(PantsMesh->GetCollisionEnabled()) : -1,
				PantsMesh ? *PantsMesh->Bounds.GetBox().ToString() : TEXT("-")));

		Interaction->CycleTarget(+1);
		const AActor* Second = Interaction->GetCurrentTarget();
		// 고른 대상은 다시 훑어도 유지돼야 한다(순서가 아니라 액터로 기억).
		const AActor* Kept = Interaction->RefreshTargetFromView(Eye, Look);
		for (int32 I = 1; I < Found.Num(); ++I)
			Interaction->CycleTarget(+1);
		C.Check(TEXT("interaction_wheel_cycles"), Second != Pants && Second != nullptr && Kept == Second && Interaction->GetCurrentTarget() == Pants,
			FString::Printf(TEXT("second=%s"), *GetNameSafe(Second)));
		Interaction->DestroyComponent();
	}

	// 옷 색 변형: 표의 에셋이 실제로 있는지, 색 고르기가 시드로 고정되는지, 버린 옷이 색을 입는지.
	void TestWearableColors(FSmokeContext& C)
	{
		bool bTableOk = true;
		for (const TCHAR* Base : { TEXT("Shirt"), TEXT("Pants"), TEXT("Armor_Vest"), TEXT("Helmet"), TEXT("ChestPouch"), TEXT("Holster"), TEXT("Backpack"), TEXT("Shoes") })
		{
			const TArray<FPGWearableColor> Colors = UPGWearableColorLibrary::GetColorsForBase(Base);
			bTableOk &= Colors.Num() >= 2;
			for (const FPGWearableColor& Color : Colors)
			{
				// 입었을 때 메시는 있는 슬롯만(신발은 겉모습 없음). 경로가 적혀 있으면 실제로 로드돼야 한다.
				const bool bWornOk = (Color.WornSkeletalMesh.IsNull() || Color.WornSkeletalMesh.LoadSynchronous() != nullptr)
					&& (Color.WornStaticMesh.IsNull() || Color.WornStaticMesh.LoadSynchronous() != nullptr);
				const bool bAssets = Color.Material.LoadSynchronous() != nullptr && Color.FloorMesh.LoadSynchronous() != nullptr && bWornOk && Color.Slot != EPGWearSlot::None;
				if (!bAssets)
					UE_LOG(LogPGObjects, Warning, TEXT("PGObjectSmokeTest: wearable %s missing material or mesh"), *Color.ItemId.ToString());
				bTableOk &= bAssets;
			}
		}
		const TSoftObjectPtr<UStaticMesh> PistolFloor = UPGWearableColorLibrary::FindItemFloorMesh(TEXT("Pistol"));
		if (PistolFloor.IsNull() || !PistolFloor.LoadSynchronous())
		{
			UE_LOG(LogPGObjects, Warning, TEXT("PGObjectSmokeTest: pistol floor mesh missing"));
			bTableOk = false;
		}
		C.Check(TEXT("wearable_color_table_assets"), bTableOk);

		const FName First = UPGWearableColorLibrary::PickColorVariant(TEXT("Pants"), 42);
		FPGWearableColor Picked;
		const bool bPickOk = First == UPGWearableColorLibrary::PickColorVariant(TEXT("Pants"), 42)
			&& UPGWearableColorLibrary::FindWearableColor(First, Picked) && Picked.BaseItemId == TEXT("Pants")
			&& UPGWearableColorLibrary::PickColorVariant(TEXT("Pants_Black"), 42) == TEXT("Pants_Black")
			&& UPGWearableColorLibrary::PickColorVariant(TEXT("Money"), 42) == TEXT("Money");
		C.Check(TEXT("wearable_pick_deterministic"), bPickOk, First.ToString());

		APGFloorItemActor* Drop = APGFloorItemActor::SpawnDrop(C.World, TEXT("Pants_Black"), 1, FTransform(FVector(600, 800, 0)));
		if (Drop) C.Cleanup.Add(Drop);
		FPGWearableColor Black;
		const UStaticMeshComponent* DropMesh = Drop ? Drop->FindComponentByClass<UStaticMeshComponent>() : nullptr;
		const bool bDropOk = DropMesh && DropMesh->GetStaticMesh() && UPGWearableColorLibrary::FindWearableColor(TEXT("Pants_Black"), Black)
			&& DropMesh->GetMaterial(0) == Black.Material.Get();
		C.Check(TEXT("wearable_drop_uses_color"), bDropOk);
	}

	void TestExtraction(FSmokeContext& C)
	{
		APGExtractionZoneActor* Wait = Cast<APGExtractionZoneActor>(C.SpawnCatalog(TEXT("OBJ-058"), FVector(0, 1200, 0)));
		if (!Wait) { C.Check(TEXT("extraction_spawn"), false); return; }
		Wait->OnExtractionCompleted.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleExtractionCompleted);
		Wait->NotifyPawnEntered(C.PawnA);
		const bool bNotYet = !Wait->AdvancePawn(C.PawnA, 4.0f) && Wait->GetProgress(C.PawnA) > 0.4f;
		Wait->NotifyPawnLeft(C.PawnA);
		const bool bCancelled = Wait->GetProgress(C.PawnA) == 0.0f && C.Listener->ExtractionCompleted == 0;
		Wait->NotifyPawnEntered(C.PawnA);
		Wait->AdvancePawn(C.PawnA, 9.0f);
		C.Check(TEXT("extraction_wait_cancel_and_complete"), bNotYet && bCancelled && C.Listener->ExtractionCompleted == 1);

		// 아이템 요구 + 플레이어별 + 1회 사용
		APGExtractionZoneActor* Heli = C.Spawn<APGExtractionZoneActor>(FVector(400, 1200, 0));
		if (!Heli) { C.Check(TEXT("extraction_item_player_once"), false); return; }
		Heli->OnExtractionCompleted.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleExtractionCompleted);
		Heli->Configure(EPGExtractionTrigger::Interact, 0.0f, TEXT("Fuel"), true, 1);
		Heli->SetAllowedPlayerKeys({ IPGItemReceiver::Execute_GetPlayerKey(C.PawnA) });
		IPGItemReceiver::Execute_ReceiveItem(C.PawnB, TEXT("Fuel"), 1);
		const bool bWrongPlayerBlocked = !C.CanInteract(Heli, C.PawnB);
		const bool bNoFuelBlocked = !C.CanInteract(Heli, C.PawnA);
		IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("Fuel"), 1);
		C.Interact(Heli, C.PawnA);
		const bool bDone = C.Listener->ExtractionCompleted == 2 && C.PawnA->GetCount(TEXT("Fuel")) == 0 && !Heli->IsActiveNow();
		C.Check(TEXT("extraction_item_player_once"), bWrongPlayerBlocked && bNoFuelBlocked && bDone,
			FString::Printf(TEXT("wrong=%d nofuel=%d done=%d"), bWrongPlayerBlocked, bNoFuelBlocked, bDone));

		// 맵 탈출 지점의 탈출 세트: 시드 몇 개로 돌려 차 세트(F·연료통)와 펜스 세트(잠긴 게이트 + 열쇠 영역)가 둘 다 나오는지.
		// PawnA 는 위에서 연료통을 다 썼다 → 차 탈출구는 막혀야 한다.
		bool bCarOk = false;
		bool bFenceOk = false;
		for (int32 SeedIndex = 0; SeedIndex < 16 && !(bCarOk && bFenceOk); ++SeedIndex)
		{
			FRandomStream Stream(SeedIndex * 7919 + 1);
			const int32 First = C.Spawner->GetSpawnedActors().Num();
			const int32 Placed = C.Spawner->SpawnExitSet(FVector(3000.0f + SeedIndex * 1500.0f, 3000.0f, 0.0f), FVector::ForwardVector, Stream, SeedIndex);
			APGExtractionZoneActor* SetZone = nullptr;
			APGDoorActor* SetGate = nullptr;
			const TArray<TWeakObjectPtr<AActor>>& All = C.Spawner->GetSpawnedActors();
			for (int32 I = First; I < All.Num(); ++I)
			{
				AActor* SetActor = All[I].Get();
				if (!SetActor)
					continue;
				C.Cleanup.Add(SetActor);
				SetZone = SetZone ? SetZone : Cast<APGExtractionZoneActor>(SetActor);
				SetGate = SetGate ? SetGate : Cast<APGDoorActor>(SetActor);
			}
			const FString Prompt = SetZone ? IInteractable::Execute_GetInteractionPrompt(SetZone).ToString() : FString();
			if (Placed == 1 && SetZone && !SetGate)
				bCarOk = !C.CanInteract(SetZone, C.PawnA) && Prompt.Contains(TEXT("연료통"));
			else if (Placed == 2 && SetZone && SetGate)
				bFenceOk = Prompt.Contains(TEXT("열쇠"));
		}
		C.Check(TEXT("extraction_exit_sets_car_and_fence"), bCarOk && bFenceOk, FString::Printf(TEXT("car=%d fence=%d"), bCarOk, bFenceOk));
	}

	void TestCorpse(FSmokeContext& C)
	{
		AActor* Corpse = C.Spawn<AActor>(FVector(0, 1600, 0));
		if (!Corpse) { C.Check(TEXT("corpse_loot"), false); return; }
		UPGLootableComponent* Lootable = NewObject<UPGLootableComponent>(Corpse, TEXT("Lootable"));
		Lootable->RegisterComponent();
		Lootable->OnEmptied.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleEmptied);
		Lootable->SetLootTableId(TEXT("LT_CorpseB"));
		const bool bInactiveBlocked = !Lootable->CanLoot(C.PawnA);
		Lootable->ActivateLoot(77);
		const int32 Contents = Lootable->GetContents().Num();
		const bool bLockBlocks = Lootable->TryBeginUse(C.PawnB) && !Lootable->CanLoot(C.PawnA);
		Lootable->EndUse(C.PawnB);
		const bool bLooted = Lootable->Loot(C.PawnA);
		C.Check(TEXT("corpse_loot"), bInactiveBlocked && Contents > 0 && bLockBlocks && bLooted && Lootable->GetContents().Num() == 0 && C.Listener->Emptied == 1,
			FString::Printf(TEXT("contents=%d"), Contents));

		// 상호작용 컴포넌트가 IInteractable 없는 시체를 찾아 루팅하는 경로.
		AActor* Corpse2 = C.Spawn<AActor>(FVector(200, 1600, 0));
		UPGLootableComponent* Lootable2 = NewObject<UPGLootableComponent>(Corpse2, TEXT("Lootable"));
		Lootable2->RegisterComponent();
		Lootable2->SetLootTableId(TEXT("LT_CorpseA"));
		Lootable2->ActivateLoot(78);
		C.Check(TEXT("interaction_component_loots_corpse"), C.PawnA->GetInteraction()->InteractWith(Corpse2) && Lootable2->GetContents().Num() == 0);
	}

	void TestService(FSmokeContext& C)
	{
		APGServiceInteractionActor* Shop = Cast<APGServiceInteractionActor>(C.SpawnCatalog(TEXT("OBJ-078"), FVector(0, 2000, 0)));
		if (!Shop) { C.Check(TEXT("service_spawn"), false); return; }
		Shop->OnServiceRequested.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleServiceRequested);
		C.PawnA->SetActorLocation(FVector(2000, 2000, 0));
		const bool bFarBlocked = !C.CanInteract(Shop, C.PawnA);
		C.PawnA->SetActorLocation(Shop->GetActorLocation() + FVector(150, 0, 0));
		C.PawnB->SetActorLocation(Shop->GetActorLocation() + FVector(150, 20, 0));
		C.Interact(Shop, C.PawnA);
		const bool bRequested = C.Listener->ServiceRequests == 1 && Shop->IsInUse();
		const bool bOtherBlocked = !C.CanInteract(Shop, C.PawnB);
		Shop->EndService(C.PawnA);
		C.Check(TEXT("service_use_point_and_lock"), bFarBlocked && bRequested && bOtherBlocked && !Shop->IsInUse() && C.CanInteract(Shop, C.PawnB));
		C.PawnA->SetActorLocation(FVector::ZeroVector);
		C.PawnB->SetActorLocation(FVector::ZeroVector);
	}

	void TestDestructible(FSmokeContext& C)
	{
		APGDestructibleActor* Weak = C.Spawn<APGDestructibleActor>(FVector(0, 2400, 0));
		if (!Weak) { C.Check(TEXT("destructible_spawn"), false); return; }
		Weak->OnDestroyedByDamage.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleDestroyed);
		Weak->Configure(EPGDestroyCondition::AnyDamage, 50.0f);
		Weak->TakeDamage(30.0f, FDamageEvent(), nullptr, C.PawnA);
		const bool bAlive = !Weak->IsDestroyed() && Weak->GetHealth() == 20.0f;
		Weak->TakeDamage(30.0f, FDamageEvent(), nullptr, C.PawnA);
		C.Check(TEXT("destructible_any_damage"), bAlive && Weak->IsDestroyed() && C.Listener->Destroyed == 1);

		APGDestructibleActor* Wall = C.Spawn<APGDestructibleActor>(FVector(200, 2400, 0));
		Wall->Configure(EPGDestroyCondition::ExplosiveOnly, 10.0f);
		Wall->TakeDamage(100.0f, FDamageEvent(), nullptr, C.PawnA);
		const bool bIgnoredBullet = !Wall->IsDestroyed();
		FRadialDamageEvent Radial;
		Radial.Params = FRadialDamageParams(100.0f, 300.0f);
		Radial.Origin = Wall->GetActorLocation();
		Wall->TakeDamage(100.0f, Radial, nullptr, C.PawnA);
		C.Check(TEXT("destructible_explosive_only"), bIgnoredBullet && Wall->IsDestroyed());

		APGDestructibleActor* Wire = C.Spawn<APGDestructibleActor>(FVector(400, 2400, 0));
		Wire->Configure(EPGDestroyCondition::ToolOnly, 10.0f, TEXT("Tool_Cutter"));
		Wire->TakeDamage(999.0f, Radial, nullptr, C.PawnA);
		const bool bNoTool = !Wire->IsDestroyed() && !C.CanInteract(Wire, C.PawnA);
		IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("Tool_Cutter"), 1);
		C.Interact(Wire, C.PawnA);
		C.Check(TEXT("destructible_tool_only"), bNoTool && Wire->IsDestroyed() && C.PawnA->GetCount(TEXT("Tool_Cutter")) == 1);
	}

	void TestQuest(FSmokeContext& C)
	{
		APGQuestObjectActor* DropBox = C.Spawn<APGQuestObjectActor>(FVector(0, 2800, 0));
		if (!DropBox) { C.Check(TEXT("quest_spawn"), false); return; }
		DropBox->OnQuestEvent.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleQuestEvent);
		DropBox->Configure(EPGQuestObjectMode::DeliverItem, TEXT("Q_Deliver"), TEXT("QuestItem_Doc"), 2);
		IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("QuestItem_Doc"), 1);
		const bool bNotEnough = !C.CanInteract(DropBox, C.PawnA);
		IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("QuestItem_Doc"), 1);
		C.Interact(DropBox, C.PawnA);
		const bool bDelivered = C.Listener->QuestEvents == 1 && C.Listener->LastQuestTag == TEXT("Q_Deliver") && C.PawnA->GetCount(TEXT("QuestItem_Doc")) == 0;
		IPGItemReceiver::Execute_ReceiveItem(C.PawnA, TEXT("QuestItem_Doc"), 2);
		C.Check(TEXT("quest_deliver_atomic_once"), bNotEnough && bDelivered && !C.CanInteract(DropBox, C.PawnA) && DropBox->HasCompleted(C.PawnA));

		APGQuestTriggerVolume* Volume = C.Spawn<APGQuestTriggerVolume>(FVector(200, 2800, 0));
		Volume->SetQuestTag(TEXT("Q_Visit"));
		Volume->OnQuestEvent.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleQuestEvent);
		const bool bFirst = Volume->NotifyPawnEntered(C.PawnA);
		const bool bRepeat = !Volume->NotifyPawnEntered(C.PawnA);
		const bool bOther = Volume->NotifyPawnEntered(C.PawnB);
		C.Check(TEXT("quest_trigger_once_per_player"), bFirst && bRepeat && bOther && C.Listener->QuestEvents == 3);

		APGQuestObjectActor* Operate = Cast<APGQuestObjectActor>(C.SpawnCatalog(TEXT("OBJ-089"), FVector(400, 2800, 0)));
		if (Operate)
		{
			Operate->OnQuestEvent.AddDynamic(C.Listener, &UPGSmokeTestListener::HandleQuestEvent);
			C.Interact(Operate, C.PawnA);
			C.Check(TEXT("quest_operate_from_catalog"), C.Listener->LastQuestTag == TEXT("Quest_Operate"));
		}
	}

	void TestVehicleComponents(FSmokeContext& C)
	{
		AActor* Vehicle = C.Spawn<AActor>(FVector(0, 3200, 0));
		if (!Vehicle) { C.Check(TEXT("vehicle_components"), false); return; }
		USceneComponent* Root = NewObject<USceneComponent>(Vehicle, TEXT("Root"));
		Root->RegisterComponent();
		Vehicle->SetRootComponent(Root);
		Vehicle->SetActorLocation(FVector(0, 3200, 0));
		UPGSeatComponent* Seat = NewObject<UPGSeatComponent>(Vehicle, TEXT("DriverSeat"));
		Seat->SetupAttachment(Root);
		Seat->RegisterComponent();
		Seat->bDriverSeat = true;
		UPGFuelComponent* Fuel = NewObject<UPGFuelComponent>(Vehicle, TEXT("FuelPort"));
		Fuel->SetupAttachment(Root);
		Fuel->RegisterComponent();
		Fuel->Capacity = 100.0f;
		Fuel->FuelPerItem = 50.0f;
		Fuel->RequiredToDepart = 100.0f;

		const bool bEnter = Seat->TryEnter(C.PawnA) && !Seat->TryEnter(C.PawnB) && Seat->GetOccupant() == C.PawnA;
		FVector ExitLocation;
		const bool bExit = Seat->Exit(C.PawnA, ExitLocation) && !Seat->IsOccupied();

		C.PawnB->SetActorLocation(Vehicle->GetActorLocation());
		// 앞선 탈출구 테스트에서 B에게 준 연료가 남아 있을 수 있다. 빈 상태에서 시작한다.
		if (C.PawnB->GetCount(TEXT("Fuel")) > 0)
			IPGItemReceiver::Execute_ConsumeItem(C.PawnB, TEXT("Fuel"), C.PawnB->GetCount(TEXT("Fuel")));
		const bool bNoFuel = !Fuel->CanRefuel(C.PawnB);
		IPGItemReceiver::Execute_ReceiveItem(C.PawnB, TEXT("Fuel"), 2);
		const bool bRefuel = Fuel->TryRefuel(C.PawnB) && !Fuel->HasEnoughToDepart() && Fuel->TryRefuel(C.PawnB) && Fuel->HasEnoughToDepart() && C.PawnB->GetCount(TEXT("Fuel")) == 0;
		C.PawnB->SetActorLocation(FVector::ZeroVector);
		C.Check(TEXT("vehicle_components"), bEnter && bExit && bNoFuel && bRefuel);
	}

	// 탱크: 세 부품 메시가 실제로 로드되고(경로 오타·에셋 누락이면 상자만 남는다), 주포가 한 발 쏘고 장전 중엔 안 나가는지.
	void TestTank(FSmokeContext& C)
	{
		APGTankPawn* Tank = C.Spawn<APGTankPawn>(FVector(0.0f, 6000.0f, 300.0f));
		if (!Tank) { C.Check(TEXT("tank_parts_and_gun"), false); return; }
		const bool bParts = Tank->GetHullMesh()->GetStaticMesh() && Tank->GetTurretMesh()->GetStaticMesh() && Tank->GetGunMesh()->GetStaticMesh();
		const float Length = Tank->GetBody()->GetUnscaledBoxExtent().X * 2.0f;
		const bool bFirst = Tank->FireMainGun();
		const bool bSecondBlocked = !Tank->FireMainGun();
		C.Check(TEXT("tank_parts_and_gun"), bParts && Length > 600.0f && bFirst && bSecondBlocked,
			FString::Printf(TEXT("parts=%d length=%.0f first=%d cooldown=%d"), bParts, Length, bFirst, bSecondBlocked));
	}

	// 피날레: 디렉터가 한 번만 시작하고, 전함 껍데기(Minerva 화물선 블루프린트) 경로가 살아 있는지.
	// 전함을 실제로 띄우진 않는다 — 부품 31개를 headless 에서 조립할 이유가 없고, 여기서 보고 싶은 건 "경로와 상태 기계"다.
	void TestFinale(FSmokeContext& C)
	{
		APGFinaleDirector* Director = C.Spawn<APGFinaleDirector>(FVector(0.0f, -9000.0f, 100.0f));
		if (!Director) { C.Check(TEXT("finale_director"), false); return; }
		const bool bIdle = Director->GetState() == EPGFinaleState::Idle;
		const bool bFound = APGFinaleDirector::Get(C.World) != nullptr;
		Director->WarningSeconds = 999.0f; // 테스트 중에 등장 단계로 넘어가 전함을 띄우지 않게
		Director->StartFinale(FVector(1000.0f, 1000.0f, 20.0f));
		const bool bWarning = Director->GetState() == EPGFinaleState::Warning;
		Director->StartFinale(FVector::ZeroVector); // 두 번째 호출은 무시돼야 한다(보스 리스폰 대비)
		const bool bOnce = Director->GetState() == EPGFinaleState::Warning && Director->GetShip() == nullptr;
		C.Check(TEXT("finale_state_machine"), bIdle && bFound && bWarning && bOnce,
			FString::Printf(TEXT("idle=%d found=%d warning=%d once=%d"), bIdle, bFound, bWarning, bOnce));

		// 껍데기 경로: 팩을 안 가져왔거나 경로가 틀리면 여기서 걸린다(전함이 "안 보이는" 제일 흔한 원인).
		APGBattleshipActor* Ship = C.Spawn<APGBattleshipActor>(FVector(0.0f, -12000.0f, 100.0f));
		const bool bHullPath = Ship && !Ship->HullBlueprint.IsNull() && Ship->HullBlueprint.LoadSynchronous() != nullptr;
		C.Check(TEXT("finale_hull_asset"), bHullPath, Ship ? Ship->HullBlueprint.ToString() : TEXT("no ship"));
	}

	void TestSpawnSockets(FSmokeContext& C)
	{
		const EPGSpawnSocketKind Kinds[] = { EPGSpawnSocketKind::Container, EPGSpawnSocketKind::Ammo, EPGSpawnSocketKind::Extraction, EPGSpawnSocketKind::Generic, EPGSpawnSocketKind::Safe };
		TArray<APGSpawnSocketActor*> Sockets;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Kinds); ++Index)
		{
			APGSpawnSocketActor* Socket = C.Spawn<APGSpawnSocketActor>(FVector(Index * 300.0f, 3600, 0));
			Socket->GetSocket()->Configure(Kinds[Index], 3, 1.0f);
			Sockets.Add(Socket);
		}
		// 배타 그룹: 두 소켓 중 하나만 채워진다.
		for (int32 Index = 0; Index < 2; ++Index)
		{
			APGSpawnSocketActor* Socket = C.Spawn<APGSpawnSocketActor>(FVector(Index * 300.0f, 3900, 0));
			Socket->GetSocket()->Configure(EPGSpawnSocketKind::Container, 1, 1.0f);
			Socket->GetSocket()->ExclusiveGroup = TEXT("TestGroup");
			Sockets.Add(Socket);
		}

		auto Collect = [&C, &Sockets](int64 Seed, TArray<FName>& OutIds) -> int32
		{
			C.Spawner->ResetSpawnState();
			for (APGSpawnSocketActor* Socket : Sockets)
			{
				if (AActor* Old = Socket->GetSocket()->GetSpawnedActor())
					Old->Destroy();
				Socket->GetSocket()->SetSpawnedActor(nullptr);
			}
			const int32 Count = C.Spawner->SpawnAllSockets(Seed);
			for (APGSpawnSocketActor* Socket : Sockets)
			{
				AActor* Spawned = Socket->GetSocket()->GetSpawnedActor();
				OutIds.Add(Spawned && Spawned->Tags.Num() > 1 ? Spawned->Tags[1] : NAME_None);
				if (Spawned) C.Cleanup.Add(Spawned);
			}
			return Count;
		};

		TArray<FName> FirstIds, SecondIds;
		const int32 FirstCount = Collect(42, FirstIds);
		const int32 SecondCount = Collect(42, SecondIds);
		C.Check(TEXT("sockets_fill_expected_count"), FirstCount == 6, FString::Printf(TEXT("spawned=%d (5 kinds + 1 of exclusive pair)"), FirstCount));
		C.Check(TEXT("sockets_deterministic_for_seed"), FirstCount == SecondCount && FirstIds == SecondIds);

		bool bKindsMatch = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Kinds); ++Index)
		{
			const FPGObjectCatalogRow* Row = C.Spawner->FindCatalogRow(FirstIds[Index]);
			bKindsMatch = bKindsMatch && Row && (Kinds[Index] == EPGSpawnSocketKind::Generic || Row->SocketKind == Kinds[Index]);
		}
		C.Check(TEXT("sockets_respect_kind"), bKindsMatch);
	}
}

int32 PGObjectSmokeTest::RunAndCount(UWorld* World)
{
	if (!IsValid(World) || World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogPGObjects, Error, TEXT("PGObjectSmokeTest: needs an authoritative world"));
		return 1;
	}

	// 편집 화면(Play 전) 월드에서는 AActor::ProcessEvent가 Execute_ 인터페이스 호출을
	// 조용히 건너뛴다. 그러면 "열 수 있나?"가 전부 false로 나와 가짜 실패가 절반쯤 생긴다.
	if (!World->IsGameWorld() || !World->AreActorsInitialized())
	{
		UE_LOG(LogPGObjects, Error, TEXT("PGObjectSmokeTest: Play(PIE)를 누른 뒤 게임 화면의 ~ 콘솔에서 실행하세요. 편집 화면에서는 인터페이스 호출이 무시되어 결과를 믿을 수 없습니다."));
		return 1;
	}

	FSmokeContext C;
	C.World = World;
	C.Spawner = UPGObjectSpawnerSubsystem::Get(World);
	if (!C.Spawner)
	{
		UE_LOG(LogPGObjects, Error, TEXT("PGObjectSmokeTest: spawner subsystem missing"));
		return 1;
	}
	RegisterDefaultCatalog(*C.Spawner);
	C.Spawner->ResetSpawnState();
	C.Listener = NewObject<UPGSmokeTestListener>(World);
	C.Listener->AddToRoot();
	C.PawnA = C.Spawn<APGObjectTestPawn>(FVector::ZeroVector);
	C.PawnB = C.Spawn<APGObjectTestPawn>(FVector(50, 0, 0));
	if (!C.PawnA || !C.PawnB)
	{
		UE_LOG(LogPGObjects, Error, TEXT("PGObjectSmokeTest: test pawns failed to spawn"));
		return 1;
	}

	TestItemValues(C);
	TestLootDeterminism(C);
	TestContainer(C);
	TestLockedContainer(C);
	TestUseLock(C);
	TestDoor(C);
	TestFloorItem(C);
	TestWearableColors(C);
	TestInteractionCandidates(C);
	TestExtraction(C);
	TestCorpse(C);
	TestService(C);
	TestDestructible(C);
	TestQuest(C);
	TestVehicleComponents(C);
	TestTank(C);
	TestSpawnSockets(C);
	TestFinale(C);

	UE_LOG(LogPGObjects, Display, TEXT("PGObjectSmokeTest summary: passed=%d failed=%d catalog_rows=%d pass=%s"),
		C.Passed, C.Failed, C.Spawner->GetCatalogCount(), C.Failed == 0 ? TEXT("true") : TEXT("false"));

	for (AActor* Actor : C.Cleanup)
		if (IsValid(Actor)) Actor->Destroy();
	C.Spawner->ResetSpawnState();
	C.Listener->RemoveFromRoot();
	return C.Failed;
}

void PGObjectSmokeTest::Run(UWorld* World)
{
	RunAndCount(World);
}

// ---------------------------------------------------------------------------
// CSV export: 에디터에서 DataTable(행 구조 FPGObjectCatalogRow / FPGLootTableRow)로 바로 가져올 수 있는 형식.

namespace
{
	FString CsvCell(const FString& Value)
	{
		FString Escaped = Value;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	}

	FString BoolCell(bool bValue) { return bValue ? TEXT("True") : TEXT("False"); }

	template <typename TEnum>
	FString EnumCell(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}
}

bool PGObjectSmokeTest::ExportCatalogCsv(UPGObjectSpawnerSubsystem& Spawner, const FString& CatalogPath, const FString& LootPath)
{
	TArray<FPGObjectCatalogRow> Rows;
	Spawner.GetAllCatalogRows(Rows);
	Rows.Sort([](const FPGObjectCatalogRow& A, const FPGObjectCatalogRow& B) { return A.ObjectId.LexicalLess(B.ObjectId); });

	FString Csv = TEXT("---,ObjectId,DisplayName,Archetype,ActorClass,Mesh,Motion,SecondaryMesh,LootTableId,ItemId,ItemCount,bLocked,RequiredKeyId,InteractSeconds,SocketKind,Tier,SpawnWeight,bUniquePerMap,Priority,bMVP,bIncluded,QuestTag,Notes\n");
	for (const FPGObjectCatalogRow& Row : Rows)
	{
		TArray<FString> Cells;
		Cells.Add(Row.ObjectId.ToString());
		Cells.Add(Row.ObjectId.ToString());
		Cells.Add(CsvCell(Row.DisplayName.ToString()));
		Cells.Add(EnumCell(Row.Archetype));
		Cells.Add(CsvCell(Row.ActorClass.ToString()));
		Cells.Add(CsvCell(Row.Mesh.ToString()));
		Cells.Add(EnumCell(Row.Motion));
		Cells.Add(CsvCell(Row.SecondaryMesh.ToString()));
		Cells.Add(Row.LootTableId.ToString());
		Cells.Add(Row.ItemId.ToString());
		Cells.Add(FString::FromInt(Row.ItemCount));
		Cells.Add(BoolCell(Row.bLocked));
		Cells.Add(Row.RequiredKeyId.ToString());
		Cells.Add(FString::SanitizeFloat(Row.InteractSeconds));
		Cells.Add(EnumCell(Row.SocketKind));
		Cells.Add(FString::FromInt(Row.Tier));
		Cells.Add(FString::SanitizeFloat(Row.SpawnWeight));
		Cells.Add(BoolCell(Row.bUniquePerMap));
		Cells.Add(EnumCell(Row.Priority));
		Cells.Add(BoolCell(Row.bMVP));
		Cells.Add(BoolCell(Row.bIncluded));
		Cells.Add(Row.QuestTag.ToString());
		Cells.Add(CsvCell(Row.Notes));
		Csv += FString::Join(Cells, TEXT(",")) + TEXT("\n");
	}
	const bool bCatalogSaved = FFileHelper::SaveStringToFile(Csv, *CatalogPath, FFileHelper::EEncodingOptions::ForceUTF8);

	TArray<FName> TableIds;
	Spawner.GetAllLootTableIds(TableIds);
	TableIds.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	FString LootCsv = TEXT("---,Entries,RollCount,bAllowDuplicates\n");
	for (const FName& TableId : TableIds)
	{
		const FPGLootTableRow* Table = Spawner.FindLootTable(TableId);
		if (!Table)
			continue;
		// UE DataTable CSV 는 구조체 배열을 (( ),( )) 텍스트로 받는다.
		TArray<FString> Entries;
		for (const FPGLootEntry& Entry : Table->Entries)
			Entries.Add(FString::Printf(TEXT("(ItemId=\"%s\",Weight=%s,MinCount=%d,MaxCount=%d)"),
				*Entry.ItemId.ToString(), *FString::SanitizeFloat(Entry.Weight), Entry.MinCount, Entry.MaxCount));
		LootCsv += FString::Printf(TEXT("%s,%s,%d,%s\n"), *TableId.ToString(),
			*CsvCell(FString::Printf(TEXT("(%s)"), *FString::Join(Entries, TEXT(",")))), Table->RollCount, *BoolCell(Table->bAllowDuplicates));
	}
	const bool bLootSaved = FFileHelper::SaveStringToFile(LootCsv, *LootPath, FFileHelper::EEncodingOptions::ForceUTF8);

	UE_LOG(LogPGObjects, Display, TEXT("Object catalog CSV export: rows=%d loot_tables=%d catalog=%s loot=%s saved=%s"),
		Rows.Num(), TableIds.Num(), *CatalogPath, *LootPath, (bCatalogSaved && bLootSaved) ? TEXT("true") : TEXT("false"));
	return bCatalogSaved && bLootSaved;
}
