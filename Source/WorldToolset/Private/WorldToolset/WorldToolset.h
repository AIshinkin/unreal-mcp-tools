#pragma once

#include "CoreMinimal.h"

#include "ToolsetRegistry/ToolsetDefinition.h"

#include "WorldToolset.generated.h"

/**
 * Creates and inspects levels (worlds) in the editor, including World Partition levels.
 * Loading existing levels is covered by SceneTools.
 */
UCLASS(BlueprintType, Hidden)
class UWorldToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	/**
	 * Creates an empty level, saves it and opens it in the editor.
	 * Raises if the currently open level has unsaved changes.
	 * @param LevelPath Content path of the new level, e.g. "/Game/Levels/L_World".
	 * @param bWorldPartition Creates a World Partition (open world) level when true.
	 */
	UFUNCTION(meta = (AICallable), Category = "World")
	static void CreateLevel(const FString& LevelPath, bool bWorldPartition);

	/** Returns whether the currently open level uses World Partition. */
	UFUNCTION(meta = (AICallable), Category = "World")
	static bool IsWorldPartitioned();
};
