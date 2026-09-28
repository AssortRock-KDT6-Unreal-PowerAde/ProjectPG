#include "Objects/PGWearableColors.h"

#include "Common/PGVisualSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Objects/PGItemValue.h"

namespace
{
	const FString QuantumMeshes = TEXT("/Game/QuantumCharacter/Mesh/Modules/");
	const FString QuantumMaterials = TEXT("/Game/PG/Characters/Quantum/Materials/");
	const FString QuantumFloor = TEXT("/Game/PG/Characters/Quantum/FloorMeshes/");
	const FString SurvivalFloor = TEXT("/Game/PG/Props/FloorItems/");
	const FString SurvivalMaterials = TEXT("/Game/Survival_Character/Materials/");
	const FString SurvivalColors = TEXT("/Game/PG/Props/FloorItems/Materials/");

	FString AssetPath(const FString& Dir, const TCHAR* Name) { return Dir + Name + TEXT(".") + Name; }

	// 한 벌(슬롯 + 메시)을 정해 두고 색만 바꿔 가며 행을 찍는다.
	struct FGarment
	{
		const TCHAR* BaseItemId;
		EPGWearSlot Slot;
		FString FloorMeshPath;
		FString SkeletalPath;  // 비면 스켈레탈 아님
		FString StaticPath;    // 비면 스태틱 아님
		FName AttachBone;
		FTransform AttachOffset;
		bool bOffsetInBodySpace = false;
	};

	void AddColor(TArray<FPGWearableColor>& Out, const FGarment& G, const TCHAR* ItemId, const FText& DisplayName, const FString& MaterialPath)
	{
		FPGWearableColor Row;
		Row.ItemId = ItemId;
		Row.BaseItemId = G.BaseItemId;
		Row.Slot = G.Slot;
		Row.DisplayName = DisplayName;
		Row.Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(MaterialPath));
		Row.FloorMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(G.FloorMeshPath));
		if (!G.SkeletalPath.IsEmpty())
			Row.WornSkeletalMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(G.SkeletalPath));
		if (!G.StaticPath.IsEmpty())
			Row.WornStaticMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(G.StaticPath));
		Row.AttachBone = G.AttachBone;
		Row.AttachOffset = G.AttachOffset;
		Row.bOffsetInBodySpace = G.bOffsetInBodySpace;
		Out.Add(Row);
	}
}

const TArray<FPGWearableColor>& UPGWearableColorLibrary::GetAllColors()
{
	// 한 번 만들어 두고 쓴다(이 함수는 바닥 아이템·루팅마다 불린다). 에디터에서 표를 고치면 다음에 부를 때 다시 읽는다.
	static TArray<FPGWearableColor> Colors;
	static TWeakObjectPtr<UDataTable> WatchedTable;
	static bool bBuilt = false;
	static bool bTableChanged = false;
	if (bBuilt && !bTableChanged)
		return Colors;
	bBuilt = true;
	bTableChanged = false;
	Colors.Reset();

	const TSoftObjectPtr<UDataTable>& Designed = UPGVisualSettings::Get().WearableTable;
	UDataTable* Table = Designed.IsNull() ? nullptr : Designed.LoadSynchronous();
	if (Table && Table->GetRowStruct() == FPGWearableColor::StaticStruct())
	{
		// 표를 고치면(에디터) 다시 읽도록 표시만 해 둔다.
		if (WatchedTable.Get() != Table)
		{
			Table->OnDataTableChanged().AddLambda([]() { bTableChanged = true; });
			WatchedTable = Table;
		}
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			FPGWearableColor Row = *reinterpret_cast<const FPGWearableColor*>(Pair.Value);
			if (Row.ItemId.IsNone())
				Row.ItemId = Pair.Key;
			Colors.Add(Row);
		}
	}
	else if (!Designed.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("PGVisuals: wearable table %s not usable — using the code table"), *Designed.ToString());
	}
	if (Colors.IsEmpty())
		Colors = BuildDefaultColors();
	return Colors;
}

