// not upstream: content scripts confined to the Orbiter folder (design-security-fixes B1)
// The wrappers call Lua's own C functions through their pointers, never as Lua values, so no hook or getinfo can reach them.

#include "Confine.h"
#include "OrbiterAPI.h"
extern "C" {
#include <Lua/lua.h>
#include <Lua/lauxlib.h>
#include <Lua/lualib.h>
}
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

const char *const confine::Hint = " (scripts are kept to the Orbiter folder; Launchpad > Options > User interface: \"Scripts: full system access\")";

namespace {

const char *const KEY = "orbiter.confined"; // registry flag of a confined state

const fs::path &Root ()
{
	static const fs::path r = [] {
		std::error_code ec;
		fs::path p = fs::canonical (fs::current_path (ec), ec);
		return ec ? fs::path () : p;
	} ();
	return r;
}

void *Ptr (lua_CFunction f) { return reinterpret_cast<void *> (f); }
lua_CFunction Fn (void *p) { return reinterpret_cast<lua_CFunction> (p); }

// upvalues: 1 the stock function, 2 the mask of arguments that are paths, 3 the name for messages
int Guarded (lua_State *L)
{
	lua_CFunction f = Fn (lua_touserdata (L, lua_upvalueindex (1)));
	int mask = (int)lua_tointeger (L, lua_upvalueindex (2));
	for (int i = 0; i < 4; i++)
		if ((mask >> i & 1) && lua_type (L, i + 1) == LUA_TSTRING && !confine::Inside (lua_tostring (L, i + 1)))
			return luaL_error (L, "%s: %s is outside the Orbiter folder%s", lua_tostring (L, lua_upvalueindex (3)), lua_tostring (L, i + 1), confine::Hint);
	return f (L);
}

// os.setlocale: a query only, Orbiter reads numbers in the C locale
int SetlocaleQuery (lua_State *L)
{
	if (!lua_isnoneornil (L, 1)) return luaL_error (L, "os.setlocale: changing the locale is refused%s", confine::Hint);
	lua_CFunction f = Fn (lua_touserdata (L, lua_upvalueindex (1)));
	return f (L);
}

// debug.setmetatable/setfenv: not on a userdata (its metatable is its type to the bindings) or a C function (its environment holds its library's state)
int KeepNative (lua_State *L)
{
	if (lua_type (L, 1) == LUA_TUSERDATA || lua_iscfunction (L, 1))
		return luaL_error (L, "%s: a userdata or C function can't be changed%s", lua_tostring (L, lua_upvalueindex (2)), confine::Hint);
	lua_CFunction f = Fn (lua_touserdata (L, lua_upvalueindex (1)));
	return f (L);
}

// the table on the top: field name wrapped with the path mask (0: removed, -1 query only, -2 not on native objects), the stock function's environment kept
void Wrap (lua_State *L, const char *lib, const char *name, int mask)
{
	lua_getfield (L, -1, name);
	if (lua_tocfunction (L, -1) == nullptr) { lua_pop (L, 1); return; }
	if (!mask) {
		lua_pop (L, 1);
		lua_pushnil (L);
		lua_setfield (L, -2, name);
		return;
	}
	lua_CFunction f = lua_tocfunction (L, -1);
	lua_getfenv (L, -1);
	std::string full = std::string (lib) + "." + name;
	lua_pushlightuserdata (L, Ptr (f));
	if (mask < 0) {
		lua_pushstring (L, full.c_str ());
		lua_pushcclosure (L, mask == -1 ? SetlocaleQuery : KeepNative, 2);
	} else {
		lua_pushinteger (L, mask);
		lua_pushstring (L, full.c_str ());
		lua_pushcclosure (L, Guarded, 3);
	}
	lua_insert (L, -2);
	lua_setfenv (L, -2);
	lua_setfield (L, -3, name);
	lua_pop (L, 1);
}

// a source file as a function on the stack (1), or nil and a message (2): inside the folder, never bytecode
int LoadSource (lua_State *L, const char *name)
{
	std::string p = oapiResolvePath (name);
	if (!confine::Inside (p.c_str ())) {
		lua_pushnil (L);
		lua_pushfstring (L, "%s is outside the Orbiter folder%s", name, confine::Hint);
		return 2;
	}
	std::ifstream f (p, std::ios::binary);
	if (!f) {
		lua_pushnil (L);
		lua_pushfstring (L, "cannot open %s", name);
		return 2;
	}
	std::string s ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char> ());
	if (!s.empty () && s[0] == '#') s.erase (0, s.find ('\n') == std::string::npos ? s.size () : s.find ('\n')); // a first "#!" line, as luaL_loadfile skips it
	if (!s.empty () && s[0] == LUA_SIGNATURE[0]) {
		lua_pushnil (L);
		lua_pushfstring (L, "%s: precompiled chunks are refused%s", name, confine::Hint);
		return 2;
	}
	std::string chunk = std::string ("@") + name;
	if (luaL_loadbuffer (L, s.data (), s.size (), chunk.c_str ())) {
		lua_pushnil (L);
		lua_insert (L, -2);
		return 2;
	}
	return 1;
}

