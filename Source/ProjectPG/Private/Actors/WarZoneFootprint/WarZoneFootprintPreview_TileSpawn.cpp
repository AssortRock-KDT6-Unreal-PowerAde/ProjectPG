// AWarZoneFootprintPreview — 설계도를 실제로 올리기 — 타일 BP 생성·흡수, 시설 레벨 불러오기, 길찾기 막이.
// (2026-09-26 WarZoneFootprintPreview.cpp 에서 책임별로 나눔. 함수 본문은 그대로다.)

#include "WarZoneFootprintPreviewInternal.h"
#include "PGGameplayPointBuilder.h"
#include "PGMapTileSpawner.h"

// ---- 타일 세우기 도우미 (2026-09-28 SpawnRuntimeBlueprintTiles 에서 떼어 냄 — 동작은 그대로) ----
// 왜: 한 함수가 814줄이라 "지금 어느 단계인지" 를 스크롤하며 찾아야 했다. 액터 하나를 손보는 작은 일·워존 타일 고르기는
//   바깥 상태를 안 쓰므로 여기 자유 함수로 두고, 본 함수는 단계 이름만 읽히게 한다. 이름공간은 이 파일 전용(유니티 빌드에서 이름이 안 겹치게).
namespace PGTileSpawnSteps
{
	// The reviewed LD_Tile maps are packed into reusable Blueprint classes by
	// PG.BuildPackedTileBlueprints. Spawning those classes keeps the authored
	// meshes/materials/collision while avoiding thousands of streamed UWorlds.
	static const TCHAR* CornerPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Corner.BPP_Tile_Road_Corner_C");
	static const TCHAR* StraightPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Straight.BPP_Tile_Road_Straight_C");
	static const TCHAR* TPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_TJunction.BPP_Tile_Road_TJunction_C");
	static const TCHAR* CrossPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Cross.BPP_Tile_Road_Cross_C");
	static const TCHAR* DeadEndPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_DeadEnd.BPP_Tile_Road_DeadEnd_C");
	static const TCHAR* SpawnPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Spawn_Staging.BPP_Tile_Spawn_Staging_C");
	static const TCHAR* ExitPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Exit_Checkpoint.BPP_Tile_Exit_Checkpoint_C");
	static const TCHAR* ObstaclePath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Obstacle_Checkpoint.BPP_Tile_Obstacle_Checkpoint_C");
	static const TCHAR* OpenPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_OpenGround.BPP_Tile_None_OpenGround_C");
	static const TCHAR* RuinsPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_Ruins.BPP_Tile_None_Ruins_C");
	static const TCHAR* YardPath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Yard.BPP_Tile_WarZone_Yard_C");
	static const TCHAR* WarehousePath = TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Warehouse.BPP_Tile_WarZone_Warehouse_C");
	static const TCHAR* NatureMeadowPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Meadow_V2.BP_Tile_Nature_Meadow_V2_C");
	static const TCHAR* NatureForestSparsePath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestSparse.BP_Tile_Nature_ForestSparse_C");
	static const TCHAR* NatureForestDensePath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ForestDense.BP_Tile_Nature_ForestDense_C");
	static const TCHAR* NatureRockyPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Rocky.BP_Tile_Nature_Rocky_C");
	static const TCHAR* NatureScrubPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Scrub.BP_Tile_Nature_Scrub_C");
	static const TCHAR* NatureAmbushPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ambush.BP_Tile_Nature_Ambush_C");
	static const TCHAR* NatureServiceCampPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_ServiceCamp.BP_Tile_Nature_ServiceCamp_C");
	static const TCHAR* NatureDitchPath = TEXT("/Game/PG/LevelDesign/Tiles/Nature/BP_Tile_Nature_Ditch.BP_Tile_Nature_Ditch_C");
	static const TCHAR* WarZoneIndustrialOpenPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_IndustrialOpen.BP_Tile_WarZoneV2_IndustrialOpen_C");
	static const TCHAR* WarZoneContainerLanePath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_ContainerLane.BP_Tile_WarZoneV2_ContainerLane_C");
	static const TCHAR* WarZoneFactoryYardPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_FactoryYard.BP_Tile_WarZoneV2_FactoryYard_C");
	static const TCHAR* WarZoneUtilityYardPath = TEXT("/Game/PG/LevelDesign/Tiles/WarZone/BP_Tile_WarZoneV2_UtilityYard.BP_Tile_WarZoneV2_UtilityYard_C");

	// 겉모습 → (타일 종류, 타일 BP). 한 줄 = 한 종류(2026-09-26 OCP — 전에는 switch 19줄). 종류를 늘리면 여기 한 줄만 더한다.
	// 워존 바닥만 표에 없다: 가운데로부터의 거리(띠)와 묶음 해시로 아래에서 따로 고른다.
	struct FSimpleTile { ETacticalTileKind Kind; const TCHAR* Path; };
	static const TMap<ETileDesignVisual, FSimpleTile> SimpleTiles = {
		{ ETileDesignVisual::RoadStraight, { ETacticalTileKind::RoadStraight, StraightPath } },
		{ ETileDesignVisual::RoadCorner, { ETacticalTileKind::RoadCorner, CornerPath } },
		{ ETileDesignVisual::RoadTJunction, { ETacticalTileKind::RoadTJunction, TPath } },
		{ ETileDesignVisual::RoadCross, { ETacticalTileKind::RoadCross, CrossPath } },
		{ ETileDesignVisual::RoadDeadEnd, { ETacticalTileKind::RoadDeadEnd, DeadEndPath } },
		{ ETileDesignVisual::Spawn, { ETacticalTileKind::SpawnStaging, SpawnPath } },
		{ ETileDesignVisual::Exit, { ETacticalTileKind::ExitCheckpoint, ExitPath } },
		{ ETileDesignVisual::Obstacle, { ETacticalTileKind::ObstacleCheckpoint, ObstaclePath } },
		{ ETileDesignVisual::Ruins, { ETacticalTileKind::Ruins, RuinsPath } },
		{ ETileDesignVisual::NatureMeadow, { ETacticalTileKind::NatureMeadow, NatureMeadowPath } },
		{ ETileDesignVisual::NatureForestSparse, { ETacticalTileKind::NatureForestSparse, NatureForestSparsePath } },
		{ ETileDesignVisual::NatureForestDense, { ETacticalTileKind::NatureForestDense, NatureForestDensePath } },
		{ ETileDesignVisual::NatureRocky, { ETacticalTileKind::NatureRocky, NatureRockyPath } },
		{ ETileDesignVisual::NatureScrub, { ETacticalTileKind::NatureScrub, NatureScrubPath } },
		{ ETileDesignVisual::NatureAmbush, { ETacticalTileKind::NatureAmbush, NatureAmbushPath } },
		{ ETileDesignVisual::NatureServiceCamp, { ETacticalTileKind::NatureServiceCamp, NatureServiceCampPath } },
		{ ETileDesignVisual::NatureDitch, { ETacticalTileKind::NatureDitch, NatureDitchPath } },
	};

	// 타일 BP 클래스 불러오기(같은 경로는 한 번만).
	UClass* ResolveTileClass(TMap<FString, UClass*>& ClassCache, const TCHAR* Path)
	{
		const FString Key(Path);
		if (UClass** Existing = ClassCache.Find(Key))
			return *Existing;
		UClass* LoadedClass = LoadClass<AActor>(nullptr, Path);
		ClassCache.Add(Key, LoadedClass);
		return LoadedClass;
	}

