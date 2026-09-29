// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// OptionsTab class
//=============================================================================

#include "TabOptions.h"
#include "Help.h"
#include "resource.h"
#include "ResDialog.h"

//=============================================================================

orbiter::OptionsTab::OptionsTab(const LaunchpadDialog* lp)
	: LaunchpadTab(lp)
	, OptionsPageContainer(OptionsPageContainer::LAUNCHPAD, lp->Cfg())
{
}

//-----------------------------------------------------------------------------

void orbiter::OptionsTab::Create()
{
	hTab = CreateTab(IDD_PAGE_OPT);
}

//-----------------------------------------------------------------------------

bool orbiter::OptionsTab::OpenHelp()
{
	const HELPCONTEXT* hc = (CurrentPage() ? CurrentPage()->HelpContext() : nullptr);
	if (hc) ::OpenHelp(LaunchpadWnd(), hc->helpfile, hc->topic);
	return true;
}

//-----------------------------------------------------------------------------

void orbiter::OptionsTab::LaunchpadShowing(bool show)
{
	if (show) UpdatePages(true);
}

// ----------------------------------------------------------------------

void orbiter::OptionsTab::SetConfig(Config* cfg)
{
	UpdateConfig();
}

//-----------------------------------------------------------------------------

BOOL orbiter::OptionsTab::OnInitDialog(QWidget *hWnd)
{
	SetWindowHandles(hWnd, oapiResDlgItem(hWnd, IDC_OPT_SPLIT), oapiResDlgItem(hWnd, IDC_OPT_PAGELIST), oapiResDlgItem(hWnd, IDC_OPT_PAGECONTAINER));
	CreatePages();
	ExpandAll();
	return TRUE;
}

//-----------------------------------------------------------------------------

BOOL orbiter::OptionsTab::OnSize(int w, int h)
{
	QWidget *split = oapiResDlgItem(hTab, IDC_OPT_SPLIT);
	split->lower(); // HWND_BOTTOM
	split->resize(w, h);

	return FALSE;
}

// WM_NOTIFY of the page list is connected in SetWindowHandles
