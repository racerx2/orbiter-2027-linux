// WineBridge helper: the calls OrbiterSound.dll (osound.cpp) makes into the helper (sound.cpp), exported by Orbiter.exe
#pragma once

#ifdef WB_OSOUND_DLL
#define WB_OS_API extern "C" __declspec(dllimport)
#else
#define WB_OS_API extern "C" __declspec(dllexport)
#endif

WB_OS_API int __cdecl WbOsSoundID (void *hVessel);
WB_OS_API int __cdecl WbOsLoad (int id, int wav, const char *file, int extended);
WB_OS_API int __cdecl WbOsSet3d (int id, int wav);
WB_OS_API int __cdecl WbOsPlay (int id, int wav, int loop, int volume, int frequency);
WB_OS_API int __cdecl WbOsPlayRadio (int id, int wav, int volume, int frequency);
WB_OS_API int __cdecl WbOsStop (int id, int wav);
WB_OS_API int __cdecl WbOsIsPlaying (int id, int wav);
WB_OS_API int __cdecl WbOsIsPlayingRadio ();
WB_OS_API int __cdecl WbOsOption (int id, int option, int onoff);
WB_OS_API int __cdecl WbOsSetFrequency ();
WB_OS_API int __cdecl WbOsReplace (int id, const char *file, int which);
WB_OS_API int __cdecl WbOsMfdID (const char *name);
WB_OS_API int __cdecl WbOsMfdLoad (int id, int wav, const char *file);
WB_OS_API int __cdecl WbOsMfdPlay (int id, int wav, int loop, int volume, int frequency);
WB_OS_API int __cdecl WbOsMfdStop (int id, int wav);
