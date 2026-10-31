// WineBridge helper: Linux's Lua states in the modules' hands. lua.dll's call on a remote state (wbr_) goes to Linux (L_LUA); a
// module's C function on one is called here (H_LUACALL, H_LUAHOOK) under a frame of this side's, so that a Lua error (Linux has
// caught it) unwinds the module's frames up to the call from Linux, as Lua's longjmp does on Windows; Linux raises it again there.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"
#include <lua/lua.h>
#define WB_HELPER
#include "../lua/wb_luaapi.h"

namespace {

// a remote state: the helper's object for a Linux state (or thread), by its id; never freed (an id stays its state's)
struct Remote { uint32_t next; uint8_t tt, marked; uint16_t pad; uint32_t id; };
Remote **g_rem;
uint32_t g_nrem;

lua_State *RemoteOf (uint32_t id)
{
	if (!id) return 0;
	if (id >= g_nrem) {
		uint32_t n = id + 64 > g_nrem * 2 ? id + 64 : g_nrem * 2;
		g_rem = (Remote **)realloc (g_rem, n * sizeof *g_rem);
		memset (g_rem + g_nrem, 0, (n - g_nrem) * sizeof *g_rem);
		g_nrem = n;
	}
	if (!g_rem[id]) {
		Remote *r = (Remote *)calloc (1, sizeof *r);
		r->tt = WB_REMOTE_TT;
		r->id = id;
		g_rem[id] = r;
	}
	return (lua_State *)g_rem[id];
}

uint32_t Sid (lua_State *L) { return ((Remote *)L)->id; }

// the calls from Linux the module's code runs in (the innermost on this thread): a Lua error ends the innermost
struct Frame { jmp_buf jb; Frame *up; int yield; };
DWORD g_tls = TlsAlloc ();
Frame *Top () { return (Frame *)TlsGetValue (g_tls); }

void Once (bool &said, const char *what) { if (!said) { said = true; wb::Log ("%s", what); } }

void Raise ()
{
	if (Frame *f = Top ()) longjmp (f->jb, 1);
	static bool said;
	Once (said, "a Windows module's Lua call on Orbiter's state raised an error outside a call from Lua; it goes on");
}

// strings and userdata copies: kept until the outermost call from Linux returns
struct Block { Block *next; uint32_t pad; };
Block *g_arena;
void *Keep (size_t n)
{
	Block *b = (Block *)malloc (sizeof (Block) + (n ? n : 1));
	b->next = g_arena;
	g_arena = b;
	return b + 1;
}
const char *KeepStr (const char *s, uint32_t n)
{
	if (!s) return 0;
	char *c = (char *)Keep (n + 1);
	memcpy (c, s, n);
	c[n] = 0;
	return c;
}
struct UdCopy { uint32_t ud; void *p; };
UdCopy *g_ud;
uint32_t g_nud, g_capud;

// the module's C functions on Linux's states (pushed, set as hooks or panic functions): only these are called from Linux; one whose
// module was freed is not
struct KnownFn { uint32_t fn; int mod; };
KnownFn *g_known;
uint32_t g_kcap, g_kn;
KnownFn *FindKnown (uint32_t fn)
{
	if (!g_kcap) return 0;
	for (uint32_t i = (fn * 2654435761u) & (g_kcap - 1);; i = (i + 1) & (g_kcap - 1)) {
		if (!g_known[i].fn) return 0;
		if (g_known[i].fn == fn) return &g_known[i];
	}
}
void AddKnown (uint32_t fn)
{
	if (!fn || FindKnown (fn)) return;
	if ((g_kn + 1) * 2 > g_kcap) {
		uint32_t cap = g_kcap ? g_kcap * 2 : 256;
		KnownFn *old = g_known;
		uint32_t oldcap = g_kcap;
		g_known = (KnownFn *)calloc (cap, sizeof *g_known);
		g_kcap = cap;
		for (uint32_t i = 0; i < oldcap; i++)
			if (old[i].fn) {
				uint32_t j = (old[i].fn * 2654435761u) & (cap - 1);
				while (g_known[j].fn) j = (j + 1) & (cap - 1);
				g_known[j] = old[i];
			}
		free (old);
	}
	uint32_t j = (fn * 2654435761u) & (g_kcap - 1);
	while (g_known[j].fn) j = (j + 1) & (g_kcap - 1);
	g_known[j] = {fn, wb::AddrModule (wb::Ptr (fn))};
	g_kn++;
}
bool Known (uint32_t fn)
{
	KnownFn *k = FindKnown (fn);
	return k && (!k->mod || wb::AddrModule (wb::Ptr (fn)) == k->mod);
}

// Linux's C functions and hooks in the module's hands: a stub of this side's for each (lua_tocfunction, lua_atpanic, lua_gethook)
const int NSTUB = 256, NHOOK = 8;
uint32_t g_stubId[NSTUB], g_hookId[NHOOK];
int CallLinux (int k, lua_State *L);
void HookLinux (int k, lua_State *L, lua_Debug *ar);
template <int k> int Stub (lua_State *L) { return CallLinux (k, L); }
template <int k> void HookStub (lua_State *L, lua_Debug *ar) { HookLinux (k, L, ar); }
#define WB_S8(b) Stub<b>, Stub<b + 1>, Stub<b + 2>, Stub<b + 3>, Stub<b + 4>, Stub<b + 5>, Stub<b + 6>, Stub<b + 7>
#define WB_S64(b) WB_S8 (b), WB_S8 (b + 8), WB_S8 (b + 16), WB_S8 (b + 24), WB_S8 (b + 32), WB_S8 (b + 40), WB_S8 (b + 48), WB_S8 (b + 56)
lua_CFunction const g_stub[NSTUB] = {WB_S64 (0), WB_S64 (64), WB_S64 (128), WB_S64 (192)};
lua_Hook const g_hook[NHOOK] = {HookStub<0>, HookStub<1>, HookStub<2>, HookStub<3>, HookStub<4>, HookStub<5>, HookStub<6>, HookStub<7>};

template <class F> F StubFor (uint32_t id, uint32_t *ids, F const *stubs, int n, const char *what)
{
	for (int k = 0; k < n; k++) if (ids[k] == id) return stubs[k];
	for (int k = 0; k < n; k++) if (!ids[k]) { ids[k] = id; return stubs[k]; }
	static bool said;
	Once (said, what);
	return 0;
}
template <class F> int StubOf (F f, F const *stubs, int n) { for (int k = 0; k < n; k++) if (stubs[k] == f) return k; return -1; }

void PutFn (WbWriter &w, lua_CFunction f)
{
	int k = StubOf (f, g_stub, NSTUB);
	if (!f) { w.i32 (LF_NONE); w.u32 (0); }
	else if (k >= 0) { w.i32 (LF_LINUX); w.u32 (g_stubId[k]); }
	else { AddKnown (wb::Id ((void *)f)); w.i32 (LF_MODULE); w.u32 (wb::Id ((void *)f)); }
}

lua_CFunction GetFn (WbReader &r)
{
	int kind = r.i32 ();
	uint32_t v = r.u32 ();
	if (r.bad || kind == LF_NONE) return 0;
	if (kind == LF_MODULE) return (lua_CFunction)wb::Ptr (v);
	return StubFor (v, g_stubId, g_stub, NSTUB, "more of Orbiter's Lua functions in a Windows module's hands than the bridge has stubs for");
}

void PutHook (WbWriter &w, lua_Hook f)
{
	int k = StubOf (f, g_hook, NHOOK);
	if (!f) { w.i32 (LF_NONE); w.u32 (0); }
	else if (k >= 0) { w.i32 (LF_LINUX); w.u32 (g_hookId[k]); }
	else { AddKnown (wb::Id ((void *)f)); w.i32 (LF_MODULE); w.u32 (wb::Id ((void *)f)); }
}

lua_CFunction LuaError () // lua.dll's luaL_error, for an error on the module's own state
{
	HMODULE h = GetModuleHandleA ("lua.dll");
	return h ? (lua_CFunction)GetProcAddress (h, "luaL_error") : 0;
}

WbWriter &Op (lua_State *L, int op)
{
	WbWriter &w = wb::Begin (L_LUA);
	w.u32 (Sid (L));
	w.i32 (op);
	return w;
}
WbReader Ask (lua_State *L, int op) { Op (L, op); return wb::Call (); }
WbReader Ask1 (lua_State *L, int op, int a) { Op (L, op).i32 (a); return wb::Call (); }
WbReader Ask2 (lua_State *L, int op, int a, int b) { WbWriter &w = Op (L, op); w.i32 (a); w.i32 (b); return wb::Call (); }
WbReader Checked (WbReader r) { if (r.b ()) Raise (); return r; } // an op Lua may raise in: its error raised here

int CallLinux (int k, lua_State *L)
{
	if (!wb_remote (L)) { // a Linux function on the module's own state
		typedef int (*ErrorFn) (lua_State *, const char *, ...);
		if (ErrorFn e = (ErrorFn)LuaError ()) return e (L, "a function of Orbiter's Lua state called on a Windows module's own state");
		return 0;
	}
	Op (L, LO_callc).u32 (g_stubId[k]);
	return Checked (wb::Call ()).i32 ();
}

void HookLinux (int k, lua_State *L, lua_Debug *ar)
{
	if (!wb_remote (L)) return;
	WbWriter &w = Op (L, LO_callhook);
	w.u32 (g_hookId[k]); w.i32 (ar->event); w.i32 (ar->currentline); w.i32 (ar->i_ci);
	Checked (wb::Call ());
}

// lua_touserdata's: a light one's value, the module's block, or a copy (a vessel's object, a handle, the bytes) for this call from Linux
void *Copy (uint32_t ud, const void *p, uint32_t n)
{
	for (uint32_t i = 0; i < g_nud; i++) if (g_ud[i].ud == ud) return g_ud[i].p;
	void *c = Keep (n);
	memcpy (c, p, n);
	if (g_nud == g_capud) { g_capud = g_capud ? g_capud * 2 : 16; g_ud = (UdCopy *)realloc (g_ud, g_capud * sizeof *g_ud); }
	g_ud[g_nud++] = {ud, c};
	return c;
}

void *UdOf (WbReader &r)
{
	switch (r.i32 ()) {
	case LU_LIGHT: case LU_BLOCK: return wb::Ptr (r.u32 ());
	case LU_VESSEL: {
		uint32_t vid = r.u32 (), ud = r.u32 ();
		if (r.bad) return 0;
		VESSEL *v = vid ? oapiGetVesselInterface ((OBJHANDLE)wb::Ptr (vid)) : 0; // may call Linux: the reply is read
		return Copy (ud, &v, sizeof v); }
	case LU_ID: {
		uint32_t id = r.u32 (), ud = r.u32 ();
		void *p = wb::Ptr (id);
		return r.bad ? 0 : Copy (ud, &p, sizeof p); }
	case LU_BYTES: {
		uint32_t ud = r.u32 (), n;
		const char *s = r.str (&n);
		return r.bad || !s ? 0 : Copy (ud, s, n); }
	default: return 0;
	}
}

bool Mapped (const void *p) // memory of this process: a module's own value, not a handle
{
	if ((uintptr_t)p < 0x10000) return false;
	MEMORY_BASIC_INFORMATION mi;
	return VirtualQuery (p, &mi, sizeof mi) && mi.State != MEM_FREE;
}

void *__cdecl HelperAlloc (void *, void *p, size_t, size_t n)
{
	if (!n) { free (p); return 0; }
	return realloc (p, n);
}

// a module's function (or hook, or clbkGeneric) Linux calls with its state, under a frame
struct CallCtx { int kind; lua_CFunction f; lua_Hook h; lua_State *L; lua_Debug *ar; VESSEL3 *v; int msg, prm; };
int Guarded (CallCtx &c, int *n)
{
	Frame fr;
	fr.up = Top ();
	fr.yield = -1;
	TlsSetValue (g_tls, &fr);
	int end;
	if (!setjmp (fr.jb)) {
		int res = c.kind == 0 ? c.f (c.L) : c.kind == 1 ? (c.h (c.L, c.ar), 0) : c.v->clbkGeneric (c.msg, c.prm, c.L);
		end = fr.yield >= 0 ? LE_YIELD : LE_OK;
		*n = fr.yield >= 0 ? fr.yield : res;
	} else {
		end = LE_ERROR;
		*n = 0;
	}
	TlsSetValue (g_tls, fr.up);
	return end;
}

} // namespace

