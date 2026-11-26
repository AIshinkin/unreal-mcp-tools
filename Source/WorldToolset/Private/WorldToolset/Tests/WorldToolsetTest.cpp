#include "WorldToolset/WorldToolset.h"

#include "Editor.h"
#include "EditorAssetLibrary.h"
#include "Engine/World.h"
#include "FileHelpers.h"
#include "Misc/AutomationTest.h"
#include "ToolsetRegistry/ToolCallExceptionHandler.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FWorldToolsetSpec, "AI.Toolsets.UnrealMcpTools.WorldToolset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	TUniquePtr<UE::ToolsetRegistry::FToolCallExceptionHandler> ExceptionHandler;

	void ExpectError(const FString& Message)
	{
		AddExpectedMessage(Message, ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
	}

	void ExpectExceptionContains(const FString& Message)
	{
		TestTrue(*FString::Printf(TEXT("Error contains '%s' (got '%s')"), *Message, *ExceptionHandler->GetException()),
			ExceptionHandler->GetException().Contains(Message));
	}

	FString CurrentLevelPath() const
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		return World ? World->GetPackage()->GetName() : FString();
	}

END_DEFINE_SPEC(FWorldToolsetSpec)

void FWorldToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		ExceptionHandler = MakeUnique<UE::ToolsetRegistry::FToolCallExceptionHandler>();
	});

	AfterEach([this]()
	{
		ExceptionHandler.Reset();
	});

	Describe("CreateLevel", [this]()
	{
		It("creates and opens a World Partition level", [this]()
		{
			const FString PreviousLevel = CurrentLevelPath();
			UWorld* PreviousWorld = GEditor->GetEditorWorldContext().World();
			if (PreviousWorld && PreviousWorld->GetPackage()->IsDirty())
			{
				AddInfo(TEXT("Skipped: the open level has unsaved changes."));
				return;
			}

			const FString LevelPath = FString::Printf(TEXT("/Game/Developers/WorldToolsetTests/L_Test_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
			bool bPartitioned = false;
			ExceptionHandler->CaptureErrorsIn([&]()
			{
				UWorldToolset::CreateLevel(LevelPath, /*bWorldPartition=*/true);
				bPartitioned = UWorldToolset::IsWorldPartitioned();
			});
			TestEqual(TEXT("Error"), ExceptionHandler->GetException(), FString());
			TestEqual(TEXT("Open level"), CurrentLevelPath(), LevelPath);
			TestTrue(TEXT("World Partition"), bPartitioned);

			if (!PreviousLevel.IsEmpty() && !FPackageName::IsTempPackage(PreviousLevel))
			{
				FEditorFileUtils::LoadMap(PreviousLevel, /*LoadAsTemplate=*/false, /*bShowProgress=*/false);
			}
			UEditorAssetLibrary::DeleteDirectory(TEXT("/Game/Developers/WorldToolsetTests"));
		});

		It("raises on an invalid path", [this]()
		{
			ExpectError(TEXT("is not a valid content path"));
			ExceptionHandler->CaptureErrorsIn([&]() { UWorldToolset::CreateLevel(TEXT("Levels/NoRoot"), true); });
			ExpectExceptionContains(TEXT("is not a valid content path"));
		});

		It("raises when the asset already exists", [this]()
		{
			const FString Existing = CurrentLevelPath();
			if (Existing.IsEmpty() || FPackageName::IsTempPackage(Existing))
			{
				AddInfo(TEXT("Skipped: no saved level is open."));
				return;
			}
			ExpectError(TEXT("already exists"));
			ExceptionHandler->CaptureErrorsIn([&]() { UWorldToolset::CreateLevel(Existing, true); });
			ExpectExceptionContains(TEXT("already exists"));
		});
	});

	Describe("IsWorldPartitioned", [this]()
	{
		It("matches the open world", [this]()
		{
			UWorld* World = GEditor->GetEditorWorldContext().World();
			bool bResult = false;
			ExceptionHandler->CaptureErrorsIn([&]() { bResult = UWorldToolset::IsWorldPartitioned(); });
			TestEqual(TEXT("Error"), ExceptionHandler->GetException(), FString());
			TestEqual(TEXT("Partitioned"), bResult, World->IsPartitionedWorld());
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
