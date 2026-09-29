// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// Launchpad tab implementations
//=============================================================================

#include "LpadTab.h"
#include "Launchpad.h"
#include "Log.h"
#include "Help.h"
#include "resource.h"
#include "ResDialog.h"
#include <QResizeEvent>
#include <algorithm>

using std::max;

//-----------------------------------------------------------------------------
// LaunchpadTab base class

orbiter::LaunchpadTab::LaunchpadTab (const LaunchpadDialog *lp)
{
	pLp = lp;
	pCfg = lp->Cfg();
	hTab = NULL;
	bActive = false;
	nitem = 0;
	item = NULL;
	itempos = NULL;
}

//-----------------------------------------------------------------------------

orbiter::LaunchpadTab::~LaunchpadTab ()
{
	if (hTab) delete hTab;
	if (nitem) {
		delete []item;
		item = NULL;
		delete []itempos;
		itempos = NULL;
	}
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadTab::Show ()
{
	if (hTab) hTab->show();
	bActive = true;
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadTab::Hide ()
{
	if (hTab) hTab->hide();
	bActive = false;
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadTab::OpenTabHelp(const char* topic)
{
	::OpenDefaultHelp(LaunchpadWnd(), topic);
}

//-----------------------------------------------------------------------------

void orbiter::LaunchpadTab::TabAreaResized(int w, int h)
{
	if (hTab) {
		if (DynamicSize())
			hTab->resize(w, h);
		else {
			int x0 = max(0, (w - hTab->width()) / 2);
			int y0 = max(0, (h - hTab->height()) / 2);
			hTab->move(x0, y0);
		}
	}
}

//-----------------------------------------------------------------------------

QWidget *orbiter::LaunchpadTab::CreateTab (int resid)
{
	QWidget *hT = oapiCreateResDialog (AppInstance(), resid, pLp->HTabContainer());
	new EventHook (hT, [this](QObject *obj, QEvent *event) { return TabProc (static_cast<QWidget*> (obj), event); });
	OnInitDialog (hT); // WM_INITDIALOG

	pos0.left = pos0.top = 0;
	pos0.right = hT->width(), pos0.bottom = hT->height();
	QPoint d = hT->mapTo (LaunchpadWnd(), QPoint (0, 0));
	int dx = d.x(), dy = d.y();
	pos0.left += dx, pos0.right += dx;
	pos0.top += dy, pos0.bottom += dy;

	return hT;
}

//-----------------------------------------------------------------------------

BOOL orbiter::LaunchpadTab::OnSize(int w, int h)
{
	if (nitem) {
		int dx = max(0, (w - (int)(pos0.right - pos0.left)) / 2);
		int dy = max(0, (h - (int)(pos0.bottom - pos0.top)) / 2);
		for (int i = 0; i < nitem; i++) {
			oapiResDlgItem(hTab, item[i])->move(itempos[i].x + dx, itempos[i].y + dy);
		}
		return FALSE;
	}
	return TRUE;
}

//-----------------------------------------------------------------------------

bool orbiter::LaunchpadTab::TabProc (QWidget *hWnd, QEvent *event)
{
	switch (event->type()) {
	case QEvent::Resize: {
		QSize s = static_cast<QResizeEvent*> (event)->size();
		OnSize(s.width(), s.height());
		} return false;
	default:
		return OnMessage(hWnd, event);
	}
	return false;
}
