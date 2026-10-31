// WineBridge: the helper's Lua states in native modules' hands (a Windows module's interpreter gives a native vessel its state). lua.so's
// call on a remote state (the functions lua_setremote registered) goes to the helper (H_LUA); a native module's C function on one is
// called here (L_LUACALL, L_LUAHOOK) under a frame of this side's, so that a Lua error (the helper holds it) unwinds the module's frames
// up to the call from the helper, as Lua's longjmp does; the helper raises it again there.
#include "Orbitersdk.h"
#include "Bridge.h"
#include "Manual.h"
#include "lua.hpp"
#if __has_include ("lua_remote.h") // an Orbiter older than this part has no lua_setremote: see the end
extern "C" {
#include "lua_remote.h"
}
#define WB_LUA_REMOTE 1
#endif
#include <array>
#include <csetjmp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using Guard = std::lock_guard<std::recursive_mutex>;

// H_LUAFREE's reply: the native blocks the helper's Lua collected
void WbLuaRemoteFreed (WbReader &r)
{
	uint32_t n = r.u32 ();
	if (r.bad || n > WB_MSG_MAX / 4) return;
	std::vector<uint32_t> ids (n);
	for (uint32_t &id : ids) id = r.u32 ();
	if (r.bad) return;
	WbBridge &b = WbBridge::Get ();
	for (uint32_t id : ids) free (b.Ptr (id));
}

#ifdef WB_LUA_REMOTE

void WbLuaRechunk (std::string &c, int to); // Lua.cpp

