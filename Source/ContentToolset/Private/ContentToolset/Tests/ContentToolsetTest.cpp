#include "ContentToolset/ContentToolset.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "AssetToolsModule.h"
#include "EditorAssetLibrary.h"
#include "FileHelpers.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "ToolsetRegistry/ToolCallExceptionHandler.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FContentToolsetSpec, "AI.Toolsets.UnrealMcpTools.ContentToolset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	TUniquePtr<UE::ToolsetRegistry::FToolCallExceptionHandler> ExceptionHandler;
	FString Folder;

END_DEFINE_SPEC(FContentToolsetSpec)

void FContentToolsetSpec::Define()
{
	BeforeEach([this]()
	{
		ExceptionHandler = MakeUnique<UE::ToolsetRegistry::FToolCallExceptionHandler>();
		Folder = FString::Printf(TEXT("/Game/Developers/ContentToolsetTests/T_%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8));
	});

	AfterEach([this]()
	{
		UEditorAssetLibrary::DeleteDirectory(Folder);
		ExceptionHandler.Reset();
	});

	It("rejects an invalid folder", [this]()
	{
		AddExpectedMessage(TEXT("is not a valid content folder"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1, /*IsRegex=*/false);
		ExceptionHandler->CaptureErrorsIn([&]() { UContentToolset::FixupRedirectors(TEXT("not a path")); });
		TestTrue(TEXT("Raised"), ExceptionHandler->GetException().Contains(TEXT("is not a valid content folder")));
	});

	It("returns nothing for a folder without redirectors", [this]()
	{
		TestEqual(TEXT("No redirectors"), UContentToolset::FixupRedirectors(Folder).Num(), 0);
	});

	It("repoints referencers and deletes the redirector", [this]()
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

		UMaterial* Parent = Cast<UMaterial>(AssetTools.CreateAsset(TEXT("M_Parent"), Folder, UMaterial::StaticClass(), NewObject<UMaterialFactoryNew>()));
		UMaterialInstanceConstantFactoryNew* InstanceFactory = NewObject<UMaterialInstanceConstantFactoryNew>();
		InstanceFactory->InitialParent = Parent;
		UMaterialInstanceConstant* Child = Cast<UMaterialInstanceConstant>(AssetTools.CreateAsset(TEXT("MI_Child"), Folder, UMaterialInstanceConstant::StaticClass(), InstanceFactory));
		if (!TestNotNull(TEXT("Parent created"), Parent) || !TestNotNull(TEXT("Child created"), Child))
		{
			return;
		}
		UEditorAssetLibrary::SaveDirectory(Folder);

		// A rename through AssetTools fixes loaded referencers right away and leaves no redirector,
		// so move the parent by hand and leave a redirector in its old package, as a move with
		// unloaded or unsaved referencers would.
		const FString MovedPath = Folder / TEXT("Moved/M_Parent");
		UPackage* OldPackage = Parent->GetPackage();
		UPackage* NewPackage = CreatePackage(*MovedPath);
		Parent->Rename(TEXT("M_Parent"), NewPackage, REN_NonTransactional | REN_DontCreateRedirectors);
		UObjectRedirector* Redirector = NewObject<UObjectRedirector>(OldPackage, TEXT("M_Parent"), RF_Public | RF_Standalone);
		Redirector->DestinationObject = Parent;
		UEditorLoadingAndSavingUtils::SavePackages({ OldPackage, NewPackage }, /*bOnlyDirty=*/false);
		TestEqual(TEXT("Redirector left behind"), UContentToolset::FindRedirectors(Folder).Num(), 1);

		TestEqual(TEXT("Nothing remains"), UContentToolset::FixupRedirectors(Folder).Num(), 0);
		TestEqual(TEXT("No redirectors found afterwards"), UContentToolset::FindRedirectors(Folder).Num(), 0);

		TArray<FName> Dependencies;
		IAssetRegistry::GetChecked().GetDependencies(Child->GetPackage()->GetFName(), Dependencies);
		TestTrue(TEXT("Child now depends on the moved parent"), Dependencies.Contains(FName(*MovedPath)));
		TestFalse(TEXT("Child no longer depends on the old path"), Dependencies.Contains(OldPackage->GetFName()));
	});
}

#endif
