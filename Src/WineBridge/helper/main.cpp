// WineBridge helper: Orbiter.exe under Wine. Loads Windows modules and serves Orbiter's calls to them.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "wb_helper.h"
#include "DrawAPI.h"
#include "compat2010.h"
#include "gen_calls.h"

typedef VESSEL *(*OVCINIT)(OBJHANDLE, int);
typedef void (*OVCEXIT)(VESSEL *);
typedef void (*DLLFUNC)(HINSTANCE);

// sdkMain: its DllMain is Orbitersdk.lib's, which calls InitModule and ExitModule; else the helper calls them, as for vessels on Windows
// api: its GetModuleVersion, the date of the SDK it was built with
struct Module { HMODULE h; OVCINIT init; OVCEXIT exit; DLLFUNC exitModule; bool sdkMain; int api; FARPROC fn[E_COUNT]; char path[MAX_PATH]; };
static Module g_mod[256];
static int g_nmod;
static VESSEL **g_obj; // by the Linux side's vessel id
static int *g_objmod;  // ... and the module that made it
static uint32_t g_nobj;
static HMODULE g_initLib;           // the module being loaded, when its DllMain called InitLib
static int g_loading;               // the number of the module being loaded, during its load
static char g_loadPath[MAX_PATH];   // ... its full path

VESSEL *wb::Obj (uint32_t vid) { return vid < g_nobj ? g_obj[vid] : 0; }

uint32_t wb::VesselIdOf (const void *v)
{
	for (uint32_t vid = 1; vid < g_nobj; vid++) if (g_obj[vid] && (const void *)g_obj[vid] == v) return vid;
	return ProxyIdOf (v);
}

// h is the module being loaded (not a library its DllMain or InitModule loads)
static bool Loading (HMODULE h)
{
	char f[MAX_PATH];
	DWORD n = g_loading && h ? GetModuleFileNameA (h, f, sizeof f) : 0;
	if (!n || n >= sizeof f) return false;
	const char *a = f, *b = g_loadPath;
	for (; *a && *b; a++, b++) {
		char x = *a == '/' ? '\\' : (char)tolower ((unsigned char)*a), y = *b == '/' ? '\\' : (char)tolower ((unsigned char)*b);
		if (x != y) return false;
	}
	return !*a && !*b;
}

void wb::InitLibCalled (void *h) { if (Loading ((HMODULE)h)) g_initLib = (HMODULE)h; }

int wb::ModuleNumber (const void *h)
{
	if (!h) return 0;
	for (int i = 0; i < g_nmod; i++) if (g_mod[i].h == h) return i + 1;
	return Loading ((HMODULE)h) ? g_loading : 0; // its DllMain is running
}

int wb::AddrModule (const void *addr)
{
	HMODULE h = 0;
	if (!addr || !GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &h)) return 0;
	return ModuleNumber (h);
}

int wb::CallerApi (const void *addr)
{
	HMODULE h = 0;
	if (!GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &h) || !h) return API_2016;
	for (int i = 0; i < g_nmod; i++) if (g_mod[i].h == h) return g_mod[i].api;
	// a library a module brought: built before the 2016 release, the 2010 layouts
	const BYTE *b = (const BYTE *)h;
	const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(b + ((const IMAGE_DOS_HEADER *)b)->e_lfanew);
	return nt->FileHeader.TimeDateStamp < 1472342400u ? 0 : API_2016; // 28 Aug 2016
}

int wb::VesselApi (uint32_t vid)
{
	int m = vid < g_nobj ? g_objmod[vid] : 0;
	return m >= 1 && m <= g_nmod ? g_mod[m - 1].api : API_2016;
}

static void SetObj (uint32_t vid, VESSEL *v, int mod)
{
	if (vid >= g_nobj) {
		uint32_t n = vid + 64;
		g_obj = (VESSEL **)realloc (g_obj, n * sizeof *g_obj);
		g_objmod = (int *)realloc (g_objmod, n * sizeof *g_objmod);
		for (uint32_t i = g_nobj; i < n; i++) g_obj[i] = 0, g_objmod[i] = 0;
		g_nobj = n;
	}
	g_obj[vid] = v;
	g_objmod[vid] = mod;
}

