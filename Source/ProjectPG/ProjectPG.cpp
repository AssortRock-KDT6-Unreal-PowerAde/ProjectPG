// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProjectPG.h"
#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"
// 게임플레이 콘솔 명령(PG.*)은 Private/Debug/PGConsoleCommands.cpp 에 있다. 여기는 에디터 전용 빌드 도구만.

#if WITH_EDITOR
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "PackedLevelActor/PackedLevelActorBuilder.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "UDynamicMesh.h"
#include "GeometryScript/CreateNewAssetUtilityFunctions.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "Engine/StaticMesh.h"
#endif

#if WITH_EDITOR
namespace ProjectPGPackedTiles
{
	struct FPackedTileSpec
	{
		const TCHAR* SourceLevel;
		const TCHAR* PackedBlueprint;
	};

	static const FPackedTileSpec TileSpecs[] = {
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Straight.LD_Tile_Road_Straight"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Straight.BPP_Tile_Road_Straight") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Corner.LD_Tile_Road_Corner"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Corner.BPP_Tile_Road_Corner") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_TJunction.LD_Tile_Road_TJunction"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_TJunction.BPP_Tile_Road_TJunction") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_Cross.LD_Tile_Road_Cross"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_Cross.BPP_Tile_Road_Cross") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Road_DeadEnd.LD_Tile_Road_DeadEnd"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Road_DeadEnd.BPP_Tile_Road_DeadEnd") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Spawn_Staging.LD_Tile_Spawn_Staging"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Spawn_Staging.BPP_Tile_Spawn_Staging") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Exit_Checkpoint.LD_Tile_Exit_Checkpoint"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Exit_Checkpoint.BPP_Tile_Exit_Checkpoint") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_Obstacle_Checkpoint.LD_Tile_Obstacle_Checkpoint"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_Obstacle_Checkpoint.BPP_Tile_Obstacle_Checkpoint") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_None_OpenGround.LD_Tile_None_OpenGround"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_OpenGround.BPP_Tile_None_OpenGround") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_None_Ruins.LD_Tile_None_Ruins"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_None_Ruins.BPP_Tile_None_Ruins") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_WarZone_Warehouse.LD_Tile_WarZone_Warehouse"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Warehouse.BPP_Tile_WarZone_Warehouse") },
		{ TEXT("/Game/PG/LevelDesign/Tiles/LD_Tile_WarZone_Yard.LD_Tile_WarZone_Yard"), TEXT("/Game/PG/LevelDesign/Tiles/Packed/BPP_Tile_WarZone_Yard.BPP_Tile_WarZone_Yard") },
	};

	void BuildPackedTileBlueprints()
	{
		const TSharedPtr<FPackedLevelActorBuilder> Builder = FPackedLevelActorBuilder::CreateDefaultBuilder();
		int32 Succeeded = 0;
		for (const FPackedTileSpec& Spec : TileSpecs)
		{
			const TSoftObjectPtr<UWorld> SourceWorld(FSoftObjectPath(Spec.SourceLevel));
			if (!SourceWorld.LoadSynchronous())
			{
				UE_LOG(LogTemp, Error, TEXT("Packed tile source missing: %s"), Spec.SourceLevel);
				continue;
			}

			TSoftObjectPtr<UBlueprint> PackedBlueprint(FSoftObjectPath(Spec.PackedBlueprint));
			UBlueprint* Blueprint = PackedBlueprint.LoadSynchronous();
			if (!Blueprint)
			{
				Blueprint = FPackedLevelActorBuilder::CreatePackedLevelActorBlueprint(
					PackedBlueprint,
					SourceWorld,
					true);
			}
			if (!Blueprint)
			{
				UE_LOG(LogTemp, Error, TEXT("Packed tile Blueprint creation failed: %s"), Spec.PackedBlueprint);
				continue;
			}

			const bool bBuilt = Builder->CreateOrUpdateBlueprint(
				SourceWorld,
				TSoftObjectPtr<UBlueprint>(Blueprint),
				true,
				false);
			UE_LOG(LogTemp, Display, TEXT("Packed tile build: source=%s target=%s result=%s"),
				Spec.SourceLevel,
				Spec.PackedBlueprint,
				bBuilt ? TEXT("success") : TEXT("failed"));
			Succeeded += bBuilt ? 1 : 0;
		}
		UE_LOG(LogTemp, Display, TEXT("Packed tile build complete: success=%d total=%d"),
			Succeeded,
			UE_ARRAY_COUNT(TileSpecs));
	}

	static FAutoConsoleCommand BuildPackedTilesCommand(
		TEXT("PG.BuildPackedTileBlueprints"),
		TEXT("Creates or refreshes runtime Packed Level Actor Blueprints from the 12 approved LD_Tile levels."),
		FConsoleCommandDelegate::CreateStatic(&BuildPackedTileBlueprints));
}
#endif