namespace {

// a remote state: Linux's object for a helper state (or thread), by its id; never freed (an id stays its state's)
struct Remote { void *next; uint8_t tt, marked; uint32_t id; };
std::unordered_map<uint32_t, Remote *> &g_rem = *new std::unordered_map<uint32_t, Remote *>;

lua_State *RemoteOf (uint32_t id)
{
	if (!id) return nullptr;
	Remote *&r = g_rem[id];
	if (!r) { r = new Remote {}; r->tt = LUA_REMOTE_TT; r->id = id; }
	return (lua_State *)r;
}

uint32_t Sid (lua_State *L) { return ((Remote *)L)->id; }

// the calls from the helper the native module's code runs in (the innermost on this thread): a Lua error ends the innermost
struct Frame { jmp_buf jb; Frame *up; int yield; };
thread_local Frame *t_top;
int g_serving; // calls from the helper being served

void Once (bool &said, const char *what) { if (!said) { said = true; oapiWriteLogV ("WineBridge: %s", what); } }

void Raise ()
{
	if (t_top) longjmp (t_top->jb, 1);
	static bool said;
	Once (said, "a native module's Lua call on a Windows module's state raised an error outside a call from Lua; it goes on");
}

// strings and userdata copies: kept until the outermost call from the helper returns
std::vector<void *> &g_arena = *new std::vector<void *>;
void *Keep (size_t n)
{
	void *b = malloc (n ? n : 1);
	g_arena.push_back (b);
	return b;
}
const char *KeepStr (const char *s, uint32_t n)
{
	if (!s) return nullptr;
	char *c = (char *)Keep (n + 1);
	memcpy (c, s, n);
	c[n] = 0;
	return c;
}
std::unordered_map<uint32_t, void *> &g_ud = *new std::unordered_map<uint32_t, void *>; // copies by the helper's userdata

void Release ()
{
	for (void *b : g_arena) free (b);
	g_arena.clear ();
	g_ud.clear ();
}

// native C functions and hooks given to the helper: only these are called from it
std::unordered_set<void *> &g_given = *new std::unordered_set<void *>;
std::unordered_set<uintptr_t> &g_hlud = *new std::unordered_set<uintptr_t>; // lightuserdata values of the helper's, kept as given

// the helper's C functions and hooks in a native module's hands: a stub of this side's for each (lua_tocfunction, lua_atpanic, lua_gethook)
const int NSTUB = 256, NHOOK = 8;
uint32_t g_stubId[NSTUB], g_hookId[NHOOK];
int CallHelper (int k, lua_State *L);
void HookHelper (int k, lua_State *L, lua_Debug *ar);
template <int k> int Stub (lua_State *L) { return CallHelper (k, L); }
template <int k> void HookStub (lua_State *L, lua_Debug *ar) { HookHelper (k, L, ar); }
template <size_t... I> std::array<lua_CFunction, sizeof... (I)> Stubs (std::index_sequence<I...>) { return {Stub<(int)I>...}; }
template <size_t... I> std::array<lua_Hook, sizeof... (I)> HookStubs (std::index_sequence<I...>) { return {HookStub<(int)I>...}; }
const std::array<lua_CFunction, NSTUB> g_stub = Stubs (std::make_index_sequence<NSTUB> ());
const std::array<lua_Hook, NHOOK> g_hook = HookStubs (std::make_index_sequence<NHOOK> ());

template <class F, size_t N> F StubFor (uint32_t id, uint32_t *ids, const std::array<F, N> &stubs, const char *what)
{
	for (size_t k = 0; k < N; k++) if (ids[k] == id) return stubs[k];
	for (size_t k = 0; k < N; k++) if (!ids[k]) { ids[k] = id; return stubs[k]; }
	static bool said;
	Once (said, what);
	return nullptr;
}
template <class F, size_t N> int StubOf (F f, const std::array<F, N> &stubs) { for (size_t k = 0; k < N; k++) if (stubs[k] == f) return (int)k; return -1; }

template <class F> void PutFn (WbWriter &w, F f, const uint32_t *ids, int k)
{
	if (!f) { w.i32 (LF_NONE); w.u32 (0); }
	else if (k >= 0) { w.i32 (LF_MODULE); w.u32 (ids[k]); }
	else { void *p = reinterpret_cast<void *> (f); g_given.insert (p); w.i32 (LF_LINUX); w.u32 (WbBridge::Get ().Id (p)); }
}
void PutFn (WbWriter &w, lua_CFunction f) { PutFn (w, f, g_stubId, StubOf (f, g_stub)); }
void PutHook (WbWriter &w, lua_Hook f) { PutFn (w, f, g_hookId, StubOf (f, g_hook)); }

void *Given (uint32_t id) // a native function given to the helper, by its id; null if not one
{
	void *p = WbBridge::Get ().Ptr (id);
	return p && g_given.count (p) ? p : nullptr;
}

lua_CFunction GetFn (WbReader &r)
{
	int kind = r.i32 ();
	uint32_t v = r.u32 ();
	if (r.bad || kind == LF_NONE) return nullptr;
	if (kind == LF_LINUX) return (lua_CFunction)Given (v);
	return StubFor (v, g_stubId, g_stub, "more of a Windows module's Lua functions in native hands than the bridge has stubs for");
}

// one call on the helper's state; the lock is held only while it runs
struct Ask {
	WbReader r;
	Ask (lua_State *L, int op, void (*args) (WbWriter &, const void *) = nullptr, const void *a = nullptr)
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		if (!b.Alive ()) return;
		WbWriter &w = b.Begin (H_LUA);
		w.u32 (Sid (L));
		w.i32 (op);
		if (args) args (w, a);
		r = b.Call ();
	}
};
WbReader Op0 (lua_State *L, int op) { return Ask (L, op).r; }
WbReader Op1 (lua_State *L, int op, int x) { return Ask (L, op, [] (WbWriter &w, const void *a) { w.i32 (*(const int *)a); }, &x).r; }
WbReader Op2 (lua_State *L, int op, int x, int y)
{
	int a[2] = {x, y};
	return Ask (L, op, [] (WbWriter &w, const void *p) { w.i32 (((const int *)p)[0]); w.i32 (((const int *)p)[1]); }, a).r;
}
template <class F> WbReader OpF (lua_State *L, int op, F &&f) // arguments written by f
{
	return Ask (L, op, [] (WbWriter &w, const void *p) { (*(F *)p) (w); }, &f).r;
}
WbReader Checked (WbReader r) { if (r.b ()) Raise (); return r; } // an op Lua may raise in: its error raised here

int CallHelper (int k, lua_State *L)
{
	if (!lua_isremote (L)) return luaL_error (L, "a function of a Windows module's Lua state called on Orbiter's own state");
	uint32_t f = g_stubId[k];
	return Checked (OpF (L, LO_callc, [f] (WbWriter &w) { w.u32 (f); })).i32 ();
}

void HookHelper (int k, lua_State *L, lua_Debug *ar)
{
	if (!lua_isremote (L)) return;
	uint32_t f = g_hookId[k];
	Checked (OpF (L, LO_callhook, [f, ar] (WbWriter &w) { w.u32 (f); w.i32 (ar->event); w.i32 (ar->currentline); w.i32 (ar->i_ci); }));
}

