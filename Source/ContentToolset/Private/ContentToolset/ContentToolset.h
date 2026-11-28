#pragma once

#include "CoreMinimal.h"

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "ContentToolset.generated.h"

/**
 * Organizes project content: cleans up the redirectors that moving or renaming assets leaves behind.
 * Moving and renaming assets themselves is covered by AssetTools.
 */
UCLASS(BlueprintType, Hidden)
class UContentToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Lists the redirectors under a content folder (recursively).
	 * @param FolderPath Content folder, e.g. "/Game/Terrain".
	 * @return Package paths of the redirectors found.
	 */
	UFUNCTION(meta = (AICallable), Category = "Content")
	static TArray<FString> FindRedirectors(const FString& FolderPath);

	/**
	 * Same as "Fix Up Redirectors" in the Content Browser: resaves every asset that references a redirector
	 * under the folder so it points at the real asset, then deletes the redirectors. World Partition
	 * external actors that reference them are resaved too.
	 * @param FolderPath Content folder, e.g. "/Game/Terrain".
	 * @return Redirectors that are still there afterwards (for example because a referencing package
	 *         could not be saved). Empty when everything was fixed.
	 */
	UFUNCTION(meta = (AICallable), Category = "Content")
	static TArray<FString> FixupRedirectors(const FString& FolderPath);
};
