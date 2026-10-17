// 소리 전체 흐름: Source/ProjectPG/Public/Audio/README.md
// 시험용 콘솔 명령. 개발 빌드에만 들어가고 배포(Shipping)엔 빠진다.
// 화면 없이 띄운 클라는 아무도 총을 안 쏴서 총소리 경로(서버 몽타주 → 모든 클라 소리)를 볼 수 없음 → 이걸로 쏘게 한다.
#include "CoreMinimal.h"

#if !UE_BUILD_SHIPPING

#include "Characters/CustomCharacter.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPGAudioTest, Log, All);

namespace PGAudioTest
{
	ACustomCharacter* FindMyCharacter()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld()) continue;
			if (APlayerController* PC = World->GetFirstPlayerController())
				if (ACustomCharacter* Me = Cast<ACustomCharacter>(PC->GetPawn())) return Me;
		}
		return nullptr;
	}

	// 접속·맵 이동 중에도 살아 있어야 해서 맵 타이머가 아닌 엔진 타이머로 0.5초마다 봄. 60초 안에 못 끝내면 Error 로그.
	// 순서: 내 캐릭터가 생김 → 주무기 꺼냄 → 2초 뒤 쏘기 시작 → 1초 뒤 멈춤.
	void Start(int32 Shots)
	{
		const double StartTime = FPlatformTime::Seconds();
		double SelectedAt = -1.0, FiringAt = -1.0;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([=](float) mutable
		{
			const double Now = FPlatformTime::Seconds();
			ACustomCharacter* Me = FindMyCharacter();
			if (!Me)
			{
				if (Now - StartTime > 60.0) { UE_LOG(LogPGAudioTest, Error, TEXT("시험 발사: 60초 안에 내 캐릭터가 안 생김 → 못 쏨")); return false; }
				return true;
			}
			if (SelectedAt < 0.0) { Me->SelectWeapon(EEquipSlot::MainWeapon); SelectedAt = Now; UE_LOG(LogPGAudioTest, Display, TEXT("시험 발사: %s 주무기 꺼냄"), *Me->GetName()); return true; }
			if (FiringAt < 0.0 && Now - SelectedAt > 2.0) { Me->SetFireInput(true); FiringAt = Now; UE_LOG(LogPGAudioTest, Display, TEXT("시험 발사: 쏘기 시작")); return true; }
			if (FiringAt > 0.0 && Now - FiringAt > 1.0)
			{
				Me->SetFireInput(false);
				UE_LOG(LogPGAudioTest, Display, TEXT("시험 발사: 멈춤"));
				if (--Shots > 0) { SelectedAt = Now - 2.0 + 3.0; FiringAt = -1.0; return true; } // 3초 쉬고 다시
				return false;
			}
			return true;
		}), 0.5f);
	}

	static FAutoConsoleCommand TestFireCommand(
		TEXT("PG.Audio.TestFire"),
		TEXT("시험: 내 캐릭터가 생기면 주무기를 들고 1초씩 쏜다(총소리·탄착음 확인용). 인자 = 몇 번(기본 1)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			Start(Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 1);
		}));
}

#endif