int32 UPGWearableColorLibrary::WriteDefaultColorsToTable(UDataTable* Table)
{
	if (!IsValid(Table) || Table->GetRowStruct() != FPGWearableColor::StaticStruct())
		return 0;
	const TArray<FPGWearableColor> Defaults = BuildDefaultColors();
	for (const FPGWearableColor& Row : Defaults)
		Table->AddRow(Row.ItemId, Row);
	return Defaults.Num();
}

TArray<FPGWearableColor> UPGWearableColorLibrary::BuildDefaultColors()
{
	// 색을 추가할 때: Tools/make_quantum_wearables.py 의 COLORS 에 한 줄 + 착장 표(DT_PGWearables, 없으면 여기)에 한 줄.
	// 원래색 행(ItemId == BaseItemId)은 지우지 않는다 — 루팅 테이블과 카탈로그가 원래색 ID 를 쓴다.
	{
		TArray<FPGWearableColor> Out;
		auto QMat = [](const TCHAR* Name) { return AssetPath(QuantumMaterials, Name); };

		const FGarment Shirt{ TEXT("Shirt"), EPGWearSlot::Top, AssetPath(QuantumFloor, TEXT("SM_PGQFloor_Shirt")), AssetPath(QuantumMeshes, TEXT("SKM_Shirt_RolledUp_Blue")) };
		AddColor(Out, Shirt, TEXT("Shirt"), NSLOCTEXT("Wearable", "ShirtBlue", "파란 셔츠"), QMat(TEXT("MI_PGQ_Shirt_Blue")));
		AddColor(Out, Shirt, TEXT("Shirt_Red"), NSLOCTEXT("Wearable", "ShirtRed", "빨간 셔츠"), QMat(TEXT("MI_PGQ_Shirt_Red")));
		AddColor(Out, Shirt, TEXT("Shirt_Olive"), NSLOCTEXT("Wearable", "ShirtOlive", "올리브 셔츠"), QMat(TEXT("MI_PGQ_Shirt_Olive")));
		AddColor(Out, Shirt, TEXT("Shirt_Black"), NSLOCTEXT("Wearable", "ShirtBlack", "검정 셔츠"), QMat(TEXT("MI_PGQ_Shirt_Black")));

		const FGarment Jeans{ TEXT("Pants"), EPGWearSlot::Bottom, AssetPath(QuantumFloor, TEXT("SM_PGQFloor_Jeans")), AssetPath(QuantumMeshes, TEXT("SKM_Jeans")) };
		AddColor(Out, Jeans, TEXT("Pants"), NSLOCTEXT("Wearable", "JeansDenim", "청바지"), QMat(TEXT("MI_PGQ_Jeans_Denim")));
		AddColor(Out, Jeans, TEXT("Pants_Black"), NSLOCTEXT("Wearable", "JeansBlack", "검정 바지"), QMat(TEXT("MI_PGQ_Jeans_Black")));
		AddColor(Out, Jeans, TEXT("Pants_Khaki"), NSLOCTEXT("Wearable", "JeansKhaki", "카키 바지"), QMat(TEXT("MI_PGQ_Jeans_Khaki")));

		const FGarment Vest{ TEXT("Armor_Vest"), EPGWearSlot::Vest, AssetPath(QuantumFloor, TEXT("SM_PGQFloor_Vest")), AssetPath(QuantumMeshes, TEXT("SKM_Bulletproof_Bege")) };
		AddColor(Out, Vest, TEXT("Armor_Vest"), NSLOCTEXT("Wearable", "VestBeige", "베이지 방탄조끼"), QMat(TEXT("MI_PGQ_Vest_Beige")));
		AddColor(Out, Vest, TEXT("Armor_Vest_Black"), NSLOCTEXT("Wearable", "VestBlack", "검정 방탄조끼"), QMat(TEXT("MI_PGQ_Vest_Black")));
		AddColor(Out, Vest, TEXT("Armor_Vest_Olive"), NSLOCTEXT("Wearable", "VestOlive", "올리브 방탄조끼"), QMat(TEXT("MI_PGQ_Vest_Olive")));

		// 모자는 팩 데모 블루프린트(CBP_QuantumCharacter)와 같은 값으로 머리뼈에 붙인다.
		const FGarment Cap{ TEXT("Helmet"), EPGWearSlot::Cap, AssetPath(QuantumFloor, TEXT("SM_PGQFloor_Cap")), FString(), AssetPath(QuantumMeshes, TEXT("SM_Cap_Bege")),
			TEXT("head"), FTransform(FRotator(-90.0f, 0.0f, 0.0f), FVector(0.0f, -0.5f, 0.0f)) };
		AddColor(Out, Cap, TEXT("Helmet"), NSLOCTEXT("Wearable", "CapBeige", "베이지 모자"), QMat(TEXT("MI_PGQ_Cap_Beige")));
		AddColor(Out, Cap, TEXT("Helmet_Black"), NSLOCTEXT("Wearable", "CapBlack", "검정 모자"), QMat(TEXT("MI_PGQ_Cap_Black")));
		AddColor(Out, Cap, TEXT("Helmet_Red"), NSLOCTEXT("Wearable", "CapRed", "빨간 모자"), QMat(TEXT("MI_PGQ_Cap_Red")));

		// 분홍 단발 가발(9/20, 교환소에서 드래곤 전리품과 바꾸는 특수 장비). 모델링 세션이 모자(SM_Cap_Bege)와 같은 공간으로 만들어서
		// 붙이는 뼈·변환이 모자와 똑같다. 모자 자리(Cap)라 모자와 같이 쓸 수 없다(쓰면 모자가 벗겨진다). 이마 소켓 Forehead = 광선 출발점.
		const FString WigDir = TEXT("/Game/PG/Characters/Quantum/Wig/");
		const FGarment Wig{ TEXT("Wig_Pink"), EPGWearSlot::Cap, AssetPath(WigDir, TEXT("SM_PGQFloor_Wig_Pink")), FString(), AssetPath(WigDir, TEXT("SM_PGQ_Wig_Pink")),
			TEXT("head"), FTransform(FRotator(-90.0f, 0.0f, 0.0f), FVector(0.0f, -0.5f, 0.0f)) };
		AddColor(Out, Wig, TEXT("Wig_Pink"), NSLOCTEXT("Wearable", "WigPink", "분홍 단발 가발"), AssetPath(WigDir, TEXT("M_PGQ_WigPink")));

		const FGarment Pouch{ TEXT("ChestPouch"), EPGWearSlot::Pouch, AssetPath(QuantumFloor, TEXT("SM_PGQFloor_Pouch")), AssetPath(QuantumMeshes, TEXT("SKM_Drops_1_Bege")) };
		AddColor(Out, Pouch, TEXT("ChestPouch"), NSLOCTEXT("Wearable", "PouchBeige", "베이지 파우치"), QMat(TEXT("MI_PGQ_Pouch_Beige")));
		AddColor(Out, Pouch, TEXT("ChestPouch_Black"), NSLOCTEXT("Wearable", "PouchBlack", "검정 파우치"), QMat(TEXT("MI_PGQ_Pouch_Black")));

		const FGarment Holster{ TEXT("Holster"), EPGWearSlot::Holster, AssetPath(QuantumFloor, TEXT("SM_PGQFloor_Holster")), AssetPath(QuantumMeshes, TEXT("SKM_Holster_Hard_Bege")) };
		AddColor(Out, Holster, TEXT("Holster"), NSLOCTEXT("Wearable", "HolsterBeige", "베이지 권총집"), QMat(TEXT("MI_PGQ_Holster_Beige")));
		AddColor(Out, Holster, TEXT("Holster_Black"), NSLOCTEXT("Wearable", "HolsterBlack", "검정 권총집"), QMat(TEXT("MI_PGQ_Holster_Black")));

		// 배낭·신발은 Quantum 무료 샘플에 없다. 배낭은 Survival_Character 에서 구운 스태틱 메시를 등에 붙이고, 신발은 겉모습 없이 아이템으로만 둔다.
		// 배낭: 바닥용(500삼각형)은 어깨끈이 뭉개져 등에 메면 덩어리만 남는다. 메는 용 메시(SM_PGQWorn_Backpack, 4000삼각형)를 따로 쓴다.
		// 그 메시는 Survival 캐릭터가 메고 있던 자리 그대로 구웠으므로 몸 공간 오프셋 = 0 이 출발점이다(bOffsetInBodySpace).
		// 등뼈(spine_05)에 붙여 상체와 같이 흔들린다. 어긋나면 PIE 에서 PG.WearTune Backpack x y z [pitch yaw roll] [scale] 로 맞춘 뒤 여기 숫자를 바꾼다.
		const FGarment Backpack{ TEXT("Backpack"), EPGWearSlot::Backpack, AssetPath(SurvivalFloor, TEXT("SM_PGFloor_Backpack")), FString(),
			AssetPath(TEXT("/Game/PG/Characters/Quantum/WornMeshes/"), TEXT("SM_PGQWorn_Backpack")),
			TEXT("spine_05"), FTransform::Identity, true };
		AddColor(Out, Backpack, TEXT("Backpack"), NSLOCTEXT("Wearable", "BackpackBase", "배낭"), AssetPath(SurvivalMaterials, TEXT("MI_Survival_Character_Backpack")));
		AddColor(Out, Backpack, TEXT("Backpack_Black"), NSLOCTEXT("Wearable", "BackpackBlack", "검정 배낭"), AssetPath(SurvivalColors, TEXT("MI_PGWear_Backpack_Black")));
		AddColor(Out, Backpack, TEXT("Backpack_Olive"), NSLOCTEXT("Wearable", "BackpackOlive", "올리브 배낭"), AssetPath(SurvivalColors, TEXT("MI_PGWear_Backpack_Olive")));

		const FGarment Shoes{ TEXT("Shoes"), EPGWearSlot::Shoes, AssetPath(SurvivalFloor, TEXT("SM_PGFloor_Shoes")) };
		AddColor(Out, Shoes, TEXT("Shoes"), NSLOCTEXT("Wearable", "ShoesBase", "운동화"), AssetPath(SurvivalMaterials, TEXT("MI_Survival_Character_Shoes")));
		AddColor(Out, Shoes, TEXT("Shoes_Black"), NSLOCTEXT("Wearable", "ShoesBlack", "검정 신발"), AssetPath(SurvivalColors, TEXT("MI_PGWear_Shoes_Black")));
		AddColor(Out, Shoes, TEXT("Shoes_Brown"), NSLOCTEXT("Wearable", "ShoesBrown", "갈색 신발"), AssetPath(SurvivalColors, TEXT("MI_PGWear_Shoes_Brown")));
		return Out;
	}
}

