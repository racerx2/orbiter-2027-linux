// WineBridge: Linux's Lua states for the helper's modules. lua.dll's call on a remote state comes here (L_LUA) and is Lua's own function
// on the state; a module's C function pushed on it is a closure of the bridge's (Tramp) that calls it in the helper (H_LUACALL). A Lua
// error in those calls is caught here (lua_pcall) and raised again from the module function's closure, so it never unwinds the
// bridge's frames.
#include "Orbitersdk.h"
#include "Bridge.h"
#include "Manual.h"
#include "lua.hpp"
#include <initializer_list>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

using Guard = std::lock_guard<std::recursive_mutex>;

namespace {

// the bridge's table in a Lua universe's registry, at &g_key
enum { WB_PROT = 1, WB_ERR, WB_UD, WB_SENT, WB_HOOKS, WB_PANIC, WB_N = WB_PANIC };
char g_key;

std::unordered_set<lua_State *> g_states; // the states the helper was given: their ids are good in L_LUA
std::unordered_set<uintptr_t> g_modlud;   // lightuserdata values a module pushed as they are

struct Sent { uint32_t block; uint64_t session; }; // a module's userdata's sentinel: its block in the helper
std::mutex g_freeLock;
std::vector<Sent> g_free; // ... whose userdata Lua collected, for the helper to free

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
	for (int slot : {WB_UD, WB_HOOKS}) { // weak keys: a module's userdata, a thread
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

// the bridge's table on the stack (made at the first use); false: no memory for it
bool PushTable (lua_State *L)
{
	lua_pushlightuserdata (L, &g_key);
	lua_rawget (L, LUA_REGISTRYINDEX);
	if (lua_istable (L, -1)) return true;
	lua_pop (L, 1);
	if (lua_cpcall (L, Setup, nullptr)) { lua_pop (L, 1); return false; }
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

// the error at the top: kept for the module function's closure to raise (rawseti into the table's array: no allocation)
void SetPending (lua_State *L)
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
	g_states.insert (L);
	return WbBridge::Get ().Id (L);
}

lua_State *StateOf (uint32_t id)
{
	lua_State *L = (lua_State *)WbBridge::Get ().Ptr (id);
	return L && g_states.count (L) ? L : nullptr;
}

// a module's upvalue i is the closure's i + 1 (the first is the module's function)
int Ix (int i) { return i < LUA_GLOBALSINDEX ? i - 1 : i; }
int Abs (lua_State *L, int i) { return i > 0 || i <= LUA_REGISTRYINDEX ? i : lua_gettop (L) + i + 1; }

// lightuserdata: a value the helper doesn't have mapped is an id of the bridge's (a handle), any other the module's own, kept as given
void *LightIn (uint32_t v, bool mapped)
{
	WbBridge &b = WbBridge::Get ();
	if (!mapped && v && v < b.Ids ()) return b.Ptr (v);
	if (v) g_modlud.insert (v);
	return (void *)(uintptr_t)v;
}

uint32_t LightOut (const void *p)
{
	uintptr_t v = (uintptr_t)p;
	if (!v) return 0;
	if (v <= 0xffffffffu && g_modlud.count (v)) return (uint32_t)v;
	return WbBridge::Get ().Id (p);
}

uint32_t VesselId (const void *v)
{
	if (!v) return 0;
	for (DWORD i = 0, n = oapiGetVesselCount (); i < n; i++) {
		OBJHANDLE h = oapiGetVesselByIndex (i);
		if (oapiGetVesselInterface (h) == v) return WbBridge::Get ().Id (h);
	}
	return 0;
}

// lua_touserdata's: the module's userdata is its block; a vessel's (the interpreter's VESSEL *) that vessel; one holding a pointer the
// bridge has an id for, that id; any other a copy of its bytes
void PutUd (lua_State *L, int i, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	int t = lua_type (L, i);
	if (t == LUA_TLIGHTUSERDATA) { w.i32 (LU_LIGHT); w.u32 (LightOut (lua_touserdata (L, i))); return; }
	if (t != LUA_TUSERDATA) { w.i32 (LU_NONE); return; }
	void *u = lua_touserdata (L, i);
	uint32_t block = 0;
	if (PushWb (L, WB_UD)) {
		lua_pushvalue (L, i);
		lua_rawget (L, -2);
		if (lua_type (L, -1) == LUA_TUSERDATA) block = ((Sent *)lua_touserdata (L, -1))->block;
		lua_pop (L, 2);
	}
	if (block) { w.i32 (LU_BLOCK); w.u32 (block); return; }
	size_t n = lua_objlen (L, i);
	uint32_t ud = b.Id (u);
	if (n == sizeof (void *)) {
		void *p = *(void **)u;
		if (uint32_t vid = VesselId (p)) { w.i32 (LU_VESSEL); w.u32 (vid); w.u32 (ud); return; }
		if (uint32_t id = b.IdIf (p)) { w.i32 (LU_ID); w.u32 (id); w.u32 (ud); return; }
	}
	if (n > WB_MSG_MAX / 2) n = 0;
	w.i32 (LU_BYTES); w.u32 (ud); w.u32 ((uint32_t)n); w.raw (u, (uint32_t)n);
}

// a C function on the wire: the module's (a closure of Tramp), Linux's (an id)
void PutFn (lua_CFunction f, uint32_t module, WbWriter &w)
{
	if (!f) { w.i32 (LF_NONE); w.u32 (0); }
	else if (module) { w.i32 (LF_MODULE); w.u32 (module); }
	else { w.i32 (LF_LINUX); w.u32 (WbBridge::Get ().Id (reinterpret_cast<void *> (f))); }
}

uint32_t ModuleFn (lua_State *L, int i) // the module's function of a Tramp closure; 0 if not one
{
	if (lua_tocfunction (L, i) != Tramp || !lua_getupvalue (L, i, 1)) return 0;
	uint32_t f = (uint32_t)(uintptr_t)lua_touserdata (L, -1);
	lua_pop (L, 1);
	return f;
}

uint32_t Slot (lua_State *L, int slot, bool thread) // a module function the table keeps (the thread's hook); 0: none
{
	uint32_t f = 0;
	if (!PushWb (L, slot)) return 0;
	if (thread) {
		lua_pushthread (L);
		lua_rawget (L, -2);
		f = (uint32_t)(uintptr_t)lua_touserdata (L, -1);
		lua_pop (L, 1);
	} else f = (uint32_t)(uintptr_t)lua_touserdata (L, -1);
	lua_pop (L, 1);
	return f;
}

void SetHook (lua_State *L, uint32_t f)
{
	if (!PushWb (L, WB_HOOKS)) return;
	lua_pushthread (L);
	if (f) lua_pushlightuserdata (L, (void *)(uintptr_t)f);
	else lua_pushnil (L);
	lua_rawset (L, -3);
	lua_pop (L, 1);
}

void SetPanic (lua_State *L, uint32_t f)
{
	if (!PushTable (L)) return;
	if (f) lua_pushlightuserdata (L, (void *)(uintptr_t)f);
	else lua_pushboolean (L, 0);
	lua_rawseti (L, -2, WB_PANIC);
	lua_pop (L, 1);
}

// a call of the module's function (H_LUACALL) or hook (H_LUAHOOK); the bridge's objects end in here
void CallModule (lua_State *L, uint32_t call, uint32_t fn, const lua_Debug *ar, int *end, int *n)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	*end = LE_GONE;
	*n = 0;
	if (!b.Alive ()) return;
	WbLuaFlush ();
	WbWriter &w = b.Begin (call);
	w.u32 (fn);
	w.u32 (StateId (L));
	if (ar) { w.i32 (ar->event); w.i32 (ar->currentline); w.i32 (ar->i_ci); }
	WbReader r = b.Call ();
	int e = r.i32 (), k = r.i32 ();
	if (!r.bad) { *end = e; *n = k; }
}

int Finish (lua_State *L, int end, int n)
{
	if (n < 0) n = 0;
	if (n > lua_gettop (L)) n = lua_gettop (L);
	switch (end) {
	case LE_OK: return n;
	case LE_YIELD: return lua_yield (L, n);
	case LE_ERROR: return RaisePending (L);
	case LE_UNKNOWN: return luaL_error (L, "a Windows module's function that isn't loaded");
	default: return luaL_error (L, "the Windows modules' helper isn't running");
	}
}

int Tramp (lua_State *L)
{
	uint32_t fn = (uint32_t)(uintptr_t)lua_touserdata (L, lua_upvalueindex (1));
	int end, n;
	CallModule (L, H_LUACALL, fn, nullptr, &end, &n);
	return Finish (L, end, n);
}

void HookTramp (lua_State *L, lua_Debug *ar)
{
	uint32_t fn = Slot (L, WB_HOOKS, true);
	if (!fn) return;
	int end, n;
	CallModule (L, H_LUAHOOK, fn, ar, &end, &n);
	if (end == LE_ERROR) RaisePending (L);
}

int PanicTramp (lua_State *L)
{
	if (uint32_t fn = Slot (L, WB_PANIC, false)) {
		int end, n;
		CallModule (L, H_LUACALL, fn, nullptr, &end, &n);
	}
	return 0;
}

struct CpReq { uint32_t fn, ud; bool mapped; };
int CpTramp (lua_State *L)
{
	CpReq q = *(CpReq *)lua_touserdata (L, 1);
	lua_pop (L, 1);
	lua_pushlightuserdata (L, LightIn (q.ud, q.mapped));
	int end, n;
	CallModule (L, H_LUACALL, q.fn, nullptr, &end, &n);
	return Finish (L, end, n);
}

int SentinelGc (lua_State *L)
{
	Sent s = *(Sent *)lua_touserdata (L, 1);
	std::lock_guard<std::mutex> g (g_freeLock);
	g_free.push_back (s);
	return 0;
}

// an op that may raise (a metamethod, a finalizer the collector runs, an error of its own): run in ProtFn by lua_pcall with copies of
// its operands as the arguments
const int OP_GENERIC = 1000; // a native vessel's clbkGeneric
struct Prot {
	int op = 0;
	int a = 0, b = 0, r = 0;
	size_t size = 0;
	std::string s;
	int kind = 0;
	uint32_t fn = 0, block = 0;
	lua_CFunction cf = nullptr;
	lua_Hook hook = nullptr;
	lua_Debug ar {};
	lua_State *thread = nullptr;
	VESSEL3 *vessel = nullptr;
};

thread_local Prot *t_direct;

int DirectFn (lua_State *L)
{
	Prot *q = t_direct;
	t_direct = nullptr;
	q->r = q->cf (L); // as the module called it: in its frame, with its upvalues
	return lua_gettop (L);
}

// a Linux C function the module called directly: on a copy of the module's frame, then the frame is the stack it left
int Direct (lua_State *L, Prot &q)
{
	int n = lua_gettop (L), f = 0, k = 0;
	lua_Debug ar;
	lua_checkstack (L, n + LUA_MINSTACK);
	if (lua_getstack (L, 1, &ar) && lua_getinfo (L, "f", &ar)) { // the module function's closure, whose upvalues the call sees
		if (lua_tocfunction (L, -1) == Tramp) f = lua_gettop (L);
		else lua_pop (L, 1);
	}
	if (f) while (lua_getupvalue (L, f, k + 2)) k++;
	lua_checkstack (L, n + 2);
	lua_pushcclosure (L, DirectFn, k);
	int d = lua_gettop (L);
	lua_pushvalue (L, d);
	for (int i = 1; i <= n; i++) lua_pushvalue (L, i);
	t_direct = &q;
	lua_call (L, n, LUA_MULTRET);
	int m = lua_gettop (L) - d;
	for (int i = 1; i <= k; i++) { // what the call wrote to them
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
	case LO_pushlstring: lua_pushlstring (L, q.s.data (), q.s.size ()); return 1;
	case LO_pushcclosure: // the upvalues are the arguments
		if (q.kind == LF_LINUX) { lua_pushcclosure (L, (lua_CFunction)WbBridge::Get ().Ptr (q.fn), q.a); return 1; }
		lua_pushlightuserdata (L, (void *)(uintptr_t)q.fn);
		lua_insert (L, 1);
		lua_pushcclosure (L, Tramp, q.a + 1);
		return 1;
	case LO_createtable: lua_createtable (L, q.a, q.b); return 1;
	case LO_newuserdata: { // Lua's of the same size; its sentinel, in the weak table under it, frees the module's block when it goes
		lua_newuserdata (L, q.size);
		PushWb (L, WB_UD);
		lua_pushvalue (L, -2);
		Sent *s = (Sent *)lua_newuserdata (L, sizeof (Sent));
		s->block = q.block;
		s->session = WbBridge::Get ().Session ();
		PushWb (L, WB_SENT);
		lua_setmetatable (L, -2);
		lua_rawset (L, -3);
		lua_pop (L, 1);
		return 1; }
	case LO_newthread: q.thread = lua_newthread (L); return 1;
	case LO_tolstring: lua_tolstring (L, 1, nullptr); return 1;
	case LO_gettable: lua_gettable (L, 1); return 1;
	case LO_getfield: lua_getfield (L, 1, q.s.c_str ()); return 1;
	case LO_settable: lua_settable (L, 1); return 0;
	case LO_setfield: lua_setfield (L, 1, q.s.c_str ()); return 0;
	case LO_rawset: lua_rawset (L, 1); return 0;
	case LO_equal: q.r = lua_equal (L, 1, 2); return 0;
	case LO_lessthan: q.r = lua_lessthan (L, 1, 2); return 0;
	case LO_next: q.r = lua_next (L, 1); return q.r ? 2 : 0;
	case LO_concat: lua_concat (L, q.a); return 1;
	case LO_gc: q.r = lua_gc (L, q.a, q.b); return 0;
	case LO_callc: return Direct (L, q);
	case LO_callhook: q.hook (L, &q.ar); return 0;
	case OP_GENERIC: q.r = q.vessel->clbkGeneric (q.a, q.b, L); return 0; // on a copy of the frame (the vessel's userdata at its top)
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

bool Run (lua_State *L, Prot &q, std::initializer_list<int> idx, int nres) { return Run (L, q, idx.begin (), (int)idx.size (), nres); }

bool RunTop (lua_State *L, Prot &q, int n, int nres) // the operands are the top n
{
	std::vector<int> idx;
	for (int i = lua_gettop (L) - n + 1; i <= lua_gettop (L); i++) idx.push_back (i);
	return Run (L, q, idx.data (), n, nres);
}

void Collapse (lua_State *L, int n) // [v1 .. vn r] -> [r]
{
	lua_insert (L, -(n + 1));
	lua_pop (L, n);
}

// a precompiled chunk of the other Lua (the helper's size_t is 4 bytes, Linux's 8): its strings' sizes rewritten; any other as it is
struct Chunk {
	const std::string &in;
	size_t p;
	int from, to;
	std::string out;
	bool bad;
	void copy (size_t n) { if (in.size () - p < n) { bad = true; return; } out.append (in, p, n); p += n; }
	int32_t i32 ()
	{
		int32_t v = 0;
		if (in.size () - p < 4) { bad = true; return 0; }
		memcpy (&v, in.data () + p, 4);
		copy (4);
		return v;
	}
	void str ()
	{
		uint64_t n = 0;
		if (in.size () - p < (size_t)from) { bad = true; return; }
		memcpy (&n, in.data () + p, from);
		p += from;
		if (to == 4 && n > 0xffffffffu) { bad = true; return; }
		out.append ((const char *)&n, to);
		copy (n);
	}
	void fn ()
	{
		str ();                                      // source
		i32 (); i32 (); copy (4);                    // lines, upvalues, parameters, vararg, stack size
		int32_t n = i32 ();                          // code
		if (n < 0) bad = true; else copy ((size_t)n * 4);
		n = i32 ();                                  // constants
		for (int32_t i = 0; i < n && !bad; i++) {
			if (p >= in.size ()) { bad = true; break; }
			char t = in[p];
			copy (1);
			if (t == LUA_TBOOLEAN) copy (1);
			else if (t == LUA_TNUMBER) copy (8);
			else if (t == LUA_TSTRING) str ();
			else if (t != LUA_TNIL) bad = true;
		}
		n = i32 ();                                  // functions
		for (int32_t i = 0; i < n && !bad; i++) fn ();
		n = i32 ();                                  // line info
		if (n < 0) bad = true; else copy ((size_t)n * 4);
		n = i32 ();                                  // locals
		for (int32_t i = 0; i < n && !bad; i++) { str (); i32 (); i32 (); }
		n = i32 ();                                  // upvalue names
		for (int32_t i = 0; i < n && !bad; i++) str ();
	}
};

void Rechunk (std::string &c, int to)
{
	int from = to == 8 ? 4 : 8;
	static const char head[8] = {'\033', 'L', 'u', 'a', 0x51, 0, 1, 4}; // version 5.1, official format, little endian, 4-byte int
	if (c.size () < 12 || memcmp (c.data (), head, 8) || c[8] != from || c[9] != 4 || c[10] != 8 || c[11] != 0) return;
	Chunk k {c, 12, from, to, c.substr (0, 12), false};
	k.out[8] = (char)to;
	k.fn ();
	if (!k.bad && k.p == c.size ()) c.swap (k.out);
}

struct Reader { const std::string *s; bool done; };
const char *ReadChunk (lua_State *, void *d, size_t *n)
{
	Reader *r = (Reader *)d;
	if (r->done) { *n = 0; return nullptr; }
	r->done = true;
	*n = r->s->size ();
	return r->s->data ();
}

int WriteChunk (lua_State *, const void *p, size_t n, void *d)
{
	((std::string *)d)->append ((const char *)p, n);
	return 0;
}

std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

void PutStr (WbWriter &w, const char *s, size_t n)
{
	if (!s) { w.str (nullptr); return; }
	w.u32 ((uint32_t)n);
	w.raw (s, (uint32_t)n);
}

// one call; its arguments are read before Lua runs (Lua may call the helper, whose calls reuse the buffer), its results written after
void Serve (lua_State *L, int op, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	Prot q;
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
		if (!r.bad && T) lua_xmove (L, T, n);
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
		if (lua_type (L, i1) != LUA_TNONE && lua_type (L, i2) != LUA_TNONE) ok = Run (L, q, {i1, i2}, 0); // an index not valid: 0
		w.b (!ok);
		w.i32 (ok ? q.r : 0);
		break; }
	case LO_rawequal: { int i1 = Ix (r.i32 ()), i2 = Ix (r.i32 ()); if (!r.bad) w.i32 (lua_rawequal (L, i1, i2)); break; }
	case LO_tonumber: { int i = Ix (r.i32 ()); if (!r.bad) w.f64 (lua_tonumber (L, i)); break; }
	case LO_tolstring: { // a number becomes a string in its slot, as Lua does
		int i = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		int t = lua_type (L, i);
		bool ok = true;
		if (t == LUA_TNUMBER && (ok = Run (L, q, {i}, 1))) lua_replace (L, i);
		size_t n = 0;
		const char *s = ok && (t == LUA_TNUMBER || t == LUA_TSTRING) ? lua_tolstring (L, i, &n) : nullptr;
		w.b (!ok);
		PutStr (w, s, n);
		break; }
	case LO_objlen: { int i = Ix (r.i32 ()); if (!r.bad) w.u32 ((uint32_t)lua_objlen (L, i)); break; }
	case LO_tocfunction: {
		int i = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		lua_CFunction f = lua_tocfunction (L, i);
		PutFn (f, f ? ModuleFn (L, i) : 0, w);
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
		else { const void *p = lua_topointer (L, i); w.u32 (p ? b.Id (p) : 0); }
		break; }
	case LO_pushnil: lua_pushnil (L); break;
	case LO_pushnumber: { double v = r.f64 (); if (!r.bad) lua_pushnumber (L, v); break; }
	case LO_pushinteger: { int v = r.i32 (); if (!r.bad) lua_pushinteger (L, v); break; }
	case LO_pushboolean: { int v = r.i32 (); if (!r.bad) lua_pushboolean (L, v); break; }
	case LO_pushlightuserdata: { uint32_t v = r.u32 (); bool mapped = r.b (); if (!r.bad) lua_pushlightuserdata (L, LightIn (v, mapped)); break; }
	case LO_pushthread: w.i32 (lua_pushthread (L)); break;
	case LO_pushlstring: {
		uint32_t n;
		const char *s = r.str (&n);
		if (r.bad || !s) break;
		q.s.assign (s, n);
		w.b (!Run (L, q, {}, 1));
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
		if (op == LO_getfield) q.s = Str (r);
		if (r.bad) break;
		bool ok = op == LO_gettable ? Run (L, q, {t, lua_gettop (L)}, 1) : Run (L, q, {t}, 1);
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
		if (!r.bad) w.b (!Run (L, q, {}, 1));
		break; }
	case LO_newuserdata: {
		q.size = r.u32 ();
		q.block = r.u32 ();
		if (!r.bad) w.b (!Run (L, q, {}, 1));
		break; }
	case LO_settable: case LO_setfield: case LO_rawset: {
		int t = Abs (L, Ix (r.i32 ()));
		if (op == LO_setfield) q.s = Str (r);
		if (r.bad) break;
		int top = lua_gettop (L);
		bool ok = op == LO_setfield ? Run (L, q, {t, top}, 0) : Run (L, q, {t, top - 1, top}, 0);
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
		bool mapped = r.b ();
		if (r.bad) break;
		CpReq c {f, ud, mapped};
		w.i32 (kind == LF_LINUX ? lua_cpcall (L, (lua_CFunction)b.Ptr (f), LightIn (ud, mapped)) : lua_cpcall (L, CpTramp, &c));
		break; }
	case LO_load: {
		std::string chunk = Str (r), name = Str (r);
		if (r.bad) break;
		Rechunk (chunk, 8); // a precompiled chunk of the helper's Lua, as Linux's
		Reader rd {&chunk, false};
		w.i32 (lua_load (L, ReadChunk, &rd, name.c_str ()));
		break; }
	case LO_dump: {
		std::string out;
		int st = lua_dump (L, WriteChunk, &out);
		if (!st) Rechunk (out, 4);
		w.i32 (st);
		PutStr (w, out.data (), out.size ());
		break; }
	case LO_resume: { int n = r.i32 (); if (!r.bad) w.i32 (lua_resume (L, n)); break; }
	case LO_status: w.i32 (lua_status (L)); break;
	case LO_gc: {
		q.a = r.i32 ();
		q.b = r.i32 ();
		if (r.bad) break;
		bool ok = Run (L, q, {}, 0);
		w.b (!ok);
		w.i32 (q.r);
		break; }
	case LO_error: SetPending (L); break;
	case LO_next: {
		int t = Abs (L, Ix (r.i32 ()));
		if (r.bad) break;
		bool ok = Run (L, q, {t, lua_gettop (L)}, LUA_MULTRET);
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
		bool ok = Run (L, q, {}, 1);
		w.b (!ok);
		w.u32 (ok ? StateId (q.thread) : 0);
		break; }
	case LO_getstack: {
		int level = r.i32 ();
		if (r.bad) break;
		lua_Debug ar {};
		int ok = lua_getstack (L, level, &ar);
		w.i32 (ok);
		w.i32 (ar.i_ci);
		break; }
	case LO_getinfo: { // the module's function has one upvalue less than its closure
		std::string what = Str (r);
		int ci = r.i32 ();
		if (r.bad || what.empty ()) break;
		lua_Debug ar {};
		ar.i_ci = ci;
		bool top = what[0] == '>', u = what.find ('u') != std::string::npos, f = what.find ('f') != std::string::npos;
		bool tramp = top && u && lua_tocfunction (L, -1) == Tramp;
		std::string w2 = what + (u && !top && !f ? "f" : "");
		int ok = lua_getinfo (L, w2.c_str (), &ar);
		if (ok && u && !top) {
			int at = what.find ('L') != std::string::npos ? -2 : -1; // 'L' pushes its table after the function
			tramp = lua_tocfunction (L, at) == Tramp;
			if (!f) lua_remove (L, at);
		}
		if (tramp && ar.nups > 0) ar.nups--;
		w.i32 (ok);
		w.str (ar.name); w.str (ar.namewhat); w.str (ar.what); w.str (ar.source);
		w.i32 (ar.currentline); w.i32 (ar.nups); w.i32 (ar.linedefined); w.i32 (ar.lastlinedefined);
		w.str (ok && what.find ('S') != std::string::npos ? ar.short_src : nullptr);
		break; }
	case LO_getlocal: case LO_setlocal: {
		lua_Debug ar {};
		ar.i_ci = r.i32 ();
		int n = r.i32 ();
		if (r.bad) break;
		w.str (op == LO_getlocal ? lua_getlocal (L, &ar, n) : lua_setlocal (L, &ar, n));
		break; }
	case LO_getupvalue: case LO_setupvalue: {
		int f = Abs (L, Ix (r.i32 ())), n = r.i32 ();
		if (r.bad) break;
		if (n >= 1 && lua_tocfunction (L, f) == Tramp) n++;
		w.str (n < 1 ? nullptr : op == LO_getupvalue ? lua_getupvalue (L, f, n) : lua_setupvalue (L, f, n));
		break; }
	case LO_sethook: {
		int kind = r.i32 ();
		uint32_t f = r.u32 ();
		int mask = r.i32 (), count = r.i32 ();
		if (r.bad) break;
		SetHook (L, kind == LF_MODULE ? f : 0);
		lua_Hook h = !f || !mask ? nullptr : kind == LF_LINUX ? (lua_Hook)b.Ptr (f) : HookTramp;
		w.i32 (lua_sethook (L, h, mask, count));
		break; }
	case LO_gethook: {
		lua_Hook h = lua_gethook (L);
		if (!h) { w.i32 (LF_NONE); w.u32 (0); }
		else if (h == HookTramp) { w.i32 (LF_MODULE); w.u32 (Slot (L, WB_HOOKS, true)); }
		else { w.i32 (LF_LINUX); w.u32 (b.Id (reinterpret_cast<void *> (h))); }
		break; }
	case LO_gethookmask: w.i32 (lua_gethookmask (L)); break;
	case LO_gethookcount: w.i32 (lua_gethookcount (L)); break;
	case LO_atpanic: {
		int kind = r.i32 ();
		uint32_t f = r.u32 ();
		if (r.bad) break;
		uint32_t before = Slot (L, WB_PANIC, false);
		if (kind == LF_MODULE) SetPanic (L, f);
		lua_CFunction old = lua_atpanic (L, kind == LF_MODULE && f ? PanicTramp : kind == LF_LINUX ? (lua_CFunction)b.Ptr (f) : nullptr);
		PutFn (old, old == PanicTramp ? before : 0, w);
		break; }
	case LO_callc: {
		q.cf = (lua_CFunction)b.Ptr (r.u32 ());
		if (r.bad || !q.cf) break;
		int n = lua_gettop (L);
		bool ok = RunTop (L, q, n, LUA_MULTRET);
		if (ok) for (int i = 0; i < n; i++) lua_remove (L, 1);
		w.b (!ok);
		w.i32 (ok ? q.r : 0);
		break; }
	case LO_callhook: {
		q.hook = (lua_Hook)b.Ptr (r.u32 ());
		q.ar.event = r.i32 ();
		q.ar.currentline = r.i32 ();
		q.ar.i_ci = r.i32 ();
		if (r.bad || !q.hook) break;
		w.b (!Run (L, q, {}, 0));
		break; }
	default:
		r.bad = true;
		break;
	}
}

} // namespace

