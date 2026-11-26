#include "TerrainToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Landscape.h"
#include "LandscapeComponent.h"
#include "LandscapeDataAccess.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "LandscapeEditorModule.h"
#include "LandscapeFileFormatInterface.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "LandscapeStreamingProxy.h"
#include "LandscapeSubsystem.h"
#include "LandscapeUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(TerrainToolset)

#define LOCTEXT_NAMESPACE "TerrainToolset"

namespace TerrainToolset::Private
{
	// Full height range of a landscape with Z scale 1, in cm: 65536 values * LANDSCAPE_ZSCALE.
	constexpr double HeightRangeAtUnitScaleCm = 65536.0 * LANDSCAPE_ZSCALE;

	void RaiseError(const FString& Message)
	{
		UKismetSystemLibrary::RaiseScriptError(Message);
	}

	UWorld* GetEditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	ILandscapeEditorModule& GetLandscapeEditorModule()
	{
		return FModuleManager::LoadModuleChecked<ILandscapeEditorModule>(TEXT("LandscapeEditor"));
	}

	/** Returns landscape info and full vertex extent, or raises and returns null. */
	ULandscapeInfo* GetInfoAndExtent(ALandscape* Landscape, FIntRect& OutExtent)
	{
		if (!Landscape)
		{
			RaiseError(TEXT("Landscape must not be null."));
			return nullptr;
		}

		ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
		if (!Info || !Info->GetLandscapeExtent(OutExtent))
		{
			RaiseError(FString::Printf(TEXT("Landscape '%s' has no loaded components."), *Landscape->GetActorLabel()));
			return nullptr;
		}
		return Info;
	}

	FLandscapeFileResolution ResolutionOf(const FIntRect& Extent)
	{
		return FLandscapeFileResolution(Extent.Width() + 1, Extent.Height() + 1);
	}

	/** Finds an edit layer GUID by name, or raises and returns an invalid GUID. */
	FGuid FindEditLayerGuid(ALandscape* Landscape, const FString& EditLayer)
	{
		const ULandscapeEditLayerBase* Layer = Landscape->GetEditLayerConst(FName(*EditLayer));
		if (!Layer)
		{
			RaiseError(FString::Printf(TEXT("Edit layer '%s' does not exist on '%s'."), *EditLayer, *Landscape->GetActorLabel()));
			return FGuid();
		}
		return Layer->GetGuid();
	}

	template <typename T>
	bool ReadImageFile(const FString& FilePath, const FLandscapeFileResolution& Resolution, TArray<T>& OutData)
	{
		const FString Extension = FPaths::GetExtension(FilePath, /*bIncludeDot=*/true);
		const ILandscapeFileFormat<T>* Format = GetLandscapeEditorModule().template GetFormatByExtension<T>(*Extension);
		if (!Format)
		{
			RaiseError(FString::Printf(TEXT("Unsupported file type '%s' for '%s'."), *Extension, *FilePath));
			return false;
		}
		if (!FPaths::FileExists(FilePath))
		{
			RaiseError(FString::Printf(TEXT("File not found: '%s'."), *FilePath));
			return false;
		}

		const FLandscapeFileInfo FileInfo = Format->Validate(*FilePath);
		if (FileInfo.ResultCode == ELandscapeImportResult::Error)
		{
			RaiseError(FString::Printf(TEXT("Cannot read '%s': %s"), *FilePath, *FileInfo.ErrorMessage.ToString()));
			return false;
		}
		if (!FileInfo.PossibleResolutions.Contains(Resolution))
		{
			const FLandscapeFileResolution Actual = FileInfo.PossibleResolutions.IsEmpty() ? FLandscapeFileResolution() : FileInfo.PossibleResolutions[0];
			RaiseError(FString::Printf(TEXT("'%s' is %ux%u, expected %ux%u."), *FilePath, Actual.Width, Actual.Height, Resolution.Width, Resolution.Height));
			return false;
		}

		FLandscapeImportData<T> Imported = Format->Import(*FilePath, Resolution);
		if (Imported.ResultCode == ELandscapeImportResult::Error)
		{
			RaiseError(FString::Printf(TEXT("Failed to import '%s': %s"), *FilePath, *Imported.ErrorMessage.ToString()));
			return false;
		}
		OutData = MoveTemp(Imported.Data);
		return true;
	}
}