int Loadfile (lua_State *L)
{
	const char *name = luaL_checkstring (L, 1); // no name: stdin, refused
	return LoadSource (L, name);
}

int Dofile (lua_State *L)
{
	const char *name = luaL_checkstring (L, 1);
	lua_settop (L, 1);
	if (LoadSource (L, name) != 1) return lua_error (L);
	lua_call (L, 0, LUA_MULTRET);
	return lua_gettop (L) - 1;
}

int Loadstring (lua_State *L)
{
	size_t n;
	const char *s = luaL_checklstring (L, 1, &n);
	const char *chunk = luaL_optstring (L, 2, s);
	if (n && s[0] == LUA_SIGNATURE[0]) {
		lua_pushnil (L);
		lua_pushfstring (L, "loadstring: precompiled chunks are refused%s", confine::Hint);
		return 2;
	}
	if (luaL_loadbuffer (L, s, n, chunk)) {
		lua_pushnil (L);
		lua_insert (L, -2);
		return 2;
	}
	return 1;
}

int Load (lua_State *L) // load (reader [, chunkname]): the pieces joined, then as loadstring
{
	luaL_checktype (L, 1, LUA_TFUNCTION);
	const char *chunk = luaL_optstring (L, 2, "=(load)");
	std::string s;
	for (;;) {
		lua_pushvalue (L, 1);
		if (lua_pcall (L, 0, 1, 0)) { lua_pushnil (L); lua_insert (L, -2); return 2; }
		if (lua_isnil (L, -1)) { lua_pop (L, 1); break; }
		if (!lua_isstring (L, -1)) { lua_pushnil (L); lua_pushliteral (L, "reader function must return a string"); return 2; }
		size_t n;
		const char *p = lua_tolstring (L, -1, &n);
		if (!n) { lua_pop (L, 1); break; }
		s.append (p, n);
		lua_pop (L, 1);
	}
	if (!s.empty () && s[0] == LUA_SIGNATURE[0]) {
		lua_pushnil (L);
		lua_pushfstring (L, "load: precompiled chunks are refused%s", confine::Hint);
		return 2;
	}
	if (luaL_loadbuffer (L, s.data (), s.size (), chunk)) {
		lua_pushnil (L);
		lua_insert (L, -2);
		return 2;
	}
	return 1;
}

// lfs, once required: chdir gone (Orbiter's paths are relative to its folder), the path functions confined
void ConfineLfs (lua_State *L)
{
	if (!lua_istable (L, -1)) return;
	Wrap (L, "lfs", "chdir", 0);
	static const char *const one[] = {"attributes", "symlinkattributes", "dir", "mkdir", "rmdir", "touch", "lock_dir"};
	for (const char *n : one) Wrap (L, "lfs", n, 1);
	Wrap (L, "lfs", "link", 3);
}

// upvalue 1: the C loader the stock searcher found for "lfs"
int LfsLoader (lua_State *L)
{
	lua_CFunction f = Fn (lua_touserdata (L, lua_upvalueindex (1)));
	int n = f (L);
	if (n > 0) {
		lua_pushvalue (L, -n);
		ConfineLfs (L);
		lua_pop (L, 1);
	}
	lua_getglobal (L, "lfs"); // luaL_register's table too
	ConfineLfs (L);
	lua_pop (L, 1);
	return n;
}

