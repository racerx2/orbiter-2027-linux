// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ======================================================================
// Template for simulation options pages
// ======================================================================

#include <array>
#include <fstream>
#include <iterator>
#include <strings.h>
#include "OptionsPages.h"
#include "DlgCtrl.h"
#include "Orbiter.h"
#include "Psys.h"
#include "Camera.h"
#include "resource.h"
#include "ResDialog.h"
#include "Util.h"
#include <QAbstractButton>
#include <QComboBox>
#include <QListWidget>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QTreeWidget>

using std::min;
using std::max;

extern Orbiter* g_pOrbiter;
extern PlanetarySystem* g_psys;
extern Camera* g_camera;

// BM_SETCHECK / BM_GETCHECK / EnableWindow / ShowWindow on a page control
static void SetCheck(QWidget *hPage, int id, bool check)
{
	if (QAbstractButton *b = DlgItem<QAbstractButton>(hPage, id)) b->setChecked(check);
}

static bool IsChecked(QWidget *hPage, int id)
{
	QAbstractButton *b = DlgItem<QAbstractButton>(hPage, id);
	return (b && b->isChecked());
}

static void EnableItem(QWidget *hPage, int id, bool enable)
{
	if (QWidget *w = oapiResDlgItem(hPage, id)) w->setEnabled(enable);
}

static void ShowItem(QWidget *hPage, int id, bool show)
{
	if (QWidget *w = oapiResDlgItem(hPage, id)) w->setVisible(show);
}

// ======================================================================

OptionsPageContainer::OptionsPageContainer(Originator orig, Config* cfg)
	: m_orig(orig)
	, m_cfg(cfg)
{
	m_hDlg = 0;
	m_pageIdx = 0;
	m_vScrollPage = 0;
	m_vScrollRange = 0;
	m_vScrollPos = 0;
	m_contextHelp = 0;
}

// ----------------------------------------------------------------------

OptionsPageContainer::~OptionsPageContainer()
{
	Clear();
}

// ----------------------------------------------------------------------

OptionsPage* OptionsPageContainer::CurrentPage()
{
	return (m_pageIdx < m_pPage.size() ? m_pPage[m_pageIdx] : nullptr);
}

// ----------------------------------------------------------------------

void OptionsPageContainer::SetWindowHandles(QWidget *hDlg, QWidget *hSplitter, QWidget *hPane1, QWidget *hPane2)
{
	m_hDlg = hDlg;
	m_hPageList = hPane1;
	m_hContainer = hPane2;
	m_splitter.SetHwnd(hSplitter, m_hPageList, m_hContainer);
	m_container.SetHwnd(m_hContainer);
	m_splitter.SetStaticPane(SplitterCtrl::PANE1, 120);

	// WM_NOTIFY of the page list
	if (QTreeWidget *hTree = qobject_cast<QTreeWidget*>(m_hPageList))
		QObject::connect(hTree, &QTreeWidget::currentItemChanged, hDlg, [this](QTreeWidgetItem *itemNew) { OnNotifyPagelist(itemNew); });
	// WM_VSCROLL of the page scroll bar, if the dialog has one
	if (QScrollBar *sb = DlgItem<QScrollBar>(hDlg, IDC_SCROLLBAR1))
		QObject::connect(sb, &QScrollBar::valueChanged, hDlg, [this, hDlg, sb](int pos) { VScroll(hDlg, pos, sb); });
}

// ----------------------------------------------------------------------

void OptionsPageContainer::CreatePages()
{
	QTreeWidgetItem *parent;
	if (m_orig == LAUNCHPAD) {
		AddPage(new OptionsPage_Visual(this));
		AddPage(new OptionsPage_Physics(this));
	}
	AddPage(new OptionsPage_Instrument(this));
	AddPage(new OptionsPage_Vessel(this));
	parent = AddPage(new OptionsPage_UI(this));
	AddPage(new OptionsPage_Joystick(this), parent);
	AddPage(new OptionsPage_CelSphere(this));
	parent = AddPage(new OptionsPage_VisHelper(this));
	AddPage(new OptionsPage_Planetarium(this), parent);
	AddPage(new OptionsPage_Labels(this), parent);
	AddPage(new OptionsPage_Forces(this), parent);
	AddPage(new OptionsPage_Axes(this), parent);
	QTreeWidget *hTree = qobject_cast<QTreeWidget*>(m_hPageList);
	hTree->setCurrentItem(hTree->topLevelItem(0));
}

// ----------------------------------------------------------------------

void OptionsPageContainer::ExpandAll()
{
	bool expand = true;
	QTreeWidget *hTree = DlgItem<QTreeWidget>(m_hDlg, IDC_OPT_PAGELIST);
	for (int i = 0; i < hTree->topLevelItemCount(); i++)
		hTree->topLevelItem(i)->setExpanded(expand);
}

// ----------------------------------------------------------------------

void OptionsPageContainer::SetPageSize(QWidget *hDlg)
{
	if (m_pageIdx >= m_pPage.size()) return; // sanity check

	int h0 = m_container.HWnd()->height();
	int h1 = m_pPage[m_pageIdx]->HPage()->height();
	bool bVscroll = h1 > h0;
	QScrollBar *sb = DlgItem<QScrollBar>(hDlg, IDC_SCROLLBAR1);
	ShowItem(hDlg, IDC_SCROLLBAR1, bVscroll);

	if (bVscroll) {
		m_vScrollRange = (h1 - h0);
		int nPos = min(m_vScrollPos, m_vScrollRange);
		m_vScrollPage = h0;
		if (sb) {
			QSignalBlocker block(sb);
			sb->setRange(0, m_vScrollRange);
			sb->setPageStep(m_vScrollPage);
			sb->setValue(nPos);
		}
		int dy = m_vScrollPos - nPos;
		m_vScrollPos = nPos;
		if (dy)
			m_pPage[m_pageIdx]->HPage()->scroll(0, dy);
	}
	else if (m_vScrollPos) {
		m_pPage[m_pageIdx]->HPage()->scroll(0, m_vScrollPos);
		m_vScrollPos = 0;
	}
}

// ----------------------------------------------------------------------

QTreeWidgetItem *OptionsPageContainer::AddPage(OptionsPage* pPage, QTreeWidgetItem *parent)
{
	m_pPage.push_back(pPage);
	return pPage->CreatePage(m_hDlg, parent);
}

// ----------------------------------------------------------------------

const OptionsPage* OptionsPageContainer::FindPage(const char* name) const
{
	for (auto page : m_pPage)
		if (!strcmp(name, page->Name()))
			return page;
	return 0;
}

// ----------------------------------------------------------------------

void OptionsPageContainer::SwitchPage(const char* name)
{
	QTreeWidget *hTree = qobject_cast<QTreeWidget*>(m_hPageList);
	for (int i = 0; i < hTree->topLevelItemCount(); i++) {
		QTreeWidgetItem *item = hTree->topLevelItem(i);
		if (!strcasecmp(item->text(0).toUtf8().constData(), name)) {
			hTree->setCurrentItem(item);
			break;
		}
		for (int j = 0; j < item->childCount(); j++) {
			QTreeWidgetItem *child = item->child(j);
			if (!strcasecmp(child->text(0).toUtf8().constData(), name)) {
				hTree->setCurrentItem(child);
				break;
			}
		}
	}
}


// ----------------------------------------------------------------------

void OptionsPageContainer::SwitchPage(size_t page)
{
	if (page >= m_pPage.size())
		return;
	m_pageIdx = page;
	for (size_t pg = 0; pg < m_pPage.size(); pg++)
		if (pg != m_pageIdx) m_pPage[pg]->Show(false);
	m_pPage[m_pageIdx]->Show(true);
	m_pPage[m_pageIdx]->UpdateControls(m_pPage[m_pageIdx]->HPage());
	m_vScrollPos = 0;
	m_vScrollRange = 0;
	m_vScrollPage = 0;
	m_contextHelp = m_pPage[m_pageIdx]->HelpContext();

	SetPageSize(m_hDlg);
	m_hDlg->update();
}

// ----------------------------------------------------------------------

void OptionsPageContainer::SwitchPage(const OptionsPage* page)
{
	for (size_t i = 0; i < m_pPage.size(); i++)
		if (m_pPage[i] == page) {
			SwitchPage(i);
			break;
		}
}

// ----------------------------------------------------------------------

void OptionsPageContainer::Clear()
{
	for (auto pPage : m_pPage)
		delete pPage;
	m_pPage.clear();
}

void OptionsPageContainer::OnNotifyPagelist(QTreeWidgetItem *itemNew)
{
	// TVN_SELCHANGED
	if (!itemNew) return;
	OptionsPage* page = (OptionsPage*)itemNew->data(0, Qt::UserRole).value<void*>();
	SwitchPage(page);
	itemNew->setExpanded(true);
}

// ----------------------------------------------------------------------

BOOL OptionsPageContainer::VScroll(QWidget *hDlg, int pos, QWidget *hControl)
{
	// the scroll bar handles the line/page/thumb requests itself and reports the new position
	QWidget *hPage = CurrentPage()->HPage();

	if (pos >= 0 && pos != m_vScrollPos) {
		int dy = -(pos - m_vScrollPos);
		m_vScrollPos = pos;
		hPage->scroll(0, dy);
	}
	return FALSE;
}

// ----------------------------------------------------------------------

void OptionsPageContainer::UpdatePages(bool resetView)
{
	for (auto pPage : m_pPage)
		pPage->UpdateControls(pPage->HPage());
	if (resetView) {
		QTreeWidget *hTree = qobject_cast<QTreeWidget*>(m_hPageList);
		hTree->setCurrentItem(hTree->topLevelItem(0));
	}
}

// ----------------------------------------------------------------------

void OptionsPageContainer::UpdateConfig()
{
	for (auto pPage : m_pPage)
		pPage->UpdateConfig(pPage->HPage());
}

// ======================================================================

OptionsPage::OptionsPage(OptionsPageContainer* container)
	: m_container(container)
	, m_hPage(0)
	, m_hItem(0)
{
}

// ----------------------------------------------------------------------

OptionsPage::~OptionsPage()
{
	// Remove the object reference from the window.
	// The page window is owned by the container control and goes with it; its connections name the window
	// as context, so they end with it.
	if (m_hPage)
		m_hPage->setProperty("OptionsPage", QVariant());
}

