// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ========================================================================
// To be linked into all Orbiter addon modules.
// Contains standard module entry point and version information.
// ========================================================================

#include <dlfcn.h>
#include <fstream>
#include <stdio.h>

#define DLLCLBK extern "C" __attribute__((visibility("default")))
#define OAPIFUNC __attribute__((visibility("default")))

// DllMain counterpart: ELF constructor/destructor of the module (Windows calls DllMain only for DLLs, so the exe is skipped)
OAPIFUNC void InitLib (void *hModule);
typedef void (*DLLEXIT)(void*);
static DLLEXIT DLLExit;
static void *hThisModule;

// GetProcAddress counterpart: dlsym also searches the module's dependencies, so keep only the module's own symbol
static void *OwnProc (void *hModule, const char *name, const void *base)
{
	void *proc = dlsym (hModule, name);
	Dl_info info;
	if (proc && (!dladdr (proc, &info) || info.dli_fbase != base)) proc = 0;
	return proc;
}

__attribute__((constructor)) static void DllMain_ProcessAttach ()
{
	Dl_info self, core;
	if (!dladdr ((void*)&DllMain_ProcessAttach, &self) || !dladdr ((void*)&InitLib, &core)) return;
	if (self.dli_fbase == core.dli_fbase) return; // linked into the Orbiter executable itself
	hThisModule = dlopen (self.dli_fname, RTLD_NOW | RTLD_NOLOAD); // same handle the loader's dlopen returns
	if (!hThisModule) return;
	dlclose (hThisModule); // drop the extra reference; the loader's one keeps the module mapped
	InitLib (hThisModule);
	DLLExit = (DLLEXIT)OwnProc (hThisModule, "ExitModule", self.dli_fbase);
	if (!DLLExit) DLLExit = (DLLEXIT)OwnProc (hThisModule, "opcDLLExit", self.dli_fbase);
}

// not upstream: DLL_PROCESS_DETACH as FreeLibrary sends it; the core calls this before dlclose, which may keep the module loaded until exit
DLLCLBK void ModuleDetach ()
{
	DLLEXIT f = DLLExit;
	DLLExit = 0;
	if (f) (*f)(hThisModule);
}

__attribute__((destructor)) static void DllMain_ProcessDetach ()
{
	ModuleDetach ();
}

int oapiGetModuleVersion ()
{
	static int v = 0;
	if (!v) {
		OAPIFUNC int Date2Int (char *date);
		v = Date2Int ((char*)__DATE__);
	}
	return v;
}

DLLCLBK int GetModuleVersion (void)
{
	return oapiGetModuleVersion();
}

void dummy () {}