using namespace TerrainToolset::Private;

ALandscape* UTerrainToolset::CreateLandscape(const FTerrainCreateSettings& Settings)
{
	UWorld* World = GetEditorWorld();
	if (!World)
	{
		RaiseError(TEXT("No editor world is loaded."));
		return nullptr;
	}

	static const TArray<int32> ValidQuadsPerSection = { 7, 15, 31, 63, 127, 255 };
	if (!ValidQuadsPerSection.Contains(Settings.QuadsPerSection))
	{
		RaiseError(TEXT("QuadsPerSection must be one of 7, 15, 31, 63, 127, 255."));
		return nullptr;
	}
	if (Settings.SectionsPerComponent != 1 && Settings.SectionsPerComponent != 2)
	{
		RaiseError(TEXT("SectionsPerComponent must be 1 or 2."));
		return nullptr;
	}
	if (Settings.ComponentCount.X < 1 || Settings.ComponentCount.Y < 1)
	{
		RaiseError(TEXT("ComponentCount must be at least 1x1."));
		return nullptr;
	}
	if (Settings.MetersPerVertex <= 0.0f || Settings.HeightRangeMeters <= 0.0f)
	{
		RaiseError(TEXT("MetersPerVertex and HeightRangeMeters must be positive."));
		return nullptr;
	}
	if (Settings.BaseEditLayer.IsEmpty())
	{
		RaiseError(TEXT("BaseEditLayer must not be empty."));
		return nullptr;
	}

	const int32 QuadsPerComponent = Settings.SectionsPerComponent * Settings.QuadsPerSection;
	const int32 SizeX = Settings.ComponentCount.X * QuadsPerComponent + 1;
	const int32 SizeY = Settings.ComponentCount.Y * QuadsPerComponent + 1;

	TArray<uint16> Heights;
	if (Settings.HeightmapFile.IsEmpty())
	{
		Heights.Init(static_cast<uint16>(LandscapeDataAccess::MidValue), SizeX * SizeY);
	}
	else if (!ReadImageFile<uint16>(Settings.HeightmapFile, FLandscapeFileResolution(SizeX, SizeY), Heights))
	{
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("CreateLandscape", "Create Landscape (Terrain Toolset)"));

	const double ScaleXY = Settings.MetersPerVertex * 100.0;
	const double ScaleZ = Settings.HeightRangeMeters * 100.0 / HeightRangeAtUnitScaleCm;
	const FVector Scale(ScaleXY, ScaleXY, ScaleZ);
	const FVector Offset(-Settings.ComponentCount.X * QuadsPerComponent * 0.5 * ScaleXY, -Settings.ComponentCount.Y * QuadsPerComponent * 0.5 * ScaleXY, 0.0);

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Settings.Center + Offset, FRotator::ZeroRotator);
	if (!Landscape)
	{
		RaiseError(TEXT("Failed to spawn the landscape actor."));
		return nullptr;
	}
	if (Settings.Material)
	{
		Landscape->LandscapeMaterial = Settings.Material;
	}
	Landscape->SetActorRelativeScale3D(Scale);
	// Same lighting LOD heuristic as the editor's New Landscape tool.
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(Heights));
	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	Landscape->Import(FGuid::NewGuid(), 0, 0, SizeX - 1, SizeY - 1, Settings.SectionsPerComponent, Settings.QuadsPerSection,
		HeightDataPerLayers, *Settings.HeightmapFile, MaterialLayerDataPerLayers, ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
	check(Info);

	FActorLabelUtilities::SetActorLabelUnique(Landscape, Settings.Label.IsEmpty() ? ALandscape::StaticClass()->GetName() : Settings.Label);
	Info->UpdateLayerInfoMap(Landscape);

	if (ULandscapeEditLayerBase* BaseLayer = Landscape->GetEditLayer(0))
	{
		BaseLayer->SetName(FName(*Settings.BaseEditLayer), /*bInModify=*/true);
	}
	for (const FString& LayerName : Settings.ExtraEditLayers)
	{
		AddEditLayer(Landscape, LayerName);
	}

	if (ULandscapeSubsystem* LandscapeSubsystem = World->GetSubsystem<ULandscapeSubsystem>(); LandscapeSubsystem && LandscapeSubsystem->IsGridBased())
	{
		LandscapeSubsystem->ChangeGridSize(Info, Settings.WorldPartitionGridSize);
	}

	return Landscape;
}