void *Copy (uint32_t ud, const void *p, size_t n)
{
	auto it = g_ud.find (ud);
	if (it != g_ud.end ()) return it->second;
	void *c = Keep (n);
	memcpy (c, p, n);
	g_ud[ud] = c;
	return c;
}

// lua_touserdata's: a light one's value, a native block, or a copy (a vessel's object, a handle, the bytes) for this call from the helper
void *UdOf (WbReader &r)
{
	WbBridge &b = WbBridge::Get ();
	switch (r.i32 ()) {
	case LU_LIGHT: {
		uint32_t v = r.u32 ();
		bool lnx = r.b ();
		if (r.bad || !v) return nullptr;
		if (lnx) return b.Ptr (v);
		g_hlud.insert (v);
		return (void *)(uintptr_t)v; }
	case LU_BLOCK: return b.Ptr (r.u32 ());
	case LU_VESSEL: {
		uint32_t vid = r.u32 (), ud = r.u32 ();
		if (r.bad) return nullptr;
		OBJHANDLE h = (OBJHANDLE)b.Ptr (vid);
		VESSEL *v = h && oapiIsVessel (h) ? oapiGetVesselInterface (h) : nullptr;
		return Copy (ud, &v, sizeof v); }
	case LU_ID: {
		uint32_t id = r.u32 (), ud = r.u32 ();
		void *p = b.Ptr (id);
		return r.bad ? nullptr : Copy (ud, &p, sizeof p); }
	case LU_BYTES: {
		uint32_t ud = r.u32 (), n;
		const char *s = r.str (&n);
		return r.bad || !s ? nullptr : Copy (ud, s, n); }
	default: return nullptr;
	}
}

// a native function (or hook, or clbkGeneric) the helper calls with its state, under a frame
struct CallCtx { int kind; lua_CFunction f; lua_Hook h; lua_State *L; lua_Debug *ar; VESSEL3 *v; int msg, prm; };
int Guarded (CallCtx &c, int *n)
{
	Frame fr;
	fr.up = t_top;
	fr.yield = -1;
	t_top = &fr;
	int end;
	if (!setjmp (fr.jb)) {
		int res = c.kind == 0 ? c.f (c.L) : c.kind == 1 ? (c.h (c.L, c.ar), 0) : c.v->clbkGeneric (c.msg, c.prm, c.L);
		end = fr.yield >= 0 ? LE_YIELD : LE_OK;
		*n = fr.yield >= 0 ? fr.yield : res;
	} else {
		end = LE_ERROR;
		*n = 0;
	}
	t_top = fr.up;
	return end;
}

void *HelperAlloc (void *, void *p, size_t, size_t n)
{
	if (!n) { free (p); return nullptr; }
	return realloc (p, n);
}

// ---------------------------------------------------------------------
// lua.so's calls on a remote state

lua_State *r_newthread (lua_State *L) { return RemoteOf (Checked (Op0 (L, LO_newthread)).u32 ()); }

lua_CFunction r_atpanic (lua_State *L, lua_CFunction f)
{
	WbReader r = OpF (L, LO_atpanic, [f] (WbWriter &w) { PutFn (w, f); });
	return GetFn (r);
}

int r_gettop (lua_State *L) { return Op0 (L, LO_gettop).i32 (); }
void r_settop (lua_State *L, int idx) { Op1 (L, LO_settop, idx); }
void r_pushvalue (lua_State *L, int idx) { Op1 (L, LO_pushvalue, idx); }
void r_remove (lua_State *L, int idx) { Op1 (L, LO_remove, idx); }
void r_insert (lua_State *L, int idx) { Op1 (L, LO_insert, idx); }
void r_replace (lua_State *L, int idx) { Op1 (L, LO_replace, idx); }
int r_checkstack (lua_State *L, int sz) { return Op1 (L, LO_checkstack, sz).i32 (); }

void r_xmove (lua_State *from, lua_State *to, int n)
{
	if (!lua_isremote (from) || !lua_isremote (to)) {
		static bool said;
		Once (said, "a native module moved values between Orbiter's Lua state and a Windows module's; they stay");
		return;
	}
	uint32_t t = Sid (to);
	OpF (from, LO_xmove, [t, n] (WbWriter &w) { w.u32 (t); w.i32 (n); });
}