// ----------------------------------------------------------------------

void OptionsPage::Show(bool bShow)
{
	m_hPage->setVisible(bShow);
}

// ----------------------------------------------------------------------

QWidget *OptionsPage::HParent() const
{
	return m_container->ContainerControl()->HWnd();
}

// ----------------------------------------------------------------------

QTreeWidgetItem *OptionsPage::CreatePage(QWidget *hDlg, QTreeWidgetItem *parent)
{
	int winId = ResourceId();
	m_hPage = oapiCreateResDialog(g_pOrbiter->GetInstance(), winId, HParent());
	if (m_hPage) {
		m_hPage->setProperty("OptionsPage", QVariant::fromValue((void*)this)); // DWLP_USER
		DlgProc(m_hPage);
		OnInitDialog(m_hPage); // WM_INITDIALOG
	}

	QTreeWidgetItem *hti = new QTreeWidgetItem();
	hti->setText(0, QString::fromUtf8(Name()));
	hti->setData(0, Qt::UserRole, QVariant::fromValue((void*)this));
	if (parent) parent->addChild(hti);
	else DlgItem<QTreeWidget>(hDlg, IDC_OPT_PAGELIST)->addTopLevelItem(hti);
	m_hItem = hti;
	return hti;
}

// ----------------------------------------------------------------------

BOOL OptionsPage::OnInitDialog(QWidget *hWnd)
{
	UpdateControls(hWnd);
	return TRUE;
}

// ----------------------------------------------------------------------

void OptionsPage::DlgProc(QWidget *hWnd)
{
	// WM_COMMAND
	oapiConnectDlgCommands(hWnd, [this, hWnd](int id, int code, QWidget *hCtrl) { OnCommand(hWnd, id, code, hCtrl); });
	for (QObject *o : hWnd->children()) {
		int id = oapiResId(qobject_cast<QWidget*>(o));
		// WM_HSCROLL of gauge controls
		if (GaugeCtrl *g = qobject_cast<GaugeCtrl*>(o))
			QObject::connect(g, &GaugeCtrl::scrolled, hWnd, [this, hWnd, id](int request, int pos) { OnHScroll(hWnd, id, request, pos); });
		// WM_NOTIFY UDN_DELTAPOS of up-down controls
		else if (ResUpDown *ud = qobject_cast<ResUpDown*>(o))
			QObject::connect(ud, &ResUpDown::deltaPos, hWnd, [this, hWnd, id](int iDelta) { OnDeltaPos(hWnd, id, iDelta); });
	}
	// other window events
	new EventHook(hWnd, [this, hWnd](QObject *obj, QEvent *event) { return obj == hWnd && OnMessage(hWnd, event); });
}

// ======================================================================

OptionsPage_Visual::OptionsPage_Visual(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Visual::ResourceId() const
{
	return IDD_OPTIONS_VISUAL;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Visual::Name() const
{
	const char* name = "Visual settings";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Visual::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_visual.htm"); // this needs to be updated
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Visual::UpdateControls(QWidget *hPage)
{
	char cbuf[256];

	SetCheck(hPage, IDC_OPT_VIS_CLOUD, Cfg()->CfgVisualPrm.bClouds);
	SetCheck(hPage, IDC_OPT_VIS_CSHADOW, Cfg()->CfgVisualPrm.bCloudShadows);
	SetCheck(hPage, IDC_OPT_VIS_HAZE, Cfg()->CfgVisualPrm.bHaze);
	SetCheck(hPage, IDC_OPT_VIS_FOG, Cfg()->CfgVisualPrm.bFog);
	SetCheck(hPage, IDC_OPT_VIS_REFWATER, Cfg()->CfgVisualPrm.bWaterreflect);
	SetCheck(hPage, IDC_OPT_VIS_RIPPLE, Cfg()->CfgVisualPrm.bSpecularRipple);
	SetCheck(hPage, IDC_OPT_VIS_LIGHTS, Cfg()->CfgVisualPrm.bNightlights);
	sprintf(cbuf, "%0.2f", Cfg()->CfgVisualPrm.LightBrightness);
	oapiSetDlgItemText(hPage, IDC_OPT_VIS_LTLEVEL, cbuf);
	SetCheck(hPage, IDC_OPT_VIS_ELEV, Cfg()->CfgVisualPrm.ElevMode);
	DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE)->setCurrentIndex(Cfg()->CfgVisualPrm.ElevMode < 2 ? 0 : 1);
	sprintf(cbuf, "%d", Cfg()->CfgVisualPrm.PlanetMaxLevel);
	oapiSetDlgItemText(hPage, IDC_OPT_VIS_MAXLEVEL, cbuf);
	SetCheck(hPage, IDC_OPT_VIS_VSHADOW, Cfg()->CfgVisualPrm.bVesselShadows);
	SetCheck(hPage, IDC_OPT_VIS_REENTRY, Cfg()->CfgVisualPrm.bReentryFlames);
	SetCheck(hPage, IDC_OPT_VIS_SHADOW, Cfg()->CfgVisualPrm.bShadows);
	SetCheck(hPage, IDC_OPT_VIS_PARTICLE, Cfg()->CfgVisualPrm.bParticleStreams);
	SetCheck(hPage, IDC_OPT_VIS_SPECULAR, Cfg()->CfgVisualPrm.bSpecular);
	SetCheck(hPage, IDC_OPT_VIS_LOCALLIGHT, Cfg()->CfgVisualPrm.bLocalLight);
	sprintf(cbuf, "%d", Cfg()->CfgVisualPrm.AmbientLevel);
	oapiSetDlgItemText(hPage, IDC_OPT_VIS_AMBIENT, cbuf);

	VisualsChanged(hPage);
}

// ----------------------------------------------------------------------

void OptionsPage_Visual::UpdateConfig(QWidget *hPage)
{
	char cbuf[256];
	DWORD i;
	double d;

	Cfg()->CfgVisualPrm.bClouds = (IsChecked(hPage, IDC_OPT_VIS_CLOUD));
	Cfg()->CfgVisualPrm.bCloudShadows = (IsChecked(hPage, IDC_OPT_VIS_CSHADOW));
	Cfg()->CfgVisualPrm.bHaze = (IsChecked(hPage, IDC_OPT_VIS_HAZE));
	Cfg()->CfgVisualPrm.bFog = (IsChecked(hPage, IDC_OPT_VIS_FOG));
	Cfg()->CfgVisualPrm.bWaterreflect = (IsChecked(hPage, IDC_OPT_VIS_REFWATER));
	Cfg()->CfgVisualPrm.bSpecularRipple = (IsChecked(hPage, IDC_OPT_VIS_RIPPLE));
	Cfg()->CfgVisualPrm.bNightlights = (IsChecked(hPage, IDC_OPT_VIS_LIGHTS));
	oapiGetDlgItemText(hPage, IDC_OPT_VIS_LTLEVEL, cbuf, 255);
	if (!sscanf(cbuf, "%lf", &d)) d = 0.5; else if (d < 0) d = 0.0; else if (d > 1) d = 1.0;
	Cfg()->CfgVisualPrm.LightBrightness = d;
	Cfg()->CfgVisualPrm.ElevMode = (!IsChecked(hPage, IDC_OPT_VIS_ELEV) ?
		0 : DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE)->currentIndex() + 1);
	oapiGetDlgItemText(hPage, IDC_OPT_VIS_MAXLEVEL, cbuf, 127);
	if (!sscanf(cbuf, "%u", &i)) i = SURF_MAX_PATCHLEVEL2;
	Cfg()->CfgVisualPrm.PlanetMaxLevel = max((DWORD)1, min((DWORD)SURF_MAX_PATCHLEVEL2, i));
	Cfg()->CfgVisualPrm.bVesselShadows = (IsChecked(hPage, IDC_OPT_VIS_VSHADOW));
	Cfg()->CfgVisualPrm.bReentryFlames = (IsChecked(hPage, IDC_OPT_VIS_REENTRY));
	Cfg()->CfgVisualPrm.bShadows = (IsChecked(hPage, IDC_OPT_VIS_SHADOW));
	Cfg()->CfgVisualPrm.bParticleStreams = (IsChecked(hPage, IDC_OPT_VIS_PARTICLE));
	Cfg()->CfgVisualPrm.bSpecular = (IsChecked(hPage, IDC_OPT_VIS_SPECULAR));
	Cfg()->CfgVisualPrm.bLocalLight = (IsChecked(hPage, IDC_OPT_VIS_LOCALLIGHT));
	oapiGetDlgItemText(hPage, IDC_OPT_VIS_AMBIENT, cbuf, 255);
	if (!sscanf(cbuf, "%u", &i)) i = 15; else if (i > 255) i = 255;
	Cfg()->SetAmbientLevel(i);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Visual::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE)->clear();
	oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE), "linear interpolation");
	oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE), "cubic interpolation");
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Visual::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId)
	{
		case IDC_OPT_VIS_CLOUD:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_CLOUD));
				Cfg()->CfgVisualPrm.bClouds = check;
				VisualsChanged( hPage );
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_CSHADOW:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_CSHADOW));
				Cfg()->CfgVisualPrm.bCloudShadows = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_HAZE:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_HAZE));
				Cfg()->CfgVisualPrm.bHaze = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_FOG:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_FOG));
				Cfg()->CfgVisualPrm.bFog = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_REFWATER:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_REFWATER));
				Cfg()->CfgVisualPrm.bWaterreflect = check;
				VisualsChanged( hPage );
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_RIPPLE:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_RIPPLE));
				Cfg()->CfgVisualPrm.bSpecularRipple = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_LIGHTS:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_LIGHTS));
				Cfg()->CfgVisualPrm.bNightlights = check;
				VisualsChanged( hPage );
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_LTLEVEL:
			if (notification == RESN_CHANGE)
			{
				char cbuf[16];
				double d;
				oapiGetDlgItemText(hPage, IDC_OPT_VIS_LTLEVEL, cbuf, 16);
				if (!sscanf( cbuf, "%lf", &d )) d = 0.5;
				else if (d < 0) d = 0.0;
				else if (d > 1) d = 1.0;
				Cfg()->CfgVisualPrm.LightBrightness = d;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_ELEV:
			if (notification == RESN_CLICKED)
			{
				int elevmode = !IsChecked(hPage, IDC_OPT_VIS_ELEV) ? 0 : (DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE)->currentIndex() + 1);
				Cfg()->CfgVisualPrm.ElevMode = elevmode;
				VisualsChanged( hPage );
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_ELEVMODE:
			if (notification == RESN_SELCHANGE)
			{
				int elevmode = !IsChecked(hPage, IDC_OPT_VIS_ELEV) ? 0 : (DlgItem<QComboBox>(hPage, IDC_OPT_VIS_ELEVMODE)->currentIndex() + 1);
				Cfg()->CfgVisualPrm.ElevMode = elevmode;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_MAXLEVEL:
			if (notification == RESN_CHANGE)
			{
				char cbuf[16];
				DWORD i;
				oapiGetDlgItemText(hPage, IDC_OPT_VIS_MAXLEVEL, cbuf, 16);
				if (!sscanf( cbuf, "%u", &i )) i = SURF_MAX_PATCHLEVEL2;
				Cfg()->CfgVisualPrm.PlanetMaxLevel = max((DWORD)1, min((DWORD)SURF_MAX_PATCHLEVEL2, i));
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_VSHADOW:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_VSHADOW));
				Cfg()->CfgVisualPrm.bVesselShadows = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_REENTRY:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_REENTRY));
				Cfg()->CfgVisualPrm.bReentryFlames = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_SHADOW:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_SHADOW));
				Cfg()->CfgVisualPrm.bShadows = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_PARTICLE:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_PARTICLE));
				Cfg()->CfgVisualPrm.bParticleStreams = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_SPECULAR:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_SPECULAR));
				Cfg()->CfgVisualPrm.bSpecular = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_LOCALLIGHT:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_VIS_LOCALLIGHT));
				Cfg()->CfgVisualPrm.bLocalLight = check;
				return FALSE;
			}
			break;
		case IDC_OPT_VIS_AMBIENT:
			if (notification == RESN_CHANGE)
			{
				char cbuf[16];
				DWORD i;
				oapiGetDlgItemText(hPage, IDC_OPT_VIS_AMBIENT, cbuf, 16);
				if (!sscanf( cbuf, "%u", &i )) i = 15;
				else if (i > 255) i = 255;
				Cfg()->SetAmbientLevel( i );
				return FALSE;
			}
			break;
	}
	return TRUE;
}

