// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// ExtraTab class
//=============================================================================

#define OAPI_IMPLEMENTATION

#include "Launchpad.h"
#include "TabExtra.h"
#include "Orbiter.h"
#include "Rigidbody.h"
#include "Log.h"
#include "Help.h"
#include "resource.h"
#include "resource2.h"
#include "ResDialog.h"
#include "Util.h"
#include <QAbstractButton>
#include <QComboBox>
#include <QDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <strings.h>

// BM_SETCHECK / BM_GETCHECK / EnableWindow / ShowWindow on a dialog control
static void SetCheck (QWidget *hDlg, int id, bool check)
{
	if (QAbstractButton *b = DlgItem<QAbstractButton> (hDlg, id)) b->setChecked (check);
}

static bool IsChecked (QWidget *hDlg, int id)
{
	QAbstractButton *b = DlgItem<QAbstractButton> (hDlg, id);
	return (b && b->isChecked());
}

static void EnableItem (QWidget *hDlg, int id, bool enable)
{
	if (QWidget *w = oapiResDlgItem (hDlg, id)) w->setEnabled (enable);
}

static void ShowItem (QWidget *hDlg, int id, bool show)
{
	if (QWidget *w = oapiResDlgItem (hDlg, id)) w->setVisible (show);
}

using std::max;

extern Orbiter *g_pOrbiter;

//-----------------------------------------------------------------------------
// ExtraTab class

orbiter::ExtraTab::ExtraTab (const LaunchpadDialog *lp): LaunchpadTab (lp)
{
	m_internalPrm = 0;
}

//-----------------------------------------------------------------------------

orbiter::ExtraTab::~ExtraTab ()
{
	// at this point, only the internally created entries should be left
	// so they should be safe to delete
	if (m_ExtPrm.size() > m_internalPrm)
		LOGOUT_WARN("Orphaned Launchpad Extra entries: %d. Some plugins may not have un-registered their entries.",
			m_ExtPrm.size() - m_internalPrm);
	for (int i = 0; i < m_ExtPrm.size(); i++)
		delete m_ExtPrm[i];
}

//-----------------------------------------------------------------------------

void orbiter::ExtraTab::Create ()
{
	hTab = CreateTab (IDD_PAGE_EXT);

	r_lst0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_EXT_LIST));  // REMOVE!
	r_dsc0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_EXT_TEXT));  // REMOVE!
	r_pane  = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_EXT_SPLIT1));
	r_edit0 = GetClientPos (hTab, oapiResDlgItem (hTab, IDC_EXT_OPEN));
	splitListDesc.SetHwnd (oapiResDlgItem (hTab, IDC_EXT_SPLIT1), oapiResDlgItem (hTab, IDC_EXT_LIST), oapiResDlgItem (hTab, IDC_EXT_TEXT));
}

//-----------------------------------------------------------------------------

BOOL orbiter::ExtraTab::OnInitDialog (QWidget *hWnd)
{
	QTreeWidget *hTree = DlgItem<QTreeWidget> (hWnd, IDC_EXT_LIST);
	// WM_NOTIFY
	QObject::connect (hTree, &QTreeWidget::currentItemChanged, hWnd, [hWnd](QTreeWidgetItem *itemNew) {
		// TVN_SELCHANGED
		if (!itemNew) return;
		LaunchpadItem* func = (LaunchpadItem*)itemNew->data (0, Qt::UserRole).value<void*>();
		char* desc = func->Description();
		if (desc) oapiSetDlgItemText(hWnd, IDC_EXT_TEXT, desc);
		else oapiSetDlgItemText(hWnd, IDC_EXT_TEXT, "");
	});
	QObject::connect (hTree, &QTreeWidget::itemDoubleClicked, hWnd, [this, hTree]() {
		// NM_DBLCLK
		QTreeWidgetItem *it = hTree->currentItem();
		BuiltinLaunchpadItem* func = (it ? (BuiltinLaunchpadItem*)it->data (0, Qt::UserRole).value<void*>() : NULL);
		if (func) func->clbkOpen(LaunchpadWnd());
	});
	// WM_COMMAND
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_EXT_OPEN), &QPushButton::clicked, hWnd, [this, hTree]() {
		QTreeWidgetItem *it = hTree->currentItem();
		BuiltinLaunchpadItem *func = (it ? (BuiltinLaunchpadItem*)it->data (0, Qt::UserRole).value<void*>() : NULL);
		if (func) func->clbkOpen (LaunchpadWnd());
	});
	return FALSE;
}

//-----------------------------------------------------------------------------

void orbiter::ExtraTab::GetConfig (const Config *cfg)
{
	QTreeWidgetItem *ht;
	ht = RegisterExtraParam(new ExtraPropagation(this), NULL); TRACENEW
	RegisterExtraParam(new ExtraDynamics(this), ht); TRACENEW
	RegisterExtraParam(new ExtraStabilisation(this), ht); TRACENEW
	ht = RegisterExtraParam(new ExtraInstruments(this), NULL); TRACENEW
	RegisterExtraParam(new ExtraMfdConfig(this), ht); TRACENEW
	RegisterExtraParam(new ExtraVesselConfig(this), NULL); TRACENEW
	RegisterExtraParam(new ExtraPlanetConfig(this), NULL); TRACENEW
	ht = RegisterExtraParam(new ExtraDebug(this), NULL); TRACENEW
	RegisterExtraParam(new ExtraShutdown(this), ht); TRACENEW
	RegisterExtraParam(new ExtraFixedStep(this), ht); TRACENEW
	RegisterExtraParam(new ExtraRenderingOptions(this), ht); TRACENEW
	RegisterExtraParam(new ExtraLaunchpadOptions(this), ht); TRACENEW
	RegisterExtraParam(new ExtraLogfileOptions(this), ht); TRACENEW
	RegisterExtraParam(new ExtraPerformanceSettings(this), ht); TRACENEW
	m_internalPrm = m_ExtPrm.size();
	oapiSetDlgItemText(hTab, IDC_EXT_TEXT, "Advanced and addon-specific configuration parameters.\r\n\r\nClick on an item to get a description.\r\n\r\nDouble-click to open or expand.");
	int listw = cfg->CfgWindowPos.LaunchpadExtListWidth;
	if (!listw) {
		listw = oapiResDlgItem (hTab, IDC_EXT_LIST)->width();
	}
	splitListDesc.SetStaticPane (SplitterCtrl::PANE1, listw);
}

//-----------------------------------------------------------------------------

void orbiter::ExtraTab::SetConfig (Config *cfg)
{
	cfg->CfgWindowPos.LaunchpadExtListWidth = splitListDesc.GetPaneWidth (SplitterCtrl::PANE1);
}

//-----------------------------------------------------------------------------

bool orbiter::ExtraTab::OpenHelp ()
{
	OpenTabHelp ("tab_extra");
	return true;
}

//-----------------------------------------------------------------------------

