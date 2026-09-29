#include "Objects/PGItemValue.h"

#include "Engine/DataTable.h"
#include "Engine/StaticMesh.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Objects/PGWearableColors.h"
#include "UObject/Package.h"

// 익명 namespace 가 아니라 이름을 붙인다 — 유니티 빌드에서 다른 cpp 의 같은 이름(Row, Cache ...)과 부딪히지 않게.
namespace PGItemValueLocal
{
	// 예비 표 한 줄. 열 순서는 CSV 와 같다(검사 스크립트가 CSV 와 한 줄씩 맞춰 본다).
	FPGItemValueRow MakeRow(const TCHAR* ItemId, const TCHAR* BaseItemId, const TCHAR* DisplayName, EPGItemCategory Category, EPGItemGrade Grade,
		int32 BaseValue, bool bTradable, float LootWeight, int32 LootMin, int32 LootMax, int32 LootCap,
		const TCHAR* LootMeshObjectId, const TCHAR* FloorMesh, const TCHAR* AmmoItemId, const TCHAR* Reason)
	{
		FPGItemValueRow R;
		R.ItemId = ItemId;
		R.BaseItemId = BaseItemId;
		R.DisplayName = DisplayName;
		R.Category = Category;
		R.Grade = Grade;
		R.BaseValue = BaseValue;
		R.bTradable = bTradable;
		R.LootWeight = LootWeight;
		R.LootMin = LootMin;
		R.LootMax = LootMax;
		R.LootCap = LootCap;
		R.LootMeshObjectId = LootMeshObjectId ? FName(LootMeshObjectId) : NAME_None;
		if (FloorMesh)
			R.FloorMesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(FloorMesh));
		R.AmmoItemId = AmmoItemId ? FName(AmmoItemId) : NAME_None;
		R.Reason = Reason;
		return R;
	}

	// 한 번 읽은 표. ItemId → 몇 번째 행인지도 같이 둔다(바닥 아이템 수천 개가 이름으로 찾으니 매번 훑지 않게).
	struct FItemTableCache
	{
		TArray<FPGItemValueRow> Rows;
		TMap<FName, int32> IndexById;
		FString Source;
		bool bLoaded = false;
	};

	FItemTableCache& TableCache()
	{
		static FItemTableCache Instance;
		return Instance;
	}

	// 읽어 온 행 다듬기. ItemId 칸을 비워 두면 행 이름을, BaseItemId 를 비워 두면 자기 자신을 쓴다 — 표를 고치는 사람이 덜 적게.
	void NormalizeRow(FPGItemValueRow& Row, FName RowName)
	{
		if (Row.ItemId.IsNone())
			Row.ItemId = RowName;
		if (Row.BaseItemId.IsNone())
			Row.BaseItemId = Row.ItemId;
		Row.LootMax = FMath::Max(Row.LootMin, Row.LootMax);
	}

	bool ReadDataTable(const UDataTable* Table, TArray<FPGItemValueRow>& Out)
	{
		if (!IsValid(Table) || Table->GetRowStruct() != FPGItemValueRow::StaticStruct())
			return false;
		Table->ForeachRow<FPGItemValueRow>(TEXT("PGItemValue"), [&Out](const FName& RowName, const FPGItemValueRow& Row)
		{
			FPGItemValueRow Copy = Row;
			NormalizeRow(Copy, RowName);
			Out.Add(Copy);
		});
		return Out.Num() > 0;
	}

	// CSV 를 메모리 속 임시 DataTable 로 읽는다. 에디터의 "CSV 가져오기"와 같은 해석기(CreateTableFromCSVString)라
	// 에디터에서 이 CSV 를 DataTable 애셋으로 가져와도 똑같이 읽힌다.
	// 문제(열 이름 오타, 없는 등급 이름 등)가 하나라도 있으면 실패로 본다 — 틀린 칸이 기본값(노말·0원)으로 조용히 들어가는 것보다 낫다.
	bool ReadCsv(const FString& Path, TArray<FPGItemValueRow>& Out, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			OutError = TEXT("file not found");
			return false;
		}
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage(), NAME_None, RF_Transient);
		Table->RowStruct = FPGItemValueRow::StaticStruct();
		const TArray<FString> Problems = Table->CreateTableFromCSVString(Text);
		for (const FString& Problem : Problems)
			UE_LOG(LogPGObjects, Warning, TEXT("PGItemValue: CSV problem in %s: %s"), *Path, *Problem);
		const bool bRead = Problems.IsEmpty() && ReadDataTable(Table, Out);
		Table->MarkAsGarbage();
		if (!bRead)
			OutError = Problems.IsEmpty() ? TEXT("no rows") : FString::Printf(TEXT("%d problems"), Problems.Num());
		return bRead;
	}

	FString ResolveCsvPath(const UPGItemGradeSettings* Settings)
	{
		FString Path;
		if (!FParse::Value(FCommandLine::Get(), TEXT("PGItemTableCsv="), Path))
			Path = Settings ? Settings->ItemValueCsv : FString(TEXT("Docs/DT_PGItemValue.csv"));
		if (FPaths::IsRelative(Path))
			Path = FPaths::Combine(FPaths::ProjectDir(), Path);
		return FPaths::ConvertRelativePathToFull(Path);
	}

	void LoadTable(FItemTableCache& Cache)
	{
		Cache.Rows.Reset();
		Cache.IndexById.Reset();
		Cache.Source.Reset();
		const UPGItemGradeSettings* Settings = GetDefault<UPGItemGradeSettings>();

		// ① DataTable 애셋(패키징 빌드용)
		if (Settings && !Settings->ItemValueTable.IsNull())
		{
			if (ReadDataTable(Settings->ItemValueTable.LoadSynchronous(), Cache.Rows))
				Cache.Source = FString::Printf(TEXT("DataTable:%s"), *Settings->ItemValueTable.ToString());
			else
			{
				UE_LOG(LogPGObjects, Warning, TEXT("PGItemValue: DataTable %s is missing or has the wrong row type - trying the CSV"),
					*Settings->ItemValueTable.ToString());
				Cache.Rows.Reset();
			}
		}
		// ② CSV
		if (Cache.Source.IsEmpty())
		{
			const FString CsvPath = ResolveCsvPath(Settings);
			FString Error;
			if (ReadCsv(CsvPath, Cache.Rows, Error))
				Cache.Source = FString::Printf(TEXT("CSV:%s"), *CsvPath);
			else
			{
				// ③ 예비 표. 게임은 계속 돌아가야 하지만, CSV 를 고쳐도 반영이 안 되는 상태라는 걸 반드시 남긴다.
				Cache.Rows = UPGItemValueLibrary::BuildFallbackRows();
				Cache.Source = TEXT("Fallback");
				UE_LOG(LogPGObjects, Warning, TEXT("PGItemValue: could not read %s (%s) - using the C++ fallback table. Edits to the CSV are NOT applied."),
					*CsvPath, *Error);
			}
		}

		for (int32 Index = 0; Index < Cache.Rows.Num(); ++Index)
		{
			const FName Id = Cache.Rows[Index].ItemId;
			// UE_LOG 는 여러 줄짜리 매크로라 중괄호 없이 if 에 두면 뒤의 else 가 짝을 잃는다(C2181).
			if (Cache.IndexById.Contains(Id))
			{
				UE_LOG(LogPGObjects, Warning, TEXT("PGItemValue: duplicate ItemId %s - the first row wins"), *Id.ToString());
			}
			else
			{
				Cache.IndexById.Add(Id, Index);
			}
		}
		Cache.bLoaded = true;
		UE_LOG(LogPGObjects, Display, TEXT("PGItemValue: %d rows from %s"), Cache.Rows.Num(), *Cache.Source);
	}

	// 시드 + ItemId 로 고정된 배율을 만든다. 루팅의 PickColorVariant 와 같은 섞기 방식을 일부러 그대로 쓴다
	// — 결정적 시드 규칙이 프로젝트 안에서 하나여야 서버가 무엇을 다시 계산해야 하는지 헷갈리지 않는다.
	// Salt 로 구매가와 판매가를 갈라 둘이 같이 오르내리지 않게 한다.
	float SeededMultiplier(FName ItemId, int64 ShopSeed, int64 Salt, float Min, float Max)
	{
		const int64 Mixed = (ShopSeed + Salt) ^ (static_cast<int64>(GetTypeHash(ItemId)) * 2654435761LL);
		FRandomStream Stream(static_cast<int32>(Mixed ^ (Mixed >> 32)));
		return Stream.FRandRange(Min, Max);
	}

	// 값이 있는 아이템이 반올림으로 0원이 되지 않게 막는다(공짜로 팔리는 구멍).
	int32 ApplyMultiplier(int32 BaseValue, float Multiplier)
	{
		if (BaseValue <= 0)
			return 0;
		return FMath::Max(1, FMath::RoundToInt(BaseValue * Multiplier));
	}

	static FAutoConsoleCommand ReloadItemTableCommand(
		TEXT("PG.ReloadItemTable"),
		TEXT("Reads Docs/DT_PGItemValue.csv (or the DataTable in Project Settings > ProjectPG Item Grades) again."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			UPGItemValueLibrary::ReloadTable();
		}));
}