//-----------------------------------------------------------------------------

void OptionsPage_Visual::VisualsChanged(QWidget *hPage)
{
	EnableItem(hPage, IDC_OPT_VIS_CSHADOW, IsChecked(hPage, IDC_OPT_VIS_CLOUD));
	EnableItem(hPage, IDC_OPT_VIS_RIPPLE, IsChecked(hPage, IDC_OPT_VIS_REFWATER));
	EnableItem(hPage, IDC_OPT_VIS_ELEVMODE, IsChecked(hPage, IDC_OPT_VIS_ELEV));
	EnableItem(hPage, IDC_OPT_VIS_LTLEVEL, IsChecked(hPage, IDC_OPT_VIS_LIGHTS));
	return;
}

// ======================================================================

OptionsPage_Physics::OptionsPage_Physics(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Physics::ResourceId() const
{
	return IDD_OPTIONS_PHYSICS;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Physics::Name() const
{
	const char* name = "Physics settings";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Physics::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_param.htm"); // this needs to be updated
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Physics::UpdateControls(QWidget *hPage)
{
	SetCheck(hPage, IDC_OPT_PHYS_COMPLEXGRAV, Cfg()->CfgPhysicsPrm.bNonsphericalGrav);
	SetCheck(hPage, IDC_OPT_PHYS_RPRESSURE, Cfg()->CfgPhysicsPrm.bRadiationPressure);
	SetCheck(hPage, IDC_OPT_PHYS_DISTMASS, Cfg()->CfgPhysicsPrm.bDistributedMass);
	SetCheck(hPage, IDC_OPT_PHYS_WIND, Cfg()->CfgPhysicsPrm.bAtmWind);
}

// ----------------------------------------------------------------------

void OptionsPage_Physics::UpdateConfig(QWidget *hPage)
{
	Cfg()->CfgPhysicsPrm.bDistributedMass = (IsChecked(hPage, IDC_OPT_PHYS_DISTMASS));
	Cfg()->CfgPhysicsPrm.bNonsphericalGrav = (IsChecked(hPage, IDC_OPT_PHYS_COMPLEXGRAV));
	Cfg()->CfgPhysicsPrm.bRadiationPressure = (IsChecked(hPage, IDC_OPT_PHYS_RPRESSURE));
	Cfg()->CfgPhysicsPrm.bAtmWind = (IsChecked(hPage, IDC_OPT_PHYS_WIND));
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Physics::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Physics::OnCommand( QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl )
{
	switch (ctrlId)
	{
		case IDC_OPT_PHYS_DISTMASS:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_PHYS_DISTMASS));
				Cfg()->CfgPhysicsPrm.bDistributedMass = check;
				return FALSE;
			}
			break;
		case IDC_OPT_PHYS_COMPLEXGRAV:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_PHYS_COMPLEXGRAV));
				Cfg()->CfgPhysicsPrm.bNonsphericalGrav = check;
				return FALSE;
			}
			break;
		case IDC_OPT_PHYS_RPRESSURE:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_PHYS_RPRESSURE));
				Cfg()->CfgPhysicsPrm.bRadiationPressure = check;
				return FALSE;
			}
			break;
		case IDC_OPT_PHYS_WIND:
			if (notification == RESN_CLICKED)
			{
				bool check = (IsChecked(hPage, IDC_OPT_PHYS_WIND));
				Cfg()->CfgPhysicsPrm.bAtmWind = check;
				return FALSE;
			}
			break;
	}
	return TRUE;
}

// ======================================================================

OptionsPage_Instrument::OptionsPage_Instrument(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Instrument::ResourceId() const
{
	return IDD_OPTIONS_INSTRUMENT;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Instrument::Name() const
{
	const char* name = "Instruments & panels";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Instrument::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_param.htm"); // this needs to be updated
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Instrument::UpdateControls(QWidget *hPage)
{
	char cbuf[256];
	double mfdUpdDt = Cfg()->CfgLogicPrm.InstrUpdDT;
	sprintf(cbuf, "%0.2f", mfdUpdDt);
	oapiSetDlgItemText(hPage, IDC_OPT_MFD_INTERVAL, cbuf);
	int mfdSize = Cfg()->CfgLogicPrm.MFDSize;
	sprintf(cbuf, "%d", mfdSize);
	oapiSetDlgItemText(hPage, IDC_OPT_MFD_SIZE, cbuf);
	bool enable = Cfg()->CfgLogicPrm.bMfdTransparent;
	SetCheck(hPage, IDC_OPT_MFD_TRANSP, enable);
	int vcmfdsize = Cfg()->CfgInstrumentPrm.VCMFDSize;
	int idx = (vcmfdsize == 1024 ? 2 : vcmfdsize == 512 ? 1 : 0);
	DlgItem<QComboBox>(hPage, IDC_OPT_MFD_VCTEXSIZE)->setCurrentIndex(idx);
	double scrollSpeed = Cfg()->CfgLogicPrm.PanelScrollSpeed;
	sprintf(cbuf, "%0.0f", scrollSpeed * 0.1);
	oapiSetDlgItemText(hPage, IDC_OPT_PANEL_SCROLLSPEED, cbuf);
	double panelSize = Cfg()->CfgLogicPrm.PanelScale;
	sprintf(cbuf, "%0.2f", panelSize);
	oapiSetDlgItemText(hPage, IDC_OPT_PANEL_SCALE, cbuf);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Instrument::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	DlgItem<QComboBox>(hPage, IDC_OPT_MFD_VCTEXSIZE)->clear();
	oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_MFD_VCTEXSIZE), "256 x 256");
	oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_MFD_VCTEXSIZE), "512 x 512");
	oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_MFD_VCTEXSIZE), "1024 x 1024");
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Instrument::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_MFD_INTERVAL:
		if (notification == RESN_CHANGE) {
			char cbuf[256];
			double updDt;
			oapiGetDlgItemText(hPage, IDC_OPT_MFD_INTERVAL, cbuf, 255);
			if (sscanf(cbuf, "%lf", &updDt)) {
				Cfg()->CfgLogicPrm.InstrUpdDT = max(0.01, updDt);
				g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_MFDUPDATEINTERVAL);
			}
			return FALSE;
		}
		break;
	case IDC_OPT_MFD_SIZE:
		if (notification == RESN_CHANGE) {
			char cbuf[256];
			int size;
			oapiGetDlgItemText(hPage, IDC_OPT_MFD_SIZE, cbuf, 256);
			if (sscanf(cbuf, "%d", &size)) {
				Cfg()->CfgLogicPrm.MFDSize = max(1, min(10, size));
				g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_MFDGENERICSIZE);
			}
			return FALSE;
		}
		break;
	case IDC_OPT_MFD_TRANSP:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, IDC_OPT_MFD_TRANSP));
			Cfg()->CfgLogicPrm.bMfdTransparent = check;
			g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_MFDGENERICTRANSP);
			return FALSE;
		}
		break;
	case IDC_OPT_MFD_VCTEXSIZE:
		if (notification == RESN_SELCHANGE) {
			int vcmfdsize[3] = { 256, 512, 1024 };
			DWORD idx = DlgItem<QComboBox>(hPage, IDC_OPT_MFD_VCTEXSIZE)->currentIndex();
			Cfg()->CfgInstrumentPrm.VCMFDSize = vcmfdsize[idx];
			g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_MFDVCSIZE);
		}
		break;
	case IDC_OPT_PANEL_SCROLLSPEED:
		if (notification == RESN_CHANGE) {
			char cbuf[256];
			double speed;
			oapiGetDlgItemText(hPage, IDC_OPT_PANEL_SCROLLSPEED, cbuf, 256);
			if (sscanf(cbuf, "%lf", &speed)) {
				Cfg()->CfgLogicPrm.PanelScrollSpeed = 10.0 * max(-100.0, min(100.0, speed));
				g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_PANELSCROLLSPEED);
			}
			return FALSE;
		}
		break;
	case IDC_OPT_PANEL_SCALE:
		if (notification == RESN_CHANGE) {
			char cbuf[256];
			double scale;
			oapiGetDlgItemText(hPage, IDC_OPT_PANEL_SCALE, cbuf, 256);
			if (sscanf(cbuf, "%lf", &scale)) {
				Cfg()->CfgLogicPrm.PanelScale = max(0.25, min(4.0, scale));
				g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_PANELSCALE);
			}
			return FALSE;
		}
		break;
	}
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Instrument::OnDeltaPos(QWidget *hPage, int ctrlId, int iDelta)
{
	{ // UDN_DELTAPOS
		int delta = -iDelta;
		switch (ctrlId) {
		case IDC_OPT_MFD_INTERVALSPIN:
			Cfg()->CfgLogicPrm.InstrUpdDT = max(0.01, Cfg()->CfgLogicPrm.InstrUpdDT + delta * 0.01);
			g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_MFDUPDATEINTERVAL);
			break;
		case IDC_OPT_MFD_SIZESPIN:
			Cfg()->CfgLogicPrm.MFDSize = max(1, min(10, Cfg()->CfgLogicPrm.MFDSize + delta));
			g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_MFDGENERICSIZE);
			break;
		case IDC_OPT_PANEL_SCROLLSPEEDSPIN:
			Cfg()->CfgLogicPrm.PanelScrollSpeed = 10.0 * max(-100.0, min(100.0, 0.1 * Cfg()->CfgLogicPrm.PanelScrollSpeed + delta));
			g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_PANELSCROLLSPEED);
			break;
		case IDC_OPT_PANEL_SCALESPIN:
			Cfg()->CfgLogicPrm.PanelScale = max(0.25, min(4.0, Cfg()->CfgLogicPrm.PanelScale + delta * 0.01));
			g_pOrbiter->OnOptionChanged(OPTCAT_INSTRUMENT, OPTITEM_INSTRUMENT_PANELSCALE);
			break;
		}
		UpdateControls(hPage);
		return TRUE;
	}
}

