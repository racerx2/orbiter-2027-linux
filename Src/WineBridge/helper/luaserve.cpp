// WineBridge helper: its Lua states in native modules' hands (a Windows module's interpreter gave a native vessel its state). A native
// module's call on one comes from Linux (H_LUA) and is lua.dll's function on the state; a native C function pushed on it is a closure of
// the helper's (Tramp) that calls it on Linux (L_LUACALL). A Lua error in those calls is caught here (lua_pcall) and raised again from
// the native function's closure, so it never unwinds the helper's frames.
#define OAPI_IMPLEMENTATION
#include "Orbitersdk.h"
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"
#include <lua/lua.h>
#define WB_HELPER
#include "../lua/wb_luaapi.h"

namespace {

// lua.dll's functions (Orbiter.exe can't import lua.dll, which imports it), looked up when the first state comes
struct Api {
#define WB_LUA(r, n, p, a) r (*n) p;
#define WB_LUAV(n, p, a) void (*n) p;
	WB_LUA_LIST
#undef WB_LUA
#undef WB_LUAV
	void (*xmove) (lua_State *from, lua_State *to, int n);
	int (*Lerror) (lua_State *L, const char *fmt, ...);
	bool ok;
} A;

bool Load ()
{
	if (A.ok) return true;
	HMODULE h = GetModuleHandleA ("lua.dll");
	if (!h) return false;
#define WB_LUA(r, n, p, a) if (!(*(FARPROC *)&A.n = GetProcAddress (h, "lua_" #n))) return false;
#define WB_LUAV(n, p, a) WB_LUA (void, n, p, a)
	WB_LUA_LIST
#undef WB_LUA
#undef WB_LUAV
	if (!(*(FARPROC *)&A.xmove = GetProcAddress (h, "lua_xmove"))) return false;
	if (!(*(FARPROC *)&A.Lerror = GetProcAddress (h, "luaL_error"))) return false;
	return A.ok = true;
}

// the API by its names, on lua.dll's functions
#define lua_newthread A.newthread
#define lua_atpanic A.atpanic
#define lua_gettop A.gettop
#define lua_settop A.settop
#define lua_pushvalue A.pushvalue
#define lua_remove A.remove
#define lua_insert A.insert
#define lua_replace A.replace
#define lua_checkstack A.checkstack
#define lua_isnumber A.isnumber
#define lua_isstring A.isstring
#define lua_iscfunction A.iscfunction
#define lua_isuserdata A.isuserdata
#define lua_type A.type
#define lua_equal A.equal
#define lua_rawequal A.rawequal
#define lua_lessthan A.lessthan
#define lua_tonumber A.tonumber
#define lua_toboolean A.toboolean
#define lua_tolstring A.tolstring
#define lua_objlen A.objlen
#define lua_tocfunction A.tocfunction
#define lua_touserdata A.touserdata
#define lua_tothread A.tothread
#define lua_topointer A.topointer
#define lua_pushnil A.pushnil
#define lua_pushnumber A.pushnumber
#define lua_pushinteger A.pushinteger
#define lua_pushlstring A.pushlstring
#define lua_pushstring A.pushstring
#define lua_pushvfstring A.pushvfstring
#define lua_pushcclosure A.pushcclosure
#define lua_pushboolean A.pushboolean
#define lua_pushlightuserdata A.pushlightuserdata
#define lua_pushthread A.pushthread
#define lua_gettable A.gettable
#define lua_getfield A.getfield
#define lua_rawget A.rawget
#define lua_rawgeti A.rawgeti
#define lua_createtable A.createtable
#define lua_newuserdata A.newuserdata
#define lua_getmetatable A.getmetatable
#define lua_getfenv A.getfenv
#define lua_settable A.settable
#define lua_setfield A.setfield
#define lua_rawset A.rawset
#define lua_rawseti A.rawseti
#define lua_setmetatable A.setmetatable
#define lua_setfenv A.setfenv
#define lua_call A.call
#define lua_pcall A.pcall
#define lua_cpcall A.cpcall
#define lua_load A.load
#define lua_dump A.dump
#define lua_yield A.yield
#define lua_resume A.resume
#define lua_status A.status
#define lua_gc A.gc
#define lua_error A.error
#define lua_next A.next
#define lua_concat A.concat
#define lua_getallocf A.getallocf
#define lua_setallocf A.setallocf
#define lua_close A.close
#define lua_getstack A.getstack
#define lua_getinfo A.getinfo
#define lua_getlocal A.getlocal
#define lua_setlocal A.setlocal
#define lua_getupvalue A.getupvalue
#define lua_setupvalue A.setupvalue
#define lua_sethook A.sethook
#define lua_gethook A.gethook
#define lua_gethookmask A.gethookmask
#define lua_gethookcount A.gethookcount
#define lua_xmove A.xmove
#define luaL_error A.Lerror

// a set of u32 values (open addressing; 0 is no value)
struct Set {
	uint32_t *v = 0, cap = 0, n = 0;
	uint32_t Slot (uint32_t x) const { return (x * 2654435761u) & (cap - 1); }
	bool Has (uint32_t x) const
	{
		if (!cap || !x) return false;
		for (uint32_t i = Slot (x);; i = (i + 1) & (cap - 1)) { if (v[i] == x) return true; if (!v[i]) return false; }
	}
	void Add (uint32_t x)
	{
		if (!x || Has (x)) return;
		if ((n + 1) * 2 > cap) {
			uint32_t *old = v, oc = cap;
			cap = cap ? cap * 2 : 64;
			v = (uint32_t *)calloc (cap, sizeof *v);
			n = 0;
			for (uint32_t i = 0; i < oc; i++) if (old[i]) Add (old[i]);
			free (old);
		}
		uint32_t i = Slot (x);
		while (v[i]) i = (i + 1) & (cap - 1);
		v[i] = x;
		n++;
	}
};

// the bridge's table in a Lua universe's registry, at &g_key
enum { WB_PROT = 1, WB_ERR, WB_UD, WB_SENT, WB_HOOKS, WB_PANIC, WB_N = WB_PANIC };
char g_key;

Set g_states; // the states Linux was given: their ids are good in H_LUA
Set g_linux;  // lightuserdata values Linux pushed (ids of the bridge's)

uint32_t *g_freeQ, g_nfree, g_capfree; // native blocks whose userdata Lua collected, for Linux to free

int Tramp (lua_State *L);
int ProtFn (lua_State *L);
int SentinelGc (lua_State *L);
void HookTramp (lua_State *L, lua_Debug *ar);
int PanicTramp (lua_State *L);

int Setup (lua_State *L)
{
	lua_createtable (L, WB_N, 0);
	lua_pushcfunction (L, ProtFn);
	lua_rawseti (L, -2, WB_PROT);
	lua_pushboolean (L, 0);
	lua_rawseti (L, -2, WB_ERR);
	const int weak[2] = {WB_UD, WB_HOOKS}; // weak keys: a native userdata, a thread
	for (int slot : weak) {
		lua_newtable (L);
		lua_createtable (L, 0, 1);
		lua_pushliteral (L, "k");
		lua_setfield (L, -2, "__mode");
		lua_setmetatable (L, -2);
		lua_rawseti (L, -2, slot);
	}
	lua_createtable (L, 0, 1);
	lua_pushcfunction (L, SentinelGc);
	lua_setfield (L, -2, "__gc");
	lua_rawseti (L, -2, WB_SENT);
	lua_pushboolean (L, 0);
	lua_rawseti (L, -2, WB_PANIC);
	lua_pushlightuserdata (L, &g_key);
	lua_insert (L, -2);
	lua_rawset (L, LUA_REGISTRYINDEX);
	return 0;
}

bool PushTable (lua_State *L)
{
	lua_pushlightuserdata (L, &g_key);
	lua_rawget (L, LUA_REGISTRYINDEX);
	if (lua_istable (L, -1)) return true;
	lua_pop (L, 1);
	if (lua_cpcall (L, Setup, 0)) { lua_pop (L, 1); return false; }
	lua_pushlightuserdata (L, &g_key);
	lua_rawget (L, LUA_REGISTRYINDEX);
	return true;
}

bool PushWb (lua_State *L, int slot)
{
	if (!PushTable (L)) return false;
	lua_rawgeti (L, -1, slot);
	lua_remove (L, -2);
	return true;
}

void SetPending (lua_State *L) // the error at the top: kept for the native function's closure to raise
{
	if (!PushTable (L)) { lua_pop (L, 1); return; }
	lua_insert (L, -2);
	lua_rawseti (L, -2, WB_ERR);
	lua_pop (L, 1);
}

int RaisePending (lua_State *L)
{
	lua_checkstack (L, 3);
	if (PushTable (L)) {
		lua_rawgeti (L, -1, WB_ERR);
		lua_pushboolean (L, 0);
		lua_rawseti (L, -3, WB_ERR);
		lua_remove (L, -2);
	} else lua_pushnil (L);
	return lua_error (L);
}

uint32_t StateId (lua_State *L)
{
	uint32_t id = wb::Id (L);
	g_states.Add (id);
	return id;
}

lua_State *StateOf (uint32_t id) { return g_states.Has (id) ? (lua_State *)wb::Ptr (id) : 0; }

int Ix (int i) { return i < LUA_GLOBALSINDEX ? i - 1 : i; } // a native upvalue i is the closure's i + 1 (the first is the function's id)
int Abs (lua_State *L, int i) { return i > 0 || i <= LUA_REGISTRYINDEX ? i : lua_gettop (L) + i + 1; }

void *LightIn (uint32_t v, bool lnx)
{
	if (lnx) g_linux.Add (v);
	return wb::Ptr (v);
}

bool Mapped (const void *p) // memory of this process: a module's own value, not a handle
{
	if ((uintptr_t)p < 0x10000) return false;
	MEMORY_BASIC_INFORMATION mi;
	return VirtualQuery (p, &mi, sizeof mi) && mi.State != MEM_FREE;
}

// lua_touserdata's: a native module's userdata is its block; a vessel's (the interpreter's VESSEL *) that vessel; one holding a handle,
// that id; any other a copy of its bytes
void PutUd (lua_State *L, int i, WbWriter &w)
{
	int t = lua_type (L, i);
	if (t == LUA_TLIGHTUSERDATA) {
		uint32_t v = wb::Id (lua_touserdata (L, i));
		w.i32 (LU_LIGHT); w.u32 (v); w.b (g_linux.Has (v));
		return;
	}
	if (t != LUA_TUSERDATA) { w.i32 (LU_NONE); return; }
	void *u = lua_touserdata (L, i);
	uint32_t block = 0;
	if (PushWb (L, WB_UD)) {
		lua_pushvalue (L, i);
		lua_rawget (L, -2);
		if (lua_type (L, -1) == LUA_TUSERDATA) block = *(uint32_t *)lua_touserdata (L, -1);
		lua_pop (L, 2);
	}
	if (block) { w.i32 (LU_BLOCK); w.u32 (block); return; }
	size_t n = lua_objlen (L, i);
	uint32_t ud = wb::Id (u);
	if (n == sizeof (void *)) {
		void *p = *(void **)u;
		if (uint32_t vid = p ? wb::VesselIdOf (p) : 0) { w.i32 (LU_VESSEL); w.u32 (vid); w.u32 (ud); return; }
		if (p && !Mapped (p)) { w.i32 (LU_ID); w.u32 (wb::Id (p)); w.u32 (ud); return; }
	}
	if (n > WB_MSG_MAX / 2) n = 0;
	w.i32 (LU_BYTES); w.u32 (ud); w.u32 ((uint32_t)n); w.raw (u, (uint32_t)n);
}

// a C function on the wire: the helper's (its address), a native one (a closure of Tramp: its id)
void PutFn (lua_CFunction f, uint32_t lnx, WbWriter &w)
{
	if (!f) { w.i32 (LF_NONE); w.u32 (0); }
	else if (lnx) { w.i32 (LF_LINUX); w.u32 (lnx); }
	else { w.i32 (LF_MODULE); w.u32 (wb::Id ((void *)f)); }
}

uint32_t LinuxFn (lua_State *L, int i) // the native function of a Tramp closure; 0 if not one
{
	if (lua_tocfunction (L, i) != Tramp || !lua_getupvalue (L, i, 1)) return 0;
	uint32_t f = wb::Id (lua_touserdata (L, -1));
	lua_pop (L, 1);
	return f;
}

uint32_t Slot (lua_State *L, int slot, bool thread) // a native function the table keeps (the thread's hook); 0: none
{
	uint32_t f = 0;
	if (!PushWb (L, slot)) return 0;
	if (thread) {
		lua_pushthread (L);
		lua_rawget (L, -2);
		f = wb::Id (lua_touserdata (L, -1));
		lua_pop (L, 1);
	} else f = wb::Id (lua_touserdata (L, -1));
	lua_pop (L, 1);
	return f;
}

void SetHook (lua_State *L, uint32_t f)
{
	if (!PushWb (L, WB_HOOKS)) return;
	lua_pushthread (L);
	if (f) lua_pushlightuserdata (L, wb::Ptr (f));
	else lua_pushnil (L);
	lua_rawset (L, -3);
	lua_pop (L, 1);
}

void SetPanic (lua_State *L, uint32_t f)
{
	if (!PushTable (L)) return;
	if (f) lua_pushlightuserdata (L, wb::Ptr (f));
	else lua_pushboolean (L, 0);
	lua_rawseti (L, -2, WB_PANIC);
	lua_pop (L, 1);
}

// a call of the native function (L_LUACALL) or hook (L_LUAHOOK)
void CallLinux (lua_State *L, uint32_t call, uint32_t fn, const lua_Debug *ar, int *end, int *n)
{
	WbWriter &w = wb::Begin (call);
	w.u32 (fn);
	w.u32 (StateId (L));
	if (ar) { w.i32 (ar->event); w.i32 (ar->currentline); w.i32 (ar->i_ci); }
	WbReader r = wb::Call ();
	int e = r.i32 (), k = r.i32 ();
	*end = r.bad ? LE_GONE : e;
	*n = r.bad ? 0 : k;
}

int Finish (lua_State *L, int end, int n)
{
	if (n < 0) n = 0;
	if (n > lua_gettop (L)) n = lua_gettop (L);
	switch (end) {
	case LE_OK: return n;
	case LE_YIELD: return lua_yield (L, n);
	case LE_ERROR: return RaisePending (L);
	case LE_UNKNOWN: return A.Lerror (L, "a native module's function that isn't there");
	default: return A.Lerror (L, "Orbiter didn't answer the call of a native module's function");
	}
}

int Tramp (lua_State *L)
{
	uint32_t fn = wb::Id (lua_touserdata (L, lua_upvalueindex (1)));
	int end, n;
	CallLinux (L, L_LUACALL, fn, 0, &end, &n);
	return Finish (L, end, n);
}

void HookTramp (lua_State *L, lua_Debug *ar)
{
	uint32_t fn = Slot (L, WB_HOOKS, true);
	if (!fn) return;
	int end, n;
	CallLinux (L, L_LUAHOOK, fn, ar, &end, &n);
	if (end == LE_ERROR) RaisePending (L);
}

int PanicTramp (lua_State *L)
{
	if (uint32_t fn = Slot (L, WB_PANIC, false)) {
		int end, n;
		CallLinux (L, L_LUACALL, fn, 0, &end, &n);
	}
	return 0;
}

struct CpReq { uint32_t fn, ud; bool lnx; };
int CpTramp (lua_State *L)
{
	CpReq q = *(CpReq *)lua_touserdata (L, 1);
	lua_pop (L, 1);
	lua_pushlightuserdata (L, LightIn (q.ud, q.lnx));
	int end, n;
	CallLinux (L, L_LUACALL, q.fn, 0, &end, &n);
	return Finish (L, end, n);
}

int SentinelGc (lua_State *L)
{
	uint32_t block = *(uint32_t *)lua_touserdata (L, 1);
	if (g_nfree == g_capfree) { g_capfree = g_capfree ? g_capfree * 2 : 64; g_freeQ = (uint32_t *)realloc (g_freeQ, g_capfree * sizeof *g_freeQ); }
	g_freeQ[g_nfree++] = block;
	wb::CountLuaFree ();
	return 0;
}

// an op that may raise: run in ProtFn by lua_pcall with copies of its operands as the arguments
struct Prot {
	int op, a, b, r, kind;
	size_t size;
	char *s;
	uint32_t slen, fn, block;
	lua_CFunction cf;
	lua_Hook hook;
	lua_Debug ar;
	lua_State *thread;
};

Prot *g_direct;

int DirectFn (lua_State *L)
{
	Prot *q = g_direct;
	g_direct = 0;
	q->r = q->cf (L);
	return lua_gettop (L);
}

// a helper function the native module called directly: on a copy of its frame, then the frame is the stack it left
int Direct (lua_State *L, Prot &q)
{
	int n = lua_gettop (L), f = 0, k = 0;
	lua_Debug ar;
	lua_checkstack (L, n + LUA_MINSTACK);
	if (lua_getstack (L, 1, &ar) && lua_getinfo (L, "f", &ar)) { // the native function's closure, whose upvalues the call sees
		if (lua_tocfunction (L, -1) == Tramp) f = lua_gettop (L);
		else lua_pop (L, 1);
	}
	if (f) while (lua_getupvalue (L, f, k + 2)) k++;
	lua_checkstack (L, n + 2);
	lua_pushcclosure (L, DirectFn, k);
	int d = lua_gettop (L);
	lua_pushvalue (L, d);
	for (int i = 1; i <= n; i++) lua_pushvalue (L, i);
	g_direct = &q;
	lua_call (L, n, LUA_MULTRET);
	int m = lua_gettop (L) - d;
	for (int i = 1; i <= k; i++) {
		lua_getupvalue (L, d, i);
		lua_setupvalue (L, f, i + 1);
	}
	return m;
}

int ProtFn (lua_State *L)
{
	Prot &q = *(Prot *)lua_touserdata (L, 1);
	lua_remove (L, 1);
	switch (q.op) {
	case LO_pushlstring: lua_pushlstring (L, q.s ? q.s : "", q.slen); return 1;
	case LO_pushcclosure: // the upvalues are the arguments
		if (q.kind == LF_MODULE) { lua_pushcclosure (L, (lua_CFunction)wb::Ptr (q.fn), q.a); return 1; }
		lua_pushlightuserdata (L, wb::Ptr (q.fn));
		lua_insert (L, 1);
		lua_pushcclosure (L, Tramp, q.a + 1);
		return 1;
	case LO_createtable: lua_createtable (L, q.a, q.b); return 1;
	case LO_newuserdata: { // the helper's of the same size; its sentinel, in the weak table under it, frees the native block when it goes
		lua_newuserdata (L, q.size);
		PushWb (L, WB_UD);
		lua_pushvalue (L, -2);
		*(uint32_t *)lua_newuserdata (L, sizeof (uint32_t)) = q.block;
		PushWb (L, WB_SENT);
		lua_setmetatable (L, -2);
		lua_rawset (L, -3);
		lua_pop (L, 1);
		return 1; }
	case LO_newthread: q.thread = lua_newthread (L); return 1;
	case LO_tolstring: lua_tolstring (L, 1, 0); return 1;
	case LO_gettable: lua_gettable (L, 1); return 1;
	case LO_getfield: lua_getfield (L, 1, q.s); return 1;
	case LO_settable: lua_settable (L, 1); return 0;
	case LO_setfield: lua_setfield (L, 1, q.s); return 0;
	case LO_rawset: lua_rawset (L, 1); return 0;
	case LO_equal: q.r = lua_equal (L, 1, 2); return 0;
	case LO_lessthan: q.r = lua_lessthan (L, 1, 2); return 0;
	case LO_next: q.r = lua_next (L, 1); return q.r ? 2 : 0;
	case LO_concat: lua_concat (L, q.a); return 1;
	case LO_gc: q.r = lua_gc (L, q.a, q.b); return 0;
	case LO_callc: return Direct (L, q);
	case LO_callhook: q.hook (L, &q.ar); return 0;
	default: return 0;
	}
}

// runs q with copies of the operands (absolute indices); its nres results land at the top. False: Lua raised (the error is pending)
bool Run (lua_State *L, Prot &q, const int *idx, int nidx, int nres)
{
	lua_checkstack (L, nidx + 3);
	if (!PushWb (L, WB_PROT)) { lua_pushliteral (L, "not enough memory"); SetPending (L); return false; }
	lua_pushlightuserdata (L, &q);
	for (int i = 0; i < nidx; i++) lua_pushvalue (L, idx[i]);
	if (lua_pcall (L, nidx + 1, nres, 0)) { SetPending (L); return false; }
	return true;
}
bool Run0 (lua_State *L, Prot &q, int nres) { return Run (L, q, 0, 0, nres); }
bool Run1 (lua_State *L, Prot &q, int a, int nres) { return Run (L, q, &a, 1, nres); }
bool Run2 (lua_State *L, Prot &q, int a, int b, int nres) { int x[2] = {a, b}; return Run (L, q, x, 2, nres); }
bool Run3 (lua_State *L, Prot &q, int a, int b, int c, int nres) { int x[3] = {a, b, c}; return Run (L, q, x, 3, nres); }

bool RunTop (lua_State *L, Prot &q, int n, int nres) // the operands are the top n
{
	int *idx = (int *)malloc ((n ? n : 1) * sizeof (int));
	for (int i = 0; i < n; i++) idx[i] = lua_gettop (L) - n + 1 + i;
	bool ok = Run (L, q, idx, n, nres);
	free (idx);
	return ok;
}

void Collapse (lua_State *L, int n) // [v1 .. vn r] -> [r]
{
	lua_insert (L, -(n + 1));
	lua_pop (L, n);
}

char *Copy (WbReader &r, uint32_t *n) // a string argument, kept while Lua runs
{
	const char *s = r.str (n);
	if (!s) return 0;
	char *c = (char *)malloc (*n + 1);
	memcpy (c, s, *n);
	c[*n] = 0;
	return c;
}

struct Reader { const char *s; uint32_t n; bool done; };
const char *ReadChunk (lua_State *, void *d, size_t *n)
{
	Reader *r = (Reader *)d;
	if (r->done) { *n = 0; return 0; }
	r->done = true;
	*n = r->n;
	return r->s;
}

struct Out { char *p; size_t n, cap; };
int WriteChunk (lua_State *, const void *p, size_t n, void *d)
{
	Out *o = (Out *)d;
	if (o->n + n > o->cap) { o->cap = (o->n + n) * 2; o->p = (char *)realloc (o->p, o->cap); }
	memcpy (o->p + o->n, p, n);
	o->n += n;
	return 0;
}

void PutStr (WbWriter &w, const char *s, size_t n)
{
	if (!s) { w.str (0); return; }
	w.u32 ((uint32_t)n);
	w.raw (s, (uint32_t)n);
}

// one call; its arguments are read before Lua runs (Lua may call Linux, whose calls reuse the buffer), its results written after
void ServeOp (lua_State *L, int op, WbReader &r, WbWriter &w)
{
	Prot q;
	memset (&q, 0, sizeof q);
	q.op = op;
	lua_checkstack (L, 8);
	switch (op) {
	case LO_gettop: w.i32 (lua_gettop (L)); break;
	case LO_settop: { int i = r.i32 (); if (!r.bad) lua_settop (L, i); break; }
	case LO_pushvalue: case LO_remove: case LO_insert: case LO_replace: {
		int i = Ix (r.i32 ());
		if (r.bad) break;
		if (op == LO_pushvalue) lua_pushvalue (L, i);
		else if (op == LO_remove) lua_remove (L, i);
		else if (op == LO_insert) lua_insert (L, i);
		else lua_replace (L, i);
		break; }
	case LO_checkstack: { int n = r.i32 (); if (!r.bad) w.i32 (lua_checkstack (L, n)); break; }
	case LO_xmove: {
		uint32_t to = r.u32 ();
		int n = r.i32 ();
		lua_State *T = StateOf (to);
		if (!r.bad && T) A.xmove (L, T, n);
		break; }
	case LO_isnumber: case LO_isstring: case LO_iscfunction: case LO_isuserdata: case LO_type: case LO_toboolean: case LO_getmetatable:
	case LO_setmetatable: case LO_setfenv: {
		int i = Ix (r.i32 ());
		if (r.bad) break;
		int v = op == LO_isnumber ? lua_isnumber (L, i) : op == LO_isstring ? lua_isstring (L, i) : op == LO_iscfunction ? lua_iscfunction (L, i)
			: op == LO_isuserdata ? lua_isuserdata (L, i) : op == LO_type ? lua_type (L, i) : op == LO_toboolean ? lua_toboolean (L, i)
			: op == LO_getmetatable ? lua_getmetatable (L, i) : op == LO_setmetatable ? lua_setmetatable (L, i) : lua_setfenv (L, i);
		w.i32 (v);
		break; }
	case LO_equal: case LO_lessthan: {
		int i1 = Abs (L, Ix (r.i32 ())), i2 = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		bool ok = true;
		if (lua_type (L, i1) != LUA_TNONE && lua_type (L, i2) != LUA_TNONE) ok = Run2 (L, q, i1, i2, 0);
		w.b (!ok);
		w.i32 (ok ? q.r : 0);
		break; }
	case LO_rawequal: { int i1 = Ix (r.i32 ()), i2 = Ix (r.i32 ()); if (!r.bad) w.i32 (lua_rawequal (L, i1, i2)); break; }
	case LO_tonumber: { int i = Ix (r.i32 ()); if (!r.bad) w.f64 (lua_tonumber (L, i)); break; }
	case LO_tolstring: {
		int i = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		int t = lua_type (L, i);
		bool ok = true;
		if (t == LUA_TNUMBER && (ok = Run1 (L, q, i, 1))) lua_replace (L, i);
		size_t n = 0;
		const char *s = ok && (t == LUA_TNUMBER || t == LUA_TSTRING) ? lua_tolstring (L, i, &n) : 0;
		w.b (!ok);
		PutStr (w, s, n);
		break; }
	case LO_objlen: { int i = Ix (r.i32 ()); if (!r.bad) w.u32 ((uint32_t)lua_objlen (L, i)); break; }
	case LO_tocfunction: {
		int i = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		lua_CFunction f = lua_tocfunction (L, i);
		PutFn (f, f ? LinuxFn (L, i) : 0, w);
		break; }
	case LO_touserdata: { int i = Abs (L, Ix (r.i32 ())); if (!r.bad) PutUd (L, i, w); break; }
	case LO_tothread: {
		int i = Ix (r.i32 ());
		if (r.bad) break;
		lua_State *T = lua_tothread (L, i);
		w.u32 (T ? StateId (T) : 0);
		break; }
	case LO_topointer: {
		int i = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		int t = lua_type (L, i);
		w.i32 (t);
		if (t == LUA_TUSERDATA || t == LUA_TLIGHTUSERDATA) PutUd (L, i, w);
		else w.u32 (wb::Id (lua_topointer (L, i)));
		break; }
	case LO_pushnil: lua_pushnil (L); break;
	case LO_pushnumber: { double v = r.f64 (); if (!r.bad) lua_pushnumber (L, v); break; }
	case LO_pushinteger: { int v = r.i32 (); if (!r.bad) lua_pushinteger (L, v); break; }
	case LO_pushboolean: { int v = r.i32 (); if (!r.bad) lua_pushboolean (L, v); break; }
	case LO_pushlightuserdata: { uint32_t v = r.u32 (); bool lnx = r.b (); if (!r.bad) lua_pushlightuserdata (L, LightIn (v, lnx)); break; }
	case LO_pushthread: w.i32 (lua_pushthread (L)); break;
	case LO_pushlstring: {
		q.s = Copy (r, &q.slen);
		if (r.bad || !q.s) break;
		w.b (!Run0 (L, q, 1));
		break; }
	case LO_pushcclosure: {
		q.kind = r.i32 ();
		q.fn = r.u32 ();
		q.a = r.i32 ();
		if (r.bad || q.a < 0 || q.a > lua_gettop (L)) break;
		bool ok = RunTop (L, q, q.a, 1);
		if (ok) Collapse (L, q.a);
		w.b (!ok);
		break; }
	case LO_gettable: case LO_getfield: {
		int t = Abs (L, Ix (r.i32 ()));
		if (op == LO_getfield) q.s = Copy (r, &q.slen);
		if (r.bad || (op == LO_getfield && !q.s)) break;
		bool ok = op == LO_gettable ? Run2 (L, q, t, lua_gettop (L), 1) : Run1 (L, q, t, 1);
		if (ok && op == LO_gettable) lua_remove (L, -2);
		w.b (!ok);
		break; }
	case LO_rawget: case LO_getfenv: { int i = Ix (r.i32 ()); if (r.bad) break; if (op == LO_rawget) lua_rawget (L, i); else lua_getfenv (L, i); break; }
	case LO_rawgeti: case LO_rawseti: {
		int i = Ix (r.i32 ()), n = r.i32 ();
		if (r.bad) break;
		if (op == LO_rawgeti) lua_rawgeti (L, i, n);
		else lua_rawseti (L, i, n);
		break; }
	case LO_createtable: {
		q.a = r.i32 ();
		q.b = r.i32 ();
		if (!r.bad) w.b (!Run0 (L, q, 1));
		break; }
	case LO_newuserdata: {
		q.size = r.u32 ();
		q.block = r.u32 ();
		if (!r.bad) w.b (!Run0 (L, q, 1));
		break; }
	case LO_settable: case LO_setfield: case LO_rawset: {
		int t = Abs (L, Ix (r.i32 ()));
		if (op == LO_setfield) q.s = Copy (r, &q.slen);
		if (r.bad || (op == LO_setfield && !q.s)) break;
		int top = lua_gettop (L);
		bool ok = op == LO_setfield ? Run2 (L, q, t, top, 0) : Run3 (L, q, t, top - 1, top, 0);
		if (ok) lua_pop (L, op == LO_setfield ? 1 : 2);
		w.b (!ok);
		break; }
	case LO_call: {
		int na = r.i32 (), nr = r.i32 ();
		if (r.bad) break;
		bool ok = lua_pcall (L, na, nr, 0) == 0;
		if (!ok) SetPending (L);
		w.b (!ok);
		break; }
	case LO_pcall: {
		int na = r.i32 (), nr = r.i32 (), ef = r.i32 ();
		if (!r.bad) w.i32 (lua_pcall (L, na, nr, ef ? Ix (ef) : 0));
		break; }
	case LO_cpcall: {
		int kind = r.i32 ();
		uint32_t f = r.u32 (), ud = r.u32 ();
		bool lnx = r.b ();
		if (r.bad) break;
		CpReq c = {f, ud, lnx};
		w.i32 (kind == LF_MODULE ? lua_cpcall (L, (lua_CFunction)wb::Ptr (f), LightIn (ud, lnx)) : lua_cpcall (L, CpTramp, &c));
		break; }
	case LO_load: {
		uint32_t n;
		char *chunk = Copy (r, &n);
		uint32_t nn;
		char *name = Copy (r, &nn);
		if (!r.bad && chunk) {
			Reader rd = {chunk, n, false};
			w.i32 (lua_load (L, ReadChunk, &rd, name ? name : "?"));
		}
		free (chunk);
		free (name);
		break; }
	case LO_dump: {
		Out o = {0, 0, 0};
		int st = lua_dump (L, WriteChunk, &o);
		w.i32 (st);
		PutStr (w, o.p ? o.p : "", o.n);
		free (o.p);
		break; }
	case LO_resume: { int n = r.i32 (); if (!r.bad) w.i32 (lua_resume (L, n)); break; }
	case LO_status: w.i32 (lua_status (L)); break;
	case LO_gc: {
		q.a = r.i32 ();
		q.b = r.i32 ();
		if (r.bad) break;
		bool ok = Run0 (L, q, 0);
		w.b (!ok);
		w.i32 (q.r);
		break; }
	case LO_error: SetPending (L); break;
	case LO_next: {
		int t = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		bool ok = Run2 (L, q, t, lua_gettop (L), LUA_MULTRET);
		if (ok && q.r) lua_remove (L, -3);
		else if (ok) lua_pop (L, 1);
		w.b (!ok);
		w.i32 (ok ? q.r : 0);
		break; }
	case LO_concat: {
		q.a = r.i32 ();
		if (r.bad || q.a < 0 || q.a > lua_gettop (L)) break;
		bool ok = RunTop (L, q, q.a, 1);
		if (ok) Collapse (L, q.a);
		w.b (!ok);
		break; }
	case LO_newthread: {
		bool ok = Run0 (L, q, 1);
		w.b (!ok);
		w.u32 (ok ? StateId (q.thread) : 0);
		break; }
	case LO_getstack: {
		int level = r.i32 ();
		if (r.bad) break;
		lua_Debug ar;
		memset (&ar, 0, sizeof ar);
		int ok = lua_getstack (L, level, &ar);
		w.i32 (ok);
		w.i32 (ar.i_ci);
		break; }
	case LO_getinfo: { // the native function has one upvalue less than its closure
		uint32_t wn;
		char *what = Copy (r, &wn);
		int ci = r.i32 ();
		if (r.bad || !what || !*what) { free (what); break; }
		lua_Debug ar;
		memset (&ar, 0, sizeof ar);
		ar.i_ci = ci;
		bool top = what[0] == '>', u = strchr (what, 'u') != 0, f = strchr (what, 'f') != 0;
		bool tramp = top && u && lua_tocfunction (L, -1) == Tramp;
		char *w2 = (char *)malloc (wn + 2);
		strcpy (w2, what);
		if (u && !top && !f) strcat (w2, "f");
		int ok = lua_getinfo (L, w2, &ar);
		if (ok && u && !top) {
			int at = strchr (what, 'L') ? -2 : -1; // 'L' pushes its table after the function
			tramp = lua_tocfunction (L, at) == Tramp;
			if (!f) lua_remove (L, at);
		}
		if (tramp && ar.nups > 0) ar.nups--;
		w.i32 (ok);
		w.str (ar.name); w.str (ar.namewhat); w.str (ar.what); w.str (ar.source);
		w.i32 (ar.currentline); w.i32 (ar.nups); w.i32 (ar.linedefined); w.i32 (ar.lastlinedefined);
		w.str (ok && strchr (what, 'S') ? ar.short_src : 0);
		free (w2);
		free (what);
		break; }
	case LO_getlocal: case LO_setlocal: {
		lua_Debug ar;
		memset (&ar, 0, sizeof ar);
		ar.i_ci = r.i32 ();
		int n = r.i32 ();
		if (r.bad) break;
		w.str (op == LO_getlocal ? lua_getlocal (L, &ar, n) : lua_setlocal (L, &ar, n));
		break; }
	case LO_getupvalue: case LO_setupvalue: {
		int f = Abs (L, Ix (r.i32 ())), n = r.i32 ();
		if (r.bad) break;
		if (n >= 1 && lua_tocfunction (L, f) == Tramp) n++;
		w.str (n < 1 ? 0 : op == LO_getupvalue ? lua_getupvalue (L, f, n) : lua_setupvalue (L, f, n));
		break; }
	case LO_sethook: {
		int kind = r.i32 ();
		uint32_t f = r.u32 ();
		int mask = r.i32 (), count = r.i32 ();
		if (r.bad) break;
		SetHook (L, kind == LF_LINUX ? f : 0);
		lua_Hook h = !f || !mask ? 0 : kind == LF_MODULE ? (lua_Hook)wb::Ptr (f) : HookTramp;
		w.i32 (lua_sethook (L, h, mask, count));
		break; }
	case LO_gethook: {
		lua_Hook h = lua_gethook (L);
		if (!h) { w.i32 (LF_NONE); w.u32 (0); }
		else if (h == HookTramp) { w.i32 (LF_LINUX); w.u32 (Slot (L, WB_HOOKS, true)); }
		else { w.i32 (LF_MODULE); w.u32 (wb::Id ((void *)h)); }
		break; }
	case LO_gethookmask: w.i32 (lua_gethookmask (L)); break;
	case LO_gethookcount: w.i32 (lua_gethookcount (L)); break;
	case LO_atpanic: {
		int kind = r.i32 ();
		uint32_t f = r.u32 ();
		if (r.bad) break;
		uint32_t before = Slot (L, WB_PANIC, false);
		if (kind == LF_LINUX) SetPanic (L, f);
		lua_CFunction old = lua_atpanic (L, kind == LF_LINUX && f ? PanicTramp : kind == LF_MODULE ? (lua_CFunction)wb::Ptr (f) : 0);
		PutFn (old, old == PanicTramp ? before : 0, w);
		break; }
	case LO_callc: {
		q.cf = (lua_CFunction)wb::Ptr (r.u32 ());
		if (r.bad || !q.cf) break;
		int n = lua_gettop (L);
		bool ok = RunTop (L, q, n, LUA_MULTRET);
		if (ok) for (int i = 0; i < n; i++) lua_remove (L, 1);
		w.b (!ok);
		w.i32 (ok ? q.r : 0);
		break; }
	case LO_callhook: {
		q.hook = (lua_Hook)wb::Ptr (r.u32 ());
		q.ar.event = r.i32 ();
		q.ar.currentline = r.i32 ();
		q.ar.i_ci = r.i32 ();
		if (r.bad || !q.hook) break;
		w.b (!Run0 (L, q, 0));
		break; }
	default:
		r.bad = true;
		break;
	}
	free (q.s);
}

} // namespace

// H_LUA: a native module's call on a helper state
void wb::ServeLuaH (WbReader &r, WbWriter &w)
{
	uint32_t sid = r.u32 ();
	int op = r.i32 ();
	lua_State *L = r.bad || !Load () ? 0 : StateOf (sid);
	if (L) ServeOp (L, op, r, w);
	if (!L || r.bad) {
		static bool said;
		if (!said) { said = true; wb::Log ("a native module's Lua call on a state the helper doesn't know, or bad arguments (op %d)", op); }
	}
}

// a helper state a Linux vessel gets (VMSG_LUAINTERPRETER, VMSG_LUAINSTANCE): its id; 0 if lua.dll isn't loaded
uint32_t wb::LuaServeState (void *L) { return L && Load () ? StateId ((lua_State *)L) : 0; }

void wb::LuaServeRaise (void *L) { RaisePending ((lua_State *)L); }

// H_LUAFREE's reply: the native blocks the helper's Lua collected
void wb::LuaServeFreed (WbWriter &w)
{
	w.u32 (g_nfree);
	for (uint32_t i = 0; i < g_nfree; i++) w.u32 (g_freeQ[i]);
	g_nfree = 0;
}