BOOL orbiter::ExtraTab::OnSize (int w, int h)
{
	int dw = w - (int)(pos0.right-pos0.left);
	int dh = h - (int)(pos0.bottom-pos0.top);
	int w0 = r_pane.right - r_pane.left; // initial splitter pane width
	int h0 = r_pane.bottom - r_pane.top; // initial splitter pane height

	// the elements below may need updating
	int lstw0 = r_lst0.right-r_lst0.left;
	int lsth0 = r_lst0.bottom-r_lst0.top;
	int dscw0 = r_dsc0.right-r_dsc0.left;
	int wg  = r_dsc0.right - r_lst0.left - lstw0 - dscw0;  // gap width
	int wl  = lstw0 + (dw*lstw0)/(lstw0+dscw0);
	wl = max (wl, lstw0/2);
	int xr = r_lst0.left+wl+wg;
	int wr = max(10,lstw0+dscw0+dw-wl);

	///SetWindowPos (GetDlgItem (hTab, IDC_EXT_LIST), NULL,
	//	0, 0, wl, lsth0+dh,
	//	SWP_NOACTIVATE|SWP_NOMOVE|SWP_NOOWNERZORDER|SWP_NOZORDER);
	//SetWindowPos (GetDlgItem (hTab, IDC_EXT_TEXT), NULL,
	//	xr, r_dsc0.top, wr, lsth0+dh,
	//	SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER);
	oapiResDlgItem (hTab, IDC_EXT_SPLIT1)->resize (w0+dw, h0+dh);
	oapiResDlgItem (hTab, IDC_EXT_OPEN)->move (r_edit0.left, r_edit0.top+dh);

	return FALSE;
}

//-----------------------------------------------------------------------------

QTreeWidgetItem *orbiter::ExtraTab::RegisterExtraParam (LaunchpadItem *item, QTreeWidgetItem *parent)
{
	// first check that the item doesn't already exist
	QTreeWidgetItem *hti = FindExtraParam (item->Name(), parent);
	if (hti) return hti;

	// add extra parameter instance to list
	m_ExtPrm.push_back(item);

	// if a name is provided, add item to tree list
	char *name = item->Name();
	if (name) {
		hti = new QTreeWidgetItem();
		hti->setText (0, QString::fromUtf8 (name));
		hti->setData (0, Qt::UserRole, QVariant::fromValue ((void*)item));
		if (parent) parent->addChild (hti); // TVI_LAST
		else DlgItem<QTreeWidget> (hTab, IDC_EXT_LIST)->addTopLevelItem (hti);
	} else hti = 0;
	item->hItem = (LAUNCHPADITEM_HANDLE)hti;
	return hti;
}

//-----------------------------------------------------------------------------

bool orbiter::ExtraTab::UnregisterExtraParam (LaunchpadItem *item)
{
	for (auto it = m_ExtPrm.begin(); it != m_ExtPrm.end(); it++) {
		if (*it == item) {
			delete (QTreeWidgetItem*)item->hItem; // remove entry from UI
			item->clbkWriteConfig(); // allow item to save state before removing
			m_ExtPrm.erase(it);        // delete the container - the actual item has to be deleted by the caller
			return true;
		}
	}
	return false;
}

//-----------------------------------------------------------------------------

QTreeWidgetItem *orbiter::ExtraTab::FindExtraParam (const char *name, QTreeWidgetItem *parent)
{
	QTreeWidgetItem *hti = FindExtraParamChild (parent);
	if (!name) return hti; // no name given - return first child

	QTreeWidget *hCtrl = DlgItem<QTreeWidget> (hTab, IDC_EXT_LIST);
	int n = (parent ? parent->childCount() : hCtrl->topLevelItemCount());

	// step through the list
	for (int i = 0; i < n; i++) {
		hti = (parent ? parent->child (i) : hCtrl->topLevelItem (i));
		if (!strcasecmp (name, hti->text (0).toUtf8().constData())) return hti;
	}

	return 0;
}

//-----------------------------------------------------------------------------

QTreeWidgetItem *orbiter::ExtraTab::FindExtraParamChild (QTreeWidgetItem *parent)
{
	QTreeWidget *hCtrl = DlgItem<QTreeWidget> (hTab, IDC_EXT_LIST);
	if (parent) return parent->child (0);
	else        return hCtrl->topLevelItem (0);
}

//-----------------------------------------------------------------------------

void orbiter::ExtraTab::WriteExtraParams ()
{
	for (auto it = m_ExtPrm.begin(); it != m_ExtPrm.end(); it++)
		(*it)->clbkWriteConfig();
}

//-----------------------------------------------------------------------------

// WM_NOTIFY (TVN_SELCHANGED, NM_DBLCLK) and WM_COMMAND (IDC_EXT_OPEN) are connected in OnInitDialog


// ****************************************************************************
// ****************************************************************************

//-----------------------------------------------------------------------------
// Additional functions (under the "Extra" tab)
//-----------------------------------------------------------------------------

BuiltinLaunchpadItem::BuiltinLaunchpadItem (const orbiter::ExtraTab *tab): LaunchpadItem ()
{
	pTab = tab;
}

bool BuiltinLaunchpadItem::OpenDialog (QWidget *hParent, int resid, DLGINIT pDlg)
{
	return LaunchpadItem::OpenDialog (pTab->AppInstance(), hParent, resid, pDlg);
}

void BuiltinLaunchpadItem::Error (const char *msg)
{
	QMessageBox::critical (pTab->LaunchpadWnd(), "Orbiter configuration error", msg);
}

void BuiltinLaunchpadItem::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	hWnd->setProperty ("LaunchpadItem", QVariant::fromValue (context)); // DWLP_USER
	// WM_COMMAND IDCANCEL (WM_CLOSE: closing a QDialog rejects it, which ends it with 0)
	if (QPushButton *b = DlgItem<QPushButton> (hWnd, IDCANCEL))
		QObject::connect (b, &QPushButton::clicked, hWnd, [hWnd]() { qobject_cast<QDialog*> (hWnd)->done (0); });
}

//-----------------------------------------------------------------------------
// Physics engine
//-----------------------------------------------------------------------------

char *ExtraPropagation::Name ()
{
	return (char*)"Time propagation";
}

char *ExtraPropagation::Description ()
{
	return (char*)"Select and configure the time propagation methods Orbiter uses to update vessel positions and velocities from one time frame to the next.";
}

//-----------------------------------------------------------------------------
// Physics engine: Parameters for dynamic state propagation
//-----------------------------------------------------------------------------

int ExtraDynamics::PropId[NPROP_METHOD] = {
	PROP_RK2, PROP_RK4, PROP_RK5, PROP_RK6, PROP_RK7, PROP_RK8,
	PROP_SY2, PROP_SY4, PROP_SY6, PROP_SY8
};

char *ExtraDynamics::Name ()
{
	return (char*)"Dynamic state propagators";
}

