#include "WorldToolset.h"

#include "Editor.h"
#include "Engine/World.h"
#include "Kismet/KismetSystemLibrary.h"
#include "LevelEditorSubsystem.h"
#include "Misc/PackageName.h"
#include "WorldPartition/LoaderAdapter/LoaderAdapterShape.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionEditorLoaderAdapter.h"

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

void UWorldToolset::LoadRegion(const FVector& Min, const FVector& Max)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UWorldPartition* WorldPartition = World ? World->GetWorldPartition() : nullptr;
	if (!WorldPartition)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("The open level is not a World Partition level."));
		return;
	}
	const FBox Box(Min.ComponentMin(Max), Min.ComponentMax(Max));
	if (Box.GetVolume() <= 0.0)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("The region must have a non-zero size on every axis."));
		return;
	}

	UWorldPartitionEditorLoaderAdapter* Adapter = WorldPartition->CreateEditorLoaderAdapter<FLoaderAdapterShape>(World, Box, TEXT("Loaded Region"));
	Adapter->GetLoaderAdapter()->SetUserCreated(true);
	Adapter->GetLoaderAdapter()->Load();
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
