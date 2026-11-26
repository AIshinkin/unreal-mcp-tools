#pragma once

#include "CoreMinimal.h"

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "TerrainToolset.generated.h"

class ALandscape;
class ULandscapeLayerInfoObject;
class UMaterialInterface;

/** Parameters for creating a new landscape. */
USTRUCT(BlueprintType)
struct FTerrainCreateSettings
{
	GENERATED_BODY()

	/** Actor label of the new landscape. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FString Label = TEXT("Landscape");

	/** World-space center of the landscape, in cm. Z is the height of heightmap value 32768 (zero level). */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FVector Center = FVector::ZeroVector;

	/** Number of landscape components along X and Y. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "64"))
	FIntPoint ComponentCount = FIntPoint(8, 8);

	/** Quads per section edge. One of 7, 15, 31, 63, 127, 255. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	int32 QuadsPerSection = 63;

	/** Sections per component edge: 1 (1x1) or 2 (2x2). */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "2"))
	int32 SectionsPerComponent = 2;

	/** Horizontal distance between neighboring vertices, in meters. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "0.01"))
	float MetersPerVertex = 1.0f;

	/** Full representable height range (lowest to highest), in meters, centered on Center.Z. 512 m equals the editor default Z scale of 100. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1.0"))
	float HeightRangeMeters = 512.0f;

	/** Landscape material. Null leaves the project default. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/** Optional heightmap (.png 16-bit grayscale or .r16) imported into the base edit layer. Its resolution must equal the landscape vertex resolution. Empty creates a flat landscape. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FString HeightmapFile;

	/** Components per landscape streaming proxy in World Partition levels. Ignored in non-partitioned levels. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain", meta = (ClampMin = "1", ClampMax = "64"))
	int32 WorldPartitionGridSize = 2;

	/** Name for the base edit layer. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FString BaseEditLayer = TEXT("Base");

	/** Additional edit layers created above the base layer, bottom to top. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	TArray<FString> ExtraEditLayers;
};

/** Snapshot of a landscape's layout and layers. */
USTRUCT(BlueprintType)
struct FTerrainInfo
{
	GENERATED_BODY()

	/** Heightmap resolution in vertices. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FIntPoint Resolution = FIntPoint::ZeroValue;

	/** Number of components along X and Y. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FIntPoint ComponentCount = FIntPoint::ZeroValue;

	/** Quads per section edge. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	int32 QuadsPerSection = 0;

	/** Sections per component edge. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	int32 SectionsPerComponent = 0;

	/** Horizontal distance between vertices, in meters. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	float MetersPerVertex = 0.0f;

	/** Full representable height range, in meters. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	float HeightRangeMeters = 0.0f;

	/** World-space XY minimum corner, in cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FVector2D WorldMin = FVector2D::ZeroVector;

	/** World-space XY maximum corner, in cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	FVector2D WorldMax = FVector2D::ZeroVector;

	/** World Z of heightmap value 32768 (zero level), in cm. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	double ZeroLevelZ = 0.0;

	/** Edit layer names, bottom to top. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	TArray<FString> EditLayers;

	/** Paint (target) layer names. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	TArray<FString> PaintLayers;

	/** Number of streaming proxies currently loaded. 0 for non-partitioned landscapes. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	int32 LoadedStreamingProxies = 0;

	/** Whether the landscape lives in a World Partition level. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	bool bWorldPartition = false;

	/** Landscape material. */
	UPROPERTY(BlueprintReadWrite, Category = "Terrain")
	TObjectPtr<UMaterialInterface> Material = nullptr;
};

/**
 * Creates and edits Landscape terrain in the current level: creation with World Partition streaming,
 * heightmap and weightmap import/export per edit layer, edit and paint layer management, and height sampling.
 * Heightmaps are 16-bit (.png or .r16); weightmaps are 8-bit (.png or .r8). Operations covering the whole
 * landscape need all of its streaming proxies loaded in the editor.
 */
UCLASS(BlueprintType, Hidden)
class UTerrainToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Creates a landscape in the current level.
	 * @return The new landscape actor.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static ALandscape* CreateLandscape(const FTerrainCreateSettings& Settings);

	/** Returns the layout and layers of a landscape. */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static FTerrainInfo GetLandscapeInfo(ALandscape* Landscape);

	/**
	 * Overwrites one edit layer's heights from a heightmap file.
	 * @param FilePath Absolute path to a .png (16-bit grayscale) or .r16 file at the landscape's resolution.
	 * @param EditLayer Target edit layer name.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static void ImportHeightmap(ALandscape* Landscape, const FString& FilePath, const FString& EditLayer);

	/**
	 * Writes landscape heights to a heightmap file.
	 * @param FilePath Absolute output path ending in .png or .r16.
	 * @param EditLayer Edit layer to export. Empty exports the final blended result.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static void ExportHeightmap(ALandscape* Landscape, const FString& FilePath, const FString& EditLayer);

	/**
	 * Adds an edit layer on top of the stack.
	 * @param Name Unique layer name.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static void AddEditLayer(ALandscape* Landscape, const FString& Name);

	/** Deletes an edit layer and its content. The last remaining layer cannot be removed. */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static void RemoveEditLayer(ALandscape* Landscape, const FString& Name);

	/**
	 * Registers a paint layer on the landscape, creating its layer info asset.
	 * @param LayerName Paint layer name; must match a layer name used by the landscape material.
	 * @param AssetFolder Content folder for the layer info asset, e.g. "/Game/Terrain/Layers".
	 * @return The layer info asset.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static ULandscapeLayerInfoObject* AddPaintLayer(ALandscape* Landscape, const FString& LayerName, const FString& AssetFolder);

	/**
	 * Overwrites a paint layer's weights in one edit layer from a weightmap file.
	 * @param LayerName Existing paint layer name.
	 * @param FilePath Absolute path to a .png (8-bit grayscale) or .r8 file at the landscape's resolution. 255 = full weight.
	 * @param EditLayer Target edit layer name.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static void ImportWeightmap(ALandscape* Landscape, const FString& LayerName, const FString& FilePath, const FString& EditLayer);

	/**
	 * Samples final landscape height at world XY positions.
	 * @param Points World-space XY positions, in cm.
	 * @return World Z per point, in cm. Raises if a point is outside the loaded landscape.
	 */
	UFUNCTION(meta = (AICallable), Category = "Terrain")
	static TArray<double> SampleHeights(ALandscape* Landscape, const TArray<FVector2D>& Points);
};