// package.path/cpath's files for module mod, Orbiter's names resolved, only those inside the folder
std::string Candidates (lua_State *L, const char *pname, std::string mod, bool root, std::string &tried) // tried: the files left out
{
	if (root) mod = mod.substr (0, mod.find ('.'));
	for (char &c : mod) if (c == '.') c = LUA_DIRSEP[0];
	lua_getfield (L, LUA_ENVIRONINDEX, pname);
	std::string tmpl = lua_isstring (L, -1) ? lua_tostring (L, -1) : "", list;
	lua_pop (L, 1);
	for (size_t i = 0, j; i <= tmpl.size (); i = j + 1) {
		if ((j = tmpl.find (LUA_PATHSEP[0], i)) == std::string::npos) j = tmpl.size ();
		std::string f = tmpl.substr (i, j - i);
		if (f.empty ()) continue;
		for (size_t k = 0; (k = f.find (LUA_PATH_MARK[0], k)) != std::string::npos; k += mod.size ())
			f.replace (k, 1, mod);
		f = oapiResolvePath (f.c_str ());
		if (!confine::Inside (f.c_str ())) { tried += "\n\toutside the Orbiter folder: '" + f + "'"; continue; }
		if (!list.empty ()) list += LUA_PATHSEP;
		list += f;
	}
	return list;
}

bool BadName (lua_State *L, const char *mod) // ';' or '?' would make template entries of their own
{
	return strpbrk (mod, LUA_PATHSEP LUA_PATH_MARK) != nullptr;
}

int LuaSearcher (lua_State *L) // require's Lua files, read here: inside the folder, never bytecode
{
	const char *mod = luaL_checkstring (L, 1);
	if (BadName (L, mod)) { lua_pushfstring (L, "\n\tmodule name '%s' has ';' or '?'", mod); return 1; }
	std::string tried, list = Candidates (L, "path", mod, false, tried);
	for (size_t i = 0, j; i < list.size (); i = j + 1) {
		if ((j = list.find (LUA_PATHSEP[0], i)) == std::string::npos) j = list.size ();
		std::string f = list.substr (i, j - i);
		std::ifstream test (f);
		if (!test) { tried += "\n\tno file '" + f + "'"; continue; }
		if (LoadSource (L, f.c_str ()) != 1)
			return luaL_error (L, "error loading module '%s' from file '%s':\n\t%s", mod, f.c_str (), lua_tostring (L, -1));
		return 1;
	}
	lua_pushstring (L, tried.c_str ());
	return 1;
}

// upvalues: 1 the stock package.loadlib, 2 root flag (loader_Croot): Lua's loader_C and loader_Croot over the files inside the folder
int CSearcher (lua_State *L)
{
	const std::string mod = luaL_checkstring (L, 1);
	if (BadName (L, mod.c_str ())) { lua_pushfstring (L, "\n\tmodule name '%s' has ';' or '?'", mod.c_str ()); return 1; }
	bool root = lua_toboolean (L, lua_upvalueindex (2)) != 0;
	if (root && mod.find ('.') == std::string::npos) return 0; // loader_Croot: a submodule only
	std::string tried, list = Candidates (L, "cpath", mod, root, tried);
	size_t ig = mod.find (LUA_IGMARK[0]);
	std::string fn = "luaopen_" + (ig == std::string::npos ? mod : mod.substr (ig + 1));
	for (size_t k = 8; k < fn.size (); k++) if (fn[k] == '.') fn[k] = '_'; // loadlib.c's LUA_OFSEP
	lua_CFunction loadlib = Fn (lua_touserdata (L, lua_upvalueindex (1)));
	for (size_t i = 0, j; i < list.size (); i = j + 1) {
		if ((j = list.find (LUA_PATHSEP[0], i)) == std::string::npos) j = list.size ();
		std::string f = list.substr (i, j - i);
		std::ifstream test (f);
		if (!test) { tried += "\n\tno file '" + f + "'"; continue; }
		lua_settop (L, 0); // loadlib's arguments: the file and the function's name
		lua_pushstring (L, f.c_str ());
		lua_pushstring (L, fn.c_str ());
		int n = loadlib (L); // by its pointer: no Lua value of it exists
		if (n == 1 && lua_iscfunction (L, -1)) {
			if (mod == "lfs") {
				lua_pushlightuserdata (L, Ptr (lua_tocfunction (L, -1)));
				lua_pushcclosure (L, LfsLoader, 1);
			}
			return 1;
		}
		if (root && n >= 3 && lua_isstring (L, -1) && !strcmp (lua_tostring (L, -1), "init")) {
			lua_pushfstring (L, "\n\tno module '%s' in file '%s'", mod.c_str (), f.c_str ());
			return 1;
		}
		return luaL_error (L, "error loading module '%s' from file '%s':\n\t%s", mod.c_str (), f.c_str (), n >= 2 ? lua_tostring (L, -n + 1) : "?");
	}
	lua_pushstring (L, tried.c_str ());
	return 1;
}
}

