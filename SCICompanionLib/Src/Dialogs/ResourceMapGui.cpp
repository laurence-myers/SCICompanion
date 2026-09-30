#include "stdafx.h"
#include "ResourceMap.h"
#include "ResourceEntity.h"
#include "ResourceBlob.h"
#include "SaveResourceDialog.h"
#include "format.h"
#include "CorePrompt.h"

// The members of CResourceMap that show a dialog. They are in the GUI library;
// the core library has the rest of CResourceMap (ResourceMap.cpp).

using namespace std;

void CResourceMap::AppendResourceAskForNumber(ResourceEntity &resource)
{
	AppendResourceAskForNumber(resource, "", false);
}

void CResourceMap::AppendResourceAskForNumber(ResourceEntity &resource, const std::string &name, bool warnOnOverwrite)
{
	// Invoke dialog to suggest/ask for a resource number
	SaveResourceDialog srd(warnOnOverwrite, resource.GetType());
	srd.Init(-1, SuggestResourceNumber(resource.GetType()), name);
	if (IDOK == srd.DoModal())
	{
		// Assign it.
		resource.ResourceNumber = srd.GetResourceNumber();
		resource.PackageNumber = srd.GetPackageNumber();
		AssignName(resource.GetType(), resource.ResourceNumber, NoBase36, srd.GetName().c_str());
		AppendResource(resource);
	}
}

//
// Ask the user where to save the resource... and then save it.
//
HRESULT CResourceMap::AppendResourceAskForNumber(ResourceBlob &resource, bool warnOnOverwrite)
{
	if (!IsVersionCompatible(resource.GetType(), resource.GetVersion(), GetSCIVersion()))
	{
		if (IDNO == AfxMessageBox("The version of the resource being added does not match the version of the game.\nAdding it might cause the game to be corrupted.\nDo you want to go ahead anyway?", MB_YESNO))
		{
			return E_FAIL;
		}
	}
	// Invoke dialog to suggest/ask for a resource number
	SaveResourceDialog srd(warnOnOverwrite, resource.GetType());
	srd.Init(-1, SuggestResourceNumber(resource.GetType()), resource.GetName());
	if (IDOK == srd.DoModal())
	{
		// Assign it.
		resource.SetNumber(srd.GetResourceNumber());
		resource.SetPackage(srd.GetPackageNumber());
		resource.SetName(nullptr);
		if (!srd.GetName().empty())
		{
			resource.SetName(srd.GetName().c_str());
		}

		// Save it.
		return AppendResource(resource);
	}
	else
	{
		return E_FAIL; // User cancelled.
	}
}

void CResourceMap::SetGameFolder(const string &gameFolder)
{
	sci::Status opened = _OpenGameFolder(gameFolder);
	if (!opened)
	{
		SafeMessageBox(fmt::format("Unable to open resource map: {0}", opened.error().message), MB_OK | MB_ICONWARNING);
		AfxThrowUserException();
	}
}