// ---------------------------------------------------------------------
// lua.dll's calls on a remote state

extern "C" {

lua_State *wbr_newthread (lua_State *L) { return RemoteOf (Checked (Ask (L, LO_newthread)).u32 ()); }

lua_CFunction wbr_atpanic (lua_State *L, lua_CFunction f)
{
	PutFn (Op (L, LO_atpanic), f);
	WbReader r = wb::Call ();
	return GetFn (r);
}

int wbr_gettop (lua_State *L) { return Ask (L, LO_gettop).i32 (); }
void wbr_settop (lua_State *L, int idx) { Ask1 (L, LO_settop, idx); }
void wbr_pushvalue (lua_State *L, int idx) { Ask1 (L, LO_pushvalue, idx); }
void wbr_remove (lua_State *L, int idx) { Ask1 (L, LO_remove, idx); }
void wbr_insert (lua_State *L, int idx) { Ask1 (L, LO_insert, idx); }
void wbr_replace (lua_State *L, int idx) { Ask1 (L, LO_replace, idx); }
int wbr_checkstack (lua_State *L, int sz) { return Ask1 (L, LO_checkstack, sz).i32 (); }

void wbr_xmove (lua_State *from, lua_State *to, int n)
{
	if (!wb_remote (from) || !wb_remote (to)) {
		static bool said;
		Once (said, "a Windows module moved values between its own Lua state and Orbiter's; they stay");
		return;
	}
	WbWriter &w = Op (from, LO_xmove);
	w.u32 (Sid (to));
	w.i32 (n);
	wb::Call ();
}

int wbr_isnumber (lua_State *L, int idx) { return Ask1 (L, LO_isnumber, idx).i32 (); }
int wbr_isstring (lua_State *L, int idx) { return Ask1 (L, LO_isstring, idx).i32 (); }
int wbr_iscfunction (lua_State *L, int idx) { return Ask1 (L, LO_iscfunction, idx).i32 (); }
int wbr_isuserdata (lua_State *L, int idx) { return Ask1 (L, LO_isuserdata, idx).i32 (); }
int wbr_type (lua_State *L, int idx) { WbReader r = Ask1 (L, LO_type, idx); int t = r.i32 (); return r.bad ? LUA_TNONE : t; }
int wbr_equal (lua_State *L, int i1, int i2) { return Checked (Ask2 (L, LO_equal, i1, i2)).i32 (); }
int wbr_rawequal (lua_State *L, int i1, int i2) { return Ask2 (L, LO_rawequal, i1, i2).i32 (); }
int wbr_lessthan (lua_State *L, int i1, int i2) { return Checked (Ask2 (L, LO_lessthan, i1, i2)).i32 (); }
lua_Number wbr_tonumber (lua_State *L, int idx) { return Ask1 (L, LO_tonumber, idx).f64 (); }
int wbr_toboolean (lua_State *L, int idx) { return Ask1 (L, LO_toboolean, idx).i32 (); }

const char *wbr_tolstring (lua_State *L, int idx, size_t *len)
{
	WbReader r = Checked (Ask1 (L, LO_tolstring, idx));
	uint32_t n;
	const char *s = r.str (&n);
	if (len) *len = s ? n : 0;
	return KeepStr (s, n);
}

size_t wbr_objlen (lua_State *L, int idx) { return Ask1 (L, LO_objlen, idx).u32 (); }

lua_CFunction wbr_tocfunction (lua_State *L, int idx)
{
	WbReader r = Ask1 (L, LO_tocfunction, idx);
	return GetFn (r);
}

void *wbr_touserdata (lua_State *L, int idx)
{
	WbReader r = Ask1 (L, LO_touserdata, idx);
	return UdOf (r);
}

lua_State *wbr_tothread (lua_State *L, int idx) { return RemoteOf (Ask1 (L, LO_tothread, idx).u32 ()); }

const void *wbr_topointer (lua_State *L, int idx)
{
	WbReader r = Ask1 (L, LO_topointer, idx);
	int t = r.i32 ();
	if (t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA) return UdOf (r);
	return wb::Ptr (r.u32 ());
}

void wbr_pushnil (lua_State *L) { Ask (L, LO_pushnil); }
void wbr_pushnumber (lua_State *L, lua_Number n) { Op (L, LO_pushnumber).f64 (n); wb::Call (); }
void wbr_pushinteger (lua_State *L, lua_Integer n) { Ask1 (L, LO_pushinteger, (int)n); }

void wbr_pushlstring (lua_State *L, const char *s, size_t l)
{
	WbWriter &w = Op (L, LO_pushlstring);
	w.u32 ((uint32_t)l);
	w.raw (s ? s : "", (uint32_t)l);
	Checked (wb::Call ());
}

void wbr_pushstring (lua_State *L, const char *s)
{
	if (!s) wbr_pushnil (L);
	else wbr_pushlstring (L, s, strlen (s));
}

// formatted here as Lua formats (luaO_pushvfstring: %s %c %d %f %p %%), then pushed
const char *wbr_pushvfstring (lua_State *L, const char *fmt, va_list ap)
{
	char *b = 0;
	size_t n = 0, cap = 0;
	auto add = [&] (const char *s, size_t k) {
		if (n + k + 1 > cap) { cap = (n + k + 1) * 2; b = (char *)realloc (b, cap); }
		memcpy (b + n, s, k);
		n += k;
	};
	for (;;) {
		const char *e = strchr (fmt, '%');
		if (!e) break;
		add (fmt, e - fmt);
		char t[64];
		const char *s = t;
		switch (e[1]) {
		case 's': s = va_arg (ap, char *); if (!s) s = "(null)"; break;
		case 'c': t[0] = (char)va_arg (ap, int); t[1] = 0; break;
		case 'd': sprintf (t, "%.14g", (double)va_arg (ap, int)); break;
		case 'f': sprintf (t, "%.14g", va_arg (ap, double)); break;
		case 'p': sprintf (t, "%p", va_arg (ap, void *)); break;
		case '%': s = "%"; break;
		default: t[0] = '%'; t[1] = e[1]; t[2] = 0; break;
		}
		add (s, strlen (s));
		if (!e[1]) { fmt = e + 1; break; }
		fmt = e + 2;
	}
	add (fmt, strlen (fmt));
	const char *c = KeepStr (b ? b : "", (uint32_t)n);
	free (b);
	wbr_pushlstring (L, c, n);
	return c;
}

void wbr_pushcclosure (lua_State *L, lua_CFunction fn, int n)
{
	WbWriter &w = Op (L, LO_pushcclosure);
	if (fn) PutFn (w, fn);
	else { w.i32 (LF_MODULE); w.u32 (0); } // Lua makes it; called, it fails
	w.i32 (n);
	Checked (wb::Call ());
}

void wbr_pushboolean (lua_State *L, int b) { Ask1 (L, LO_pushboolean, b); }

void wbr_pushlightuserdata (lua_State *L, void *p)
{
	WbWriter &w = Op (L, LO_pushlightuserdata);
	w.u32 (wb::Id (p));
	w.b (Mapped (p));
	wb::Call ();
}

int wbr_pushthread (lua_State *L) { return Ask (L, LO_pushthread).i32 (); }
void wbr_gettable (lua_State *L, int idx) { Checked (Ask1 (L, LO_gettable, idx)); }

void wbr_getfield (lua_State *L, int idx, const char *k)
{
	WbWriter &w = Op (L, LO_getfield);
	w.i32 (idx);
	w.str (k);
	Checked (wb::Call ());
}

void wbr_rawget (lua_State *L, int idx) { Ask1 (L, LO_rawget, idx); }
void wbr_rawgeti (lua_State *L, int idx, int n) { Ask2 (L, LO_rawgeti, idx, n); }
void wbr_createtable (lua_State *L, int narr, int nrec) { Checked (Ask2 (L, LO_createtable, narr, nrec)); }

// the module's block; Linux's userdata of the same size stands for it, and when Lua collects that, the block is freed (H_LUAFREE)
void *wbr_newuserdata (lua_State *L, size_t sz)
{
	void *b = malloc (sz ? sz : 1);
	WbWriter &w = Op (L, LO_newuserdata);
	w.u32 ((uint32_t)sz);
	w.u32 (wb::Id (b));
	WbReader r = wb::Call ();
	if (r.b () || r.bad) { free (b); Raise (); return 0; }
	return b;
}

int wbr_getmetatable (lua_State *L, int idx) { return Ask1 (L, LO_getmetatable, idx).i32 (); }
void wbr_getfenv (lua_State *L, int idx) { Ask1 (L, LO_getfenv, idx); }
void wbr_settable (lua_State *L, int idx) { Checked (Ask1 (L, LO_settable, idx)); }

void wbr_setfield (lua_State *L, int idx, const char *k)
{
	WbWriter &w = Op (L, LO_setfield);
	w.i32 (idx);
	w.str (k);
	Checked (wb::Call ());
}

void wbr_rawset (lua_State *L, int idx) { Checked (Ask1 (L, LO_rawset, idx)); }
void wbr_rawseti (lua_State *L, int idx, int n) { Ask2 (L, LO_rawseti, idx, n); }
int wbr_setmetatable (lua_State *L, int idx) { return Ask1 (L, LO_setmetatable, idx).i32 (); }
int wbr_setfenv (lua_State *L, int idx) { return Ask1 (L, LO_setfenv, idx).i32 (); }
void wbr_call (lua_State *L, int nargs, int nresults) { Checked (Ask2 (L, LO_call, nargs, nresults)); }

int wbr_pcall (lua_State *L, int nargs, int nresults, int errfunc)
{
	WbWriter &w = Op (L, LO_pcall);
	w.i32 (nargs); w.i32 (nresults); w.i32 (errfunc);
	return wb::Call ().i32 ();
}

int wbr_cpcall (lua_State *L, lua_CFunction func, void *ud)
{
	WbWriter &w = Op (L, LO_cpcall);
	PutFn (w, func);
	w.u32 (wb::Id (ud));
	w.b (Mapped (ud));
	return wb::Call ().i32 ();
}

// the reader's whole chunk, loaded on Linux with the same name
int wbr_load (lua_State *L, lua_Reader reader, void *dt, const char *chunkname)
{
	char *b = 0;
	size_t n = 0, cap = 0;
	for (;;) {
		size_t k = 0;
		const char *p = reader (L, dt, &k);
		if (!p || !k) break;
		if (n + k > cap) { cap = (n + k) * 2; b = (char *)realloc (b, cap); }
		memcpy (b + n, p, k);
		n += k;
	}
	if (n > WB_MSG_MAX - 4096) {
		free (b);
		wbr_pushstring (L, "chunk too large for the bridge");
		return LUA_ERRMEM;
	}
	WbWriter &w = Op (L, LO_load);
	w.u32 ((uint32_t)n);
	w.raw (b ? b : "", (uint32_t)n);
	w.str (chunkname ? chunkname : "?");
	free (b);
	return wb::Call ().i32 ();
}

int wbr_dump (lua_State *L, lua_Writer writer, void *data)
{
	WbReader r = Ask (L, LO_dump);
	int st = r.i32 ();
	uint32_t n;
	const char *s = r.str (&n);
	if (r.bad) return 1;
	if (st || !s || !n) return st;
	void *c = malloc (n); // the writer may call Lua, whose calls reuse the buffer
	memcpy (c, s, n);
	st = writer (L, c, n, data);
	free (c);
	return st;
}

int wbr_yield (lua_State *L, int nresults) // the closure on Linux yields when the module's function returns
{
	if (Frame *f = Top ()) f->yield = nresults;
	return -1;
}

int wbr_resume (lua_State *L, int narg) { return Ask1 (L, LO_resume, narg).i32 (); }
int wbr_status (lua_State *L) { return Ask (L, LO_status).i32 (); }
int wbr_gc (lua_State *L, int what, int data) { return Checked (Ask2 (L, LO_gc, what, data)).i32 (); }

int wbr_error (lua_State *L)
{
	Ask (L, LO_error);
	Raise ();
	return 0;
}

int wbr_next (lua_State *L, int idx) { return Checked (Ask1 (L, LO_next, idx)).i32 (); }
void wbr_concat (lua_State *L, int n) { Checked (Ask1 (L, LO_concat, n)); }

lua_Alloc wbr_getallocf (lua_State *L, void **ud) // memory of the helper's: Orbiter's allocator is Linux's
{
	if (ud) *ud = 0;
	return HelperAlloc;
}

void wbr_setallocf (lua_State *L, lua_Alloc f, void *ud)
{
	static bool said;
	Once (said, "a Windows module set the allocator of Orbiter's Lua state: Linux's stays");
}

void wbr_close (lua_State *L)
{
	static bool said;
	Once (said, "a Windows module closed Orbiter's Lua state: it stays open");
}

int wbr_getstack (lua_State *L, int level, lua_Debug *ar)
{
	WbReader r = Ask1 (L, LO_getstack, level);
	int ok = r.i32 (), ci = r.i32 ();
	if (ok && !r.bad) ar->i_ci = ci;
	return r.bad ? 0 : ok;
}

// the fields of the options asked for
int wbr_getinfo (lua_State *L, const char *what, lua_Debug *ar)
{
	WbWriter &w = Op (L, LO_getinfo);
	w.str (what);
	w.i32 (ar->i_ci);
	WbReader r = wb::Call ();
	int ok = r.i32 ();
	uint32_t n[5];
	const char *s[5];
	for (int i = 0; i < 4; i++) {
		const char *t = r.str (&n[i]);
		s[i] = KeepStr (t, n[i]);
	}
	int line = r.i32 (), nups = r.i32 (), ld = r.i32 (), lld = r.i32 ();
	s[4] = r.str (&n[4]);
	if (r.bad) return 0;
	for (const char *p = what; *p; p++)
		switch (*p) {
		case 'n': ar->name = s[0]; ar->namewhat = s[1]; break;
		case 'S':
			ar->what = s[2]; ar->source = s[3]; ar->linedefined = ld; ar->lastlinedefined = lld;
			if (s[4]) { uint32_t k = n[4] < LUA_IDSIZE - 1 ? n[4] : LUA_IDSIZE - 1; memcpy (ar->short_src, s[4], k); ar->short_src[k] = 0; }
			break;
		case 'l': ar->currentline = line; break;
		case 'u': ar->nups = nups; break;
		}
	return ok;
}

const char *wbr_getlocal (lua_State *L, const lua_Debug *ar, int n)
{
	WbReader r = Ask2 (L, LO_getlocal, ar->i_ci, n);
	uint32_t k;
	const char *s = r.str (&k);
	return KeepStr (s, k);
}

const char *wbr_setlocal (lua_State *L, const lua_Debug *ar, int n)
{
	WbReader r = Ask2 (L, LO_setlocal, ar->i_ci, n);
	uint32_t k;
	const char *s = r.str (&k);
	return KeepStr (s, k);
}

const char *wbr_getupvalue (lua_State *L, int funcindex, int n)
{
	WbReader r = Ask2 (L, LO_getupvalue, funcindex, n);
	uint32_t k;
	const char *s = r.str (&k);
	return KeepStr (s, k);
}

const char *wbr_setupvalue (lua_State *L, int funcindex, int n)
{
	WbReader r = Ask2 (L, LO_setupvalue, funcindex, n);
	uint32_t k;
	const char *s = r.str (&k);
	return KeepStr (s, k);
}

int wbr_sethook (lua_State *L, lua_Hook func, int mask, int count)
{
	WbWriter &w = Op (L, LO_sethook);
	PutHook (w, func);
	w.i32 (mask); w.i32 (count);
	return wb::Call ().i32 ();
}

lua_Hook wbr_gethook (lua_State *L)
{
	WbReader r = Ask (L, LO_gethook);
	int kind = r.i32 ();
	uint32_t v = r.u32 ();
	if (r.bad || kind == LF_NONE) return 0;
	if (kind == LF_MODULE) return (lua_Hook)wb::Ptr (v);
	return StubFor (v, g_hookId, g_hook, NHOOK, "more of Orbiter's Lua hooks in a Windows module's hands than the bridge has stubs for");
}

int wbr_gethookmask (lua_State *L) { return Ask (L, LO_gethookmask).i32 (); }
int wbr_gethookcount (lua_State *L) { return Ask (L, LO_gethookcount).i32 (); }

} // extern "C"