// ---------------------------------------------------------------------
// The vtable slot of each 2010 callback: a class overriding only that one, compared with VESSEL3's own
#pragma clang diagnostic ignored "-Woverloaded-virtual" // each probe overrides exactly one overload on purpose
#define PROBE(c, decl) struct P_##c : VESSEL3 { P_##c () : VESSEL3 ((OBJHANDLE)0, 1) {} decl };
PROBE (C_SetClassCaps, void clbkSetClassCaps (FILEHANDLE) {})
PROBE (C_SaveState, void clbkSaveState (FILEHANDLE) {})
PROBE (C_LoadStateEx, void clbkLoadStateEx (FILEHANDLE, void *) {})
PROBE (C_SetStateEx, void clbkSetStateEx (const void *) {})
PROBE (C_PostCreation, void clbkPostCreation () {})
PROBE (C_FocusChanged, void clbkFocusChanged (bool, OBJHANDLE, OBJHANDLE) {})
PROBE (C_PreStep, void clbkPreStep (double, double, double) {})
PROBE (C_PostStep, void clbkPostStep (double, double, double) {})
PROBE (C_PlaybackEvent, bool clbkPlaybackEvent (double, double, const char *, const char *) { return false; })
PROBE (C_VisualCreated, void clbkVisualCreated (VISHANDLE, int) {})
PROBE (C_VisualDestroyed, void clbkVisualDestroyed (VISHANDLE, int) {})
PROBE (C_DrawHUD2, void clbkDrawHUD (int, const HUDPAINTSPEC *, HDC) {})
PROBE (C_RCSMode, void clbkRCSMode (int) {})
PROBE (C_ADCtrlMode, void clbkADCtrlMode (DWORD) {})
PROBE (C_HUDMode, void clbkHUDMode (int) {})
PROBE (C_MFDMode, void clbkMFDMode (int, int) {})
PROBE (C_NavMode, void clbkNavMode (int, bool) {})
PROBE (C_DockEvent, void clbkDockEvent (int, OBJHANDLE) {})
PROBE (C_Animate, void clbkAnimate (double) {})
PROBE (C_ConsumeDirectKey, int clbkConsumeDirectKey (char *) { return 0; })
PROBE (C_ConsumeBufferedKey, int clbkConsumeBufferedKey (DWORD, bool, char *) { return 0; })
PROBE (C_LoadGenericCockpit, bool clbkLoadGenericCockpit () { return false; })
PROBE (C_LoadPanel, bool clbkLoadPanel (int) { return false; })
PROBE (C_PanelMouseEvent2, bool clbkPanelMouseEvent (int, int, int, int) { return false; })
PROBE (C_PanelRedrawEvent2, bool clbkPanelRedrawEvent (int, int, SURFHANDLE) { return false; })
PROBE (C_LoadVC, bool clbkLoadVC (int) { return false; })
PROBE (C_VCMouseEvent, bool clbkVCMouseEvent (int, int, VECTOR3 &) { return false; })
PROBE (C_VCRedrawEvent, bool clbkVCRedrawEvent (int, int, SURFHANDLE) { return false; })
PROBE (C_PanelMouseEvent3, bool clbkPanelMouseEvent (int, int, int, int, void *) { return false; })
PROBE (C_PanelRedrawEvent3, bool clbkPanelRedrawEvent (int, int, SURFHANDLE, void *) { return false; })
PROBE (C_Generic, int clbkGeneric (int, int, void *) { return 0; })
PROBE (C_LoadPanel2D, bool clbkLoadPanel2D (int, PANELHANDLE, DWORD, DWORD) { return false; })
PROBE (C_DrawHUD3, bool clbkDrawHUD (int, const HUDPAINTSPEC *, oapi::Sketchpad *) { return false; })
PROBE (C_RenderHUD, void clbkRenderHUD (int, const HUDPAINTSPEC *, SURFHANDLE) {})
PROBE (C_GetRadiationForce, void clbkGetRadiationForce (const VECTOR3 &, VECTOR3 &, VECTOR3 &) {})

static void **Vt (VESSEL2 *v) { return *(void ***)v; }

static int g_slot[C_COUNT];
static int g_nslot[4]; // the slots of the callbacks of interface version 1, 2, 3: 0 .. n - 1
static void **g_base, **g_base4; // VESSEL3's and VESSEL4's own vtables

int wb::Slot (int c) { return g_slot[c]; }

template <class P> static int SlotOf (void **base, int n)
{
	P p;
	void **v = Vt (&p);
	int found = -1;
	for (int i = 0; i < n; i++)
		if (v[i] != base[i]) { if (found >= 0) return -2; found = i; }
	return found;
}

static bool Probe ()
{
	static VESSEL3 base ((OBJHANDLE)0, 1);
	g_base = Vt (&base);
#define S(c) g_slot[c] = SlotOf<P_##c> (g_base, WB_V4_FIRST);
	S (C_SetClassCaps) S (C_SaveState) S (C_LoadStateEx) S (C_SetStateEx) S (C_PostCreation) S (C_FocusChanged)
	S (C_PreStep) S (C_PostStep) S (C_PlaybackEvent) S (C_VisualCreated) S (C_VisualDestroyed) S (C_DrawHUD2)
	S (C_RCSMode) S (C_ADCtrlMode) S (C_HUDMode) S (C_MFDMode) S (C_NavMode) S (C_DockEvent) S (C_Animate)
	S (C_ConsumeDirectKey) S (C_ConsumeBufferedKey) S (C_LoadGenericCockpit) S (C_LoadPanel)
	S (C_PanelMouseEvent2) S (C_PanelRedrawEvent2) S (C_LoadVC) S (C_VCMouseEvent) S (C_VCRedrawEvent)
	S (C_PanelMouseEvent3) S (C_PanelRedrawEvent3) S (C_Generic) S (C_LoadPanel2D) S (C_DrawHUD3) S (C_RenderHUD)
	S (C_GetRadiationForce)
#undef S
	static VESSEL4 base4 ((OBJHANDLE)0, 1);
	g_base4 = Vt (&base4);
	g_slot[C_NavProcess] = wb::VcallSlot (&VESSEL4::clbkNavProcess); // VESSEL4's callback, past VESSEL3's vtable
	if (wb::VcallSlot (&VESSEL3::clbkPreStep) != g_slot[C_PreStep] || wb::VcallSlot (&VESSEL3::clbkGetRadiationForce) != g_slot[C_GetRadiationForce]) {
		fprintf (stderr, "WineBridge helper: the vcall thunks don't give the probed slots\n");
		return false;
	}
	// the two sketchpad layouts modules see (compat2010.h): 2016 put GetOrigin before Text
	if (wb::VcallSlot (&wb::Sketchpad2010::Text) != 11 || wb::VcallSlot (&wb::Sketchpad2010::GetDC) != 23 ||
		wb::VcallSlot (&oapi::Sketchpad::GetOrigin) != 11 || wb::VcallSlot (&oapi::Sketchpad::Text) != 12 || wb::VcallSlot (&oapi::Sketchpad::GetDC) != 24) {
		fprintf (stderr, "WineBridge helper: the sketchpad classes are not in the SDKs' layouts\n");
		return false;
	}
	if (!wb::MfdProbe ()) {
		fprintf (stderr, "WineBridge helper: the MFD, ExternMFD or Module classes are not in the SDKs' layout\n");
		return false;
	}
	if (!wb::CelbodyProbe ()) {
		fprintf (stderr, "WineBridge helper: the CELBODY, CELBODY2 or ATMOSPHERE classes are not in the SDKs' layout\n");
		return false;
	}
	bool seen[256] = {};
	for (int c = 0; c < C_COUNT; c++) {
		if (g_slot[c] < 0 || g_slot[c] >= 256 || seen[g_slot[c]]) { fprintf (stderr, "WineBridge helper: no vtable slot for callback %d\n", c); return false; }
		seen[g_slot[c]] = true;
	}
	for (int c = 0; c < C_COUNT; c++) { // each version's callbacks fill the slots before the next version's
		int ver = c >= WB_V4_FIRST ? 3 : c >= WB_V3_FIRST ? 2 : 1;
		for (int v = ver; v <= 3; v++) if (g_slot[c] + 1 > g_nslot[v]) g_nslot[v] = g_slot[c] + 1;
	}
	if (g_nslot[1] != WB_V3_FIRST || g_nslot[2] != WB_V4_FIRST || g_nslot[3] != C_COUNT) {
		fprintf (stderr, "WineBridge helper: the vessel callbacks' slots are not in the SDK's order\n");
		return false;
	}
	if (!wb::HooksProbe (wb::ProxyVt ())) {
		fprintf (stderr, "WineBridge helper: VirtualProtect refuses WRITECOPY on the vessel vtable copies\n");
		return false;
	}
	return true;
}