	void DisableCollisionOnHiddenPrimitives(AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Component : PrimitiveComponents)
		{
			// Packed tile Blueprints retain intentionally hidden alternate props.
			// Hidden meshes must never leave an invisible gameplay collision behind.
			if (IsValid(Component) && !Component->IsVisible())
				Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	void NormalizePackedBaseGround(AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		const TSoftObjectPtr<UMaterialInterface>& GroundAsset = UPGMapVisualSet::GetActive()->UnifiedGroundMaterial;
		UMaterialInterface* UnifiedGround = GroundAsset.IsNull() ? nullptr : GroundAsset.LoadSynchronous();
		if (!IsValid(UnifiedGround))
			return;

		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			const UMaterialInterface* Material = IsValid(Component) ? Component->GetMaterial(0) : nullptr;
			if (!IsValid(Mesh) || !IsValid(Material)
				|| Mesh->GetFName() != TEXT("SM_Floor_2x2")
				|| !Material->GetPathName().Contains(TEXT("MI_RoadStraight_Ground")))
			{
				continue;
			}
			// Four 10x10 m packed slabs form the 20x20 m base. Only replace that
			// terrain material; road lanes, roofs and authored industrial floors stay.
			Component->SetMaterial(0, UnifiedGround);
		}
	}

	int32 HidePerTileTerrainUnderlay(AActor* Actor)
	{
		if (!IsValid(Actor))
			return 0;

		int32 HiddenComponentCount = 0;
		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		Actor->GetComponents(PrimitiveComponents);
		for (UPrimitiveComponent* Primitive : PrimitiveComponents)
		{
			if (!IsValid(Primitive))
				continue;

			const FName ComponentName = Primitive->GetFName();
			bool bIsTerrainUnderlay = ComponentName == TEXT("Ground_20m")
				|| ComponentName == TEXT("RoadPieces")
				|| ComponentName == TEXT("Road_6_5m")
				|| ComponentName.ToString().Contains(TEXT("RoadSurface"));
			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Primitive))
			{
				const UStaticMesh* Mesh = Instances->GetStaticMesh();
				const UMaterialInterface* Material = Instances->GetMaterial(0);
				const bool bPackedGroundMesh = IsValid(Mesh) && Mesh->GetFName() == TEXT("SM_Floor_2x2");
				const bool bGroundMaterial = IsValid(Material)
					&& (Material->GetPathName().Contains(TEXT("MI_RoadStraight_Ground"))
						|| Material->GetPathName().Contains(TEXT("MI_RuntimeGround_NatureUnified")));
				// Runtime tactical-tile actors use SM_Floor_2x2 only as their old base
				// terrain/road slab.  Facilities are handled separately and never enter
				// this lambda, so hiding every such slab is both safe and deterministic.
				bIsTerrainUnderlay |= bPackedGroundMesh || bGroundMaterial;
			}

			if (!bIsTerrainUnderlay)
				continue;

			Primitive->SetVisibility(false, true);
			Primitive->SetHiddenInGame(true);
			Primitive->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Primitive->SetCanEverAffectNavigation(false);
			++HiddenComponentCount;
		}
		return HiddenComponentCount;
	}

	void LiftPackedExitMarking(AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || Mesh->GetPathName() != TEXT("/Engine/BasicShapes/Plane.Plane"))
				continue;
			for (int32 InstanceIndex = 0; InstanceIndex < Component->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, false))
					continue;
				// The EXIT stencil is authored 2 cm above the terrain datum, which is
				// where the road surface used to sit, and a flat 8 cm lift was enough
				// to clear it. Raising the road to RoadSurfaceLiftCm put the stencil
				// back exactly on the asphalt - the audit caught 16 m2 of it at a
				// 0.00 cm gap. Derive the lift from the road height instead of a
				// constant so the two cannot drift apart again.
				constexpr float AuthoredMarkingHeightCm = 2.0f;
				constexpr float MarkingClearanceCm = 8.0f;
				const float MarkingLiftCm =
					RoadSurfaceLiftCm + MarkingClearanceCm - AuthoredMarkingHeightCm;
				InstanceTransform.AddToTranslation(FVector(0.0f, 0.0f, MarkingLiftCm));
				Component->UpdateInstanceTransform(InstanceIndex, InstanceTransform, false, true, true);
			}
		}
	}

	int32 FloorDivide(const int32 Value, const int32 Divisor)
	{
		const int32 Quotient = Value / Divisor;
		const int32 Remainder = Value % Divisor;
		return Remainder < 0 ? Quotient - 1 : Quotient;
	}

	uint32 MakeWarZoneClusterHash(const int64 RuntimeRaidSeed, const int32 DeltaX, const int32 DeltaY, const int32 ClusterSize, const uint32 BandSalt)
	{
		// Offset by half a cluster so the central 3x3 facility and its immediate
		// apron belong to one coherent patch instead of straddling four quadrants.
		const int32 ClusterX = FloorDivide(DeltaX + ClusterSize / 2, ClusterSize);
		const int32 ClusterY = FloorDivide(DeltaY + ClusterSize / 2, ClusterSize);
		return HashCombine(
			GetTypeHash(RuntimeRaidSeed),
			HashCombine(
				GetTypeHash(ClusterX),
				HashCombine(GetTypeHash(ClusterY), BandSalt)));
	}

	int32 GetWarZoneClusterSlot(const int32 DeltaX, const int32 DeltaY, const int32 ClusterSize)
	{
		const int32 OffsetX = DeltaX + ClusterSize / 2;
		const int32 OffsetY = DeltaY + ClusterSize / 2;
		const int32 LocalX = OffsetX - FloorDivide(OffsetX, ClusterSize) * ClusterSize;
		const int32 LocalY = OffsetY - FloorDivide(OffsetY, ClusterSize) * ClusterSize;
		return LocalY * ClusterSize + LocalX;
	}

	// 워존 바닥 칸의 타일 고르기: 가운데 건물에서 떨어진 거리(띠)와 묶음 해시로 종류·BP·태그·방향을 정한다.
	// FromCore = 이 칸 - 워존 가운데 칸. 같은 시드면 서버·클라이언트가 같은 것을 고른다.
	void ChooseWarZoneTile(const FIntPoint& FromCore, const uint32 StableHash, const int64 RuntimeRaidSeed,
		ETacticalTileKind& Kind, const TCHAR*& ClassPath, FName& WarZoneBandTag, FName& WarZoneVisualTag, int32& WarZoneRotationQuarterTurns)
	{
		// WarZone is one large logical region, not the single 3x3 anchor building.
		// Convert it into deterministic concentric combat bands: an unmistakably
		// industrial core, a mixed firefight belt and a natural outer buffer.
		const int32 DeltaX = FromCore.X;
		const int32 DeltaY = FromCore.Y;
		const int32 DistanceSquared = DeltaX * DeltaX + DeltaY * DeltaY;
		if (DistanceSquared <= 64)
		{
			WarZoneBandTag = TEXT("WarZone_Core");
			const uint32 ClusterHash = MakeWarZoneClusterHash(RuntimeRaidSeed, DeltaX, DeltaY, 3, 0xA341316Cu);
			const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0x51ED270Bu);
			const int32 ClusterRoll = ClusterHash % 100;
			const int32 DetailRoll = DetailHash % 100;
			const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 3);
			const int32 FeatureSlotA = static_cast<int32>((ClusterHash >> 16) % 9u);
			const int32 FeatureSlotB = (FeatureSlotA + 2 + static_cast<int32>((ClusterHash >> 21) % 5u)) % 9;
			const int32 FeatureSlotC = (FeatureSlotA + 5 + static_cast<int32>((ClusterHash >> 25) % 4u)) % 9;
			const bool bFeatureCell = LocalSlot == FeatureSlotA
				|| LocalSlot == FeatureSlotB
				|| LocalSlot == FeatureSlotC;
			WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
			if (bFeatureCell)
			{
				const int32 FeatureRoll = (ClusterRoll + LocalSlot * 17) % 100;
				if (FeatureRoll < 24) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneContainerLanePath; WarZoneVisualTag = TEXT("WZ_ContainerLane"); }
				else if (FeatureRoll < 47) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarZoneFactoryYardPath; WarZoneVisualTag = TEXT("WZ_FactoryYard"); }
				else if (FeatureRoll < 68) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneUtilityYardPath; WarZoneVisualTag = TEXT("WZ_UtilityYard"); }
				else if (FeatureRoll < 86) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
				else if (FeatureRoll < 96) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarehousePath; WarZoneVisualTag = TEXT("WZ_AuthoredWarehouse"); }
				else { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
			}
			// The combat core must read as a broad industrial yard, not a maze of
			// one-cell wall fragments.  Feature cells still provide containers,
			// tanks and authored yards; only a small minority become ruins.
			else if (DetailRoll < 88) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
			else { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
		}
		else if (DistanceSquared <= 225)
		{
			WarZoneBandTag = TEXT("WarZone_Mid");
			const uint32 ClusterHash = MakeWarZoneClusterHash(RuntimeRaidSeed, DeltaX, DeltaY, 4, 0xC8013EA4u);
			const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0x68E31DA4u);
			const int32 ClusterRoll = ClusterHash % 100;
			const int32 DetailRoll = DetailHash % 100;
			const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 4);
			const int32 FeatureSlotA = static_cast<int32>((ClusterHash >> 16) % 16u);
			const int32 FeatureSlotB = (FeatureSlotA + 5 + static_cast<int32>((ClusterHash >> 22) % 7u)) % 16;
			WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
			if (LocalSlot == FeatureSlotA || LocalSlot == FeatureSlotB)
			{
				if (ClusterRoll < 30) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneContainerLanePath; WarZoneVisualTag = TEXT("WZ_ContainerLane"); }
				else if (ClusterRoll < 50) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarZoneFactoryYardPath; WarZoneVisualTag = TEXT("WZ_FactoryYard"); }
				else if (ClusterRoll < 70) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = WarZoneUtilityYardPath; WarZoneVisualTag = TEXT("WZ_UtilityYard"); }
				else if (ClusterRoll < 85) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
				else if (ClusterRoll < 93) { Kind = ETacticalTileKind::WarZoneWarehouse; ClassPath = WarehousePath; WarZoneVisualTag = TEXT("WZ_AuthoredWarehouse"); }
				else { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
			}
			else if (DetailRoll < 26) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
			else if (DetailRoll < 34) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
			else if (DetailRoll < 46) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
			else if (DetailRoll < 70) { Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; WarZoneVisualTag = TEXT("WZ_Scrub"); }
			else if (DetailRoll < 91) { Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; WarZoneVisualTag = TEXT("WZ_Meadow"); }
			else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
		}
		else
		{
			WarZoneBandTag = TEXT("WarZone_Outer");
			const uint32 ClusterHash = MakeWarZoneClusterHash(RuntimeRaidSeed, DeltaX, DeltaY, 5, 0xAD90777Du);
			const uint32 DetailHash = HashCombine(ClusterHash, StableHash ^ 0xB5297A4Du);
			const int32 ClusterRoll = ClusterHash % 100;
			const int32 DetailRoll = DetailHash % 100;
			const int32 LocalSlot = GetWarZoneClusterSlot(DeltaX, DeltaY, 5);
			const int32 FeatureSlot = static_cast<int32>((ClusterHash >> 16) % 25u);
			const int32 BlendedRoll = (ClusterRoll * 3 + DetailRoll) / 4;
			WarZoneRotationQuarterTurns = static_cast<int32>((DetailHash >> 8) % 4);
			if (LocalSlot == FeatureSlot)
			{
				if (ClusterRoll < 25) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
				else if (ClusterRoll < 50) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
				else if (ClusterRoll < 62) { Kind = ETacticalTileKind::OpenGround; ClassPath = WarZoneIndustrialOpenPath; WarZoneVisualTag = TEXT("WZ_IndustrialOpen"); }
				else if (ClusterRoll < 75) { Kind = ETacticalTileKind::WarZoneYard; ClassPath = YardPath; WarZoneVisualTag = TEXT("WZ_AuthoredYard"); }
				else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
			}
			else if (BlendedRoll < 14) { Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; WarZoneVisualTag = TEXT("WZ_Ruins"); }
			else if (BlendedRoll < 27) { Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; WarZoneVisualTag = TEXT("WZ_ServiceCamp"); }
			else if (BlendedRoll < 52) { Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; WarZoneVisualTag = TEXT("WZ_Scrub"); }
			else if (BlendedRoll < 79) { Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; WarZoneVisualTag = TEXT("WZ_Meadow"); }
			else { Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; WarZoneVisualTag = TEXT("WZ_ForestBuffer"); }
		}
	}
}

