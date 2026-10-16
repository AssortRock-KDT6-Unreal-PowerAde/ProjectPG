// PG.AutoPlaceLootSlots: 타일·시설 BP 와 시설 레벨에 상자 자리(Loot Slot)를 자동으로 찍는 에디터 명령.
// 쓰는 법: 에디터 아래 콘솔(Cmd)에 입력한다.
//   PG.AutoPlaceLootSlots            DA_MapAssets 에 있는 타일·시설 BP 와 시설 레벨 전부
//   PG.AutoPlaceLootSlots Dry        고르기만 하고 로그로 보여 준다(아무것도 안 바꾼다)
//   PG.AutoPlaceLootSlots Replace    전에 자동으로 찍은 자리를 지우고 다시 찍는다(손으로 찍은 자리는 그대로)
//   PG.AutoPlaceLootSlots Tiles      BP 만 / Levels 레벨만
//   PG.AutoPlaceLootSlots Downtown   이름에 Downtown 이 들어간 것만(여러 개 섞어 써도 된다)
// 이미 자리가 있는 곳은 그 수까지 쳐서 모자란 만큼만 더 찍는다. 그래서 손으로 찍은 자리와 섞어 써도 된다.
// 자동으로 찍은 자리에는 "AutoLootSlot" 꼬리표가 붙는다. 마음에 안 들면 옮기거나 지우면 된다.
//
// 자리 고르는 법(칸 안을 1~1.5m 간격으로 훑는다):
// ① 위에서 아래로 선을 쏴서 바닥을 찾는다. 2층 건물이면 2층 바닥도 찾는다.
//    지붕(위에 아무것도 없고 땅보다 1.5m 넘게 높은 바닥)·물·기울어진 바닥은 뺀다.
// ② 바깥(하늘이 보이는 땅)에서 걸어서 갈 수 있는 바닥만 남긴다. 이웃 칸으로는 계단 높이 차까지,
//    사이에 벽이 없을 때만 건너간다 → 문 없는 건물 안, 계단 없는 2층, 막힌 담장 안은 빠진다.
// ③ 상자 크기보다 조금 큰 공간(가로세로 96cm, 높이 1m)에 벽·소품이 박히면 뺀다.
// ④ 상자 네 귀퉁이 아래가 다 바닥이어야 한다(턱 끝에 걸치지 않게).
// ⑤ 벽·소품 옆, 지붕 아래를 먼저 고른다. 상자 앞면은 벽 반대쪽(트인 쪽)을 본다.
// ⑥ 자리끼리 4m 는 띄운다(층이 다르면 괜찮다). 개수는 1칸 타일 3개, 그보다 큰 시설 4개.

#if WITH_EDITOR

#include "Actors/LootSlotActor.h"
#include "Actors/LootSlotComponent.h"
#include "Actors/MapBuilder/MapAssetSet.h"
#include "Actors/MapBuilder/MapBuildShared.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/OverlapResult.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Regex.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

namespace LootSlotAutoPlacer
{
	static const FName AutoTag(TEXT("AutoLootSlot"));
	// ULootSlotComponent 노란 상자의 반높이. 자리 위치 = 바닥 + 이만큼.
	constexpr float SlotHalfHeight = 20.0f;
	constexpr float MinSpacing = 400.0f;
	// 층이 이만큼 다르면 가까워도 다른 자리로 친다.
	constexpr float FloorGap = 250.0f;
	// 임시 BP 를 띄울 높이. 열린 레벨의 물건과 안 겹치게 높이 띄운다.
	constexpr float TempSpawnZ = 300000.0f;
	// 띄운 뒤 이만큼 프레임을 기다려야 물리 검사가 새 액터를 본다.
	constexpr int32 SettleFrames = 3;

	struct FScene
	{
		UWorld* World = nullptr;
		// 이 액터들만 본다(BP 검사용). 비어 있으면 월드 전체(레벨 검사용).
		TSet<const AActor*> Only;
		// 바닥 판이 없는 칸의 땅 높이. 게임에서는 공통 땅이 이 높이에 깔린다.
		float GroundZ = 0.0f;
		FBox2D Region;
		float TopZ = 0.0f;
		float BottomZ = 0.0f;
	};

	struct FFloor
	{
		float Z = 0.0f;
		bool bVirtual = false;
		bool bWater = false;
		FVector Normal = FVector::UpVector;
	};

	struct FCandidate
	{
		FVector Floor;
		float Yaw = 0.0f;
		float Score = 0.0f;
	};

	struct FResult
	{
		int32 Cells = 1;
		int32 Target = 0;
		int32 Existing = 0;
		int32 Candidates = 0;
		// 사람이 설 수 있는 바닥 수 / 그중 바깥에서 걸어 갈 수 있는 수.
		int32 Walkable = 0;
		int32 Reachable = 0;
		TArray<FTransform> Picked;
	};

	static FCollisionObjectQueryParams SolidObjects()
	{
		FCollisionObjectQueryParams Params;
		Params.AddObjectTypesToQuery(ECC_WorldStatic);
		Params.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Params;
	}

