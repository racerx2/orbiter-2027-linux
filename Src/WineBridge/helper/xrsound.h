// WineBridge helper: the calls XRSound.dll (xrsdll.cpp) makes into the helper (xrsound.cpp), exported by Orbiter.exe
#pragma once

#ifdef WB_XRS_DLL
#define WB_XRS_API extern "C" __declspec(dllimport)
#else
#define WB_XRS_API extern "C" __declspec(dllexport)
#endif

WB_XRS_API void *__cdecl WbXrsVessel (void *hVessel);     // the engine of a vessel (Orbiter's handle), 0 if none
WB_XRS_API void *__cdecl WbXrsModule (const char *name);  // ... of a module, by its unique name