void UPGMapTileSpawner::SpawnRuntimeBlueprintTiles()
{
	// 9/28: 도우미는 위 PGTileSpawnSteps, 액터 묶기·호숫가 칸·코드 시설은 아래 멤버 함수로 떼어 냈다. 순서·동작은 그대로다.
	using namespace PGTileSpawnSteps;
	if (!Map->bUseRuntimeBlueprintTiles || !IsValid(GetWorld()))
		return;

	for (AActor* SpawnedTile : Map->SpawnedRuntimeTiles)
	{
		if (IsValid(SpawnedTile))
			SpawnedTile->Destroy();
	}
	Map->SpawnedRuntimeTiles.Reset();
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : Map->RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->DestroyComponent();
	}
	Map->RuntimePackedVisualHISMs.Reset();
	BuildElevatedFacilityTerrain();

	FIntPoint WarZoneCoreCell = FIntPoint::ZeroValue;
	for (const FFacilityPlacement& FacilityPlacement : Map->FacilityPlacements)
	{
		if (FacilityPlacement.VisualSet == EFacilityVisualSet::Warehouse
			&& FacilityPlacement.Footprint == WarZoneCoreFootprint)
		{
			WarZoneCoreCell = FacilityPlacement.AnchorCell + WarZoneCoreCentreOffset;
			break;
		}
	}

	TMap<FString, UClass*> ClassCache;
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> PackedComponentByKey;
	int32 PackedVisualInstanceCount = 0;

	int32 SpawnFailures = 0;
	int32 PackedTileActorCount = 0;
	int32 WaterTileSkipCount = 0;
	const TSet<FIntPoint> ShoreCellsForTileSkip = CollectShoreCellsForTileSkip();
	int32 HiddenTerrainUnderlayCount = 0;
	TMap<ETacticalTileKind, int32> KindCounts;
	TMap<FName, int32> WarZoneVariantCounts;
	const int64 RuntimeRaidSeed = Map->GetRaidSeed(); // 서버=게임모드 시드, 클라=설계도 시드(멀티)
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
	{
		const uint32 StableHash = Placement.LocalSeed;
		// Lake cells get no tile actor at all. Their ground is 2.8 m under the water
		// surface, so a spawned tile would put its cars, containers and walls in open
		// water - which is exactly how the first pass looked.
		//
		// Shore cells are skipped for the same reason: every tile places its props
		// against a flat Z=20 surface, but a shore cell's ground is the generated
		// beach mesh sloping away underneath. The first pass left pine trees and
		// boulders standing in mid-air over the slope. The beach is meant to be open
		// anyway - the shore rock and reed scatter dresses it.
		if (Placement.Visual == ETileDesignVisual::Water
			|| ShoreCellsForTileSkip.Contains(Placement.GridCell))
		{
			++WaterTileSkipCount;
			continue;
		}

		ETacticalTileKind Kind = ETacticalTileKind::OpenGround;
		const TCHAR* ClassPath = OpenPath;
		FName WarZoneBandTag = NAME_None;
		FName WarZoneVisualTag = NAME_None;
		int32 WarZoneRotationQuarterTurns = INDEX_NONE;
		if (const FSimpleTile* Simple = SimpleTiles.Find(Placement.Visual))
		{
			Kind = Simple->Kind;
			ClassPath = Simple->Path;
		}
		else if (Placement.Visual == ETileDesignVisual::WarZoneGround)
		{
			ChooseWarZoneTile(Placement.GridCell - WarZoneCoreCell, StableHash, RuntimeRaidSeed,
				Kind, ClassPath, WarZoneBandTag, WarZoneVisualTag, WarZoneRotationQuarterTurns);
		}

		// Long POI access roads deliberately use the C++ tactical road builder.
		// The packed LD_Tile roads remain on generator-authored road cells and at
		// landmarks, while access corridors get clean shoulders plus sparse cover.
		// This prevents walls, barrels and cars from repeating every single cell.
		UClass* TileClass = Placement.bSupplementalAccessRoad
			? ATacticalTileActor::StaticClass()
			: ResolveTileClass(ClassCache, ClassPath);
		if (!IsValid(TileClass)) { ++SpawnFailures; continue; }
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const bool bSocketOrientedTile =
			Placement.Visual == ETileDesignVisual::RoadStraight
			|| Placement.Visual == ETileDesignVisual::RoadCorner
			|| Placement.Visual == ETileDesignVisual::RoadTJunction
			|| Placement.Visual == ETileDesignVisual::RoadCross
			|| Placement.Visual == ETileDesignVisual::RoadDeadEnd
			|| Placement.Visual == ETileDesignVisual::Spawn
			|| Placement.Visual == ETileDesignVisual::Exit
			|| Placement.Visual == ETileDesignVisual::Obstacle;
		const int32 RotationQuarterTurns = bSocketOrientedTile
			? Placement.RotationQuarterTurns
			: (WarZoneRotationQuarterTurns != INDEX_NONE
				? WarZoneRotationQuarterTurns
				: Placement.LayoutVariant);
		const int32 SpawnRotationQuarterTurns = Placement.bSupplementalAccessRoad
			? 0
			: RotationQuarterTurns;
		AActor* Tile = GetWorld()->SpawnActor<AActor>(
			TileClass,
			Placement.WorldLocation,
			FRotator(0.0f, SpawnRotationQuarterTurns * 90.0f, 0.0f),
			Parameters);
		if (!IsValid(Tile)) { ++SpawnFailures; continue; }
		if (ATacticalTileActor* TacticalTile = Cast<ATacticalTileActor>(Tile))
		{
			// Runtime placement is authoritative. Blueprint defaults are only an
			// editor preview and must not override the server/seed-selected tile kind.
			TacticalTile->TileKind = Kind;
			if (!WarZoneVisualTag.IsNone())
			{
				TacticalTile->bShowDynamicProps = false;
				const bool bNatureWarZoneTile = Kind >= ETacticalTileKind::NatureMeadow
					&& Kind <= ETacticalTileKind::NatureDitch;
				if (bNatureWarZoneTile)
				{
					TacticalTile->DressingDensityScale = WarZoneBandTag == TEXT("WarZone_Core") ? 0.55f
						: (WarZoneBandTag == TEXT("WarZone_Mid") ? 0.85f : 1.05f);
				}
				else if (Kind == ETacticalTileKind::WarZoneYard
					|| Kind == ETacticalTileKind::WarZoneWarehouse)
				{
					// Authored industrial cells still need an overgrown shoulder. The old
					// zero multiplier silently removed all grass added by their layouts.
					TacticalTile->DressingDensityScale = 0.70f;
				}
				else if (Kind == ETacticalTileKind::OpenGround
					|| Kind == ETacticalTileKind::Ruins)
				{
					TacticalTile->DressingDensityScale = WarZoneBandTag == TEXT("WarZone_Core") ? 0.50f : 0.75f;
				}
				else
				{
					TacticalTile->DressingDensityScale = 0.20f;
				}
			}
			if (Placement.bSupplementalAccessRoad)
			{
				TacticalTile->bShowDynamicProps = false;
				TacticalTile->bIsAccessRoad = true;
				TacticalTile->DressingDensityScale = 0.35f;
			}
			// Rebuild needs the combat-band tags to choose its ground treatment.
			if (!WarZoneBandTag.IsNone()) TacticalTile->Tags.AddUnique(WarZoneBandTag);
			if (!WarZoneVisualTag.IsNone()) TacticalTile->Tags.AddUnique(WarZoneVisualTag);
			TacticalTile->RebuildFromRuntimeSpec(
				static_cast<int32>(StableHash),
				Placement.ConnectionMask,
				Placement.LayoutVariant);
		}
		NormalizePackedBaseGround(Tile);
		HiddenTerrainUnderlayCount += HidePerTileTerrainUnderlay(Tile);
		DisableCollisionOnHiddenPrimitives(Tile);
		// All authored road/ground underlays are hidden above.  The one shared road
		// HISM is now the only rendered surface, eliminating both the leaf-pattern
		// material and coplanar flicker.
		if (Placement.Visual == ETileDesignVisual::Exit)
			LiftPackedExitMarking(Tile);
		Tile->Tags.AddUnique(TEXT("RuntimeTacticalTile"));
		Tile->Tags.AddUnique(FName(*StaticEnum<ETacticalTileKind>()->GetNameStringByValue(
			static_cast<int64>(Kind))));
		if (!WarZoneBandTag.IsNone())
			Tile->Tags.AddUnique(WarZoneBandTag);
		if (!WarZoneVisualTag.IsNone())
		{
			Tile->Tags.AddUnique(WarZoneVisualTag);
			WarZoneVariantCounts.FindOrAdd(WarZoneVisualTag)++;
		}
		#if WITH_EDITOR
		Tile->SetActorLabel(FString::Printf(TEXT("RuntimeTile_%d_%d"), Placement.GridCell.X, Placement.GridCell.Y));
		Tile->SetFolderPath(TEXT("RuntimeTacticalTiles"));
		#endif
		// The tile actor is only a deterministic construction template. Consolidate
		// its visible static meshes into shared HISM batches, then discard the actor;
		// the authoritative placement manifest and gameplay points remain unchanged.
		PackTileActorVisuals(Tile, PackedComponentByKey, PackedVisualInstanceCount);
		Tile->Destroy();
		++PackedTileActorCount;
		KindCounts.FindOrAdd(Kind)++;
	}
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : Map->RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->BuildTreeIfOutdated(false, true);
	}

	const int32 SpawnedFacilityCount = SpawnRuntimeFacilityActors(ClassCache, SpawnFailures, HiddenTerrainUnderlayCount);

	// Render a single unified ground layer for the whole generated footprint.
	// Per-tile terrain slabs are hidden above, so there are no coplanar surfaces
	// and no material discontinuity at the 20 m cell boundary.
	Map->GroundHISM->SetVisibility(true, true);
	Map->GroundHISM->SetHiddenInGame(false);
	Map->WarZoneGroundHISM->SetVisibility(true, true);
	Map->WarZoneGroundHISM->SetHiddenInGame(false);
	Map->TransitionGroundHISM->SetVisibility(true, true);
	Map->TransitionGroundHISM->SetHiddenInGame(false);
	Map->RoadSurfaceHISM->SetVisibility(true, true);
	Map->RoadSurfaceHISM->SetHiddenInGame(false);
	Map->GroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Map->WarZoneGroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Map->TransitionGroundHISM->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Map->RoadSurfaceHISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UE_LOG(LogTemp, Display, TEXT("Runtime packed tile world: requested=%d packed_tile_actors=%d water_cells_skipped=%d packed_hism_components=%d packed_visual_instances=%d persistent_facilities=%d failures=%d shared_ground_visible=true shared_road_visible=true hidden_tile_ground_components=%d"),
		Map->TileDesignPlacements.Num(), PackedTileActorCount, WaterTileSkipCount,
		Map->RuntimePackedVisualHISMs.Num(), PackedVisualInstanceCount,
		SpawnedFacilityCount, SpawnFailures, HiddenTerrainUnderlayCount);
	for (const TPair<ETacticalTileKind, int32>& Pair : KindCounts)
		UE_LOG(LogTemp, Display, TEXT("Runtime Blueprint tile count: kind=%s count=%d"),
			*StaticEnum<ETacticalTileKind>()->GetNameStringByValue(static_cast<int64>(Pair.Key)), Pair.Value);
	for (const TPair<FName, int32>& Pair : WarZoneVariantCounts)
		UE_LOG(LogTemp, Display, TEXT("WarZone visual count: variant=%s count=%d"),
			*Pair.Key.ToString(), Pair.Value);

	RefreshNavigationBlockerRegion(Map->CenterNavigationBlockers, Map->GetActorLocation(), 6000.0f);
}