// ======================================================================

OptionsPage_Vessel::OptionsPage_Vessel(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Vessel::ResourceId() const
{
	return IDD_OPTIONS_VESSEL;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Vessel::Name() const
{
	const char* name = "Vessel settings";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Vessel::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_param.htm"); // this needs to be updated
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Vessel::UpdateControls(QWidget *hPage)
{
	SetCheck(hPage, IDC_OPT_VESSEL_FUELLIMIT, Cfg()->CfgLogicPrm.bLimitedFuel);
	SetCheck(hPage, IDC_OPT_VESSEL_PADFUEL, Cfg()->CfgLogicPrm.bPadRefuel);
	SetCheck(hPage, IDC_OPT_VESSEL_COMPLEXMODEL, Cfg()->CfgLogicPrm.FlightModelLevel);
	SetCheck(hPage, IDC_OPT_VESSEL_DAMAGE, Cfg()->CfgLogicPrm.DamageSetting);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Vessel::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	if (Container()->Environment() == OptionsPageContainer::INLINE) {
		UpdateControls(hPage);
		EnableItem(hPage, IDC_OPT_VESSEL_COMPLEXMODEL, FALSE);
		EnableItem(hPage, IDC_OPT_VESSEL_DAMAGE, FALSE);
	}
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Vessel::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_VESSEL_FUELLIMIT:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, IDC_OPT_VESSEL_FUELLIMIT));
			Cfg()->CfgLogicPrm.bLimitedFuel = check;
			g_pOrbiter->OnOptionChanged(OPTCAT_VESSEL, OPTITEM_VESSEL_LIMITEDFUEL);
			return FALSE;
		}
		break;
	case IDC_OPT_VESSEL_PADFUEL:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, IDC_OPT_VESSEL_PADFUEL));
			Cfg()->CfgLogicPrm.bPadRefuel = check;
			g_pOrbiter->OnOptionChanged(OPTCAT_VESSEL, OPTITEM_VESSEL_PADREFUEL);
			return FALSE;
		}
		break;
	case IDC_OPT_VESSEL_COMPLEXMODEL:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, IDC_OPT_VESSEL_COMPLEXMODEL));
			Cfg()->CfgLogicPrm.FlightModelLevel = check;
			return FALSE;
		}
		break;
	case IDC_OPT_VESSEL_DAMAGE:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, IDC_OPT_VESSEL_DAMAGE));
			Cfg()->CfgLogicPrm.DamageSetting = check;
			return FALSE;
		}
		break;
	}
	return TRUE;
}

// ======================================================================

OptionsPage_UI::OptionsPage_UI(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_UI::ResourceId() const
{
	return IDD_OPTIONS_UI;
}

// ----------------------------------------------------------------------

const char* OptionsPage_UI::Name() const
{
	const char* name = "User interface";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_UI::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_param.htm"); // this needs to be updated
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_UI::UpdateControls(QWidget *hPage)
{
	DWORD mode = Cfg()->CfgUIPrm.MouseFocusMode;
	DlgItem<QComboBox>(hPage, IDC_OPT_UI_MOUSEFOCUSMODE)->setCurrentIndex(mode);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_UI::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);

	DlgItem<QComboBox>(hPage, IDC_OPT_UI_MOUSEFOCUSMODE)->clear();
	const char* strMouseMode[3] = { "Focus requires click", "Hybrid: Click required only for child windows", "Focus follows mouse" };
	for (int i = 0; i < 3; i++)
		oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_UI_MOUSEFOCUSMODE), strMouseMode[i]);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_UI::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_UI_MOUSEFOCUSMODE:
		if (notification == RESN_SELCHANGE) {
			DWORD mode = DlgItem<QComboBox>(hPage, IDC_OPT_UI_MOUSEFOCUSMODE)->currentIndex();
			Cfg()->CfgUIPrm.MouseFocusMode = mode;
			return FALSE;
		}
		break;
	}
	return TRUE;
}

// ======================================================================

OptionsPage_Joystick::OptionsPage_Joystick(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Joystick::ResourceId() const
{
	return IDD_OPTIONS_JOYSTICK;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Joystick::Name() const
{
	const char* name = "Joystick";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Joystick::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/tab_joystick.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Joystick::UpdateControls(QWidget *hPage)
{
	char cbuf[256];

	DlgItem<QComboBox>(hPage, IDC_OPT_JOY_DEVICE)->setCurrentIndex((int)Cfg()->CfgJoystickPrm.Joy_idx);
	DlgItem<QComboBox>(hPage, IDC_OPT_JOY_THROTTLE)->setCurrentIndex((int)Cfg()->CfgJoystickPrm.ThrottleAxis);
	SetCheck(hPage, IDC_OPT_JOY_INIT, Cfg()->CfgJoystickPrm.bThrottleIgnore);

	int sat = Cfg()->CfgJoystickPrm.ThrottleSaturation / 10;
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_JOY_SAT), sat);
	sprintf(cbuf, "%d", sat);
	oapiSetDlgItemText(hPage, IDC_OPT_JOY_STATIC1, cbuf);

	int dz = Cfg()->CfgJoystickPrm.Deadzone / 10;
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_JOY_DEAD), dz);
	sprintf(cbuf, "%d", dz);
	oapiSetDlgItemText(hPage, IDC_OPT_JOY_STATIC2, cbuf);

	int residJoystick[] = {
		IDC_OPT_JOY_THROTTLE, IDC_OPT_JOY_INIT, IDC_OPT_JOY_SAT, IDC_OPT_JOY_DEAD,
		IDC_OPT_JOY_STATIC1, IDC_OPT_JOY_STATIC2, IDC_OPT_JOY_STATIC3, IDC_OPT_JOY_STATIC4
	};
	bool enable = Cfg()->CfgJoystickPrm.Joy_idx > 0;
	for (int i = 0; i < std::size(residJoystick); i++) {
		EnableItem(hPage, residJoystick[i], enable);
	}
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Joystick::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);

	DWORD ndev;
	JoyDeviceInstance* joylist;
	g_pOrbiter->GetDInput()->GetJoysticks(&joylist, &ndev);

	DlgItem<QComboBox>(hPage, IDC_OPT_JOY_DEVICE)->clear();
	oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_JOY_DEVICE), "<Disabled>");
	for (DWORD i = 0; i < ndev; i++)
		oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_JOY_DEVICE), (joylist[i].tszProductName));

	const char* thmode[4] = { "<Keyboard only>", "Z-axis", "Slider 0", "Slider 1" };
	DlgItem<QComboBox>(hPage, IDC_OPT_JOY_THROTTLE)->clear();
	for (int i = 0; i < std::size(thmode); i++)
		oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_JOY_THROTTLE), thmode[i]);

	GAUGEPARAM gp = { 0, 1000, GAUGEPARAM::LEFT, GAUGEPARAM::BLACK };
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_JOY_SAT), &gp);
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_JOY_DEAD), &gp);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Joystick::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_JOY_DEVICE:
		if (notification == RESN_SELCHANGE) {
			DWORD idx = DlgItem<QComboBox>(hPage, IDC_OPT_JOY_DEVICE)->currentIndex();
			Cfg()->CfgJoystickPrm.Joy_idx = idx;
			g_pOrbiter->OnOptionChanged(OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_DEVICE);
			UpdateControls(hPage);
			return FALSE;
		}
		break;
	case IDC_OPT_JOY_THROTTLE:
		if (notification == RESN_SELCHANGE) {
			DWORD axis = DlgItem<QComboBox>(hPage, IDC_OPT_JOY_THROTTLE)->currentIndex();
			Cfg()->CfgJoystickPrm.ThrottleAxis = axis;
			g_pOrbiter->OnOptionChanged(OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_PARAM);
			return FALSE;
		}
		break;
	case IDC_OPT_JOY_INIT:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, IDC_OPT_JOY_INIT));
			Cfg()->CfgJoystickPrm.bThrottleIgnore = check;
			break;
		}
		break;
	}
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Joystick::OnHScroll(QWidget *hPage, int ctrlId, int request, int pos)
{
	int val;
	switch (ctrlId) {
	case IDC_OPT_JOY_SAT:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			val = pos;
			Cfg()->CfgJoystickPrm.ThrottleSaturation = val * 10;
			UpdateControls(hPage);
			g_pOrbiter->OnOptionChanged(OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_PARAM);
			return 0;
		}
		break;
	case IDC_OPT_JOY_DEAD:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			val = pos;
			Cfg()->CfgJoystickPrm.Deadzone = val * 10;
			UpdateControls(hPage);
			g_pOrbiter->OnOptionChanged(OPTCAT_JOYSTICK, OPTITEM_JOYSTICK_PARAM);
			return 0;
		}
		break;
	}
	return FALSE;
}

