#include "Interpreter.h"
#include <Lua/lua_handles.h>

#include <cstring>
#include <memory>

// these collide with std::min/max
#undef min
#undef max

#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main() - only do this in one cpp file
#include "catch2/catch_all.hpp"

using std::make_unique;
using std::string;

// Test that interpreter is created and destroyed without exceptions
TEST_CASE("Create and test Lua interpreter", "[LuaInterpreter]" ) 
{
	auto interp = make_unique<Interpreter>();
	interp->Initialise();

	string script = "print(\"Hello World\")";
	interp->RunChunk(script.data(), script.size());

	script = "a = 1 + 3";
	interp->RunChunk(script.data(), script.size());
	auto L = interp->GetState();
	lua_getglobal(L, "a");
	REQUIRE(lua_tointeger(L, -1) == 4);
};

// not upstream: a content script's interpreter is confined (design-security-fixes B1): no programs, files only in the Orbiter folder, no bytecode
TEST_CASE("Content scripts are confined to the Orbiter folder", "[LuaInterpreter]")
{
	auto interp = make_unique<Interpreter>();
	interp->Initialise();
	string script =
		"r_gone = os.execute == nil and io.popen == nil and package.loadlib == nil and debug.getregistry == nil and debug.getupvalue == nil and debug.sethook == nil\n"
		"local ok, e = pcall(io.open, '/etc/hostname')\n"
		"r_outside = not ok and tostring(e):find('outside the Orbiter folder', 1, true) ~= nil\n"
		"local f = io.open('lua_confine.tmp', 'w') f:write('x = 5') f:close()\n"
		"dofile('lua_confine.tmp') r_inside = (x == 5)\n"
		"os.remove('lua_confine.tmp')\n"
		"r_dofile = not pcall(dofile, '/etc/passwd')\n"
		"local fn, m = loadstring(string.dump(function() end))\n"
		"r_bytecode = fn == nil and m:find('precompiled', 1, true) ~= nil\n"
		"r_udmeta = not pcall(debug.setmetatable, io.stdout, {}) and not pcall(debug.setfenv, io.stdout, {}) and not pcall(debug.setfenv, print, {})\n"
		"r_tablemeta = pcall(debug.setmetatable, {}, {})\n";
	interp->RunChunk(script.data(), script.size());
	auto L = interp->GetState();
	for (const char *r : {"r_gone", "r_outside", "r_inside", "r_dofile", "r_bytecode", "r_udmeta", "r_tablemeta"}) {
		lua_getglobal(L, r);
		INFO(r);
		CHECK(lua_toboolean(L, -1));
		lua_pop(L, 1);
	}
}

// not upstream: a terminated interpreter's script ends, also a loop that catches the error (design-security-fixes B7)
TEST_CASE("A terminated script ends", "[LuaInterpreter]")
{
	auto interp = make_unique<Interpreter>();
	interp->Initialise();
	interp->Terminate();
	string script = "while true do pcall(function() while true do end end) end";
	CHECK(interp->RunChunk(script.data(), script.size()) != 0);
}

// not upstream: the handles a state was given, with their kind and owner (Lua/lua_handles.h)
TEST_CASE("Handle registry", "[LuaInterpreter]")
{
	auto interp = make_unique<Interpreter>();
	interp->Initialise();
	auto L = interp->GetState();
	int a, b, owner, other;
	int top = lua_gettop(L);
	luahandle::Add(L, &a, luahandle::FILE);
	luahandle::Add(L, &b, luahandle::AIRFOIL, &owner);
	CHECK(luahandle::Known(L, &a, luahandle::FILE));
	CHECK_FALSE(luahandle::Known(L, &a, luahandle::AIRFOIL));
	CHECK(luahandle::Owner(L, &b) == &owner);
	luahandle::DropOwned(L, &other);
	CHECK(luahandle::Known(L, &b, luahandle::AIRFOIL));
	luahandle::DropOwned(L, &owner);
	CHECK_FALSE(luahandle::Known(L, &b, luahandle::AIRFOIL));
	luahandle::Drop(L, &a);
	CHECK_FALSE(luahandle::Known(L, &a, luahandle::FILE));
	CHECK(lua_gettop(L) == top);
}

// not upstream: a status the script owns frees its lists, and set frees the ones it replaces; the own __gc takes no other status (design-lua-status)
TEST_CASE("A script's own vessel status frees its lists", "[LuaInterpreter]")
{
	auto interp = make_unique<Interpreter>();
	interp->Initialise();
	auto L = interp->GetState();
	int top = lua_gettop(L);
	auto *own = (VESSELSTATUS2 *)lua_newuserdata(L, sizeof(VESSELSTATUS2));
	memset(own, 0, sizeof *own);
	own->version = 2;
	luaL_getmetatable(L, "VESSELSTATUS2.own");
	REQUIRE(lua_istable(L, -1));
	lua_setmetatable(L, -2);
	lua_setglobal(L, "own");
	auto *orb = (VESSELSTATUS2 *)lua_newuserdata(L, sizeof(VESSELSTATUS2));
	memset(orb, 0, sizeof *orb);
	orb->version = 2;
	luaL_getmetatable(L, "VESSELSTATUS2.table");
	lua_setmetatable(L, -2);
	lua_setglobal(L, "orb");
	string script =
		"own:set{fuel={{idx=0,level=0.5},{idx=1,level=1}}, thruster={{idx=2,level=0.25}}, dockinfo={{idx=0,ridx=1}}}\n"
		"own:set{fuel={{idx=3,level=0.75}}, thruster={5}}\n"
		"local t = own:get()\n"
		"r_get = #t.fuel == 1 and t.fuel[1].idx == 3 and t.fuel[1].level == 0.75 and #t.thruster == 0 and #t.dockinfo == 1\n"
		"r_other = not pcall(getmetatable(own).__gc, orb)\n"
		"orb:set{fuel={{idx=0,level=1}}} orb:set{fuel={{idx=1,level=1}}}\n"
		"getmetatable(own).__gc(own)\n";
	interp->RunChunk(script.data(), script.size());
	for (const char *r : {"r_get", "r_other"}) {
		lua_getglobal(L, r);
		INFO(r);
		CHECK(lua_toboolean(L, -1));
		lua_pop(L, 1);
	}
	CHECK((own->fuel == nullptr && own->thruster == nullptr && own->dockinfo == nullptr));
	CHECK((own->nfuel == 0 && own->nthruster == 0 && own->ndockinfo == 0));
	REQUIRE(orb->nfuel == 1);
	CHECK(orb->fuel[0].idx == 1);
	delete[] orb->fuel; // a borrowed status's lists are its owner's
	orb->fuel = nullptr, orb->nfuel = 0;
	CHECK(lua_gettop(L) == top);
}