// ---------------------------------------------------------------------
// Linux's calls

// H_LUACALL: a module's C function on Linux's state; H_LUAHOOK: its hook
void wb::ServeLua (uint32_t fn, WbReader &r, WbWriter &w)
{
	if (fn == H_LUAFREE) { // read all, then free
		uint32_t n = r.u32 ();
		if (r.bad || n > WB_MSG_MAX / 4) return;
		uint32_t *b = (uint32_t *)malloc ((n ? n : 1) * sizeof *b);
		for (uint32_t i = 0; i < n; i++) b[i] = r.u32 ();
		if (!r.bad) for (uint32_t i = 0; i < n; i++) free (wb::Ptr (b[i]));
		free (b);
		wb::LuaServeFreed (w); // the native blocks the helper's Lua collected (luaserve.cpp)
		return;
	}
	uint32_t f = r.u32 (), sid = r.u32 ();
	lua_Debug ar;
	memset (&ar, 0, sizeof ar);
	if (fn == H_LUAHOOK) { ar.event = r.i32 (); ar.currentline = r.i32 (); ar.i_ci = r.i32 (); }
	int end = LE_UNKNOWN, n = 0;
	if (!r.bad && sid && Known (f)) {
		CallCtx c {};
		c.kind = fn == H_LUAHOOK ? 1 : 0;
		c.f = (lua_CFunction)wb::Ptr (f);
		c.h = (lua_Hook)wb::Ptr (f);
		c.L = RemoteOf (sid);
		c.ar = &ar;
		end = Guarded (c, &n);
		if (fn == H_LUAHOOK) n = 0;
	}
	w.i32 (end);
	w.i32 (n);
}