// ======================================================================

OptionsPage_CelSphere::OptionsPage_CelSphere(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_CelSphere::ResourceId() const
{
	return IDD_OPTIONS_CELSPHERE;
}

// ----------------------------------------------------------------------

const char* OptionsPage_CelSphere::Name() const
{
	const char* name = "Celestial sphere";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_CelSphere::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/opt_celsphere.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_CelSphere::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);

	GAUGEPARAM gp = { 0, 100, GAUGEPARAM::LEFT, GAUGEPARAM::BLACK };
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_CSP_BGBRIGHTNESS), &gp);

	PopulateStarmapList(hPage);
	PopulateBgImageList(hPage);
	UpdateControls(hPage);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_CelSphere::OnCommand(QWidget *hPage, WORD id, WORD code, QWidget *hControl)
{
	switch (id) {
	case IDC_OPT_CSP_ENABLESTARPIX:
		if (code == RESN_CLICKED) {
			StarPixelActivationChanged(hPage);
			return FALSE;
		}
		break;
	case IDC_OPT_CSP_ENABLESTARMAP:
		if (code == RESN_CLICKED) {
			StarmapActivationChanged(hPage);
			return FALSE;
		}
		break;
	case IDC_OPT_CSP_ENABLEBKGMAP:
		if (code == RESN_CLICKED) {
			BackgroundActivationChanged(hPage);
			return FALSE;
		}
		break;
	case IDC_OPT_CSP_STARMAPIMAGE:
		if (code == RESN_SELCHANGE) {
			StarmapImageChanged(hPage);
			return false;
		}
		break;
	case IDC_OPT_CSP_BKGIMAGE:
		if (code == RESN_SELCHANGE) {
			BackgroundImageChanged(hPage);
			return FALSE;
		}
		break;
	case IDC_OPT_CSP_STARMAPLIN:
	case IDC_OPT_CSP_STARMAPEXP:
		if (code == RESN_CLICKED) {
			Cfg()->CfgVisualPrm.StarPrm.map_log = (id == IDC_OPT_CSP_STARMAPEXP);
			g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_STARDISPLAYPARAM);
			return FALSE;
		}
		break;
	}
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_CelSphere::OnHScroll(QWidget *hPage, int ctrlId, int request, int pos)
{
	switch (ctrlId) {
	case IDC_OPT_CSP_BGBRIGHTNESS:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			BackgroundBrightnessChanged(hPage, 0.01 * pos);
			return 0;
		}
		break;
	}
	return FALSE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_CelSphere::OnDeltaPos(QWidget *hPage, int ctrlId, int iDelta)
{
	{ // UDN_DELTAPOS
		int delta = -iDelta;
		StarRenderPrm& prm = Cfg()->CfgVisualPrm.StarPrm;
		switch (ctrlId) {
		case IDC_OPT_CSP_STARMAGHISPIN:
			prm.mag_hi = min(prm.mag_lo, max(-2.0, prm.mag_hi + delta * 0.1));
			break;
		case IDC_OPT_CSP_STARMAGLOSPIN:
			prm.mag_lo = min(15.0, max(prm.mag_hi, prm.mag_lo + delta * 0.1));
			break;
		case IDC_OPT_CSP_STARMINBRTSPIN:
			prm.brt_min = min(1.0, max(0.0, prm.brt_min + delta * 0.01));
			break;
		}
		UpdateControls(hPage);
		g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_STARDISPLAYPARAM);
		return TRUE;
	}
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::UpdateControls(QWidget *hPage)
{
	char cbuf[256];

	std::string starpath = std::string(Cfg()->CfgVisualPrm.StarImagePath);
	for (int idx = 0; idx < m_pathStarmap.size(); idx++)
		if (!starpath.compare(m_pathStarmap[idx].second)) {
			DlgItem<QComboBox>(hPage, IDC_OPT_CSP_STARMAPIMAGE)->setCurrentIndex(idx);
			break;
		}

	std::string bgpath = std::string(Cfg()->CfgVisualPrm.CSphereBgPath);
	for (int idx = 0; idx < m_pathBgImage.size(); idx++)
		if (!bgpath.compare(m_pathBgImage[idx].second)) {
			DlgItem<QComboBox>(hPage, IDC_OPT_CSP_BKGIMAGE)->setCurrentIndex(idx);
			break;
		}

	bool checked = Cfg()->CfgVisualPrm.bUseStarImage;
	SetCheck(hPage, IDC_OPT_CSP_ENABLESTARMAP, checked);
	EnableItem(hPage, IDC_OPT_CSP_STARMAPIMAGE, checked ? TRUE : FALSE);

	checked = Cfg()->CfgVisualPrm.bUseBgImage;
	SetCheck(hPage, IDC_OPT_CSP_ENABLEBKGMAP, checked);
	int brt = (int)(Cfg()->CfgVisualPrm.CSphereBgIntens * 100.0);
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_CSP_BGBRIGHTNESS), brt);
	EnableItem(hPage, IDC_OPT_CSP_BKGIMAGE, checked ? TRUE : FALSE);
	EnableItem(hPage, IDC_STATIC1, checked ? TRUE : FALSE);
	EnableItem(hPage, IDC_OPT_CSP_BGBRIGHTNESS, checked ? TRUE : FALSE);

	checked = Cfg()->CfgVisualPrm.bUseStarDots;
	SetCheck(hPage, IDC_OPT_CSP_ENABLESTARPIX, checked);
	sprintf(cbuf, "%0.1f", Cfg()->CfgVisualPrm.StarPrm.mag_hi);
	oapiSetDlgItemText(hPage, IDC_OPT_CSP_STARMAGHI, cbuf);
	sprintf(cbuf, "%0.1f", Cfg()->CfgVisualPrm.StarPrm.mag_lo);
	oapiSetDlgItemText(hPage, IDC_OPT_CSP_STARMAGLO, cbuf);
	sprintf(cbuf, "%0.2f", Cfg()->CfgVisualPrm.StarPrm.brt_min);
	oapiSetDlgItemText(hPage, IDC_OPT_CSP_STARMINBRT, cbuf);
	SetCheck(hPage, IDC_OPT_CSP_STARMAPLIN, !(Cfg()->CfgVisualPrm.StarPrm.map_log));
	SetCheck(hPage, IDC_OPT_CSP_STARMAPEXP, Cfg()->CfgVisualPrm.StarPrm.map_log);
	std::vector<int> ctrlStarPix{
		IDC_STATIC2, IDC_STATIC3, IDC_STATIC4, IDC_STATIC5, IDC_STATIC6,
		IDC_OPT_CSP_STARMAGHISPIN, IDC_OPT_CSP_STARMAGLOSPIN, IDC_OPT_CSP_STARMINBRTSPIN,
		IDC_OPT_CSP_STARMAGHI, IDC_OPT_CSP_STARMAGLO, IDC_OPT_CSP_STARMINBRT, IDC_OPT_CSP_STARMAPLIN, IDC_OPT_CSP_STARMAPEXP
	};
	for (auto ctrl : ctrlStarPix)
		EnableItem(hPage, ctrl, checked ? TRUE : FALSE);
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::PopulateStarmapList(QWidget *hPage)
{
	DlgItem<QComboBox>(hPage, IDC_OPT_CSP_STARMAPIMAGE)->clear();
	m_pathStarmap.clear();

	std::ifstream ifs(oapiResolvePath(Cfg()->ConfigPath("CSphere/bkgimage")));
	if (ifs) {
		char* c;
		char cbuf[256];
		bool found = false;
		while (ifs.getline(cbuf, 256)) {
			if (!found) {
				if (!strcmp(cbuf, "BEGIN_STARMAPS"))
					found = true;
				continue;
			}
			if (!strcmp(cbuf, "END_STARMAPS"))
				break;
			c = strtok(cbuf, "|");
			if (c) {
				oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_CSP_STARMAPIMAGE), c);
				std::string label(c);
				c = strtok(NULL, "\n");
				std::string path(c);
				m_pathStarmap.push_back(std::make_pair(label, path));
			}
		}
	}
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::PopulateBgImageList(QWidget *hPage)
{
	DlgItem<QComboBox>(hPage, IDC_OPT_CSP_BKGIMAGE)->clear();
	m_pathBgImage.clear();

	std::ifstream ifs(oapiResolvePath(Cfg()->ConfigPath("CSphere/bkgimage")));
	if (ifs) {
		char* c;
		char cbuf[256];
		bool found = false;
		while (ifs.getline(cbuf, 256)) {
			if (!found) {
				if (!strcmp(cbuf, "BEGIN_BACKGROUNDS"))
					found = true;
				continue;
			}
			if (!strcmp(cbuf, "END_BACKGROUNDS"))
				break;
			c = strtok(cbuf, "|");
			if (c) {
				oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_CSP_BKGIMAGE), c);
				std::string label(c);
				c = strtok(NULL, "\n");
				std::string path(c);
				m_pathBgImage.push_back(std::make_pair(label, path));
			}
		}
	}
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::StarPixelActivationChanged(QWidget *hPage)
{
	bool activated = IsChecked(hPage, IDC_OPT_CSP_ENABLESTARPIX);
	bool active = Cfg()->CfgVisualPrm.bUseStarDots;

	if (activated != active) {
		Cfg()->CfgVisualPrm.bUseStarDots = activated;
		g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_ACTIVATESTARDOTS);
	}
	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::StarmapActivationChanged(QWidget *hPage)
{
	bool activated = IsChecked(hPage, IDC_OPT_CSP_ENABLESTARMAP);
	bool active = Cfg()->CfgVisualPrm.bUseStarImage;

	if (activated != active) {
		Cfg()->CfgVisualPrm.bUseStarImage = activated;
		g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_ACTIVATESTARIMAGE);
	}
	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::StarmapImageChanged(QWidget *hPage)
{
	int idx = DlgItem<QComboBox>(hPage, IDC_OPT_CSP_STARMAPIMAGE)->currentIndex();
	std::string& path = m_pathStarmap[idx].second;
	strncpy(Cfg()->CfgVisualPrm.StarImagePath, path.c_str(), 128);
	g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_STARIMAGECHANGED);
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::BackgroundActivationChanged(QWidget *hPage)
{
	bool activated = IsChecked(hPage, IDC_OPT_CSP_ENABLEBKGMAP);
	bool active = Cfg()->CfgVisualPrm.bUseBgImage;

	if (activated != active) {
		Cfg()->CfgVisualPrm.bUseBgImage = activated;
		g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_ACTIVATEBGIMAGE);
	}
	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::BackgroundImageChanged(QWidget *hPage)
{
	int idx = DlgItem<QComboBox>(hPage, IDC_OPT_CSP_BKGIMAGE)->currentIndex();
	std::string& path = m_pathBgImage[idx].second;
	strncpy(Cfg()->CfgVisualPrm.CSphereBgPath, path.c_str(), 128);
	g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_BGIMAGECHANGED);
}

