// PG.PerfSweep — 그래픽 설정을 하나씩 바꿔 가며 GPU·게임 스레드 시간을 자동으로 재서 표로 남긴다.
//
// 왜: 4060 8GB 기준으로 무엇을 낮출지 정하려면 "설정 하나 바꾸고 → stat gpu 스크린샷" 을 열 번 넘게 반복해야 했다.
//     같은 자리·같은 시야에서 한 번에 재야 비교가 공정하다. PIE 에서 가만히 서서 이 명령 한 줄만 치면 된다.
// 쓰는 법: 대표 장면(공장·시가지가 보이는 곳)을 바라보고 가만히 선다 → 콘솔 PG.PerfSweep → 약 80초 기다림(마우스·키 안 건드리기).
//          결과는 출력 로그(LogTemp "PerfSweep")와 화면에 나온다. 끝나면 모든 설정을 원래대로 돌려놓는다.
// 한계: 에디터 창 크기에 따라 절대값은 달라진다. 이 표는 "같은 조건에서 무엇이 얼마나 줄었나" 비교용이다.
// 자동 측정: PG.PerfSweep <시작 지연 초>. 실행 인자에 -PGPerfSweepQuit 이 있으면 끝나고 게임을 닫는다.
//   예) UnrealEditor.exe <uproject> -game -windowed -ResX=1920 -ResY=1080 -PGMapSeed=12345 -ExecCmds="PG.PerfSweep 45" -PGPerfSweepQuit
//   PIE 창은 작아서(1633×606) 실제 1080p 전체 화면의 절반 픽셀만 그린다. 4060 판단은 이 1080p 측정으로 한다.
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "RenderTimer.h"

namespace
{
	struct FPerfVariant
	{
		FString Name;
		TArray<TPair<FString, FString>> Settings; // 콘솔 변수 이름, 값
	};

	struct FPerfSweepState
	{
		TArray<FPerfVariant> Variants;
		TMap<FString, FString> Original; // 바꾼 변수의 원래 값(끝나거나 다음 단계로 갈 때 되돌림)
		int32 Index = -1;
		bool bMeasuring = false;
		double PhaseStart = 0.0;
		double GpuSum = 0.0, GameSum = 0.0, RenderSum = 0.0, GpuMax = 0.0;
		int32 Frames = 0;
		double BaselineGpu = 0.0;
		TArray<FString> Lines;
		FTSTicker::FDelegateHandle Handle;
	};

	TUniquePtr<FPerfSweepState> GSweep;
	constexpr double WarmupSeconds = 2.5; // 설정을 바꾼 직후는 셰이더·캐시가 다시 만들어져 튄다. 이 시간은 버린다.
	constexpr double MeasureSeconds = 4.0;

