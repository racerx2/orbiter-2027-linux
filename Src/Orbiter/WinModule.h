// not upstream (dll-modules): Windows modules (.dll) opened through the Wine bridge (Modules/WineBridge.so)
#pragma once
#include <filesystem>
#include <string>

// the Windows module a module path stands for: the path when it names one, or for a .so that isn't there the .dll beside it; "" if none
std::string WinModulePath (const char *path);

// LoadLibrary through the bridge: null when it can't be loaded (logged)
void *WinModuleLoad (const char *dllpath);

bool WinModuleHandle (void *hModule);
void *WinModuleProc (void *hModule, const char *name);
void WinModuleFree (void *hModule);
const char *WinModuleFileName (void *hModule);

// a folder entry that is a Windows module no .so beside it has the name of
bool WinModuleEntry (const std::filesystem::path &file);
