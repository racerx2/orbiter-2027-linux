// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// STRICT left out: windows.h handle type-checking switch
#define ORBITER_MODULE
#include "Orbitersdk.h"
#include "OrbiterResource.h"
#include "AC_resource.h"
#include <stdio.h>
#include <unistd.h>
#include <cerrno> // not upstream: EEXIST
#include <sys/stat.h> // not upstream: lstat
#include <QAbstractButton>
#include <QDialog>

class VesselConfig;
class AtlantisConfig;

static const char *tex_hires_enabled = "Textures2/Atlantis";
static const char *tex_hires_disabled = "Textures2/~Atlantis";

static const char *vcmsh_fname = "Meshes/Atlantis/AtlantisVC.msh";
static const char *msh_hires_bkup = "Meshes/Atlantis/~AtlantisVC_hi.msh";
static const char *msh_lores_bkup = "Meshes/Atlantis/~AtlantisVC_lo.msh";

// not upstream: io.h _access and rename on Orbiter data paths (resolved case-insensitively)
static int access_path (const char *path, int mode)
{
	return access (oapiResolvePath (path).c_str(), mode);
}

static int rename_path (const char *from, const char *to)
{
	std::string t = oapiResolvePath (to);
	struct stat st;
	if (lstat (t.c_str(), &st) == 0) { errno = EEXIST; return -1; } // Windows rename refuses an existing target, POSIX replaces it
	return rename (oapiResolvePath (from).c_str(), t.c_str());
}

// not upstream: BM_SETCHECK / BM_GETCHECK on a dialog control
static void SetCheck (QWidget *hDlg, int id, bool check)
{
	if (QAbstractButton *b = DlgItem<QAbstractButton> (hDlg, id)) b->setChecked (check);
}

static bool IsChecked (QWidget *hDlg, int id)
{
	QAbstractButton *b = DlgItem<QAbstractButton> (hDlg, id);
	return (b && b->isChecked());
}

struct {
	void *hInst;
	AtlantisConfig *item;
} gParams;

class AtlantisConfig: public LaunchpadItem {
public:
	AtlantisConfig(): LaunchpadItem() {}
	char *Name() { return (char*)"Atlantis Configuration"; }
	char *Description();
	bool clbkOpen (QWidget *hLaunchpad);
	bool TexHiresEnabled() const;
	void TexEnableHires (bool enable);
	bool MshHiresEnabled() const;
	bool MshEnableHires (bool enable);
	void InitDialog (QWidget *hWnd);
	void Apply (QWidget *hWnd);
	static void DlgProc (QWidget *hWnd, void *context);
};

char *AtlantisConfig::Description()
{
	return (char*)"Global configuration for the default Space Shuttle Atlantis.";
}

bool AtlantisConfig::clbkOpen (QWidget *hLaunchpad)
{
	// respond to user double-clicking the item in the list
	return OpenDialog (gParams.hInst, hLaunchpad, IDD_ACONFIG, DlgProc);
}

bool AtlantisConfig::TexHiresEnabled () const
{
	// check if the Atlantis highres texture directory is present
	return (access_path (tex_hires_enabled, 0) != -1);
}

void AtlantisConfig::TexEnableHires (bool enable)
{
	if (TexHiresEnabled() == enable) return; // nothing to do

	if (enable) {
		rename_path (tex_hires_disabled, tex_hires_enabled);
	} else {
		// to disable the highres textures, we simply rename the directory
		// so that orbiter's texture manager can't find it
		rename_path (tex_hires_enabled, tex_hires_disabled);
	}
}

bool AtlantisConfig::MshHiresEnabled () const
{
	// check if backup of low-res mesh is present
	return (access_path (msh_lores_bkup, 0) != -1);
}

bool AtlantisConfig::MshEnableHires (bool enable)
{
	if (MshHiresEnabled() == enable) return false; // nothing to do

	if (enable) {
		if (access_path (msh_hires_bkup, 0) == -1) return false; // high-res backup not found
		if (access_path (vcmsh_fname, 0) != -1 && access_path (msh_lores_bkup, 0) == -1)
			rename_path (vcmsh_fname, msh_lores_bkup); // back up low-res mesh
		rename_path (msh_hires_bkup, vcmsh_fname); // activate high-res mesh
	} else {
		if (access_path (msh_lores_bkup, 0) == -1) return false; // low-res backup not found
		if (access_path (vcmsh_fname, 0) != -1 && access_path (msh_hires_bkup, 0) == -1)
			rename_path (vcmsh_fname, msh_hires_bkup); // back up high-res mesh
		rename_path (msh_lores_bkup, vcmsh_fname); // activate low-res mesh
	}
	return true;
}

void AtlantisConfig::InitDialog (QWidget *hWnd)
{
	bool texhires = TexHiresEnabled();
	bool mshhires = MshHiresEnabled();
	SetCheck (hWnd, IDC_RADIO1, texhires);
	SetCheck (hWnd, IDC_RADIO2, !texhires);
	SetCheck (hWnd, IDC_RADIO3, mshhires);
	SetCheck (hWnd, IDC_RADIO4, !mshhires);
}

void AtlantisConfig::Apply (QWidget *hWnd)
{
	bool texhires = IsChecked (hWnd, IDC_RADIO1);
	TexEnableHires (texhires);
	bool mshhires = IsChecked (hWnd, IDC_RADIO3);
	MshEnableHires (mshhires);
}

void AtlantisConfig::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((AtlantisConfig*)context)->InitDialog (hWnd);
	// WM_COMMAND (the item passed as DWLP_USER is the context)
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDOK:
			((AtlantisConfig*)context)->Apply (hWnd);
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
	gParams.item = new AtlantisConfig;
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
