// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// STRICT left out: windows.h handle type-checking switch
#define ORBITER_MODULE

#include "Orbitersdk.h"
#include "OrbiterResource.h"
#include "resource.h"
#include <QComboBox>
#include <QDialog>
#include <cstring>
#include <strings.h>
#include <dlfcn.h>
#include <filesystem>
namespace fs = std::filesystem;

using namespace std;

class AtmConfig;

const fs::path CelbodyDir = fs::path("Modules") / "Celbody";
const char *ModuleItem = "MODULE_ATM";

struct {
	void *hInst;
	AtmConfig *item;
} gParams;

class AtmConfig: public LaunchpadItem {
public:
	AtmConfig();
	~AtmConfig();
	char *Name() { return (char*)"Atmosphere Configuration"; }
	char *Description();
	void Read (const char *celbody);
	void Write(const char *celbody);
	bool clbkOpen (QWidget *hLaunchpad);
	void InitDialog (QWidget *hWnd);
	void UpdateData (QWidget *hWnd);
	void Apply (QWidget *hWnd);
	void OpenHelp (QWidget *hWnd);
	static void DlgProc (QWidget*, void*);

protected:
	// scan the 'Modules\Celbody' folder for directories, and
	// 'atmosphere' directories in these.
	void ScanCelbodies (QWidget *hWnd);

	// scan the 'Modules\Celbody\<Name>\Atmosphere' folder for
	// atmosphere plugin modules
	void ScanModules (const char *celbody);

	void ClearModules ();

	// Populate celbody list and atmosphere model list
	void ListCelbodies (QWidget *hWnd);
	void ListModules (QWidget *hWnd);

	void CelbodyChanged (QWidget *hWnd);
	void ModelChanged (QWidget *hWnd);

	char celbody[256];

	struct MODULESPEC {
		char module_name[256];
		char model_name[256];
		char model_desc[512];
		MODULESPEC *next;
	} *module_first, *module_curr;
	
};

AtmConfig::AtmConfig(): LaunchpadItem()
{
	module_first = module_curr = 0;
	celbody[0] = '\0';
}

AtmConfig::~AtmConfig ()
{
	ClearModules ();
}

void AtmConfig::ClearModules ()
{
	while (module_first) {
		MODULESPEC *ms = module_first;
		module_first = module_first->next;
		delete ms;
	}
	module_curr = 0;
}

char *AtmConfig::Description()
{
	return (char*)"Configure atmospheric parameters for celestial bodies.";
}

void AtmConfig::Read (const char *celbody)
{
	char cfgname[256];
	strcpy (cfgname, celbody); strcat (cfgname, "/Atmosphere.cfg");
	FILEHANDLE hFile = oapiOpenFile (cfgname, FILE_IN, CONFIG);
	if (hFile) {
		char name[256];
		oapiReadItem_string (hFile, (char*)ModuleItem, name);
		for (module_curr = module_first; module_curr; module_curr = module_curr->next)
			if (!strcasecmp (module_curr->module_name, name)) break;
		oapiCloseFile (hFile, FILE_IN);
	}
}

void AtmConfig::Write (const char *celbody)
{
	char cfgname[256];
	strcpy (cfgname, celbody); strcat (cfgname, "/Atmosphere.cfg");
	FILEHANDLE hFile = oapiOpenFile (cfgname, FILE_OUT, CONFIG);
	if (hFile) {
		if (module_curr && module_curr->module_name[0])
			oapiWriteItem_string (hFile, (char*)ModuleItem, module_curr->module_name);
		else
			oapiWriteItem_string (hFile, (char*)ModuleItem, (char*)"[None]");
		oapiCloseFile (hFile, FILE_OUT);
	}
}

bool AtmConfig::clbkOpen (QWidget *hLaunchpad)
{
	// respond to user double-clicking the item in the list
	return OpenDialog (gParams.hInst, hLaunchpad, IDD_CONFIG, DlgProc);
}

void AtmConfig::InitDialog (QWidget *hWnd)
{
	ListCelbodies (hWnd);
}

void AtmConfig::ListCelbodies (QWidget *hWnd)
{
	ScanCelbodies (hWnd);
	if (!DlgItem<QComboBox> (hWnd, IDC_COMBO2)->count()) return;
	int idx = DlgItem<QComboBox> (hWnd, IDC_COMBO2)->findText ("Earth", Qt::MatchFixedString); // CB_FINDSTRINGEXACT ignores case
	if (idx < 0) idx = 0;
	DlgItem<QComboBox> (hWnd, IDC_COMBO2)->setCurrentIndex (idx);
	CelbodyChanged (hWnd);
}

