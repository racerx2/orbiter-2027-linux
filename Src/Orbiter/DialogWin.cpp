// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#include "DialogWin.h"
#include "DlgMgr.h"
#include "OrbiterAPI.h"
#include "OrbiterResource.h"
#include "Orbiter.h"
#include "resource.h"
#include "Log.h"
#include <QKeyEvent>
#include <QMoveEvent>
#include <QPointer>
#include <QResizeEvent>
#include <QWidget>
#include <QWindow>

#define DLG_CAPTIONBUTTON (DLG_CAPTIONCLOSE|DLG_CAPTIONHELP)

extern Orbiter *g_pOrbiter;

DialogWin *DialogWin::dlg_create = 0;

// routes the events of a dialog window to DialogWin::DlgProc (window procedure hook)
class DialogEvents: public QObject {
public:
	DialogEvents (QWidget *hDlg): QObject (hDlg) { hDlg->installEventFilter (this); }
	bool eventFilter (QObject *obj, QEvent *event) override
	{
		QWidget *w = qobject_cast<QWidget*> (obj);
		return (w ? DialogWin::DlgProc (w, event) : false);
	}
};

// ======================================================================

DialogWin::DialogWin (void *hInstance, QWindow *hParent, int resourceId,
					  DLGINIT pDlg, DWORD flags, void *pContext)
{
	gc      = g_pOrbiter->GetGraphicsClient();
	hInst   = hInstance;
	resId   = resourceId;
	flag    = flags;
	context = pContext;
	hPrnt   = hParent;
	hWnd    = NULL;
	pos     = NULL;
	dlgproc = pDlg; // without a module function, OnInitDialog sets up the controls
	events  = NULL;

	memset (tbtn, 0, 5*sizeof(TitleBtn));
	//int i = 0;
	//if (flag & DLG_CAPTIONCLOSE) tbtn[i++].DlgMsg = IDCANCEL;
	//if (flag & DLG_CAPTIONHELP)  tbtn[i++].DlgMsg = IDHELP;
}

// ======================================================================

DialogWin::DialogWin (void *hInstance, QWidget *hWindow, QWindow *hParent, DWORD flags)
{
	gc      = g_pOrbiter->GetGraphicsClient();
	hInst   = hInstance;
	resId   = 0;
	flag    = flags;
	context = 0;
	hWnd    = hWindow;
	hPrnt   = hParent;
	pos     = NULL;
	dlgproc = NULL;
	events  = NULL;

	memset (tbtn, 0, 5*sizeof(TitleBtn));
	//int i = 0;
	//if (flag & DLG_CAPTIONCLOSE) tbtn[i++].DlgMsg = IDCANCEL;
	//if (flag & DLG_CAPTIONHELP)  tbtn[i++].DlgMsg = IDHELP;
}

// ======================================================================

DialogWin::~DialogWin ()
{
	if (hWnd) {
		// DestroyWindow; deferred, since the request may come from a signal of one of the dialog's own controls
		if (events) hWnd->removeEventFilter (events);
		hWnd->setProperty ("DialogWin", QVariant());
		hWnd->hide();
		hWnd->deleteLater();
	}
}

// ======================================================================

QWidget *DialogWin::OpenWindow ()
{
	bool newwin = false;
	dlg_create = this; // is this still necessary ?

	if (gc) gc->clbkPreOpenPopup();

	if (!hWnd) { // otherwise window exists already
		hWnd = oapiCreateResDialog (hInst, resId, NULL, hPrnt);
		if (!hWnd) {
			LOGOUT_ERR ("Dialog resource %d not found", resId);
			dlg_create = 0;
			return NULL;
		}
		newwin = true;
	}
	hWnd->setProperty ("DialogWin", QVariant::fromValue ((void*)this)); // DWLP_USER
	if (!events) events = new DialogEvents (hWnd);
	if (newwin) {
		// WM_INITDIALOG
		if (dlgproc) dlgproc (hWnd, context);
		else OnInitDialog (hWnd, context);
	}
	if (newwin && pos && pos->right-pos->left) {
		if (hWnd->minimumSize() != hWnd->maximumSize()) // WS_SIZEBOX
			hWnd->setGeometry (pos->left, pos->top, pos->right-pos->left, pos->bottom-pos->top);
		else
			hWnd->move (pos->left, pos->top);
	}

	hWnd->setAttribute (Qt::WA_ShowWithoutActivating); // SW_SHOWNOACTIVATE
	hWnd->show();
	psize = hWnd->frameGeometry().height();

	dlg_create = 0;

	return hWnd;
}

// ======================================================================

void DialogWin::Update ()
{
}

// ======================================================================

bool DialogWin::DlgProc (QWidget *hDlg, QEvent *event)
{
	DialogWin *dlg = GetDialogWin (hDlg);
	if (!dlg) return false;
	switch (event->type()) {
	case QEvent::Move: {
		QPoint p = static_cast<QMoveEvent*> (event)->pos();
		dlg->OnMove (hDlg, p.x(), p.y());
		} break;
	case QEvent::Resize: {
		QSize s = static_cast<QResizeEvent*> (event)->size();
		dlg->OnSize (hDlg, s.width(), s.height());
		} break;
	case QEvent::Close: // title bar close button: WM_COMMAND IDCANCEL
		event->ignore();
		dlg->OnCommand (hDlg, IDCANCEL, 0, NULL);
		return true;
	case QEvent::KeyPress:
		if (static_cast<QKeyEvent*> (event)->key() == Qt::Key_Escape) { // IDCANCEL from the keyboard
			dlg->OnCommand (hDlg, IDCANCEL, 0, NULL);
			return true;
		}
		break;
	default:
		break;
	}
	return OrbiterDefDialogProc (hDlg, event);
}