int r_isnumber (lua_State *L, int idx) { return Op1 (L, LO_isnumber, idx).i32 (); }
int r_isstring (lua_State *L, int idx) { return Op1 (L, LO_isstring, idx).i32 (); }
int r_iscfunction (lua_State *L, int idx) { return Op1 (L, LO_iscfunction, idx).i32 (); }
int r_isuserdata (lua_State *L, int idx) { return Op1 (L, LO_isuserdata, idx).i32 (); }
int r_type (lua_State *L, int idx) { WbReader r = Op1 (L, LO_type, idx); int t = r.i32 (); return r.bad ? LUA_TNONE : t; }
int r_equal (lua_State *L, int i1, int i2) { return Checked (Op2 (L, LO_equal, i1, i2)).i32 (); }
int r_rawequal (lua_State *L, int i1, int i2) { return Op2 (L, LO_rawequal, i1, i2).i32 (); }
int r_lessthan (lua_State *L, int i1, int i2) { return Checked (Op2 (L, LO_lessthan, i1, i2)).i32 (); }
lua_Number r_tonumber (lua_State *L, int idx) { return Op1 (L, LO_tonumber, idx).f64 (); }
int r_toboolean (lua_State *L, int idx) { return Op1 (L, LO_toboolean, idx).i32 (); }

const char *r_tolstring (lua_State *L, int idx, size_t *len)
{
	WbReader r = Checked (Op1 (L, LO_tolstring, idx));
	uint32_t n;
	const char *s = r.str (&n);
	if (len) *len = s ? n : 0;
	return KeepStr (s, n);
}

size_t r_objlen (lua_State *L, int idx) { return Op1 (L, LO_objlen, idx).u32 (); }

lua_CFunction r_tocfunction (lua_State *L, int idx)
{
	WbReader r = Op1 (L, LO_tocfunction, idx);
	return GetFn (r);
}

void *r_touserdata (lua_State *L, int idx)
{
	WbReader r = Op1 (L, LO_touserdata, idx);
	return UdOf (r);
}

lua_State *r_tothread (lua_State *L, int idx) { return RemoteOf (Op1 (L, LO_tothread, idx).u32 ()); }

const void *r_topointer (lua_State *L, int idx)
{
	WbReader r = Op1 (L, LO_topointer, idx);
	int t = r.i32 ();
	if (t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA) return UdOf (r);
	uint32_t v = r.u32 ();
	return r.bad ? nullptr : (const void *)(uintptr_t)v; // the helper's address: only its identity means something
}

void r_pushnil (lua_State *L) { Op0 (L, LO_pushnil); }
void r_pushnumber (lua_State *L, lua_Number n) { OpF (L, LO_pushnumber, [n] (WbWriter &w) { w.f64 (n); }); }
void r_pushinteger (lua_State *L, lua_Integer n) { Op1 (L, LO_pushinteger, (int)n); }

void r_pushlstring (lua_State *L, const char *s, size_t l)
{
	if (l > WB_MSG_MAX / 2) l = 0;
	Checked (OpF (L, LO_pushlstring, [s, l] (WbWriter &w) { w.u32 ((uint32_t)l); w.raw (s ? s : "", (uint32_t)l); }));
}

void r_pushstring (lua_State *L, const char *s)
{
	if (!s) r_pushnil (L);
	else r_pushlstring (L, s, strlen (s));
}

// formatted here as Lua formats (luaO_pushvfstring: %s %c %d %f %p %%), then pushed
const char *r_pushvfstring (lua_State *L, const char *fmt, va_list ap)
{
	std::string b;
	for (;;) {
		const char *e = strchr (fmt, '%');
		if (!e) break;
		b.append (fmt, e - fmt);
		char t[64];
		const char *s = t;
		switch (e[1]) {
		case 's': s = va_arg (ap, char *); if (!s) s = "(null)"; break;
		case 'c': t[0] = (char)va_arg (ap, int); t[1] = 0; break;
		case 'd': snprintf (t, sizeof t, "%.14g", (double)va_arg (ap, int)); break;
		case 'f': snprintf (t, sizeof t, "%.14g", va_arg (ap, double)); break;
		case 'p': snprintf (t, sizeof t, "%p", va_arg (ap, void *)); break;
		case '%': s = "%"; break;
		default: t[0] = '%'; t[1] = e[1]; t[2] = 0; break;
		}
		b += s;
		if (!e[1]) { fmt = e + 1; break; }
		fmt = e + 2;
	}
	b += fmt;
	const char *c = KeepStr (b.data (), (uint32_t)b.size ());
	r_pushlstring (L, c, b.size ());
	return c;
}