// 타일 액터 하나의 보이는 메시를 공용 HISM 묶음으로 옮긴다(액터는 부르는 쪽이 지운다). 9/28 SpawnRuntimeBlueprintTiles 에서 떼어 냄.
void UPGMapTileSpawner::PackTileActorVisuals(AActor* Actor, TMap<FString, UHierarchicalInstancedStaticMeshComponent*>& PackedComponentByKey, int32& PackedVisualInstanceCount)
{
	if (!IsValid(Actor))
		return;

	TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
	Actor->GetComponents(MeshComponents);
	for (UStaticMeshComponent* Source : MeshComponents)
	{
		const UStaticMesh* Mesh = IsValid(Source) ? Source->GetStaticMesh() : nullptr;
		if (!IsValid(Mesh) || !Source->IsVisible())
			continue;
		int32 StartCullDistance = 0;
		int32 EndCullDistance = 0;
		if (const UInstancedStaticMeshComponent* SourceInstances = Cast<UInstancedStaticMeshComponent>(Source))
			SourceInstances->GetCullDistances(StartCullDistance, EndCullDistance);
		// Authored Fab props are often plain StaticMeshComponents and therefore
		// arrive with infinite draw distance. Once packed into the runtime HISM,
		// apply foliage-specific culling while preserving the rare Pivot Painter
		// hero shrubs and their wind close to the player.
		const FString MeshPath = Mesh->GetPathName();
		const bool bIsFreeShrub = MeshPath.Contains(TEXT("/GV_FreeShrubsPack/"));
		const bool bIsRuntimeGrass = MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
			|| MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_"));
		const bool bIsSmallFoliage = bIsRuntimeGrass
			|| MeshPath.Contains(TEXT("/Meshes/Foliage/Bush_"))
			|| MeshPath.Contains(TEXT("/Meshes/Foliage/Shrubs_"))
			|| bIsFreeShrub;
		const TSoftObjectPtr<UMaterialInterface>& LeafAsset = UPGMapVisualSet::GetActive()->HeroShrubLeafMaterial;
		UMaterialInterface* RuntimeHeroShrubMaterial = LeafAsset.IsNull() ? nullptr : LeafAsset.LoadSynchronous();
		auto ResolvePackedMaterial = [bIsFreeShrub, RuntimeHeroShrubMaterial](UMaterialInterface* SourceMaterial)
		{
			if (!bIsFreeShrub || !IsValid(SourceMaterial) || !IsValid(RuntimeHeroShrubMaterial))
				return SourceMaterial;
			// Preserve bark while replacing the neon, high-amplitude Pivot Painter
			// leaf material with the local dark, nearly-static gameplay variant.
			return SourceMaterial->GetPathName().Contains(TEXT("Leaf"))
				? RuntimeHeroShrubMaterial
				: SourceMaterial;
		};
		const bool bPackedEvaluateWorldPositionOffset = Source->bEvaluateWorldPositionOffset && !bIsFreeShrub;
		if (bIsRuntimeGrass)
		{
			// Dense grass remains unchanged at player distance; only far-away cells
			// stop drawing earlier. This keeps the Tarkov-like silhouette without
			// submitting tens of thousands of invisible grass cards.
			// 거리는 맵 에셋 묶음(DA_PGMapVisuals)에서(9/28 코드에서 옮김, 기본 18m~60m).
			StartCullDistance = FMath::RoundToInt(UPGMapVisualSet::GetActive()->RuntimeGrassFadeStartCm);
			EndCullDistance = FMath::RoundToInt(UPGMapVisualSet::GetActive()->RuntimeGrassCullEndCm);
		}
		else if (MeshPath.Contains(TEXT("/Meshes/Foliage/Bush_"))
			|| MeshPath.Contains(TEXT("/Meshes/Foliage/Shrubs_")))
		{
			StartCullDistance = 2500;
			EndCullDistance = 8000;
		}
		else if (MeshPath.Contains(TEXT("/Meshes/Foliage/SM_Pine_Tree_")))
		{
			StartCullDistance = 10000;
			EndCullDistance = 22000;
		}
		else if (MeshPath.Contains(TEXT("/Assets/props/prop_rocks/")))
		{
			StartCullDistance = 8000;
			EndCullDistance = 18000;
		}
		else if (EndCullDistance <= 0)
		{
			if (MeshPath.Contains(TEXT("/GV_FreeShrubsPack/")))
			{
				StartCullDistance = 4500;
				EndCullDistance = 12000;
			}
			else if (MeshPath.Contains(TEXT("/Foliage/Grass_Patch"))
				|| MeshPath.Contains(TEXT("/RuntimeOptimized/SM_GrassPatch_")))
			{
				StartCullDistance = 2500;
				EndCullDistance = 8000;
			}
			else
			{
				// 위 표에 안 걸린 나머지 — 벽·컨테이너·바렐·펜스·도로 조각 같은 구조물이다.
				// 지금까지 이 갈래가 없어서 600m 맵 끝까지 전부 그리고 있었다.
				//
				// 350m 가 아니라 500m 로 잡은 이유: 맵 지름이 600m 라 500m 면 사실상
				// 반대편 끝이다. 원거리 교전 중에 벽이 사라져 보이면 버그로 읽히는데,
				// 그 위험을 피하면서도 시야 뒤쪽 절반은 제출하지 않는다(사용자 선택).
				StartCullDistance = 35000;
				EndCullDistance = 50000;
			}
		}

		FString BaseKey = FString::Printf(TEXT("%s|C%d|P%s|S%d|W%d|D%d-%d"),
			*Mesh->GetPathName(),
			static_cast<int32>(Source->GetCollisionEnabled()),
			*Source->GetCollisionProfileName().ToString(),
			Source->CastShadow ? 1 : 0,
			bPackedEvaluateWorldPositionOffset ? 1 : 0,
			StartCullDistance,
			EndCullDistance);
		for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
		{
			const UMaterialInterface* Material = ResolvePackedMaterial(Source->GetMaterial(MaterialIndex));
			BaseKey += TEXT("|M") + (IsValid(Material) ? Material->GetPathName() : TEXT("None"));
		}

		// 같은 메시라도 200m 구역(3×3)마다 따로 묶는다. 예전에는 메시 종류마다 맵 전체(600m)가 HISM 하나였다.
		// 그러면 소품 하나를 부술 때(RemoveInstance) 600m 짜리 묶음 전체의 컬링 트리를 다시 만들고,
		// 묶음 바운드 전체의 그림자 캐시(VSM)가 무효가 됐다(Unreal Fest 26 Seoul, Neverness to Everness 발표 p.43 과 같은 문제).
		// 구역으로 쪼개면 부순 자리 근처 묶음만 갱신되고, 화면 밖 구역은 통째로 컬링된다.
		constexpr float PackedChunkSizeCm = 20000.0f;
		auto TargetFor = [&](const FVector& WorldLocation) -> UHierarchicalInstancedStaticMeshComponent*
		{
			const FString Key = FString::Printf(TEXT("%s|G%d_%d"), *BaseKey,
				FMath::FloorToInt(WorldLocation.X / PackedChunkSizeCm), FMath::FloorToInt(WorldLocation.Y / PackedChunkSizeCm));
			UHierarchicalInstancedStaticMeshComponent*& Target = PackedComponentByKey.FindOrAdd(Key);
			if (!IsValid(Target))
			{
				Target = NewObject<UHierarchicalInstancedStaticMeshComponent>(
					Map.Get(),
					*FString::Printf(TEXT("RuntimePackedVisual_%d"), Map->RuntimePackedVisualHISMs.Num()));
				Target->SetupAttachment(Map->SceneRoot);
				Target->SetMobility(EComponentMobility::Movable);
				// 멀티(9/27): 이 묶음은 사람이 밟고 서는 바닥(시작 구역 아스팔트·길 조각 등)이기도 하다. 캐릭터 이동 복제는 "무엇을 밟고 있나" 를
				//   서버·클라가 주고받는데, 실행 중에 만든 부품은 그대로면 네트워크로 가리킬 수 없다("NOT Supported" 경고).
				//   그러면 움직이는 바닥(Movable) 기준 위치가 엉뚱하게 풀려 서버가 계속 위치를 되돌렸다 — 가만히 있어도 캐릭터가 꿈틀댔다(9/27 PIE).
				//   서버·클라가 같은 설계도로 같은 순서에 만들어 이름(RuntimePackedVisual_N)이 같으므로 이름으로 가리키게 한다.
				Target->SetNetAddressable();
				Target->SetStaticMesh(const_cast<UStaticMesh*>(Mesh));
				Target->SetCollisionProfileName(Source->GetCollisionProfileName());
				Target->SetCollisionEnabled(Source->GetCollisionEnabled());
				Target->SetGenerateOverlapEvents(false);
				Target->SetCanEverAffectNavigation(false);
				Target->SetCastShadow(Source->CastShadow && !bIsSmallFoliage);
				Target->SetEvaluateWorldPositionOffset(bPackedEvaluateWorldPositionOffset);
				Target->SetCullDistances(StartCullDistance, EndCullDistance);
				for (int32 MaterialIndex = 0; MaterialIndex < Source->GetNumMaterials(); ++MaterialIndex)
					Target->SetMaterial(MaterialIndex, ResolvePackedMaterial(Source->GetMaterial(MaterialIndex)));
				Target->RegisterComponent();
				Map->RuntimePackedVisualHISMs.Add(Target);
			}
			return Target;
		};

		if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Source))
		{
			for (int32 InstanceIndex = 0; InstanceIndex < Instances->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform WorldTransform;
				if (Instances->GetInstanceTransform(InstanceIndex, WorldTransform, true))
				{
					TargetFor(WorldTransform.GetLocation())->AddInstance(WorldTransform, true);
					++PackedVisualInstanceCount;
				}
			}
		}
		else
		{
			TargetFor(Source->GetComponentLocation())->AddInstance(Source->GetComponentTransform(), true);
			++PackedVisualInstanceCount;
		}
	}
}

