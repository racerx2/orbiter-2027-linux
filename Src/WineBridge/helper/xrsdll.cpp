// WineBridge: XRSound.dll for the Windows modules. The XRSound SDK linked into each module finds it with GetModuleHandle ("XRSound.dll")
// and takes these two entry points by name; the engines they give are the helper's, whose methods go to Linux's XRSound
#define WB_XRS_DLL
#include "xrsound.h"

#define XRS_API extern "C" __declspec(dllexport)

extern "C" int _fltused = 0;

XRS_API void *__cdecl GetXRSoundEngineInstance (void *hVessel) { return WbXrsVessel (hVessel); }
XRS_API void *__cdecl GetModuleXRSoundEngineInstance (const char *pUniqueModuleName) { return WbXrsModule (pUniqueModuleName); }
