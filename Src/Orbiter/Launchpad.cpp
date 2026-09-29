// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include <stdio.h>
#include <strings.h>
#include <time.h>
#include <fstream>
#include "resource.h"
#include "Orbiter.h"
#include "Launchpad.h"
#include "TabScenario.h"
#include "TabOptions.h"
#include "TabModule.h"
#include "TabVideo.h"
#include "TabExtra.h"
#include "TabAbout.h"
#include "Config.h"
#include "Log.h"
#include "Util.h"
#include "about.hpp"
#include "Help.h"
#include "Memstat.h"
#include "ResDialog.h"
#include <QApplication>
#include <QCloseEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QTimer>
#include <QTreeWidget>

using namespace std;

// WM_SIZE modes
#define SIZE_RESTORED  0
#define SIZE_MINIMIZED 1

//=============================================================================
// Name: class LaunchpadDialog
// Desc: Handles the startup dialog ("Launchpad")
//=============================================================================

static orbiter::LaunchpadDialog *g_pDlg = 0;
static time_t time0 = 0;

const DWORD dlgcol = 0xF0F4F8; // main dialog background colour

// COLORREF (0x00bbggrr) as QColor
static QColor Colorref (DWORD c)
{
	return QColor (c & 0xff, (c >> 8) & 0xff, (c >> 16) & 0xff);
}

// GetClientRect counterpart
static RECT ClientRect (QWidget *w)
{
	RECT r = {0, 0, w ? w->width() : 0, w ? w->height() : 0};
	return r;
}

//static int mnubt[] = {
//	IDC_MNU_SCN, IDC_MNU_OPT, IDC_MNU_MOD,
//	IDC_MNU_VID, IDC_MNU_EXT, IDC_MNU_ABT
//};

//-----------------------------------------------------------------------------
// Name: LaunchpadDialog()
// Desc: This is the constructor for LaunchpadDialog
//-----------------------------------------------------------------------------
orbiter::LaunchpadDialog::LaunchpadDialog (Orbiter *app)
{
	hDlg    = NULL;
	hWait   = NULL;
	hInst   = app->GetInstance();
	pApp    = app;
	pCfg    = app->Cfg();
	g_pDlg  = this; // for nonmember callbacks
	CTab    = NULL;
	hTabContainer = NULL;
	timer   = NULL;
	m_bVisible = false;

	hDlgBrush = new QBrush (Colorref (dlgcol));
	hShadowImg = oapiLoadResImage (hInst, IDB_SHADOW);

}

//-----------------------------------------------------------------------------
// Name: ~LaunchpadDialog()
// Desc: This is the destructor for LaunchpadDialog
//-----------------------------------------------------------------------------
orbiter::LaunchpadDialog::~LaunchpadDialog ()
{
	for (auto tab : TabList)
		delete tab;
	TabList.clear();

	delete hWait;
	delete hDlg;
	delete hDlgBrush;
	delete hShadowImg;
}