// 호숫가 칸(물가 비탈)은 타일을 세우지 않는다 — 아래 SpawnRuntimeBlueprintTiles 의 물·호숫가 건너뛰기 주석. 9/28 떼어 냄.
TSet<FIntPoint> UPGMapTileSpawner::CollectShoreCellsForTileSkip() const
{
	TSet<FIntPoint> ShoreCellsForTileSkip;
	TSet<FIntPoint> LakeCellsForTileSkip;
	for (const FTileDesignPlacement& Placement : Map->TileDesignPlacements)
		if (Placement.Visual == ETileDesignVisual::Water)
			LakeCellsForTileSkip.Add(Placement.GridCell);
	if (LakeCellsForTileSkip.Num() > 0)
	{
		TMap<FIntPoint, TPair<int32, int32>> ShoreTiles;
		Map->BuildShoreTransitionMap(LakeCellsForTileSkip, ShoreTiles);
		for (const TPair<FIntPoint, TPair<int32, int32>>& Entry : ShoreTiles)
			ShoreCellsForTileSkip.Add(Entry.Key);
	}
	return ShoreCellsForTileSkip;
}

// 코드로 짓는 시설(설계 레벨이 없는 것)을 세운다. 세운 수를 돌려준다. 9/28 SpawnRuntimeBlueprintTiles 에서 떼어 냄.
int32 UPGMapTileSpawner::SpawnRuntimeFacilityActors(TMap<FString, UClass*>& ClassCache, int32& SpawnFailures, int32& HiddenTerrainUnderlayCount)
{
	using namespace PGTileSpawnSteps;
	int32 SpawnedFacilityCount = 0;
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
	{
		// An authored level supplies the whole facility. Spawning the procedural
		// builder as well would stack a second building inside the first.
		if (FacilityUsesAuthoredLevel(Placement.VisualSet, Placement.Footprint))
			continue;

		EProceduralFacilityKind FacilityKind = EProceduralFacilityKind::SatelliteCamp2x2;
		if (Placement.VisualSet == EFacilityVisualSet::Warehouse)
			FacilityKind = EProceduralFacilityKind::IndustrialRaid3x3;
		else if (Placement.VisualSet == EFacilityVisualSet::LongBarracks)
			FacilityKind = EProceduralFacilityKind::LongBarracks2x1;
		else if (Placement.VisualSet == EFacilityVisualSet::LinearTrench)
			FacilityKind = EProceduralFacilityKind::LinearTrench4x1;
		else if (Placement.VisualSet == EFacilityVisualSet::DowntownBlock)
			FacilityKind = EProceduralFacilityKind::DowntownBlock3x3;
		else if (Placement.VisualSet == EFacilityVisualSet::FactoryConstruction)
			FacilityKind = EProceduralFacilityKind::FactoryConstruction2x2;
		else if (Placement.VisualSet == EFacilityVisualSet::RuralHideout)
			FacilityKind = EProceduralFacilityKind::RuralHideout2x2;

		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector FacilityLocation = Map->GetDesignFootprintCenter(Placement);
		const FRotator FacilityRotation(0.0f, Placement.RotationQuarterTurns * 90.0f, 0.0f);

		// An authored Blueprint, when one exists for this facility, is preferred over
		// the code-built version: its walls and props are individual components a
		// designer can select and drag in the editor, which the HISM instances
		// AProceduralFacilityActor emits can never be. The procedural builder stays
		// as the fallback for every kind that has not been authored yet, so the two
		// can coexist while the library is filled in one facility at a time.
		UClass* AuthoredFacilityClass = nullptr;
		if (Map->bUseAuthoredFacilityBlueprints)
		{
			if (const TCHAR* AuthoredPath = GetAuthoredFacilityBlueprintPath(Placement.VisualSet))
				AuthoredFacilityClass = ResolveTileClass(ClassCache, AuthoredPath);
		}

		AActor* Facility = nullptr;
		if (IsValid(AuthoredFacilityClass))
		{
			Facility = GetWorld()->SpawnActor<AActor>(
				AuthoredFacilityClass, FacilityLocation, FacilityRotation, Parameters);
			if (IsValid(Facility))
			{
				// The authored Blueprint derives from ATacticalTileActor, so its
				// inherited 20 m ground slab and nature dressing would be layered
				// under a 40-60 m footprint. The shared terrain pad already covers
				// this cell range; hide the inherited underlay exactly as the packed
				// tiles do.
				HiddenTerrainUnderlayCount += HidePerTileTerrainUnderlay(Facility);
			}
		}
		if (!IsValid(Facility))
		{
			// 설정(ProjectPG Visuals > Procedural Facility Class)에 블루프린트가 있으면 그것으로(9/23 블루프린트 분리).
			AProceduralFacilityActor* ProceduralFacility = GetWorld()->SpawnActor<AProceduralFacilityActor>(
				UPGVisualSettings::ResolveActorClass(UPGVisualSettings::Get().ProceduralFacilityClass, AProceduralFacilityActor::StaticClass()),
				FacilityLocation,
				FacilityRotation,
				Parameters);
			if (IsValid(ProceduralFacility))
			{
				// Rotation controls how the footprint connects to the generated road
				// graph; the facility's deterministic interior variant is derived
				// independently from the server-authored local seed.
				const uint8 FacilityLayoutVariant = static_cast<uint8>(Placement.LocalSeed) & 3;
				ProceduralFacility->Configure(FacilityKind, Placement.LocalSeed, FacilityLayoutVariant);
			}
			Facility = ProceduralFacility;
		}
		if (!IsValid(Facility))
		{
			++SpawnFailures;
			continue;
		}
		DisableCollisionOnHiddenPrimitives(Facility);
		Facility->Tags.AddUnique(TEXT("RuntimeTacticalFacility"));
		Facility->Tags.AddUnique(FName(*StaticEnum<EProceduralFacilityKind>()->GetNameStringByValue(
			static_cast<int64>(FacilityKind))));
		#if WITH_EDITOR
		Facility->SetActorLabel(FString::Printf(TEXT("RuntimeFacility_%s_%d_%d%s"),
			*Placement.FacilityId.ToString(), Placement.AnchorCell.X, Placement.AnchorCell.Y,
			IsValid(AuthoredFacilityClass) ? TEXT("_Authored") : TEXT("")));
		Facility->SetFolderPath(TEXT("RuntimeTacticalFacilities"));
		#endif
		UE_LOG(LogTemp, Display,
			TEXT("Facility visual source: id=%s source=%s class=%s at=(%.0f,%.0f,%.0f) rotation=%.0f"),
			*Placement.FacilityId.ToString(),
			IsValid(AuthoredFacilityClass) ? TEXT("authored_blueprint") : TEXT("procedural_code"),
			*Facility->GetClass()->GetName(),
			FacilityLocation.X, FacilityLocation.Y, FacilityLocation.Z,
			FacilityRotation.Yaw);
		Map->SpawnedRuntimeTiles.Add(Facility);
		++SpawnedFacilityCount;
	}
	return SpawnedFacilityCount;
}

