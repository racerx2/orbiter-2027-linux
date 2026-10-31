/* not upstream (dll-modules): lua.so's API. A remote state's calls go to what a bridge registered (lua_setremote), a real state's to
   Lua's own (lual_: Lua's core renamed, lua_rename.h). Lua's libraries call these, so they work on either. */
#define LUA_CORE
#include "lua.h"
#include "luaconf.h"
#include "lua_remote.h"

static const lua_Remote *remote;

#define F(r, n, p, a) r lual_##n p;
#define FV(n, p, a) void lual_##n p;
LUA_REMOTE_LIST (F, FV)
#undef F
#undef FV
lua_State *lual_newstate (lua_Alloc f, void *ud);
void lual_xmove (lua_State *from, lua_State *to, int n);
lua_Integer lual_tointeger (lua_State *L, int idx);
const char *lual_typename (lua_State *L, int tp);

#define R(L) (remote && lua_isremote (L))
#define F(r, n, p, a) LUA_API r lua_##n p { return R (L) ? remote->n a : lual_##n a; }
#define FV(n, p, a) LUA_API void lua_##n p { if (R (L)) remote->n a; else lual_##n a; }
LUA_REMOTE_LIST (F, FV)
#undef F
#undef FV

LUA_API lua_State *lua_newstate (lua_Alloc f, void *ud) { return lual_newstate (f, ud); }

LUA_API void lua_xmove (lua_State *from, lua_State *to, int n)
{
	if (R (from) || R (to)) remote->xmove (from, to, n);
	else lual_xmove (from, to, n);
}

LUA_API lua_Integer lua_tointeger (lua_State *L, int idx)
{
	lua_Integer r;
	lua_Number n;
	if (!R (L)) return lual_tointeger (L, idx);
	n = remote->tonumber (L, idx);
	lua_number2integer (r, n);
	return r;
}

LUA_API const char *lua_typename (lua_State *L, int tp) { return lual_typename (L, tp); }

LUA_API const char *lua_pushfstring (lua_State *L, const char *fmt, ...)
{
	const char *s;
	va_list ap;
	va_start (ap, fmt);
	s = lua_pushvfstring (L, fmt, ap);
	va_end (ap);
	return s;
}

LUA_API int lua_setremote (const lua_Remote *r, size_t size)
{
	if (r && size != sizeof (lua_Remote)) return 0;
	remote = r;
	return 1;
}
