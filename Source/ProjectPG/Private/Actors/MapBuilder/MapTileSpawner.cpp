#include "Actors/MapBuilder/MapTileSpawner.h"

#include "Actors/MapBuilder/MapBuildShared.h"
#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/TacticalTileActor.h"
#include "Actors/TacticalTileRoadStraight.h"
#include "Actors/ProceduralFacilityActor.h"
#include "Algo/AnyOf.h"
#include "Algo/Count.h"
#include "Algo/MinElement.h"
#include "Actors/MapTile.h"
#include "GameFramework/Pawn.h"
#include "GameModes/GameModePG.h"
#include "AIController.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Components/PrimitiveComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Engine/Level.h"
#include "Engine/CollisionProfile.h"
#include "Engine/OverlapResult.h"
#include "Engine/LevelStreamingDynamic.h"
#include "Engine/TargetPoint.h"
#include "Engine/StaticMesh.h"
#include "LandscapeProxy.h"
#include "GameFramework/PlayerController.h"
#include "NavigationInvokerComponent.h"
#include "NavigationPath.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "PCGComponent.h"
#include "PCGGraph.h"
#include "PCGNode.h"
#include "Elements/PCGCreatePoints.h"
#include "Elements/PCGStaticMeshSpawner.h"
#include "MeshSelectors/PCGMeshSelectorWeighted.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/TacticalTileNavModifierComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "TimerManager.h"
#include "HAL/PlatformMemory.h"
#include "Materials/MaterialInterface.h"

using namespace MapBuild;

void UMapTileSpawner::Init(AMapBuilder* InMap)
{
	Map = InMap;
}

UWorld* UMapTileSpawner::GetWorld() const
{
	return Map ? Map->GetWorld() : nullptr;
}

