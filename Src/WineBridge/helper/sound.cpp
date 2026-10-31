// WineBridge helper: OrbiterSound.dll's calls (osound.h), sent to Linux (linux/Sound.cpp). An empty reply (no
// bridge) reads as failure.
#include "wb_helper.h"
#include "osound.h"

static int Bool (WbReader r) { bool v = r.b (); return !r.bad && v; }
static int Id (WbReader r) { int v = r.i32 (); return r.bad ? -1 : v; }

WB_OS_API int __cdecl WbOsSoundID (void *hVessel)
{
	WbWriter &w = wb::Begin (L_OsSoundID); w.u32 (wb::Id (hVessel));
	return Id (wb::Call ());
}

WB_OS_API int __cdecl WbOsLoad (int id, int wav, const char *file, int extended)
{
	WbWriter &w = wb::Begin (L_OsLoad); w.i32 (id); w.i32 (wav); w.str (file); w.i32 (extended);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsSet3d (int id, int wav)
{
	WbWriter &w = wb::Begin (L_OsSet3d); w.i32 (id); w.i32 (wav);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsPlay (int id, int wav, int loop, int volume, int frequency)
{
	WbWriter &w = wb::Begin (L_OsPlay); w.i32 (id); w.i32 (wav); w.i32 (loop); w.i32 (volume); w.i32 (frequency);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsPlayRadio (int id, int wav, int volume, int frequency)
{
	WbWriter &w = wb::Begin (L_OsPlayRadio); w.i32 (id); w.i32 (wav); w.i32 (volume); w.i32 (frequency);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsStop (int id, int wav)
{
	WbWriter &w = wb::Begin (L_OsStop); w.i32 (id); w.i32 (wav);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsIsPlaying (int id, int wav)
{
	WbWriter &w = wb::Begin (L_OsIsPlaying); w.i32 (id); w.i32 (wav);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsIsPlayingRadio ()
{
	wb::Begin (L_OsIsPlayingRadio);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsOption (int id, int option, int onoff)
{
	WbWriter &w = wb::Begin (L_OsOption); w.i32 (id); w.i32 (option); w.i32 (onoff);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsSetFrequency ()
{
	wb::Begin (L_OsSetFrequency);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsReplace (int id, const char *file, int which)
{
	WbWriter &w = wb::Begin (L_OsReplace); w.i32 (id); w.str (file); w.i32 (which);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsMfdID (const char *name)
{
	WbWriter &w = wb::Begin (L_OsMfdID); w.str (name);
	return Id (wb::Call ());
}

WB_OS_API int __cdecl WbOsMfdLoad (int id, int wav, const char *file)
{
	WbWriter &w = wb::Begin (L_OsMfdLoad); w.i32 (id); w.i32 (wav); w.str (file);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsMfdPlay (int id, int wav, int loop, int volume, int frequency)
{
	WbWriter &w = wb::Begin (L_OsMfdPlay); w.i32 (id); w.i32 (wav); w.i32 (loop); w.i32 (volume); w.i32 (frequency);
	return Bool (wb::Call ());
}

WB_OS_API int __cdecl WbOsMfdStop (int id, int wav)
{
	WbWriter &w = wb::Begin (L_OsMfdStop); w.i32 (id); w.i32 (wav);
	return Bool (wb::Call ());
}
