// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ==============================================================
//           ORBITER MODULE: LUA Inline Interpreter
//                  Part of the ORBITER SDK
//            Copyright (C) 2007-2026 Martin Schweiger
//                   All rights reserved
//
// LuaInline.h
// This library is loaded by the Orbiter core on demand to provide
// interpreter instances to modules via API requests.
//
// Notes:
// * LuaInline.dll must be placed in the Orbiter root directory
//   (not in the Modules subdirectory). It is loaded automatically
//   by Orbiter when required. It must not be loaded manually via
//   the Launchpad "Modules" tab.
// * LuaInline.dll depends on LuaInterpreter.dll and Lua5.1.dll
//   which must also be present in the Orbiter root directory.
// ==============================================================

#ifndef __LUAINLINE_H
#define __LUAINLINE_H

#include "Interpreter.h"
#include <atomic>
#include <memory>
#include <thread>
#include <future>

// ==============================================================
// class InterpreterList: interface

class InterpreterList: public oapi::Module {
public:
	struct Environment {    // interpreter environment
		struct Run {          // not upstream: what the thread uses, kept by it for as long as it runs (it frees the interpreter last)
			Interpreter *interp = nullptr;
			std::atomic<bool> termInterp {false};
			bool singleCmd = false;
			char *cmd = nullptr;
			~Run () { delete interp; delete []cmd; }
		};
		Environment();
		~Environment();
		Interpreter *CreateInterpreter ();
		std::shared_ptr<Run> run; // not upstream
		Interpreter *interp;  // interpreter instance
		std::thread *hThread; // interpreter thread
		std::future<unsigned int> thExit; // not upstream: thread end, for the timed wait on the thread
		std::atomic<bool> &termInterp; // interpreter kill flag (not upstream: run's)
		bool &singleCmd;      // terminate after single command (not upstream: run's)
		char *&cmd;           // interpreter command (not upstream: run's)
		static unsigned int InterpreterThreadProc (std::shared_ptr<Run> run);
	};

	InterpreterList (void *hDLL);
	~InterpreterList ();

	void clbkSimulationEnd () override;
	void clbkPostStep (double simt, double simdt, double mjd) override;
	void clbkSimulationStart (RenderMode mode) override;
	void clbkDeleteVessel (OBJHANDLE hVessel) override;
	
	Environment *AddInterpreter ();
	int DelInterpreter (Environment *env);

private:

	Environment **list;     // interpreter list
	DWORD nlist;            // list size
	DWORD nbuf;             // buffer size
};

#endif // !__LUAINLINE_H
