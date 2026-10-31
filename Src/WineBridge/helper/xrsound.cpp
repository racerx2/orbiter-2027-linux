// WineBridge helper: XRSound's engines for the Windows modules. XRSound.dll (xrsdll.cpp) hands them out; each method goes to the
// Linux engine (L_Xrs, linux/Sound.cpp). An engine is kept for the helper's life, as XRSound.dll keeps its own (a borrowed reference)
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include "wb_helper.h"
#include "xrsound.h"

namespace {

// XRSoundEngine10's, 20's and 30's methods in their order (Sound/XRSound/src/XRSoundEngine10.h, 20.h, 30.h): the XRSound SDK in a
// module calls them through this table. Enums are ints (PlaybackType, DefaultSoundID, EngineType)
class Engine {
public:
	explicit Engine (uint32_t id): m_id (id) { m_folder[0] = m_logid[0] = 0; }
	virtual float GetVersion () const { Begin (XR_GetVersion); return F32 (); }
	virtual bool LoadWav (const int soundID, const char *pSoundFilename, const int playbackType)
	{
		WbWriter &w = Begin (XR_LoadWav); w.i32 (soundID); w.str (pSoundFilename); w.i32 (playbackType); return B ();
	}
	virtual bool PlayWav (const int soundID, const bool bLoop, const float volume)
	{
		WbWriter &w = Begin (XR_PlayWav); w.i32 (soundID); w.b (bLoop); w.f64 (volume); return B ();
	}
	virtual bool StopWav (const int soundID) { Begin (XR_StopWav).i32 (soundID); return B (); }
	virtual bool IsWavPlaying (const int soundID) { Begin (XR_IsWavPlaying).i32 (soundID); return B (); }
	virtual bool SetPaused (const int soundID, const bool bPause) { WbWriter &w = Begin (XR_SetPaused); w.i32 (soundID); w.b (bPause); return B (); }
	virtual bool IsPaused (const int soundID) { Begin (XR_IsPaused).i32 (soundID); return B (); }
	virtual bool SetDefaultSoundEnabled (const int soundID, const bool bEnabled)
	{
		WbWriter &w = Begin (XR_SetDefaultSoundEnabled); w.i32 (soundID); w.b (bEnabled); return B ();
	}
	virtual bool GetDefaultSoundEnabled (const int soundID) { Begin (XR_GetDefaultSoundEnabled).i32 (soundID); return B (); }
	virtual bool SetDefaultSoundGroupFolder (const int groupSoundID, const char *pSubfolderPath)
	{
		WbWriter &w = Begin (XR_SetDefaultSoundGroupFolder); w.i32 (groupSoundID); w.str (pSubfolderPath); return B ();
	}
	virtual const char *GetDefaultSoundGroupFolder (const int groupSoundID) const
	{
		Begin (XR_GetDefaultSoundGroupFolder).i32 (groupSoundID);
		return Str (m_folder, sizeof m_folder);
	}
	virtual bool IsDefaultSound (const int soundID) const { Begin (XR_IsDefaultSound).i32 (soundID); return B (); }
	virtual bool IsDefaultSoundGroup (const int soundID) const { Begin (XR_IsDefaultSoundGroup).i32 (soundID); return B (); }
	virtual bool IsDefaultSoundOrGroup (const int soundID) const { Begin (XR_IsDefaultSoundOrGroup).i32 (soundID); return B (); }
	virtual int GetEngineType ()
	{
		Begin (XR_GetEngineType);
		WbReader r = wb::Call ();
		int t = r.i32 ();
		return r.bad ? -1 : t; // Unknown
	}
	virtual const char *GetLogID () { Begin (XR_GetLogID); return Str (m_logid, sizeof m_logid); }
	virtual bool SetPan (const int soundID, const float pan) { WbWriter &w = Begin (XR_SetPan); w.i32 (soundID); w.f64 (pan); return B (); }
	virtual float GetPan (const int soundID) { Begin (XR_GetPan).i32 (soundID); return F32 (); }
	virtual bool SetPlaybackSpeed (const int soundID, const float speed)
	{
		WbWriter &w = Begin (XR_SetPlaybackSpeed); w.i32 (soundID); w.f64 (speed); return B ();
	}
	virtual float GetPlaybackSpeed (const int soundID) { Begin (XR_GetPlaybackSpeed).i32 (soundID); return F32 (); }
	virtual bool SetPlayPosition (const int soundID, const unsigned int positionMillis)
	{
		WbWriter &w = Begin (XR_SetPlayPosition); w.i32 (soundID); w.u32 (positionMillis); return B ();
	}
	virtual int GetPlayPosition (const int soundID)
	{
		Begin (XR_GetPlayPosition).i32 (soundID);
		WbReader r = wb::Call ();
		int p = r.i32 ();
		return r.bad ? 0 : p;
	}

	uint32_t Id () const { return m_id; }

private:
	uint32_t m_id;
	mutable char m_folder[MAX_PATH];  // GetDefaultSoundGroupFolder's text, until its next call
	char m_logid[256];                // GetLogID's

	WbWriter &Begin (int method) const
	{
		WbWriter &w = wb::Begin (L_Xrs);
		w.u32 (m_id); w.i32 (method);
		return w;
	}
	static bool B ()
	{
		WbReader r = wb::Call ();
		bool v = r.b ();
		return v && !r.bad;
	}
	static float F32 ()
	{
		WbReader r = wb::Call ();
		double v = r.f64 ();
		return r.bad ? 0.0f : (float)v;
	}
	static const char *Str (char *buf, int n)
	{
		WbReader r = wb::Call ();
		if (!r.b () || r.bad) return 0;
		wb::OutStr (r, buf, n);
		return r.bad ? 0 : buf;
	}
};

Engine **g_engine;
int g_nengine, g_capengine;

// the helper's engine for Linux's engine of that id (0: none)
void *EngineFor (WbReader r)
{
	uint32_t id = r.u32 ();
	if (r.bad || !id) return 0;
	for (int i = 0; i < g_nengine; i++) if (g_engine[i]->Id () == id) return g_engine[i];
	if (g_nengine == g_capengine) {
		g_capengine = g_capengine ? g_capengine * 2 : 16;
		g_engine = (Engine **)realloc (g_engine, g_capengine * sizeof *g_engine);
	}
	return g_engine[g_nengine++] = new Engine (id);
}

} // namespace

WB_XRS_API void *__cdecl WbXrsVessel (void *hVessel)
{
	WbWriter &w = wb::Begin (L_XrsEngine); w.b (true); w.u32 (wb::Id (hVessel));
	return EngineFor (wb::Call ());
}

WB_XRS_API void *__cdecl WbXrsModule (const char *name)
{
	WbWriter &w = wb::Begin (L_XrsEngine); w.b (false); w.str (name);
	return EngineFor (wb::Call ());
}
