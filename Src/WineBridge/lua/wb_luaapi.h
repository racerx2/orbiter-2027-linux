// WineBridge: lua.dll's API on a Linux state (not upstream). The helper's wbr_ functions serve a remote state, Lua's own (renamed
// wbl_, wb_rename.h) a real one. WB_LUA lists the functions both serve alike (result type, name, parameters, arguments). Lua's lua.h
// comes first (the helper's is the SDK's).
#pragma once

// a remote state: the helper's object for a Linux state; its type byte is where a real state's is (LUA_TTHREAD)
#define WB_REMOTE_TT 0xe7
#define wb_remote(L) (((const unsigned char *)(L))[4] == WB_REMOTE_TT)

#define WB_LUA_LIST \
	WB_LUA (lua_State *, newthread, (lua_State *L), (L)) \
	WB_LUA (lua_CFunction, atpanic, (lua_State *L, lua_CFunction panicf), (L, panicf)) \
	WB_LUA (int, gettop, (lua_State *L), (L)) \
	WB_LUAV (settop, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (pushvalue, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (remove, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (insert, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (replace, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, checkstack, (lua_State *L, int sz), (L, sz)) \
	WB_LUA (int, isnumber, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, isstring, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, iscfunction, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, isuserdata, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, type, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, equal, (lua_State *L, int idx1, int idx2), (L, idx1, idx2)) \
	WB_LUA (int, rawequal, (lua_State *L, int idx1, int idx2), (L, idx1, idx2)) \
	WB_LUA (int, lessthan, (lua_State *L, int idx1, int idx2), (L, idx1, idx2)) \
	WB_LUA (lua_Number, tonumber, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (int, toboolean, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (const char *, tolstring, (lua_State *L, int idx, size_t *len), (L, idx, len)) \
	WB_LUA (size_t, objlen, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (lua_CFunction, tocfunction, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (void *, touserdata, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (lua_State *, tothread, (lua_State *L, int idx), (L, idx)) \
	WB_LUA (const void *, topointer, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (pushnil, (lua_State *L), (L)) \
	WB_LUAV (pushnumber, (lua_State *L, lua_Number n), (L, n)) \
	WB_LUAV (pushinteger, (lua_State *L, lua_Integer n), (L, n)) \
	WB_LUAV (pushlstring, (lua_State *L, const char *s, size_t l), (L, s, l)) \
	WB_LUAV (pushstring, (lua_State *L, const char *s), (L, s)) \
	WB_LUA (const char *, pushvfstring, (lua_State *L, const char *fmt, va_list argp), (L, fmt, argp)) \
	WB_LUAV (pushcclosure, (lua_State *L, lua_CFunction fn, int n), (L, fn, n)) \
	WB_LUAV (pushboolean, (lua_State *L, int b), (L, b)) \
	WB_LUAV (pushlightuserdata, (lua_State *L, void *p), (L, p)) \
	WB_LUA (int, pushthread, (lua_State *L), (L)) \
	WB_LUAV (gettable, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (getfield, (lua_State *L, int idx, const char *k), (L, idx, k)) \
	WB_LUAV (rawget, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (rawgeti, (lua_State *L, int idx, int n), (L, idx, n)) \
	WB_LUAV (createtable, (lua_State *L, int narr, int nrec), (L, narr, nrec)) \
	WB_LUA (void *, newuserdata, (lua_State *L, size_t sz), (L, sz)) \
	WB_LUA (int, getmetatable, (lua_State *L, int objindex), (L, objindex)) \
	WB_LUAV (getfenv, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (settable, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (setfield, (lua_State *L, int idx, const char *k), (L, idx, k)) \
	WB_LUAV (rawset, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (rawseti, (lua_State *L, int idx, int n), (L, idx, n)) \
	WB_LUA (int, setmetatable, (lua_State *L, int objindex), (L, objindex)) \
	WB_LUA (int, setfenv, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (call, (lua_State *L, int nargs, int nresults), (L, nargs, nresults)) \
	WB_LUA (int, pcall, (lua_State *L, int nargs, int nresults, int errfunc), (L, nargs, nresults, errfunc)) \
	WB_LUA (int, cpcall, (lua_State *L, lua_CFunction func, void *ud), (L, func, ud)) \
	WB_LUA (int, load, (lua_State *L, lua_Reader reader, void *dt, const char *chunkname), (L, reader, dt, chunkname)) \
	WB_LUA (int, dump, (lua_State *L, lua_Writer writer, void *data), (L, writer, data)) \
	WB_LUA (int, yield, (lua_State *L, int nresults), (L, nresults)) \
	WB_LUA (int, resume, (lua_State *L, int narg), (L, narg)) \
	WB_LUA (int, status, (lua_State *L), (L)) \
	WB_LUA (int, gc, (lua_State *L, int what, int data), (L, what, data)) \
	WB_LUA (int, error, (lua_State *L), (L)) \
	WB_LUA (int, next, (lua_State *L, int idx), (L, idx)) \
	WB_LUAV (concat, (lua_State *L, int n), (L, n)) \
	WB_LUA (lua_Alloc, getallocf, (lua_State *L, void **ud), (L, ud)) \
	WB_LUAV (setallocf, (lua_State *L, lua_Alloc f, void *ud), (L, f, ud)) \
	WB_LUAV (close, (lua_State *L), (L)) \
	WB_LUA (int, getstack, (lua_State *L, int level, lua_Debug *ar), (L, level, ar)) \
	WB_LUA (int, getinfo, (lua_State *L, const char *what, lua_Debug *ar), (L, what, ar)) \
	WB_LUA (const char *, getlocal, (lua_State *L, const lua_Debug *ar, int n), (L, ar, n)) \
	WB_LUA (const char *, setlocal, (lua_State *L, const lua_Debug *ar, int n), (L, ar, n)) \
	WB_LUA (const char *, getupvalue, (lua_State *L, int funcindex, int n), (L, funcindex, n)) \
	WB_LUA (const char *, setupvalue, (lua_State *L, int funcindex, int n), (L, funcindex, n)) \
	WB_LUA (int, sethook, (lua_State *L, lua_Hook func, int mask, int count), (L, func, mask, count)) \
	WB_LUA (lua_Hook, gethook, (lua_State *L), (L)) \
	WB_LUA (int, gethookmask, (lua_State *L), (L)) \
	WB_LUA (int, gethookcount, (lua_State *L), (L))
// the others: lua_newstate (a real state), lua_xmove (two states), lua_tointeger (lua_tonumber's, converted here), lua_typename
// (the same names), lua_pushfstring (lua_pushvfstring's)

#ifdef WB_HELPER
#define WBR_API __declspec(dllexport)
#else
#define WBR_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define WB_LUA(r, n, p, a) WBR_API r wbr_##n p;
#define WB_LUAV(n, p, a) WBR_API void wbr_##n p;
WB_LUA_LIST
#undef WB_LUA
#undef WB_LUAV
WBR_API void wbr_xmove (lua_State *from, lua_State *to, int n); // either remote
#ifdef __cplusplus
}
#endif
