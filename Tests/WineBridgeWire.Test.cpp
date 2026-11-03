// not upstream: WineBridge's checks of what the helper sends: ids and their kinds, Lua indices and levels, counts and lengths (design-security-fixes F1-F4, F6)
#include <catch2/catch_test_macros.hpp>
#include "Bridge.h"
#include "Checks.h"
#include <climits>

namespace {
int AnyFn (lua_State *) { return 0; }

struct Seen { bool up1, up2, up3, env, ci, ci9; };
int Inside (lua_State *L)
{
	Seen *s = (Seen *)lua_touserdata (L, 1);
	s->up1 = wbcheck::Valid (L, lua_upvalueindex (1));
	s->up2 = wbcheck::Valid (L, lua_upvalueindex (2), true);
	s->up3 = wbcheck::Valid (L, lua_upvalueindex (3));
	s->env = wbcheck::Valid (L, LUA_ENVIRONINDEX);
	lua_Debug ar;
	s->ci = lua_getstack (L, 0, &ar) && wbcheck::CiValid (L, ar.i_ci);
	s->ci9 = wbcheck::CiValid (L, 9999);
	return 0;
}
}

TEST_CASE ("WineBridge ids: a kind each, functions only as functions, retired ones name nothing", "[winebridge]")
{
	WbIds ids;
	int mesh, vessel, any, block;
	uint32_t m = ids.Id (&mesh, WK_MESHHANDLE), v = ids.Id (&vessel, WK_OBJHANDLE), a = ids.Id (&any), b = ids.Id (&block, WK_BLOCK);
	uint32_t f = ids.Id (reinterpret_cast<void *> (&AnyFn), WK_FN);
	CHECK (ids.Ptr (m, WK_MESHHANDLE) == &mesh);
	CHECK (ids.Ptr (m, WK_OBJHANDLE) == nullptr); // a mesh where a vessel is asked for
	CHECK (ids.Ptr (v, WK_MESHHANDLE) == nullptr);
	CHECK (ids.Ptr (a, WK_SURFHANDLE) == &any);  // hand-written code's ids go anywhere
	CHECK (ids.Is (a, WK_FN) == nullptr);        // ... but never as code
	CHECK (ids.Is (m, WK_FN) == nullptr);
	CHECK (ids.Is (v, WK_FN) == nullptr);
	CHECK (ids.Is (f, WK_FN) == reinterpret_cast<void *> (&AnyFn));
	CHECK (ids.Ptr (f, WK_MESHHANDLE) == nullptr); // a function as a handle
	CHECK (ids.Is (b, WK_BLOCK) == &block);
	CHECK (ids.Is (a, WK_BLOCK) == nullptr);       // only a block is ever freed
	CHECK (ids.Ptr (1u << 30) == nullptr);
	CHECK (ids.Ptr (1u << 30, WK_ANY) == nullptr);
	CHECK (ids.Id (&mesh) == m); // the same pointer, the same id and kind
	CHECK (ids.Kind (m) == WK_MESHHANDLE);
	CHECK (ids.Id (&any, WK_SURFHANDLE) == a);
	CHECK (ids.Kind (a) == WK_SURFHANDLE); // a kind given later
	ids.Id (&any, WK_OBJHANDLE);
	CHECK (ids.Kind (a) == WK_ANY); // given as two kinds: either
	ids.Retire (&mesh);
	CHECK (ids.Ptr (m) == nullptr);
	CHECK (ids.Ptr (m, WK_MESHHANDLE) == nullptr);
	uint32_t m2 = ids.Id (&mesh, WK_MESHHANDLE);
	CHECK (m2 != m); // a new id: the old one stays retired
	CHECK (ids.Ptr (m) == nullptr);
	ids.Retire (&block);
	CHECK (ids.Is (b, WK_BLOCK) == nullptr); // freed once
	ids.Clear ();
	CHECK (ids.Ptr (v) == nullptr);
	CHECK (ids.Id (nullptr, WK_FN) == 0);
}

TEST_CASE ("WineBridge Lua checks: indices on the stack or real pseudo-indices, levels Lua gave, one universe", "[winebridge]")
{
	lua_State *L = luaL_newstate ();
	REQUIRE (L);
	lua_pushnil (L); lua_pushnil (L); lua_pushnil (L);
	CHECK (wbcheck::Valid (L, 1));
	CHECK (wbcheck::Valid (L, 3, true));
	CHECK_FALSE (wbcheck::Valid (L, 4, true)); // past the top: no write
	CHECK (wbcheck::Valid (L, 4));             // ... a read is none
	CHECK_FALSE (wbcheck::Valid (L, 100000));
	CHECK (wbcheck::Valid (L, -3));
	CHECK_FALSE (wbcheck::Valid (L, -4));      // below the frame
	CHECK_FALSE (wbcheck::Valid (L, -100000));
	CHECK_FALSE (wbcheck::Valid (L, 0));
	CHECK (wbcheck::Valid (L, LUA_REGISTRYINDEX));
	CHECK (wbcheck::Valid (L, LUA_GLOBALSINDEX));
	CHECK_FALSE (wbcheck::Valid (L, LUA_ENVIRONINDEX)); // no function runs
	CHECK_FALSE (wbcheck::Valid (L, lua_upvalueindex (1)));
	CHECK_FALSE (wbcheck::CiValid (L, 0));
	Seen s {};
	lua_pushnumber (L, 1); lua_pushnumber (L, 2);
	lua_pushcclosure (L, Inside, 2);
	lua_pushlightuserdata (L, &s);
	REQUIRE (lua_pcall (L, 1, 0, 0) == 0);
	CHECK (s.up1);
	CHECK (s.up2);
	CHECK_FALSE (s.up3); // past its upvalues
	CHECK (s.env);
	CHECK (s.ci);
	CHECK_FALSE (s.ci9);
	lua_State *T = lua_newthread (L);
	CHECK (wbcheck::SameUniverse (L, T));
	lua_State *O = luaL_newstate ();
	CHECK_FALSE (wbcheck::SameUniverse (L, O));
	lua_close (O);
	lua_close (L);
}

TEST_CASE ("WineBridge counts and lengths: no negative point count, the sum in 64 bits, a text within its string", "[winebridge]")
{
	CHECK (wbcheck::PolyTotal ({-1000, 1003}, 1 << 20) == -1);
	CHECK (wbcheck::PolyTotal ({INT_MAX, INT_MAX}, 1 << 20) == -1);
	CHECK (wbcheck::PolyTotal ({INT_MAX, 2}, (int64_t)1 << 40) == (int64_t)INT_MAX + 2); // no wrap
	CHECK (wbcheck::PolyTotal ({2, 3}, 1 << 20) == 5);
	CHECK (wbcheck::PolyTotal ({}, 1 << 20) == 0);
	CHECK (wbcheck::TextLen (-1, "abc") == 3);
	CHECK (wbcheck::TextLen (0, "abc") == 0);
	CHECK (wbcheck::TextLen (2, "abc") == 2);
	CHECK (wbcheck::TextLen (100000, "abc") == 3);
}
