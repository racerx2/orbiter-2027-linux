// not upstream: content scripts confined to the Orbiter folder (design-security-fixes B1)
#pragma once
#include <string>

struct lua_State;

namespace confine {
	void Apply (lua_State *L);           // the state's libraries, in place: no programs, files only under the Orbiter folder, no bytecode
	bool Active (lua_State *L);          // Apply ran on this state
	bool Inside (const char *path);      // the path's canonical form (symlinks resolved) lies under the Orbiter folder
	bool Relative (const char *name);    // a name that stays in its folder: not absolute, no ".." part
	extern const char *const Hint;       // what to tell the user about a refused call
}
