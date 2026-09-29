#include "GameModes/PGObjectTestGameMode.h"

#include "Engine/World.h"
#include "GameFramework/SpectatorPawn.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Objects/PGObjectSmokeTest.h"
#include "Objects/PGObjectSpawnerSubsystem.h"
#include "Objects/PGObjectTypes.h"
#include "TimerManager.h"

APGObjectTestGameMode::APGObjectTestGameMode()
{
	DefaultPawnClass = ASpectatorPawn::StaticClass();
}

void APGObjectTestGameMode::StartPlay()
{
	Super::StartPlay();

	// -PGExportObjectCatalog=<폴더>: 기본 카탈로그를 CSV 두 개로 내보낸다 (DataTable 가져오기용).
	FString ExportDir;
	if (FParse::Value(FCommandLine::Get(), TEXT("PGExportObjectCatalog="), ExportDir) && !ExportDir.IsEmpty())
	{
		if (UPGObjectSpawnerSubsystem* Spawner = UPGObjectSpawnerSubsystem::Get(this))
		{
			PGObjectSmokeTest::RegisterDefaultCatalog(*Spawner);
			PGObjectSmokeTest::ExportCatalogCsv(*Spawner,
				FPaths::Combine(ExportDir, TEXT("DT_PGObjectCatalog.csv")),
				FPaths::Combine(ExportDir, TEXT("DT_PGLootTables.csv")));
		}
	}

	if (!FParse::Param(FCommandLine::Get(), TEXT("PGObjectSmokeTest")))
	{
		if (!ExportDir.IsEmpty())
		{
			FTimerHandle ExitHandle;
			GetWorldTimerManager().SetTimer(ExitHandle, []() { FPlatformMisc::RequestExitWithStatus(false, 0); }, 1.0f, false);
		}
		return;
	}

	const int32 Failed = PGObjectSmokeTest::RunAndCount(GetWorld());
	UE_LOG(LogPGObjects, Display, TEXT("PGObjectSmokeTest headless run finished: failed=%d"), Failed);

	// 로그가 flush 될 시간을 준 뒤 종료한다. 종료 코드는 실패 수.
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, [Failed]()
	{
		FPlatformMisc::RequestExitWithStatus(false, Failed == 0 ? 0 : 1);
	}, 1.0f, false);
}
