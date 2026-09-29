// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// Date.cpp : Defines the class behaviors for the application.
//

#include "StdAfx.h"
#include "Date.h"
#include "DateDlg.h"
#include <clocale>

// DEBUG_NEW (_DEBUG) left out: MFC's debug allocator

/////////////////////////////////////////////////////////////////////////////
// CDateApp

// BEGIN_MESSAGE_MAP left out: its only entry, ID_HELP -> CWinApp::OnHelp, opens the app's WinHelp file, and Date has none

/////////////////////////////////////////////////////////////////////////////
// CDateApp construction

CDateApp::CDateApp()
{
}

/////////////////////////////////////////////////////////////////////////////
// The one and only CDateApp object

CDateApp theApp;

/////////////////////////////////////////////////////////////////////////////
// CDateApp initialization

BOOL CDateApp::InitInstance()
{
	// Standard initialization

	CDateDlg dlg;
	m_pMainWnd = &dlg;
	int nResponse = dlg.DoModal();
	if (nResponse == IDOK)
	{
	}
	else if (nResponse == IDCANCEL)
	{
	}

	// Since the dialog has been closed, return FALSE so that we exit the
	//  application, rather than start the application's message pump.
	return FALSE;
}

// not upstream: main() stands in for MFC's WinMain (AfxWinMain): InitInstance, then the message pump if it returns TRUE
int main(int argc, char *argv[])
{
	QApplication app(argc, argv);
	setlocale(LC_ALL, "C"); // Qt takes the environment locale; the number texts need "C" as on Windows
	if (theApp.InitInstance())
		app.exec(); // CWinApp::Run
	return 0;
}