TArray<FPGItemValueRow> UPGItemValueLibrary::BuildFallbackRows()
{
	// CSV 를 못 읽었을 때만 쓰는 예비 표. Docs/DT_PGItemValue.csv 와 한 줄씩 같아야 한다(Tools/check_item_tables.py 가 검사).
	// 값을 매긴 축 세 가지 — 표의 모든 Reason 이 이 셋 중 하나로 설명된다:
	//  1) 전투 효율 = 초당 피해(한 발 피해 / 공격 간격) x 사거리. 숫자는 PGWeaponComponent.cpp 의 무기 등록 줄에서 가져왔다.
	//  2) 생존·운반 = 죽지 않게 하거나 들고 나갈 수 있는 양을 늘리는 것(방탄조끼·가방·치료제).
	//  3) 희소성 = 어디서 얼마나 나오나. 1)이 비슷한 것들의 순서를 가르는 데만 쓴다.
	// 무기 레어·에픽은 노말 값의 x1.6 / x2.5 (100 단위 반올림). 성능은 같고 이름·색·값만 다르다.
	TArray<FPGItemValueRow> Out;
	// ---- Money
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Money"), TEXT("Money"), TEXT("재화"), EPGItemCategory::Money, EPGItemGrade::Normal, 1, true, 2.0f, 30, 120, -1, nullptr, nullptr, nullptr,
		TEXT("기준점. 재화 한 개 = 1. 다른 모든 값이 이것의 배수다")));
	// ---- Ammo
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Ammo_Pistol"), TEXT("Ammo_Pistol"), TEXT("권총탄"), EPGItemCategory::Ammo, EPGItemGrade::Normal, 4, true, 3.0f, 10, 20, -1, TEXT("OBJ-033"), nullptr, nullptr,
		TEXT("권총탄 한 발 피해 20. 제일 흔하고 제일 약하다. 제 메시가 없어 소총탄 더미(OBJ-033)를 빌린다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Ammo_Rifle"), TEXT("Ammo_Rifle"), TEXT("소총탄"), EPGItemCategory::Ammo, EPGItemGrade::Normal, 6, true, 3.0f, 10, 30, -1, nullptr, nullptr, nullptr,
		TEXT("소총탄 한 발 피해 32~35. 권총탄의 1.6배 피해라 1.5배 값")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Ammo_Shotgun"), TEXT("Ammo_Shotgun"), TEXT("샷건탄"), EPGItemCategory::Ammo, EPGItemGrade::Normal, 10, true, 2.0f, 4, 10, -1, TEXT("OBJ-033"), nullptr, nullptr,
		TEXT("샷건탄 한 발 8펠릿x14=112. 25m 안에서만 나오는 피해라 소총탄의 1.7배로 깎음")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Magazine_Rifle"), TEXT("Magazine_Rifle"), TEXT("소총 탄창"), EPGItemCategory::Ammo, EPGItemGrade::Normal, 120, true, 2.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("소총탄 20발치(6x20). 재장전 한 번을 통째로 사는 셈")));
	// ---- Material
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Scrap"), TEXT("Scrap"), TEXT("고철"), EPGItemCategory::Material, EPGItemGrade::Normal, 45, true, 3.0f, 1, 2, -1, nullptr, nullptr, nullptr,
		TEXT("어디서나 나오는 재료. 한 묶음(1~4개) 주워도 탄약 한 줌 수준")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("RareMaterial"), TEXT("RareMaterial"), TEXT("희귀 재료"), EPGItemCategory::Material, EPGItemGrade::Epic, 3200, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("세력 C(보스)와 금고에서만 나온다. 한 판에 한두 번뿐인 보상이라 에픽. 바닥에는 안 떨어진다")));
	// ---- Food
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Water"), TEXT("Water"), TEXT("물"), EPGItemCategory::Food, EPGItemGrade::Normal, 50, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("갈증 유지용. 식량 상자에서 두 개씩 나와 흔하다. 바닥 메시가 없어 맵 바닥에는 안 떨어진다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Ration"), TEXT("Ration"), TEXT("전투식량"), EPGItemCategory::Food, EPGItemGrade::Normal, 70, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("물보다 회복량이 크다는 전제. 흔한 축. 바닥 메시가 없어 맵 바닥에는 안 떨어진다")));
	// ---- Medical
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Bandage"), TEXT("Bandage"), TEXT("붕대"), EPGItemCategory::Medical, EPGItemGrade::Normal, 90, true, 4.0f, 1, 2, -1, nullptr, nullptr, nullptr,
		TEXT("기본 회복. 모든 상자에서 나오는 가장 흔한 소모품")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Painkiller"), TEXT("Painkiller"), TEXT("진통제"), EPGItemCategory::Medical, EPGItemGrade::Normal, 150, true, 1.0f, 1, 1, -1, TEXT("OBJ-039"), nullptr, nullptr,
		TEXT("잠깐 버티게 해 주는 버프형이라 붕대의 1.7배. 카탈로그 행이 꺼져 있어 그 행의 주사기 메시만 빌린다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Splint"), TEXT("Splint"), TEXT("부목"), EPGItemCategory::Medical, EPGItemGrade::Normal, 260, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("골절 치료. 특정 상태이상 전용이라 붕대의 3배")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Tourniquet"), TEXT("Tourniquet"), TEXT("지혈대"), EPGItemCategory::Medical, EPGItemGrade::Normal, 320, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("과다 출혈 치료. 안 쓰면 죽는 상태를 푸는 것이라 부목보다 위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Medkit"), TEXT("Medkit"), TEXT("구급상자"), EPGItemCategory::Medical, EPGItemGrade::Normal, 900, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("한 번에 완전 회복. 붕대 10개치. 바닥 메시가 없어 의료 상자·세력 B 시체에서만 나온다")));
	// ---- Throwable
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Grenade"), TEXT("Grenade"), TEXT("수류탄"), EPGItemCategory::Throwable, EPGItemGrade::Normal, 700, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("한 번 쓰면 사라지지만 교전 하나를 끝낼 수 있다")));
	// ---- Wear
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Shirt"), TEXT("Shirt"), TEXT("셔츠"), EPGItemCategory::Wear, EPGItemGrade::Normal, 60, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("항상 입는 기본 옷. 능력치가 없어 사실상 외형값")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Pants"), TEXT("Pants"), TEXT("청바지"), EPGItemCategory::Wear, EPGItemGrade::Normal, 80, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("셔츠와 같은 기본 착장. 면적이 넓어 셔츠보다 조금 위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Shoes"), TEXT("Shoes"), TEXT("운동화"), EPGItemCategory::Wear, EPGItemGrade::Normal, 110, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("기본 착장 중 제일 위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("ChestPouch"), TEXT("ChestPouch"), TEXT("가슴 파우치"), EPGItemCategory::Wear, EPGItemGrade::Normal, 200, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("소형 수납. 가방보다 훨씬 적게 늘려 주는 보조 칸")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Holster"), TEXT("Holster"), TEXT("권총집"), EPGItemCategory::Wear, EPGItemGrade::Normal, 220, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("권총 한 자루를 따로 들 수 있게 해 준다. 파우치와 같은 급")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Backpack"), TEXT("Backpack"), TEXT("배낭"), EPGItemCategory::Wear, EPGItemGrade::Normal, 900, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("들고 나갈 수 있는 양 자체를 늘린다. 노말 중 제일 비싸다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Helmet"), TEXT("Helmet"), TEXT("모자"), EPGItemCategory::Wear, EPGItemGrade::Rare, 1200, true, 2.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("머리 피해 감소. 한 판을 살아서 끝내는 데 직접 기여해 레어")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Armor_Vest"), TEXT("Armor_Vest"), TEXT("방탄조끼"), EPGItemCategory::Wear, EPGItemGrade::Rare, 2200, true, 1.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("몸통 방어. 죽음을 가장 많이 막아 주는 장비라 착장 최상위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Wig_Pink"), TEXT("Wig_Pink"), TEXT("분홍 단발 가발"), EPGItemCategory::Wear, EPGItemGrade::Epic, 0, false, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("교환소에서 드래곤 전리품과 바꾸는 특수 장비. 상점 거래 대상이 아니라 값 0")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Dragon_Scale"), TEXT("Dragon_Scale"), TEXT("드래곤 비늘"), EPGItemCategory::Key, EPGItemGrade::Epic, 0, false, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("드래곤에게 가장 큰 피해를 준 사람이 자동으로 받는 전리품. 교환소에서 분홍 단발 가발로 바꾼다. 루팅으로는 안 나온다(가중치 0)")));
	// ---- Tool
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Tool_Cutter"), TEXT("Tool_Cutter"), TEXT("절단기"), EPGItemCategory::Tool, EPGItemGrade::Normal, 500, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("장애물 제거용. 바닥 메시가 없어 공구 상자에서만 나온다")));
	// ---- Key
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Key_Common"), TEXT("Key_Common"), TEXT("일반 열쇠"), EPGItemCategory::Key, EPGItemGrade::Normal, 600, true, 1.0f, 1, 1, -1, nullptr, TEXT("/Game/Fab/Old_Rusty_Key/old_rusty_key/StaticMeshes/old_rusty_key.old_rusty_key"), nullptr,
		TEXT("잠긴 상자·문. 여는 곳이 흔한 축")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Fuel"), TEXT("Fuel"), TEXT("연료통"), EPGItemCategory::Key, EPGItemGrade::Rare, 1300, true, 1.0f, 1, 1, -2, nullptr, TEXT("/Game/PG/Props/FuelCan/SM_PGFuelCan_Red.SM_PGFuelCan_Red"), nullptr,
		TEXT("헬기·선박 탈출구가 요구한다(기획 3.3.7). 이게 없으면 그 탈출구가 잠긴다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Key_Special"), TEXT("Key_Special"), TEXT("특수 열쇠"), EPGItemCategory::Key, EPGItemGrade::Epic, 4500, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("금고와 특수 탈출구를 연다. 금고에서만 나온다")));
	// ---- Weapon
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Pistol"), TEXT("Pistol"), TEXT("권총 (노말)"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 2600, true, 0.6f, 1, 1, -1, nullptr, TEXT("/Game/PG/Characters/Quantum/FloorMeshes/SM_PGQFloor_Pistol.SM_PGQFloor_Pistol"), TEXT("Ammo_Pistol"),
		TEXT("20딜/0.3초 = 67dps, 60m. 한 발 20이라 교전을 못 끝낸다. 제일 흔해 최하위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Pistol_Rare"), TEXT("Pistol"), TEXT("권총 (레어)"), EPGItemCategory::Weapon, EPGItemGrade::Rare, 4200, true, 1.2f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Pistol"),
		TEXT("노말과 성능이 같다. 이름·파란 테두리·값(x1.6)만 다르다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Pistol_Epic"), TEXT("Pistol"), TEXT("권총 (에픽)"), EPGItemCategory::Weapon, EPGItemGrade::Epic, 6500, true, 3.0f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Pistol"),
		TEXT("노말과 성능이 같다. 이름·보라 테두리·값(x2.5)만 다르다. 워존·보스 쪽에서만 나온다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Revolver"), TEXT("Revolver"), TEXT("리볼버 (노말)"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 3400, true, 0.3f, 1, 1, -1, nullptr, TEXT("/Game/PG/Weapons/SM_PGWFloor_Pistol.SM_PGWFloor_Pistol"), TEXT("Ammo_Pistol"),
		TEXT("34딜/0.55초 = 62dps, 60m. 한 발 34로 한 발 교환에서 이겨 권총 위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Revolver_Rare"), TEXT("Revolver"), TEXT("리볼버 (레어)"), EPGItemCategory::Weapon, EPGItemGrade::Rare, 5400, true, 0.6f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Pistol"),
		TEXT("노말과 성능이 같다. 이름·파란 테두리·값(x1.6)만 다르다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Revolver_Epic"), TEXT("Revolver"), TEXT("리볼버 (에픽)"), EPGItemCategory::Weapon, EPGItemGrade::Epic, 8500, true, 1.5f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Pistol"),
		TEXT("노말과 성능이 같다. 이름·보라 테두리·값(x2.5)만 다르다. 워존·보스 쪽에서만 나온다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Shotgun"), TEXT("Shotgun"), TEXT("샷건 (노말)"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 4200, true, 0.3f, 1, 1, -1, nullptr, TEXT("/Game/PG/Weapons/SM_PGWFloor_Shotgun.SM_PGWFloor_Shotgun"), TEXT("Ammo_Shotgun"),
		TEXT("112딜/0.9초 = 124dps. 화력은 리볼버의 두 배지만 25m 밖에서는 퍼져서 값이 사거리에 깎였다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Shotgun_Rare"), TEXT("Shotgun"), TEXT("샷건 (레어)"), EPGItemCategory::Weapon, EPGItemGrade::Rare, 6700, true, 0.6f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Shotgun"),
		TEXT("노말과 성능이 같다. 이름·파란 테두리·값(x1.6)만 다르다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Shotgun_Epic"), TEXT("Shotgun"), TEXT("샷건 (에픽)"), EPGItemCategory::Weapon, EPGItemGrade::Epic, 10500, true, 1.5f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Shotgun"),
		TEXT("노말과 성능이 같다. 이름·보라 테두리·값(x2.5)만 다르다. 워존·보스 쪽에서만 나온다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Rifle_AK"), TEXT("Rifle_AK"), TEXT("소총 AK (노말)"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 7800, true, 0.2f, 1, 1, -1, nullptr, TEXT("/Game/PG/Weapons/SM_PGWFloor_AK47.SM_PGWFloor_AK47"), TEXT("Ammo_Rifle"),
		TEXT("32딜/0.12초 = 267dps 로 화력 1위. 사거리 90m 이고 AR70 보다 두 배 흔해 2위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Rifle_AK_Rare"), TEXT("Rifle_AK"), TEXT("소총 AK (레어)"), EPGItemCategory::Weapon, EPGItemGrade::Rare, 12500, true, 0.4f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Rifle"),
		TEXT("노말과 성능이 같다. 이름·파란 테두리·값(x1.6)만 다르다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Rifle_AK_Epic"), TEXT("Rifle_AK"), TEXT("소총 AK (에픽)"), EPGItemCategory::Weapon, EPGItemGrade::Epic, 19500, true, 1.0f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Rifle"),
		TEXT("노말과 성능이 같다. 이름·보라 테두리·값(x2.5)만 다르다. 워존·보스 쪽에서만 나온다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Rifle_AR70"), TEXT("Rifle_AR70"), TEXT("소총 AR70 (노말)"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 9500, true, 0.1f, 1, 1, -1, nullptr, TEXT("/Game/PG/Weapons/SM_PGWFloor_AR70.SM_PGWFloor_AR70"), TEXT("Ammo_Rifle"),
		TEXT("35딜/0.15초 = 233dps, 사거리 100m 로 최장. 가장 드물어 1위")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Rifle_AR70_Rare"), TEXT("Rifle_AR70"), TEXT("소총 AR70 (레어)"), EPGItemCategory::Weapon, EPGItemGrade::Rare, 15200, true, 0.2f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Rifle"),
		TEXT("노말과 성능이 같다. 이름·파란 테두리·값(x1.6)만 다르다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Rifle_AR70_Epic"), TEXT("Rifle_AR70"), TEXT("소총 AR70 (에픽)"), EPGItemCategory::Weapon, EPGItemGrade::Epic, 23800, true, 0.5f, 1, 1, -1, nullptr, nullptr, TEXT("Ammo_Rifle"),
		TEXT("노말과 성능이 같다. 이름·보라 테두리·값(x2.5)만 다르다. 워존·보스 쪽에서만 나온다")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Bow"), TEXT("Bow"), TEXT("활"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 900, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("9/17 무기 범위에서 뺌. 되살릴 때 값을 다시 정하지 않도록 행만 남김")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Axe"), TEXT("Axe"), TEXT("도끼"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 400, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("9/17 무기 범위에서 뺌")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Bat"), TEXT("Bat"), TEXT("방망이"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 300, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("9/17 무기 범위에서 뺌")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("ThrowingKnife"), TEXT("ThrowingKnife"), TEXT("투척용 칼"), EPGItemCategory::Weapon, EPGItemGrade::Normal, 180, true, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("9/17 무기 범위에서 뺌. 한 개당 값")));
	// ---- Quest
	Out.Add(PGItemValueLocal::MakeRow(TEXT("QuestItem_Doc"), TEXT("QuestItem_Doc"), TEXT("퀘스트 문서"), EPGItemCategory::Quest, EPGItemGrade::Normal, 0, false, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("퀘스트 아이템. 팔리면 퀘스트가 막히므로 거래 금지")));
	Out.Add(PGItemValueLocal::MakeRow(TEXT("Recipe"), TEXT("Recipe"), TEXT("제조 레시피"), EPGItemCategory::Quest, EPGItemGrade::Normal, 0, false, 0.0f, 1, 1, -1, nullptr, nullptr, nullptr,
		TEXT("제조 NPC 확정 전까지 거래 금지")));
	return Out;
}

const TArray<FPGItemValueRow>& UPGItemValueLibrary::GetAllRows()
{
	PGItemValueLocal::FItemTableCache& Cache = PGItemValueLocal::TableCache();
	if (!Cache.bLoaded)
		PGItemValueLocal::LoadTable(Cache);
	return Cache.Rows;
}

void UPGItemValueLibrary::ReloadTable()
{
	PGItemValueLocal::LoadTable(PGItemValueLocal::TableCache());
}

FString UPGItemValueLibrary::GetTableSource()
{
	GetAllRows();
	return PGItemValueLocal::TableCache().Source;
}

const FPGItemValueRow* UPGItemValueLibrary::FindRow(FName ItemId)
{
	if (ItemId.IsNone())
		return nullptr;

	const TArray<FPGItemValueRow>& Rows = GetAllRows();
	const TMap<FName, int32>& Index = PGItemValueLocal::TableCache().IndexById;
	if (const int32* Found = Index.Find(ItemId))
		return &Rows[*Found];

	// 색 변형(Pants_Black)은 행을 안 만들고 원래색(Pants) 값을 물려받는다.
	// 왜: 색은 같은 물건의 겉모습일 뿐이라 값이 달라질 이유가 없는데, 행을 만들면 20여 줄이 늘고 셋이 어긋날 자리가 생긴다.
	// 무기 등급(Pistol_Rare)은 반대로 자기 행이 있다 — 등급은 값이 달라지기 때문이다.
	FPGWearableColor Color;
	if (UPGWearableColorLibrary::FindWearableColor(ItemId, Color) && !Color.BaseItemId.IsNone() && Color.BaseItemId != ItemId)
	{
		if (const int32* Found = Index.Find(Color.BaseItemId))
			return &Rows[*Found];
	}
	return nullptr;
}

int32 UPGItemValueLibrary::GetItemValue(FName ItemId)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	return Found ? Found->BaseValue : 0;
}

int32 UPGItemValueLibrary::GetStackValue(const FPGItemStack& Stack)
{
	return GetItemValue(Stack.ItemId) * FMath::Max(0, Stack.Count);
}

int32 UPGItemValueLibrary::GetTotalValue(const TArray<FPGItemStack>& Items)
{
	int32 Total = 0;
	for (const FPGItemStack& Stack : Items)
		Total += GetStackValue(Stack);
	return Total;
}

EPGItemGrade UPGItemValueLibrary::GetItemGrade(FName ItemId)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	return Found ? Found->Grade : EPGItemGrade::Normal;
}

