// WineBridge: OrbiterSound.dll for the Windows modules. The OrbiterSound SDK linked into each module finds it with
// GetModuleHandleA ("OrbiterSound.dll") and takes these entry points by name (all __cdecl, as OrbiterSound's own); the
// helper sends them to Linux, where XRSound plays. The SDK's wrappers check the ID and wave ranges before calling.
#define WB_OSOUND_DLL
#include "osound.h"

#define OS_API extern "C" __declspec(dllexport)

extern "C" int _fltused = 0;

OS_API int __cdecl GetVesselSoundID3 (void *hVessel) { return WbOsSoundID (hVessel); }
OS_API int __cdecl LoadVesselWaveOBSound3 (int id, int wav, const char *file, int extended) { return WbOsLoad (id, wav, file, extended); }

// OrbiterSound 4.0's 3D: XRSound has none, so a 3D wave is a plain one (as the native ports load it)
OS_API int __cdecl LoadVesselWave3dOBSound4 (int id, int wav, const char *file, int extended, void *, void *, int)
{
	return WbOsLoad (id, wav, file, extended);
}
OS_API int __cdecl Set3dWaveParametersOBSound4 (int id, int wav, void *, void *, void *, void *, void *) { return WbOsSet3d (id, wav); }
OS_API int __cdecl IsOrbiterSound3dOBSound4 () { return 0; }

OS_API int __cdecl PlayVesselWaveOBSound3 (int id, int wav, int loop, int volume, int frequency)
{
	return WbOsPlay (id, wav, loop, volume, frequency);
}
// once, on OrbiterSound's one radio channel (OrbiterSound ignores the loop flag here)
OS_API int __cdecl PlayVesselExclusiveWaveOBSound3 (int id, int wav, int, int volume, int frequency)
{
	return WbOsPlayRadio (id, wav, volume, frequency);
}
OS_API int __cdecl StopVesselWave3 (int id, int wav) { return WbOsStop (id, wav); }
OS_API int __cdecl IsPlayingVesselWave3 (int id, int wav) { return WbOsIsPlaying (id, wav); }
OS_API int __cdecl IsPlayingRadioVesselWave () { return WbOsIsPlayingRadio (); }
OS_API int __cdecl SoundOption3 (int id, int option, int onoff) { return WbOsOption (id, option, onoff); }
OS_API int __cdecl SetFrequency (const char *) { return WbOsSetFrequency (); } // the radio's frequency text
OS_API int __cdecl ReplaceStockOBSound3 (int id, const char *file, int which) { return WbOsReplace (id, file, which); }
OS_API float __cdecl GetVersionOBSound3 () { return 4.0f; } // the SDK level served (no 3D), as the native ports report

// the MFD part of the SDK (ConnectMFDToOrbiterSound and its wave calls)
OS_API int __cdecl GetMFDId (const char *name) { return WbOsMfdID (name); }
OS_API int __cdecl LoadMFDWaveOBSound3 (int id, int wav, const char *file, int) { return WbOsMfdLoad (id, wav, file); }
OS_API int __cdecl PlayMFDWaveOBSound3 (int id, int wav, int loop, int volume, int frequency)
{
	return WbOsMfdPlay (id, wav, loop, volume, frequency);
}
OS_API int __cdecl StopMFDWave3 (int id, int wav) { return WbOsMfdStop (id, wav); }
