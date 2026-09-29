// Copyright (c) Martin Schweiger
// Licensed under the MIT License

//=============================================================================
// AboutTab class
//=============================================================================

#include "Orbiter.h"
#include "TabAbout.h"
#include "Util.h"
#include "Help.h"
#include "resource.h"
#include "about.hpp"
#include "ResDialog.h"
#include <QDesktopServices>
#include <QDialog>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QUrl>

//-----------------------------------------------------------------------------
// AboutTab class

orbiter::AboutTab::AboutTab (const LaunchpadDialog *lp): LaunchpadTab (lp)
{
}

//-----------------------------------------------------------------------------

bool orbiter::AboutTab::OpenHelp ()
{
	OpenTabHelp ("tab_about");
	return true;
}

//-----------------------------------------------------------------------------

void orbiter::AboutTab::Create ()
{
	hTab = CreateTab (IDD_PAGE_ABT);

	DlgItem<QLabel> (hTab, IDC_ABT_TXT_NAME)->setText (NAME1);
	DlgItem<QLabel> (hTab, IDC_ABT_TXT_BUILDDATE)->setText (SIG4);
	DlgItem<QLabel> (hTab, IDC_ABT_TXT_CPR)->setText (SIG1B);
	DlgItem<QLabel> (hTab, IDC_ABT_TXT_WEBADDR)->setText (SIG2 "\n" SIG5 "\n" SIG6);
	DlgItem<QListWidget> (hTab, IDC_ABT_LBOX_COMPONENT)->addItem (
		"D3D9Client module by Jarmo Nikkanen and Peter Schneider"
	);
	DlgItem<QListWidget> (hTab, IDC_ABT_LBOX_COMPONENT)->addItem (
		"XRSound module Copyright (c) Doug Beachy"
	);
}

//-----------------------------------------------------------------------------

BOOL orbiter::AboutTab::OnInitDialog(QWidget *hWnd)
{
	// WM_COMMAND
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_ABT_WEB), &QPushButton::clicked, hWnd, []() {
		QDesktopServices::openUrl (QUrl ("http://orbit.medphys.ucl.ac.uk/"));
	});
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_ABT_DISCLAIM), &QPushButton::clicked, hWnd, [this]() {
		QDialog *dlg = qobject_cast<QDialog*> (oapiCreateResDialog (AppInstance(), IDD_MSG, LaunchpadWnd()));
		if (!dlg) return;
		AboutProc (dlg, IDT_DISCLAIMER);
		dlg->exec(); // DialogBoxParam
		delete dlg;
	});
	QObject::connect (DlgItem<QPushButton> (hWnd, IDC_ABT_CREDIT), &QPushButton::clicked, hWnd, [hWnd]() {
		::OpenHelp(hWnd, "html\\Credit.chm", "Credit");
	});
	return FALSE;
}

//-----------------------------------------------------------------------------
// Name: AboutProc()
// Desc: Minimal set-up function for the about box
//-----------------------------------------------------------------------------
void orbiter::AboutTab::AboutProc (QWidget *hWnd, int textId)
{
	// WM_INITDIALOG
	const RESDATA *txt = oapiFindResData (NULL, "TEXT", textId);
	if (txt)
		DlgItem<QPlainTextEdit> (hWnd, IDC_MSG)->setPlainText (QString::fromLatin1 ((const char*)txt->data, txt->size));
	// WM_COMMAND
	QDialog *dlg = qobject_cast<QDialog*> (hWnd);
	QObject::connect (DlgItem<QPushButton> (hWnd, IDOK), &QPushButton::clicked, dlg, &QDialog::accept);
}

