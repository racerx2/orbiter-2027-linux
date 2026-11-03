// Copyright (c) Martin Schweiger
// Licensed under the MIT License

#ifndef __LUACONSOLE_H
#define __LUACONSOLE_H

#include "OrbiterAPI.h"
#include "ModuleAPI.h"
#include "ConsoleInterpreter.h"
#include <atomic>
#include <memory>
#include <thread>
#include <future>

#define NLINE 100 // number of buffered lines

class LuaConsoleDlg;
enum class LineType {
	LUA_IN,
	LUA_OUT,
	LUA_OUT_ERROR,
};

class LuaConsole: public oapi::Module {
	friend class ConsoleInterpreter;
	friend class ConsoleConfig;

public:
	LuaConsole (void *hDLL);
	~LuaConsole ();

	void clbkSimulationStart (RenderMode mode);
	void clbkSimulationEnd ();
	void clbkPreStep (double simt, double simdt, double mjd);
	void clbkDeleteVessel (OBJHANDLE hVessel) override { Interpreter::DeleteVessel (hVessel); } // not upstream: the console's interpreter forgets the vessel too, LuaInline may not be loaded

	QWidget *Open ();
	void Close ();

	void AddLine(const char *str, LineType type = LineType::LUA_OUT);
	void Clear();

private:
	struct Run { // not upstream: what the thread uses, kept by it for as long as it runs (it frees the interpreter last)
		Interpreter *interp = nullptr;
		std::atomic<bool> termInterp {false};
		char cmd[4096] = "";
		~Run () { delete interp; }
	};
	static unsigned int InterpreterThreadProc (std::shared_ptr<Run> run);
	void Unlink (); // not upstream: the thread's interpreter no longer reaches this console
	std::shared_ptr<Run> run;
	std::shared_ptr<ConsoleLink> link;
	static void OpenDlgClbk (void *context); // called when user requests console window
	Interpreter *CreateInterpreter ();
	std::thread *hThread; // interpreter thread handle
	std::future<unsigned int> thExit; // not upstream: thread end, for the timed wait on the thread

	Interpreter *interp; // interpreter instance
	DWORD dwCmd;    // custom command id
	int dwMenuCmd;    // custom command id
	LuaConsoleDlg *hDlg;
	char cConsoleCmd[4096];
};

#endif // !__LUA_CONSOLE_H