// 타일·건물 세우기. 게임에서 플레이어가 실제로 걸어 다니고 숨는 모든 것.
// 1) 언덕 위 건물 단 2) 칸마다 타일 BP 생성 3) 타일의 메시를 같은 종류끼리 HISM 묶음으로 옮기고 원래 액터는 숨김
// 4) 큰 건물: BP 가 있으면 BP, 레벨 파일이 있으면 레벨, 없으면 코드로 짓는 시설 5) 맵 가운데 길찾기 막힘.
void UMapTileSpawner::SpawnRuntimeBlueprintTiles()
{
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

	// The reviewed LD_Tile maps are packed into reusable Blueprint classes by
	// PG.BuildPackedTileBlueprints. Spawning those classes keeps the authored
	// meshes/materials/collision while avoiding thousands of streamed UWorlds.
	// 타일 BP 는 DA_MapAssets(보이는 것 목록)에서 고른다. 예전엔 여기 경로 24줄이 글자로 박혀 있었다.
	const UMapAssetSet& Assets = Map->GetMapAssets();
	const TSoftClassPtr<AActor>& CornerPath = Assets.RoadCornerTile;
	const TSoftClassPtr<AActor>& StraightPath = Assets.RoadStraightTile;
	const TSoftClassPtr<AActor>& TPath = Assets.RoadTJunctionTile;
	const TSoftClassPtr<AActor>& CrossPath = Assets.RoadCrossTile;
	const TSoftClassPtr<AActor>& DeadEndPath = Assets.RoadDeadEndTile;
	const TSoftClassPtr<AActor>& SpawnPath = Assets.SpawnTile;
	const TSoftClassPtr<AActor>& ExitPath = Assets.ExitTile;
	const TSoftClassPtr<AActor>& ObstaclePath = Assets.ObstacleTile;
	const TSoftClassPtr<AActor>& OpenPath = Assets.OpenGroundTile;
	const TSoftClassPtr<AActor>& RuinsPath = Assets.RuinsTile;
	const TSoftClassPtr<AActor>& YardPath = Assets.WarZoneYardTile;
	const TSoftClassPtr<AActor>& WarehousePath = Assets.WarZoneWarehouseTile;
	const TSoftClassPtr<AActor>& NatureMeadowPath = Assets.NatureMeadowTile;
	const TSoftClassPtr<AActor>& NatureForestSparsePath = Assets.NatureForestSparseTile;
	const TSoftClassPtr<AActor>& NatureForestDensePath = Assets.NatureForestDenseTile;
	const TSoftClassPtr<AActor>& NatureRockyPath = Assets.NatureRockyTile;
	const TSoftClassPtr<AActor>& NatureScrubPath = Assets.NatureScrubTile;
	const TSoftClassPtr<AActor>& NatureAmbushPath = Assets.NatureAmbushTile;
	const TSoftClassPtr<AActor>& NatureServiceCampPath = Assets.NatureServiceCampTile;
	const TSoftClassPtr<AActor>& NatureDitchPath = Assets.NatureDitchTile;
	const TSoftClassPtr<AActor>& WarZoneIndustrialOpenPath = Assets.WarZoneIndustrialOpenTile;
	const TSoftClassPtr<AActor>& WarZoneContainerLanePath = Assets.WarZoneContainerLaneTile;
	const TSoftClassPtr<AActor>& WarZoneFactoryYardPath = Assets.WarZoneFactoryYardTile;
	const TSoftClassPtr<AActor>& WarZoneUtilityYardPath = Assets.WarZoneUtilityYardTile;

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
	auto ResolveClass = [&ClassCache](const TSoftClassPtr<AActor>& Path)
	{
		const FString Key = Path.ToString();
		if (UClass** Existing = ClassCache.Find(Key))
			return *Existing;
		UClass* LoadedClass = Path.LoadSynchronous();
		ClassCache.Add(Key, LoadedClass);
		return LoadedClass;
	};
	auto DisableCollisionOnHiddenPrimitives = [](AActor* Actor)
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
	};
	UMaterialInterface* UnifiedGround = Assets.NatureGroundMaterial.LoadSynchronous();
	auto NormalizePackedBaseGround = [UnifiedGround](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
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
	};
	auto HidePerTileTerrainUnderlay = [](AActor* Actor)
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
	};
	TMap<FString, UHierarchicalInstancedStaticMeshComponent*> PackedComponentByKey;
	int32 PackedVisualInstanceCount = 0;
	auto PackActorVisuals = [this, &PackedComponentByKey, &PackedVisualInstanceCount](AActor* Actor)
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
			UMaterialInterface* RuntimeHeroShrubMaterial = Map->GetMapAssets().HeroShrubLeafMaterial.LoadSynchronous();
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
				StartCullDistance = 1800;
				EndCullDistance = 6000;
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
			}

			FString Key = FString::Printf(TEXT("%s|C%d|P%s|S%d|W%d|D%d-%d"),
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
				Key += TEXT("|M") + (IsValid(Material) ? Material->GetPathName() : TEXT("None"));
			}

			UHierarchicalInstancedStaticMeshComponent*& Target = PackedComponentByKey.FindOrAdd(Key);
			if (!IsValid(Target))
			{
				Target = NewObject<UHierarchicalInstancedStaticMeshComponent>(
					Map,
					*FString::Printf(TEXT("RuntimePackedVisual_%d"), Map->RuntimePackedVisualHISMs.Num()));
				Target->SetupAttachment(Map->SceneRoot);
				Target->SetMobility(EComponentMobility::Movable);
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

			if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Source))
			{
				for (int32 InstanceIndex = 0; InstanceIndex < Instances->GetInstanceCount(); ++InstanceIndex)
				{
					FTransform WorldTransform;
					if (Instances->GetInstanceTransform(InstanceIndex, WorldTransform, true))
					{
						Target->AddInstance(WorldTransform, true);
						++PackedVisualInstanceCount;
					}
				}
			}
			else
			{
				Target->AddInstance(Source->GetComponentTransform(), true);
				++PackedVisualInstanceCount;
			}
		}
	};
	auto LiftCoplanarPackedRoadSurfaces = [](AActor* Actor)
	{
		if (!IsValid(Actor))
			return;
		TInlineComponentArray<UInstancedStaticMeshComponent*> InstanceComponents;
		Actor->GetComponents(InstanceComponents);
		for (UInstancedStaticMeshComponent* Component : InstanceComponents)
		{
			const UStaticMesh* Mesh = IsValid(Component) ? Component->GetStaticMesh() : nullptr;
			if (!IsValid(Mesh) || Mesh->GetFName() != TEXT("SM_Floor_2x2"))
				continue;
			for (int32 InstanceIndex = 0; InstanceIndex < Component->GetInstanceCount(); ++InstanceIndex)
			{
				FTransform InstanceTransform;
				if (!Component->GetInstanceTransform(InstanceIndex, InstanceTransform, false)
					|| InstanceTransform.GetScale3D().Z > 0.1f)
					continue;
				// Packed road slabs started almost coplanar with the common 20 cm
				// terrain surface. Keep their centre at least 25 cm high: this leaves
				// a small, stable render separation without creating a gameplay step.
				FVector Translation = InstanceTransform.GetTranslation();
				Translation.Z = FMath::Max(Translation.Z, 25.0f);
				InstanceTransform.SetTranslation(Translation);
				Component->UpdateInstanceTransform(InstanceIndex, InstanceTransform, false, true, true);
			}
		}
	};
	auto LiftPackedExitMarking = [](AActor* Actor)
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
	};

	int32 SpawnFailures = 0;
	int32 PackedTileActorCount = 0;
	int32 WaterTileSkipCount = 0;
	TSet<FIntPoint> ShoreCellsForTileSkip;
	{
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
	}
	int32 HiddenTerrainUnderlayCount = 0;
	TMap<ETacticalTileKind, int32> KindCounts;
	TMap<FName, int32> WarZoneVariantCounts;
	const int64 RuntimeRaidSeed = Map->GetRaidSeed();
	auto FloorDivide = [](const int32 Value, const int32 Divisor)
	{
		const int32 Quotient = Value / Divisor;
		const int32 Remainder = Value % Divisor;
		return Remainder < 0 ? Quotient - 1 : Quotient;
	};
	auto MakeWarZoneClusterHash = [RuntimeRaidSeed, &FloorDivide](
		const int32 DeltaX,
		const int32 DeltaY,
		const int32 ClusterSize,
		const uint32 BandSalt)
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
	};
	auto GetWarZoneClusterSlot = [&FloorDivide](
		const int32 DeltaX,
		const int32 DeltaY,
		const int32 ClusterSize)
	{
		const int32 OffsetX = DeltaX + ClusterSize / 2;
		const int32 OffsetY = DeltaY + ClusterSize / 2;
		const int32 LocalX = OffsetX - FloorDivide(OffsetX, ClusterSize) * ClusterSize;
		const int32 LocalY = OffsetY - FloorDivide(OffsetY, ClusterSize) * ClusterSize;
		return LocalY * ClusterSize + LocalX;
	};
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
		TSoftClassPtr<AActor> ClassPath = OpenPath;
		FName WarZoneBandTag = NAME_None;
		FName WarZoneVisualTag = NAME_None;
		int32 WarZoneRotationQuarterTurns = INDEX_NONE;
		switch (Placement.Visual)
		{
		case ETileDesignVisual::RoadStraight: Kind = ETacticalTileKind::RoadStraight; ClassPath = StraightPath; break;
		case ETileDesignVisual::RoadCorner: Kind = ETacticalTileKind::RoadCorner; ClassPath = CornerPath; break;
		case ETileDesignVisual::RoadTJunction: Kind = ETacticalTileKind::RoadTJunction; ClassPath = TPath; break;
		case ETileDesignVisual::RoadCross: Kind = ETacticalTileKind::RoadCross; ClassPath = CrossPath; break;
		case ETileDesignVisual::RoadDeadEnd: Kind = ETacticalTileKind::RoadDeadEnd; ClassPath = DeadEndPath; break;
		case ETileDesignVisual::Spawn: Kind = ETacticalTileKind::SpawnStaging; ClassPath = SpawnPath; break;
		case ETileDesignVisual::Exit: Kind = ETacticalTileKind::ExitCheckpoint; ClassPath = ExitPath; break;
		case ETileDesignVisual::Obstacle: Kind = ETacticalTileKind::ObstacleCheckpoint; ClassPath = ObstaclePath; break;
		case ETileDesignVisual::Ruins: Kind = ETacticalTileKind::Ruins; ClassPath = RuinsPath; break;
		case ETileDesignVisual::NatureMeadow: Kind = ETacticalTileKind::NatureMeadow; ClassPath = NatureMeadowPath; break;
		case ETileDesignVisual::NatureForestSparse: Kind = ETacticalTileKind::NatureForestSparse; ClassPath = NatureForestSparsePath; break;
		case ETileDesignVisual::NatureForestDense: Kind = ETacticalTileKind::NatureForestDense; ClassPath = NatureForestDensePath; break;
		case ETileDesignVisual::NatureRocky: Kind = ETacticalTileKind::NatureRocky; ClassPath = NatureRockyPath; break;
		case ETileDesignVisual::NatureScrub: Kind = ETacticalTileKind::NatureScrub; ClassPath = NatureScrubPath; break;
		case ETileDesignVisual::NatureAmbush: Kind = ETacticalTileKind::NatureAmbush; ClassPath = NatureAmbushPath; break;
		case ETileDesignVisual::NatureServiceCamp: Kind = ETacticalTileKind::NatureServiceCamp; ClassPath = NatureServiceCampPath; break;
		case ETileDesignVisual::NatureDitch: Kind = ETacticalTileKind::NatureDitch; ClassPath = NatureDitchPath; break;
		case ETileDesignVisual::WarZoneGround:
		{
			// WarZone is one large logical region, not the single 3x3 anchor building.
			// Convert it into deterministic concentric combat bands: an unmistakably
			// industrial core, a mixed firefight belt and a natural outer buffer.
			const int32 DeltaX = Placement.GridCell.X - WarZoneCoreCell.X;
			const int32 DeltaY = Placement.GridCell.Y - WarZoneCoreCell.Y;
			const int32 DistanceSquared = DeltaX * DeltaX + DeltaY * DeltaY;
			if (DistanceSquared <= 64)
			{
				WarZoneBandTag = TEXT("WarZone_Core");
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 3, 0xA341316Cu);
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
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 4, 0xC8013EA4u);
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
				const uint32 ClusterHash = MakeWarZoneClusterHash(DeltaX, DeltaY, 5, 0xAD90777Du);
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
			break;
		}
		default: break;
		}

		// Long POI access roads deliberately use the C++ tactical road builder.
		// The packed LD_Tile roads remain on generator-authored road cells and at
		// landmarks, while access corridors get clean shoulders plus sparse cover.
		// This prevents walls, barrels and cars from repeating every single cell.
		UClass* TileClass = Placement.bSupplementalAccessRoad
			? ATacticalTileActor::StaticClass()
			: ResolveClass(ClassPath);
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
		PackActorVisuals(Tile);
		Tile->Destroy();
		++PackedTileActorCount;
		KindCounts.FindOrAdd(Kind)++;
	}
	for (UHierarchicalInstancedStaticMeshComponent* PackedComponent : Map->RuntimePackedVisualHISMs)
	{
		if (IsValid(PackedComponent))
			PackedComponent->BuildTreeIfOutdated(false, true);
	}

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
			// 공장 단지 BP(DA_MapAssets 의 WarZoneCoreBlueprint). 나머지 종류는 아직 BP 가 없어서 레벨이나 코드로 짓는다.
			if (Placement.VisualSet == EFacilityVisualSet::Warehouse && !Assets.WarZoneCoreBlueprint.IsNull())
				AuthoredFacilityClass = ResolveClass(Assets.WarZoneCoreBlueprint);
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
			AProceduralFacilityActor* ProceduralFacility = GetWorld()->SpawnActor<AProceduralFacilityActor>(
				AProceduralFacilityActor::StaticClass(),
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

	RefreshNavigationBlockerRegion(Map->CenterNavigationBlockers, Map->GetActorLocation(), Map->NavigationBlockerRadiusCm);
}