//-----------------------------------------------------------------------------
// Name: Create()
// Desc: Creates the main application dialog
//-----------------------------------------------------------------------------
bool orbiter::LaunchpadDialog::Create (bool startvideotab)
{
	if (!hDlg) {
		QWidget *hWnd = oapiCreateResDialog (hInst, IDD_MAIN, NULL);
		if (!hWnd) return false;
		OnInitDialog (hWnd);
		hTabContainer = oapiResDlgItem(hDlg, IDC_MNU_PAGECONTAINER);
		AddTab (new ScenarioTab (this));
		AddTab(new OptionsTab(this));
		AddTab (new ModuleTab (this));
		AddTab (new DefVideoTab (this));
		AddTab (pExtra = new ExtraTab (this));
		AddTab (new AboutTab (this));
		InitTabControl (hDlg);
		InitSize (hDlg);
		SwitchTabPage (hDlg, 0);
		if (pCfg->CfgDemoPrm.bDemo) {
			SetDemoMode ();
			time0 = time (NULL);
			timer->start (1000);
		}
		Resize (hDlg, client0.right, client0.bottom, SIZE_RESTORED);
		if (pCfg->rLaunchpad.right > pCfg->rLaunchpad.left) {
			RECT lr = pCfg->rLaunchpad;
			int x = lr.left, y = lr.top, w = lr.right-lr.left, h = lr.bottom-lr.top;
			QRect dr = QApplication::primaryScreen()->virtualGeometry();
			x = min (max (x, dr.left()), dr.left()+dr.width()-w);
			y = min (max (y, dr.top()), dr.top()+dr.height()-h);
			// the stored rectangle is the outer window; the frame is added back by the window manager
			QSize frame = hDlg->frameGeometry().size() - hDlg->size();
			hDlg->move (x, y);
			hDlg->resize (w - frame.width(), h - frame.height());
		}
		oapiResDlgItem (hDlg, IDC_BLACKBOX)->setProperty ("text", SIG4 "  \n" SIG2 "  \n" SIG1AA "  \n" SIG1AB "  ");
		oapiResDlgItem (hDlg, IDC_VERSION)->setProperty ("text", SIG7);
		Show();
		if (startvideotab) {
			SwitchTabPage (hDlg, PG_VID);
		}
	} else
		SwitchTabPage (hDlg, PG_SCN);

	return (hDlg != NULL);
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadDialog::Show()
{
	hDlg->show();
	m_bVisible = true;
	for (auto tab : TabList)
		tab->LaunchpadShowing(true);
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadDialog::Hide()
{
	hDlg->hide();
	m_bVisible = false;
	for (auto tab : TabList)
		tab->LaunchpadShowing(false);
}

//-----------------------------------------------------------------------------

orbiter::LaunchpadTab* orbiter::LaunchpadDialog::GetTab(UINT i) const
{
	return (i < TabList.size() ? TabList[i] : nullptr);
}

//-----------------------------------------------------------------------------
// Name: AddTab()
// Desc: Inserts a new tab into the list
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::AddTab (LaunchpadTab *tab)
{
	TabList.push_back(tab);
}

//-----------------------------------------------------------------------------
// Name: InitTabControl()
// Desc: Sets up the tabs for the tab control interface
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::InitTabControl (QWidget *hWnd)
{
	for (auto tab : TabList) {
		tab->Create();
		tab->GetConfig (pCfg);
	}
	hWait = oapiCreateResDialog (hInst, IDD_PAGE_WAIT2, hWnd);
	WaitProc (hWait);
	hWait->hide();
}

//-----------------------------------------------------------------------------
// Name: EnableLaunchButton()
// Desc: Enable/disable "Launch Orbiter" button
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::EnableLaunchButton (bool enable) const
{
	oapiResDlgItem (hDlg, IDLAUNCH)->setEnabled (enable);
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadDialog::InitSize (QWidget *hWnd)
{
	RECT r, rl;
	client0 = ClientRect (hWnd);
	copyr0 = ClientRect (oapiResDlgItem (hWnd, IDC_BLACKBOX));
	r = ClientRect (oapiResDlgItem (hWnd, IDC_SHADOW));
	shadowh = r.bottom;
	r_launch0 = GetClientPos (hWnd, oapiResDlgItem (hWnd, IDLAUNCH));
	r_help0   = GetClientPos (hWnd, oapiResDlgItem (hWnd, 9));
	r_exit0   = GetClientPos (hWnd, oapiResDlgItem (hWnd, IDEXIT));
	r_wait0   = GetClientPos (hWnd, hWait);
	r_data0   = GetClientPos (hWnd, oapiResDlgItem(hWnd, IDC_MNU_PAGECONTAINER));
	r_version0= GetClientPos (hWnd, oapiResDlgItem (hWnd, IDC_VERSION));

	r = GetClientPos (hDlg, oapiResDlgItem (hDlg, IDC_MNU_SCN));
	int y0 = r.top;
	r = GetClientPos (hDlg, oapiResDlgItem (hDlg, IDC_MNU_OPT));
	dy_bt = r.top - y0;

	rl = ClientRect (oapiResDlgItem (hWnd, IDC_LOGO));
	if (rl.bottom != copyr0.bottom) {
		oapiResDlgItem (hWnd, IDC_LOGO)->resize (client0.right-copyr0.right, copyr0.bottom);
	}
}

//-----------------------------------------------------------------------------

BOOL orbiter::LaunchpadDialog::Resize (QWidget *hWnd, DWORD w, DWORD h, DWORD mode)
{
	if (mode == SIZE_MINIMIZED) return TRUE;

	int w4, h4;
	int dw = (int)w - (int)client0.right;   // width change compared to initial size
	int dh = (int)h - (int)client0.bottom;  // height change compared to initial size
	int xb1 = r_launch0.left, xb2, xb3;
	int bg = r_exit0.left - r_help0.right;  // button gap
	int wb1 = r_launch0.right-r_launch0.left;
	int wb2 = r_help0.right-r_help0.left;
	int wb3 = r_exit0.right-r_exit0.left;
	int ww = wb1+wb2+wb3;
	int wf = r_exit0.right-r_launch0.left+dw-2*bg;
	if (wf < ww) { // shrink buttons
		wb1 = (wb1*wf)/ww;
		wb2 = (wb2*wf)/ww;
		wb3 = (wb3*wf)/ww;
		xb2 = xb1 + wb1 + bg;
		xb3 = xb2 + wb2 + bg;
	} else {
		xb2 = r_help0.left + dw;
		xb3 = r_exit0.left + dw;
	}
	int bh = r_exit0.bottom - r_exit0.top;  // button height

	oapiResDlgItem (hWnd, IDC_BLACKBOX)->resize (copyr0.right + dw, copyr0.bottom);
	oapiResDlgItem (hWnd, IDC_SHADOW)->resize (w, shadowh);
	w4 = r_exit0.right - r_data0.left + dw;
	h4 = max ((LONG)10, r_data0.bottom - r_data0.top + dh);
	oapiResDlgItem (hWnd, IDLAUNCH)->setGeometry (xb1, r_launch0.top+dh, wb1, bh);
	oapiResDlgItem (hWnd, 9)->setGeometry (xb2, r_help0.top+dh, wb2, bh);
	oapiResDlgItem (hWnd, IDEXIT)->setGeometry (xb3, r_exit0.top+dh, wb3, bh);
	hWait->move ((w-(r_wait0.right-r_wait0.left))/2, max (r_wait0.top, r_wait0.top+((LONG)h-r_wait0.bottom)/2));
	oapiResDlgItem (hWnd, IDC_VERSION)->move (r_version0.left, r_version0.top+dh);
	DWORD tabAreaW = r_data0.right - r_data0.left + dw;
	DWORD tabAreaH = r_data0.bottom - r_data0.top + dh;
	oapiResDlgItem(hWnd, IDC_MNU_PAGECONTAINER)->resize (tabAreaW, tabAreaH);
	for (auto tab : TabList) {
		tab->TabAreaResized(tabAreaW, tabAreaH);
	}
	return FALSE;
}

//-----------------------------------------------------------------------------
// Name: SetDemoMode()
// Desc: Set launchpad controls into demo mode
//-----------------------------------------------------------------------------

void orbiter::LaunchpadDialog::SetDemoMode ()
{
	//EnableWindow (GetDlgItem (hDlg, IDC_MAINTAB), FALSE);
	//ShowWindow (GetDlgItem (hDlg, IDC_MAINTAB), FALSE);

	static int hide_mnu[] = {
		IDC_MNU_OPT, IDC_MNU_MOD,
		IDC_MNU_VID, IDC_MNU_EXT,
	};
	for (size_t i = 0; i < sizeof(hide_mnu)/sizeof(hide_mnu[0]); i++) oapiResDlgItem (hDlg, hide_mnu[i])->setEnabled (false);
	if (pCfg->CfgDemoPrm.bBlockExit) oapiResDlgItem (hDlg, IDEXIT)->setEnabled (false);
	hDlg->setMouseTracking (true); // WM_MOUSEMOVE resets the idle timer
}

//-----------------------------------------------------------------------------
// Name: UpdateConfig()
// Desc: Save current dialog settings in configuration
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::UpdateConfig ()
{
	for (auto tab : TabList)
		tab->SetConfig (pCfg);

	// get launchpad window geometry (if not minimised)
	if (!hDlg->isMinimized()) {
		QRect g = hDlg->frameGeometry();
		pCfg->rLaunchpad.left = g.left(), pCfg->rLaunchpad.top = g.top();
		pCfg->rLaunchpad.right = g.left()+g.width(), pCfg->rLaunchpad.bottom = g.top()+g.height();
	}
}

//-----------------------------------------------------------------------------
// Name: OnInitDialog()
// Desc: WM_INITDIALOG of the main dialog: hooks its events and connects its controls
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::OnInitDialog (QWidget *hWnd)
{
	hDlg = hWnd;

	// WM_COMMAND
	static const int cmd[] = {IDLAUNCH, IDEXIT, IDHELP, IDC_MNU_SCN, IDC_MNU_OPT, IDC_MNU_MOD, IDC_MNU_VID, IDC_MNU_EXT, IDC_MNU_ABT};
	for (int id : cmd)
		if (QPushButton *b = DlgItem<QPushButton> (hWnd, id))
			QObject::connect (b, &QPushButton::clicked, hWnd, [this, id]() { OnCommand (id); });

	// window events, and the owner-drawn shadow bar and page container (WM_DRAWITEM)
	EventHook *hook = new EventHook (hWnd, [this](QObject *obj, QEvent *event) { return DlgProc (obj, event); });
	hook->Attach (oapiResDlgItem (hWnd, IDC_SHADOW));
	hook->Attach (oapiResDlgItem (hWnd, IDC_MNU_PAGECONTAINER));

	// WM_CTLCOLORSTATIC: light text on black for the copyright box
	QWidget *bb = oapiResDlgItem (hWnd, IDC_BLACKBOX);
	QPalette pal = bb->palette();
	pal.setColor (QPalette::WindowText, Colorref (0xF0B0B0));
	pal.setColor (QPalette::Window, Qt::black);
	bb->setPalette (pal);
	bb->setAutoFillBackground (true);

	// WM_GETMINMAXINFO
	hWnd->setMinimumSize (550, 350);

	// WM_TIMER: demo mode auto-launch
	timer = new QTimer (hWnd);
	QObject::connect (timer, &QTimer::timeout, hWnd, [this]() {
		if (difftime (time (NULL), time0) > pCfg->CfgDemoPrm.LPIdleTime) { // auto-launch a demo
			if (SelectDemoScenario ())
				QMetaObject::invokeMethod (hDlg, [this]() { OnCommand (IDLAUNCH); }, Qt::QueuedConnection);
		}
	});
}

//-----------------------------------------------------------------------------
// Name: OnCommand()
// Desc: WM_COMMAND of the main dialog's buttons
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::OnCommand (int id)
{
	char cbuf[256];

	switch (id) {
	case IDLAUNCH:
		if (((ScenarioTab*)TabList[0])->GetSelScenario (cbuf, 256) == 1) {
			UpdateConfig ();
			pApp->Launch (cbuf);
		}
		return;
	case IDEXIT:
		QMetaObject::invokeMethod (hDlg, "close", Qt::QueuedConnection);
		return;
	case IDHELP:
		if (CTab) CTab->OpenHelp ();
		return;
	case IDC_MNU_SCN:
		SwitchTabPage (hDlg, PG_SCN);
		return;
	case IDC_MNU_OPT:
		SwitchTabPage(hDlg, PG_OPT);
		return;
	case IDC_MNU_MOD:
		SwitchTabPage (hDlg, PG_MOD);
		return;
	case IDC_MNU_VID:
		SwitchTabPage (hDlg, PG_VID);
		return;
	case IDC_MNU_EXT:
		SwitchTabPage (hDlg, PG_EXT);
		return;
	case IDC_MNU_ABT:
		SwitchTabPage (hDlg, PG_ABT);
		return;
	}
}

//-----------------------------------------------------------------------------
// Name: DlgProc()
// Desc: Event callback function for main dialog
//-----------------------------------------------------------------------------
bool orbiter::LaunchpadDialog::DlgProc (QObject *obj, QEvent *event)
{
	if (obj == hDlg) {
		switch (event->type()) {
		case QEvent::Close:
			if (pCfg->CfgDemoPrm.bBlockExit) {
				event->ignore();
				return true;
			}
			UpdateConfig ();
			// WM_DESTROY
			if (pCfg->CfgDemoPrm.bDemo && timer->isActive())
				timer->stop();
			QCoreApplication::quit(); // PostQuitMessage
			return false;
		case QEvent::Resize: {
			QSize s = static_cast<QResizeEvent*> (event)->size();
			Resize (hDlg, s.width(), s.height(), hDlg->isMinimized() ? SIZE_MINIMIZED : SIZE_RESTORED);
			} return false;
		case QEvent::Show:
		case QEvent::Hide:
			if (pCfg->CfgDemoPrm.bDemo) {
				if (event->type() == QEvent::Show) {
					time0 = time (NULL);
					if (!timer->isActive()) timer->start (1000);
				} else {
					if (timer->isActive()) timer->stop();
				}
			}
			return false;
		case QEvent::MouseMove:
			if (pCfg->CfgDemoPrm.bDemo) time0 = time(NULL); // reset timer
			return false;
		case QEvent::KeyPress:
			if (pCfg->CfgDemoPrm.bDemo) time0 = time(NULL); // reset timer
			// Escape is IDCANCEL, which the Launchpad ignores (QDialog would hide itself)
			return static_cast<QKeyEvent*> (event)->key() == Qt::Key_Escape;
		default:
			return false;
		}
	}
	if (event->type() == QEvent::Paint) {
		QWidget *w = static_cast<QWidget*> (obj);
		if (obj == oapiResDlgItem (hDlg, IDC_SHADOW)) {
			QPainter p (w);
			if (hShadowImg)
				p.drawImage (QRect (0, 0, w->width(), w->height()), *hShadowImg, QRect (0, 0, 8, 8));
			return true;
		}
		else if (obj == oapiResDlgItem (hDlg, IDC_MNU_PAGECONTAINER)) {
			QPainter p (w);
			p.fillRect (w->rect(), w->palette().color (QPalette::Button)); // COLOR_3DFACE
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------
// Name: WaitProc()
// Desc: Set-up of the wait page (WM_INITDIALOG and colours)
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::WaitProc (QWidget *hWnd)
{
	QProgressBar *pb = DlgItem<QProgressBar> (hWnd, IDC_PROGRESS1);
	if (pb) pb->setRange (0, 1000);

	// WM_CTLCOLORDLG / WM_CTLCOLORSTATIC: dialog brush behind a transparent wait text
	QPalette pal = hWnd->palette();
	pal.setBrush (QPalette::Window, *hDlgBrush);
	hWnd->setPalette (pal);
	hWnd->setAutoFillBackground (true);
	if (QWidget *wt = oapiResDlgItem (hWnd, IDC_WAITTEXT))
		wt->setAutoFillBackground (false);
}

//-----------------------------------------------------------------------------
// Name: SwitchTabPage()
// Desc: Display a new page
//-----------------------------------------------------------------------------
void orbiter::LaunchpadDialog::SwitchTabPage (QWidget *hWnd, int cpg)
{
	for (size_t pg = 0; pg < TabList.size(); pg++)
		if (pg != (size_t)cpg) TabList[pg]->Hide();
	CTab = (cpg >= 0 && cpg < (int)TabList.size() ? TabList[cpg] : nullptr);
	if (CTab) CTab->Show();
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadDialog::ShowWaitPage (bool show, long mem_committed)
{
	size_t i;
	int item[3] = {IDLAUNCH, 9, IDEXIT};

	if (show) {
		for (i = 0; i < 3; i++)
			oapiResDlgItem(hDlg, item[i])->hide();
	}
	if (show) {
		QApplication::setOverrideCursor (Qt::WaitCursor);
		for (auto tab : TabList)
			tab->Hide();
		mem_wait = mem_committed/1000;
		mem0 = pApp->memstat->HeapUsage();
		QProgressBar *pb = DlgItem<QProgressBar> (hWait, IDC_PROGRESS1);
		pb->setValue (0);
		pb->setVisible (mem_wait != 0);
		hWait->show();
	} else {
		QApplication::restoreOverrideCursor();
		hWait->hide();
		SwitchTabPage (hDlg, 0);
	}
	if (!show)
		for (i = 0; i < 3; i++)
			oapiResDlgItem (hDlg, item[i])->show();

	hDlg->repaint(); // RDW_UPDATENOW: the caller keeps the event loop busy
}

void orbiter::LaunchpadDialog::UpdateWaitProgress ()
{
	if (mem_wait) {
		long mem = pApp->memstat->HeapUsage();
		DlgItem<QProgressBar> (hWait, IDC_PROGRESS1)->setValue ((mem0-mem)/mem_wait);
	}
}

//-----------------------------------------------------------------------------
// Name: GetDemoScenario()
// Desc: returns the name of an arbitrary scenario in the demo folder
//-----------------------------------------------------------------------------
int orbiter::LaunchpadDialog::SelectDemoScenario ()
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (GetTab(PG_SCN)->TabWnd(), IDC_SCN_LIST);
	QTreeWidgetItem *demo = NULL;
	for (int i = 0; i < hTree->topLevelItemCount(); i++) {
		QTreeWidgetItem *it = hTree->topLevelItem (i);
		if (!strcasecmp (it->text (0).toUtf8().constData(), "Demo")) {
			demo = it;
			break;
		}
	}
	if (!demo) return 0;

	int seldemo, ndemo = 0;
	for (int i = 0; i < demo->childCount(); i++)
		if (!demo->child (i)->childCount()) ndemo++;
	if (!ndemo) return 0;
	seldemo = (int)(((double)rand()*ndemo)/((double)RAND_MAX+1.0)); // glibc RAND_MAX is 2^31-1: no int product
	ndemo = 0;
	for (int i = 0; i < demo->childCount(); i++) {
		QTreeWidgetItem *it = demo->child (i);
		if (!it->childCount()) {
			if (ndemo == seldemo) {
				hTree->setCurrentItem (it);
				return 1;
			}
			ndemo++;
		}
	}
	return 0;
}

// ****************************************************************************
// "Extra Parameters" page
// ****************************************************************************

static const char *desc_fixedstep = "Force Orbiter to advance the simulation by a fixed time interval in each frame.";

void OpenDynamics (void*, QWidget*);

QTreeWidgetItem *orbiter::LaunchpadDialog::RegisterExtraParam (LaunchpadItem *item, QTreeWidgetItem *parent)
{
	return pExtra->RegisterExtraParam (item, parent);
}

bool orbiter::LaunchpadDialog::UnregisterExtraParam (LaunchpadItem *item)
{
	return pExtra->UnregisterExtraParam (item);
}

QTreeWidgetItem *orbiter::LaunchpadDialog::FindExtraParam (const char *name, QTreeWidgetItem *parent)
{
	return pExtra->FindExtraParam (name, parent);
}

void orbiter::LaunchpadDialog::WriteExtraParams ()
{
	pExtra->WriteExtraParams ();
}