bool WbServeLua (uint32_t fn, WbReader &r, WbWriter &w)
{
	if (fn != L_LUA) return false;
	uint32_t sid = r.u32 ();
	int op = r.i32 ();
	lua_State *L = r.bad ? nullptr : StateOf (sid);
	if (L) Serve (L, op, r, w);
	if (!L || r.bad) WbBridge::Get ().BadArgs (fn);
	return true;
}

uint32_t WbLuaStateId (lua_State *L) { return StateId (L); }

void WbLuaRechunk (std::string &c, int to) { Rechunk (c, to); }

void WbLuaRaise (lua_State *L) { RaisePending (L); }

// a native vessel's clbkGeneric with Linux's state, which a module's hook passed on: run as an op (its error raised by the module's)
int WbLuaNativeGeneric (VESSEL3 *v, int msg, int prm, uint32_t sid, bool *err)
{
	*err = false;
	lua_State *L = StateOf (sid);
	if (!L) return 0;
	Prot q;
	q.op = OP_GENERIC;
	q.vessel = v;
	q.a = msg;
	q.b = prm;
	if (!RunTop (L, q, lua_gettop (L), 0)) { *err = true; return 0; }
	return q.r;
}

void WbLuaFlush ()
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	std::vector<uint32_t> blocks;
	{
		std::lock_guard<std::mutex> f (g_freeLock);
		for (const Sent &s : g_free) if (s.session == b.Session ()) blocks.push_back (s.block);
		g_free.clear ();
	}
	static uint32_t seen; // the helper's count of native blocks its Lua collected (LuaRemote.cpp)
	uint32_t helper = b.LuaFreeCount ();
	if ((blocks.empty () && helper == seen) || !b.Alive ()) return;
	seen = helper;
	WbWriter &w = b.Begin (H_LUAFREE);
	w.u32 ((uint32_t)blocks.size ());
	for (uint32_t k : blocks) w.u32 (k);
	WbReader r = b.Call ();
	WbLuaRemoteFreed (r);
}
