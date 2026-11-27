#include "PerfToolset/PerfToolset.h"

#include "Containers/Ticker.h"
#include "Misc/AutomationTest.h"
#include "ToolsetRegistry/ToolCallExceptionHandler.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FPerfToolsetSpec, "AI.Toolsets.UnrealMcpTools.PerfToolset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	TUniquePtr<UE::ToolsetRegistry::FToolCallExceptionHandler> ExceptionHandler;

	void ExpectError(const FString& Message)
	{
		AddExpectedMessage(Message, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	}

END_DEFINE_SPEC(FPerfToolsetSpec)

void FPerfToolsetSpec::Define()
{
	BeforeEach([this]() { ExceptionHandler = MakeUnique<UE::ToolsetRegistry::FToolCallExceptionHandler>(); });
	AfterEach([this]() { ExceptionHandler.Reset(); });

	Describe("MeasureFrames", [this]()
	{
		LatentIt("returns frame statistics after the window", [this](const FDoneDelegate& Done)
		{
			TStrongObjectPtr<UPerfStatsAsyncResult> Result(UPerfToolset::MeasureFrames(0.3f, 0.f));
			FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([this, Result, Done](float)
			{
				if (!Result->bIsComplete)
				{
					return true;
				}
				TestEqual(TEXT("Error"), Result->Error, FString());
				TestTrue(TEXT("Frames sampled"), Result->Value.Frames > 0);
				TestTrue(TEXT("Frame time positive"), Result->Value.AvgFrameMs > 0.f);
				TestTrue(TEXT("P95 >= average is plausible"), Result->Value.P95FrameMs >= 0.f);
				TestFalse(TEXT("Bottleneck set"), Result->Value.Bottleneck.IsEmpty());
				Done.Execute();
				return false;
			}));
		});

		It("fails on a non-positive duration", [this]()
		{
			UPerfStatsAsyncResult* Result = UPerfToolset::MeasureFrames(0.f, 0.f);
			TestTrue(TEXT("Complete"), Result->bIsComplete);
			TestTrue(TEXT("Error"), Result->Error.Contains(TEXT("DurationSeconds")));
		});

		It("fails on a negative warmup", [this]()
		{
			UPerfStatsAsyncResult* Result = UPerfToolset::MeasureFrames(1.f, -1.f);
			TestTrue(TEXT("Complete"), Result->bIsComplete);
			TestTrue(TEXT("Error"), Result->Error.Contains(TEXT("WarmupSeconds")));
		});
	});

	Describe("RunConsoleCommand", [this]()
	{
		It("returns the command output", [this]()
		{
			FString Output;
			ExceptionHandler->CaptureErrorsIn([&]() { Output = UPerfToolset::RunConsoleCommand(TEXT("r.ScreenPercentage")); });
			TestEqual(TEXT("Error"), ExceptionHandler->GetException(), FString());
			TestTrue(*FString::Printf(TEXT("Output mentions the cvar (got '%s')"), *Output), Output.Contains(TEXT("r.ScreenPercentage")));
		});

		It("raises on an empty command", [this]()
		{
			ExpectError(TEXT("must not be empty"));
			ExceptionHandler->CaptureErrorsIn([&]() { UPerfToolset::RunConsoleCommand(TEXT("  ")); });
			TestTrue(TEXT("Error"), ExceptionHandler->GetException().Contains(TEXT("must not be empty")));
		});

		It("raises on an unknown command", [this]()
		{
			ExpectError(TEXT("Unknown console command"));
			ExceptionHandler->CaptureErrorsIn([&]() { UPerfToolset::RunConsoleCommand(TEXT("definitely_not_a_command_42")); });
			TestTrue(TEXT("Error"), ExceptionHandler->GetException().Contains(TEXT("Unknown console command")));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