void r_pushcclosure (lua_State *L, lua_CFunction fn, int n)
{
	Checked (OpF (L, LO_pushcclosure, [fn, n] (WbWriter &w) {
		if (fn) PutFn (w, fn);
		else { w.i32 (LF_LINUX); w.u32 (0); } // Lua makes it; called, it fails
		w.i32 (n);
	}));
}

void r_pushboolean (lua_State *L, int b) { Op1 (L, LO_pushboolean, b); }

void r_pushlightuserdata (lua_State *L, void *p)
{
	uintptr_t v = (uintptr_t)p;
	bool helper = v && v <= 0xffffffffu && g_hlud.count (v);
	uint32_t x = !v ? 0 : helper ? (uint32_t)v : WbBridge::Get ().Id (p);
	OpF (L, LO_pushlightuserdata, [x, helper] (WbWriter &w) { w.u32 (x); w.b (x && !helper); });
}

int r_pushthread (lua_State *L) { return Op0 (L, LO_pushthread).i32 (); }
void r_gettable (lua_State *L, int idx) { Checked (Op1 (L, LO_gettable, idx)); }

void r_getfield (lua_State *L, int idx, const char *k)
{
	Checked (OpF (L, LO_getfield, [idx, k] (WbWriter &w) { w.i32 (idx); w.str (k); }));
}

void r_rawget (lua_State *L, int idx) { Op1 (L, LO_rawget, idx); }
void r_rawgeti (lua_State *L, int idx, int n) { Op2 (L, LO_rawgeti, idx, n); }
void r_createtable (lua_State *L, int narr, int nrec) { Checked (Op2 (L, LO_createtable, narr, nrec)); }

// the native module's block; the helper's userdata of the same size stands for it, and when its Lua collects that, the block is freed
void *r_newuserdata (lua_State *L, size_t sz)
{
	void *b = malloc (sz ? sz : 1);
	uint32_t id = WbBridge::Get ().Id (b);
	WbReader r = OpF (L, LO_newuserdata, [sz, id] (WbWriter &w) { w.u32 ((uint32_t)sz); w.u32 (id); });
	if (r.b () || r.bad) { free (b); Raise (); return nullptr; }
	return b;
}

int r_getmetatable (lua_State *L, int idx) { return Op1 (L, LO_getmetatable, idx).i32 (); }
void r_getfenv (lua_State *L, int idx) { Op1 (L, LO_getfenv, idx); }
void r_settable (lua_State *L, int idx) { Checked (Op1 (L, LO_settable, idx)); }

void r_setfield (lua_State *L, int idx, const char *k)
{
	Checked (OpF (L, LO_setfield, [idx, k] (WbWriter &w) { w.i32 (idx); w.str (k); }));
}

void r_rawset (lua_State *L, int idx) { Checked (Op1 (L, LO_rawset, idx)); }
void r_rawseti (lua_State *L, int idx, int n) { Op2 (L, LO_rawseti, idx, n); }
int r_setmetatable (lua_State *L, int idx) { return Op1 (L, LO_setmetatable, idx).i32 (); }
int r_setfenv (lua_State *L, int idx) { return Op1 (L, LO_setfenv, idx).i32 (); }
void r_call (lua_State *L, int nargs, int nresults) { Checked (Op2 (L, LO_call, nargs, nresults)); }

int r_pcall (lua_State *L, int nargs, int nresults, int errfunc)
{
	return OpF (L, LO_pcall, [=] (WbWriter &w) { w.i32 (nargs); w.i32 (nresults); w.i32 (errfunc); }).i32 ();
}

int r_cpcall (lua_State *L, lua_CFunction func, void *ud)
{
	uintptr_t v = (uintptr_t)ud;
	bool helper = v && v <= 0xffffffffu && g_hlud.count (v);
	uint32_t x = !v ? 0 : helper ? (uint32_t)v : WbBridge::Get ().Id (ud);
	return OpF (L, LO_cpcall, [=] (WbWriter &w) { PutFn (w, func); w.u32 (x); w.b (x && !helper); }).i32 ();
}