	// 사람을 막는 진짜 물건만 센다(트리거·안 보이는 표시는 뺀다).
	static bool IsSolid(const FScene& Scene, const UPrimitiveComponent* Component)
	{
		if (!Component || Component->IsA<ULootSlotComponent>())
			return false;
		if (!Component->IsQueryCollisionEnabled() || Component->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Block)
			return false;
		return Scene.Only.IsEmpty() || Scene.Only.Contains(Component->GetOwner());
	}

	static bool IsWater(const UPrimitiveComponent* Component)
	{
		auto Has = [](const FString& Name)
		{
			return Name.Contains(TEXT("Water")) || Name.Contains(TEXT("Lake")) || Name.Contains(TEXT("Ocean")) || Name.Contains(TEXT("River"));
		};
		if (Has(Component->GetName()) || (Component->GetOwner() && Has(Component->GetOwner()->GetName())))
			return true;
		const UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(Component);
		return Mesh && Mesh->GetStaticMesh() && Has(Mesh->GetStaticMesh()->GetName());
	}

	// A→B 선에서 처음 닿는 진짜 물건. 물건 속에서 시작한 닿음은 뺀다(두꺼운 바닥 아래로 내려가며 다시 쏠 때).
	static bool FirstHit(const FScene& Scene, const FVector& A, const FVector& B, FHitResult& Out)
	{
		TArray<FHitResult> Hits;
		FCollisionQueryParams Query(SCENE_QUERY_STAT(AutoLootSlot), true);
		Scene.World->LineTraceMultiByObjectType(Hits, A, B, SolidObjects(), Query);
		bool bFound = false;
		for (const FHitResult& Hit : Hits)
		{
			if (Hit.bStartPenetrating || !IsSolid(Scene, Hit.GetComponent()))
				continue;
			if (!bFound || Hit.Distance < Out.Distance)
			{
				Out = Hit;
				bFound = true;
			}
		}
		return bFound;
	}

	static bool IsBlocked(const FScene& Scene, const FVector& Center, const FVector& HalfExtent)
	{
		TArray<FOverlapResult> Overlaps;
		Scene.World->OverlapMultiByObjectType(Overlaps, Center, FQuat::Identity, SolidObjects(),
			FCollisionShape::MakeBox(HalfExtent), FCollisionQueryParams(SCENE_QUERY_STAT(AutoLootSlotOverlap), false));
		for (const FOverlapResult& Overlap : Overlaps)
			if (IsSolid(Scene, Overlap.GetComponent()))
				return true;
		return false;
	}

	// 한 점에서 위→아래로 만나는 바닥들(위층부터). 땅 높이 근처에 아무것도 없으면 공통 땅을 하나 더한다.
	static TArray<FFloor> FloorsAt(const FScene& Scene, float X, float Y)
	{
		TArray<FFloor> Floors;
		float Z = Scene.TopZ;
		for (int32 Layer = 0; Layer < 8 && Z > Scene.BottomZ; ++Layer)
		{
			FHitResult Hit;
			if (!FirstHit(Scene, FVector(X, Y, Z), FVector(X, Y, Scene.BottomZ), Hit))
				break;
			FFloor& Floor = Floors.AddDefaulted_GetRef();
			Floor.Z = Hit.ImpactPoint.Z;
			Floor.Normal = Hit.ImpactNormal;
			Floor.bWater = IsWater(Hit.GetComponent());
			Z = Hit.ImpactPoint.Z - 2.0f;
		}
		const bool bHasGround = Floors.ContainsByPredicate([&Scene](const FFloor& Floor)
		{
			return Floor.Z <= Scene.GroundZ + 30.0f;
		});
		if (!bHasGround)
		{
			FFloor& Ground = Floors.AddDefaulted_GetRef();
			Ground.Z = Scene.GroundZ;
			Ground.bVirtual = true;
		}
		return Floors;
	}

