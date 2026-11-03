// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __CONSOLEINTERPRETER_H
#define __CONSOLEINTERPRETER_H

#include "Interpreter.h"
#include <memory>
#include <mutex>

class LuaConsole; // g++ doesn't take the friend declaration below as a declaration (MSVC does)

// not upstream: the console as the interpreter's thread reaches it; the console clears it under the lock before it goes
struct ConsoleLink {
	std::mutex m;
	LuaConsole *console;
};

// ==============================================================
// class ConsoleInterpreter

class ConsoleInterpreter: public Interpreter {
	friend class LuaConsole;
public:
	ConsoleInterpreter (std::shared_ptr<ConsoleLink> _link); // not upstream: the link, not the console itself
	void LoadAPI();
	void term_strout (const char *str, bool iserr=false);
	void term_clear ();

protected:
	static int termOut (lua_State *L);
	static int termLineUp (lua_State *L);
	static int termSetVerbosity (lua_State *L);
	static int termClear (lua_State *L);

private:
	std::shared_ptr<ConsoleLink> link; // not upstream
};

#endif // !__CONSOLEINTERPRETER_H