// the reader's whole chunk, loaded in the helper with the same name; a precompiled one as the helper's Lua has it
int r_load (lua_State *L, lua_Reader reader, void *dt, const char *chunkname)
{
	std::string b;
	for (;;) {
		size_t k = 0;
		const char *p = reader (L, dt, &k);
		if (!p || !k) break;
		b.append (p, k);
	}
	WbLuaRechunk (b, 4);
	if (b.size () > WB_MSG_MAX - 4096) {
		r_pushstring (L, "chunk too large for the bridge");
		return LUA_ERRMEM;
	}
	return OpF (L, LO_load, [&b, chunkname] (WbWriter &w) {
		w.u32 ((uint32_t)b.size ());
		w.raw (b.data (), (uint32_t)b.size ());
		w.str (chunkname ? chunkname : "?");
	}).i32 ();
}

int r_dump (lua_State *L, lua_Writer writer, void *data)
{
	WbReader r = Op0 (L, LO_dump);
	int st = r.i32 ();
	uint32_t n;
	const char *s = r.str (&n);
	if (r.bad) return 1;
	if (st || !s || !n) return st;
	std::string c (s, n); // the writer may call Lua, whose calls reuse the buffer
	WbLuaRechunk (c, 8);
	return writer (L, c.data (), c.size (), data);
}

int r_yield (lua_State *, int nresults) // the closure in the helper yields when the native function returns
{
	if (t_top) t_top->yield = nresults;
	return -1;
}

int r_resume (lua_State *L, int narg) { return Op1 (L, LO_resume, narg).i32 (); }
int r_status (lua_State *L) { return Op0 (L, LO_status).i32 (); }
int r_gc (lua_State *L, int what, int data) { return Checked (Op2 (L, LO_gc, what, data)).i32 (); }

int r_error (lua_State *L)
{
	Op0 (L, LO_error);
	Raise ();
	return 0;
}

int r_next (lua_State *L, int idx) { return Checked (Op1 (L, LO_next, idx)).i32 (); }
void r_concat (lua_State *L, int n) { Checked (Op1 (L, LO_concat, n)); }

lua_Alloc r_getallocf (lua_State *, void **ud) // memory of Linux's: the helper's allocator is its own
{
	if (ud) *ud = nullptr;
	return HelperAlloc;
}

void r_setallocf (lua_State *, lua_Alloc, void *)
{
	static bool said;
	Once (said, "a native module set the allocator of a Windows module's Lua state: the helper's stays");
}

void r_close (lua_State *)
{
	static bool said;
	Once (said, "a native module closed a Windows module's Lua state: it stays open");
}

int r_getstack (lua_State *L, int level, lua_Debug *ar)
{
	WbReader r = Op1 (L, LO_getstack, level);
	int ok = r.i32 (), ci = r.i32 ();
	if (ok && !r.bad) ar->i_ci = ci;
	return r.bad ? 0 : ok;
}