void UPGMapTileSpawner::BuildElevatedFacilityTerrain()
{
	for (UStaticMeshComponent* Component : Map->ElevatedTerrainComponents)
	{
		if (IsValid(Component))
			Component->DestroyComponent();
	}
	Map->ElevatedTerrainComponents.Reset();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	const TSoftObjectPtr<UMaterialInterface>& RampAsset = UPGMapVisualSet::GetActive()->RampMaterial;
	UMaterialInterface* RampMaterial = RampAsset.IsNull() ? nullptr : RampAsset.LoadSynchronous();
	if (!IsValid(Cube))
	{
		UE_LOG(LogTemp, Error, TEXT("Elevated terrain: engine cube missing"));
		return;
	}

	auto AddTerrainComponent = [this, Cube](
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Size,
		UMaterialInterface* Material,
		const FName& TerrainRole)
	{
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Map.Get());
		Component->ComponentTags.AddUnique(TEXT("ElevatedFacilityTerrain"));
		Component->ComponentTags.AddUnique(TerrainRole);
		Component->SetMobility(EComponentMobility::Static);
		Component->SetupAttachment(Map->SceneRoot);
		Component->SetStaticMesh(Cube);
		Component->SetRelativeLocation(Location);
		Component->SetRelativeRotation(Rotation);
		Component->SetRelativeScale3D(Size / 100.0f);
		if (IsValid(Material))
			Component->SetMaterial(0, Material);
		Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Component->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
		Component->SetGenerateOverlapEvents(false);
		Component->SetCanEverAffectNavigation(true);
		Map->AddInstanceComponent(Component);
		Component->RegisterComponent();
		Map->ElevatedTerrainComponents.Add(Component);
		return Component;
	};

	int32 RaisedFacilityCount = 0;
	int32 LoweredFacilityCount = 0;
	int32 RampCount = 0;
	int32 StairStepCount = 0;
	for (const FFacilityPlacement& Placement : Map->FacilityPlacements)
	{
		const float FacilitySurfaceZ = Placement.ElevationProfile == EFacilityElevationProfile::Ground
			? BaseGroundSurfaceZ : Placement.BaseElevationCm;
		if (FMath::IsNearlyEqual(FacilitySurfaceZ, BaseGroundSurfaceZ, 1.0f)
			|| Placement.OccupiedCells.IsEmpty())
			continue;

		TArray<TPair<FIntPoint, FIntPoint>> AccessEdges;
		Map->GetFacilityAccessEdges(Placement, AccessEdges);
		if (AccessEdges.IsEmpty())
			continue;

		const float RampRun = Placement.ElevationProfile == EFacilityElevationProfile::WarZoneStronghold
			? 1800.0f : (Placement.ElevationProfile == EFacilityElevationProfile::RaisedCompound
				? 1400.0f : 1000.0f);
		const float RampWidth = Placement.ElevationProfile == EFacilityElevationProfile::WarZoneStronghold
			? 900.0f : 700.0f;

		for (const TPair<FIntPoint, FIntPoint>& AccessEdge : AccessEdges)
		{
			const FIntPoint& EntranceCell = AccessEdge.Key;
			const FIntPoint& EntranceDirection = AccessEdge.Value;
			const FVector2D Outward(
				static_cast<float>(EntranceDirection.X),
				static_cast<float>(EntranceDirection.Y));
			const FVector EdgePoint(
				EntranceCell.X * DesignCellSize + Outward.X * DesignCellSize * 0.5f,
				EntranceCell.Y * DesignCellSize + Outward.Y * DesignCellSize * 0.5f,
				0.0f);
			const FIntPoint OutsideCell = EntranceCell + EntranceDirection;
			const float OutsideSurfaceZ = Map->GetSurfaceElevationForCell(OutsideCell);
			const FVector GroundPoint = EdgePoint + FVector(Outward.X, Outward.Y, 0.0f) * RampRun
				+ FVector(0.0f, 0.0f, OutsideSurfaceZ);
			const FVector TopPoint = EdgePoint + FVector(0.0f, 0.0f, FacilitySurfaceZ + 1.0f);
			const FVector RampDelta = TopPoint - GroundPoint;
			const FRotator RampRotation = RampDelta.Rotation();
			constexpr float RampThickness = 35.0f;
			// The slab is centred on the line from ground to pad, so its walkable face
			// used to stand half a thickness proud at both ends: an 18 cm lip to climb
			// before the ramp even began, and another 18 cm drop on to the pad at the
			// top. Sink it by that half thickness along its own up axis so the top face,
			// not the centre line, is what meets the ground and the pad.
			const FVector RampUp = RampRotation.RotateVector(FVector::UpVector);
			AddTerrainComponent(
				(GroundPoint + TopPoint) * 0.5f - RampUp * (RampThickness * 0.5f),
				RampRotation,
				FVector(RampDelta.Size(), RampWidth, RampThickness),
				RampMaterial,
				TEXT("VehicleRamp"));
			++RampCount;

			// A parallel infantry stair remains usable if the vehicle ramp is occupied.
			const FVector2D Perpendicular(-Outward.Y, Outward.X);
			const FVector StairSideOffset(Perpendicular.X * (RampWidth * 0.5f + 230.0f),
				Perpendicular.Y * (RampWidth * 0.5f + 230.0f), 0.0f);
			const int32 StepCount = FMath::Clamp(
				FMath::CeilToInt(FMath::Abs(TopPoint.Z - GroundPoint.Z) / 35.0f), 4, 12);
			const float StepRun = RampRun / StepCount;
			const FVector Inward(-Outward.X, -Outward.Y, 0.0f);
			const float StairSolidBottomZ = FMath::Min(-10.0f, FMath::Min(GroundPoint.Z, TopPoint.Z) - 50.0f);
			for (int32 StepIndex = 0; StepIndex < StepCount; ++StepIndex)
			{
				const float StepTop = FMath::Lerp(
					GroundPoint.Z, TopPoint.Z, static_cast<float>(StepIndex + 1) / StepCount);
				const float StepHeight = FMath::Max(10.0f, StepTop - StairSolidBottomZ);
				const FVector StepCenter = GroundPoint + StairSideOffset
					+ Inward * (StepRun * (StepIndex + 0.5f))
					+ FVector(0.0f, 0.0f, (StairSolidBottomZ + StepTop) * 0.5f - GroundPoint.Z);
				AddTerrainComponent(
					StepCenter,
					FRotator(0.0f, Inward.Rotation().Yaw, 0.0f),
					FVector(StepRun + 4.0f, 360.0f, StepHeight),
					RampMaterial,
					TEXT("InfantryStair"));
				++StairStepCount;
			}
		}

		if (FacilitySurfaceZ > BaseGroundSurfaceZ)
			++RaisedFacilityCount;
		else
			++LoweredFacilityCount;
		UE_LOG(LogTemp, Display,
			TEXT("Elevated facility: id=%s footprint=%dx%d elevation_cm=%.0f entrance=(%d,%d) access=(%d,%d) direction=(%d,%d) access_points=%d"),
			*Placement.FacilityId.ToString(), Placement.Footprint.X, Placement.Footprint.Y,
			FacilitySurfaceZ, Placement.EntranceCell.X, Placement.EntranceCell.Y,
			Placement.AccessCell.X, Placement.AccessCell.Y,
			Placement.EntranceDirection.X, Placement.EntranceDirection.Y,
			AccessEdges.Num());
	}

	UE_LOG(LogTemp, Display,
		TEXT("Macro elevation terrain: raised_facilities=%d lowered_facilities=%d ramps=%d stair_steps=%d nav_components=%d"),
		RaisedFacilityCount, LoweredFacilityCount, RampCount, StairStepCount, Map->ElevatedTerrainComponents.Num());
}