#if WITH_EDITOR
namespace ProjectPGShoreMeshes
{
	// Shore cells are the boundary between the border lake and the land. Deciding
	// water membership per 20 m cell makes that boundary a staircase, and the fix has
	// to be geometry finer than the cell.
	//
	// The first attempt picked a mesh from *which sides* of a cell were water. That
	// cannot tile: a beach cell's edge profile runs from -110 up to +20 across its
	// length, while the flat land cell beside it is +20 everywhere, so wherever the
	// waterline ended there was a 130 cm wall. Marching squares is the fix - height is
	// defined at the four *corners*, which adjacent cells share, and the interior is a
	// bilinear blend of them. Along any edge that blend collapses to an interpolation
	// between the two corners of that edge, evaluated identically from both sides, so
	// neighbouring cells agree by construction rather than by luck.
	//
	// Corner order is SW, SE, NE, NW; bit i of the mask means that corner is wet. A
	// +90 degree yaw rotates SW->SE->NE->NW, i.e. shifts the mask by one bit, so the
	// sixteen cases collapse to four generated meshes plus flat land and flat bed.
	constexpr float ShoreCellHalfSize = 1000.0f;
	constexpr float ShoreLandZ = 20.0f;
	constexpr float ShoreWaterZ = -110.0f;
	constexpr int32 ShoreGridResolution = 24;

	// Base masks, one per generated mesh. Every other mask is one of these rotated.
	constexpr uint8 ShoreBaseMasks[] = { 0b0001, 0b0011, 0b0101, 0b0111 };

	static float ShoreCornerHeight(uint8 CornerMask, int32 CornerIndex)
	{
		return (CornerMask & (1 << CornerIndex)) ? ShoreWaterZ : ShoreLandZ;
	}

	static float EvaluateShoreHeight(uint8 CornerMask, float U, float V)
	{
		// Smoothstep on each axis so the beach meets flat land with matching slope
		// instead of a crease. The edges stay a pure function of that edge's two
		// corners, which is what keeps neighbours seamless.
		const float SmoothU = FMath::SmoothStep(0.0f, 1.0f, U);
		const float SmoothV = FMath::SmoothStep(0.0f, 1.0f, V);
		const float South = FMath::Lerp(
			ShoreCornerHeight(CornerMask, 0), ShoreCornerHeight(CornerMask, 1), SmoothU);
		const float North = FMath::Lerp(
			ShoreCornerHeight(CornerMask, 3), ShoreCornerHeight(CornerMask, 2), SmoothU);
		return FMath::Lerp(South, North, SmoothV);
	}

	static void BuildShoreMesh(uint8 CornerMask, UE::Geometry::FDynamicMesh3& OutMesh)
	{
		using namespace UE::Geometry;
		OutMesh.Clear();
		OutMesh.EnableAttributes();
		OutMesh.Attributes()->SetNumUVLayers(1);
		FDynamicMeshUVOverlay* UVOverlay = OutMesh.Attributes()->GetUVLayer(0);

		const int32 Side = ShoreGridResolution + 1;
		const float Step = (ShoreCellHalfSize * 2.0f) / ShoreGridResolution;

		// Top surface only. The first version was a closed solid - bottom grid plus
		// four side walls - and those walls are what broke the shoreline: on an edge
		// shared by two shore cells the neighbour's wall faces the camera and rises
		// above the waterline to the dry corner, a black triangular fin standing on a
		// beach that is otherwise seamless. The underside is closed by the per-cell
		// lake bed slabs instead, which nothing inside the map can see past.
		TArray<int32> TopVertices;
		TArray<int32> TopUVs;
		TopVertices.Reserve(Side * Side);
		TopUVs.Reserve(Side * Side);
		for (int32 IndexY = 0; IndexY < Side; ++IndexY)
		{
			for (int32 IndexX = 0; IndexX < Side; ++IndexX)
			{
				const float U = static_cast<float>(IndexX) / ShoreGridResolution;
				const float V = static_cast<float>(IndexY) / ShoreGridResolution;
				const float X = -ShoreCellHalfSize + IndexX * Step;
				const float Y = -ShoreCellHalfSize + IndexY * Step;
				TopVertices.Add(OutMesh.AppendVertex(
					FVector3d(X, Y, EvaluateShoreHeight(CornerMask, U, V))));
				TopUVs.Add(UVOverlay->AppendElement(FVector2f(U, V)));
			}
		}

		auto GridIndex = [Side](int32 IndexX, int32 IndexY) { return IndexY * Side + IndexX; };
		for (int32 IndexY = 0; IndexY < ShoreGridResolution; ++IndexY)
		{
			for (int32 IndexX = 0; IndexX < ShoreGridResolution; ++IndexX)
			{
				const int32 A = GridIndex(IndexX, IndexY);
				const int32 B = GridIndex(IndexX + 1, IndexY);
				const int32 C = GridIndex(IndexX + 1, IndexY + 1);
				const int32 D = GridIndex(IndexX, IndexY + 1);

				const int32 TriangleOne = OutMesh.AppendTriangle(
					TopVertices[A], TopVertices[B], TopVertices[C]);
				const int32 TriangleTwo = OutMesh.AppendTriangle(
					TopVertices[A], TopVertices[C], TopVertices[D]);
				if (TriangleOne >= 0)
					UVOverlay->SetTriangle(TriangleOne, FIndex3i(TopUVs[A], TopUVs[B], TopUVs[C]));
				if (TriangleTwo >= 0)
					UVOverlay->SetTriangle(TriangleTwo, FIndex3i(TopUVs[A], TopUVs[C], TopUVs[D]));
			}
		}
	}