char *ExtraDynamics::Description ()
{
	return (char*)"Select the numerical integration methods used for dynamic state updates.\r\n\r\nState propagators affect the accuracy and stability of spacecraft orbits and trajectory calculations.";
}

bool ExtraDynamics::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_DYNAMICS, DlgProc);
	return true;
}

void ExtraDynamics::InitDialog (QWidget *hWnd)
{
	DWORD i, j;
	for (i = 0; i < 5; i++) {
		DlgItem<QComboBox>(hWnd, IDC_PROP_PROP0+i)->clear();
		for (j = 0; j < NPROP_METHOD; j++)
			oapiComboAddString(DlgItem<QComboBox>(hWnd, IDC_PROP_PROP0+i), RigidBody::PropagatorStr(j));
	}
	SetDialog (hWnd, pTab->Cfg()->CfgPhysicsPrm);
}

void ExtraDynamics::ResetDialog (QWidget *hWnd)
{
	extern CFG_PHYSICSPRM CfgPhysicsPrm_default;
	SetDialog (hWnd, CfgPhysicsPrm_default);
}

void ExtraDynamics::SetDialog (QWidget *hWnd, const CFG_PHYSICSPRM &prm)
{
	char cbuf[64];
	int i, j;
	int n = prm.nLPropLevel;
	for (i = 0; i < 5; i++) {
		SetCheck(hWnd, IDC_PROP_ACTIVE0+i, i < n);
		EnableItem(hWnd, IDC_PROP_ACTIVE0+i, i < n-1 || i > n || i == 0 ? FALSE : TRUE);
		ShowItem(hWnd, IDC_PROP_PROP0+i, i < n);
		ShowItem(hWnd, IDC_PROP_TTGT0+i, i < n);
		ShowItem(hWnd, IDC_PROP_ATGT0+i, i < n);
		if (i < 4) {
			ShowItem(hWnd, IDC_PROP_TLIMIT01+i, i < n-1);
			ShowItem(hWnd, IDC_PROP_ALIMIT01+i, i < n-1);
		}
		if (i < n) {
			int id = prm.PropMode[i];
			for (j = 0; j < NPROP_METHOD; j++)
				if (id == PropId[j]) {
					DlgItem<QComboBox>(hWnd, IDC_PROP_PROP0+i)->setCurrentIndex(j);
					break;
				}
			sprintf (cbuf, "%0.2f", prm.PropTTgt[i]);
			oapiSetDlgItemText(hWnd, IDC_PROP_TTGT0+i, cbuf);
			sprintf (cbuf, "%0.1f", prm.PropATgt[i]*DEG);
			oapiSetDlgItemText(hWnd, IDC_PROP_ATGT0+i, cbuf);
			if (i < n-1) {
				sprintf (cbuf, "%0.2f", prm.PropTLim[i]);
				oapiSetDlgItemText(hWnd, IDC_PROP_TLIMIT01+i, cbuf);
				sprintf (cbuf, "%0.1f", prm.PropALim[i]*DEG);
				oapiSetDlgItemText(hWnd, IDC_PROP_ALIMIT01+i, cbuf);
			}
		}
	}
	sprintf (cbuf, "%d", prm.PropSubMax);
	oapiSetDlgItemText(hWnd, IDC_PROP_MAXSAMPLE, cbuf);
}

void ExtraDynamics::Activate (QWidget *hWnd, int which)
{
	int i = which-IDC_PROP_ACTIVE0;
	bool check = IsChecked (hWnd, which);
	if (check) {
		if (i < 4) EnableItem(hWnd, which+1, TRUE);
		if (i > 0) EnableItem(hWnd, which-1, FALSE);
		ShowItem(hWnd, IDC_PROP_PROP0+i, true);
		ShowItem(hWnd, IDC_PROP_TTGT0+i, true);
		ShowItem(hWnd, IDC_PROP_ATGT0+i, true);
		if (i > 0) {
			ShowItem(hWnd, IDC_PROP_TLIMIT01+i-1, true);
			ShowItem(hWnd, IDC_PROP_ALIMIT01+i-1, true);
		}
	} else {
		if (i > 1) EnableItem(hWnd, which-1, TRUE);
		if (i < 4) EnableItem(hWnd, which+1, FALSE);
		ShowItem(hWnd, IDC_PROP_PROP0+i, false);
		ShowItem(hWnd, IDC_PROP_TTGT0+i, false);
		ShowItem(hWnd, IDC_PROP_ATGT0+i, false);
		if (i > 0) {
			ShowItem(hWnd, IDC_PROP_TLIMIT01+i-1, false);
			ShowItem(hWnd, IDC_PROP_ALIMIT01+i-1, false);
		}
	}
}

bool ExtraDynamics::StoreParams (QWidget *hWnd)
{
	char cbuf[256];
	int i, n = 0;
	double ttgt[MAX_PROP_LEVEL], atgt[MAX_PROP_LEVEL], tlim[MAX_PROP_LEVEL], alim[MAX_PROP_LEVEL];
	int mode[MAX_PROP_LEVEL];
	for (i = 0; i < MAX_PROP_LEVEL; i++) {
		if (IsChecked(hWnd, IDC_PROP_ACTIVE0+i))
			n++;
	}
	for (i = 0; i < n; i++) {
		mode[i] = DlgItem<QComboBox>(hWnd, IDC_PROP_PROP0+i)->currentIndex();
		if (mode[i] == -1) { // CB_ERR
			sprintf (cbuf, "Invalid propagator for integration stage %d.", i+1);
			Error (cbuf);
			return false;
		}
		oapiGetDlgItemText(hWnd, IDC_PROP_TTGT0+i, cbuf, 256);
		if ((sscanf (cbuf, "%lf", ttgt+i) != 1) || (ttgt[i] <= 0)) {
			sprintf (cbuf, "Invalid time step target for integration stage %d.", i+1);
			Error (cbuf);
			return false;
		}
		oapiGetDlgItemText(hWnd, IDC_PROP_ATGT0+i, cbuf, 256);
		if ((sscanf (cbuf, "%lf", atgt+i) != 1) || (atgt[i] <= 0)) {
			sprintf (cbuf, "Invalid angle step target for integration stage %d.", i+1);
			Error (cbuf);
			return false;
		}
		if (i < n-1) {
			oapiGetDlgItemText(hWnd, IDC_PROP_TLIMIT01+i, cbuf, 256);
			if ((sscanf (cbuf, "%lf", tlim+i) != 1) || (tlim[i] <= 0)) {
				sprintf (cbuf, "Invalid time step limit for integration stage %d -> %d.", i+1, i+2);
				Error (cbuf);
				return false;
			}
			oapiGetDlgItemText(hWnd, IDC_PROP_ALIMIT01+i, cbuf, 256);
			if ((sscanf (cbuf, "%lf", alim+i) != 1) || (alim[i] <= 0)) {
				sprintf (cbuf, "Invalid angle step limit for integration stage %d -> %d.", i+1, i+2);
				Error (cbuf);
				return false;
			}
			if (i > 0 && (tlim[i] <= tlim[i-1] || alim[i] <= alim[i-1])) {
				Error ("Step limits must be in ascending order");
				return false;
			}
		}
	}

	Config *cfg = pTab->Cfg();
	cfg->CfgPhysicsPrm.nLPropLevel = n;
	for (i = 0; i < n; i++) {
		cfg->CfgPhysicsPrm.PropMode[i] = PropId[mode[i]];
		cfg->CfgPhysicsPrm.PropTTgt[i] = ttgt[i];
		cfg->CfgPhysicsPrm.PropATgt[i] = atgt[i]*RAD;
		cfg->CfgPhysicsPrm.PropTLim[i] = (i < n-1 ? tlim[i] : 1e10);
		cfg->CfgPhysicsPrm.PropALim[i] = (i < n-1 ? alim[i]*RAD : 1e10);
	}

	oapiGetDlgItemText(hWnd, IDC_PROP_MAXSAMPLE, cbuf, 256);
	if ((sscanf (cbuf, "%d", &i) != 1) || i < 1) {
		Error ("Invalid value for max. subsamples (integer value >= 1 required).");
		return false;
	} else {
		cfg->CfgPhysicsPrm.PropSubMax = i;
	}

	return true;
}