void UPGMapTileSpawner::RefreshNavigationBlockerRegion(
	UTacticalTileNavModifierComponent* Modifier,
	const FVector& WorldCenter,
	float RadiusCm)
{
	if (!IsValid(Modifier))
		return;

	TArray<FBox> WorldBlockers;
	const float RadiusSquared = FMath::Square(RadiusCm);
	for (AActor* TileActor : Map->SpawnedRuntimeTiles)
	{
		if (!IsValid(TileActor)
			|| FVector::DistSquared2D(TileActor->GetActorLocation(), WorldCenter) > RadiusSquared)
		{
			continue;
		}

		TArray<AActor*> GeometryActors;
		GeometryActors.Add(TileActor);
		TArray<AActor*> AttachedActors;
		TileActor->GetAttachedActors(AttachedActors, true, true);
		GeometryActors.Append(AttachedActors);
		for (const AActor* GeometryActor : GeometryActors)
		{
			if (!IsValid(GeometryActor))
				continue;
			TArray<UPrimitiveComponent*> PrimitiveComponents;
			GeometryActor->GetComponents<UPrimitiveComponent>(PrimitiveComponents);
			for (const UPrimitiveComponent* Primitive : PrimitiveComponents)
			{
				if (!IsValid(Primitive)
					|| Primitive->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
				{
					continue;
				}
				const FString ComponentName = Primitive->GetName();
				if (ComponentName.Contains(TEXT("Ground"))
					|| ComponentName.Contains(TEXT("Road"))
					|| ComponentName.Contains(TEXT("Roof"))
					|| ComponentName.Contains(TEXT("Grass"))
					|| ComponentName.Contains(TEXT("Marking")))
				{
					continue;
				}

				auto AddBlocker = [&WorldBlockers](const FBox& Box)
				{
					if (Box.GetExtent().Z >= 45.0f && Box.GetSize().X * Box.GetSize().Y >= 900.0f)
						WorldBlockers.Add(Box.ExpandBy(FVector(20.0f, 20.0f, 0.0f)));
				};
				if (const UInstancedStaticMeshComponent* ISM = Cast<UInstancedStaticMeshComponent>(Primitive))
				{
					if (!IsValid(ISM->GetStaticMesh()))
						continue;
					const FBox MeshBox = ISM->GetStaticMesh()->GetBoundingBox();
					for (int32 InstanceIndex = 0; InstanceIndex < ISM->GetInstanceCount(); ++InstanceIndex)
					{
						FTransform WorldTransform;
						if (ISM->GetInstanceTransform(InstanceIndex, WorldTransform, true))
							AddBlocker(MeshBox.TransformBy(WorldTransform));
					}
				}
				else
				{
					AddBlocker(Primitive->Bounds.GetBox());
				}
			}
		}
	}

	Modifier->SetWorldBlockers(WorldBlockers);
	UE_LOG(LogTemp, Display,
		TEXT("Local navigation blockers: center=(%.0f,%.0f) radius=%.0f boxes=%d"),
		WorldCenter.X, WorldCenter.Y, RadiusCm, WorldBlockers.Num());
}

void UPGMapTileSpawner::LoadFacilityDesignLevel(
	const FFacilityPlacement& Placement,
	int32 PlacementIndex)
{
	if (Placement.FacilityLevel.IsNull() || PlacementIndex < 0)
		return;

	while (Map->FacilityDesignLevelInstances.Num() <= PlacementIndex)
		Map->FacilityDesignLevelInstances.Add(nullptr);
	while (Map->FacilityLoadRequestTimeSeconds.Num() <= PlacementIndex)
		Map->FacilityLoadRequestTimeSeconds.Add(0.0);

	const FVector Location = Map->GetDesignFootprintCenter(Placement);
	const FRotator Rotation(0.0f, Placement.RotationQuarterTurns * 90.0f, 0.0f);
	bool bRequested = false;
	Map->FacilityLoadRequestTimeSeconds[PlacementIndex] = FPlatformTime::Seconds();
	const FString OptionalName = FString::Printf(TEXT("Facility_%02d_Type_%d"),
		PlacementIndex, static_cast<int32>(Placement.VisualSet));
	Map->FacilityDesignLevelInstances[PlacementIndex] = ULevelStreamingDynamic::LoadLevelInstanceBySoftObjectPtr(
		Map.Get(),
		Placement.FacilityLevel,
		Location,
		Rotation,
		bRequested,
		OptionalName);
	if (IsValid(Map->FacilityDesignLevelInstances[PlacementIndex]))
	{
		Map->FacilityDesignLevelInstances[PlacementIndex]->SetShouldBeLoaded(true);
		Map->FacilityDesignLevelInstances[PlacementIndex]->SetShouldBeVisible(true);
	}

	UE_LOG(LogTemp, Display,
		TEXT("Facility design level instance: index=%d type=%d %s at=(%.0f,%.0f,%.0f) rotation=%.0f"),
		PlacementIndex,
		static_cast<int32>(Placement.VisualSet),
		bRequested ? TEXT("requested") : TEXT("failed"),
		Location.X, Location.Y, Location.Z, Rotation.Yaw);
}

bool UPGMapTileSpawner::AreAllFacilityLevelsLoaded() const
{
	if (Map->FacilityPlacements.IsEmpty())
		return false;

	if (Map->bUseRuntimeBlueprintTiles)
	{
		for (int32 Index = 0; Index < Map->FacilityPlacements.Num(); ++Index)
		{
			if (Map->FacilityPlacements[Index].VisualSet != EFacilityVisualSet::Checkpoint)
				continue;
			if (!Map->FacilityDesignLevelInstances.IsValidIndex(Index))
				return false;
			const ULevelStreamingDynamic* Instance = Map->FacilityDesignLevelInstances[Index];
			if (!IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
				return false;
		}
		return true;
	}

	if (Map->FacilityDesignLevelInstances.Num() != Map->FacilityPlacements.Num()
		|| Map->FacilityDesignLevelInstances.IsEmpty())
		return false;
	for (const ULevelStreamingDynamic* Instance : Map->FacilityDesignLevelInstances)
		if (!IsValid(Instance) || !Instance->IsLevelLoaded() || !Instance->IsLevelVisible())
			return false;
	return true;
}