// C_Generic's VMSG_LUAINTERPRETER and VMSG_LUAINSTANCE: the module gets Linux's state (sid); err: it raised a Lua error
int wb::LuaGeneric (VESSEL3 *v, int msg, int prm, uint32_t sid, bool *err)
{
	CallCtx c {};
	c.kind = 2;
	c.v = v;
	c.msg = msg;
	c.prm = prm;
	c.L = RemoteOf (sid);
	int n = 0;
	*err = Guarded (c, &n) == LE_ERROR;
	return *err ? 0 : n;
}

// a remote state a module passes to a Linux vessel's clbkGeneric: its id (the Linux state), else 0
uint32_t wb::LuaRemoteId (const void *L) { return L && wb_remote ((lua_State *)L) ? Sid ((lua_State *)L) : 0; }

void wb::LuaRaise () { Raise (); }

// a served call that isn't one of Lua's: a Lua error in it doesn't unwind past it
void *wb::LuaBarrier ()
{
	void *t = Top ();
	TlsSetValue (g_tls, 0);
	return t;
}

void wb::LuaRestore (void *t) { TlsSetValue (g_tls, t); }

// the outermost call from Linux returned: the copies go
void wb::LuaRelease ()
{
	while (g_arena) {
		Block *b = g_arena;
		g_arena = b->next;
		free (b);
	}
	g_nud = 0;
}
