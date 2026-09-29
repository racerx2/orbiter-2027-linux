// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// STRICT left out: windows.h handle type-checking switch
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include "OrbiterResource.h"
#include "DGC_resource.h"
#include <stdio.h>
#include <unistd.h>
#include <QAbstractButton>
#include <QDialog>

class VesselConfig;
class DGConfig;

static const char *hires_enabled = "Textures2/DG";
static const char *hires_disabled = "Textures2/~DG";

struct {
	void *hInst;
	DGConfig *item;
} gParams;

class DGConfig: public LaunchpadItem {
public:
	DGConfig(): LaunchpadItem() {}
	char *Name() { return (char*)"DG Configuration"; }
	char *Description();
	bool clbkOpen (QWidget *hLaunchpad);
	bool HiresEnabled() const;
	void EnableHires (bool enable);
	void InitDialog (QWidget *hWnd);
	void Apply (QWidget *hWnd);
	static void DlgProc (QWidget *hWnd, void *context);
};

char *DGConfig::Description()
{
	return (char*)"Global configuration for the default Delta-glider.";
}

bool DGConfig::clbkOpen (QWidget *hLaunchpad)
{
	// respond to user double-clicking the item in the list
	return OpenDialog (gParams.hInst, hLaunchpad, IDD_DGCONFIG, DlgProc);
}

bool DGConfig::HiresEnabled () const
{
	// check if the DG highres texture directory is present
	return (access (oapiResolvePath (hires_enabled).c_str(), F_OK) != -1);
}

void DGConfig::EnableHires (bool enable)
{
	if (HiresEnabled() == enable) return; // nothing to do

	if (enable) {
		rename (oapiResolvePath (hires_disabled).c_str(), oapiResolvePath (hires_enabled).c_str());
	} else {
		// to disable the highres textures, we simply rename the directory
		// so that orbiter's texture manager can't find it
		rename (oapiResolvePath (hires_enabled).c_str(), oapiResolvePath (hires_disabled).c_str());
	}
}

void DGConfig::InitDialog (QWidget *hWnd)
{
	bool hires = HiresEnabled();
	DlgItem<QAbstractButton> (hWnd, IDC_RADIO1)->setChecked (hires);
	DlgItem<QAbstractButton> (hWnd, IDC_RADIO2)->setChecked (!hires);
}

void DGConfig::Apply (QWidget *hWnd)
{
	bool enable = DlgItem<QAbstractButton> (hWnd, IDC_RADIO1)->isChecked();
	EnableHires (enable);
}

void DGConfig::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((DGConfig*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDOK:
			((DGConfig*)context)->Apply (hWnd); // DWLP_USER: the item is the dialog context
			qobject_cast<QDialog*> (hWnd)->done (0);
			return;
		case IDCANCEL:
			qobject_cast<QDialog*> (hWnd)->done (0);
			return;
		}
	});
}

// ==============================================================
// The DLL entry point
// ==============================================================

DLLCLBK void InitModule (void *hDLL)
{
	gParams.hInst = hDLL;
	gParams.item = new DGConfig;
	// create the new config item
	LAUNCHPADITEM_HANDLE root = oapiFindLaunchpadItem ("Vessel configuration");
	// find the config root entry provided by orbiter
	oapiRegisterLaunchpadItem (gParams.item, root);
	// register the DG config entry
}

// ==============================================================
// The DLL exit point
// ==============================================================

DLLCLBK void ExitModule (void *hDLL)
{
	// Unregister the launchpad items
	oapiUnregisterLaunchpadItem (gParams.item);
	delete gParams.item;
}
