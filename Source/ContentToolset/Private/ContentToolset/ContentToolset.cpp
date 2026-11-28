#include "ContentToolset.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "Editor.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/PackageName.h"
#include "ObjectTools.h"
#include "UObject/Package.h"
#include "UObject/ObjectRedirector.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(ContentToolset)

namespace UE::ContentToolset::Private
{
	bool NormalizeFolder(const FString& FolderPath, FString& OutFolder)
	{
		OutFolder = FolderPath;
		OutFolder.RemoveFromEnd(TEXT("/"));
		FText Reason;
		if (!FPackageName::IsValidLongPackageName(OutFolder, /*bIncludeReadOnlyRoots=*/false, &Reason))
		{
			UKismetSystemLibrary::RaiseScriptError(FString::Printf(TEXT("'%s' is not a valid content folder such as '/Game/Terrain': %s"), *FolderPath, *Reason.ToString()));
			return false;
		}
		return true;
	}

	TArray<FAssetData> GetRedirectors(const FString& Folder)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		AssetRegistry.ScanPathsSynchronous({ Folder }, /*bForceRescan=*/true);

		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(UObjectRedirector::StaticClass()->GetClassPathName());

		TArray<FAssetData> Redirectors;
		AssetRegistry.GetAssets(Filter, Redirectors);
		return Redirectors;
	}

	TArray<FString> ToPackageNames(const TArray<FAssetData>& Assets)
	{
		TArray<FString> Names;
		for (const FAssetData& Asset : Assets)
		{
			Names.AddUnique(Asset.PackageName.ToString());
		}
		Names.Sort();
		return Names;
	}
}

TArray<FString> UContentToolset::FindRedirectors(const FString& FolderPath)
{
	using namespace UE::ContentToolset::Private;
	FString Folder;
	if (!NormalizeFolder(FolderPath, Folder))
	{
		return {};
	}
	return ToPackageNames(GetRedirectors(Folder));
}

TArray<FString> UContentToolset::FixupRedirectors(const FString& FolderPath)
{
	using namespace UE::ContentToolset::Private;
	FString Folder;
	if (!NormalizeFolder(FolderPath, Folder))
	{
		return {};
	}
	if (GEditor && GEditor->PlayWorld)
	{
		UKismetSystemLibrary::RaiseScriptError(TEXT("Stop Play-In-Editor before fixing up redirectors."));
		return {};
	}

	// IAssetTools::FixupReferencers always shows a modal report in 5.8, which blocks an agent,
	// so this does the same work directly: resave referencers, then delete unreferenced redirectors.
	TArray<UObjectRedirector*> Redirectors;
	TSet<FName> RedirectorPackages;
	TMap<FSoftObjectPath, FSoftObjectPath> RedirectorMap;
	for (const FAssetData& Asset : GetRedirectors(Folder))
	{
		if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Asset.GetAsset()))
		{
			Redirectors.Add(Redirector);
			RedirectorPackages.Add(Redirector->GetOutermost()->GetFName());
			if (Redirector->DestinationObject)
			{
				RedirectorMap.Add(FSoftObjectPath(Redirector), FSoftObjectPath(Redirector->DestinationObject));
			}
		}
	}
	if (Redirectors.IsEmpty())
	{
		return {};
	}

	// Load every package that references a redirector. Loading resolves hard references to the
	// destination, so saving the package rewrites them.
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	TSet<FName> ReferencerNames;
	for (const FName& RedirectorPackage : RedirectorPackages)
	{
		TArray<FName> Referencers;
		AssetRegistry.GetReferencers(RedirectorPackage, Referencers);
		ReferencerNames.Append(Referencers);
	}
	ReferencerNames = ReferencerNames.Difference(RedirectorPackages);

	TArray<UPackage*> ReferencingPackages;
	for (const FName& Name : ReferencerNames)
	{
		UPackage* Package = FindPackage(nullptr, *Name.ToString());
		if (!Package)
		{
			Package = LoadPackage(nullptr, *Name.ToString(), LOAD_None);
		}
		if (Package)
		{
			ReferencingPackages.Add(Package);
		}
	}

	if (!ReferencingPackages.IsEmpty())
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		AssetTools.RenameReferencingSoftObjectPaths(ReferencingPackages, RedirectorMap);
		for (UPackage* Package : ReferencingPackages)
		{
			Package->MarkPackageDirty();
		}
		UEditorLoadingAndSavingUtils::SavePackages(ReferencingPackages, /*bOnlyDirty=*/false);
	}

	// Delete the redirectors nothing references any more.
	TArray<FString> ReferencerFolders = { Folder };
	for (UPackage* Package : ReferencingPackages)
	{
		ReferencerFolders.AddUnique(FPackageName::GetLongPackagePath(Package->GetName()));
	}
	AssetRegistry.ScanPathsSynchronous(ReferencerFolders, /*bForceRescan=*/true);

	TArray<UObject*> ToDelete;
	for (UObjectRedirector* Redirector : Redirectors)
	{
		TArray<FName> Remaining;
		AssetRegistry.GetReferencers(Redirector->GetOutermost()->GetFName(), Remaining);
		Remaining.RemoveAll([&RedirectorPackages](const FName& Name) { return RedirectorPackages.Contains(Name); });
		if (Remaining.IsEmpty())
		{
			Redirector->RemoveFromRoot();
			ToDelete.Add(Redirector);
		}
	}
	if (!ToDelete.IsEmpty())
	{
		ObjectTools::ForceDeleteObjects(ToDelete, /*ShowConfirmation=*/false);
	}

	return ToPackageNames(GetRedirectors(Folder));
}