FTerrainInfo UTerrainToolset::GetLandscapeInfo(ALandscape* Landscape)
{
	FIntRect Extent;
	ULandscapeInfo* Info = GetInfoAndExtent(Landscape, Extent);
	if (!Info)
	{
		return FTerrainInfo();
	}

	const FTransform Transform = Landscape->GetActorTransform();
	const FVector Scale = Landscape->GetActorScale3D();

	FTerrainInfo Result;
	Result.Resolution = FIntPoint(Extent.Width() + 1, Extent.Height() + 1);
	Result.ComponentCount = FIntPoint(Extent.Width() / Info->ComponentSizeQuads, Extent.Height() / Info->ComponentSizeQuads);
	Result.QuadsPerSection = Info->SubsectionSizeQuads;
	Result.SectionsPerComponent = Info->ComponentNumSubsections;
	Result.MetersPerVertex = static_cast<float>(Scale.X / 100.0);
	Result.HeightRangeMeters = static_cast<float>(Scale.Z * HeightRangeAtUnitScaleCm / 100.0);

	const FVector Min = Transform.TransformPosition(FVector(Extent.Min.X, Extent.Min.Y, 0.0));
	const FVector Max = Transform.TransformPosition(FVector(Extent.Max.X, Extent.Max.Y, 0.0));
	Result.WorldMin = FVector2D(FMath::Min(Min.X, Max.X), FMath::Min(Min.Y, Max.Y));
	Result.WorldMax = FVector2D(FMath::Max(Min.X, Max.X), FMath::Max(Min.Y, Max.Y));
	Result.ZeroLevelZ = Transform.GetLocation().Z;

	for (const ULandscapeEditLayerBase* Layer : Landscape->GetEditLayersConst())
	{
		if (Layer)
		{
			Result.EditLayers.Add(Layer->GetName().ToString());
		}
	}
	for (const TPair<FName, FLandscapeTargetLayerSettings>& Pair : Landscape->GetTargetLayers())
	{
		Result.PaintLayers.Add(Pair.Key.ToString());
	}

	const ULandscapeSubsystem* LandscapeSubsystem = Landscape->GetWorld()->GetSubsystem<ULandscapeSubsystem>();
	Result.bWorldPartition = LandscapeSubsystem && LandscapeSubsystem->IsGridBased();
	Result.LoadedStreamingProxies = Info->GetSortedStreamingProxies().Num();
	Result.Material = Landscape->LandscapeMaterial;
	return Result;
}

void UTerrainToolset::ImportHeightmap(ALandscape* Landscape, const FString& FilePath, const FString& EditLayer)
{
	FIntRect Extent;
	ULandscapeInfo* Info = GetInfoAndExtent(Landscape, Extent);
	if (!Info)
	{
		return;
	}
	const FGuid LayerGuid = FindEditLayerGuid(Landscape, EditLayer);
	if (!LayerGuid.IsValid())
	{
		return;
	}

	TArray<uint16> Heights;
	if (!ReadImageFile<uint16>(FilePath, ResolutionOf(Extent), Heights))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ImportHeightmap", "Import Heightmap (Terrain Toolset)"));
	{
		FLandscapeEditDataInterface LandscapeEdit(Info, LayerGuid);
		LandscapeEdit.SetHeightData(Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y, Heights.GetData(), /*InStride=*/0, /*InCalcNormals=*/true);
	}
	Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Heightmap_All);
}

