/* not upstream (dll-modules): another process's Lua states in lua.so. A state whose type byte (after its first pointer, where a real
   state's is) is LUA_REMOTE_TT is a bridge's object for one: lua.so's API functions send its calls to what lua_setremote registered. */
#ifndef lua_remote_h
#define lua_remote_h

#include <stdarg.h>
#include <stddef.h>
#include "lua.h"

#define LUA_REMOTE_TT 0xe7
#define lua_isremote(L) (((const unsigned char *)(L))[sizeof (void *)] == LUA_REMOTE_TT)

/* the functions a remote state's calls go to: result type, name, parameters, arguments */
#define LUA_REMOTE_LIST(F, FV) \
	F (lua_State *, newthread, (lua_State *L), (L)) \
	F (lua_CFunction, atpanic, (lua_State *L, lua_CFunction panicf), (L, panicf)) \
	F (int, gettop, (lua_State *L), (L)) \
	FV (settop, (lua_State *L, int idx), (L, idx)) \
	FV (pushvalue, (lua_State *L, int idx), (L, idx)) \
	FV (remove, (lua_State *L, int idx), (L, idx)) \
	FV (insert, (lua_State *L, int idx), (L, idx)) \
	FV (replace, (lua_State *L, int idx), (L, idx)) \
	F (int, checkstack, (lua_State *L, int sz), (L, sz)) \
	F (int, isnumber, (lua_State *L, int idx), (L, idx)) \
	F (int, isstring, (lua_State *L, int idx), (L, idx)) \
	F (int, iscfunction, (lua_State *L, int idx), (L, idx)) \
	F (int, isuserdata, (lua_State *L, int idx), (L, idx)) \
	F (int, type, (lua_State *L, int idx), (L, idx)) \
	F (int, equal, (lua_State *L, int idx1, int idx2), (L, idx1, idx2)) \
	F (int, rawequal, (lua_State *L, int idx1, int idx2), (L, idx1, idx2)) \
	F (int, lessthan, (lua_State *L, int idx1, int idx2), (L, idx1, idx2)) \
	F (lua_Number, tonumber, (lua_State *L, int idx), (L, idx)) \
	F (int, toboolean, (lua_State *L, int idx), (L, idx)) \
	F (const char *, tolstring, (lua_State *L, int idx, size_t *len), (L, idx, len)) \
	F (size_t, objlen, (lua_State *L, int idx), (L, idx)) \
	F (lua_CFunction, tocfunction, (lua_State *L, int idx), (L, idx)) \
	F (void *, touserdata, (lua_State *L, int idx), (L, idx)) \
	F (lua_State *, tothread, (lua_State *L, int idx), (L, idx)) \
	F (const void *, topointer, (lua_State *L, int idx), (L, idx)) \
	FV (pushnil, (lua_State *L), (L)) \
	FV (pushnumber, (lua_State *L, lua_Number n), (L, n)) \
	FV (pushinteger, (lua_State *L, lua_Integer n), (L, n)) \
	FV (pushlstring, (lua_State *L, const char *s, size_t l), (L, s, l)) \
	FV (pushstring, (lua_State *L, const char *s), (L, s)) \
	F (const char *, pushvfstring, (lua_State *L, const char *fmt, va_list argp), (L, fmt, argp)) \
	FV (pushcclosure, (lua_State *L, lua_CFunction fn, int n), (L, fn, n)) \
	FV (pushboolean, (lua_State *L, int b), (L, b)) \
	FV (pushlightuserdata, (lua_State *L, void *p), (L, p)) \
	F (int, pushthread, (lua_State *L), (L)) \
	FV (gettable, (lua_State *L, int idx), (L, idx)) \
	FV (getfield, (lua_State *L, int idx, const char *k), (L, idx, k)) \
	FV (rawget, (lua_State *L, int idx), (L, idx)) \
	FV (rawgeti, (lua_State *L, int idx, int n), (L, idx, n)) \
	FV (createtable, (lua_State *L, int narr, int nrec), (L, narr, nrec)) \
	F (void *, newuserdata, (lua_State *L, size_t sz), (L, sz)) \
	F (int, getmetatable, (lua_State *L, int objindex), (L, objindex)) \
	FV (getfenv, (lua_State *L, int idx), (L, idx)) \
	FV (settable, (lua_State *L, int idx), (L, idx)) \
	FV (setfield, (lua_State *L, int idx, const char *k), (L, idx, k)) \
	FV (rawset, (lua_State *L, int idx), (L, idx)) \
	FV (rawseti, (lua_State *L, int idx, int n), (L, idx, n)) \
	F (int, setmetatable, (lua_State *L, int objindex), (L, objindex)) \
	F (int, setfenv, (lua_State *L, int idx), (L, idx)) \
	FV (call, (lua_State *L, int nargs, int nresults), (L, nargs, nresults)) \
	F (int, pcall, (lua_State *L, int nargs, int nresults, int errfunc), (L, nargs, nresults, errfunc)) \
	F (int, cpcall, (lua_State *L, lua_CFunction func, void *ud), (L, func, ud)) \
	F (int, load, (lua_State *L, lua_Reader reader, void *dt, const char *chunkname), (L, reader, dt, chunkname)) \
	F (int, dump, (lua_State *L, lua_Writer writer, void *data), (L, writer, data)) \
	F (int, yield, (lua_State *L, int nresults), (L, nresults)) \
	F (int, resume, (lua_State *L, int narg), (L, narg)) \
	F (int, status, (lua_State *L), (L)) \
	F (int, gc, (lua_State *L, int what, int data), (L, what, data)) \
	F (int, error, (lua_State *L), (L)) \
	F (int, next, (lua_State *L, int idx), (L, idx)) \
	FV (concat, (lua_State *L, int n), (L, n)) \
	F (lua_Alloc, getallocf, (lua_State *L, void **ud), (L, ud)) \
	FV (setallocf, (lua_State *L, lua_Alloc f, void *ud), (L, f, ud)) \
	FV (close, (lua_State *L), (L)) \
	F (int, getstack, (lua_State *L, int level, lua_Debug *ar), (L, level, ar)) \
	F (int, getinfo, (lua_State *L, const char *what, lua_Debug *ar), (L, what, ar)) \
	F (const char *, getlocal, (lua_State *L, const lua_Debug *ar, int n), (L, ar, n)) \
	F (const char *, setlocal, (lua_State *L, const lua_Debug *ar, int n), (L, ar, n)) \
	F (const char *, getupvalue, (lua_State *L, int funcindex, int n), (L, funcindex, n)) \
	F (const char *, setupvalue, (lua_State *L, int funcindex, int n), (L, funcindex, n)) \
	F (int, sethook, (lua_State *L, lua_Hook func, int mask, int count), (L, func, mask, count)) \
	F (lua_Hook, gethook, (lua_State *L), (L)) \
	F (int, gethookmask, (lua_State *L), (L)) \
	F (int, gethookcount, (lua_State *L), (L))
/* the others: lua_newstate (a real state), lua_xmove (either state remote: xmove below), lua_tointeger (tonumber's, converted as Lua
   converts), lua_typename (the same names), lua_pushfstring (pushvfstring's) */

#define LUA_REMOTE_F(r, n, p, a) r (*n) p;
#define LUA_REMOTE_FV(n, p, a) void (*n) p;
typedef struct lua_Remote {
	LUA_REMOTE_LIST (LUA_REMOTE_F, LUA_REMOTE_FV)
	void (*xmove) (lua_State *from, lua_State *to, int n);
} lua_Remote;
#undef LUA_REMOTE_F
#undef LUA_REMOTE_FV

/* r: the functions (null: none), size: sizeof (lua_Remote) as the caller has it; 0 if that isn't lua.so's */
LUA_API int lua_setremote (const lua_Remote *r, size_t size);

#endif
