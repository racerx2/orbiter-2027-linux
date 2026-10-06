
// custom: launcher layouts; the hook CreateResDialog calls just before it shows a dialog

#ifndef __CUSTOM_LAYOUTHOOK_H
#define __CUSTOM_LAYOUTHOOK_H

class QWidget;
struct RESDIALOG;

namespace custom {

	typedef void (*ResDialogHook) (QWidget *dlg, const RESDIALOG *d, void *hModule);
	extern ResDialogHook g_resDialogHook; // null: no hook (defined in ResDialog.cpp)

}

#endif // !__CUSTOM_LAYOUTHOOK_H