	void SetCVar(const FString& Name, const FString& Value)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Name))
			Var->Set(*Value, ECVF_SetByConsole);
	}

	void RestoreAll(FPerfSweepState& S)
	{
		for (const TPair<FString, FString>& Pair : S.Original)
			SetCVar(Pair.Key, Pair.Value);
		S.Original.Reset();
	}

	void ApplyVariant(FPerfSweepState& S, const FPerfVariant& Variant)
	{
		RestoreAll(S);
		for (const TPair<FString, FString>& Setting : Variant.Settings)
		{
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*Setting.Key);
			if (!Var)
			{
				UE_LOG(LogTemp, Warning, TEXT("PerfSweep: no cvar %s (variant %s)"), *Setting.Key, *Variant.Name);
				continue;
			}
			S.Original.Add(Setting.Key, Var->GetString());
			Var->Set(*Setting.Value, ECVF_SetByConsole);
		}
	}

	void BuildVariants(FPerfSweepState& S)
	{
		auto Add = [&S](const TCHAR* Name, std::initializer_list<TPair<FString, FString>> Settings)
		{
			FPerfVariant V;
			V.Name = Name;
			V.Settings = Settings;
			S.Variants.Add(MoveTemp(V));
		};
		using P = TPair<FString, FString>;
		Add(TEXT("baseline (지금 설정)"), {});
		Add(TEXT("그림자 품질 High (sg.ShadowQuality 2)"), { P(TEXT("sg.ShadowQuality"), TEXT("2")) });
		Add(TEXT("방향광 VSM 해상도 한 단계 낮춤"), { P(TEXT("r.Shadow.Virtual.ResolutionLodBiasDirectional"), TEXT("1")) });
		Add(TEXT("GI 품질 High (Lumen)"), { P(TEXT("sg.GlobalIlluminationQuality"), TEXT("2")) });
		Add(TEXT("반사 품질 High"), { P(TEXT("sg.ReflectionQuality"), TEXT("2")) });
		Add(TEXT("후처리 품질 High"), { P(TEXT("sg.PostProcessQuality"), TEXT("2")) });
		Add(TEXT("이펙트 품질 High"), { P(TEXT("sg.EffectsQuality"), TEXT("2")) });
		Add(TEXT("풀·폴리지 품질 High"), { P(TEXT("sg.FoliageQuality"), TEXT("2")) });
		Add(TEXT("시야 거리 품질 High"), { P(TEXT("sg.ViewDistanceQuality"), TEXT("2")) });
		Add(TEXT("볼류메트릭 구름 끔"), { P(TEXT("r.VolumetricCloud"), TEXT("0")) });
		Add(TEXT("대비 적응 셰이딩(VRS) 끔"), { P(TEXT("r.VRS.ContrastAdaptiveShading"), TEXT("0")) });
		// 렌더 해상도를 낮추고 TSR 로 1080p 에 맞춰 올린다. 4060 급에서 보통 가장 큰 절약. 지금 기본은 약 86%.
		Add(TEXT("렌더 해상도 75% (TSR 업스케일)"), { P(TEXT("r.ScreenPercentage"), TEXT("75")) });
		Add(TEXT("렌더 해상도 67% (TSR 업스케일)"), { P(TEXT("r.ScreenPercentage"), TEXT("67")) });
		Add(TEXT("전부 High (sg.* = 2)"), {
			P(TEXT("sg.ShadowQuality"), TEXT("2")), P(TEXT("sg.GlobalIlluminationQuality"), TEXT("2")),
			P(TEXT("sg.ReflectionQuality"), TEXT("2")), P(TEXT("sg.PostProcessQuality"), TEXT("2")),
			P(TEXT("sg.EffectsQuality"), TEXT("2")), P(TEXT("sg.FoliageQuality"), TEXT("2")),
			P(TEXT("sg.ViewDistanceQuality"), TEXT("2")), P(TEXT("sg.TextureQuality"), TEXT("2")) });
	}

	void Finish(FPerfSweepState& S)
	{
		RestoreAll(S);
		UE_LOG(LogTemp, Display, TEXT("PerfSweep ===== 결과 (GPU 평균 / 최대, 게임 스레드, 렌더 스레드, baseline 대비) ====="));
		for (const FString& Line : S.Lines)
		{
			UE_LOG(LogTemp, Display, TEXT("PerfSweep %s"), *Line);
			if (GEngine)
				GEngine->AddOnScreenDebugMessage(-1, 60.0f, FColor::Cyan, Line);
		}
		UE_LOG(LogTemp, Display, TEXT("PerfSweep ===== 끝. 모든 설정을 원래대로 되돌렸다 ====="));
		if (FParse::Param(FCommandLine::Get(), TEXT("PGPerfSweepQuit")))
			FPlatformMisc::RequestExit(false, TEXT("PG.PerfSweep finished"));
	}

	bool TickSweep(float)
	{
		if (!GSweep.IsValid())
			return false;
		FPerfSweepState& S = *GSweep;
		const double Now = FPlatformTime::Seconds();

		if (!S.bMeasuring)
		{
			if (Now - S.PhaseStart < WarmupSeconds)
				return true;
			S.bMeasuring = true;
			S.PhaseStart = Now;
			S.GpuSum = S.GameSum = S.RenderSum = S.GpuMax = 0.0;
			S.Frames = 0;
			return true;
		}

		const double Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0));
		S.GpuSum += Gpu;
		S.GpuMax = FMath::Max(S.GpuMax, Gpu);
		S.GameSum += FPlatformTime::ToMilliseconds(GGameThreadTime);
		S.RenderSum += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		++S.Frames;
		if (Now - S.PhaseStart < MeasureSeconds)
			return true;

		const double N = FMath::Max(1, S.Frames);
		const double AvgGpu = S.GpuSum / N;
		if (S.Index == 0)
			S.BaselineGpu = AvgGpu;
		const double Delta = S.Index == 0 ? 0.0 : AvgGpu - S.BaselineGpu;
		const FString Line = FString::Printf(TEXT("%-34s GPU %5.2f / %5.2f ms  Game %5.2f  Render %5.2f  (%+.2f ms)"),
			*S.Variants[S.Index].Name, AvgGpu, S.GpuMax, S.GameSum / N, S.RenderSum / N, Delta);
		S.Lines.Add(Line);
		UE_LOG(LogTemp, Display, TEXT("PerfSweep [%d/%d] %s"), S.Index + 1, S.Variants.Num(), *Line);

		++S.Index;
		if (!S.Variants.IsValidIndex(S.Index))
		{
			Finish(S);
			GSweep.Reset();
			return false;
		}
		ApplyVariant(S, S.Variants[S.Index]);
		S.bMeasuring = false;
		S.PhaseStart = Now;
		return true;
	}

	static FAutoConsoleCommand PerfSweepCommand(
		TEXT("PG.PerfSweep"),
		TEXT("PG.PerfSweep [delaySeconds] - measure GPU/game/render thread time while toggling graphics settings one by one (about 80 s; stand still). Results in the output log (PerfSweep)."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
		{
			// 실행 인자(-ExecCmds)로 바로 부르면 맵 생성·시설 로딩 중에 재게 된다. 그 시간을 기다렸다가 시작.
			const double Delay = Args.Num() > 0 ? FMath::Max(0.0, FCString::Atod(*Args[0])) : 0.0;
			if (GSweep.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("PerfSweep: already running"));
				return;
			}
			GSweep = MakeUnique<FPerfSweepState>();
			BuildVariants(*GSweep);
			GSweep->Index = 0;
			// 첫 단계(baseline) 워밍업을 지연만큼 늘린다.
			GSweep->PhaseStart = FPlatformTime::Seconds() + Delay;
			const double Total = GSweep->Variants.Num() * (WarmupSeconds + MeasureSeconds);
			UE_LOG(LogTemp, Display, TEXT("PerfSweep: %d variants, about %.0f s. Stand still."), GSweep->Variants.Num(), Total);
			if (GEngine)
				GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow, FString::Printf(TEXT("PerfSweep: %.0f초 동안 가만히 있어 주세요"), Total));
			GSweep->Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&TickSweep));
		}));
}