	void BuildShoreMeshAssets()
	{
		static const TCHAR* AssetPaths[] = {
			TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner1"),
			TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Adjacent"),
			TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner2Diagonal"),
			TEXT("/Game/PG/LevelDesign/Tiles/Terrain/SM_Shore_Corner3")
		};

		int32 Succeeded = 0;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(ShoreBaseMasks); ++Index)
		{
			UE::Geometry::FDynamicMesh3 Mesh;
			BuildShoreMesh(ShoreBaseMasks[Index], Mesh);

			UDynamicMesh* DynamicMesh = NewObject<UDynamicMesh>();
			DynamicMesh->SetMesh(MoveTemp(Mesh));

			FGeometryScriptCreateNewStaticMeshAssetOptions Options;
			Options.bEnableRecomputeNormals = true;
			Options.bEnableRecomputeTangents = true;
			EGeometryScriptOutcomePins Outcome = EGeometryScriptOutcomePins::Failure;
			UGeometryScriptLibrary_CreateNewAssetFunctions::CreateNewStaticMeshAssetFromMesh(
				DynamicMesh, AssetPaths[Index], Options, Outcome);

			// CreateNewStaticMeshAssetFromMesh only creates the package in memory. Left
			// unsaved it vanishes on editor shutdown, and the next session loads a
			// world whose shore cells have had their flat slabs suppressed with
			// nothing to replace them - holes straight through to the sky.
			//
			// Creation and saving are reported separately on purpose. Folding both
			// into one bool hid which half was failing and cost a whole debugging
			// pass; "created=false" and "saved=false" need completely different fixes.
			const bool bCreated = Outcome == EGeometryScriptOutcomePins::Success;
			bool bFound = false;
			bool bSaved = false;
			if (bCreated)
			{
				// The package exists in memory but the asset registry has not been told
				// about it yet within this call, so every path-based lookup - the
				// editor asset library, the MCP bridge's save helper - reports "asset
				// not found" and the mesh is silently lost on shutdown. FindObject
				// walks loaded objects directly and does not depend on the registry.
				const FString ObjectPath = FString::Printf(TEXT("%s.%s"),
					AssetPaths[Index], *FPaths::GetCleanFilename(AssetPaths[Index]));
				if (UStaticMesh* CreatedMesh = FindObject<UStaticMesh>(nullptr, *ObjectPath))
				{
					bFound = true;
					if (UPackage* Package = CreatedMesh->GetPackage())
					{
						Package->MarkPackageDirty();
						bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
					}
				}
			}
			UE_LOG(LogTemp, Display,
				TEXT("Shore mesh build: asset=%s corner_mask=%d created=%s found=%s saved=%s"),
				AssetPaths[Index], ShoreBaseMasks[Index],
				bCreated ? TEXT("true") : TEXT("false"),
				bFound ? TEXT("true") : TEXT("false"),
				bSaved ? TEXT("true") : TEXT("false"));
			Succeeded += (bCreated && bSaved) ? 1 : 0;
		}
		UE_LOG(LogTemp, Display,
			TEXT("Shore mesh build complete: success=%d total=%d land_z=%.0f water_z=%.0f"),
			Succeeded, UE_ARRAY_COUNT(ShoreBaseMasks), ShoreLandZ, ShoreWaterZ);
	}

	static FAutoConsoleCommand BuildShoreMeshesCommand(
		TEXT("PG.BuildShoreMeshes"),
		TEXT("Generates the four marching-squares shoreline meshes used along the border lake."),
		FConsoleCommandDelegate::CreateStatic(&BuildShoreMeshAssets));
}
#endif

// ---- 팀 코드(네트워크 버전 맞추기 — 서버와 접속자가 같은 빌드인지) ----
uint32 MyCustomGetNetworkVersion()
{
    uint32 Version = FCrc::StrCrc32(TEXT("SameVersion561"));
    UE_LOG(LogTemp, Warning, TEXT(">>> Global Custom Network Version Called! Return: %u"), Version);
    return Version;
}

// 2. 커스텀 모듈 클래스 정의
class FProjectPGModule : public FDefaultGameModuleImpl
{
public:
    virtual void StartupModule() override
    {
        FDefaultGameModuleImpl::StartupModule();

        // 엔진 초기화 직후 네트워크 버전 델리게이트 바인딩
        FNetworkVersion::GetLocalNetworkVersionOverride.BindStatic(&MyCustomGetNetworkVersion);
    }

    virtual void ShutdownModule() override
    {
        FDefaultGameModuleImpl::ShutdownModule();
    }
};

// 3. 매크로에 FDefaultGameModuleImpl 대신 우리가 만든 FProjectPGModule을 연결합니다.

// 매크로에 FDefaultGameModuleImpl 대신 FProjectPGModule 을 연결한다(팀 코드).
IMPLEMENT_PRIMARY_GAME_MODULE(FProjectPGModule, ProjectPG, "ProjectPG");