// a vtable entry through the import thunks a DLL puts there for functions it imports (jmp [iat], jmp rel32)
const void *wb::Resolve (const void *p)
{
	for (int i = 0; i < 4 && p; i++) {
		const unsigned char *b = (const unsigned char *)p;
		if (b[0] == 0xFF && b[1] == 0x25) p = **(void ***)(b + 2);
		else if (b[0] == 0xE9) p = b + 5 + *(const int32_t *)(b + 1);
		else break;
	}
	return p;
}

static void Overridden (VESSEL *v, uint32_t mask[2])
{
	mask[0] = mask[1] = 0;
	int ver = v->Version ();
	if (ver < 1) return;
	void **vt = Vt (static_cast<VESSEL2 *> (v));
	int n = ver >= 3 ? C_COUNT : ver >= 2 ? WB_V4_FIRST : WB_V3_FIRST;
	for (int c = 0; c < n; c++)
		if (wb::Resolve (vt[g_slot[c]]) != (c >= WB_V4_FIRST ? g_base4 : g_base)[g_slot[c]]) mask[c >> 5] |= 1u << (c & 31);
}

// vessel hooks: the masks of the module objects of a class whose vtable changed (H_HOOKS)
void wb::ObjMasks (void **vt, WbWriter &w, uint32_t *n)
{
	for (uint32_t vid = 0; vid < g_nobj; vid++) {
		VESSEL *v = g_obj[vid];
		if (!v || v->Version () < 1 || Vt (static_cast<VESSEL2 *> (v)) != vt) continue;
		uint32_t mask[2];
		Overridden (v, mask);
		w.u32 (vid); w.u32 (mask[0]); w.u32 (mask[1]);
		(*n)++;
	}
}

// ---------------------------------------------------------------------
// The DLL's imports from Orbiter.exe that this helper doesn't export, before it is loaded for real
static void MissingImports (const char *path, char *out, size_t outlen)
{
	out[0] = 0;
	HMODULE m = LoadLibraryExA (path, 0, DONT_RESOLVE_DLL_REFERENCES);
	if (!m) return;
	const BYTE *base = (const BYTE *)m;
	const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	HMODULE self = GetModuleHandleA (0);
	char list[1536];
	size_t used = 0;
	int missing = 0;
	bool cut = false;
	list[0] = 0;
	if (dir.VirtualAddress) {
		for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
			if (_stricmp ((const char *)(base + d->Name), "Orbiter.exe")) continue;
			const IMAGE_THUNK_DATA32 *t = (const IMAGE_THUNK_DATA32 *)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
			for (; t->u1.AddressOfData; t++) {
				const char *name;
				char ord[32];
				if (t->u1.Ordinal & IMAGE_ORDINAL_FLAG32) { _snprintf (ord, sizeof ord, "#%lu", (unsigned long)(t->u1.Ordinal & 0xffff)); name = ord; }
				else name = (const char *)((const IMAGE_IMPORT_BY_NAME *)(base + t->u1.AddressOfData))->Name;
				if (name[0] != '#' && GetProcAddress (self, name)) continue;
				if (!missing++) fprintf (stderr, "WineBridge helper: %s imports from Orbiter.exe what isn't bridged yet:\n", path);
				fprintf (stderr, "  %s\n", name); // the whole list goes to WineBridge.log
				size_t n = strlen (name);
				if (used + n + 2 < sizeof list) { if (used) list[used++] = ' '; memcpy (list + used, name, n); used += n; list[used] = 0; }
				else cut = true;
			}
		}
	}
	FreeLibrary (m);
	fflush (stderr);
	if (missing) _snprintf (out, outlen, "%d Orbiter functions not bridged yet (all in WineBridge.log): %s%s", missing, list, cut ? " ..." : "");
	out[outlen - 1] = 0;
}

