/* WineBridge: lua.dll's API (not upstream): a remote state's calls go to the helper (Orbiter.exe's wbr_ functions), a real state's to
   Lua's own (wbl_: Lua's core renamed, wb_rename.h). Lua's libraries call these, so they work on either. */
#define LUA_CORE
#include <stdarg.h>
#include "lua.h"
#include "wb_luaapi.h"

#define WB_LUA(r, n, p, a) r wbl_##n p;
#define WB_LUAV(n, p, a) void wbl_##n p;
WB_LUA_LIST
#undef WB_LUA
#undef WB_LUAV
lua_State *wbl_newstate (lua_Alloc f, void *ud);
void wbl_xmove (lua_State *from, lua_State *to, int n);
lua_Integer wbl_tointeger (lua_State *L, int idx);
const char *wbl_typename (lua_State *L, int tp);

#define WB_LUA(r, n, p, a) LUA_API r lua_##n p { return wb_remote (L) ? wbr_##n a : wbl_##n a; }
#define WB_LUAV(n, p, a) LUA_API void lua_##n p { if (wb_remote (L)) wbr_##n a; else wbl_##n a; }
WB_LUA_LIST
#undef WB_LUA
#undef WB_LUAV

LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud) { return wbl_newstate (f, ud); }

LUA_API void lua_xmove (lua_State *from, lua_State *to, int n)
{
	if (wb_remote (from) || wb_remote (to)) wbr_xmove (from, to, n);
	else wbl_xmove (from, to, n);
}

/* converted as Lua converts (luaconf.h, as the MSVC build: rounded) */
LUA_API lua_Integer lua_tointeger (lua_State *L, int idx)
{
	lua_Integer r;
	lua_Number n;
	if (!wb_remote (L)) return wbl_tointeger (L, idx);
	n = wbr_tonumber (L, idx);
	lua_number2integer (r, n);
	return r;
}

LUA_API const char *lua_typename (lua_State *L, int tp) { return wbl_typename (L, tp); }

LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...)
{
	const char *s;
	va_list ap;
	va_start (ap, fmt);
	s = lua_pushvfstring (L, fmt, ap);
	va_end (ap);
	return s;
}
