// WineBridge helper: dialogs (DlgMgr.cpp and DialogWin.cpp of Orbiter 2024). The modules' dialogs are the helper's windows, with no
// render window to own them; their messages are dispatched when Linux pumps (H_PUMP)
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <string.h>
#include "wb_helper.h"

#define WM_USERMESSAGE (WM_USER+10)
#define DLG_CAPTIONBUTTON (DLG_CAPTIONCLOSE|DLG_CAPTIONHELP)

static bool g_running; // Orbiter's dialog manager is there: a simulation runs

INT_PTR OrbiterDefDialogProc (HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

namespace {

// ==================================================================
// DialogWin: a dialog of a module (no Update, no position kept)

class DialogWin {
public:
	DialogWin (HINSTANCE hInstance, HWND hParent, int resourceId, DLGPROC pDlg, DWORD flags, void *pContext);
	DialogWin (HINSTANCE hInstance, HWND hWindow, HWND hParent, DWORD flags);
	~DialogWin ();
	HWND OpenWindow ();
	void Message (DWORD msg, void *data);
	void ToggleShrink ();
	HWND GetHwnd () const { return hWnd; }
	HINSTANCE GetHinst () const { return hInst; }
	int GetResId () const { return resId; }
	void *GetContext () const { return context; }
	static DialogWin *GetDialogWin (HWND hDlg);
	bool AddTitleButton (DWORD msg, HBITMAP hBmp, DWORD flag);
	DWORD GetTitleButtonState (DWORD msg);
	bool SetTitleButtonState (DWORD msg, DWORD state);
	void PaintTitleButtons ();
	static bool Create_AddTitleButton (DWORD msg, HBITMAP hBmp, DWORD flag);
	static bool Create_SetTitleButtonState (DWORD msg, DWORD state);
	static INT_PTR CALLBACK DlgProc (HWND, UINT, WPARAM, LPARAM);
	static DialogWin *dlg_create;

private:
	HINSTANCE hInst;
	HWND hWnd;
	HWND hPrnt;
	int resId;
	void *context;
	DLGPROC dlgproc;
	DWORD flag;
	int psize;
	struct TitleBtn { DWORD DlgMsg; DWORD flag; HBITMAP hBmp; } tbtn[5];
};

DialogWin *DialogWin::dlg_create = 0;

DialogWin::DialogWin (HINSTANCE hInstance, HWND hParent, int resourceId, DLGPROC pDlg, DWORD flags, void *pContext)
{
	hInst   = hInstance;
	resId   = resourceId;
	flag    = flags;
	context = pContext;
	hPrnt   = hParent;
	hWnd    = NULL;
	dlgproc = (pDlg ? pDlg : DlgProc);
	psize   = 0;
	memset (tbtn, 0, 5*sizeof(TitleBtn));
}

DialogWin::DialogWin (HINSTANCE hInstance, HWND hWindow, HWND hParent, DWORD flags)
{
	hInst   = hInstance;
	resId   = 0;
	flag    = flags;
	context = 0;
	hWnd    = hWindow;
	hPrnt   = hParent;
	dlgproc = NULL;
	psize   = 0;
	memset (tbtn, 0, 5*sizeof(TitleBtn));
}

DialogWin::~DialogWin ()
{
	if (hWnd && IsWindow (hWnd)) DestroyWindow (hWnd); // not upstream (helper): a module may have destroyed it itself
}

HWND DialogWin::OpenWindow ()
{
	dlg_create = this;
	if (!hWnd) // otherwise window exists already
		hWnd = CreateDialogParamA (hInst, MAKEINTRESOURCEA(resId), hPrnt, dlgproc, (LPARAM)context); // not upstream (helper): no position kept, as a module's
	if (!hWnd) { dlg_create = 0; return NULL; } // not upstream (helper): no display, or no such resource
	SetWindowLongPtrA (hWnd, DWLP_USER, (LONG_PTR)this);
	RECT r;
	ShowWindow (hWnd, SW_SHOWNOACTIVATE);
	GetWindowRect (hWnd, &r);
	psize = r.bottom - r.top;
	dlg_create = 0;
	return hWnd;
}

INT_PTR CALLBACK DialogWin::DlgProc (HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (uMsg == WM_COMMAND && LOWORD(wParam) == IDCANCEL) { // DialogWin::OnCommand
		oapiCloseDialog (hDlg);
		return TRUE;
	}
	return OrbiterDefDialogProc (hDlg, uMsg, wParam, lParam);
}

void DialogWin::Message (DWORD msg, void *data)
{
	PostMessageA (hWnd, WM_USERMESSAGE, msg, (LPARAM)data);
}

void DialogWin::ToggleShrink ()
{
	RECT r;
	GetWindowRect (hWnd, &r);
	int hw = r.bottom - r.top;
	int h0 = GetSystemMetrics (SM_CYMIN);
	if (hw == h0) { // restore window
		hw = psize;
		SetWindowPos (hWnd, 0, r.left, r.top, r.right-r.left, hw, SWP_SHOWWINDOW);
	} else {
		psize = hw;
		SetWindowPos (hWnd, 0, r.left, r.top, r.right-r.left, h0, SWP_SHOWWINDOW);
	}
}

DialogWin *DialogWin::GetDialogWin (HWND hDlg)
{
	DialogWin *dlg = (DialogWin*)GetWindowLongPtrA (hDlg, DWLP_USER);
	if (!dlg)
		dlg = dlg_create;
	return dlg;
}

bool DialogWin::AddTitleButton (DWORD msg, HBITMAP hBmp, DWORD flag)
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

DWORD DialogWin::GetTitleButtonState (DWORD msg)
{
	for (int i = 0; i < 5; i++)
		if (tbtn[i].DlgMsg == msg)
			return (tbtn[i].flag & 0x80000000 ? 1:0);
	return 0;
}

bool DialogWin::SetTitleButtonState (DWORD msg, DWORD state)
{
	for (int i = 0; i < 5; i++)
		if (tbtn[i].DlgMsg == msg) {
			if (tbtn[i].flag & DLG_CB_TWOSTATE) {
				DWORD oldstate = (tbtn[i].flag & 0x80000000 ? 1:0);
				if (oldstate != state) {
					tbtn[i].flag ^= 0x80000000;
					PaintTitleButtons ();
					PostMessageA (hWnd, WM_COMMAND, MAKELONG (tbtn[i].DlgMsg, state), 0);
					return true;
				}
			}
			return false;
		}
	return false;
}

// not upstream (helper): Orbiter.exe's close and help button bitmap isn't in the helper; the module's own buttons are painted
void DialogWin::PaintTitleButtons ()
{
	if (!(flag & DLG_CAPTIONBUTTON)) return;
	RECT r;
	int x0, y0;
	GetWindowRect (hWnd, &r);
	if (GetWindowLongPtrA (hWnd, GWL_STYLE) & WS_THICKFRAME) {
		x0 = -GetSystemMetrics (SM_CYSIZEFRAME),  y0 = GetSystemMetrics (SM_CXSIZEFRAME);
	} else {
		x0 = -GetSystemMetrics (SM_CYFIXEDFRAME), y0 = GetSystemMetrics (SM_CXFIXEDFRAME);
	}
	x0 += r.right-r.left-15;
	HDC hDC = GetWindowDC (hWnd);
	HDC hDCsrc = CreateCompatibleDC (hDC);
	int i = 0;
	if (flag & DLG_CAPTIONCLOSE) { x0 -= 16; i++; }
	if (flag & DLG_CAPTIONHELP) { x0 -= 16; i++; }
	for (; i < 5; i++) {
		if (tbtn[i].DlgMsg && tbtn[i].hBmp) {
			SelectObject (hDCsrc, tbtn[i].hBmp);
			BitBlt (hDC, x0, y0, 15, 15, hDCsrc, 0, tbtn[i].flag & 0x80000000 ? 15:0, SRCCOPY);
			x0 -= 16;
		}
	}
	DeleteDC (hDCsrc);
	ReleaseDC (hWnd, hDC);
}

bool DialogWin::Create_AddTitleButton (DWORD msg, HBITMAP hBmp, DWORD flag)
{
	if (dlg_create) return dlg_create->AddTitleButton (msg, hBmp, flag);
	else            return false;
}

bool DialogWin::Create_SetTitleButtonState (DWORD msg, DWORD state)
{
	if (dlg_create) return dlg_create->SetTitleButtonState (msg, state);
	else            return false;
}

// ==================================================================
// the dialog list (DialogManager's entries)

DialogWin **g_dlg;
int g_ndlg, g_capdlg;

HWND AddEntry (DialogWin *dlg)
{
	if (g_ndlg == g_capdlg) {
		g_capdlg = g_capdlg ? g_capdlg * 2 : 16;
		g_dlg = (DialogWin **)realloc (g_dlg, g_capdlg * sizeof *g_dlg);
	}
	g_dlg[g_ndlg++] = dlg;
	HWND hWnd = dlg->OpenWindow ();
	if (!hWnd) { // not upstream (helper): the window couldn't be made
		for (int i = 0; i < g_ndlg; i++) if (g_dlg[i] == dlg) { memmove (g_dlg + i, g_dlg + i + 1, (g_ndlg - i - 1) * sizeof *g_dlg); g_ndlg--; break; }
		delete dlg;
	}
	return hWnd;
}

// remove dialog entry; a component that is 0 isn't compared
bool DelEntry (HWND hDlg, HINSTANCE hInst, int id)
{
	for (int i = 0; i < g_ndlg; i++) {
		DialogWin *dlg = g_dlg[i];
		if (hDlg && hDlg != dlg->GetHwnd()) continue;
		if (hInst && hInst != dlg->GetHinst()) continue;
		if (id && id != dlg->GetResId()) continue;
		memmove (g_dlg + i, g_dlg + i + 1, (g_ndlg - i - 1) * sizeof *g_dlg); // first, as the window's destruction may call back
		g_ndlg--;
		delete dlg;
		return true;
	}
	return false;
}

HWND IsEntry (HINSTANCE hInst, int id)
{
	for (int i = 0; i < g_ndlg; i++)
		if (g_dlg[i]->GetHinst() == hInst && g_dlg[i]->GetResId() == id)
			return g_dlg[i]->GetHwnd();
	return 0;
}

DialogWin *Entry (HWND hDlg)
{
	for (int i = 0; i < g_ndlg; i++) if (g_dlg[i]->GetHwnd() == hDlg) return g_dlg[i];
	return 0;
}

void Clear ()
{
	while (g_ndlg) DelEntry (g_dlg[g_ndlg - 1]->GetHwnd (), 0, 0);
}

} // namespace

// ==================================================================
// the API (OrbiterAPI.cpp, Orbiter.cpp): no dialog manager while no simulation runs

HWND oapiOpenDialog (HINSTANCE hDLLInst, int resourceId, DLGPROC msgProc, void *context)
{
	return oapiOpenDialogEx (hDLLInst, resourceId, msgProc, 0, context);
}

HWND oapiOpenDialogEx (HINSTANCE hDLLInst, int resourceId, DLGPROC msgProc, DWORD flag, void *context)
{
	if (!g_running) return NULL;
	if ((flag & DLG_ALLOWMULTI) == 0)
		if (IsEntry (hDLLInst, resourceId)) return NULL; // already open, and multiple instances not allowed
	return AddEntry (new DialogWin (hDLLInst, NULL, resourceId, msgProc, flag, context));
}

HWND oapiFindDialog (HINSTANCE hDLLInst, int resourceId)
{
	return g_running ? IsEntry (hDLLInst, resourceId) : NULL;
}

void oapiCloseDialog (HWND hDlg)
{
	if (g_running) DelEntry (hDlg, 0, 0);
}

void *oapiGetDialogContext (HWND hDlg)
{
	if (!g_running) return NULL;
	DialogWin *dlg = Entry (hDlg) ? (DialogWin*)GetWindowLongPtrA (hDlg, DWLP_USER) : 0; // not upstream (helper): only its own
	return (dlg ? dlg->GetContext() : 0);
}

bool oapiRegisterWindow (HINSTANCE hDLLInst, HWND hWnd, DWORD flag)
{
	if (!g_running || !hWnd) return false;
	if (g_ndlg == g_capdlg) {
		g_capdlg = g_capdlg ? g_capdlg * 2 : 16;
		g_dlg = (DialogWin **)realloc (g_dlg, g_capdlg * sizeof *g_dlg);
	}
	g_dlg[g_ndlg++] = new DialogWin (hDLLInst, hWnd, NULL, flag);
	return true;
}

bool oapiAddTitleButton (DWORD msgid, HBITMAP hBmp, DWORD flag)
{
	return g_running ? DialogWin::Create_AddTitleButton (msgid, hBmp, flag) : false;
}

DWORD oapiGetTitleButtonState (HWND hDlg, DWORD msgid)
{
	DialogWin *dlg = g_running ? Entry (hDlg) : 0;
	return dlg ? dlg->GetTitleButtonState (msgid) : 0;
}

bool oapiSetTitleButtonState (HWND hDlg, DWORD msgid, DWORD state)
{
	if (!g_running) return false;
	state = (state ? 1:0);
	if (DialogWin::Create_SetTitleButtonState (msgid, state)) return true;
	DialogWin *dlg = Entry (hDlg);
	return dlg ? dlg->SetTitleButtonState (msgid, state) : false;
}

// not upstream (helper): the focus-follows-mouse mode and the frame while a dialog is moved need Orbiter's render window
INT_PTR OrbiterDefDialogProc (HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg) {
	case WM_NCLBUTTONDBLCLK: {
		DialogWin *dlg = Entry (hDlg) ? (DialogWin*)GetWindowLongPtrA (hDlg, DWLP_USER) : 0;
		if (dlg) dlg->ToggleShrink();
		} break;
	}
	return FALSE;
}