bool ExtraDynamics::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, "extra_linprop");
	return true;
}

void ExtraDynamics::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraDynamics*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_PROP_ACTIVE0:
		case IDC_PROP_ACTIVE1:
		case IDC_PROP_ACTIVE2:
		case IDC_PROP_ACTIVE3:
		case IDC_PROP_ACTIVE4:
			((ExtraDynamics*)context)->Activate (hWnd, id);
			break;
		case IDC_RESET:
			((ExtraDynamics*)context)->ResetDialog (hWnd);
			return;
		case IDCHELP:
			((ExtraDynamics*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraDynamics*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}

//-----------------------------------------------------------------------------
// Physics engine: Parameters for angular state propagation
//-----------------------------------------------------------------------------
#ifdef UNDEF

int ExtraAngDynamics::PropId[NAPROP_METHOD] = {
	PROP_RK2, PROP_RK4, PROP_RK5, PROP_RK6, PROP_RK7, PROP_RK8
};

char *ExtraAngDynamics::Name ()
{
	return "Angular state propagators";
}

char *ExtraAngDynamics::Description ()
{
	static char *desc = "Select the numerical integration method for dynamic angular state updates.\r\n\r\nAngular propagators affect the simulation accuracy and stability of rotating vessels.";
	return desc;
}

bool ExtraAngDynamics::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_ADYNAMICS, DlgProc);
	return true;
}

void ExtraAngDynamics::InitDialog (QWidget *hWnd)
{
	static char *label[NAPROP_METHOD] = {
		"Runge-Kutta, 2nd order (RK2)", "Runge-Kutta, 4th order (RK4)", "Runge-Kutta, 5th order (RK5)",
		"Runge-Kutta, 6th order (RK6)", "Runge-Kutta, 7th order (RK7)", "Runge-Kutta, 8th order (RK8)"
	};

	int i, j;
	for (i = 0; i < 5; i++) {
		DlgItem<QComboBox>(hWnd, IDC_COMBO1+i)->clear();
		for (j = 0; j < NAPROP_METHOD; j++)
			oapiComboAddString(DlgItem<QComboBox>(hWnd, IDC_COMBO1+i), label[j]);
	}
	SetDialog (hWnd, pTab->Cfg()->CfgPhysicsPrm);
}

void ExtraAngDynamics::ResetDialog (QWidget *hWnd)
{
	extern CFG_PHYSICSPRM CfgPhysicsPrm_default;
	SetDialog (hWnd, CfgPhysicsPrm_default);
}

void ExtraAngDynamics::SetDialog (QWidget *hWnd, const CFG_PHYSICSPRM &prm)
{
	char cbuf[64];
	int i, j;
	int n = prm.nAPropLevel;
	for (i = 0; i < 5; i++) {
		SetCheck(hWnd, IDC_CHECK1+i, i < n);
		EnableItem(hWnd, IDC_CHECK1+i, i < n-1 || i > n || i == 0 ? FALSE : TRUE);
		ShowItem(hWnd, IDC_COMBO1+i, i < n);
		if (i < 4) {
			ShowItem(hWnd, IDC_EDIT1+i, i < n-1);
			ShowItem(hWnd, IDC_EDIT5+i, i < n-1);
		}
		if (i < n) {
			int id = prm.APropMode[i];
			for (j = 0; j < NAPROP_METHOD; j++)
				if (id == PropId[j]) {
					DlgItem<QComboBox>(hWnd, IDC_COMBO1+i)->setCurrentIndex(j);
					break;
				}
			if (i < n-1) {
				sprintf (cbuf, "%0.2f", prm.APropTLimit[i]);
				oapiSetDlgItemText(hWnd, IDC_EDIT1+i, cbuf);
				sprintf (cbuf, "%0.1f", prm.PropALimit[i]*DEG);
				oapiSetDlgItemText(hWnd, IDC_EDIT5+i, cbuf);
			}
		}
	}
	sprintf (cbuf, "%0.1f", prm.APropSubLimit*DEG);
	oapiSetDlgItemText(hWnd, IDC_EDIT9, cbuf);
	sprintf (cbuf, "%d", prm.APropSubMax);
	oapiSetDlgItemText(hWnd, IDC_EDIT10, cbuf);
	sprintf (cbuf, "%0.1f", prm.APropCouplingLimit*DEG);
	oapiSetDlgItemText(hWnd, IDC_EDIT11, cbuf);
	sprintf (cbuf, "%0.1f", prm.APropTorqueLimit*DEG);
	oapiSetDlgItemText(hWnd, IDC_EDIT12, cbuf);
}

void ExtraAngDynamics::Activate (QWidget *hWnd, int which)
{
	int i = which-IDC_CHECK1;
	bool check = IsChecked (hWnd, which);
	if (check) {
		if (i < 4) EnableItem(hWnd, which+1, TRUE);
		if (i > 0) EnableItem(hWnd, which-1, FALSE);
		ShowItem(hWnd, IDC_COMBO1+i, true);
		if (i > 0) {
			ShowItem(hWnd, IDC_EDIT1+i-1, true);
			ShowItem(hWnd, IDC_EDIT5+i-1, true);
		}
	} else {
		if (i > 1) EnableItem(hWnd, which-1, TRUE);
		if (i < 4) EnableItem(hWnd, which+1, FALSE);
		ShowItem(hWnd, IDC_COMBO1+i, false);
		if (i > 0) {
			ShowItem(hWnd, IDC_EDIT1+i-1, false);
			ShowItem(hWnd, IDC_EDIT5+i-1, false);
		}
	}
}