FName UPGItemValueLibrary::GetBaseItemId(FName ItemId)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	// 자기 행일 때만 BaseItemId 를 믿는다. 색 변형은 원래색 행을 빌려 온 것이라 그 행의 BaseItemId 는 원래색 자신이다.
	if (Found && Found->ItemId == ItemId && !Found->BaseItemId.IsNone())
		return Found->BaseItemId;
	return ItemId;
}

FText UPGItemValueLibrary::GetDisplayName(FName ItemId)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	// 색 변형(Pants_Black)이 원래색 이름("청바지")으로 보이면 안 되므로 자기 행일 때만.
	if (Found && Found->ItemId == ItemId && !Found->DisplayName.IsEmpty())
		return FText::FromString(Found->DisplayName);
	return FText::GetEmpty();
}

FText UPGItemValueLibrary::GetGradeText(EPGItemGrade Grade)
{
	switch (Grade)
	{
	case EPGItemGrade::Rare: return NSLOCTEXT("ItemGrade", "Rare", "레어");
	case EPGItemGrade::Epic: return NSLOCTEXT("ItemGrade", "Epic", "에픽");
	default:                 return NSLOCTEXT("ItemGrade", "Normal", "노말");
	}
}

FLinearColor UPGItemValueLibrary::GetGradeColor(EPGItemGrade Grade)
{
	const UPGItemGradeSettings* Settings = GetDefault<UPGItemGradeSettings>();
	switch (Grade)
	{
	case EPGItemGrade::Rare: return Settings ? Settings->RareColor : FLinearColor(0.10f, 0.45f, 1.0f, 1.0f);
	case EPGItemGrade::Epic: return Settings ? Settings->EpicColor : FLinearColor(0.65f, 0.15f, 1.0f, 1.0f);
	default:                 return FLinearColor(0.6f, 0.6f, 0.6f, 1.0f); // 노말 = 회색(UI 글자색용. 바닥에는 테두리를 안 그린다)
	}
}

