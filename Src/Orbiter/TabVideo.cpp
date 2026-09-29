// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// VideoTab class
//=============================================================================

#define OAPI_IMPLEMENTATION

#include "Orbiter.h"
#include "TabVideo.h"
#include "resource.h"
#include "ResDialog.h"
#include <QComboBox>
#include <QDialog>
#include <QPlainTextEdit>
#include <QPushButton>

using namespace std;

static PCSTR strInfo_Default = "No graphics engine has been selected. Orbiter will run in console mode.";

//-----------------------------------------------------------------------------
// DefVideoTab class

orbiter::DefVideoTab::DefVideoTab (const LaunchpadDialog *lp): LaunchpadTab (lp)
{
	idxClient = 0;
	strInfo = 0;
	SetInfoString(strInfo_Default);
}

//-----------------------------------------------------------------------------

orbiter::DefVideoTab::~DefVideoTab()
{
	if (strInfo) {
		delete []strInfo;
		strInfo = NULL;
	}
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::Create ()
{
	hTab = CreateTab (IDD_PAGE_DEV);
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::ShowInterface(QWidget *hTab, bool show)
{
	static int item[] = {
		IDC_VID_STATIC1, IDC_VID_STATIC2, IDC_VID_STATIC3, IDC_VID_STATIC5,
		IDC_VID_STATIC6, IDC_VID_STATIC7, IDC_VID_STATIC8, IDC_VID_STATIC9,
		IDC_VID_DEVICE, IDC_VID_ENUM, IDC_VID_STENCIL,
		IDC_VID_FULL, IDC_VID_WINDOW, IDC_VID_MODE, IDC_VID_BPP, IDC_VID_VSYNC,
		IDC_VID_PAGEFLIP, IDC_VID_WIDTH, IDC_VID_HEIGHT, IDC_VID_ASPECT,
		IDC_VID_4X3, IDC_VID_16X10, IDC_VID_16X9, IDC_VID_INFO
	};
	for (size_t i = 0; i < sizeof(item)/sizeof(item[0]); i++) {
		oapiResDlgItem(hTab, item[i])->setVisible(show);
	}
}

//-----------------------------------------------------------------------------

BOOL orbiter::DefVideoTab::OnInitDialog(QWidget *hWnd)
{
	ShowInterface(hWnd, false);
	EnumerateClients(hWnd);

	// WM_COMMAND
	QObject::connect(DlgItem<QComboBox>(hWnd, IDC_VID_COMBO_MODULE), &QComboBox::activated, hWnd, [this](int idx) {
		if (idx >= 0) SelectClientIndex(idx); // CBN_SELCHANGE
	});
	QObject::connect(DlgItem<QPushButton>(hWnd, IDC_VID_MODULE_INFO), &QPushButton::clicked, hWnd, [this]() {
		QDialog *dlg = qobject_cast<QDialog*>(oapiCreateResDialog(AppInstance(), IDD_MSG, LaunchpadWnd()));
		if (!dlg) return;
		InfoProc(dlg, strInfo);
		dlg->exec(); // DialogBoxParam
		delete dlg;
	});
	return TRUE;
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::OnGraphicsClientLoaded(oapi::GraphicsClient* gc, const char *moduleName)
{
	std::string fname = fs::path(moduleName).stem().string();

	int newIdx = DlgItem<QComboBox>(hTab, IDC_VID_COMBO_MODULE)->findText(QString::fromStdString(fname), Qt::MatchStartsWith);
	if (newIdx != (int)idxClient) {
		DlgItem<QComboBox>(hTab, IDC_VID_COMBO_MODULE)->setCurrentIndex(newIdx);
		ShowInterface(hTab, newIdx > 0);
		pCfg->AddActiveModule(fname.c_str());
		idxClient = newIdx;
	}

	char buf[1024];
	// read module info string
	if (LoadModuleString(moduleName, 1000, buf, 1024)) {
		buf[1023] = '\0';
		SetInfoString(buf);
	}

	// the client connects the signals of the video tab controls it uses (window procedure of the tab)
	gc->LaunchpadVideoWndProc(hTab);
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::SetConfig (Config *cfg)
{
	// retrieve standard parameters from client, if available
	oapi::GraphicsClient *gc = pLp->App()->GetGraphicsClient();
	if (gc) {
		gc->clbkRefreshVideoData();
		oapi::GraphicsClient::VIDEODATA *data = gc->GetVideoData();
		cfg->CfgDevPrm.bFullscreen = data->fullscreen;
		cfg->CfgDevPrm.bNoVsync    = data->novsync;
		cfg->CfgDevPrm.bPageflip   = data->pageflip;
		cfg->CfgDevPrm.bTryStencil = data->trystencil;
		cfg->CfgDevPrm.bForceEnum  = data->forceenum;
		cfg->CfgDevPrm.WinW        = data->winw;
		cfg->CfgDevPrm.WinH        = data->winh;
		cfg->CfgDevPrm.Device_idx  = data->deviceidx;
		cfg->CfgDevPrm.Device_mode = data->modeidx;
		cfg->CfgDevPrm.Device_out  = data->outputidx;
		cfg->CfgDevPrm.Device_style= data->style;
	} else {
		// should not be required
		cfg->CfgDevPrm.bFullscreen = false;
		cfg->CfgDevPrm.bNoVsync    = true;
		cfg->CfgDevPrm.bPageflip   = true;
		cfg->CfgDevPrm.bTryStencil = false;
		cfg->CfgDevPrm.bForceEnum  = true;
		cfg->CfgDevPrm.WinW        = 400;
		cfg->CfgDevPrm.WinH        = 300;
		cfg->CfgDevPrm.Device_idx  = 0;
		cfg->CfgDevPrm.Device_mode = 0;
		cfg->CfgDevPrm.Device_out  = 0;
		cfg->CfgDevPrm.Device_style= 1;
	}
	cfg->CfgDevPrm.bStereo = false; // not currently set
}

//-----------------------------------------------------------------------------

bool orbiter::DefVideoTab::OpenHelp ()
{
	OpenTabHelp ("tab_video");
	return true;
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::EnumerateClients(QWidget *hTab)
{
	QComboBox *cb = DlgItem<QComboBox>(hTab, IDC_VID_COMBO_MODULE);
	cb->clear();
	PCSTR strConsole = "Console mode (no engine loaded)";
	oapiComboAddString(cb, strConsole);
	ScanDir(hTab, "Modules/Plugin");
	cb->setCurrentIndex(0);
}

//-----------------------------------------------------------------------------
//! Find Graphics engine modules in dir
void orbiter::DefVideoTab::ScanDir(QWidget *hTab, const fs::path& dir)
{
	std::error_code ec;
	fs::path rdir = oapiResolvePath(dir.string().c_str());
	for (auto& entry : fs::directory_iterator(rdir, ec)) {
		fs::path modulepath;
		auto clientname = entry.path().stem().string();
		if (entry.is_directory()) {
			modulepath = oapiResolvePath((rdir / clientname / (clientname + ".so")).string().c_str());
			if (!fs::exists(modulepath))
				continue;
		}
		else if (entry.path().extension().string() == ".so")
			modulepath = entry.path();
		else
			continue;

		// We've found a potential module. Read its strings without loading it.
		char catstr[256];
		// read category string
		if (LoadModuleString(modulepath.string().c_str(), 1001, catstr, 256)) {
			if (!strcmp(catstr, "Graphics engines")) {
				oapiComboAddString(DlgItem<QComboBox>(hTab, IDC_VID_COMBO_MODULE), clientname.c_str());
			}
		}
	}
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::SelectClientIndex(UINT idx)
{
	ShowInterface(hTab, idx > 0);

	char name[256];
	QComboBox *cb = DlgItem<QComboBox>(hTab, IDC_VID_COMBO_MODULE);
	if (idxClient) { // unload the current client
		snprintf(name, 256, "%s", cb->itemText(idxClient).toUtf8().constData());
		// drop the client's connections to the video controls with the client
		static int item[] = {
			IDC_VID_DEVICE, IDC_VID_ENUM, IDC_VID_STENCIL, IDC_VID_FULL, IDC_VID_WINDOW, IDC_VID_MODE, IDC_VID_BPP,
			IDC_VID_VSYNC, IDC_VID_PAGEFLIP, IDC_VID_WIDTH, IDC_VID_HEIGHT, IDC_VID_ASPECT, IDC_VID_4X3, IDC_VID_16X10,
			IDC_VID_16X9, IDC_VID_INFO
		};
		for (int id : item)
			QObject::disconnect(oapiResDlgItem(hTab, id), nullptr, nullptr, nullptr);
		pCfg->DelActiveModule(name);
		pLp->App()->UnloadModule(name);
		pCfg->CfgDevPrm.Device_idx = -1;
	}
	if (idx) { // load the new client
		const char* path = "Modules/Plugin";
		snprintf(name, 256, "%s", cb->itemText(idx).toUtf8().constData());
		pLp->App()->LoadModule(path, name);
	}
	else
		SetInfoString(strInfo_Default);
}

void orbiter::DefVideoTab::SetInfoString(PCSTR str)
{
	if (strInfo)
		delete []strInfo;
	strInfo = new char[strlen(str) + 1];
	strcpy(strInfo, str);
}

//-----------------------------------------------------------------------------

void orbiter::DefVideoTab::InfoProc(QWidget *hWnd, const char *info)
{
	// WM_INITDIALOG
	DlgItem<QPlainTextEdit>(hWnd, IDC_MSG)->setPlainText(QString::fromUtf8(info));
	// WM_COMMAND
	QObject::connect(DlgItem<QPushButton>(hWnd, IDOK), &QPushButton::clicked, qobject_cast<QDialog*>(hWnd), &QDialog::accept);
}

// the video parameters go to the graphics client through the signals it connects in LaunchpadVideoWndProc