bool UPGWearableColorLibrary::FindWearableColor(FName ItemId, FPGWearableColor& OutColor)
{
	for (const FPGWearableColor& Color : GetAllColors())
	{
		if (Color.ItemId == ItemId)
		{
			OutColor = Color;
			return true;
		}
	}
	return false;
}

TArray<FPGWearableColor> UPGWearableColorLibrary::GetColorsForBase(FName BaseItemId)
{
	TArray<FPGWearableColor> Result;
	for (const FPGWearableColor& Color : GetAllColors())
	{
		if (Color.BaseItemId == BaseItemId)
			Result.Add(Color);
	}
	return Result;
}

FName UPGWearableColorLibrary::PickColorVariant(FName ItemId, int64 Seed)
{
	// 원래색 ID 만 굴린다. Pants_Black 처럼 이미 색이 정해진 ID(상점·퀘스트 보상 등)는 건드리지 않는다.
	const TArray<FPGWearableColor> Colors = GetColorsForBase(ItemId);
	if (Colors.Num() <= 1)
		return ItemId;
	// ItemId 해시를 섞어서 같은 시드라도 하의와 신발 색이 따로 논다.
	// [버그 수정 9/23] 예전에는 GetTypeHash(FName) 을 섞었다. 그 값은 글자가 아니라 "이 실행에서 이름이 몇 번째로 만들어졌나" 번호라서,
	//   에셋을 읽는 순서만 바뀌어도(착장 표를 데이터 테이블로 옮기자 셔츠 색이 달라졌다) 같은 시드에서 다른 색이 나왔다.
	//   서버와 클라이언트도 번호가 달라 같은 맵에서 옷 색이 어긋날 수 있었다. 글자로 해시하면 어디서 돌려도 같다.
	const int64 Mixed = Seed ^ (static_cast<int64>(GetTypeHash(ItemId.ToString())) * 2654435761LL);
	FRandomStream Stream(static_cast<int32>(Mixed ^ (Mixed >> 32)));
	return Colors[Stream.RandRange(0, Colors.Num() - 1)].ItemId;
}