UMaterialInterface* UPGItemValueLibrary::GetGradeOverlayMaterial(EPGItemGrade Grade)
{
	if (Grade == EPGItemGrade::Normal)
		return nullptr;

	// 재질 애셋은 한 번만 찾아 본다. 없을 때 아이템마다 다시 찾으면 바닥 아이템 수천 개가 파일을 수천 번 뒤진다.
	static bool bTriedLoad = false;
	static UMaterialInterface* BaseMaterial = nullptr;
	if (!bTriedLoad)
	{
		bTriedLoad = true;
		const UPGItemGradeSettings* Settings = GetDefault<UPGItemGradeSettings>();
		BaseMaterial = Settings ? Settings->GradeOverlayMaterial.LoadSynchronous() : nullptr;
		if (!BaseMaterial)
			UE_LOG(LogPGObjects, Warning, TEXT("PGItemValue: grade overlay material %s not found - floor items show the grade by name only. Run Tools/make_grade_overlay.py in the editor."),
				Settings ? *Settings->GradeOverlayMaterial.ToString() : TEXT("(no settings)"));
	}
	if (!BaseMaterial)
		return nullptr;

	// 등급마다 색을 넣은 인스턴스 하나. 가비지 수집에서 빼 둔다(모든 바닥 아이템이 게임 내내 같이 쓴다).
	static UMaterialInstanceDynamic* GradeInstances[3] = { nullptr, nullptr, nullptr };
	const int32 Slot = FMath::Clamp(static_cast<int32>(Grade), 0, 2);
	if (!GradeInstances[Slot])
	{
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
		if (!Instance)
			return nullptr;
		Instance->SetVectorParameterValue(TEXT("GradeColor"), GetGradeColor(Grade));
		Instance->AddToRoot();
		GradeInstances[Slot] = Instance;
	}
	return GradeInstances[Slot];
}

