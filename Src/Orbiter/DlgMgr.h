// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __DLGMGR_H
#define __DLGMGR_H

#include "OrbiterPlatform.h"

#include "DialogWin.h"
#include "Orbiter.h"
#include <list>
#include "imgui.h"
#include "imgui_extras.h"
class ImGuiDialog;

namespace oapi { class GraphicsClient; }
extern Orbiter *g_pOrbiter;

struct DIALOGENTRY {
	DialogWin *dlg;
	struct DIALOGENTRY *prev, *next;
};

class DialogManager {
public:
	DialogManager(Orbiter *orbiter, QWindow *hAppWnd);
	~DialogManager();

	void Init (QWindow *hAppWnd);
	void Clear ();

	inline QWidget *OpenDialog (void *hInst, int id, QWindow *hParent, DLGINIT pDlg, void *context)
	{ return OpenDialogEx (hInst, id, hParent, pDlg, 0, context); }

	QWidget *OpenDialogEx (void *hInst, int id, QWindow *hParent, DLGINIT pDlg, DWORD flag, void *context);

	bool CloseDialog (QWidget *hDlg);

	void *GetDialogContext (QWidget *hDlg);
	inline DWORD Size() const { return nEntry; }
	QWidget *GetNextEntry (QWidget *hWnd) const;

	bool AddTitleButton (DWORD msg, QImage *hBmp, DWORD flag);
	DWORD GetTitleButtonState (QWidget *hDlg, DWORD msg);
	bool SetTitleButtonState (QWidget *hDlg, DWORD msg, DWORD state);

	DIALOGENTRY *AddWindow (void *hInst, QWidget *hWnd, QWindow *hParent, DWORD flag);

	QWidget *AddEntry (void *hInst, int id, QWindow *hParent, DLGINIT pDlg, DWORD flag, void *context);
	QWidget *AddEntry (DialogWin *dlg);

	bool DelEntry (QWidget *hDlg, void *hInst, int id);
	// remove dialog entry. If either 'hDlg' or 'id' is 0,
	// only the other component is checked

	QWidget *IsEntry (void *hInst, int id);
	// Returns window handle of dialog with identifier 'id' if it is in the list
	// Otherwise returns 0

	inline DWORD GetDlgList (QWidget *const **hDlgList) const
	{ *hDlgList = DlgList; return nList; }
	// Returns current dialog window list

	void UpdateDialogs();
	// periodic dialog updates

	void BroadcastMessage (DWORD msg, void *data);
	// broadcast a message to all open dialog windows, using the WM_USER+10 channel

private:
	void AddList (QWidget *hWnd);
	void DelList (QWidget *hWnd);
	// add/remove window handle from window list

	DWORD nEntry;
	DIALOGENTRY *firstEntry, *lastEntry;
	mutable DIALOGENTRY *searchEntry;

	QWidget **DlgList;
	DWORD nList, nListBuf;

	Orbiter *pOrbiter;
	oapi::GraphicsClient *gc;
	QWindow *hWnd;

	// ====================================================================
	// Tread management for dialog thread
	// ====================================================================

public:
	void OpenDialogAsync (void *hInst, int id, QWindow *hParent, DLGINIT pDlg, DWORD flag, void *context);

protected:
	void StartDialogThread ();
	// start the dialog handler thread (during render window creation)

	void DestroyDialogThread ();
	// kill the dialog handler thread (during render window destruction)

	void AddEntryAsync (void *hInst, int id, QWindow *hParent, DLGINIT pDlg, DWORD flag, void *context);

	// ====================================================================
	// End tread management
	// ====================================================================


	// ====================================================================
	// ImGui management
	// ====================================================================
	std::list<ImGuiDialog*> DlgImGuiList;
public:
	// Make sure that a dialog of type DlgType is open and return a pointer to it.
	// This opens the dialog if not yet present.
	// Use this function for dialogs that should only have a single instance
	template<typename T, std::enable_if_t<std::is_base_of_v<ImGuiDialog, T>, bool> = true>
	T* EnsureEntry()
	{
		T* dlg = EntryExists<T>();
		if(!dlg)
			dlg = MakeEntry<T>();
		if(dlg)
			dlg->Activate();
		return dlg;
	}

	// Create a new instance of dialog type DlgType and return a pointer to it.
	// This opens a new dialog, even if one of this type was open already.
	// Use this function for dialogs that can have multiple instances.
	template<typename T, std::enable_if_t<std::is_base_of_v<ImGuiDialog, T>, bool> = true>
	T* MakeEntry()
	{
		T* pDlg = new T();
		AddEntry(pDlg);
		return pDlg;
	}

	// Returns a pointer to the first instance of dialog type DlgType,
	// or 0 if no instance exists.
	template<typename T, std::enable_if_t<std::is_base_of_v<ImGuiDialog, T>, bool> = true>
	T* EntryExists()
	{
		for (auto& e : DlgImGuiList) {
			T *dlg = dynamic_cast<T *>(e);
			if(dlg) return dlg;
		}
		return nullptr;
	}

	void AddEntry(ImGuiDialog* dlg)
	{
		for (auto& e : DlgImGuiList) {
			if (e == dlg) {
				return;
			}
		}
		DlgImGuiList.push_back(dlg);
	}

	bool DelEntry(ImGuiDialog* dlg)
	{
		for (auto it = DlgImGuiList.begin(); it != DlgImGuiList.end(); ) {
			if (*it == dlg) {
				it = DlgImGuiList.erase(it);
				return true;
			}
			else {
				++it;
			}
		}
		return false;
	}

	void ImGuiNewFrame();
	ImFont *GetFont(ImGuiFont f);

	void SetMainColor(COLORREF col);
private:
	void InitImGui();
	void ShutdownImGui();
	ImFont *defaultFont;
	ImFont *consoleFont;
	ImFont *monoFont;
	ImFont *manuscriptFont;
};

bool OrbiterDefDialogProc (QWidget *hDlg, QEvent *event);

#endif // !__DLGMGR_H