// ----------------------------------------------------------------------

void OptionsPage_CelSphere::BackgroundBrightnessChanged(QWidget *hPage, double level)
{
	if (level != Cfg()->CfgVisualPrm.CSphereBgIntens) {
		Cfg()->CfgVisualPrm.CSphereBgIntens = level;
		g_pOrbiter->OnOptionChanged(OPTCAT_CELSPHERE, OPTITEM_CELSPHERE_BGIMAGEBRIGHTNESS);
	}
}

// ======================================================================

OptionsPage_VisHelper::OptionsPage_VisHelper(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_VisHelper::ResourceId() const
{
	return IDD_OPTIONS_VISHELPER;
}

// ----------------------------------------------------------------------

const char* OptionsPage_VisHelper::Name() const
{
	const char* name = "Visual helpers";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_VisHelper::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/vishelper.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_VisHelper::UpdateControls(QWidget *hPage)
{
	int& plnFlag = Cfg()->CfgVisHelpPrm.flagPlanetarium;
	bool enable = plnFlag & PLN_ENABLE;
	SetCheck(hPage, IDC_OPT_PLN, enable);
	int& mkrFlag = Cfg()->CfgVisHelpPrm.flagMarkers;
	enable = mkrFlag & MKR_ENABLE;
	SetCheck(hPage, IDC_OPT_MKR, enable);
	int vecFlag = Cfg()->CfgVisHelpPrm.flagBodyForce;
	enable = (vecFlag & BFV_ENABLE);
	SetCheck(hPage, IDC_OPT_VEC, enable);
	int crdFlag = Cfg()->CfgVisHelpPrm.flagFrameAxes;
	enable = (crdFlag & FAV_ENABLE);
	SetCheck(hPage, IDC_OPT_CRD, enable);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_VisHelper::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_VisHelper::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_PLN:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, ctrlId));
			DWORD flag = PLN_ENABLE;
			int& plnFlag = Cfg()->CfgVisHelpPrm.flagPlanetarium;
			if (check) plnFlag |= flag;
			else       plnFlag &= ~flag;
			return TRUE;
		}
		break;
	case IDC_OPT_MKR:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, ctrlId));
			DWORD flag = MKR_ENABLE;
			int& mkrFlag = Cfg()->CfgVisHelpPrm.flagMarkers;
			if (check) mkrFlag |= flag;
			else       mkrFlag &= ~flag;
			return TRUE;
		}
		break;
	case IDC_OPT_VEC:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, ctrlId));
			DWORD flag = BFV_ENABLE;
			int& vecFlag = Cfg()->CfgVisHelpPrm.flagBodyForce;
			if (check) vecFlag |= flag;
			else       vecFlag &= ~flag;
		}
		break;
	case IDC_OPT_CRD:
		if (notification == RESN_CLICKED) {
			bool check = (IsChecked(hPage, ctrlId));
			DWORD flag = FAV_ENABLE;
			int& crdFlag = Cfg()->CfgVisHelpPrm.flagFrameAxes;
			if (check) crdFlag |= flag;
			else       crdFlag &= ~flag;
		}
		break;
	case IDC_OPT_VHELP_PLN:
		if (notification == RESN_CLICKED)
			Container()->SwitchPage("Planetarium");
		break;
	case IDC_OPT_VHELP_MKR:
		if (notification == RESN_CLICKED)
			Container()->SwitchPage("Labels");
		break;
	case IDC_OPT_VHELP_VEC:
		if (notification == RESN_CLICKED)
			Container()->SwitchPage("Body forces");
		break;
	case IDC_OPT_VHELP_CRD:
		if (notification == RESN_CLICKED)
			Container()->SwitchPage("Object axes");
		break;
	}
	return FALSE;
}

// ======================================================================