bool UPGItemValueLibrary::IsTradable(FName ItemId)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	// 모르는 아이템은 거래 금지. 값을 모르는 것을 거래에 끼우면 차액 계산이 조용히 틀어진다.
	return Found ? Found->bTradable : false;
}

int32 UPGItemValueLibrary::GetBuyPrice(FName ItemId, int64 ShopSeed)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	if (!Found || !Found->bTradable)
		return 0;
	// 기획 14쪽: 구매가 = 기본가치 x1.1~1.5.
	return PGItemValueLocal::ApplyMultiplier(Found->BaseValue, PGItemValueLocal::SeededMultiplier(ItemId, ShopSeed, 0x5117LL, 1.1f, 1.5f));
}

int32 UPGItemValueLibrary::GetSellPrice(FName ItemId, int64 ShopSeed)
{
	const FPGItemValueRow* Found = FindRow(ItemId);
	if (!Found || !Found->bTradable)
		return 0;
	// 기획 14쪽: 판매가 = 기본가치 x0.6~0.8.
	return PGItemValueLocal::ApplyMultiplier(Found->BaseValue, PGItemValueLocal::SeededMultiplier(ItemId, ShopSeed, 0x2E11LL, 0.6f, 0.8f));
}

FPGTradeResult UPGItemValueLibrary::EvaluateTrade(const TArray<FPGItemStack>& Sold, const TArray<FPGItemStack>& Bought, int64 ShopSeed)
{
	FPGTradeResult Result;

	for (const FPGItemStack& Stack : Sold)
	{
		if (!IsTradable(Stack.ItemId))
		{
			Result.RejectedItemId = Stack.ItemId;
			return Result;
		}
		Result.SoldValue += GetSellPrice(Stack.ItemId, ShopSeed) * FMath::Max(0, Stack.Count);
	}
	for (const FPGItemStack& Stack : Bought)
	{
		if (!IsTradable(Stack.ItemId))
		{
			Result.RejectedItemId = Stack.ItemId;
			return Result;
		}
		Result.BoughtValue += GetBuyPrice(Stack.ItemId, ShopSeed) * FMath::Max(0, Stack.Count);
	}

	Result.Difference = Result.SoldValue - Result.BoughtValue;
	// 기획 14쪽: "판 것의 가치 > 산 것의 가치"일 때만 거래 버튼이 활성된다. 같으면 성립하지 않는다.
	// 빈 거래(아무것도 안 팔고 안 사기)도 차액 0이라 여기서 자동으로 막힌다.
	Result.bValid = Result.Difference > 0;
	return Result;
}