// H_LOAD's reply for module number m (0: failed, with err)
static void LoadReply (WbWriter &w, int m, const char *err)
{
	w.i32 (m); w.str (err);
	uint32_t exports = 0;
	int version = 0;
	const char *date = 0, *model = 0, *desc = 0;
	if (m) {
		Module &d = g_mod[m - 1];
		for (int e = 0; e < E_COUNT; e++) if (d.fn[e]) exports |= 1u << e;
		if (d.fn[E_GetModuleVersion]) version = ((int (*)())d.fn[E_GetModuleVersion]) ();
		if (d.fn[E_ModuleDate]) date = ((char *(*)())d.fn[E_ModuleDate]) ();
		if (d.fn[E_ModelName]) model = ((char *(*)())d.fn[E_ModelName]) (); // an atmosphere module's, for the Launchpad's list
		if (d.fn[E_ModelDesc]) desc = ((char *(*)())d.fn[E_ModelDesc]) ();
	}
	w.u32 (exports); w.i32 (version); w.str (date); w.str (model); w.str (desc);
}

// the DLLs the helper itself stands in for, which a module finds by name
static const char *const g_standIn[] = {"OrbiterSound.dll", "XRSound.dll"};

static void LoadSoundDlls ();

static void Load (WbReader &r, WbWriter &w)
{
	uint32_t n;
	const char *s = r.str (&n);
	char path[MAX_PATH] = "";
	if (s && n < sizeof path) { memcpy (path, s, n); path[n] = 0; }
	if (r.b ()) LoadSoundDlls (); // XRSound is there: before the module, whose OrbiterSound or XRSound SDK looks for it
	for (int i = 0; i < g_nmod; i++)
		if (g_mod[i].h && !_stricmp (g_mod[i].path, path)) { LoadReply (w, i + 1, ""); return; }
	const char *mp = path;
	for (const char *c = path; *c; c++) if (*c == '\\' || *c == '/') mp = c + 1;
	char err[2048];
	for (const char *si : g_standIn)
		if (!_stricmp (mp, si)) { _snprintf (err, sizeof err, "the bridge's own %s stands in for it", si); err[sizeof err - 1] = 0; LoadReply (w, 0, err); return; }
	// a load during another's (its InitModule made an interpreter, so Orbiter loaded LuaInline): that one's slot and state are kept
	static bool busy[sizeof g_mod / sizeof *g_mod];
	int slot = 0;
	while (slot < (int)(sizeof g_mod / sizeof *g_mod) && (g_mod[slot].h || busy[slot])) slot++;
	if (slot == (int)(sizeof g_mod / sizeof *g_mod)) { LoadReply (w, 0, "too many modules"); return; }
	MissingImports (path, err, sizeof err);
	if (err[0]) { LoadReply (w, 0, err); return; }
	HMODULE outerInit = g_initLib;
	int outerLoading = g_loading;
	char outerPath[MAX_PATH];
	strcpy (outerPath, g_loadPath);
	g_initLib = 0;
	DWORD fl = GetFullPathNameA (path, sizeof g_loadPath, g_loadPath, 0);
	g_loading = fl && fl < sizeof g_loadPath ? slot + 1 : 0;
	busy[slot] = true;
	HMODULE h = LoadLibraryA (path);
	busy[slot] = false;
	bool sdkMain = h && g_initLib == h;
	g_initLib = outerInit; g_loading = outerLoading; strcpy (g_loadPath, outerPath);
	if (!h) { _snprintf (err, sizeof err, "LoadLibrary failed, Windows error %lu", GetLastError ()); LoadReply (w, 0, err); return; }
	Module &m = g_mod[slot];
	memset (&m, 0, sizeof m);
	m.h = h;
	m.init = (OVCINIT)GetProcAddress (h, "ovcInit");
	m.exit = (OVCEXIT)GetProcAddress (h, "ovcExit");
	m.exitModule = (DLLFUNC)GetProcAddress (h, "ExitModule");
	if (!m.exitModule) m.exitModule = (DLLFUNC)GetProcAddress (h, "opcDLLExit");
	m.sdkMain = sdkMain;
	for (int e = 0; e < E_COUNT; e++) m.fn[e] = GetProcAddress (h, WbExportName[e]);
	m.api = m.fn[E_GetModuleVersion] ? ((int (*)())m.fn[E_GetModuleVersion]) () : 0;
	strcpy (m.path, path);
	if (slot >= g_nmod) g_nmod = slot + 1;
	if (!m.sdkMain) { // a module without Orbitersdk's DllMain: InitModule as Orbiter calls it
		wb::Log ("Module %s (Windows, through the Wine bridge; no Orbitersdk.lib start; %s SDK layouts)", mp, m.api < wb::API_2016 ? "2010" : "2016");
		DLLFUNC f = (DLLFUNC)GetProcAddress (h, "InitModule");
		if (!f) f = (DLLFUNC)GetProcAddress (h, "opcDLLInit");
		if (f) f (h);
	}
	LoadReply (w, slot + 1, "");
}

// the module's last open: ExitModule (by Orbitersdk.lib's DllMain, or here as Orbiter calls it), then FreeLibrary
static void Free (int i)
{
	Module &m = g_mod[i];
	if (!m.h) return;
	if (!m.sdkMain && m.exitModule) m.exitModule (m.h);
	wb::DialogsFreed (m.h); // what its ExitModule left open
	const BYTE *base = (const BYTE *)m.h;
	size_t size = ((const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew))->OptionalHeader.SizeOfImage;
	FreeLibrary (m.h);
	wb::HooksFreed (base, size); // its classes' vtables are gone (only compared, never read)
	wb::MfdFreed (i + 1);    // what the module left (its ExitModule ran in either case): its code is gone
	wb::ModuleFreed (i + 1);
	wb::CelbodyFreed (i + 1);
	wb::LpadFreed (i + 1);
	wb::CameraFreed (i + 1);
	wb::ScriptFreed (i + 1);
	memset (&m, 0, sizeof m);
	while (g_nmod && !g_mod[g_nmod - 1].h) g_nmod--;
}