bool confine::Inside (const char *path)
{
	if (!path || !*path) return false;
	const fs::path &r = Root ();
	if (r.empty ()) return false;
	std::error_code ec;
	fs::path a = fs::absolute (fs::path (path), ec);
	if (ec) return false;
	fs::path c = fs::weakly_canonical (a, ec);
	if (ec) return false;
	auto ci = c.begin ();
	for (auto ri = r.begin (); ri != r.end (); ++ri, ++ci)
		if (ci == c.end () || *ri != *ci) return false;
	return true;
}

bool confine::Relative (const char *name)
{
	if (!name || name[0] == '/' || name[0] == '\\') return false;
	std::string s (name);
	for (char &c : s) if (c == '\\') c = '/';
	for (size_t i = 0, j; i <= s.size (); i = j + 1) {
		if ((j = s.find ('/', i)) == std::string::npos) j = s.size ();
		if (s.compare (i, j - i, "..") == 0) return false;
	}
	return true;
}

bool confine::Active (lua_State *L)
{
	lua_getfield (L, LUA_REGISTRYINDEX, KEY);
	bool on = lua_toboolean (L, -1) != 0;
	lua_pop (L, 1);
	return on;
}

void confine::Apply (lua_State *L)
{
	lua_pushboolean (L, 1);
	lua_setfield (L, LUA_REGISTRYINDEX, KEY);

	lua_getglobal (L, "os");
	Wrap (L, "os", "execute", 0);
	Wrap (L, "os", "exit", 0);
	Wrap (L, "os", "tmpname", 0);
	Wrap (L, "os", "remove", 1);
	Wrap (L, "os", "rename", 3);
	Wrap (L, "os", "setlocale", -1);
	lua_pop (L, 1);

	lua_getglobal (L, "io");
	Wrap (L, "io", "popen", 0);
	Wrap (L, "io", "open", 1);
	Wrap (L, "io", "lines", 1);
	Wrap (L, "io", "input", 1);
	Wrap (L, "io", "output", 1);
	lua_pop (L, 1);

	lua_getglobal (L, "debug"); // these reach the wrappers' upvalues or a running C call's arguments
	for (const char *n : {"getupvalue", "setupvalue", "getlocal", "setlocal", "getregistry", "sethook"}) Wrap (L, "debug", n, 0); // sethook: the termination hook stays
	Wrap (L, "debug", "setmetatable", -2);
	Wrap (L, "debug", "setfenv", -2);
	lua_pop (L, 1);

	static const struct { const char *name; lua_CFunction f; } base[] = {{"dofile", Dofile}, {"loadfile", Loadfile}, {"loadstring", Loadstring}, {"load", Load}};
	for (auto &b : base) {
		lua_pushcfunction (L, b.f);
		lua_setglobal (L, b.name);
	}

	lua_getglobal (L, "package");
	lua_getfield (L, -1, "loadlib");
	lua_CFunction loadlib = lua_tocfunction (L, -1);
	lua_pop (L, 1);
	lua_pushnil (L);
	lua_setfield (L, -2, "loadlib");
	lua_getfield (L, -1, "loaders");
	lua_pushcfunction (L, LuaSearcher); // a C function's environment is the global table: the package table is set below
	lua_pushvalue (L, -3);
	lua_setfenv (L, -2);
	lua_rawseti (L, -2, 2);
	for (int i = 3; i <= 4 && loadlib; i++) {
		lua_pushlightuserdata (L, Ptr (loadlib));
		lua_pushboolean (L, i == 4);
		lua_pushcclosure (L, CSearcher, 2);
		lua_pushvalue (L, -3);
		lua_setfenv (L, -2);
		lua_rawseti (L, -2, i);
	}
	lua_pop (L, 2);
}
