// WineBridge: the Interpreter class's versions. Its data are the same in 2010, 2016 and 2024; its table of virtual functions isn't
// (2016 gave term_strout and term_out an error flag, OpenOrbiter added term_clear and loaders between them). A module's class built
// with an older Interpreter.h has its table in that version's order, so the interpreter's calls of its own virtual functions go by
// the object's version: its maker's SDK date (GetModuleVersion), checked against the table's entries that are this library's functions
#define INTERPRETER_IMPLEMENTATION
#include "Interpreter.h"
#include "wb_interp.h"
#include <unordered_map>

namespace {
	// a version's slots (0 is the destructor; -1: none), from the SDK date its Interpreter.h took that order (upstream's history)
	struct Layout { int from; bool flag; signed char slot[WB_VCOUNT]; };
	const Layout g_layout[] = {
		//          Init Wait End  API  Ves  MFD  NTV  LE   Bcn  Cam  Skp  Bit  Start PC   RC   strout out clear XRS
		{0,      false, {1, 2, 3, 4, 5, 6, -1, 7, -1, -1, 8, -1, 9, 10, 11, 12, 13, -1, -1}}, // 2010
		{160828, true,  {1, 2, 3, 4, 5, 6, -1, 7, -1, -1, 8, -1, 9, 10, 11, 12, 13, -1, -1}}, // 2016: the error flag
		{210706, true,  {1, 2, 3, 4, 5, 6, -1, 7, -1, -1, 8, -1, 9, 10, 11, 12, 13, 14, -1}}, // OpenOrbiter: term_clear
		{240315, true,  {1, 2, 3, 4, 5, 6, 7, 8, -1, -1, 9, 10, 11, 12, 13, 14, 15, 16, -1}}, // LoadNTVERTEXAPI, LoadBitAPI
		{240320, true,  {1, 2, 3, 4, 5, 6, 7, 8, 9, -1, 10, 11, 12, 13, 14, 15, 16, 17, -1}}, // LoadBeaconMethods
		{240629, true,  {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, -1}}, // LoadCustomCameraMethods
		{240816, true,  {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}}, // LoadXRSoundAPI: 2024, this class
	};
	const int NLAYOUT = sizeof g_layout / sizeof *g_layout, LATEST = NLAYOUT - 1, MAXSLOT = 20;
	const int OWN_TERM_OUT2010 = WB_VCOUNT; // this library's 2010 term_out, in a table

	struct Rec { const void *maker; int layout; };
	SRWLOCK g_lock = SRWLOCK_INIT;
	std::unordered_map<const Interpreter *, Rec> *g_rec;
	void *const *g_ownVt;                  // the table of this library's class
	std::unordered_map<const void *, int> *g_own; // this library's functions in it: their WbVfn