void *wb::ModuleExport (int module, int e)
{
	return module >= 1 && module <= g_nmod && e >= 0 && e < E_COUNT ? (void *)g_mod[module - 1].fn[e] : 0;
}

void *wb::ModuleExport (int module, const char *name)
{
	return module >= 1 && module <= g_nmod && g_mod[module - 1].h && name ? (void *)GetProcAddress (g_mod[module - 1].h, name) : 0;
}

void *wb::ModuleHandle (int module)
{
	return module >= 1 && module <= g_nmod ? g_mod[module - 1].h : 0;
}

// the function a module imports by the first of the two names it imports (its IAT entry, as the module calls it); 0: neither
static void *ModuleImport (int module, const char *first, const char *second)
{
	if (module < 1 || module > g_nmod || !g_mod[module - 1].h) return 0;
	const BYTE *base = (const BYTE *)g_mod[module - 1].h;
	const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(base + ((const IMAGE_DOS_HEADER *)base)->e_lfanew);
	const IMAGE_DATA_DIRECTORY &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	const char *const name[2] = {first, second};
	void *found[2] = {};
	if (dir.VirtualAddress) {
		for (const IMAGE_IMPORT_DESCRIPTOR *d = (const IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress); d->Name; d++) {
			if (!d->OriginalFirstThunk) continue; // no names kept: bound by ordinal or by a linker of the 1990s
			const IMAGE_THUNK_DATA32 *t = (const IMAGE_THUNK_DATA32 *)(base + d->OriginalFirstThunk);
			const IMAGE_THUNK_DATA32 *iat = (const IMAGE_THUNK_DATA32 *)(base + d->FirstThunk);
			for (; t->u1.AddressOfData; t++, iat++) {
				if (t->u1.Ordinal & IMAGE_ORDINAL_FLAG32) continue;
				const char *n = (const char *)((const IMAGE_IMPORT_BY_NAME *)(base + t->u1.AddressOfData))->Name;
				for (int k = 0; k < 2; k++) if (!found[k] && !strcmp (n, name[k])) found[k] = (void *)(uintptr_t)iat->u1.Function;
			}
		}
	}
	return found[0] ? found[0] : found[1];
}

// the function the module frees its new objects with: its operator delete, else its C runtime's free, as it imports them
void *wb::ModuleDelete (int module)
{
	return ModuleImport (module, "??3@YAXPAX@Z", "free");
}

void *wb::ModuleAlloc (int module, size_t n)
{
	void *(*f) (size_t) = (void *(*) (size_t))ModuleImport (module, "??2@YAPAXI@Z", "malloc");
	return f ? f (n) : malloc (n);
}

void wb::ModuleRelease (int module, void *p)
{
	void *(*a) (size_t) = (void *(*) (size_t))ModuleImport (module, "??2@YAPAXI@Z", "malloc");
	void (*f) (void *) = (void (*) (void *))ModuleDelete (module);
	if (a && f) f (p);
	else if (!a) free (p);
}

static void Opc (int mod, int e, WbReader &r)
{
	FARPROC f = mod >= 1 && mod <= g_nmod && e >= 0 && e < E_COUNT ? g_mod[mod - 1].fn[e] : 0;
	switch (e) {
	case E_opcOpenRenderViewport: { DWORD vw = r.u32 (), vh = r.u32 (); BOOL fs = r.b (); if (f && !r.bad) ((void (*)(HWND, DWORD, DWORD, BOOL))f) (0, vw, vh, fs); break; }
	case E_opcCloseRenderViewport: if (f) ((void (*)())f) (); break;
	case E_opcPreStep: case E_opcPostStep: { double a = r.f64 (), b = r.f64 (), c = r.f64 (); if (f && !r.bad) ((void (*)(double, double, double))f) (a, b, c); break; }
	case E_opcFocusChanged: { uint32_t a = r.u32 (), b = r.u32 (); if (f && !r.bad) ((void (*)(OBJHANDLE, OBJHANDLE))f) ((OBJHANDLE)wb::Ptr (a), (OBJHANDLE)wb::Ptr (b)); break; }
	case E_opcTimeAccChanged: { double a = r.f64 (), b = r.f64 (); if (f && !r.bad) ((void (*)(double, double))f) (a, b); break; }
	case E_opcDeleteVessel: { uint32_t a = r.u32 (); if (f && !r.bad) ((void (*)(OBJHANDLE))f) ((OBJHANDLE)wb::Ptr (a)); break; }
	case E_opcPause: { bool a = r.b (); if (f && !r.bad) ((void (*)(bool))f) (a); break; }
	case E_opcSaveState: case E_opcLoadState: { uint32_t a = r.u32 (); if (f && !r.bad) ((void (*)(FILEHANDLE))f) ((FILEHANDLE)wb::Ptr (a)); break; }
	default: break;
	}
}

// OrbiterSound.dll and XRSound.dll from the helper's folder: the modules' SDKs find them with GetModuleHandle, as the plugins
// Orbiter loaded on Windows
static void LoadSoundDlls ()
{
	static bool tried;
	if (tried) return;
	tried = true;
	char path[MAX_PATH];
	DWORD n = GetModuleFileNameA (NULL, path, sizeof path);
	char *slash = path;
	for (char *c = path; n && n < sizeof path && *c; c++) if (*c == '\\' || *c == '/') slash = c + 1;
	for (const char *si : g_standIn) {
		if (!n || n >= sizeof path || (size_t)(slash - path) + strlen (si) + 1 > sizeof path) {
			wb::Log ("%s not loaded: the helper's path is too long; the modules have no sound", si);
			continue;
		}
		strcpy (slash, si);
		if (!LoadLibraryA (path)) wb::Log ("%s not loaded (Windows error %lu): the modules have no sound", si, GetLastError ());
	}
}

