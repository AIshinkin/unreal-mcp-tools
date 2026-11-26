#include "WorldToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LevelEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "WorldPartition/WorldPartition.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(WorldToolset)

void UWorldToolset::CreateLevel(const FString& LevelPath, bool bWorldPartition)
{
	if (!GEditor)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("The editor is not available."));
		return;
	}
	if (GEditor->PlayWorld)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Stop Play-In-Editor before creating a level."));
		return;
	}
	if (!FPackageName::IsValidLongPackageName(LevelPath))
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("'%s' is not a valid content path such as '/Game/Levels/L_World'."), *LevelPath));
		return;
	}
	if (FPackageName::DoesPackageExist(LevelPath))
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("An asset already exists at '%s'."), *LevelPath));
		return;
	}

	UWorld* CurrentWorld = GEditor->GetEditorWorldContext().World();
	if (CurrentWorld && CurrentWorld->GetPackage()->IsDirty())
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("The open level '%s' has unsaved changes. Save or discard them first."), *CurrentWorld->GetPackage()->GetName()));
		return;
	}

	ULevelEditorSubsystem* LevelEditor = GEditor->GetEditorSubsystem<ULevelEditorSubsystem>();
	if (!LevelEditor || !LevelEditor->NewLevel(LevelPath, bWorldPartition))
	{
		UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("Failed to create level '%s'. See the output log."), *LevelPath));
	}
}

bool UWorldToolset::IsWorldPartitioned()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("No editor world is loaded."));
		return false;
	}
	return World->IsPartitionedWorld();
}
