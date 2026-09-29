// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ======================================================================
// Base class for dialog windows
// ======================================================================

#ifndef __DIALOGWIN_H
#define __DIALOGWIN_H

#include "OrbiterPlatform.h"
#include "GraphicsAPI.h"

class QObject;

const BOOL MSG_DEFAULT = -1;

class DialogWin {
public:
	// Creates a new instance of a dialog window class, but does not
	// actually create the window itself yet
	DialogWin (void *hInstance, QWindow *hParent, int resourceId,
		DLGINIT pDlg, DWORD flags, void *pContext);

	// Creates a dialog window instance for an already existing window
	DialogWin (void *hInstance, QWidget *hWindow, QWindow *hParent, DWORD flags);

	virtual ~DialogWin();

	// Opens the window and returns its handle
	virtual QWidget *OpenWindow ();

	virtual void Message (DWORD msg, void *data);
	virtual void ToggleShrink ();

	/**
	 * \brief Request an update at every time step
	 * \return If true, the Update method is called at each time step for all
	 *    open dialogs. If false, Update has to be called explicitly whenever
	 *    the dialog should update itself.
	 * \default true
	 */
	virtual bool UpdateContinuously() const { return true; }

	QWidget *GetHwnd () const { return hWnd; }
	void *GetHinst () const { return hInst; }
	int GetResId () const { return resId; }
	void *GetContext () const { return context; }
	static DialogWin *GetDialogWin (QWidget *hDlg);

	virtual void Update ();

	// handlers of the dialog window's events; the controls' own events are connected by the DLGINIT function
	virtual BOOL OnInitDialog (QWidget *hDlg, void *context) { return MSG_DEFAULT; }
	virtual BOOL OnCommand (QWidget *hDlg, WORD id, WORD code, QWidget *hControl);
	virtual BOOL OnUserMessage (QWidget *hDlg, DWORD msg, void *data) { return MSG_DEFAULT; }
	virtual BOOL OnSize (QWidget *hDlg, int w, int h);
	virtual BOOL OnMove (QWidget *hDlg, int x, int y);

	bool AddTitleButton (DWORD msg, QImage *hBmp, DWORD flag);
	DWORD GetTitleButtonState (DWORD msg);
	bool SetTitleButtonState (DWORD msg, DWORD state);
	void PaintTitleButtons ();
	bool CheckTitleButtons (const POINT &pt);

	static bool Create_AddTitleButton (DWORD msg, QImage *hBmp, DWORD flag);
	static bool Create_SetTitleButtonState (DWORD msg, DWORD state);

	// Default event handler of the dialog window (DlgProc counterpart); true if handled
	static bool DlgProc (QWidget *hDlg, QEvent *event);

protected:
	static DialogWin *dlg_create;

	oapi::GraphicsClient *gc; // graphics client instance
	void *hInst;              // instance handle
	QWidget *hWnd;            // dialog window handle
	QWindow *hPrnt;           // parent window handle
	int  resId;               // dialog resource identifier
	void *context;            // dialog context pointer
	RECT *pos;                // window position; needs to be assigned in constructor of derived classes
	DLGINIT dlgproc;          // control set-up function of the module
	DWORD flag;               // flags
	int psize;                // window height
	QObject *events;          // routes the dialog window's events to DlgProc

	struct TitleBtn {         // custom buttons in title bar
		DWORD DlgMsg;
		DWORD flag;
		QImage *hBmp;
	} tbtn[5];
};

#endif // !__DIALOGWIN_H
