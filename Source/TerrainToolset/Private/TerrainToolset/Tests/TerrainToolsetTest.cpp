#include "TerrainToolset/TerrainToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ToolsetRegistry/ToolCallExceptionHandler.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	const auto TestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter;

	// 1 component of 1 section with 7 quads: an 8x8 vertex landscape.
	FTerrainCreateSettings MakeSmallSettings()
	{
		FTerrainCreateSettings Settings;
		Settings.Label = TEXT("TerrainToolsetTest");
		Settings.Center = FVector(0.0, 0.0, 1000.0);
		Settings.ComponentCount = FIntPoint(1, 1);
		Settings.QuadsPerSection = 7;
		Settings.SectionsPerComponent = 1;
		Settings.MetersPerVertex = 1.0f;
		Settings.HeightRangeMeters = 512.0f;
		return Settings;
	}

	// Writes an 8x8 .r16 heightmap with a horizontal gradient.
	TArray<uint16> WriteGradientR16(const FString& Path)
	{
		TArray<uint16> Heights;
		for (int32 Y = 0; Y < 8; ++Y)
		{
			for (int32 X = 0; X < 8; ++X)
			{
				Heights.Add(static_cast<uint16>(32768 + X * 256));
			}
		}
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Heights.GetData()), Heights.Num() * sizeof(uint16));
		FFileHelper::SaveArrayToFile(Bytes, *Path);
		return Heights;
	}

	TArray<uint16> ReadR16(const FString& Path)
	{
		TArray<uint8> Bytes;
		FFileHelper::LoadFileToArray(Bytes, *Path);
		TArray<uint16> Heights;
		Heights.SetNumUninitialized(Bytes.Num() / sizeof(uint16));
		FMemory::Memcpy(Heights.GetData(), Bytes.GetData(), Heights.Num() * sizeof(uint16));
		return Heights;
	}
}

BEGIN_DEFINE_SPEC(FTerrainToolsetSpec, "AI.Toolsets.UnrealMcpTools.TerrainToolset", TestFlags)

	TUniquePtr<UE::ToolsetRegistry::FToolCallExceptionHandler> ExceptionHandler;
	TArray<TWeakObjectPtr<ALandscape>> CreatedLandscapes;
	FString TempDir;

	void ExpectNoException()
	{
		TestEqual(TEXT("Error"), ExceptionHandler->GetException(), FString());
		ExceptionHandler = MakeUnique<UE::ToolsetRegistry::FToolCallExceptionHandler>();
	}

	void ExpectExceptionContains(const FString& Message)
	{
		TestTrue(*FString::Printf(TEXT("Error contains '%s' (got '%s')"), *Message, *ExceptionHandler->GetException()),
			ExceptionHandler->GetException().Contains(Message));
		ExceptionHandler = MakeUnique<UE::ToolsetRegistry::FToolCallExceptionHandler>();
	}

	ALandscape* Create(const FTerrainCreateSettings& Settings)
	{
		ALandscape* Landscape = nullptr;
		ExceptionHandler->CaptureErrorsIn([&]() { Landscape = UTerrainToolset::CreateLandscape(Settings); });
		if (Landscape)
		{
			CreatedLandscapes.Add(Landscape);
		}
		return Landscape;
	}

END_DEFINE_SPEC(FTerrainToolsetSpec)

void FTerrainToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		ExceptionHandler = MakeUnique<UE::ToolsetRegistry::FToolCallExceptionHandler>();
		TempDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("AutomationTests") / TEXT("TerrainToolset"));
		IFileManager::Get().MakeDirectory(*TempDir, /*Tree=*/true);
	});

	AfterEach([this]()
	{
		UWorld* World = GEditor->GetEditorWorldContext().World();
		for (const TWeakObjectPtr<ALandscape>& Landscape : CreatedLandscapes)
		{
			if (!Landscape.IsValid())
			{
				continue;
			}
			TArray<ALandscapeProxy*> Proxies;
			if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
			{
				Info->ForEachLandscapeProxy([&Proxies](ALandscapeProxy* Proxy) { Proxies.Add(Proxy); return true; });
			}
			for (ALandscapeProxy* Proxy : Proxies)
			{
				if (Proxy != Landscape.Get())
				{
					World->EditorDestroyActor(Proxy, /*bShouldModifyLevel=*/true);
				}
			}
			World->EditorDestroyActor(Landscape.Get(), /*bShouldModifyLevel=*/true);
		}
		CreatedLandscapes.Reset();
		IFileManager::Get().DeleteDirectory(*TempDir, /*RequireExists=*/false, /*Tree=*/true);
		ExceptionHandler.Reset();
	});

	Describe("CreateLandscape", [this]()
	{
		It("creates a landscape with the requested layout and layers", [this]()
		{
			FTerrainCreateSettings Settings = MakeSmallSettings();
			Settings.ExtraEditLayers = { TEXT("Gameplay") };
			ALandscape* Landscape = Create(Settings);
			ExpectNoException();
			if (!TestNotNull(TEXT("Landscape"), Landscape))
			{
				return;
			}

			FTerrainInfo Info;
			ExceptionHandler->CaptureErrorsIn([&]() { Info = UTerrainToolset::GetLandscapeInfo(Landscape); });
			ExpectNoException();
			TestTrue(TEXT("Resolution"), Info.Resolution == FIntPoint(8, 8));
			TestTrue(TEXT("ComponentCount"), Info.ComponentCount == FIntPoint(1, 1));
			TestEqual(TEXT("QuadsPerSection"), Info.QuadsPerSection, 7);
			TestEqual(TEXT("MetersPerVertex"), Info.MetersPerVertex, 1.0f, 0.001f);
			TestEqual(TEXT("HeightRangeMeters"), Info.HeightRangeMeters, 512.0f, 0.01f);
			TestEqual(TEXT("ZeroLevelZ"), Info.ZeroLevelZ, 1000.0, 0.01);
			TestTrue(TEXT("WorldMin"), Info.WorldMin.Equals(FVector2D(-350.0, -350.0), 0.01));
			TestTrue(TEXT("WorldMax"), Info.WorldMax.Equals(FVector2D(350.0, 350.0), 0.01));
			TestTrue(TEXT("EditLayers"), Info.EditLayers == TArray<FString>{ TEXT("Base"), TEXT("Gameplay") });
		});

		It("imports the heightmap file at creation", [this]()
		{
			const FString Path = TempDir / TEXT("gradient.r16");
			const TArray<uint16> Expected = WriteGradientR16(Path);
			FTerrainCreateSettings Settings = MakeSmallSettings();
			Settings.HeightmapFile = Path;
			ALandscape* Landscape = Create(Settings);
			ExpectNoException();
			if (!TestNotNull(TEXT("Landscape"), Landscape))
			{
				return;
			}

			const FString ExportPath = TempDir / TEXT("export.r16");
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ExportHeightmap(Landscape, ExportPath, TEXT("Base")); });
			ExpectNoException();
			TestTrue(TEXT("Heights"), ReadR16(ExportPath) == Expected);
		});

		It("raises on invalid QuadsPerSection", [this]()
		{
			FTerrainCreateSettings Settings = MakeSmallSettings();
			Settings.QuadsPerSection = 10;
			TestNull(TEXT("Landscape"), Create(Settings));
			ExpectExceptionContains(TEXT("QuadsPerSection"));
		});

		It("raises on invalid SectionsPerComponent", [this]()
		{
			FTerrainCreateSettings Settings = MakeSmallSettings();
			Settings.SectionsPerComponent = 3;
			TestNull(TEXT("Landscape"), Create(Settings));
			ExpectExceptionContains(TEXT("SectionsPerComponent"));
		});

		It("raises on non-positive scale", [this]()
		{
			FTerrainCreateSettings Settings = MakeSmallSettings();
			Settings.MetersPerVertex = 0.0f;
			TestNull(TEXT("Landscape"), Create(Settings));
			ExpectExceptionContains(TEXT("must be positive"));
		});

		It("raises when the heightmap resolution does not match", [this]()
		{
			const FString Path = TempDir / TEXT("gradient.r16");
			WriteGradientR16(Path);
			FTerrainCreateSettings Settings = MakeSmallSettings();
			Settings.QuadsPerSection = 15;
			Settings.HeightmapFile = Path;
			TestNull(TEXT("Landscape"), Create(Settings));
			ExpectExceptionContains(TEXT("expected 16x16"));
		});
	});

	Describe("ImportHeightmap / ExportHeightmap", [this]()
	{
		It("round-trips an edit layer through a file", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			const FString Path = TempDir / TEXT("gradient.r16");
			const TArray<uint16> Expected = WriteGradientR16(Path);
			const FString ExportPath = TempDir / TEXT("export.r16");

			ExceptionHandler->CaptureErrorsIn([&]()
			{
				UTerrainToolset::ImportHeightmap(Landscape, Path, TEXT("Base"));
				UTerrainToolset::ExportHeightmap(Landscape, ExportPath, TEXT("Base"));
			});
			ExpectNoException();
			TestTrue(TEXT("Heights"), ReadR16(ExportPath) == Expected);
		});

		It("raises on an unknown edit layer", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			const FString Path = TempDir / TEXT("gradient.r16");
			WriteGradientR16(Path);
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ImportHeightmap(Landscape, Path, TEXT("Missing")); });
			ExpectExceptionContains(TEXT("Edit layer 'Missing' does not exist"));
		});

		It("raises on a missing file", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ImportHeightmap(Landscape, TempDir / TEXT("none.r16"), TEXT("Base")); });
			ExpectExceptionContains(TEXT("File not found"));
		});

		It("raises on an unsupported export format", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ExportHeightmap(Landscape, TempDir / TEXT("out.txt"), FString()); });
			ExpectExceptionContains(TEXT("Cannot export heightmaps"));
		});

		It("raises on a null landscape", [this]()
		{
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ExportHeightmap(nullptr, TempDir / TEXT("out.r16"), FString()); });
			ExpectExceptionContains(TEXT("must not be null"));
		});
	});

	Describe("AddEditLayer / RemoveEditLayer", [this]()
	{
		It("adds and removes a layer", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			FTerrainInfo Info;
			ExceptionHandler->CaptureErrorsIn([&]()
			{
				UTerrainToolset::AddEditLayer(Landscape, TEXT("Roads"));
				Info = UTerrainToolset::GetLandscapeInfo(Landscape);
			});
			ExpectNoException();
			TestTrue(TEXT("Roads added"), Info.EditLayers.Contains(TEXT("Roads")));

			ExceptionHandler->CaptureErrorsIn([&]()
			{
				UTerrainToolset::RemoveEditLayer(Landscape, TEXT("Roads"));
				Info = UTerrainToolset::GetLandscapeInfo(Landscape);
			});
			ExpectNoException();
			TestFalse(TEXT("Roads removed"), Info.EditLayers.Contains(TEXT("Roads")));
		});

		It("raises on a duplicate name", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::AddEditLayer(Landscape, TEXT("Base")); });
			ExpectExceptionContains(TEXT("already exists"));
		});

		It("raises on an empty name", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::AddEditLayer(Landscape, FString()); });
			ExpectExceptionContains(TEXT("must not be empty"));
		});

		It("raises when removing an unknown layer", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::RemoveEditLayer(Landscape, TEXT("Missing")); });
			ExpectExceptionContains(TEXT("does not exist"));
		});

		It("raises when removing the last layer", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::RemoveEditLayer(Landscape, TEXT("Base")); });
			ExpectExceptionContains(TEXT("last edit layer"));
		});
	});

	Describe("AddPaintLayer / ImportWeightmap", [this]()
	{
		It("adds a paint layer and imports weights into it", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			const FString Folder = TEXT("/Game/Developers/TerrainToolsetTests");
			const FString LayerName = FString::Printf(TEXT("Test%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
			ULandscapeLayerInfoObject* LayerInfo = nullptr;
			FTerrainInfo Info;
			ExceptionHandler->CaptureErrorsIn([&]()
			{
				LayerInfo = UTerrainToolset::AddPaintLayer(Landscape, LayerName, Folder);
				Info = UTerrainToolset::GetLandscapeInfo(Landscape);
			});
			ExpectNoException();
			TestNotNull(TEXT("LayerInfo"), LayerInfo);
			TestTrue(TEXT("Paint layer listed"), Info.PaintLayers.Contains(LayerName));

			const FString WeightPath = TempDir / TEXT("weights.r8");
			TArray<uint8> Weights;
			Weights.Init(255, 64);
			FFileHelper::SaveArrayToFile(Weights, *WeightPath);
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ImportWeightmap(Landscape, LayerName, WeightPath, TEXT("Base")); });
			ExpectNoException();
		});

		It("raises on a duplicate paint layer", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			const FString Folder = TEXT("/Game/Developers/TerrainToolsetTests");
			const FString LayerName = FString::Printf(TEXT("Dup%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::AddPaintLayer(Landscape, LayerName, Folder); });
			ExpectNoException();
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::AddPaintLayer(Landscape, LayerName, Folder); });
			ExpectExceptionContains(TEXT("already exists"));
		});

		It("raises on a non-content asset folder", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::AddPaintLayer(Landscape, TEXT("Grass"), TEXT("C:/Temp")); });
			ExpectExceptionContains(TEXT("content path"));
		});

		It("raises when importing into an unknown paint layer", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::ImportWeightmap(Landscape, TEXT("Missing"), TempDir / TEXT("w.r8"), TEXT("Base")); });
			ExpectExceptionContains(TEXT("Paint layer 'Missing'"));
		});
	});

	Describe("SampleHeights", [this]()
	{
		It("returns the zero level on a flat landscape", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			TArray<double> Heights;
			ExceptionHandler->CaptureErrorsIn([&]() { Heights = UTerrainToolset::SampleHeights(Landscape, { FVector2D(0.0, 0.0), FVector2D(100.0, -200.0) }); });
			ExpectNoException();
			if (TestEqual(TEXT("Count"), Heights.Num(), 2))
			{
				TestEqual(TEXT("Height 0"), Heights[0], 1000.0, 1.0);
				TestEqual(TEXT("Height 1"), Heights[1], 1000.0, 1.0);
			}
		});

		It("raises for a point outside the landscape", [this]()
		{
			ALandscape* Landscape = Create(MakeSmallSettings());
			ExceptionHandler->CaptureErrorsIn([&]() { UTerrainToolset::SampleHeights(Landscape, { FVector2D(100000.0, 0.0) }); });
			ExpectExceptionContains(TEXT("outside the loaded landscape"));
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