bool ExtraAngDynamics::StoreParams (QWidget *hWnd)
{
	char cbuf[256];
	int i, n = 0;
	double val, tlimit[5], alimit[5], couplim, torqlim;
	int mode[5];
	for (i = 0; i < 5; i++) {
		if (IsChecked(hWnd, IDC_CHECK1+i))
			n++;
	}
	for (i = 0; i < n-1; i++) {
		oapiGetDlgItemText(hWnd, IDC_EDIT1+i, cbuf, 256);
		if ((sscanf (cbuf, "%lf", tlimit+i) != 1) || (tlimit[i] <= 0)) {
			Error ("Invalid step limit entry.");
			return false;
		}
		oapiGetDlgItemText(hWnd, IDC_EDIT5+i, cbuf, 256);
		if ((sscanf (cbuf, "%lf", alimit+i) != 1) || (alimit[i] <= 0)) {
			Error ("Invalid angle step limit entry.");
			return false;
		}
		if (i > 0 && (tlimit[i] <= tlimit[i-1] || alimit[i] <= alimit[i-1])) {
			Error ("Step limits must be in ascending order");
			return false;
		}
	}
	for (i = 0; i < n; i++) {
		mode[i] = DlgItem<QComboBox>(hWnd, IDC_COMBO1+i)->currentIndex();
		if (mode[i] == -1) { // CB_ERR
			Error ("Invalid propagator.");
			return false;
		}
	}
	oapiGetDlgItemText(hWnd, IDC_EDIT11, cbuf, 256);
	if ((sscanf (cbuf, "%lf", &couplim) != 1) || (couplim < 0.0)) {
		Error ("Invalid coupling step limit");
		return false;
	}
	oapiGetDlgItemText(hWnd, IDC_EDIT12, cbuf, 256);
	if ((sscanf (cbuf, "%lf", &torqlim) != 1) || (torqlim < couplim)) {
		Error ("Torque step limit must be greater than coupling limit");
		return false;
	}

	Config *cfg = pTab->Cfg();
	cfg->CfgPhysicsPrm.nAPropLevel = n;
	for (i = 0; i < n; i++) {
		cfg->CfgPhysicsPrm.APropMode[i] = PropId[mode[i]];
		cfg->CfgPhysicsPrm.APropTLimit[i] = (i < n-1 ? tlimit[i]     : 1e10);
		cfg->CfgPhysicsPrm.PropALimit[i] = (i < n-1 ? alimit[i]*RAD : 1e10);
	}
	cfg->CfgPhysicsPrm.APropCouplingLimit = couplim*RAD;
	cfg->CfgPhysicsPrm.APropTorqueLimit = torqlim*RAD;

	oapiGetDlgItemText(hWnd, IDC_EDIT9, cbuf, 256);
	if ((sscanf (cbuf, "%lf", &val) != 1 || val < 0)) {
		Error ("Invalid subsampling target step.");
		return false;
	} else cfg->CfgPhysicsPrm.APropSubLimit = val*RAD;
	oapiGetDlgItemText(hWnd, IDC_EDIT10, cbuf, 256);
	if ((sscanf (cbuf, "%d", &i) != 1) || (i < 1)) {
		Error ("Invalid subsampling steps.");
		return false;
	} else cfg->CfgPhysicsPrm.APropSubMax = i;

	return true;
}

bool ExtraAngDynamics::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, pTab->Launchpad()->GetInstance(), "extra_angprop");
	return true;
}

void ExtraAngDynamics::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraAngDynamics*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_CHECK1:
		case IDC_CHECK2:
		case IDC_CHECK3:
		case IDC_CHECK4:
		case IDC_CHECK5:
			((ExtraAngDynamics*)context)->Activate (hWnd, id);
			break;
		case IDC_BUTTON1:
			((ExtraAngDynamics*)context)->ResetDialog (hWnd);
			return;
		case IDC_BUTTON2:
			((ExtraAngDynamics*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraAngDynamics*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}

#endif

//-----------------------------------------------------------------------------
// Physics engine: Parameters for orbit stabilisation
//-----------------------------------------------------------------------------

char *ExtraStabilisation::Name ()
{
	return (char*)"Orbit stabilisation";
}

char *ExtraStabilisation::Description ()
{
	return (char*)"Select the parameters that determine the conditions when Orbiter switches between dynamic and stabilised state updates.";
}

bool ExtraStabilisation::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_STABILISATION, DlgProc);
	return true;
}

void ExtraStabilisation::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgPhysicsPrm);
}

void ExtraStabilisation::ResetDialog (QWidget *hWnd)
{
	extern CFG_PHYSICSPRM CfgPhysicsPrm_default;
	SetDialog (hWnd, CfgPhysicsPrm_default);
}

void ExtraStabilisation::SetDialog (QWidget *hWnd, const CFG_PHYSICSPRM &prm)
{
	char cbuf[256];
	SetCheck(hWnd, IDC_STAB_ENABLE, prm.bOrbitStabilise);
	sprintf (cbuf, "%0.4g", prm.Stabilise_PLimit*100.0);
	oapiSetDlgItemText(hWnd, IDC_EDIT1, cbuf);
	sprintf (cbuf, "%0.4g", prm.Stabilise_SLimit*100.0);
	oapiSetDlgItemText(hWnd, IDC_EDIT2, cbuf);
	sprintf (cbuf, "%0.4g", prm.PPropSubLimit*100.0);
	oapiSetDlgItemText(hWnd, IDC_EDIT3, cbuf);
	sprintf (cbuf, "%d", prm.PPropSubMax);
	oapiSetDlgItemText(hWnd, IDC_EDIT4, cbuf);
	sprintf (cbuf, "%0.4g", prm.PPropStepLimit*100.0);
	oapiSetDlgItemText(hWnd, IDC_EDIT5, cbuf);
	ToggleEnable (hWnd);
}

bool ExtraStabilisation::StoreParams (QWidget *hWnd)
{
	char cbuf[256];
	int i;
	double plimit, slimit, val;
	Config *cfg = pTab->Cfg();
	oapiGetDlgItemText(hWnd, IDC_EDIT1, cbuf, 256);
	if (sscanf (cbuf, "%lf", &plimit) != 1 || plimit < 0.0 || plimit > 100.0) {
		Error ("Invalid perturbation limit.");
		return false;
	}
	oapiGetDlgItemText(hWnd, IDC_EDIT2, cbuf, 256);
	if (sscanf (cbuf, "%lf", &slimit) != 1 || slimit < 0.0) {
		Error ("Invalid step limit.");
		return false;
	}
	oapiGetDlgItemText(hWnd, IDC_EDIT3, cbuf, 256);
	if ((sscanf (cbuf, "%lf", &val) != 1 || val < 0)) {
		Error ("Invalid subsampling target step.");
		return false;
	} else cfg->CfgPhysicsPrm.PPropSubLimit = val*0.01;
	oapiGetDlgItemText(hWnd, IDC_EDIT4, cbuf, 256);
	if ((sscanf (cbuf, "%d", &i) != 1) || (i < 1)) {
		Error ("Invalid subsampling steps.");
		return false;
	} else cfg->CfgPhysicsPrm.PPropSubMax = i;
	oapiGetDlgItemText(hWnd, IDC_EDIT5, cbuf, 256);
	if ((sscanf (cbuf, "%lf", &val) != 1 || val < 0)) {
		Error ("Invalid perturbation limit value.");
		return false;
	} else cfg->CfgPhysicsPrm.PPropStepLimit = val*0.01;

	cfg->CfgPhysicsPrm.bOrbitStabilise = (IsChecked(hWnd, IDC_STAB_ENABLE));
	cfg->CfgPhysicsPrm.Stabilise_PLimit = plimit * 0.01;
	cfg->CfgPhysicsPrm.Stabilise_SLimit = slimit * 0.01;
	return true;
}