OptionsPage_Planetarium::OptionsPage_Planetarium(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Planetarium::ResourceId() const
{
	return IDD_OPTIONS_PLANETARIUM;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Planetarium::Name() const
{
	const char* name = "Planetarium";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Planetarium::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/vh_planetarium.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Planetarium::UpdateControls(QWidget *hPage)
{
	std::array<int, 15> residPlanetarium = {
		IDC_OPT_PLN_CELGRID, IDC_OPT_PLN_ECLGRID, IDC_OPT_PLN_GALGRID, IDC_OPT_PLN_HRZGRID, IDC_OPT_PLN_EQU,
		IDC_OPT_PLN_CNSTLABEL, IDC_OPT_PLN_CNSTLABEL_FULL, IDC_OPT_PLN_CNSTLABEL_SHORT, IDC_OPT_PLN_CNSTBND,
		IDC_OPT_PLN_CNSTPATTERN, IDC_OPT_PLN_MARKER, IDC_OPT_PLN_MKRLIST,
		IDC_STATIC1, IDC_STATIC2, IDC_STATIC3
	};

	int& plnFlag = Cfg()->CfgVisHelpPrm.flagPlanetarium;
	bool enable = plnFlag & PLN_ENABLE;
	SetCheck(hPage, IDC_OPT_PLN, enable);
	for (auto resid : residPlanetarium)
		EnableItem(hPage, resid, enable ? TRUE : FALSE);
	if (enable && !(plnFlag & PLN_CNSTLABEL)) {
		EnableItem(hPage, IDC_OPT_PLN_CNSTLABEL_FULL, FALSE);
		EnableItem(hPage, IDC_OPT_PLN_CNSTLABEL_SHORT, FALSE);
	}
	if (enable && !(plnFlag & PLN_CCMARK))
		EnableItem(hPage, IDC_OPT_PLN_MKRLIST, FALSE);

	SetCheck(hPage, IDC_OPT_PLN_CELGRID, plnFlag & PLN_CGRID);
	SetCheck(hPage, IDC_OPT_PLN_ECLGRID, plnFlag & PLN_EGRID);
	SetCheck(hPage, IDC_OPT_PLN_GALGRID, plnFlag & PLN_GGRID);
	SetCheck(hPage, IDC_OPT_PLN_HRZGRID, plnFlag & PLN_HGRID);
	SetCheck(hPage, IDC_OPT_PLN_EQU, plnFlag & PLN_EQU);
	SetCheck(hPage, IDC_OPT_PLN_CNSTLABEL, plnFlag & PLN_CNSTLABEL);
	SetCheck(hPage, IDC_OPT_PLN_CNSTBND, plnFlag & PLN_CNSTBND);
	SetCheck(hPage, IDC_OPT_PLN_CNSTPATTERN, plnFlag & PLN_CONST);
	SetCheck(hPage, IDC_OPT_PLN_MARKER, plnFlag & PLN_CCMARK);
	SetCheck(hPage, IDC_OPT_PLN_CNSTLABEL_FULL, plnFlag & PLN_CNSTLONG);
	SetCheck(hPage, IDC_OPT_PLN_CNSTLABEL_SHORT, !(plnFlag & PLN_CNSTLONG));
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Planetarium::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	RescanMarkerList(hPage);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Planetarium::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_PLN:
	case IDC_OPT_PLN_CELGRID:
	case IDC_OPT_PLN_ECLGRID:
	case IDC_OPT_PLN_GALGRID:
	case IDC_OPT_PLN_HRZGRID:
	case IDC_OPT_PLN_EQU:
	case IDC_OPT_PLN_CNSTLABEL:
	case IDC_OPT_PLN_CNSTBND:
	case IDC_OPT_PLN_CNSTPATTERN:
	case IDC_OPT_PLN_MARKER:
	case IDC_OPT_PLN_CNSTLABEL_FULL:
	case IDC_OPT_PLN_CNSTLABEL_SHORT:
		if (notification == RESN_CLICKED) {
			OnItemClicked(hPage, ctrlId);
			return TRUE;
		}
		break;
	case IDC_OPT_PLN_MKRLIST:
		if (notification == RESN_SELCHANGE)
			return OnMarkerSelectionChanged(hPage);
		break;
	}
	return FALSE;
}

// ----------------------------------------------------------------------

void OptionsPage_Planetarium::OnItemClicked(QWidget *hPage, WORD ctrlId)
{
	bool check = (IsChecked(hPage, ctrlId));
	DWORD flag;
	switch (ctrlId) {
	case IDC_OPT_PLN:                 flag = PLN_ENABLE;    break;
	case IDC_OPT_PLN_CELGRID:         flag = PLN_CGRID;     break;
	case IDC_OPT_PLN_ECLGRID:         flag = PLN_EGRID;     break;
	case IDC_OPT_PLN_GALGRID:         flag = PLN_GGRID;     break;
	case IDC_OPT_PLN_HRZGRID:         flag = PLN_HGRID;     break;
	case IDC_OPT_PLN_EQU:             flag = PLN_EQU;       break;
	case IDC_OPT_PLN_CNSTLABEL:       flag = PLN_CNSTLABEL; break;
	case IDC_OPT_PLN_CNSTBND:         flag = PLN_CNSTBND;   break;
	case IDC_OPT_PLN_CNSTPATTERN:     flag = PLN_CONST;     break;
	case IDC_OPT_PLN_MARKER:          flag = PLN_CCMARK;    break;
	case IDC_OPT_PLN_CNSTLABEL_FULL:  flag = PLN_CNSTLONG;  break;
	case IDC_OPT_PLN_CNSTLABEL_SHORT: flag = PLN_CNSTLONG; check = !check; break;
	default:                          flag = 0;             break;
	}
	int& plnFlag = Cfg()->CfgVisHelpPrm.flagPlanetarium;
	if (check) plnFlag |= flag;
	else       plnFlag &= ~flag;

	g_pOrbiter->OnOptionChanged(OPTCAT_PLANETARIUM, OPTITEM_PLANETARIUM_DISPFLAG);
	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Planetarium::OnMarkerSelectionChanged(QWidget *hPage)
{
	std::vector<oapi::GraphicsClient::LABELLIST>& list = g_psys->LabelList();
	if (list.size()) {
		for (int i = 0; i < list.size(); i++) {
			int sel = DlgItem<QListWidget>(hPage, IDC_OPT_PLN_MKRLIST)->item(i)->isSelected();
			list[i].active = (sel ? true : false);
		}
		std::ifstream fcfg(oapiResolvePath(Cfg()->ConfigPath(g_psys->Name().c_str())));
		g_psys->ScanLabelLists(fcfg);
	}
	return 0;
}

// ----------------------------------------------------------------------

void OptionsPage_Planetarium::RescanMarkerList(QWidget *hPage)
{
	QSignalBlocker block(DlgItem<QListWidget>(hPage, IDC_OPT_PLN_MKRLIST)); // LB_ messages don't notify
	DlgItem<QListWidget>(hPage, IDC_OPT_PLN_MKRLIST)->clear();

	if (!g_psys) return;
	const std::vector< oapi::GraphicsClient::LABELLIST>& list = g_psys->LabelList();
	if (!list.size()) return;

	int n = 0;
	g_psys->ForEach(FILETYPE_MARKER, [&](const fs::directory_entry& entry) {
		DlgItem<QListWidget>(hPage, IDC_OPT_PLN_MKRLIST)->addItem(QString::fromUtf8(entry.path().stem().string().c_str()));
		if (n < list.size() && list[n].active)
			DlgItem<QListWidget>(hPage, IDC_OPT_PLN_MKRLIST)->item(n)->setSelected(true);
		n++;
	});
}

// ======================================================================

OptionsPage_Labels::OptionsPage_Labels(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Labels::ResourceId() const
{
	return IDD_OPTIONS_LABELS;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Labels::Name() const
{
	const char* name = "Labels";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Labels::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/vh_labels.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Labels::UpdateControls(QWidget *hPage)
{
	std::array<int, 10> residLabels = {
		IDC_OPT_MKR_VESSEL, IDC_OPT_MKR_CELBODY, IDC_OPT_MKR_FEATUREBODY, IDC_OPT_MKR_BASE,
		IDC_OPT_MKR_BEACON, IDC_OPT_MKR_FEATURES, IDC_OPT_MKR_FEATUREBODY, IDC_OPT_MKR_FEATURELIST,
		IDC_STATIC1, IDC_STATIC2
	};

	int& mkrFlag = Cfg()->CfgVisHelpPrm.flagMarkers;
	bool enable = mkrFlag & MKR_ENABLE;
	SetCheck(hPage, IDC_OPT_MKR, enable);
	for (auto resid : residLabels)
		EnableItem(hPage, resid, enable ? TRUE : FALSE);
	if (enable && !(mkrFlag & MKR_LMARK)) {
		EnableItem(hPage, IDC_OPT_MKR_FEATUREBODY, FALSE);
		EnableItem(hPage, IDC_OPT_MKR_FEATURELIST, FALSE);
	}

	SetCheck(hPage, IDC_OPT_MKR_VESSEL, mkrFlag & MKR_VMARK);
	SetCheck(hPage, IDC_OPT_MKR_CELBODY, mkrFlag & MKR_CMARK);
	SetCheck(hPage, IDC_OPT_MKR_BASE, mkrFlag & MKR_BMARK);
	SetCheck(hPage, IDC_OPT_MKR_BEACON, mkrFlag & MKR_RMARK);
	SetCheck(hPage, IDC_OPT_MKR_FEATURES, mkrFlag & MKR_LMARK);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Labels::OnInitDialog(QWidget *hPage)
{
	OptionsPage::OnInitDialog(hPage);
	ScanPsysBodies(hPage);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Labels::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_MKR:
	case IDC_OPT_MKR_VESSEL:
	case IDC_OPT_MKR_CELBODY:
	case IDC_OPT_MKR_BASE:
	case IDC_OPT_MKR_BEACON:
	case IDC_OPT_MKR_FEATURES:
		if (notification == RESN_CLICKED) {
			OnItemClicked(hPage, ctrlId);
			return TRUE;
		}
		break;
	case IDC_OPT_MKR_FEATUREBODY:
		if (notification == RESN_SELCHANGE)
			UpdateFeatureList(hPage);
		return TRUE;
	case IDC_OPT_MKR_FEATURELIST:
		if (notification == RESN_SELCHANGE)
			RescanFeatures(hPage);
		return TRUE;
	}
	return FALSE;
}

// ----------------------------------------------------------------------

void OptionsPage_Labels::OnItemClicked(QWidget *hPage, WORD ctrlId)
{
	bool check = (IsChecked(hPage, ctrlId));
	DWORD flag;
	switch (ctrlId) {
	case IDC_OPT_MKR:          flag = MKR_ENABLE; break;
	case IDC_OPT_MKR_VESSEL:   flag = MKR_VMARK;  break;
	case IDC_OPT_MKR_CELBODY:  flag = MKR_CMARK;  break;
	case IDC_OPT_MKR_BASE:     flag = MKR_BMARK;  break;
	case IDC_OPT_MKR_BEACON:   flag = MKR_RMARK;  break;
	case IDC_OPT_MKR_FEATURES: flag = MKR_LMARK;  break;
	default:                   flag = 0;          break;
	}
	int& mkrFlag = Cfg()->CfgVisHelpPrm.flagMarkers;
	if (check) mkrFlag |= flag;
	else       mkrFlag &= ~flag;

	if (g_psys && ctrlId == IDC_OPT_MKR_FEATURES)
		g_psys->ActivatePlanetLabels(mkrFlag & MKR_ENABLE && mkrFlag & MKR_LMARK);

	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

void OptionsPage_Labels::ScanPsysBodies(QWidget *hPage)
{
	if (!g_psys) return;
	const Body* sel = nullptr;
	for (int i = 0; i < g_psys->nPlanet(); i++) {
		Planet* planet = g_psys->GetPlanet(i);
		if (planet == g_camera->Target())
			sel = planet;
		if (planet->isMoon())
			continue;
		oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY), planet->Name());
		for (int j = 0; j < planet->nSecondary(); j++) {
			char cbuf[256] = "    ";
			strncpy(cbuf + 4, planet->Secondary(j)->Name(), 252);
			oapiComboAddString(DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY), cbuf);
		}
	}
	if (!sel) {
		Body* tgt = g_camera->Target();
		if (tgt->Type() == OBJTP_VESSEL)
			sel = ((Vessel*)tgt)->GetSurfParam()->ref;
	}
	int idx = (sel ? DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY)->findText(QString::fromUtf8(sel->Name()), Qt::MatchFixedString) : 0);
	DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY)->setCurrentIndex(idx);
	UpdateFeatureList(hPage);
}

// ----------------------------------------------------------------------

void OptionsPage_Labels::UpdateFeatureList(QWidget *hPage)
{
	int n, nlist;
	char cbuf[256], cpath[256];
	int idx = DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY)->currentIndex();
	snprintf(cbuf, sizeof(cbuf), "%s", DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY)->itemText(idx).toUtf8().constData());
	QSignalBlocker block(DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)); // LB_ messages don't notify
	DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->clear();
	Planet* planet = g_psys->GetPlanet(trim_string(cbuf), true);
	if (!planet) return;

	if (planet->LabelFormat() < 2) {
		oapi::GraphicsClient::LABELLIST* list = planet->LabelList(&nlist);
		if (!nlist) return;

		n = 0;
		planet->ForEach(FILETYPE_MARKER, [&](const fs::directory_entry& entry) {
				DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->addItem(QString::fromUtf8(entry.path().stem().string().c_str()));
				if (n < nlist && list[n].active)
					DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->item(n)->setSelected(true);
				n++;
			});
	}
	else {
		int nlabel = planet->NumLabelLegend();
		if (nlabel) {
			const oapi::GraphicsClient::LABELTYPE* lspec = planet->LabelLegend();
			for (int i = 0; i < nlabel; i++) {
				DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->addItem(QString::fromUtf8(lspec[i].name));
				if (lspec[i].active)
					DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->item(i)->setSelected(true);
			}
		}
	}
}

// ----------------------------------------------------------------------

void OptionsPage_Labels::RescanFeatures(QWidget *hPage)
{
	char cbuf[256];
	int nlist;

	int idx = DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY)->currentIndex();
	snprintf(cbuf, sizeof(cbuf), "%s", DlgItem<QComboBox>(hPage, IDC_OPT_MKR_FEATUREBODY)->itemText(idx).toUtf8().constData());
	Planet* planet = g_psys->GetPlanet(trim_string(cbuf), true);
	if (!planet) return;

	if (planet->LabelFormat() < 2) {
		oapi::GraphicsClient::LABELLIST* list = planet->LabelList(&nlist);
		if (!nlist) return;

		for (int i = 0; i < nlist; i++) {
			BOOL sel = DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->item(i)->isSelected();
			list[i].active = (sel ? true : false);
		}

		std::ifstream fcfg(oapiResolvePath(Cfg()->ConfigPath(planet->Name())));
		planet->ScanLabelLists(fcfg);
	}
	else {
		nlist = planet->NumLabelLegend();
		for (int i = 0; i < nlist; i++) {
			BOOL sel = DlgItem<QListWidget>(hPage, IDC_OPT_MKR_FEATURELIST)->item(i)->isSelected();
			planet->SetLabelActive(i, sel ? true : false);
		}
	}
}

// ======================================================================

