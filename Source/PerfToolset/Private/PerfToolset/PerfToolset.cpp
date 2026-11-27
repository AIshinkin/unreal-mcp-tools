#include "PerfToolset.h"

#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/StringOutputDevice.h"
#include "RenderTimer.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(PerfToolset)

namespace PerfToolset::Private
{
	struct FSample
	{
		float FrameMs;
		float GameMs;
		float RenderMs;
		float RHIMs;
		float GPUMs;
	};

	float Percentile(TArray<float> Values, float Fraction)
	{
		if (Values.IsEmpty())
		{
			return 0.f;
		}
		Values.Sort();
		const int32 Index = FMath::Clamp(FMath::CeilToInt(Fraction * Values.Num()) - 1, 0, Values.Num() - 1);
		return Values[Index];
	}

	FPerfFrameStats Summarize(const TArray<FSample>& Samples)
	{
		FPerfFrameStats Stats;
		Stats.Frames = Samples.Num();
		if (Samples.IsEmpty())
		{
			return Stats;
		}

		TArray<float> FrameTimes;
		TArray<float> GPUTimes;
		double Frame = 0, Game = 0, Render = 0, RHI = 0, GPU = 0;
		for (const FSample& S : Samples)
		{
			Frame += S.FrameMs;
			Game += S.GameMs;
			Render += S.RenderMs;
			RHI += S.RHIMs;
			GPU += S.GPUMs;
			FrameTimes.Add(S.FrameMs);
			GPUTimes.Add(S.GPUMs);
			Stats.MaxFrameMs = FMath::Max(Stats.MaxFrameMs, S.FrameMs);
		}
		const double N = Samples.Num();
		Stats.AvgFrameMs = static_cast<float>(Frame / N);
		Stats.AvgGameThreadMs = static_cast<float>(Game / N);
		Stats.AvgRenderThreadMs = static_cast<float>(Render / N);
		Stats.AvgRHIThreadMs = static_cast<float>(RHI / N);
		Stats.AvgGPUMs = static_cast<float>(GPU / N);
		Stats.P95FrameMs = Percentile(FrameTimes, 0.95f);
		Stats.P95GPUMs = Percentile(GPUTimes, 0.95f);

		Stats.Bottleneck = TEXT("Game");
		float Worst = Stats.AvgGameThreadMs;
		if (Stats.AvgRenderThreadMs > Worst) { Worst = Stats.AvgRenderThreadMs; Stats.Bottleneck = TEXT("Render"); }
		if (Stats.AvgGPUMs > Worst) { Stats.Bottleneck = TEXT("GPU"); }
		return Stats;
	}

	UWorld* GetTargetWorld()
	{
		if (!GEditor)
		{
			return nullptr;
		}
		if (GEditor->PlayWorld)
		{
			return GEditor->PlayWorld;
		}
		return GEditor->GetEditorWorldContext().World();
	}
}

UPerfStatsAsyncResult* UPerfToolset::MeasureFrames(float DurationSeconds, float WarmupSeconds)
{
	using namespace PerfToolset::Private;

	UPerfStatsAsyncResult* Result = NewObject<UPerfStatsAsyncResult>();
	if (DurationSeconds <= 0.f || DurationSeconds > 120.f)
	{
		Result->SetError(TEXT("DurationSeconds must be in (0, 120]."));
		return Result;
	}
	if (WarmupSeconds < 0.f || WarmupSeconds > 60.f)
	{
		Result->SetError(TEXT("WarmupSeconds must be in [0, 60]."));
		return Result;
	}

	struct FState
	{
		TStrongObjectPtr<UPerfStatsAsyncResult> Result;
		TArray<FSample> Samples;
		double StartTime = 0.0;
		double Warmup = 0.0;
		double Duration = 0.0;
	};
	TSharedRef<FState> State = MakeShared<FState>();
	State->Result.Reset(Result);
	State->StartTime = FPlatformTime::Seconds();
	State->Warmup = WarmupSeconds;
	State->Duration = DurationSeconds;

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State](float DeltaTime)
	{
		const double Elapsed = FPlatformTime::Seconds() - State->StartTime;
		if (Elapsed < State->Warmup)
		{
			return true;
		}
		if (Elapsed < State->Warmup + State->Duration)
		{
			FSample Sample;
			Sample.FrameMs = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
			Sample.GameMs = static_cast<float>(FPlatformTime::ToMilliseconds(GGameThreadTime));
			Sample.RenderMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRenderThreadTime));
			Sample.RHIMs = static_cast<float>(FPlatformTime::ToMilliseconds(GRHIThreadTime));
			Sample.GPUMs = static_cast<float>(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles(0)));
			State->Samples.Add(Sample);
			return true;
		}

		if (State->Samples.IsEmpty())
		{
			State->Result->SetError(TEXT("No frames were rendered during the sampling window."));
		}
		else
		{
			State->Result->SetValue(Summarize(State->Samples));
		}
		State->Result.Reset();
		return false;
	}));

	return Result;
}

FString UPerfToolset::RunConsoleCommand(const FString& Command)
{
	using namespace PerfToolset::Private;

	if (Command.TrimStartAndEnd().IsEmpty())
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Command must not be empty."));
		return FString();
	}
	UWorld* World = GetTargetWorld();
	if (!World || !GEngine)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("No world is available to run the command in."));
		return FString();
	}

	FStringOutputDevice Output;
	Output.SetAutoEmitLineTerminator(true);
	if (!GEngine->Exec(World, *Command, Output))
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("Unknown console command: '%s'."), *Command));
		return FString();
	}
	return Output;
}