void ExtraStabilisation::ToggleEnable (QWidget *hWnd)
{
	int i;
	bool bstab = (IsChecked(hWnd, IDC_STAB_ENABLE));
	for (i = IDC_EDIT1; i <= IDC_EDIT5; i++)
		EnableItem(hWnd, i, bstab);
	for (i = IDC_STATIC1; i <= IDC_STATIC13; i++)
		EnableItem(hWnd, i, bstab);
}

bool ExtraStabilisation::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, "extra_orbitstab");
	return true;
}

void ExtraStabilisation::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraStabilisation*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_STAB_ENABLE:
			if (code == RESN_CLICKED) {
				((ExtraStabilisation*)context)->ToggleEnable (hWnd);
				return;
			}
			break;
		case IDC_BUTTON1:
			((ExtraStabilisation*)context)->ResetDialog (hWnd);
			return;
		case IDC_BUTTON2:
			((ExtraStabilisation*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraStabilisation*)context)->StoreParams(hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}


//=============================================================================
// Instruments and panels
//=============================================================================

char *ExtraInstruments::Name ()
{
	return (char*)"Instruments and panels";
}

char *ExtraInstruments::Description ()
{
	return (char*)"Select general configuration parameters for spacecraft instruments, MFD displays and instrument panels.";
}

//-----------------------------------------------------------------------------
// Instruments and panels: MFDs
//-----------------------------------------------------------------------------

char *ExtraMfdConfig::Name()
{
	return (char*)"MFD parameter configuration";
}

char *ExtraMfdConfig::Description ()
{
	return (char*)"Select display parameters for multifunctional displays (MFD).";
}

bool ExtraMfdConfig::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_MFDCONFIG, DlgProc);
	return true;
}

void ExtraMfdConfig::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgInstrumentPrm);
}

void ExtraMfdConfig::ResetDialog (QWidget *hWnd)
{
	extern CFG_INSTRUMENTPRM CfgInstrumentPrm_default;
	SetDialog (hWnd, CfgInstrumentPrm_default);
}

void ExtraMfdConfig::SetDialog (QWidget *hWnd, const CFG_INSTRUMENTPRM &prm)
{
	char cbuf[256];
	int i, idx;
	for (i = 0; i < 3; i++)
		SetCheck(hWnd, IDC_RADIO1+i, i == prm.bMfdPow2);
	sprintf (cbuf, "%d", prm.MfdHiresThreshold);
	oapiSetDlgItemText(hWnd, IDC_EDIT1, cbuf);

	idx = (prm.VCMFDSize == 256 ? 0 : prm.VCMFDSize == 512 ? 1 : 2);
	for (i = 0; i < 3; i++)
		SetCheck(hWnd, IDC_RADIO4+i, i == idx);
}

bool ExtraMfdConfig::StoreParams (QWidget *hWnd)
{
	Config *cfg = pTab->Cfg();
	char cbuf[256];
	int i, size, check;
	oapiGetDlgItemText(hWnd, IDC_EDIT1, cbuf, 256);
	if ((sscanf (cbuf, "%d", &size) != 1) || size < 8) {
		return false;
	} else
		cfg->CfgInstrumentPrm.MfdHiresThreshold = size;

	for (i = 0; i < 3; i++) {
		check = IsChecked (hWnd, IDC_RADIO1+i);
		if (check) {
			cfg->CfgInstrumentPrm.bMfdPow2 = i;
			break;
		}
	}

	size = 256;
	for (i = 0; i < 3; i++) {
		check = IsChecked (hWnd, IDC_RADIO4+i);
		if (check) {
			cfg->CfgInstrumentPrm.VCMFDSize = size;
			break;
		}
		size *= 2;
	}

	return true;
}

void ExtraMfdConfig::ToggleEnable (QWidget *hWnd)
{
	// todo
}

bool ExtraMfdConfig::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, "extra_mfdconfig");
	return true;
}

void ExtraMfdConfig::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraMfdConfig*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BUTTON1:
			((ExtraMfdConfig*)context)->ResetDialog (hWnd);
			return;
		case IDC_BUTTON2:
			((ExtraMfdConfig*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraMfdConfig*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}


//=============================================================================
// Root item for vessel configurations (sub-items to be added by modules)
//=============================================================================

char *ExtraVesselConfig::Name ()
{
	return (char*)"Vessel configuration";
}

char *ExtraVesselConfig::Description ()
{
	return (char*)"Configure spacecraft parameters";
}

//=============================================================================
// Root item for planet configurations (sub-items to be added by modules)
//=============================================================================

char *ExtraPlanetConfig::Name ()
{
	return (char*)"Celestial body configuration";
}

char *ExtraPlanetConfig::Description ()
{
	return (char*)"Configure options for celestial objects";
}

//=============================================================================
// Debugging parameters
//=============================================================================

char *ExtraDebug::Name ()
{
	return (char*)"Debugging options";
}

char *ExtraDebug::Description ()
{
	return (char*)"Various options that are useful for debugging and special tasks. Not generally used for standard simulation sessions.";
}

//-----------------------------------------------------------------------------
// Debugging parameters: shutdown options
//-----------------------------------------------------------------------------

char *ExtraShutdown::Name ()
{
	return (char*)"Orbiter shutdown options";
}

char *ExtraShutdown::Description ()
{
	return (char*)"Set the behaviour of Orbiter after closing the simulation window: return to Launchpad, respawn or terminate.";
}

bool ExtraShutdown::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_SHUTDOWN, DlgProc);
	return true;
}

void ExtraShutdown::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgDebugPrm);
}

void ExtraShutdown::ResetDialog (QWidget *hWnd)
{
	extern CFG_DEBUGPRM CfgDebugPrm_default;
	SetDialog (hWnd, CfgDebugPrm_default);
}

