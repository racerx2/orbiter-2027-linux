// WineBridge helper: Launchpad items (TabExtra.cpp of the Windows source), custom commands and help. Orbiter holds a proxy of each
// item a module registers, and a Linux function for each of its custom commands
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"

namespace {
	struct Item { LaunchpadItem *obj; int module; };
	Item *g_item;
	int g_nitem, g_capitem;

	Item *ItemOf (uint32_t id)
	{
		for (int i = 0; i < g_nitem; i++) if (wb::Id (g_item[i].obj) == id) return &g_item[i];
		return 0;
	}
	void Drop (const LaunchpadItem *obj)
	{
		for (int i = 0; i < g_nitem; i++) if (g_item[i].obj == obj) { g_item[i] = g_item[--g_nitem]; return; }
	}
}

// ---------------------------------------------------------------------
// LaunchpadItem

LaunchpadItem::LaunchpadItem ()
{
	hItem = 0;
}

LaunchpadItem::~LaunchpadItem ()
{}

char *LaunchpadItem::Name ()
{
	return 0;
}

char *LaunchpadItem::Description ()
{
	return 0;
}

static bool g_dlgSaid[257]; // a module's Launchpad dialog that couldn't be shown was logged (by its number)

// modal, as Orbiter's: the Launchpad waits (the helper has no window for it: no owner)
bool LaunchpadItem::OpenDialog (HINSTANCE hInst, HWND hLaunchpad, int resId, DLGPROC pDlg)
{
	if (DialogBoxParamA (hInst, MAKEINTRESOURCEA (resId), hLaunchpad, pDlg, (LPARAM)this) == -1) {
		int m = wb::ModuleNumber (hInst);
		if (m <= 0 || m > 256 || !g_dlgSaid[m]) {
			char path[MAX_PATH] = "";
			GetModuleFileNameA (hInst, path, MAX_PATH);
			const char *nm = strrchr (path, '\\');
			wb::Log ("%s: a Launchpad item's dialog %d couldn't be shown (Wine error %lu)", nm ? nm + 1 : path, resId, GetLastError ());
			if (m > 0 && m <= 256) g_dlgSaid[m] = true;
		}
	}
	return true;
}

bool LaunchpadItem::clbkOpen (HWND hLaunchpad)
{
	return false;
}

int LaunchpadItem::clbkWriteConfig ()
{
	return 0;
}

// Orbiter's proxy holds the item's place in the Extra tab; Orbiter's handle is the item's hItem, as Orbiter sets it
LAUNCHPADITEM_HANDLE oapiRegisterLaunchpadItem (LaunchpadItem *item, LAUNCHPADITEM_HANDLE parent)
{
	if (!item) return 0;
	int module = wb::AddrModule (*(void **)item);
	bool had = false;
	for (int i = 0; i < g_nitem; i++) had |= g_item[i].obj == item;
	if (!had) {
		if (g_nitem == g_capitem) { g_capitem = g_capitem ? g_capitem * 2 : 16; g_item = (Item *)realloc (g_item, g_capitem * sizeof *g_item); }
		g_item[g_nitem++] = {item, module}; // Orbiter asks for its name while it registers it
	}
	WbWriter &w = wb::Begin (L_LpadRegister);
	w.u32 (wb::Id (item)); w.i32 (module); w.u32 (wb::Id (parent));
	WbReader r = wb::Call ();
	uint32_t h = r.u32 ();
	bool added = r.b ();
	if (r.bad) { h = 0; added = false; }
	if (added) item->hItem = (LAUNCHPADITEM_HANDLE)wb::Ptr (h);
	else if (!had) Drop (item); // an item of that name was there: Orbiter keeps the other
	return (LAUNCHPADITEM_HANDLE)wb::Ptr (h);
}

bool oapiUnregisterLaunchpadItem (LaunchpadItem *item)
{
	wb::Begin (L_LpadUnregister).u32 (wb::Id (item));
	WbReader r = wb::Call ();
	bool ok = r.b ();
	Drop (item); // Orbiter called its clbkWriteConfig while unregistering it
	return ok && !r.bad;
}

void wb::ServeLpad (WbReader &r, WbWriter &w)
{
	uint32_t id = r.u32 ();
	int m = r.i32 ();
	Item *it = r.bad ? 0 : ItemOf (id);
	LaunchpadItem *obj = it ? it->obj : 0;
	switch (m) {
	case P_Name: case P_Description: {
		const char *s = obj ? (m == P_Name ? obj->Name () : obj->Description ()) : 0;
		w.b (obj != 0); wb::TextStr (w, s);
		break; }
	case P_Open: {
		bool res = obj && obj->clbkOpen (0); // no Win32 window stands for Orbiter's Launchpad
		w.b (obj != 0); w.b (res);
		break; }
	case P_WriteConfig: {
		int res = obj ? obj->clbkWriteConfig () : 0;
		w.b (obj != 0); w.i32 (res);
		break; }
	}
}

// ---------------------------------------------------------------------
// custom commands: Orbiter calls a Linux function, which calls the module's (H_CUSTOMCMD)

DWORD oapiRegisterCustomCmd (char *label, char *desc, CustomFunc func, void *context)
{
	WbWriter &w = wb::Begin (L_CustomCmd);
	wb::TextStr (w, label); wb::TextStr (w, desc); w.u32 (wb::Id ((void *)func)); w.u32 (wb::Id (context)); w.i32 (wb::AddrModule ((void *)func));
	WbReader r = wb::Call ();
	DWORD id = r.u32 ();
	return r.bad ? 0 : id;
}

bool oapiUnregisterCustomCmd (int cmdId)
{
	wb::Begin (L_CustomCmdGone).u32 ((uint32_t)cmdId);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	return ok && !r.bad;
}

void wb::ServeCustomCmd (WbReader &r)
{
	uint32_t f = r.u32 (), ctx = r.u32 ();
	if (!r.bad && f && AddrModule (Ptr (f))) ((CustomFunc)Ptr (f)) (Ptr (ctx)); // only while its module is loaded
}

// ---------------------------------------------------------------------
// help

static bool Help (bool launchpad, const HELPCONTEXT *hc)
{
	if (!hc) return false;
	WbWriter &w = wb::Begin (L_OpenHelp);
	w.b (launchpad); w.str (hc->helpfile); w.str (hc->topic); w.str (hc->toc); w.str (hc->index);
	WbReader r = wb::Call ();
	bool ok = r.b ();
	return ok && !r.bad;
}

bool oapiOpenHelp (HELPCONTEXT *hcontext)
{
	return Help (false, hcontext);
}

bool oapiOpenLaunchpadHelp (HELPCONTEXT *hcontext)
{
	return Help (true, hcontext);
}

// the module is freed: Linux unregistered its items and commands
void wb::LpadFreed (int module)
{
	if (module > 0 && module <= 256) g_dlgSaid[module] = false;
	for (int i = g_nitem - 1; i >= 0; i--) if (g_item[i].module == module) g_item[i] = g_item[--g_nitem];
}

// ---------------------------------------------------------------------
// the inline and compiler-made members Orbiter.exe exports (api/inline.def): made here (this file is built without inlining)

__attribute__ ((used)) static void Made (LaunchpadItem *a)
{
	LaunchpadItem c (*a);
	*a = c;
}
