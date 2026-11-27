#pragma once

#include "CoreMinimal.h"

#include "ToolsetRegistry/ToolCallAsyncResult.h"
#include "ToolsetRegistry/ToolsetDefinition.h"

#include "PerfToolset.generated.h"

/** Frame timing statistics over a sampling window. All times in milliseconds. */
USTRUCT(BlueprintType)
struct FPerfFrameStats
{
	GENERATED_BODY()

	/** Number of frames sampled. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	int32 Frames = 0;

	/** Average frame time (1000 / average FPS). */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float AvgFrameMs = 0.f;

	/** 95th percentile frame time: what the slowest 5% of frames exceed. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float P95FrameMs = 0.f;

	/** Slowest frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float MaxFrameMs = 0.f;

	/** Average game thread time (gameplay, ticking, editor UI). */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float AvgGameThreadMs = 0.f;

	/** Average render thread time (draw submission, culling). */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float AvgRenderThreadMs = 0.f;

	/** Average RHI thread time. 0 when the RHI thread is not used. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float AvgRHIThreadMs = 0.f;

	/** Average GPU time per frame. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float AvgGPUMs = 0.f;

	/** 95th percentile GPU time. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	float P95GPUMs = 0.f;

	/** Which of game, render, GPU had the highest average: "Game", "Render" or "GPU". */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	FString Bottleneck;
};

/** Async tool call result that completes with frame statistics. */
UCLASS(BlueprintType)
class UPerfStatsAsyncResult : public UToolCallAsyncResult
{
	GENERATED_BODY()

public:
	bool SetValue(const FPerfFrameStats& InValue)
	{
		return MaybeBroadcastSuccessfulCompletion(FPerfFrameStats(InValue), Value);
	}

	/** Value of the result. */
	UPROPERTY(BlueprintReadOnly, Category = "Perf")
	FPerfFrameStats Value;
};

/**
 * Measures rendering performance in the running editor or Play-In-Editor session and runs console
 * commands (stat toggles, scalability, cvars) to compare settings. Frame times include editor
 * overhead and are throttled when the editor window is in the background.
 */
UCLASS(BlueprintType, Hidden)
class UPerfToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Samples frame timings over a time window.
	 * @param DurationSeconds Sampling window length, in seconds.
	 * @param WarmupSeconds Time to let the frame settle before sampling, in seconds.
	 * @return Frame statistics for the window.
	 */
	UFUNCTION(meta = (AICallable), Category = "Perf")
	static UPerfStatsAsyncResult* MeasureFrames(float DurationSeconds, float WarmupSeconds);

	/**
	 * Runs a console command in the play world if Play-In-Editor is running, otherwise in the editor world.
	 * @param Command Console command, e.g. "r.VolumetricFog 0" or "grass.Enable 0".
	 * @return Text the command printed, if any.
	 */
	UFUNCTION(meta = (AICallable), Category = "Perf")
	static FString RunConsoleCommand(const FString& Command);
};
