// not upstream (dll-modules): Windows modules (.dll) opened through the Wine bridge (Modules/WineBridge.so)
#pragma once
#include "OrbiterAPI.h"
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

// a folder entry that is a Windows module no .so beside it has the name of; exported for the plugins that list modules (AtmConfig)
DLLEXPORT bool WinModuleEntry (const std::filesystem::path &file);

// not upstream (vessel-hooks): the bridge takes a vessel's callbacks where a Windows module hooked them in its object
class VESSEL;
VESSEL *WinModuleVesselIntf (void *hVessel, VESSEL *own); // the object Orbiter calls the callbacks on: own when the bridge has none
void WinModuleVesselNew (void *hVessel, VESSEL *own); // the vessel's module object is made
void WinModuleVesselGone (void *hVessel); // the vessel is being destroyed

// not upstream (dll-modules): the camera that held this external camera control is destroyed; the bridge drops its proxy
class ExternalCameraControl;
void WinModuleCameraControlGone (ExternalCameraControl *ecc);

// not upstream (dll-modules): Orbiter's dialog manager, for the Windows modules' dialogs: made, cleared, deleted, or a message it broadcasts
enum { WMD_DELETED, WMD_MADE, WMD_CLEARED, WMD_MESSAGE };
void WinModuleDialogs (int what, DWORD msg = 0, void *data = 0);