// ======================================================================

BOOL DialogWin::OnCommand (QWidget *hDlg, WORD id, WORD code, QWidget *hControl)
{
	switch (id) {
	case IDCANCEL:
		g_pOrbiter->CloseDialog (hDlg);
		return TRUE;
	}
	return MSG_DEFAULT;
}

// ======================================================================

static void WindowRect (QWidget *hWnd, RECT *r)
{
	QRect g = hWnd->frameGeometry();
	r->left = g.left(), r->top = g.top(), r->right = g.left()+g.width(), r->bottom = g.top()+g.height();
}

int DialogWin::OnSize (QWidget *hWnd, int w, int h)
{
	if (pos) WindowRect (hWnd, pos);
	return 0;
}

// ======================================================================

int DialogWin::OnMove (QWidget *hWnd, int x, int y)
{
	if (pos) WindowRect (hWnd, pos);
	return 0;
}

// ======================================================================

void DialogWin::Message (DWORD msg, void *data)
{
	// PostMessage: delivered from the event loop, if the dialog is still open then
	QPointer<QWidget> w (hWnd);
	QMetaObject::invokeMethod (hWnd, [w, msg, data]() {
		DialogWin *dlg = (w ? (DialogWin*)w->property ("DialogWin").value<void*>() : NULL);
		if (dlg) dlg->OnUserMessage (w, msg, data);
	}, Qt::QueuedConnection);
}

// ======================================================================

void DialogWin::ToggleShrink ()
{
	QRect r = hWnd->geometry();
	int hw = r.height();
	int h0 = 1; // SM_CYMIN: no client area left below the title bar
	bool fixed = (hWnd->minimumHeight() == hWnd->maximumHeight());
	if (hw == h0) { // restore window
		hw = psize;
	} else {
		psize = hw;
		hw = h0;
	}
	if (fixed) hWnd->setFixedHeight (hw);
	else hWnd->resize (r.width(), hw);
	hWnd->show();
}

// ======================================================================

DialogWin *DialogWin::GetDialogWin (QWidget *hDlg)
{
	DialogWin *dlg = (hDlg ? (DialogWin*)hDlg->property ("DialogWin").value<void*>() : NULL);
	if (!dlg)
		dlg = dlg_create;
	return dlg;
}

// ======================================================================

bool DialogWin::AddTitleButton (DWORD msg, QImage *hBmp, DWORD flag)
{
	for (int i = 0; i < 5; i++) {
		if (tbtn[i].DlgMsg == 0) {
			tbtn[i].DlgMsg = msg;
			tbtn[i].hBmp = hBmp;
			tbtn[i].flag = flag;
			return true;
		}
	}
	return false;
}

// ======================================================================

DWORD DialogWin::GetTitleButtonState (DWORD msg)
{
	for (int i = 0; i < 5; i++)
		if (tbtn[i].DlgMsg == msg)
			return (tbtn[i].flag & 0x80000000 ? 1:0);
	return 0;
}

// ======================================================================

bool DialogWin::SetTitleButtonState (DWORD msg, DWORD state)
{
	for (int i = 0; i < 5; i++)
		if (tbtn[i].DlgMsg == msg) {
			if (tbtn[i].flag & DLG_CB_TWOSTATE) {
				DWORD oldstate = (tbtn[i].flag & 0x80000000 ? 1:0);
				if (oldstate != state) {
					tbtn[i].flag ^= 0x80000000;
					PaintTitleButtons ();
					// PostMessage WM_COMMAND
					QPointer<QWidget> w (hWnd);
					WORD id = (WORD)tbtn[i].DlgMsg;
					QMetaObject::invokeMethod (hWnd, [w, id, state]() {
						DialogWin *dlg = (w ? (DialogWin*)w->property ("DialogWin").value<void*>() : NULL);
						if (dlg) dlg->OnCommand (w, id, (WORD)state, NULL);
					}, Qt::QueuedConnection);
					return true;
				}
			}
			return false;
		}
	return false;
}

// ======================================================================

void DialogWin::PaintTitleButtons ()
{
	// The title bar belongs to the window manager, so the buttons are not drawn into it
	// (upstream's WM_NCPAINT hook that painted them is disabled as well)
	if (!(flag & DLG_CAPTIONBUTTON)) return;
}

// ======================================================================

bool DialogWin::CheckTitleButtons (const POINT &pt)
{
	// no title bar buttons are drawn (see PaintTitleButtons), so none can be hit
	if (!(flag & DLG_CAPTIONBUTTON)) return false;
	return false;
}

// ======================================================================

bool DialogWin::Create_AddTitleButton (DWORD msg, QImage *hBmp, DWORD flag)
{
	if (dlg_create) return dlg_create->AddTitleButton (msg, hBmp, flag);
	else            return false;
}

// ======================================================================

bool DialogWin::Create_SetTitleButtonState (DWORD msg, DWORD state)
{
	if (dlg_create) return dlg_create->SetTitleButtonState (msg, state);
	else            return false;
}