static HUDPAINTSPEC Hps (WbReader &r)
{
	HUDPAINTSPEC h;
	h.W = r.i32 (); h.H = r.i32 (); h.CX = r.i32 (); h.CY = r.i32 (); h.Scale = r.f64 (); h.Markersize = r.i32 ();
	return h;
}

static void Clbk (uint32_t vid, uint32_t c, WbReader &r, WbWriter &w)
{
	VESSEL *v = wb::Obj (vid);
	if (!v) v = wb::ProxyOf (vid); // a Linux vessel whose class a module hooked (H_PROXY made it)
	if (!v || v->Version () < 1) return;
	VESSEL2 *v2 = static_cast<VESSEL2 *> (v);
	VESSEL3 *v3 = v->Version () >= 2 ? static_cast<VESSEL3 *> (v) : 0;
	VESSEL4 *v4 = v->Version () >= 3 ? static_cast<VESSEL4 *> (v) : 0;
	switch (c) {
	case C_SetClassCaps: { uint32_t f = r.u32 (); v2->clbkSetClassCaps ((FILEHANDLE)wb::Ptr (f)); break; }
	case C_SaveState: { uint32_t f = r.u32 (); v2->clbkSaveState ((FILEHANDLE)wb::Ptr (f)); break; }
	case C_LoadStateEx: { // Orbiter's status struct: a copy here, filled by the module, goes back in the reply
		uint32_t f = r.u32 ();
		VESSELSTATUS2 s;
		memset (&s, 0, sizeof s);
		wb::GetStatus (r, &s);
		v2->clbkLoadStateEx ((FILEHANDLE)wb::Ptr (f), &s);
		wb::PutStatus (w, &s);
		wb::FreeStatus (&s);
		break; }
	case C_SetStateEx: {
		VESSELSTATUS2 s;
		memset (&s, 0, sizeof s);
		wb::GetStatus (r, &s);
		v2->clbkSetStateEx (&s);
		wb::FreeStatus (&s);
		break; }
	case C_PostCreation: v2->clbkPostCreation (); break;
	case C_FocusChanged: { bool g = r.b (); uint32_t a = r.u32 (), b = r.u32 (); v2->clbkFocusChanged (g, (OBJHANDLE)wb::Ptr (a), (OBJHANDLE)wb::Ptr (b)); break; }
	case C_PreStep: { double a = r.f64 (), b = r.f64 (), d = r.f64 (); v2->clbkPreStep (a, b, d); break; }
	case C_PostStep: { double a = r.f64 (), b = r.f64 (), d = r.f64 (); v2->clbkPostStep (a, b, d); break; }
	case C_PlaybackEvent: {
		double t = r.f64 (), et = r.f64 ();
		uint32_t n1, n2;
		const char *s1 = r.str (&n1), *s2 = r.str (&n2);
		char *e1 = s1 ? (char *)malloc (n1 + 1) : 0, *e2 = s2 ? (char *)malloc (n2 + 1) : 0;
		if (e1) { memcpy (e1, s1, n1); e1[n1] = 0; }
		if (e2) { memcpy (e2, s2, n2); e2[n2] = 0; }
		bool ok = v2->clbkPlaybackEvent (t, et, e1, e2);
		free (e1); free (e2);
		w.b (ok);
		break; }
	case C_RCSMode: { int m = r.i32 (); v2->clbkRCSMode (m); break; }
	case C_ADCtrlMode: { DWORD m = r.u32 (); v2->clbkADCtrlMode (m); break; }
	case C_HUDMode: { int m = r.i32 (); v2->clbkHUDMode (m); break; }
	case C_MFDMode: { int a = r.i32 (), b = r.i32 (); v2->clbkMFDMode (a, b); break; }
	case C_NavMode: { int m = r.i32 (); bool a = r.b (); v2->clbkNavMode (m, a); break; }
	case C_DockEvent: { int d = r.i32 (); uint32_t m = r.u32 (); v2->clbkDockEvent (d, (OBJHANDLE)wb::Ptr (m)); break; }
	case C_Animate: { double t = r.f64 (); v2->clbkAnimate (t); break; }
	case C_ConsumeDirectKey: {
		char ks[256]; r.raw (ks, 256);
		int res = v2->clbkConsumeDirectKey (ks);
		w.i32 (res); w.raw (ks, 256);
		break; }
	case C_ConsumeBufferedKey: {
		DWORD k = r.u32 (); bool d = r.b ();
		char ks[256]; r.raw (ks, 256);
		int res = v2->clbkConsumeBufferedKey (k, d, ks);
		w.i32 (res);
		break; }
	case C_Generic: { // VMSG_LUAINTERPRETER, VMSG_LUAINSTANCE: cx is Linux's Lua state (luaremote.cpp)
		int m = r.i32 (), p = r.i32 (); uint32_t cx = r.u32 ();
		bool err = false;
		int res = !v3 ? 0 : m == VMSG_LUAINTERPRETER || m == VMSG_LUAINSTANCE ? wb::LuaGeneric (v3, m, p, cx, &err) : v3->clbkGeneric (m, p, wb::Ptr (cx));
		w.i32 (res); w.b (err);
		break; }
	case C_NavProcess: { int mode = r.i32 (); w.i32 (v4 ? v4->clbkNavProcess (mode) : 0); break; }
	case C_GetRadiationForce: {
		VECTOR3 mf, F, pos;
		r.v3 (mf.data); r.v3 (F.data); r.v3 (pos.data);
		if (v3) v3->clbkGetRadiationForce (mf, F, pos);
		w.v3 (F.data); w.v3 (pos.data);
		break; }
	case C_VisualCreated: { uint32_t vis = r.u32 (); int rc = r.i32 (); v2->clbkVisualCreated ((VISHANDLE)wb::Ptr (vis), rc); break; }
	case C_VisualDestroyed: { uint32_t vis = r.u32 (); int rc = r.i32 (); v2->clbkVisualDestroyed ((VISHANDLE)wb::Ptr (vis), rc); break; }
	case C_LoadGenericCockpit: w.b (v2->clbkLoadGenericCockpit ()); break;
	case C_LoadPanel: { int id = r.i32 (); w.b (v2->clbkLoadPanel (id)); break; }
	case C_PanelMouseEvent2: { int id = r.i32 (), ev = r.i32 (), mx = r.i32 (), my = r.i32 (); w.b (v2->clbkPanelMouseEvent (id, ev, mx, my)); break; }
	case C_PanelRedrawEvent2: { int id = r.i32 (), ev = r.i32 (); uint32_t s = r.u32 (); w.b (v2->clbkPanelRedrawEvent (id, ev, (SURFHANDLE)wb::Ptr (s))); break; }
	case C_LoadVC: { int id = r.i32 (); w.b (v2->clbkLoadVC (id)); break; }
	case C_VCMouseEvent: {
		int id = r.i32 (), ev = r.i32 ();
		VECTOR3 p;
		r.v3 (p.data);
		bool res = v2->clbkVCMouseEvent (id, ev, p);
		w.b (res); w.v3 (p.data);
		break; }
	case C_VCRedrawEvent: { int id = r.i32 (), ev = r.i32 (); uint32_t s = r.u32 (); w.b (v2->clbkVCRedrawEvent (id, ev, (SURFHANDLE)wb::Ptr (s))); break; }
	case C_PanelMouseEvent3: {
		int id = r.i32 (), ev = r.i32 (), mx = r.i32 (), my = r.i32 (); uint32_t cx = r.u32 ();
		w.b (v3 ? v3->clbkPanelMouseEvent (id, ev, mx, my, wb::Ptr (cx)) : false);
		break; }
	case C_PanelRedrawEvent3: {
		int id = r.i32 (), ev = r.i32 (); uint32_t s = r.u32 (), cx = r.u32 ();
		w.b (v3 ? v3->clbkPanelRedrawEvent (id, ev, (SURFHANDLE)wb::Ptr (s), wb::Ptr (cx)) : false);
		break; }
	case C_LoadPanel2D: {
		int id = r.i32 (); uint32_t hp = r.u32 (); DWORD vw = r.u32 (), vh = r.u32 ();
		w.b (v3 ? v3->clbkLoadPanel2D (id, (PANELHANDLE)wb::Ptr (hp), vw, vh) : false);
		break; }
	case C_DrawHUD3: {
		int mode = r.i32 ();
		HUDPAINTSPEC hps = Hps (r);
		uint32_t surf = r.u32 (), skp = r.u32 ();
		bool old = v3 && wb::CallerApi (wb::Resolve (Vt (v2)[g_slot[C_DrawHUD3]])) < wb::API_2016; // the layout of the code that gets it first: a hook's, else the vessel's module's
		bool drawn = v3 && wb::DrawHUD3 (v3, old, mode, &hps, surf, skp);
		if (!drawn) drawn = wb::DrawHUD2 (v2, mode, &hps, surf, skp); // not drawn: the HDC HUD, as Orbiter 2016 called it
		w.b (drawn);
		break; }
	case C_DrawHUD2: { // a VESSEL2 module's HUD (Linux sends it only for those)
		int mode = r.i32 ();
		HUDPAINTSPEC hps = Hps (r);
		uint32_t surf = r.u32 (), skp = r.u32 ();
		w.b (wb::DrawHUD2 (v2, mode, &hps, surf, skp));
		break; }
	case C_RenderHUD: {
		int mode = r.i32 ();
		HUDPAINTSPEC hps = Hps (r);
		uint32_t tex = r.u32 ();
		if (v3) v3->clbkRenderHUD (mode, &hps, (SURFHANDLE)wb::Ptr (tex));
		break; }
	default:
		break;
	}
}