	static float Hash01(const FVector& Point)
	{
		uint32 Hash = GetTypeHash(FMath::RoundToInt(Point.X));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Point.Y)));
		Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Point.Z)));
		return (Hash % 1000) / 1000.0f;
	}

	// 자리 수: 1칸 타일 3개, 그보다 큰 시설(BP·레벨) 4개.
	static int32 TargetCount(int32 Cells)
	{
		return Cells <= 1 ? 3 : 4;
	}

	static bool TooClose(const FVector& A, const FVector& B)
	{
		return FVector::Dist2D(A, B) < MinSpacing && FMath::Abs(A.Z - B.Z) < FloorGap;
	}

	// 칸 안을 훑어 자리 후보를 모으고, 점수 높은 것부터 띄엄띄엄 고른다.
	static void PickSlots(const FScene& Scene, const TArray<FVector>& ExistingBottoms, FResult& Result)
	{
		Result.Existing = ExistingBottoms.Num();
		Result.Target = TargetCount(Result.Cells);
		if (Result.Existing >= Result.Target)
			return;

		const float Step = Result.Cells <= 4 ? 100.0f : 150.0f;
		const FVector BoxHalf(48.0f, 48.0f, 50.0f);

		// ① 사람이 설 수 있는 바닥을 칸마다 모은다.
		struct FNode
		{
			FIntPoint Grid;
			FFloor Floor;
			bool bHasCeiling = false;
			float CeilingZ = 0.0f;
			bool bReached = false;
		};
		TArray<FNode> Nodes;
		TMap<FIntPoint, TArray<int32>> NodesAt;
		const int32 CountX = FMath::FloorToInt((Scene.Region.Max.X - Scene.Region.Min.X) / Step) + 1;
		const int32 CountY = FMath::FloorToInt((Scene.Region.Max.Y - Scene.Region.Min.Y) / Step) + 1;
		auto PointOf = [&](const FNode& Node)
		{
			return FVector(Scene.Region.Min.X + Node.Grid.X * Step, Scene.Region.Min.Y + Node.Grid.Y * Step, Node.Floor.Z);
		};
		for (int32 IX = 0; IX < CountX; ++IX)
		{
			for (int32 IY = 0; IY < CountY; ++IY)
			{
				const float X = Scene.Region.Min.X + IX * Step;
				const float Y = Scene.Region.Min.Y + IY * Step;
				const TArray<FFloor> Floors = FloorsAt(Scene, X, Y);
				for (int32 Index = 0; Index < Floors.Num(); ++Index)
				{
					const FFloor& Floor = Floors[Index];
					const bool bHasCeiling = Index > 0;
					// 지붕: 위에 아무것도 없는데 땅보다 높다.
					if (!bHasCeiling && Floor.Z > Scene.GroundZ + 150.0f)
						continue;
					// 물, 비탈(계단·경사로까지는 된다).
					if (Floor.bWater || (!Floor.bVirtual && Floor.Normal.Z < 0.7f))
						continue;
					// 천장이 너무 낮으면(기어 들어가야 하는 틈) 뺀다.
					if (bHasCeiling && Floors[Index - 1].Z - Floor.Z < 200.0f)
						continue;
					// 사람 하나 설 자리(무릎 높이부터 머리까지)가 비어야 한다.
					if (IsBlocked(Scene, FVector(X, Y, Floor.Z + 115.0f), FVector(25.0f, 25.0f, 70.0f)))
						continue;
					FNode& Node = Nodes.AddDefaulted_GetRef();
					Node.Grid = FIntPoint(IX, IY);
					Node.Floor = Floor;
					Node.bHasCeiling = bHasCeiling;
					Node.CeilingZ = bHasCeiling ? Floors[Index - 1].Z : 0.0f;
					NodesAt.FindOrAdd(Node.Grid).Add(Nodes.Num() - 1);
				}
			}
		}

		// ② 바깥에서 걸어 들어갈 수 있는 바닥만 남긴다.
		// 시작: 하늘이 보이는 땅. 이웃 칸으로는 높이 차가 계단 정도이고 사이에 벽이 없을 때만 건너간다.
		// 그래서 문 없는 건물 안, 계단 없는 2층, 막힌 담장 안은 빠진다.
		TArray<int32> Queue;
		for (int32 Index = 0; Index < Nodes.Num(); ++Index)
		{
			if (!Nodes[Index].bHasCeiling && FMath::Abs(Nodes[Index].Floor.Z - Scene.GroundZ) <= 150.0f)
			{
				Nodes[Index].bReached = true;
				Queue.Add(Index);
			}
		}
		const float MaxRise = Step * 0.7f;
		for (int32 Head = 0; Head < Queue.Num(); ++Head)
		{
			const FNode From = Nodes[Queue[Head]];
			for (const FIntPoint Offset : { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) })
			{
				const TArray<int32>* Neighbors = NodesAt.Find(From.Grid + Offset);
				if (!Neighbors)
					continue;
				for (const int32 NeighborIndex : *Neighbors)
				{
					FNode& To = Nodes[NeighborIndex];
					if (To.bReached || FMath::Abs(To.Floor.Z - From.Floor.Z) > MaxRise)
						continue;
					const float WalkZ = FMath::Max(From.Floor.Z, To.Floor.Z) + 50.0f;
					FHitResult Hit;
					const FVector A = PointOf(From), B = PointOf(To);
					if (FirstHit(Scene, FVector(A.X, A.Y, WalkZ), FVector(B.X, B.Y, WalkZ), Hit))
						continue;
					To.bReached = true;
					Queue.Add(NeighborIndex);
				}
			}
		}
		Result.Reachable = Queue.Num();
		Result.Walkable = Nodes.Num();

		// ③ 갈 수 있는 바닥 중 상자를 놓을 만한 곳.
		TArray<FCandidate> Candidates;
		for (const FNode& Node : Nodes)
		{
			if (!Node.bReached)
				continue;
			const FFloor& Floor = Node.Floor;
			const bool bHasCeiling = Node.bHasCeiling;
			if (!Floor.bVirtual && Floor.Normal.Z < 0.95f)
				continue;
			const FVector Point = PointOf(Node);
			if (IsBlocked(Scene, Point + FVector(0.0f, 0.0f, BoxHalf.Z + 6.0f), BoxHalf))
				continue;
			// 네 귀퉁이 아래도 바닥이어야 한다(공통 땅 위는 늘 평평하다).
			if (!Floor.bVirtual)
			{
				bool bFlat = true;
				for (const FVector2D Corner : { FVector2D(40, 30), FVector2D(-40, 30), FVector2D(40, -30), FVector2D(-40, -30) })
				{
					const FVector Above = Point + FVector(Corner.X, Corner.Y, 25.0f);
					FHitResult Hit;
					if (!FirstHit(Scene, Above, Above - FVector(0.0f, 0.0f, 50.0f), Hit)
						|| FMath::Abs(Hit.ImpactPoint.Z - Floor.Z) > 10.0f)
					{
						bFlat = false;
						break;
					}
				}
				if (!bFlat)
					continue;
			}

			// 둘레 8 방향으로 2m 선을 쏴서 벽·소품이 몇 쪽에 있는지 본다.
			int32 WallSides = 0;
			float NearestDistance = TNumericLimits<float>::Max();
			float NearestYaw = 0.0f;
			for (int32 Side = 0; Side < 8; ++Side)
			{
				const float Yaw = Side * 45.0f;
				const FVector Start = Point + FVector(0.0f, 0.0f, 50.0f);
				FHitResult Hit;
				if (FirstHit(Scene, Start, Start + FRotator(0.0f, Yaw, 0.0f).Vector() * 200.0f, Hit))
				{
					++WallSides;
					if (Hit.Distance < NearestDistance)
					{
						NearestDistance = Hit.Distance;
						NearestYaw = Yaw;
					}
				}
			}

			FCandidate& Candidate = Candidates.AddDefaulted_GetRef();
			Candidate.Floor = Point;
			Candidate.Score = (WallSides >= 1 && WallSides <= 5 ? 3.0f : (WallSides > 5 ? 1.0f : 0.0f))
				+ (bHasCeiling ? 2.0f : 0.0f) + Hash01(Point) * 0.9f;
			// 상자 앞면(X축)은 가장 가까운 벽의 반대쪽. 벽이 없으면 15도 단위로 아무렇게나.
			Candidate.Yaw = WallSides > 0
				? FRotator::NormalizeAxis(NearestYaw + 180.0f)
				: FMath::FloorToFloat(Hash01(Point + FVector(7.0f)) * 24.0f) * 15.0f;
		}
		Result.Candidates = Candidates.Num();

		Candidates.Sort([](const FCandidate& A, const FCandidate& B) { return A.Score > B.Score; });
		TArray<FVector> Taken = ExistingBottoms;
		for (const FCandidate& Candidate : Candidates)
		{
			if (Result.Existing + Result.Picked.Num() >= Result.Target)
				break;
			if (Taken.ContainsByPredicate([&Candidate](const FVector& Other) { return TooClose(Candidate.Floor, Other); }))
				continue;
			Taken.Add(Candidate.Floor);
			Result.Picked.Add(FTransform(FRotator(0.0f, Candidate.Yaw, 0.0f), Candidate.Floor + FVector(0.0f, 0.0f, SlotHalfHeight)));
		}
	}

	static FVector SlotBottom(const ULootSlotComponent* Slot)
	{
		return Slot->GetComponentLocation() - Slot->GetUpVector() * Slot->GetScaledBoxExtent().Z;
	}

	// 이름 끝의 "_6x6" 같은 칸 수.
	static bool ParseCells(const FString& Name, int32& OutX, int32& OutY)
	{
		FRegexMatcher Matcher(FRegexPattern(TEXT("_(\\d+)x(\\d+)")), Name);
		if (!Matcher.FindNext())
			return false;
		OutX = FCString::Atoi(*Matcher.GetCaptureGroup(1));
		OutY = FCString::Atoi(*Matcher.GetCaptureGroup(2));
		return OutX > 0 && OutY > 0;
	}

	struct FBlueprintJob
	{
		TWeakObjectPtr<UBlueprint> Blueprint;
		TWeakObjectPtr<AActor> Temp;
	};

	struct FJob
	{
		bool bDry = false;
		bool bReplace = false;
		TArray<FString> NameFilters;
		TArray<FBlueprintJob> Blueprints;
		TArray<FString> Levels;
		FString OriginalMap;
		int32 Stage = 0;
		int32 LevelIndex = 0;
		bool bLevelLoaded = false;
		int32 WaitFrames = 0;
		int32 TotalAdded = 0;
		int32 TotalRemoved = 0;
		int32 ChangedAssets = 0;
	};

	static bool bRunning = false;

	static bool PassesFilter(const FJob& Job, const FString& Name)
	{
		if (Job.NameFilters.IsEmpty())
			return true;
		for (const FString& Filter : Job.NameFilters)
			if (Name.Contains(Filter))
				return true;
		return false;
	}

	static UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	static int32 RemoveAutoNodes(UBlueprint* Blueprint)
	{
		USimpleConstructionScript* Construction = Blueprint->SimpleConstructionScript;
		if (!Construction)
			return 0;
		int32 Removed = 0;
		const TArray<USCS_Node*> Nodes = Construction->GetAllNodes();
		for (USCS_Node* Node : Nodes)
		{
			if (Node && Node->ComponentTemplate && Node->ComponentTemplate->ComponentTags.Contains(AutoTag))
			{
				Construction->RemoveNodeAndPromoteChildren(Node);
				++Removed;
			}
		}
		return Removed;
	}

	static void CompileAndSave(UBlueprint* Blueprint)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
		UEditorLoadingAndSavingUtils::SavePackages({ Blueprint->GetPackage() }, false);
	}

	// ① BP 를 하늘 높이 임시로 띄운다(검사는 몇 프레임 뒤에).
	static void SpawnBlueprintTemps(FJob& Job, UWorld* World)
	{
		for (int32 Index = 0; Index < Job.Blueprints.Num(); ++Index)
		{
			FBlueprintJob& Entry = Job.Blueprints[Index];
			UBlueprint* Blueprint = Entry.Blueprint.Get();
			if (!Blueprint || !Blueprint->GeneratedClass)
				continue;
			if (Job.bReplace && !Job.bDry)
			{
				const int32 Removed = RemoveAutoNodes(Blueprint);
				if (Removed > 0)
				{
					Job.TotalRemoved += Removed;
					FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
					FKismetEditorUtilities::CompileBlueprint(Blueprint);
				}
			}
			FActorSpawnParameters Params;
			Params.bTemporaryEditorActor = true;
			Params.bHideFromSceneOutliner = true;
			Params.ObjectFlags |= RF_Transient;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			const FVector Location(Index * 20000.0f, 0.0f, TempSpawnZ);
			Entry.Temp = World->SpawnActor<AActor>(Blueprint->GeneratedClass, FTransform(Location), Params);
		}
	}

	static void GatherAttached(AActor* Actor, TSet<const AActor*>& Out)
	{
		Out.Add(Actor);
		TArray<AActor*> Children;
		Actor->GetAttachedActors(Children);
		for (AActor* Child : Children)
			if (!Out.Contains(Child))
				GatherAttached(Child, Out);
	}

	// ② 띄운 BP 하나를 훑어 자리를 고르고 BP 에 LootSlot 부품으로 넣는다.
	static void ProcessBlueprint(FJob& Job, FBlueprintJob& Entry, UWorld* World)
	{
		UBlueprint* Blueprint = Entry.Blueprint.Get();
		AActor* Temp = Entry.Temp.Get();
		if (!Blueprint || !Temp)
		{
			UE_LOG(LogTemp, Warning, TEXT("Auto loot slots: %s - could not spawn, skipped"),
				Blueprint ? *Blueprint->GetName() : TEXT("(missing)"));
			return;
		}

		FScene Scene;
		Scene.World = World;
		GatherAttached(Temp, Scene.Only);
		const FVector Origin = Temp->GetActorLocation();
		Scene.GroundZ = Origin.Z;

		FBox Bounds(ForceInit);
		for (const AActor* Actor : Scene.Only)
			Bounds += Actor->GetComponentsBoundingBox(true);
		// 덮는 칸: 가장자리 25% 를 깎고 칸 단위로(게임에서 칸을 세는 법과 같게).
		const float Cell = MapBuild::DesignCellSize;
		FIntPoint MinCell(0, 0), MaxCell(0, 0);
		if (Bounds.IsValid)
		{
			const float Margin = Cell * 0.25f;
			MinCell = FIntPoint(FMath::RoundToInt((Bounds.Min.X - Origin.X + Margin) / Cell), FMath::RoundToInt((Bounds.Min.Y - Origin.Y + Margin) / Cell));
			MaxCell = FIntPoint(FMath::RoundToInt((Bounds.Max.X - Origin.X - Margin) / Cell), FMath::RoundToInt((Bounds.Max.Y - Origin.Y - Margin) / Cell));
			MaxCell.X = FMath::Max(MaxCell.X, MinCell.X);
			MaxCell.Y = FMath::Max(MaxCell.Y, MinCell.Y);
		}
		const float Inset = 150.0f;
		Scene.Region = FBox2D(
			FVector2D(Origin.X + MinCell.X * Cell - Cell * 0.5f + Inset, Origin.Y + MinCell.Y * Cell - Cell * 0.5f + Inset),
			FVector2D(Origin.X + MaxCell.X * Cell + Cell * 0.5f - Inset, Origin.Y + MaxCell.Y * Cell + Cell * 0.5f - Inset));
		Scene.TopZ = (Bounds.IsValid ? Bounds.Max.Z : Origin.Z) + 100.0f;
		Scene.BottomZ = FMath::Min(Bounds.IsValid ? Bounds.Min.Z : Origin.Z, Origin.Z) - 200.0f;

		TArray<FVector> Existing;
		for (const AActor* Actor : Scene.Only)
		{
			TInlineComponentArray<ULootSlotComponent*> Slots(const_cast<AActor*>(Actor));
			for (const ULootSlotComponent* Slot : Slots)
				Existing.Add(SlotBottom(Slot));
		}

		FResult Result;
		Result.Cells = (MaxCell.X - MinCell.X + 1) * (MaxCell.Y - MinCell.Y + 1);
		PickSlots(Scene, Existing, Result);
		const FTransform ActorTransform = Temp->GetActorTransform();
		World->EditorDestroyActor(Temp, false);

		UE_LOG(LogTemp, Display, TEXT("Auto loot slots: %s cells=%d target=%d existing=%d walkable=%d reachable=%d candidates=%d added=%d%s"),
			*Blueprint->GetName(), Result.Cells, Result.Target, Result.Existing, Result.Walkable, Result.Reachable, Result.Candidates, Result.Picked.Num(),
			Job.bDry ? TEXT(" (dry)") : TEXT(""));
		if (Job.bDry || Result.Picked.IsEmpty())
		{
			// Replace 로 지운 것만 있으면 그것도 저장한다.
			if (!Job.bDry && Job.bReplace && Blueprint->GetPackage()->IsDirty())
				CompileAndSave(Blueprint);
			return;
		}

		USimpleConstructionScript* Construction = Blueprint->SimpleConstructionScript;
		if (!Construction)
		{
			UE_LOG(LogTemp, Warning, TEXT("Auto loot slots: %s has no construction script - skipped"), *Blueprint->GetName());
			return;
		}
		const AActor* Defaults = Blueprint->GeneratedClass->GetDefaultObject<AActor>();
		USCS_Node* AttachTo = nullptr;
		// C++ 부모가 뿌리 부품을 가지면 맨 위에 넣어도 그 뿌리에 붙는다. 아니면 BP 의 뿌리 부품 밑에.
		if (!Defaults->GetRootComponent())
			AttachTo = Construction->GetDefaultSceneRootNode()
				? Construction->GetDefaultSceneRootNode()
				: (Construction->GetRootNodes().IsEmpty() ? nullptr : Construction->GetRootNodes()[0]);
		Blueprint->Modify();
		Construction->Modify();
		for (const FTransform& SlotTransform : Result.Picked)
		{
			const FName Name = Construction->GenerateNewComponentName(ULootSlotComponent::StaticClass(), AutoTag);
			USCS_Node* Node = Construction->CreateNode(ULootSlotComponent::StaticClass(), Name);
			ULootSlotComponent* Template = CastChecked<ULootSlotComponent>(Node->ComponentTemplate);
			const FTransform Relative = SlotTransform.GetRelativeTransform(ActorTransform);
			Template->SetRelativeLocation_Direct(Relative.GetLocation());
			Template->SetRelativeRotation_Direct(Relative.Rotator());
			Template->ComponentTags.Add(AutoTag);
			if (AttachTo)
				AttachTo->AddChildNode(Node);
			else
				Construction->AddNode(Node);
		}
		CompileAndSave(Blueprint);
		Job.TotalAdded += Result.Picked.Num();
		++Job.ChangedAssets;
	}

	// ③ 열린 시설 레벨을 훑어 Loot Slot 액터를 놓는다. 레벨 가운데(원점) = 게임에서 시설 칸들의 가운데.
	static void ProcessLevel(FJob& Job, UWorld* World)
	{
		const FString Name = FPackageName::GetShortName(World->GetOutermost()->GetName());
		FScene Scene;
		Scene.World = World;
		Scene.GroundZ = 0.0f;

		TArray<AActor*> ToRemove;
		TArray<FVector> Existing;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (Job.bReplace && !Job.bDry && It->IsA<ALootSlotActor>() && It->Tags.Contains(AutoTag))
			{
				ToRemove.Add(*It);
				continue;
			}
			TInlineComponentArray<ULootSlotComponent*> Slots(*It);
			for (const ULootSlotComponent* Slot : Slots)
				Existing.Add(SlotBottom(Slot));
		}
		for (AActor* Actor : ToRemove)
			World->EditorDestroyActor(Actor, true);
		const int32 RemovedHere = ToRemove.Num();
		Job.TotalRemoved += RemovedHere;

		// 시설 크기: 진짜 물건(하늘·큰 판은 빼고)이 차지한 범위.
		FBox Bounds(ForceInit);
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			TInlineComponentArray<UPrimitiveComponent*> Primitives(*It);
			for (const UPrimitiveComponent* Primitive : Primitives)
			{
				if (!IsSolid(Scene, Primitive))
					continue;
				const FBox Box = Primitive->Bounds.GetBox();
				if (Box.GetSize().GetMax() < 30000.0f)
					Bounds += Box;
			}
		}
		if (!Bounds.IsValid)
		{
			UE_LOG(LogTemp, Warning, TEXT("Auto loot slots: %s has no solid geometry - skipped"), *Name);
			return;
		}

		const float Cell = MapBuild::DesignCellSize;
		const float Inset = 150.0f;
		int32 CellsX = 0, CellsY = 0;
		FVector2D Half(Bounds.GetExtent().X, Bounds.GetExtent().Y);
		if (ParseCells(Name, CellsX, CellsY))
		{
			// 이름의 칸 수로 범위를 자른다. 긴 쪽을 물건이 더 넓게 퍼진 축에 맞춘다.
			const float Long = FMath::Max(CellsX, CellsY) * Cell * 0.5f;
			const float Short = FMath::Min(CellsX, CellsY) * Cell * 0.5f;
			const bool bXLonger = Bounds.GetExtent().X >= Bounds.GetExtent().Y;
			Half = FVector2D(bXLonger ? Long : Short, bXLonger ? Short : Long);
		}
		else
		{
			CellsX = FMath::Max(1, FMath::RoundToInt(Bounds.GetSize().X / Cell));
			CellsY = FMath::Max(1, FMath::RoundToInt(Bounds.GetSize().Y / Cell));
		}
		Scene.Region = FBox2D(
			FVector2D(FMath::Max(-Half.X, Bounds.Min.X) + Inset, FMath::Max(-Half.Y, Bounds.Min.Y) + Inset),
			FVector2D(FMath::Min(Half.X, Bounds.Max.X) - Inset, FMath::Min(Half.Y, Bounds.Max.Y) - Inset));
		Scene.TopZ = Bounds.Max.Z + 100.0f;
		Scene.BottomZ = FMath::Min(Bounds.Min.Z, 0.0f) - 200.0f;

		FResult Result;
		Result.Cells = CellsX * CellsY;
		PickSlots(Scene, Existing, Result);
		UE_LOG(LogTemp, Display, TEXT("Auto loot slots: %s cells=%d target=%d existing=%d walkable=%d reachable=%d candidates=%d added=%d removed=%d%s"),
			*Name, Result.Cells, Result.Target, Result.Existing, Result.Walkable, Result.Reachable, Result.Candidates, Result.Picked.Num(), RemovedHere,
			Job.bDry ? TEXT(" (dry)") : TEXT(""));
		if (Job.bDry)
			return;

		for (int32 Index = 0; Index < Result.Picked.Num(); ++Index)
		{
			const FTransform& Transform = Result.Picked[Index];
			FActorSpawnParameters Params;
			Params.OverrideLevel = World->PersistentLevel;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ALootSlotActor* Slot = World->SpawnActor<ALootSlotActor>(Transform.GetLocation(), Transform.Rotator(), Params);
			if (!Slot)
				continue;
			Slot->Tags.Add(AutoTag);
			Slot->SetActorLabel(FString::Printf(TEXT("AutoLootSlot_%02d"), Result.Existing + Index + 1));
			Slot->SetFolderPath(TEXT("LootSlots"));
			++Job.TotalAdded;
		}
		if (!Result.Picked.IsEmpty() || RemovedHere > 0)
		{
			UEditorLoadingAndSavingUtils::SaveDirtyPackages(true, false);
			++Job.ChangedAssets;
		}
	}

	// 한 프레임에 한 걸음씩. 물리 검사가 새로 띄운 것·새로 연 레벨을 보려면 몇 프레임 기다려야 해서 나눠 돈다.
	static bool Tick(TSharedRef<FJob> Job)
	{
		if (Job->WaitFrames > 0)
		{
			--Job->WaitFrames;
			return true;
		}
		UWorld* World = EditorWorld();
		if (!World || GEditor->PlayWorld)
		{
			UE_LOG(LogTemp, Error, TEXT("Auto loot slots: stopped (editor world gone or Play started)"));
			bRunning = false;
			return false;
		}

		switch (Job->Stage)
		{
		case 0:
			SpawnBlueprintTemps(*Job, World);
			Job->WaitFrames = SettleFrames;
			Job->Stage = 1;
			return true;
		case 1:
			for (FBlueprintJob& Entry : Job->Blueprints)
				ProcessBlueprint(*Job, Entry, World);
			Job->Stage = 2;
			return true;
		case 2:
			if (Job->LevelIndex >= Job->Levels.Num())
			{
				Job->Stage = 3;
				return true;
			}
			if (!Job->bLevelLoaded)
			{
				if (!UEditorLoadingAndSavingUtils::LoadMap(Job->Levels[Job->LevelIndex]))
				{
					UE_LOG(LogTemp, Warning, TEXT("Auto loot slots: could not open %s - skipped"), *Job->Levels[Job->LevelIndex]);
					++Job->LevelIndex;
					return true;
				}
				Job->bLevelLoaded = true;
				Job->WaitFrames = SettleFrames;
				return true;
			}
			ProcessLevel(*Job, World);
			Job->bLevelLoaded = false;
			++Job->LevelIndex;
			return true;
		default:
			if (!Job->Levels.IsEmpty() && !Job->OriginalMap.IsEmpty())
				UEditorLoadingAndSavingUtils::LoadMap(Job->OriginalMap);
			UE_LOG(LogTemp, Display, TEXT("Auto loot slots: done - blueprints=%d levels=%d changed_assets=%d added=%d removed_auto=%d%s"),
				Job->Blueprints.Num(), Job->Levels.Num(), Job->ChangedAssets, Job->TotalAdded, Job->TotalRemoved,
				Job->bDry ? TEXT(" (dry run, nothing changed)") : TEXT(""));
			bRunning = false;
			return false;
		}
	}

	static void Run(const TArray<FString>& Args)
	{
		if (bRunning)
		{
			UE_LOG(LogTemp, Warning, TEXT("Auto loot slots: already running"));
			return;
		}
		UWorld* World = EditorWorld();
		if (!World || GEditor->PlayWorld)
		{
			UE_LOG(LogTemp, Error, TEXT("Auto loot slots: stop Play first"));
			return;
		}

		TSharedRef<FJob> Job = MakeShared<FJob>();
		bool bTiles = true, bLevels = true;
		for (const FString& Arg : Args)
		{
			if (Arg.Equals(TEXT("Dry"), ESearchCase::IgnoreCase)) Job->bDry = true;
			else if (Arg.Equals(TEXT("Replace"), ESearchCase::IgnoreCase)) Job->bReplace = true;
			else if (Arg.Equals(TEXT("Tiles"), ESearchCase::IgnoreCase)) bLevels = false;
			else if (Arg.Equals(TEXT("Levels"), ESearchCase::IgnoreCase)) bTiles = false;
			else Job->NameFilters.Add(Arg);
		}

		// 게임이 실제로 쓰는 목록(DA_MapAssets)에서 타일·시설 BP 와 시설 레벨을 모은다.
		const UMapAssetSet* Assets = LoadObject<UMapAssetSet>(nullptr, TEXT("/Game/PG/LevelDesign/DA_MapAssets.DA_MapAssets"));
		if (!Assets)
			Assets = GetDefault<UMapAssetSet>();
		TSet<FSoftObjectPath> SeenClasses;
		for (TFieldIterator<FProperty> It(UMapAssetSet::StaticClass()); It; ++It)
		{
			if (const FSoftClassProperty* ClassProperty = CastField<FSoftClassProperty>(*It))
			{
				if (!bTiles || !ClassProperty->MetaClass || !ClassProperty->MetaClass->IsChildOf(AActor::StaticClass()))
					continue;
				const FSoftObjectPath Path = ClassProperty->GetPropertyValuePtr_InContainer(const_cast<UMapAssetSet*>(Assets))->ToSoftObjectPath();
				if (Path.IsNull() || SeenClasses.Contains(Path) || !PassesFilter(*Job, Path.GetAssetName()))
					continue;
				SeenClasses.Add(Path);
				UClass* Class = Cast<UClass>(Path.TryLoad());
				UBlueprint* Blueprint = Class ? UBlueprint::GetBlueprintFromClass(Class) : nullptr;
				if (!Blueprint)
				{
					UE_LOG(LogTemp, Warning, TEXT("Auto loot slots: %s is not a blueprint - skipped"), *Path.ToString());
					continue;
				}
				Job->Blueprints.Add({ Blueprint, nullptr });
			}
			else if (const FSoftObjectProperty* ObjectProperty = CastField<FSoftObjectProperty>(*It))
			{
				if (!bLevels || ObjectProperty->PropertyClass != UWorld::StaticClass())
					continue;
				const FSoftObjectPath Path = ObjectProperty->GetPropertyValuePtr_InContainer(const_cast<UMapAssetSet*>(Assets))->ToSoftObjectPath();
				const FString Package = Path.GetLongPackageName();
				if (!Path.IsNull() && !Job->Levels.Contains(Package) && PassesFilter(*Job, Path.GetAssetName()))
					Job->Levels.Add(Package);
			}
		}

		// 레벨을 하나씩 열어야 하므로, 지금 레벨에 저장 안 한 변경이 있으면 레벨은 건너뛴다(날아가지 않게).
		if (!Job->Levels.IsEmpty())
		{
			TArray<UPackage*> DirtyMaps;
			FEditorFileUtils::GetDirtyWorldPackages(DirtyMaps);
			if (!DirtyMaps.IsEmpty())
			{
				UE_LOG(LogTemp, Error, TEXT("Auto loot slots: the open level has unsaved changes - save it first. Levels skipped (%d)"), Job->Levels.Num());
				Job->Levels.Reset();
			}
		}
		const FString Current = World->GetOutermost()->GetName();
		if (!Current.StartsWith(TEXT("/Temp/")))
			Job->OriginalMap = Current;

		UE_LOG(LogTemp, Display, TEXT("Auto loot slots: start - blueprints=%d levels=%d%s%s"),
			Job->Blueprints.Num(), Job->Levels.Num(), Job->bDry ? TEXT(" dry") : TEXT(""), Job->bReplace ? TEXT(" replace") : TEXT(""));
		bRunning = true;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Job](float)
		{
			return Tick(Job);
		}));
	}

	static FAutoConsoleCommand Command(
		TEXT("PG.AutoPlaceLootSlots"),
		TEXT("Place Loot Slots automatically in the tile/facility blueprints and facility levels listed in DA_MapAssets. Args: Dry, Replace, Tiles, Levels, <name filter>"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&Run));
}

#endif
