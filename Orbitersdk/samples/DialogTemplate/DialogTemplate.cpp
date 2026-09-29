// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ==============================================================
//                 ORBITER MODULE: DialogTemplate
//                    Part of the ORBITER SDK
//
// DialogTemplate.cpp
//
// This module demonstrates how to build an Orbiter plugin which
// opens a Windows dialog box. This is a good starting point for
// your own dialog-based addons.
// ==============================================================

// STRICT left out: windows.h handle type-checking switch
#define ORBITER_MODULE
// windows.h left out: the dialog controls are Qt widgets
#include "Orbitersdk.h"
#include "OrbiterResource.h"
#include "resource.h"
#include <stdio.h>
#include <strings.h>
#include <functional>
#include <QEvent>

// ==============================================================
// Global variables
// ==============================================================

void *g_hInst;      // module instance handle
DWORD g_dwCmd;      // custom function identifier
int myprm = 0;

// ==============================================================
// Local prototypes
// ==============================================================

void OpenDlgClbk (void *context);
void MsgProc (QWidget*, void*);

// ==============================================================
// API interface
// ==============================================================

// ==============================================================
// This function is called when Orbiter starts or when the module
// is activated.

DLLCLBK void InitModule (void *hDLL)
{
	g_hInst = hDLL; // remember the instance handle

	// To allow the user to open our new dialog box, we create
	// an entry in the "Custom Functions" list which is accessed
	// in Orbiter via Ctrl-F4.
	g_dwCmd = oapiRegisterCustomCmd ("My dialog",
		"Opens a test dialog box which doesn't do much.",
		OpenDlgClbk, NULL);
}

// ==============================================================
// This function is called when Orbiter shuts down or when the
// module is deactivated

DLLCLBK void ExitModule (void *hDLL)
{
	// Unregister the custom function in Orbiter
	oapiUnregisterCustomCmd (g_dwCmd);
}


// ==============================================================
// Write some parameters to the scenario file

DLLCLBK void opcSaveState (FILEHANDLE scn)
{
	oapiWriteScenario_int (scn, "Param", myprm);
}

// ==============================================================
// Read custom parameters from scenario

DLLCLBK void opcLoadState (FILEHANDLE scn)
{
	char *line;
	while (oapiReadScenario_nextline (scn, line)) {
		if (!strncasecmp (line, "Param", 5)) {
			sscanf (line+5, "%d", &myprm);
		}
	}
}

// ==============================================================
// Open the dialog window

void OpenDlgClbk (void *context)
{
	QWidget *hDlg = oapiOpenDialog (g_hInst, IDD_MYDIALOG, MsgProc);
	// Don't use a standard Windows function like CreateWindow to
	// open the dialog box, because it won't work in fullscreen mode
}

// ==============================================================
// Close the dialog

void CloseDlg (QWidget *hDlg)
{
	oapiCloseDialog (hDlg);
}

// not upstream: WM_DESTROY comes while the controls still exist; here that is the dialog's deferred delete
class DestroyHook: public QObject {
public:
	DestroyHook (QWidget *hWnd, std::function<void()> f): QObject (hWnd), onDestroy (f) { hWnd->installEventFilter (this); }
	bool eventFilter (QObject *o, QEvent *e) override {
		if (e->type() == QEvent::DeferredDelete) onDestroy();
		return false;
	}
private:
	std::function<void()> onDestroy;
};

// ==============================================================
// Windows message handler for the dialog box

void MsgProc (QWidget *hDlg, void *context)
{
	char name[256];

	// WM_INITDIALOG
		sprintf (name, "%d", myprm);
		oapiSetDlgItemText (hDlg, IDC_REMEMBER, name);

	// WM_DESTROY
	new DestroyHook (hDlg, [hDlg]() {
		char name[256];
		oapiGetDlgItemText (hDlg, IDC_REMEMBER, name, 256);
		sscanf (name, "%d", &myprm);
	});

	// WM_COMMAND
	oapiConnectDlgCommands (hDlg, [hDlg](int id, int code, QWidget *hCtrl) {
		char name[256];
		switch (id) {

		case IDC_WHOAMI:  // user pressed dialog button
			// display the focus vessel name
			oapiGetObjectName (oapiGetFocusObject(), name, 256);
			oapiSetDlgItemText (hDlg, IDC_IAM, name);
			return;

		case IDCANCEL: // dialog closed by user
			CloseDlg (hDlg);
			return;
		}
	});
	// oapiDefDialogProc left out: oapiOpenDialog wires the default dialog behaviour
}