INT_PTR oapiDefDialogProc (HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	return OrbiterDefDialogProc (hDlg, uMsg, wParam, lParam);
}

// Orbiter.exe's render window and video tab message functions, which graphics clients forward to: the helper has neither
static void SaidOnce (const char *what)
{
	static bool said[2];
	int i = what[0] == 'W' ? 0 : 1;
	if (!said[i]) { said[i] = true; wb::Log ("%s called: Orbiter's window isn't the helper's, so the message goes to Wine's default", what); }
}

__declspec(dllexport) LRESULT CALLBACK WndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SaidOnce ("WndProc");
	return DefWindowProcA (hWnd, uMsg, wParam, lParam);
}

__declspec(dllexport) BOOL CALLBACK LaunchpadVideoWndProc (HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	SaidOnce ("LaunchpadVideoWndProc");
	return FALSE;
}

// ==================================================================
// from Linux

// H_PUMP: the helper's messages, as Orbiter's Run loop dispatches them
void wb::Pump ()
{
	MSG msg;
	for (int n = 0; n < 256 && PeekMessageA (&msg, NULL, 0U, 0U, PM_REMOVE); n++) {
		if (msg.message == WM_QUIT) continue; // Orbiter's loop alone ends Orbiter
		TranslateMessage (&msg);
		DispatchMessageA (&msg);
	}
}

// H_DIALOGS: Orbiter's dialog manager was made (dialogs may open), cleared (they close), deleted; or it broadcast a message
void wb::ServeDialogs (WbReader &r)
{
	int what = r.i32 ();
	uint32_t msg = r.u32 (), data = r.u32 ();
	if (r.bad) return;
	switch (what) {
	case WD_MADE: g_running = true; break;
	case WD_CLEARED: Clear (); break;
	case WD_DELETED: Clear (); g_running = false; break;
	case WD_MESSAGE:
		for (int i = 0; i < g_ndlg; i++) if (g_dlg[i]->GetHwnd ()) g_dlg[i]->Message (msg, wb::Ptr (data));
		break;
	}
}

// the module is being freed: its dialogs go before its code
void wb::DialogsFreed (void *hModule)
{
	while (DelEntry (0, (HINSTANCE)hModule, 0)) {}
}