void AtmConfig::ListModules (QWidget *hWnd)
{
	DlgItem<QComboBox> (hWnd, IDC_COMBO1)->clear();
	oapiComboAddString (DlgItem<QComboBox> (hWnd, IDC_COMBO1), "[None]");

	if (!celbody[0]) return; // nothing to do

	ScanModules (celbody);
	Read (celbody);

	MODULESPEC *ms = module_first;
	while (ms) {
		oapiComboAddString (DlgItem<QComboBox> (hWnd, IDC_COMBO1), ms->model_name);
		ms = ms->next;
	}
	int idx = 0;
	if (module_curr) {
		MODULESPEC *ms = module_first;
		for (idx = 0; ms && ms != module_curr; ms = ms->next, idx++);
		idx++;
	}
	DlgItem<QComboBox> (hWnd, IDC_COMBO1)->setCurrentIndex (idx);
	ModelChanged (hWnd);
}

void AtmConfig::UpdateData (QWidget *hWnd)
{
	int i, model = DlgItem<QComboBox> (hWnd, IDC_COMBO1)->currentIndex();
	if (!model) {
		module_curr = 0;
	} else {
		for (module_curr = module_first, i = 1; module_curr && i < model; module_curr = module_curr->next, i++);
	}
}

void AtmConfig::CelbodyChanged (QWidget *hWnd)
{
	int idx = DlgItem<QComboBox> (hWnd, IDC_COMBO2)->currentIndex();
	snprintf (celbody, 256, "%s", DlgItem<QComboBox> (hWnd, IDC_COMBO2)->itemText (idx).toUtf8().constData());
	ListModules (hWnd);
}

void AtmConfig::ModelChanged (QWidget *hWnd)
{
	int i, model = DlgItem<QComboBox> (hWnd, IDC_COMBO1)->currentIndex();
	if (!model) {
		oapiSetDlgItemText (hWnd, IDC_EDIT1, "Atmosphere effects disabled.");
	} else {
		MODULESPEC *ms = module_first;
		for (i = 1; i < model && ms; i++)
			ms = ms->next;
		if (ms) oapiSetDlgItemText (hWnd, IDC_EDIT1, ms->model_desc);
	}
}

void AtmConfig::Apply (QWidget *hWnd)
{
	UpdateData (hWnd);
	Write (celbody);
}

void AtmConfig::OpenHelp (QWidget *hWnd)
{
	HELPCONTEXT hc = {
		(char*)"html/Orbiter.chm",
		(char*)"extra_atmconfig",
		0, 0
	};
	oapiOpenLaunchpadHelp (&hc);
}

void AtmConfig::ScanCelbodies (QWidget *hWnd)
{
	DlgItem<QComboBox> (hWnd, IDC_COMBO2)->clear();

	for (auto& dir : fs::directory_iterator(CelbodyDir)) {
		auto path = dir.path();
		if (dir.is_directory()) {
			std::error_code ec;
			auto atmdir = fs::directory_entry(path / "Atmosphere", ec);
			if(!ec && atmdir.is_directory()) {
				oapiComboAddString(DlgItem<QComboBox>(hWnd, IDC_COMBO2), path.filename().string().c_str());
			}
		}
	}
}

void AtmConfig::ScanModules (const char *celbody)
{
	ClearModules ();

	auto path = CelbodyDir / celbody / "Atmosphere";
	MODULESPEC* module_last = 0;
	for (auto& entry : fs::directory_iterator(path)) {
		auto module = entry.path();
		if (module.extension().string() == ".so") {
			const auto name = module.stem().string();

			MODULESPEC* ms = new MODULESPEC;
			if (module_last) module_last->next = ms;
			else             module_first = ms;
			module_last = ms;
			strncpy(ms->module_name, name.c_str(), 255);
			strncpy(ms->model_name, name.c_str(), 255);
			ms->model_desc[0] = '\0';
			ms->next = 0;

			// get info from the module
			void *hModule = dlopen(module.string().c_str(), RTLD_NOW);
			if (hModule) {
				char* (*name_func)() = (char* (*)())dlsym(hModule, "ModelName");
				if (name_func) strncpy(ms->model_name, name_func(), 255);
				char* (*desc_func)() = (char* (*)())dlsym(hModule, "ModelDesc");
				if (desc_func) strncpy(ms->model_desc, desc_func(), 511);
				dlclose(hModule);
			}
		}
	}
}

void AtmConfig::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG: the class instance is the context, kept by the handler below (DWLP_USER)
		((AtmConfig*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDOK:
			((AtmConfig*)context)->Apply (hWnd);
			//EndDialog (hWnd, 0);
			return;
		case IDCANCEL:
			qobject_cast<QDialog*> (hWnd)->done (0);
			return;
		case IDC_BUTTON1:
			((AtmConfig*)context)->OpenHelp (hWnd);
			return;
		case IDC_COMBO1:
			if (code == RESN_SELCHANGE)
				((AtmConfig*)context)->ModelChanged (hWnd);
			return;
		case IDC_COMBO2:
			if (code == RESN_SELCHANGE)
				((AtmConfig*)context)->CelbodyChanged (hWnd);
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
	gParams.item = new AtmConfig;
	// create the new config item
	LAUNCHPADITEM_HANDLE root = oapiFindLaunchpadItem ("Celestial body configuration");
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