void ExtraShutdown::SetDialog (QWidget *hWnd, const CFG_DEBUGPRM &prm)
{
	for (int i = 0; i < 3; i++)
		SetCheck (hWnd, IDC_RADIO1+i, i==prm.ShutdownMode);
}

bool ExtraShutdown::StoreParams (QWidget *hWnd)
{
	Config *cfg = pTab->Cfg();
	int mode;
	for (mode = 0; mode < 2; mode++)
		if (IsChecked(hWnd, IDC_RADIO1+mode)) break;
	cfg->CfgDebugPrm.ShutdownMode = mode;
	return true;
}

bool ExtraShutdown::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, "extra_shutdown");
	return true;
}

void ExtraShutdown::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraShutdown*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BUTTON1:
			((ExtraShutdown*)context)->ResetDialog (hWnd);
			return;
		case IDC_BUTTON2:
			((ExtraShutdown*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraShutdown*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}

//-----------------------------------------------------------------------------
// Debugging parameters: fixed time steps
//-----------------------------------------------------------------------------

char *ExtraFixedStep::Name ()
{
	return (char*)"Fixed time steps";
}

char *ExtraFixedStep::Description ()
{
	return (char*)"This option assigns a fixed simulation time interval to each frame. Useful for debugging, and when numerical accuracy and stability of the dynamic propagators are important (for example, to generate trajectory data or when recording high-fidelity playbacks).\r\n\r\nWarning: Selecting this option leads to nonlinear time flow and a simulation that is no longer real-time.";
}

bool ExtraFixedStep::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_FIXEDSTEP, DlgProc);
	return true;
}

void ExtraFixedStep::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgDebugPrm);
}

void ExtraFixedStep::ResetDialog (QWidget *hWnd)
{
	extern CFG_DEBUGPRM CfgDebugPrm_default;
	SetDialog (hWnd, CfgDebugPrm_default);
}

void ExtraFixedStep::SetDialog (QWidget *hWnd, const CFG_DEBUGPRM &prm)
{
	char cbuf[256];
	double step = prm.FixedStep;

	if (pTab->Cfg()->CfgCmdlinePrm.FixedStep) {
		// fixed step is set by command line options - disable the dialog
		SetCheck(hWnd, IDC_CHECK1, true);
		sprintf(cbuf, "%0.4g", pTab->Cfg()->CfgCmdlinePrm.FixedStep);
		oapiSetDlgItemText(hWnd, IDC_EDIT1, cbuf);
		EnableItem(hWnd, IDC_CHECK1, FALSE);
		EnableItem(hWnd, IDC_EDIT1, FALSE);
	}
	else {
		SetCheck(hWnd, IDC_CHECK1, step);
		sprintf(cbuf, "%0.4g", step ? step : 0.01);
		oapiSetDlgItemText(hWnd, IDC_EDIT1, cbuf);
		ToggleEnable(hWnd);
	}
}

bool ExtraFixedStep::StoreParams (QWidget *hWnd)
{
	Config *cfg = pTab->Cfg();
	bool fixed = (IsChecked(hWnd, IDC_CHECK1));
	if (!fixed) {
		cfg->CfgDebugPrm.FixedStep = 0;
	} else {
		char cbuf[256];
		double dt;
		oapiGetDlgItemText(hWnd, IDC_EDIT1, cbuf, 256);
		if (sscanf (cbuf, "%lf", &dt) != 1 || dt <= 0) {
			Error ("Invalid frame interval length");
			return false;
		}
		cfg->CfgDebugPrm.FixedStep = dt;
	}
	return true;
}

void ExtraFixedStep::ToggleEnable (QWidget *hWnd)
{
	bool fixed = (IsChecked(hWnd, IDC_CHECK1));
	EnableItem(hWnd, IDC_STATIC1, fixed);
	EnableItem(hWnd, IDC_EDIT1, fixed);
}

bool ExtraFixedStep::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, "extra_fixedstep");
	return true;
}

void ExtraFixedStep::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraFixedStep*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_CHECK1:
			if (code == RESN_CLICKED) {
				((ExtraFixedStep*)context)->ToggleEnable (hWnd);
				return;
			}
			break;
		case IDC_BUTTON1:
			((ExtraFixedStep*)context)->ResetDialog (hWnd);
			return;
		case IDC_BUTTON2:
			((ExtraFixedStep*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraFixedStep*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}

//-----------------------------------------------------------------------------
// Debugging parameters: rendering options
//-----------------------------------------------------------------------------

char *ExtraRenderingOptions::Name ()
{
	return (char*)"Rendering options";
}

char *ExtraRenderingOptions::Description ()
{
	return (char*)"Some rendering options that can be used for debugging problems.";
}

bool ExtraRenderingOptions::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_DBGRENDER, DlgProc);
	return true;
}

void ExtraRenderingOptions::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgDebugPrm);
}

void ExtraRenderingOptions::ResetDialog (QWidget *hWnd)
{
	extern CFG_DEBUGPRM CfgDebugPrm_default;
	SetDialog (hWnd, CfgDebugPrm_default);
}

void ExtraRenderingOptions::SetDialog (QWidget *hWnd, const CFG_DEBUGPRM &prm)
{
	SetCheck(hWnd, IDC_CHECK1, prm.bWireframeMode);
	SetCheck(hWnd, IDC_CHECK2, prm.bNormaliseNormals);
}

bool ExtraRenderingOptions::StoreParams (QWidget *hWnd)
{
	Config *cfg = pTab->Cfg();
	cfg->CfgDebugPrm.bWireframeMode = (IsChecked(hWnd, IDC_CHECK1));
	cfg->CfgDebugPrm.bNormaliseNormals = (IsChecked(hWnd, IDC_CHECK2));
	return true;
}

void ExtraRenderingOptions::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraRenderingOptions*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BUTTON1:
			((ExtraRenderingOptions*)context)->ResetDialog (hWnd);
			return;
	//	case IDC_BUTTON2:
	//		((ExtraTimerSettings*)context)->OpenHelp (hWnd);
	//		return;
		case IDOK:
			if (((ExtraRenderingOptions*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}

//-----------------------------------------------------------------------------
// Debugging parameters: performance options
//-----------------------------------------------------------------------------

char *ExtraPerformanceSettings::Name ()
{
	return (char*)"Performance options";
}

char *ExtraPerformanceSettings::Description ()
{
	return (char*)"This option can be used to modify Windows environment parameters that can improve the simulator performance.";
}

bool ExtraPerformanceSettings::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_PERFORMANCE, DlgProc);
	return true;
}

void ExtraPerformanceSettings::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgDebugPrm);
}

void ExtraPerformanceSettings::ResetDialog (QWidget *hWnd)
{
	extern CFG_DEBUGPRM CfgDebugPrm_default;
	SetDialog (hWnd, CfgDebugPrm_default);
}

void ExtraPerformanceSettings::SetDialog (QWidget *hWnd, const CFG_DEBUGPRM &prm)
{
	SetCheck(hWnd, IDC_CHECK1, prm.bDisableSmoothFont);
	SetCheck(hWnd, IDC_CHECK2, prm.bForceReenableSmoothFont);
}