void UPGItemValueLibrary::GetGradeRange(EPGItemGrade Grade, int32& OutMin, int32& OutMax)
{
	// 무기가 아닌 아이템만 이 구간을 따른다. 무기는 같은 총끼리 노말 < 레어 < 에픽이면 된다
	// (노말 AR70 이 에픽 권총보다 비쌀 수 있다 — 총의 급과 등급은 다른 축이다).
	switch (Grade)
	{
	case EPGItemGrade::Normal: OutMin = 1;    OutMax = 999;       break;
	case EPGItemGrade::Rare:   OutMin = 1000; OutMax = 2999;      break;
	case EPGItemGrade::Epic:   OutMin = 3000; OutMax = MAX_int32; break;
	default:                   OutMin = 0;    OutMax = MAX_int32; break;
	}
}

bool UPGItemValueLibrary::ExportCsv(const FString& Path)
{
	const TArray<FPGItemValueRow>& Rows = GetAllRows();
	auto Quote = [](const FString& Text)
	{
		FString Escaped = Text;
		Escaped.ReplaceInline(TEXT("\""), TEXT("\"\""));
		return FString::Printf(TEXT("\"%s\""), *Escaped);
	};
	auto NameOrNone = [](FName Name) { return Name.IsNone() ? FString(TEXT("None")) : Name.ToString(); };

	// 열 순서·이름은 FPGItemValueRow 의 속성 이름 그대로다(에디터 DataTable 가져오기가 이름으로 맞춘다).
	FString Csv = TEXT("---,ItemId,BaseItemId,DisplayName,Category,Grade,BaseValue,bTradable,LootWeight,LootMin,LootMax,LootCap,LootMeshObjectId,FloorMesh,AmmoItemId,Reason\n");
	for (const FPGItemValueRow& Row : Rows)
	{
		const FString Mesh = Row.FloorMesh.IsNull() ? FString() : Quote(Row.FloorMesh.ToString());
		Csv += FString::Printf(TEXT("%s,%s,%s,%s,%s,%s,%d,%s,%.1f,%d,%d,%d,%s,%s,%s,%s\n"),
			*Row.ItemId.ToString(), *Row.ItemId.ToString(), *NameOrNone(Row.BaseItemId), *Quote(Row.DisplayName),
			*StaticEnum<EPGItemCategory>()->GetNameStringByValue(static_cast<int64>(Row.Category)),
			*StaticEnum<EPGItemGrade>()->GetNameStringByValue(static_cast<int64>(Row.Grade)),
			Row.BaseValue, Row.bTradable ? TEXT("True") : TEXT("False"), Row.LootWeight, Row.LootMin, Row.LootMax, Row.LootCap,
			*NameOrNone(Row.LootMeshObjectId), *Mesh, *NameOrNone(Row.AmmoItemId), *Quote(Row.Reason));
	}

	const bool bSaved = FFileHelper::SaveStringToFile(Csv, *Path, FFileHelper::EEncodingOptions::ForceUTF8);
	UE_LOG(LogPGObjects, Display, TEXT("Item value CSV export: rows=%d source=%s path=%s saved=%s"),
		Rows.Num(), *GetTableSource(), *Path, bSaved ? TEXT("true") : TEXT("false"));
	return bSaved;
}