	// n bytes at p are in a loaded module's image (a table read past its end may hold anything)
	bool InImage (const void *p, size_t n)
	{
		HMODULE h = 0;
		if (!p || !GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &h) || !h) return false;
		const BYTE *b = (const BYTE *)h;
		const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(b + ((const IMAGE_DOS_HEADER *)b)->e_lfanew);
		return (const BYTE *)p + n <= b + nt->OptionalHeader.SizeOfImage;
	}

	// a function's code, past import and incremental-link thunks; 0: not code of a module
	const void *Resolve (const void *f)
	{
		for (int i = 0; i < 8; i++) {
			if (!InImage (f, 6)) return 0;
			const BYTE *b = (const BYTE *)f;
			if (b[0] == 0xFF && b[1] == 0x25) { // jmp [import]
				void *const *slot = *(void *const *const *)(b + 2);
				if (!InImage (slot, sizeof *slot)) return 0;
				f = *slot;
			}
			else if (b[0] == 0xE9) f = b + 5 + *(const LONG *)(b + 1); // jmp rel32
			else break;
		}
		return f;
	}

	// the module (or library) holding an address: its SDK date; a library without GetModuleVersion by its build time
	int ApiOf (const void *addr)
	{
		HMODULE h = 0;
		if (!addr || !GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)addr, &h) || !h)
			return g_layout[LATEST].from;
		int (*version) () = (int (*) ())GetProcAddress (h, "GetModuleVersion");
		if (version) return version ();
		const BYTE *b = (const BYTE *)h;
		const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(b + ((const IMAGE_DOS_HEADER *)b)->e_lfanew);
		return nt->FileHeader.TimeDateStamp < 1472342400u ? 100830 : 160828; // 28 Aug 2016
	}

	// the slots of a table that are in its module's image (the reads past a shorter table's end stay there)
	int Readable (void *const *vt)
	{
		HMODULE h = 0;
		if (!GetModuleHandleExA (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)vt, &h) || !h) return 0;
		const BYTE *b = (const BYTE *)h;
		const IMAGE_NT_HEADERS32 *nt = (const IMAGE_NT_HEADERS32 *)(b + ((const IMAGE_DOS_HEADER *)b)->e_lfanew);
		size_t n = (size_t)(b + nt->OptionalHeader.SizeOfImage - (const BYTE *)vt) / sizeof (void *);
		return n < (size_t)MAXSLOT ? (int)n : MAXSLOT;
	}

	// a layout the table can have: each of its slots that holds one of this library's functions holds that layout's function there
	bool Consistent (void *const *vt, int nread, const Layout &l)
	{
		int last = 0;
		for (int k = 0; k < WB_VCOUNT; k++) if (l.slot[k] > last) last = l.slot[k];
		for (int s = 1; s <= last && s < nread; s++) {
			int f = -1;
			for (int k = 0; k < WB_VCOUNT; k++) if (l.slot[k] == s) f = k;
			auto it = g_own->find (Resolve (vt[s]));
			if (it == g_own->end ()) continue;
			int g = it->second;
			if (g == OWN_TERM_OUT2010) { if (f != WB_term_out || l.flag) return false; }
			else if (g == WB_term_out) { if (f != WB_term_out || !l.flag) return false; }
			else if (g != f) return false;
		}
		return true;
	}

	int Choose (const Interpreter *p, const void *maker)
	{
		void *const *vt = *(void *const *const *)p;
		if (vt == g_ownVt) return LATEST;
		int api = ApiOf (maker), nread = Readable (vt), dated = 0, first = -1, best = -1;
		for (int i = 0; i < NLAYOUT; i++) {
			if (g_layout[i].from <= api) dated = i;
			if (!Consistent (vt, nread, g_layout[i])) continue;
			if (first < 0) first = i;
			if (g_layout[i].from <= api) best = i;
		}
		return best >= 0 ? best : first >= 0 ? first : dated;
	}

	const Layout &LayoutOf (const Interpreter *p)
	{
		AcquireSRWLockExclusive (&g_lock);
		int l = LATEST;
		auto it = g_rec->find (p);
		if (it != g_rec->end ()) {
			if (it->second.layout < 0) it->second.layout = Choose (p, it->second.maker);
			l = it->second.layout;
		}
		ReleaseSRWLockExclusive (&g_lock);
		return g_layout[l];
	}

	template <class R, class... A> R VCall (Interpreter *p, int s, A... a)
	{
		typedef R (__thiscall *F) (Interpreter *, A...);
		return ((F)(*(void *const *const *)p)[s]) (p, a...);
	}
}

void WbInterpMade (Interpreter *p, const void *maker)
{
	AcquireSRWLockExclusive (&g_lock);
	if (!g_rec) {
		g_rec = new std::unordered_map<const Interpreter *, Rec>;
		g_own = new std::unordered_map<const void *, int>;
		g_ownVt = *(void *const *const *)p; // still this library's class: its constructor runs
		for (int f = 0; f < WB_VCOUNT; f++) (*g_own)[Resolve (g_ownVt[g_layout[LATEST].slot[f]])] = f;
		void (Interpreter::*t) (lua_State *) = &Interpreter::wb_term_out2010;
		(*g_own)[Resolve (*(void **)&t)] = OWN_TERM_OUT2010;
	}
	(*g_rec)[p] = Rec {maker, -1}; // the version is chosen at the first call, when the class's constructor has set its table
	ReleaseSRWLockExclusive (&g_lock);
}

void WbInterpGone (Interpreter *p)
{
	AcquireSRWLockExclusive (&g_lock);
	if (g_rec) g_rec->erase (p);
	ReleaseSRWLockExclusive (&g_lock);
}