void wb::Dispatch (uint32_t fn, WbReader &r, WbWriter &w)
{
	switch (fn) {
	case H_HELLO: {
		uint32_t v = r.u32 (), g = r.u32 ();
		w.i32 (v == WB_VERSION && g == WB_GEN_HASH);
		break; }
	case H_LOAD: Load (r, w); break;
	case H_OVCINIT: {
		int mod = r.i32 (); uint32_t vid = r.u32 (); int fm = r.i32 ();
		if (mod < 1 || mod > g_nmod || !g_mod[mod - 1].init) { w.i32 (0); break; }
		VESSEL *v = g_mod[mod - 1].init ((OBJHANDLE)wb::Ptr (vid), fm);
		if (!v) { w.i32 (0); break; }
		SetObj (vid, v, mod);
		if (v->Version () >= 1) wb::HooksTrack (Vt (static_cast<VESSEL2 *> (v)), g_nslot[v->Version () >= 3 ? 3 : v->Version ()]);
		uint32_t mask[2];
		Overridden (v, mask);
		w.i32 (1); w.i32 (v->Version ()); w.u32 (mask[0]); w.u32 (mask[1]);
		break; }
	case H_OVCEXIT: {
		int mod = r.i32 (); uint32_t vid = r.u32 ();
		VESSEL *v = Obj (vid);
		SetObj (vid, 0, 0);
		VesselGone (vid); // before the module frees what Orbiter read
		if (v && mod >= 1 && mod <= g_nmod && g_mod[mod - 1].exit) g_mod[mod - 1].exit (v);
		break; }
	case H_CLBK: { uint32_t vid = r.u32 (), c = r.u32 (); Clbk (vid, c, r, w); break; }
	case H_AIRFOIL: {
		uint32_t f = r.u32 (), cx = r.u32 (), vid = r.u32 ();
		int kind = r.i32 ();
		if (kind == A_Ex2) { // force and moment
			VECTOR3 vinf;
			r.v3 (vinf.data);
			double aoa = r.f64 (), beta = r.f64 (), gamma = r.f64 (), M = r.f64 (), Re = r.f64 ();
			double k[6] = {0, 0, 0, 0, 0, 0};
			if (f && !r.bad) ((AirfoilCoeffFuncEx2)wb::Ptr (f)) (Obj (vid), vinf, aoa, beta, gamma, M, Re, wb::Ptr (cx), k, k + 1, k + 2, k + 3, k + 4, k + 5);
			for (double v : k) w.f64 (v);
			break;
		}
		double aoa = r.f64 (), M = r.f64 (), Re = r.f64 ();
		double cl = 0, cm = 0, cd = 0;
		if (f && !r.bad && kind == A_Old) ((AirfoilCoeffFunc)wb::Ptr (f)) (aoa, M, Re, &cl, &cm, &cd);
		else if (f && !r.bad) ((AirfoilCoeffFuncEx)wb::Ptr (f)) (Obj (vid), aoa, M, Re, wb::Ptr (cx), &cl, &cm, &cd);
		w.f64 (cl); w.f64 (cm); w.f64 (cd);
		break; }
	case H_LIFT: {
		uint32_t f = r.u32 ();
		double aoa = r.f64 ();
		w.f64 (f ? ((LiftCoeffFunc)wb::Ptr (f)) (aoa) : 0.0);
		break; }
	case H_INPUTBOX: {
		uint32_t f = r.u32 (), id = r.u32 ();
		uint32_t n;
		const char *s = r.str (&n);
		uint32_t usr = r.u32 ();
		char *str = wb::ToModule (s, s ? n : 0, wb::Ptr (f)); // the typed text in the module's code page; its callback may change it
		bool ok = f && !r.bad ? ((bool (*)(void *, char *, void *))wb::Ptr (f)) (wb::Ptr (id), str, wb::Ptr (usr)) : false;
		free (str);
		w.b (ok);
		break; }
	case H_MESHLOADED: { // oapiLoadMeshGlobal's callback
		uint32_t f = r.u32 (), mesh = r.u32 ();
		bool first = r.b ();
		if (f && !r.bad) ((LoadMeshClbkFunc)wb::Ptr (f)) ((MESHHANDLE)wb::Ptr (mesh), first);
		break; }
	case H_FRAME:
		MeshFrame ();
		break;
	case H_FREE: { int mod = r.i32 (); if (mod >= 1 && mod <= g_nmod) Free (mod - 1); break; }
	case H_OPC: { int mod = r.i32 (), e = r.i32 (); Opc (mod, e, r); break; }
	case H_MFDOPEN: case H_MFDCLOSED: case H_MFD: case H_EMFD: ServeMfd (fn, r, w); break;
	case H_MODULE: ServeModule (r, w); break;
	case H_CELBODY: ServeCelbody (r, w); break;
	case H_LPAD: ServeLpad (r, w); break;
	case H_CUSTOMCMD: ServeCustomCmd (r); break;
	case H_ECC: ServeEcc (r, w); break;
	case H_ECCGONE: ServeEccGone (r); break;
	case H_HOOKS: ServeHooks (w); break;
	case H_LUACALL: case H_LUAHOOK: case H_LUAFREE: ServeLua (fn, r, w); break;
	case H_LUA: ServeLuaH (r, w); break;
	case H_PUMP: Pump (); break;
	case H_DIALOGS: ServeDialogs (r); break;
	case H_PROXY: {
		uint32_t vid = r.u32 ();
		VESSEL *v = r.bad ? 0 : Obj (vid);
		if (!v && !r.bad) v = oapiGetVesselInterface ((OBJHANDLE)wb::Ptr (vid));
		w.b (v != 0);
		break; }
	case H_VESSELGONE: {
		uint32_t vid = r.u32 (), cls = r.u32 ();
		if (r.bad) break;
		ProxyGone (vid);
		if (cls) ClassGone (cls);
		break; }
	case H_QUIT:
		LiveClear (); // the modules' memory goes with them; Orbiter reads no more of it
		for (int i = g_nmod - 1; i >= 0; i--) Free (i);
		break;
	}
}

static LONG WINAPI Crash (EXCEPTION_POINTERS *e)
{
	fprintf (stderr, "WineBridge helper: exception %08lx at %p in a Windows module; the helper ends\n",
		e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
	fflush (stderr);
	ExitProcess (3);
	return EXCEPTION_EXECUTE_HANDLER;
}

int __cdecl main (int argc, char **argv)
{
	SetErrorMode (SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
	const DWORD pmax = 32768; // Modules on the DLL search path, as Orbiter's SetEnvironmentVars: a module's libraries there load
	char *path = (char *)malloc (pmax);
	DWORD np = path ? GetEnvironmentVariableA ("PATH", path, pmax) : pmax;
	if (np < pmax - 16) { strcpy (path + np, np ? ";Modules" : "Modules"); SetEnvironmentVariableA ("PATH", path); }
	free (path);
	SetUnhandledExceptionFilter (Crash);
	if (argc < 4) { fprintf (stderr, "usage: Orbiter.exe <channel> <helper fifo> <linux fifo>\n"); return 1; }
	if (!Probe ()) return 1;
	if (!wb::Open (argv[1], argv[2], argv[3])) return 1;
	wb::Serve ();
	return 0;
}