FText UPGWearableColorLibrary::GetItemDisplayName(FName ItemId)
{
	// 옷은 색마다 이름이 다르다("검정 바지") — 착장 표가 먼저.
	FPGWearableColor Color;
	if (FindWearableColor(ItemId, Color) && !Color.DisplayName.IsEmpty())
		return Color.DisplayName;
	// 그 밖의 아이템 이름은 등급 표(Docs/DT_PGItemValue.csv)의 DisplayName 이다. 무기는 "권총 (레어)" 처럼 등급이 이름에 붙는다.
	// 9/22 전에는 여기 cpp 에 이름 목록(LooseItems)을 따로 두었는데, 표와 두 군데라 어긋날 자리가 생겨 표로 합쳤다.
	const FText TableName = UPGItemValueLibrary::GetDisplayName(ItemId);
	if (!TableName.IsEmpty())
		return TableName;
	return FText::FromName(ItemId);
}

TSoftObjectPtr<UStaticMesh> UPGWearableColorLibrary::FindItemFloorMeshVariant(FName ItemId, int32 Variant)
{
	// 연료통 세 색(Tools/make_fuel_can.py). 빨강이 기본(기획서 그림) — 버리기·더미처럼 변형 번호가 없으면 0번.
	// 9/23 블루프린트 분리: 목록은 설정(ProjectPG Visuals > Fuel Can Meshes)에 있다. 기본값 = 원래 세 색.
	const TArray<TSoftObjectPtr<UStaticMesh>>& FuelCans = UPGVisualSettings::Get().FuelCanMeshes;
	if (ItemId == TEXT("Fuel") && !FuelCans.IsEmpty())
		return FuelCans[FMath::Abs(Variant) % FuelCans.Num()];
	return nullptr;
}

TSoftObjectPtr<UStaticMesh> UPGWearableColorLibrary::FindItemFloorMesh(FName ItemId)
{
	FPGWearableColor Color;
	if (FindWearableColor(ItemId, Color))
		return Color.FloorMesh;
	// 착장이 아니면 등급 표의 FloorMesh. 등급 변형(Pistol_Epic)은 칸을 비워 두고 원래 총(Pistol)의 메시를 쓴다 —
	// 등급은 모양이 아니라 이름·색 테두리로만 구분하므로 메시를 세 벌 만들 이유가 없다.
	if (const FPGItemValueRow* Row = UPGItemValueLibrary::FindRow(ItemId); Row && Row->ItemId == ItemId)
	{
		if (!Row->FloorMesh.IsNull())
			return Row->FloorMesh;
		if (!Row->BaseItemId.IsNone() && Row->BaseItemId != ItemId)
			if (const FPGItemValueRow* Base = UPGItemValueLibrary::FindRow(Row->BaseItemId); Base && Base->ItemId == Row->BaseItemId)
				return Base->FloorMesh;
	}
	return nullptr;
}