// the fields of the options asked for
int r_getinfo (lua_State *L, const char *what, lua_Debug *ar)
{
	int ci = ar->i_ci;
	WbReader r = OpF (L, LO_getinfo, [what, ci] (WbWriter &w) { w.str (what); w.i32 (ci); });
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

const char *Name (WbReader r)
{
	uint32_t k;
	const char *s = r.str (&k);
	return KeepStr (s, k);
}

const char *r_getlocal (lua_State *L, const lua_Debug *ar, int n) { return Name (Op2 (L, LO_getlocal, ar->i_ci, n)); }
const char *r_setlocal (lua_State *L, const lua_Debug *ar, int n) { return Name (Op2 (L, LO_setlocal, ar->i_ci, n)); }
const char *r_getupvalue (lua_State *L, int funcindex, int n) { return Name (Op2 (L, LO_getupvalue, funcindex, n)); }
const char *r_setupvalue (lua_State *L, int funcindex, int n) { return Name (Op2 (L, LO_setupvalue, funcindex, n)); }

int r_sethook (lua_State *L, lua_Hook func, int mask, int count)
{
	return OpF (L, LO_sethook, [=] (WbWriter &w) { PutHook (w, func); w.i32 (mask); w.i32 (count); }).i32 ();
}

lua_Hook r_gethook (lua_State *L)
{
	WbReader r = Op0 (L, LO_gethook);
	int kind = r.i32 ();
	uint32_t v = r.u32 ();
	if (r.bad || kind == LF_NONE) return nullptr;
	if (kind == LF_LINUX) return (lua_Hook)Given (v);
	return StubFor (v, g_hookId, g_hook, "more of a Windows module's Lua hooks in native hands than the bridge has stubs for");
}

int r_gethookmask (lua_State *L) { return Op0 (L, LO_gethookmask).i32 (); }
int r_gethookcount (lua_State *L) { return Op0 (L, LO_gethookcount).i32 (); }

const lua_Remote g_remote = {
#define F(r, n, p, a) r_##n,
#define FV(n, p, a) r_##n,
	LUA_REMOTE_LIST (F, FV)
#undef F
#undef FV
	r_xmove,
};

// lua.so's (the core's, not upstream: dll-modules), registered while WineBridge.so is loaded
using SetRemote = int (*) (const lua_Remote *, size_t);
struct Registration {
	SetRemote set = nullptr;
	Registration ()
	{
		set = reinterpret_cast<SetRemote> (dlsym (RTLD_DEFAULT, "lua_setremote"));
		if (set && !set (&g_remote, sizeof g_remote)) set = nullptr;
	}
	~Registration () { if (set) set (nullptr, 0); }
} g_registration;

} // namespace

// ---------------------------------------------------------------------
// the helper's calls

// L_LUACALL: a native C function on a helper state; L_LUAHOOK: its hook
bool WbServeLuaRemote (uint32_t fn, WbReader &r, WbWriter &w)
{
	if (fn != L_LUACALL && fn != L_LUAHOOK) return false;
	uint32_t f = r.u32 (), sid = r.u32 ();
	lua_Debug ar {};
	if (fn == L_LUAHOOK) { ar.event = r.i32 (); ar.currentline = r.i32 (); ar.i_ci = r.i32 (); }
	if (r.bad) { WbBridge::Get ().BadArgs (fn); return true; }
	int end = LE_UNKNOWN, n = 0;
	if (void *p = sid ? Given (f) : nullptr) {
		CallCtx c {};
		c.kind = fn == L_LUAHOOK ? 1 : 0;
		c.f = (lua_CFunction)p;
		c.h = (lua_Hook)p;
		c.L = RemoteOf (sid);
		c.ar = &ar;
		end = Guarded (c, &n);
		if (fn == L_LUAHOOK) n = 0;
	}
	w.i32 (end);
	w.i32 (n);
	return true;
}

// a native vessel's clbkGeneric (VMSG_LUAINTERPRETER, VMSG_LUAINSTANCE) with a helper state; err: it raised a Lua error
int WbLuaHelperGeneric (VESSEL3 *v, int msg, int prm, uint32_t sid, bool *err)
{
	*err = false;
	if (!g_registration.set) {
		static bool said;
		Once (said, "this Orbiter's lua.so has no lua_setremote: native vessels have no Lua methods in Windows modules' scripts");
		return 0;
	}
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

// a served call: a Lua error in it doesn't unwind past it; the outermost one's end frees the copies
void *WbLuaRemoteBarrier ()
{
	void *t = t_top;
	t_top = nullptr;
	g_serving++;
	return t;
}

void WbLuaRemoteRestore (void *t)
{
	t_top = (Frame *)t;
	if (--g_serving == 0) Release ();
}

#else

// no remote states in this Orbiter's lua.so: a native vessel gets nothing for a helper state, as before part 8
bool WbServeLuaRemote (uint32_t fn, WbReader &, WbWriter &w)
{
	if (fn != L_LUACALL && fn != L_LUAHOOK) return false;
	w.i32 (LE_UNKNOWN);
	w.i32 (0);
	return true;
}

int WbLuaHelperGeneric (VESSEL3 *, int, int, uint32_t, bool *err)
{
	static bool said;
	if (!said) { said = true; oapiWriteLog ((char *)"WineBridge: this Orbiter's lua.so has no lua_setremote: native vessels have no Lua methods in Windows modules' scripts"); }
	*err = false;
	return 0;
}

void *WbLuaRemoteBarrier () { return nullptr; }
void WbLuaRemoteRestore (void *) {}

#endif
