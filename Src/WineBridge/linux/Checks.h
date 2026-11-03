// not upstream (design-security-fixes F3, F4, F6): checks of what the helper sends, before Lua or Orbiter sees it
#pragma once
#include "lua.hpp"
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace wbcheck {

// an index of a module's op, as Ix gave it: on the stack (a read may go past the top), the
// registry, the globals, or the running C function's environment or one of its upvalues; Lua's own checks are compiled out
inline bool Valid (lua_State *L, int i, bool write = false)
{
	int top = lua_gettop (L);
	if (i > 0) return write ? i <= top : i <= top + LUAI_MAXCSTACK;
	if (i < 0 && i > LUA_REGISTRYINDEX) return -i <= top;
	if (i == LUA_REGISTRYINDEX || i == LUA_GLOBALSINDEX) return true;
	if (!i) return false;
	lua_Debug ar;
	if (!lua_getstack (L, 0, &ar) || !lua_getinfo (L, "Su", &ar) || strcmp (ar.what, "C")) return false;
	return i == LUA_ENVIRONINDEX || LUA_GLOBALSINDEX - i <= ar.nups;
}

// two states of one Lua universe (their registry is one table)
inline bool SameUniverse (lua_State *a, lua_State *b)
{
	if (a == b) return true;
	if (!lua_checkstack (a, 1) || !lua_checkstack (b, 1)) return false;
	lua_pushvalue (a, LUA_REGISTRYINDEX);
	lua_pushvalue (b, LUA_REGISTRYINDEX);
	bool same = lua_topointer (a, -1) == lua_topointer (b, -1);
	lua_pop (a, 1);
	lua_pop (b, 1);
	return same;
}

// a call level's i_ci from the helper is one lua_getstack gives
inline bool CiValid (lua_State *L, int ci)
{
	lua_Debug ar;
	for (int level = 0; level <= LUAI_MAXCCALLS * 2 && lua_getstack (L, level, &ar); level++)
		if (ar.i_ci == ci) return true;
	return false;
}


// a text's length within its string; less than 0, all of it
inline int TextLen (int len, const std::string &t) { return len >= 0 && (size_t)len <= t.size () ? len : (int)t.size (); }

// a poly-polyline's point counts: each 0 or more, their sum in 64 bits within most; -1 if not
inline int64_t PolyTotal (const std::vector<int> &n, int64_t most)
{
	int64_t total = 0;
	for (int k : n) {
		if (k < 0) return -1;
		total += k;
	}
	return total <= most ? total : -1;
}

}