OptionsPage_Forces::OptionsPage_Forces(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Forces::ResourceId() const
{
	return IDD_OPTIONS_BODYFORCE;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Forces::Name() const
{
	const char* name = "Body forces";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Forces::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/vh_force.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Forces::UpdateControls(QWidget *hPage)
{
	std::array<int, 16> residForces = {
		IDC_OPT_VEC_WEIGHT, IDC_OPT_VEC_THRUST, IDC_OPT_VEC_LIFT, IDC_OPT_VEC_DRAG, IDC_OPT_VEC_SIDEFORCE, IDC_OPT_VEC_TOTAL,
		IDC_OPT_VEC_TORQUE, IDC_OPT_VEC_LINSCL, IDC_OPT_VEC_LOGSCL, IDC_OPT_VEC_SCALE, IDC_OPT_VEC_OPACITY,
		IDC_STATIC1, IDC_STATIC2, IDC_STATIC3, IDC_STATIC4, IDC_STATIC5
	};

	DWORD vecFlag = Cfg()->CfgVisHelpPrm.flagBodyForce;
	bool enable = (vecFlag & BFV_ENABLE);
	SetCheck(hPage, IDC_OPT_VEC, enable);
	for (auto resid : residForces)
		EnableItem(hPage, resid, enable ? TRUE : FALSE);

	SetCheck(hPage, IDC_OPT_VEC_WEIGHT, vecFlag & BFV_WEIGHT);
	SetCheck(hPage, IDC_OPT_VEC_THRUST, vecFlag & BFV_THRUST);
	SetCheck(hPage, IDC_OPT_VEC_LIFT, vecFlag & BFV_LIFT);
	SetCheck(hPage, IDC_OPT_VEC_DRAG, vecFlag & BFV_DRAG);
	SetCheck(hPage, IDC_OPT_VEC_SIDEFORCE, vecFlag & BFV_SIDEFORCE);
	SetCheck(hPage, IDC_OPT_VEC_TOTAL, vecFlag & BFV_TOTAL);
	SetCheck(hPage, IDC_OPT_VEC_TORQUE, vecFlag & BFV_TORQUE);
	SetCheck(hPage, IDC_OPT_VEC_LINSCL, !(vecFlag & BFV_LOGSCALE));
	SetCheck(hPage, IDC_OPT_VEC_LOGSCL, vecFlag & BFV_LOGSCALE);

	int scalePos = (int)(25.0 * (1.0 + 0.5 * log(Cfg()->CfgVisHelpPrm.scaleBodyForce) / log(2.0)));
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_VEC_SCALE), scalePos);
	int opacPos = (int)(Cfg()->CfgVisHelpPrm.opacBodyForce * 50.0);
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_VEC_OPACITY), opacPos);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Forces::OnInitDialog(QWidget *hPage)
{
	GAUGEPARAM gp = { 0, 50, GAUGEPARAM::LEFT, GAUGEPARAM::BLACK };
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_VEC_SCALE), &gp);
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_VEC_OPACITY), &gp);

	UpdateControls(hPage);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Forces::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_VEC:
	case IDC_OPT_VEC_WEIGHT:
	case IDC_OPT_VEC_THRUST:
	case IDC_OPT_VEC_LIFT:
	case IDC_OPT_VEC_DRAG:
	case IDC_OPT_VEC_SIDEFORCE:
	case IDC_OPT_VEC_TOTAL:
	case IDC_OPT_VEC_TORQUE:
	case IDC_OPT_VEC_LINSCL:
	case IDC_OPT_VEC_LOGSCL:
		if (notification == RESN_CLICKED) {
			OnItemClicked(hPage, ctrlId);
			return FALSE;
		}
		break;
	}
	return FALSE;
}

// ----------------------------------------------------------------------

void OptionsPage_Forces::OnItemClicked(QWidget *hPage, WORD ctrlId)
{
	bool check = (IsChecked(hPage, ctrlId));
	DWORD flag;
	switch (ctrlId) {
	case IDC_OPT_VEC:           flag = BFV_ENABLE;    break;
	case IDC_OPT_VEC_WEIGHT:    flag = BFV_WEIGHT;    break;
	case IDC_OPT_VEC_THRUST:    flag = BFV_THRUST;    break;
	case IDC_OPT_VEC_LIFT:      flag = BFV_LIFT;      break;
	case IDC_OPT_VEC_DRAG:      flag = BFV_DRAG;      break;
	case IDC_OPT_VEC_SIDEFORCE: flag = BFV_SIDEFORCE; break;
	case IDC_OPT_VEC_TOTAL:     flag = BFV_TOTAL;     break;
	case IDC_OPT_VEC_TORQUE:    flag = BFV_TORQUE;    break;
	case IDC_OPT_VEC_LINSCL:    flag = BFV_LOGSCALE; check = false; break;
	case IDC_OPT_VEC_LOGSCL: flag = BFV_LOGSCALE; check = true;  break;
	default:                 flag = 0;           break;
	}
	int& vecFlag = Cfg()->CfgVisHelpPrm.flagBodyForce;
	if (check) vecFlag |= flag;
	else       vecFlag &= ~flag;

	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Forces::OnHScroll(QWidget *hTab, int ctrlId, int request, int pos)
{
	switch (ctrlId) {
	case IDC_OPT_VEC_SCALE:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			Cfg()->CfgVisHelpPrm.scaleBodyForce = (float)pow(2.0, (pos - 25) * 0.08);
			return 0;
		}
		break;
	case IDC_OPT_VEC_OPACITY:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			Cfg()->CfgVisHelpPrm.opacBodyForce = (float)(pos * 0.02);
			return 0;
		}
		break;
	}
	return FALSE;
}

// ======================================================================

OptionsPage_Axes::OptionsPage_Axes(OptionsPageContainer* container)
	: OptionsPage(container)
{
}

// ----------------------------------------------------------------------

int OptionsPage_Axes::ResourceId() const
{
	return IDD_OPTIONS_FRAMEAXES;
}

// ----------------------------------------------------------------------

const char* OptionsPage_Axes::Name() const
{
	const char* name = "Object axes";
	return name;
}

// ----------------------------------------------------------------------

const HELPCONTEXT* OptionsPage_Axes::HelpContext() const
{
	static HELPCONTEXT hcontext = g_pOrbiter->DefaultHelpPage("/vh_coord.htm");
	return &hcontext;
}

// ----------------------------------------------------------------------

void OptionsPage_Axes::UpdateControls(QWidget *hPage)
{
	std::array<int, 10> residAxes = {
		IDC_OPT_CRD_VESSEL, IDC_OPT_CRD_CELBODY, IDC_OPT_CRD_BASE, IDC_OPT_CRD_NEGATIVE,
		IDC_OPT_CRD_SCALE, IDC_OPT_CRD_OPACITY,
		IDC_STATIC1, IDC_STATIC2, IDC_STATIC3, IDC_STATIC4
	};

	DWORD crdFlag = Cfg()->CfgVisHelpPrm.flagFrameAxes;
	bool enable = (crdFlag & FAV_ENABLE);
	SetCheck(hPage, IDC_OPT_CRD, enable);
	for (auto resid : residAxes)
		EnableItem(hPage, resid, enable ? TRUE : FALSE);

	SetCheck(hPage, IDC_OPT_CRD_VESSEL, crdFlag & FAV_VESSEL);
	SetCheck(hPage, IDC_OPT_CRD_CELBODY, crdFlag & FAV_CELBODY);
	SetCheck(hPage, IDC_OPT_CRD_BASE, crdFlag & FAV_BASE);
	SetCheck(hPage, IDC_OPT_CRD_NEGATIVE, crdFlag & FAV_NEGATIVE);

	int scalePos = (int)(25.0 * (1.0 + 0.5 * log(Cfg()->CfgVisHelpPrm.scaleFrameAxes) / log(2.0)));
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_CRD_SCALE), scalePos);
	int opacPos = (int)(Cfg()->CfgVisHelpPrm.opacFrameAxes * 50.0);
	oapiSetGaugePos(oapiResDlgItem(hPage, IDC_OPT_CRD_OPACITY), opacPos);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Axes::OnInitDialog(QWidget *hPage)
{
	GAUGEPARAM gp = { 0, 50, GAUGEPARAM::LEFT, GAUGEPARAM::BLACK };
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_CRD_SCALE), &gp);
	oapiSetGaugeParams(oapiResDlgItem(hPage, IDC_OPT_CRD_OPACITY), &gp);

	UpdateControls(hPage);

	return TRUE;
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Axes::OnCommand(QWidget *hPage, WORD ctrlId, WORD notification, QWidget *hCtrl)
{
	switch (ctrlId) {
	case IDC_OPT_CRD:
	case IDC_OPT_CRD_VESSEL:
	case IDC_OPT_CRD_CELBODY:
	case IDC_OPT_CRD_BASE:
	case IDC_OPT_CRD_NEGATIVE:
		if (notification == RESN_CLICKED) {
			OnItemClicked(hPage, ctrlId);
			return FALSE;
		}
		break;
	}
	return FALSE;
}

// ----------------------------------------------------------------------

void OptionsPage_Axes::OnItemClicked(QWidget *hPage, WORD ctrlId)
{
	bool check = (IsChecked(hPage, ctrlId));
	DWORD flag;
	switch (ctrlId) {
	case IDC_OPT_CRD:          flag = FAV_ENABLE;   break;
	case IDC_OPT_CRD_VESSEL:   flag = FAV_VESSEL;   break;
	case IDC_OPT_CRD_CELBODY:  flag = FAV_CELBODY;  break;
	case IDC_OPT_CRD_BASE:     flag = FAV_BASE;     break;
	case IDC_OPT_CRD_NEGATIVE: flag = FAV_NEGATIVE; break;
	default:                   flag = 0;            break;
	}
	int& crdFlag = Cfg()->CfgVisHelpPrm.flagFrameAxes;
	if (check) crdFlag |= flag;
	else       crdFlag &= ~flag;

	UpdateControls(hPage);
}

// ----------------------------------------------------------------------

BOOL OptionsPage_Axes::OnHScroll(QWidget *hTab, int ctrlId, int request, int pos)
{
	switch (ctrlId) {
	case IDC_OPT_CRD_SCALE:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			Cfg()->CfgVisHelpPrm.scaleFrameAxes = (float)pow(2.0, (pos - 25) * 0.08);
			return 0;
		}
		break;
	case IDC_OPT_CRD_OPACITY:
		switch (request) {
		case GAUGE_THUMBTRACK:
		case GAUGE_LINEDEC:
		case GAUGE_LINEINC:
			Cfg()->CfgVisHelpPrm.opacFrameAxes = (float)(pos * 0.02);
			return 0;
		}
		break;
	}
	return FALSE;
}