// 언덕 위 건물 단·경사로. 게임에서: 마당·막사가 1m쯤 높은 단 위에 있고, 입구 쪽에 올라가는 경사로가 있다.
void UMapTileSpawner::BuildElevatedFacilityTerrain()
{
	for (UStaticMeshComponent* Component : Map->ElevatedTerrainComponents)
	{
		if (IsValid(Component))
			Component->DestroyComponent();
	}
	Map->ElevatedTerrainComponents.Reset();

	UStaticMesh* Cube = LoadObject<UStaticMesh>(
		nullptr, TEXT("/Script/Engine.StaticMesh'/Engine/BasicShapes/Cube.Cube'"));
	// 경사로는 도로와 같은 아스팔트(DA_MapAssets 의 RoadMaterial).
	UMaterialInterface* RampMaterial = Map->GetMapAssets().RoadMaterial.LoadSynchronous();
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
		UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(Map);
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

// 큰 건물 레벨 불러오기. 건물 칸들의 가운데, 땅 높이 +1cm 에 레벨을 띄운다(바닥과 겹쳐 깜빡이지 않게).
void UMapTileSpawner::LoadFacilityDesignLevel(
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
		Map,
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

// 길찾기 막힘 갱신. 게임에서: 몬스터가 건물 벽을 뚫고 지나가는 길을 고르지 않게.
void UMapTileSpawner::RefreshNavigationBlockerRegion(
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

