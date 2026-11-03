// not upstream: handles a script may pass back, kept per Lua state with their kind and owner vessel (design-security-fixes B5); the bindings
// check a handle against this, or against the core, before they use it, and a vessel's go when it is deleted (Interpreter::DeleteVessel)
#pragma once
extern "C" {
#include <Lua/lua.h>
}
#include <initializer_list>

namespace luahandle {

enum Kind {
	OBJECT = 1, PROPELLANT, THRUSTER, THGROUP, DOCK, ATTACHMENT, NAV, SURFACE, PSTREAM, ANIMCOMP, // checked against the core
	FILE, AIRFOIL, CTRLSURF, TRANSFORM, PANEL, VISUAL, KEYSTATE, FONT, PEN, BRUSH // given out by the bindings, kept here
};

inline void Table (lua_State *L, const char *name) // the registry table name, made on first use, on the stack
{
	lua_getfield (L, LUA_REGISTRYINDEX, name);
	if (!lua_istable (L, -1)) {
		lua_pop (L, 1);
		lua_newtable (L);
		lua_pushvalue (L, -1);
		lua_setfield (L, LUA_REGISTRYINDEX, name);
	}
}

inline void Add (lua_State *L, const void *h, int kind, const void *owner = nullptr)
{
	if (!h) return;
	Table (L, "orbiter.handles");
	lua_pushlightuserdata (L, const_cast<void *> (h));
	lua_pushinteger (L, kind);
	lua_rawset (L, -3);
	lua_pop (L, 1);
	Table (L, "orbiter.handleowners");
	lua_pushlightuserdata (L, const_cast<void *> (h));
	if (owner) lua_pushlightuserdata (L, const_cast<void *> (owner)); else lua_pushnil (L);
	lua_rawset (L, -3);
	lua_pop (L, 1);
}

inline void Drop (lua_State *L, const void *h)
{
	for (const char *t : {"orbiter.handles", "orbiter.handleowners"}) {
		lua_getfield (L, LUA_REGISTRYINDEX, t);
		if (lua_istable (L, -1)) {
			lua_pushlightuserdata (L, const_cast<void *> (h));
			lua_pushnil (L);
			lua_rawset (L, -3);
		}
		lua_pop (L, 1);
	}
}

inline void DropOwned (lua_State *L, const void *owner) // every handle of the owner vessel
{
	lua_getfield (L, LUA_REGISTRYINDEX, "orbiter.handleowners");
	if (!lua_istable (L, -1)) { lua_pop (L, 1); return; }
	int t = lua_gettop (L), n = 0;
	lua_newtable (L); // the handles to drop, collected first (a table can't change while lua_next walks it)
	lua_pushnil (L);
	while (lua_next (L, t)) {
		if (lua_touserdata (L, -1) == owner) {
			lua_pushvalue (L, -2);
			lua_rawseti (L, t + 1, ++n);
		}
		lua_pop (L, 1);
	}
	for (int i = 1; i <= n; i++) {
		lua_rawgeti (L, t + 1, i);
		const void *h = lua_touserdata (L, -1);
		lua_pop (L, 1);
		Drop (L, h);
	}
	lua_pop (L, 2);
}

inline bool Known (lua_State *L, const void *h, int kind)
{
	if (!h) return false;
	lua_getfield (L, LUA_REGISTRYINDEX, "orbiter.handles");
	bool ok = false;
	if (lua_istable (L, -1)) {
		lua_pushlightuserdata (L, const_cast<void *> (h));
		lua_rawget (L, -2);
		ok = lua_tointeger (L, -1) == kind;
		lua_pop (L, 1);
	}
	lua_pop (L, 1);
	return ok;
}

// a light userdata pushed as a handle of kind (of vessel owner), so the bindings take it back
inline void Push (lua_State *L, const void *h, int kind, const void *owner = nullptr)
{
	Add (L, h, kind, owner);
	lua_pushlightuserdata (L, const_cast<void *> (h));
}

inline const void *Owner (lua_State *L, const void *h)
{
	lua_getfield (L, LUA_REGISTRYINDEX, "orbiter.handleowners");
	const void *o = nullptr;
	if (lua_istable (L, -1)) {
		lua_pushlightuserdata (L, const_cast<void *> (h));
		lua_rawget (L, -2);
		o = lua_touserdata (L, -1);
		lua_pop (L, 1);
	}
	lua_pop (L, 1);
	return o;
}

}