void UTerrainToolset::ExportHeightmap(ALandscape* Landscape, const FString& FilePath, const FString& EditLayer)
{
	FIntRect Extent;
	ULandscapeInfo* Info = GetInfoAndExtent(Landscape, Extent);
	if (!Info)
	{
		return;
	}

	const FString Extension = FPaths::GetExtension(FilePath, /*bIncludeDot=*/true);
	const ILandscapeHeightmapFileFormat* Format = GetLandscapeEditorModule().GetHeightmapFormatByExtension(*Extension);
	if (!Format || !Format->GetInfo().bSupportsExport)
	{
		RaiseError(FString::Printf(TEXT("Cannot export heightmaps as '%s'. Use .png or .r16."), *Extension));
		return;
	}

	FGuid LayerGuid;
	if (!EditLayer.IsEmpty())
	{
		LayerGuid = FindEditLayerGuid(Landscape, EditLayer);
		if (!LayerGuid.IsValid())
		{
			return;
		}
	}

	if (!LayerGuid.IsValid())
	{
		// Exporting the blended result: apply pending edit layer merges first.
		Landscape->ForceUpdateLayersContent();
	}

	FScopedSetLandscapeEditingLayer Scope(Landscape, LayerGuid);
	Info->ExportHeightmap(FilePath, Extent);
}

void UTerrainToolset::AddEditLayer(ALandscape* Landscape, const FString& Name)
{
	if (!Landscape)
	{
		RaiseError(TEXT("Landscape must not be null."));
		return;
	}
	if (Name.IsEmpty())
	{
		RaiseError(TEXT("Edit layer name must not be empty."));
		return;
	}
	if (!Landscape->IsLayerNameUnique(FName(*Name)))
	{
		RaiseError(FString::Printf(TEXT("Edit layer '%s' already exists."), *Name));
		return;
	}
	if (Landscape->IsMaxLayersReached())
	{
		RaiseError(TEXT("Maximum number of edit layers reached (Project Settings > Landscape > Max Number Of Layers)."));
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("AddEditLayer", "Add Edit Layer (Terrain Toolset)"));
	if (Landscape->CreateLayer(FName(*Name)) == INDEX_NONE)
	{
		RaiseError(FString::Printf(TEXT("Failed to create edit layer '%s'."), *Name));
	}
}

void UTerrainToolset::RemoveEditLayer(ALandscape* Landscape, const FString& Name)
{
	if (!Landscape)
	{
		RaiseError(TEXT("Landscape must not be null."));
		return;
	}
	const int32 LayerIndex = Landscape->GetLayerIndex(FName(*Name));
	if (LayerIndex == INDEX_NONE)
	{
		RaiseError(FString::Printf(TEXT("Edit layer '%s' does not exist."), *Name));
		return;
	}
	if (Landscape->GetEditLayersConst().Num() <= 1)
	{
		RaiseError(TEXT("Cannot remove the last edit layer."));
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("RemoveEditLayer", "Remove Edit Layer (Terrain Toolset)"));
	if (!Landscape->DeleteLayer(LayerIndex))
	{
		RaiseError(FString::Printf(TEXT("Failed to remove edit layer '%s'."), *Name));
	}
}