bool ExtraPerformanceSettings::StoreParams (QWidget *hWnd)
{
	Config *cfg = pTab->Cfg();
	cfg->CfgDebugPrm.bDisableSmoothFont = (IsChecked(hWnd, IDC_CHECK1) ? true : false);
	cfg->CfgDebugPrm.bForceReenableSmoothFont = (IsChecked(hWnd, IDC_CHECK2) ? true : false);
	if (cfg->CfgDebugPrm.bDisableSmoothFont)
		g_pOrbiter->ActivateRoughType();
	else
		g_pOrbiter->DeactivateRoughType();
	return true;
}

bool ExtraPerformanceSettings::OpenHelp (QWidget *hWnd)
{
	OpenDefaultHelp (hWnd, "extra_performance");
	return true;
}

void ExtraPerformanceSettings::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraPerformanceSettings*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BUTTON1:
			((ExtraPerformanceSettings*)context)->ResetDialog (hWnd);
			return;
		case IDC_BUTTON2:
			((ExtraPerformanceSettings*)context)->OpenHelp (hWnd);
			return;
		case IDOK:
			if (((ExtraPerformanceSettings*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}


//-----------------------------------------------------------------------------
// Debugging parameters: launchpad options
//-----------------------------------------------------------------------------

char *ExtraLaunchpadOptions::Name ()
{
	return (char*)"Launchpad options";
}

char *ExtraLaunchpadOptions::Description ()
{
	return (char*)"Configure the behaviour of the Orbiter Launchpad dialog.";
}

bool ExtraLaunchpadOptions::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_LAUNCHPAD, DlgProc);
	return true;
}

void ExtraLaunchpadOptions::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgDebugPrm);
}

void ExtraLaunchpadOptions::ResetDialog (QWidget *hWnd)
{
	extern CFG_DEBUGPRM CfgDebugPrm_default;
	SetDialog (hWnd, CfgDebugPrm_default);
}

void ExtraLaunchpadOptions::SetDialog (QWidget *hWnd, const CFG_DEBUGPRM &prm)
{
	int i;
	for (i = 0; i < 3; i++)
		SetCheck(hWnd, IDC_RADIO1+i, prm.bHtmlScnDesc == i);
	SetCheck(hWnd, IDC_CHECK1, prm.bSaveExitScreen);
}

bool ExtraLaunchpadOptions::StoreParams (QWidget *hWnd)
{
	int i;
	Config *cfg = pTab->Cfg();
	cfg->CfgDebugPrm.bSaveExitScreen = (IsChecked(hWnd, IDC_CHECK1) ? true : false);
	for (i = 0; i < 3; i++) {
		if (IsChecked(hWnd, IDC_RADIO1+i)) {
			break;
		}
	}
	if (i != cfg->CfgDebugPrm.bHtmlScnDesc) {
		cfg->CfgDebugPrm.bHtmlScnDesc = i;
		QMessageBox::warning (NULL, "Orbiter settings", "You need to restart Orbiter for these changes to take effect.");
	}
	return true;
}

void ExtraLaunchpadOptions::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraLaunchpadOptions*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BUTTON1:
			((ExtraLaunchpadOptions*)context)->ResetDialog (hWnd);
			return;
		//case IDC_BUTTON2:
		//	((ExtraLaunchpadOptions*)context)->OpenHelp (hWnd);
		//	return;
		case IDOK:
			if (((ExtraLaunchpadOptions*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}


//-----------------------------------------------------------------------------
// Debugging parameters: logfile options
//-----------------------------------------------------------------------------

char *ExtraLogfileOptions::Name ()
{
	return (char*)"Logfile options";
}

char *ExtraLogfileOptions::Description ()
{
	return (char*)"Configure options for log file output.";
}

bool ExtraLogfileOptions::clbkOpen (QWidget *hParent)
{
	OpenDialog (hParent, IDD_EXTRA_LOGFILE, DlgProc);
	return true;
}

void ExtraLogfileOptions::InitDialog (QWidget *hWnd)
{
	SetDialog (hWnd, pTab->Cfg()->CfgDebugPrm);
}

void ExtraLogfileOptions::ResetDialog (QWidget *hWnd)
{
	extern CFG_DEBUGPRM CfgDebugPrm_default;
	SetDialog (hWnd, CfgDebugPrm_default);
}

void ExtraLogfileOptions::SetDialog (QWidget *hWnd, const CFG_DEBUGPRM &prm)
{
	SetCheck(hWnd, IDC_CHECK1, prm.bVerboseLog);
}

bool ExtraLogfileOptions::StoreParams (QWidget *hWnd)
{
	Config *cfg = pTab->Cfg();
	cfg->CfgDebugPrm.bVerboseLog = (IsChecked(hWnd, IDC_CHECK1) ? true : false);
	return true;
}

void ExtraLogfileOptions::DlgProc (QWidget *hWnd, void *context)
{
	// WM_INITDIALOG
	((ExtraLogfileOptions*)context)->InitDialog (hWnd);
	// WM_COMMAND
	oapiConnectDlgCommands (hWnd, [hWnd, context](int id, int code, QWidget *hCtrl) {
		switch (id) {
		case IDC_BUTTON1:
			((ExtraLogfileOptions*)context)->ResetDialog (hWnd);
			return;
		//case IDC_BUTTON2:
		//	((ExtraLogfileOptions*)context)->OpenHelp (hWnd);
		//	return;
		case IDOK:
			if (((ExtraLogfileOptions*)context)->StoreParams (hWnd))
				qobject_cast<QDialog*> (hWnd)->done (0);
			break;
		}
	});
	BuiltinLaunchpadItem::DlgProc (hWnd, context);
}

//-----------------------------------------------------------------------------
// class LaunchpadItem: addon-defined items for the "Extra" tab
// Interface in OrbiterAPI.h
//-----------------------------------------------------------------------------

LaunchpadItem::LaunchpadItem ()
{
	hItem = 0;
}

LaunchpadItem::~LaunchpadItem ()
{}

char *LaunchpadItem::Name ()
{
	return 0;
}

char *LaunchpadItem::Description ()
{
	return 0;
}

bool LaunchpadItem::OpenDialog (void *hInst, QWidget *hLaunchpad, int resId, DLGINIT pDlg)
{
	// DialogBoxParam: modal, the item is the context of the set-up function
	QDialog *dlg = qobject_cast<QDialog*> (oapiCreateResDialog (hInst, resId, hLaunchpad));
	if (!dlg) return true;
	if (pDlg) pDlg (dlg, this);
	dlg->exec();
	delete dlg;
	return true;
}

bool LaunchpadItem::clbkOpen (QWidget *hLaunchpad)
{
	return false;
}

int LaunchpadItem::clbkWriteConfig ()
{
	return 0;
}
