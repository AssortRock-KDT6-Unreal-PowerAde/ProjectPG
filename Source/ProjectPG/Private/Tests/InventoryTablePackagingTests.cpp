#include "Common/TableData.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Parse.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FInventoryTablePackagingTest, "ProjectPG.Inventory.RuntimeTablePackaging", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FInventoryTablePackagingTest::RunTest(const FString& Parameters)
{
	TArray<FString> CookEntries;
	GConfig->GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("DirectoriesToAlwaysCook"), CookEntries, GGameIni);
	auto IsAlwaysCooked = [&CookEntries](const UObject* Asset)
	{
		if (!Asset) return false;
		const FString PackageName = Asset->GetOutermost()->GetName();
		for (const FString& Entry : CookEntries)
		{
			FString Path;
			if (FParse::Value(*Entry, TEXT("Path="), Path) && !Path.IsEmpty())
			{
				if (PackageName == Path || PackageName.StartsWith(Path + TEXT("/"))) return true;
			}
		}
		return false;
	};

	UDataTable* Loader = LoadObject<UDataTable>(nullptr, TEXT("/Game/PG/Table/TableLoader.TableLoader"));
	if (!TestNotNull(TEXT("Runtime TableLoader exists"), Loader)) return false;
	TestTrue(TEXT("TableLoader is explicitly cooked"), IsAlwaysCooked(Loader));
	if (!TestTrue(TEXT("TableLoader row type"), Loader->GetRowStruct() == FTablePathRow::StaticStruct())) return false;

	TSet<FName> LoadedTables;
	Loader->ForeachRow<FTablePathRow>(TEXT("Runtime table packaging test"), [this, &IsAlwaysCooked, &LoadedTables](const FName& Name, const FTablePathRow& Row)
	{
		if (!Row.UseThis) return;
		UDataTable* Table = LoadObject<UDataTable>(nullptr, *Row.Path);
		if (!TestNotNull(FString::Printf(TEXT("Enabled table loads: %s"), *Name.ToString()), Table)) return;
		LoadedTables.Add(Name);
		TestTrue(FString::Printf(TEXT("Dynamic table is explicitly cooked: %s"), *Name.ToString()), IsAlwaysCooked(Table));
		TestTrue(FString::Printf(TEXT("Table contains rows: %s"), *Name.ToString()), Table->GetRowMap().Num() > 0);
		if (Name == TEXT("ItemTable"))
		{
			if (TestTrue(TEXT("Item table row type"), Table->GetRowStruct() == FItemTableRow::StaticStruct()))
			{
				TestNotNull(TEXT("Travel regression item 1003 exists"), Table->FindRow<FItemTableRow>(TEXT("1003"), TEXT("Inventory travel regression"), false));
			}
		}
	});
	TestTrue(TEXT("ItemTable is enabled"), LoadedTables.Contains(TEXT("ItemTable")));
	TestTrue(TEXT("EquipTable is enabled"), LoadedTables.Contains(TEXT("EquipTable")));
	TestTrue(TEXT("BackpackTable is enabled"), LoadedTables.Contains(TEXT("BackpackTable")));
	return true;
}

#endif