ULandscapeLayerInfoObject* UTerrainToolset::AddPaintLayer(ALandscape* Landscape, const FString& LayerName, const FString& AssetFolder)
{
	FIntRect Extent;
	ULandscapeInfo* Info = GetInfoAndExtent(Landscape, Extent);
	if (!Info)
	{
		return nullptr;
	}
	if (LayerName.IsEmpty())
	{
		RaiseError(TEXT("LayerName must not be empty."));
		return nullptr;
	}
	if (!AssetFolder.StartsWith(TEXT("/")))
	{
		RaiseError(TEXT("AssetFolder must be a content path such as '/Game/Terrain/Layers'."));
		return nullptr;
	}
	const FName Name(*LayerName);
	if (Landscape->HasTargetLayer(Name))
	{
		RaiseError(FString::Printf(TEXT("Paint layer '%s' already exists."), *LayerName));
		return nullptr;
	}

	FScopedTransaction Transaction(LOCTEXT("AddPaintLayer", "Add Paint Layer (Terrain Toolset)"));

	ULandscapeLayerInfoObject* LayerInfo = UE::Landscape::CreateTargetLayerInfo(Name, AssetFolder);
	if (!LayerInfo)
	{
		RaiseError(FString::Printf(TEXT("Failed to create layer info for '%s' in '%s'."), *LayerName, *AssetFolder));
		return nullptr;
	}

	Landscape->AddTargetLayer(Name, FLandscapeTargetLayerSettings(LayerInfo));
	Info->UpdateLayerInfoMap(Landscape);
	const int32 LayerInfoIndex = Info->GetLayerInfoIndex(Name);
	if (Info->Layers.IsValidIndex(LayerInfoIndex))
	{
		Info->Layers[LayerInfoIndex].LayerInfoObj = LayerInfo;
	}
	return LayerInfo;
}

void UTerrainToolset::ImportWeightmap(ALandscape* Landscape, const FString& LayerName, const FString& FilePath, const FString& EditLayer)
{
	FIntRect Extent;
	ULandscapeInfo* Info = GetInfoAndExtent(Landscape, Extent);
	if (!Info)
	{
		return;
	}
	ULandscapeLayerInfoObject* LayerInfo = Info->GetLayerInfoByName(FName(*LayerName));
	if (!LayerInfo)
	{
		RaiseError(FString::Printf(TEXT("Paint layer '%s' does not exist or has no layer info asset."), *LayerName));
		return;
	}
	const FGuid LayerGuid = FindEditLayerGuid(Landscape, EditLayer);
	if (!LayerGuid.IsValid())
	{
		return;
	}

	TArray<uint8> Weights;
	if (!ReadImageFile<uint8>(FilePath, ResolutionOf(Extent), Weights))
	{
		return;
	}

	FScopedTransaction Transaction(LOCTEXT("ImportWeightmap", "Import Weightmap (Terrain Toolset)"));
	{
		FLandscapeEditDataInterface LandscapeEdit(Info, LayerGuid);
		LandscapeEdit.SetAlphaData(LayerInfo, Extent.Min.X, Extent.Min.Y, Extent.Max.X, Extent.Max.Y, Weights.GetData(), /*Stride=*/0);
	}
	Landscape->RequestLayersContentUpdate(ELandscapeLayerUpdateMode::Update_Weightmap_All);
}

TArray<double> UTerrainToolset::SampleHeights(ALandscape* Landscape, const TArray<FVector2D>& Points)
{
	FIntRect Extent;
	ULandscapeInfo* Info = GetInfoAndExtent(Landscape, Extent);
	if (!Info)
	{
		return {};
	}

	// Apply pending edit layer merges so collision reflects the latest edits.
	Landscape->ForceUpdateLayersContent();

	TArray<double> Heights;
	Heights.Reserve(Points.Num());
	for (const FVector2D& Point : Points)
	{
		TOptional<float> Height;
		Info->ForEachLandscapeProxy([&Height, &Point](ALandscapeProxy* Proxy)
		{
			Height = Proxy->GetHeightAtLocation(FVector(Point.X, Point.Y, 0.0));
			return !Height.IsSet();
		});

		if (!Height.IsSet())
		{
			RaiseError(FString::Printf(TEXT("Point (%.1f, %.1f) is outside the loaded landscape."), Point.X, Point.Y));
			return {};
		}
		Heights.Add(Height.GetValue());
	}
	return Heights;
}

#undef LOCTEXT_NAMESPACE