// ---------------------------------------------------------------------
// the calls: this class's own table for the latest version; a slot of the version's; this class's function where the version has none

void Interpreter::wb_v (int f)
{
	const Layout &l = LayoutOf (this);
	bool own = &l == &g_layout[LATEST];
	if (!own && l.slot[f] > 0) { VCall<void> (this, l.slot[f]); return; }
	switch (f) {
	case WB_Initialise: own ? Initialise () : Interpreter::Initialise (); break;
	case WB_EndExec: own ? EndExec () : Interpreter::EndExec (); break;
	case WB_LoadAPI: own ? LoadAPI () : Interpreter::LoadAPI (); break;
	case WB_LoadVesselAPI: own ? LoadVesselAPI () : Interpreter::LoadVesselAPI (); break;
	case WB_LoadMFDAPI: own ? LoadMFDAPI () : Interpreter::LoadMFDAPI (); break;
	case WB_LoadNTVERTEXAPI: own ? LoadNTVERTEXAPI () : Interpreter::LoadNTVERTEXAPI (); break;
	case WB_LoadLightEmitterMethods: own ? LoadLightEmitterMethods () : Interpreter::LoadLightEmitterMethods (); break;
	case WB_LoadBeaconMethods: own ? LoadBeaconMethods () : Interpreter::LoadBeaconMethods (); break;
	case WB_LoadCustomCameraMethods: own ? LoadCustomCameraMethods () : Interpreter::LoadCustomCameraMethods (); break;
	case WB_LoadSketchpadAPI: own ? LoadSketchpadAPI () : Interpreter::LoadSketchpadAPI (); break;
	case WB_LoadBitAPI: own ? LoadBitAPI () : Interpreter::LoadBitAPI (); break;
	case WB_LoadStartupScript: own ? LoadStartupScript () : Interpreter::LoadStartupScript (); break;
	case WB_term_clear: own ? term_clear () : Interpreter::term_clear (); break;
	case WB_LoadXRSoundAPI: own ? LoadXRSoundAPI () : Interpreter::LoadXRSoundAPI (); break;
	}
}

void Interpreter::wb_WaitExec (DWORD timeout)
{
	const Layout &l = LayoutOf (this);
	if (&l == &g_layout[LATEST]) WaitExec (timeout);
	else VCall<void, DWORD> (this, l.slot[WB_WaitExec], timeout);
}

int Interpreter::wb_Chunk (int f, const char *chunk, int n)
{
	const Layout &l = LayoutOf (this);
	if (&l != &g_layout[LATEST]) return VCall<int, const char *, int> (this, l.slot[f], chunk, n);
	return f == WB_ProcessChunk ? ProcessChunk (chunk, n) : RunChunk (chunk, n);
}

void Interpreter::wb_term_strout (const char *str, bool iserr)
{
	const Layout &l = LayoutOf (this);
	if (&l == &g_layout[LATEST]) term_strout (str, iserr);
	else if (l.flag) VCall<void, const char *, bool> (this, l.slot[WB_term_strout], str, iserr);
	else VCall<void, const char *> (this, l.slot[WB_term_strout], str);
}

void Interpreter::wb_term_out (lua_State *L, bool iserr)
{
	const Layout &l = LayoutOf (this);
	if (&l == &g_layout[LATEST]) term_out (L, iserr);
	else if (l.flag) VCall<void, lua_State *, bool> (this, l.slot[WB_term_out], L, iserr);
	else VCall<void, lua_State *> (this, l.slot[WB_term_out], L);
}

void Interpreter::wb_term_out2010 (lua_State *L)
{
	Interpreter::term_out (L, false);
}

void Interpreter::wb_term_strout2010 (const char *str)
{
}

extern "C" void *wb_hnote2010 = 0; // the 2010 class's private screen note, which no module reads

// ---------------------------------------------------------------------
// the 2010 names the 2024 class lacks, exported by them (compat.def)

struct WbInterp {
	static void TermStrout2010 (lua_State *L, const char *str);
};

void WbInterp::TermStrout2010 (lua_State *L, const char *str)
{
	Interpreter::term_strout (L, str, false);
}

extern "C" BOOL WINAPI DllMain (HINSTANCE, DWORD, LPVOID)
{
	return TRUE;
}
