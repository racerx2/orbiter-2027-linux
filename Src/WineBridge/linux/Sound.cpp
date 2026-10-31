// WineBridge: OrbiterSound for the Windows modules, on XRSound. The helper's OrbiterSound.dll sends its calls here; the
// mapping is the native ports' (DGIV-linux src/orbitersound.cpp and orbitersound.notes.md). Its XRSound.dll's engines are XRSound's.
#include "Orbitersdk.h"
#include "XRSound.h"
#include "Bridge.h"
#include "Manual.h"
#include <dlfcn.h>
#include <climits>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

// XRSoundEngine30 (Sound/XRSound/src/XRSoundEngine10.h, 20.h, 30.h): XRSound.so's engines, which the XRSound SDK in a Windows
// module calls through the helper's (helper/xrsound.cpp). Outside the anonymous namespace: GCC takes a class there with no
// derived one in this file for the objects' own type, and calls its pure functions
class XrsEngine {
public:
	virtual float GetVersion () const = 0;
	virtual bool LoadWav (int soundID, const char *pSoundFilename, XRSound::PlaybackType playbackType) = 0;
	virtual bool PlayWav (int soundID, bool bLoop, float volume) = 0;
	virtual bool StopWav (int soundID) = 0;
	virtual bool IsWavPlaying (int soundID) = 0;
	virtual bool SetPaused (int soundID, bool bPause) = 0;
	virtual bool IsPaused (int soundID) = 0;
	virtual bool SetDefaultSoundEnabled (XRSound::DefaultSoundID soundID, bool bEnabled) = 0;
	virtual bool GetDefaultSoundEnabled (XRSound::DefaultSoundID soundID) = 0;
	virtual bool SetDefaultSoundGroupFolder (XRSound::DefaultSoundID groupSoundID, const char *pSubfolderPath) = 0;
	virtual const char *GetDefaultSoundGroupFolder (XRSound::DefaultSoundID groupSoundID) const = 0;
	virtual bool IsDefaultSound (int soundID) const = 0;
	virtual bool IsDefaultSoundGroup (int soundID) const = 0;
	virtual bool IsDefaultSoundOrGroup (int soundID) const = 0;
	virtual int GetEngineType () = 0; // XRSoundEngine20::EngineType
	virtual const char *GetLogID () = 0;
	virtual bool SetPan (int soundID, float pan) = 0;
	virtual float GetPan (int soundID) = 0;
	virtual bool SetPlaybackSpeed (int soundID, float speed) = 0;
	virtual float GetPlaybackSpeed (int soundID) = 0;
	virtual bool SetPlayPosition (int soundID, unsigned int positionMillis) = 0;
	virtual int GetPlayPosition (int soundID) = 0;
protected:
	~XrsEngine () = default;
};

namespace {

enum { OS_MAXID = 60, OS_MAXWAVE = 60, OS_MFDMAXID = 40, OS_MFDMAXWAVE = 10 };

struct Wave {
	uint32_t rate = 0;   // the wav file's sample rate (0: unknown, frequency ignored)
	float speed = 1.0f;  // the playback speed last set
};

struct Proxy {
	OBJHANDLE hVessel = nullptr; // a vessel's
	std::string name;            // an MFD's
	XRSound *xr = nullptr;
	Wave wave[OS_MAXWAVE + 1];
};

// plain arrays: nothing to destroy at exit
Proxy *g_vessel[OS_MAXID];   // index: the sound ID; 0 is never handed out
Proxy *g_mfd[OS_MFDMAXID];   // index: the MFD ID
int g_radioId = -1, g_radioWav = 0; // OrbiterSound's one radio channel: the exclusive wave on it

Proxy *VesselProxy (int id) { return (uint32_t)id < OS_MAXID ? g_vessel[id] : nullptr; }
Proxy *MfdProxy (int id) { return (uint32_t)id < OS_MFDMAXID ? g_mfd[id] : nullptr; }
bool VesselWave (int wav) { return (uint32_t)(wav - 1) < OS_MAXWAVE; }
bool MfdWave (int wav) { return (uint32_t)wav < OS_MFDMAXWAVE; }

std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

// a Windows path relative to the Orbiter folder ("Sound\\DeltaGliderIV\\gear.wav"): the on-disk spelling, relative, with '/'
std::string RelSoundPath (const std::string &path)
{
	std::string p = oapiResolvePath (path.c_str ());
	for (char &c : p) if (c == '\\') c = '/';
	if (!p.empty () && p[0] == '/') {
		char cwd[PATH_MAX];
		if (getcwd (cwd, sizeof cwd)) {
			std::string root = cwd;
			if (root.empty () || root.back () != '/') root += '/';
			if (p.compare (0, root.size (), root) == 0) p.erase (0, root.size ());
		}
	}
	while (p.compare (0, 2, "./") == 0) p.erase (0, 2);
	return p;
}

// the sample rate of a RIFF/WAVE file ("fmt " chunk), 0 if it is not one
uint32_t WavSampleRate (const std::string &path)
{
	FILE *f = fopen (path.c_str (), "rb");
	if (!f) return 0;
	uint32_t rate = 0;
	unsigned char hdr[12];
	if (fread (hdr, 1, 12, f) == 12 && !memcmp (hdr, "RIFF", 4) && !memcmp (hdr + 8, "WAVE", 4)) {
		unsigned char ck[8];
		while (fread (ck, 1, 8, f) == 8) {
			uint32_t len = ck[4] | ck[5] << 8 | ck[6] << 16 | (uint32_t)ck[7] << 24;
			if (!memcmp (ck, "fmt ", 4)) {
				unsigned char fmt[8];
				if (len >= 8 && fread (fmt, 1, 8, f) == 8) rate = fmt[4] | fmt[5] << 8 | fmt[6] << 16 | (uint32_t)fmt[7] << 24;
				break;
			}
			if (fseek (f, (long)len + (len & 1), SEEK_CUR) != 0) break;
		}
	}
	fclose (f);
	return rate;
}

// OrbiterSound's EXTENDEDPLAY: XRSound has no external-only type, and its Radio plays at full volume in both views
XRSound::PlaybackType PlaybackFor (int extended)
{
	switch (extended) {
	case 1:  return XRSound::PlaybackType::InternalOnly;   // INTERNAL_ONLY
	case 2:                                                 // BOTHVIEW_FADED_CLOSE
	case 5:  return XRSound::PlaybackType::BothViewClose;  // EXTERNAL_ONLY_FADED_CLOSE
	case 3:                                                 // BOTHVIEW_FADED_MEDIUM
	case 6:  return XRSound::PlaybackType::BothViewMedium; // EXTERNAL_ONLY_FADED_MEDIUM
	case 4:                                                 // BOTHVIEW_FADED_FAR
	case 7:  return XRSound::PlaybackType::BothViewFar;    // EXTERNAL_ONLY_FADED_FAR
	default: return XRSound::PlaybackType::Radio;          // DEFAULT, RADIO_SOUND
	}
}

bool LoadWave (Proxy *p, int wav, const std::string &file, XRSound::PlaybackType type)
{
	Wave &w = p->wave[wav];
	w = Wave ();
	if (file.empty ()) return false;
	std::string rel = RelSoundPath (file);
	if (!p->xr->LoadWav (wav, rel.c_str (), type)) return false;
	w.rate = WavSampleRate (rel);
	return true;
}

// volume 0..255; frequency in Hz (0: the wav's own) as a speed against the wav's rate, clamped to the SDK's range
bool Play (Proxy *p, int wav, bool loop, int volume, int frequency, int lo, int hi)
{
	float vol = volume <= 0 ? 0.0f : volume >= 255 ? 1.0f : (float)volume / 255.0f;
	if (!p->xr->PlayWav (wav, loop, vol)) return false;
	Wave &w = p->wave[wav];
	float speed = 1.0f;
	if (frequency != 0 && w.rate != 0) speed = (float)(frequency < lo ? lo : frequency > hi ? hi : frequency) / (float)w.rate;
	if (speed != 1.0f || w.speed != 1.0f) { // only after PlayWav: XRSound sets the speed of a playing sound
		p->xr->SetPlaybackSpeed (wav, speed);
		w.speed = speed;
	}
	return true;
}

void RadioStop ()
{
	if (Proxy *p = VesselProxy (g_radioId)) p->xr->StopWav (g_radioWav);
	g_radioId = -1;
	g_radioWav = 0;
}

void Drop (Proxy *&p)
{
	delete p->xr;
	delete p;
	p = nullptr;
}

// GetVesselSoundID3: the same vessel, the same ID; -1 when XRSound gives no proxy or 59 vessels have one
int SoundId (OBJHANDLE h)
{
	if (!h || !oapiIsVessel (h)) return -1;
	for (int id = 1; id < OS_MAXID; id++)
		if (g_vessel[id] && g_vessel[id]->hVessel == h) return id;
	int id = 1;
	while (id < OS_MAXID && g_vessel[id]) id++;
	if (id == OS_MAXID) return -1;
	VESSEL *v = oapiGetVesselInterface (h);
	XRSound *xr = v ? XRSound::CreateInstance (v) : nullptr;
	if (!xr || !xr->IsPresent ()) { delete xr; return -1; }
	Proxy *p = new Proxy;
	p->hVessel = h;
	p->xr = xr;
	g_vessel[id] = p;
	return id;
}

// GetMFDId: the same name, the same ID; an XRSound module proxy (module sounds play as PlaybackType::Global)
int MfdId (const std::string &name)
{
	if (name.empty ()) return -1;
	for (int id = 0; id < OS_MFDMAXID; id++)
		if (g_mfd[id] && g_mfd[id]->name == name) return id;
	int id = 0;
	while (id < OS_MFDMAXID && g_mfd[id]) id++;
	if (id == OS_MFDMAXID) return -1;
	XRSound *xr = XRSound::CreateInstance (name.c_str ());
	if (!xr || !xr->IsPresent ()) { delete xr; return -1; }
	Proxy *p = new Proxy;
	p->name = name;
	p->xr = xr;
	g_mfd[id] = p;
	return id;
}

// ReplaceStockOBSound3: the XRSound default sounds with a counterpart (notes: REPLACE_COCKPIT_AMBIENCE_1..9 and the
// RCS mode callouts can't be single waves in XRSound; 27..44 have no counterpart)
bool Replace (int id, const std::string &file, int which)
{
	using X = XRSound;
	static const X::DefaultSoundID mainEng[] = {X::MainEngines}, hover[] = {X::HoverEngines}, sustain[] = {X::RCSSustain},
		aircond[] = {X::AirConditioning}, attack[] = {X::RCSAttackPlusX, X::RCSAttackPlusY, X::RCSAttackPlusZ, X::RCSAttackMinusX,
		X::RCSAttackMinusY, X::RCSAttackMinusZ};
	Proxy *p = VesselProxy (id);
	if (!p || file.empty ()) return false;
	const X::DefaultSoundID *ids;
	size_t n;
	X::PlaybackType type;
	switch (which) {
	case 10: ids = mainEng; n = 1; type = X::PlaybackType::BothViewFar; break;     // REPLACE_MAIN_THRUST
	case 11: ids = hover; n = 1; type = X::PlaybackType::BothViewFar; break;       // REPLACE_HOVER_THRUST
	case 12: ids = attack; n = 6; type = X::PlaybackType::BothViewClose; break;    // REPLACE_RCS_THRUST_ATTACK
	case 13: ids = sustain; n = 1; type = X::PlaybackType::BothViewClose; break;   // REPLACE_RCS_THRUST_SUSTAIN
	case 14: ids = aircond; n = 1; type = X::PlaybackType::InternalOnly; break;    // REPLACE_AIR_CONDITIONNING
	default: return false;
	}
	std::string rel = RelSoundPath (file);
	bool ok = false;
	for (size_t i = 0; i < n; i++) ok |= p->xr->LoadWav (ids[i], rel.c_str (), type);
	return ok;
}

// SoundOption3: the XRSound default sounds with a counterpart; the others are accepted and do nothing
bool Option (int id, int option, int onoff)
{
	using X = XRSound;
	static const X::DefaultSoundID attmode[] = {X::Rotation, X::Translation, X::Off}, gpws[] = {X::AltitudeCalloutsGroup},
		mainEng[] = {X::MainEngines}, hover[] = {X::HoverEngines}, rcs[] = {X::RCSSustain, X::RCSAttackPlusX, X::RCSAttackPlusY,
		X::RCSAttackPlusZ, X::RCSAttackMinusX, X::RCSAttackMinusY, X::RCSAttackMinusZ, X::RCSClosePlusX, X::RCSClosePlusY,
		X::RCSClosePlusZ, X::RCSCloseMinusX, X::RCSCloseMinusY, X::RCSCloseMinusZ}, docking[] = {X::Docking, X::DockingCallout,
		X::Undocking, X::UndockingCallout}, radar[] = {X::DockingRadarBeep}, wind[] = {X::FlightWind},
		clearance[] = {X::YouAreClearedToLand}, ground[] = {X::Touchdown, X::WheelChirp, X::WheelStop, X::TiresRolling,
		X::Wheekbrakes, X::Crash, X::MetalCrunch}, aircond[] = {X::AirConditioning}, ambience[] = {X::CabinAmbienceGroup},
		landedwind[] = {X::LandedWind}, atc[] = {X::RadioATCGroup}, retro[] = {X::RetroEngines}, user[] = {X::CustomEngines},
		reentry[] = {X::ReentryPlasma};
	if ((uint32_t)(option - 1) >= 24) return false;
	Proxy *p = VesselProxy (id);
	if (!p) return false;
	const X::DefaultSoundID *ids = nullptr;
	size_t n = 0;
#define OS_OPT(o, a) case o: ids = a; n = sizeof (a) / sizeof (a[0]); break
	switch (option) {
	OS_OPT (3, attmode);     // PLAYWHENATTITUDEMODECHANGE
	OS_OPT (4, gpws);        // PLAYGPWS
	OS_OPT (5, mainEng);     // PLAYMAINTHRUST
	OS_OPT (6, hover);       // PLAYHOVERTHRUST
	OS_OPT (7, rcs);         // PLAYATTITUDETHRUST
	OS_OPT (8, docking);     // PLAYDOCKINGSOUND
	OS_OPT (9, radar);       // PLAYRADARBIP
	OS_OPT (10, wind);       // PLAYWINDAIRSPEED
	OS_OPT (11, clearance);  // PLAYDOCKLANDCLEARANCE
	OS_OPT (12, ground);     // PLAYLANDINGANDGROUNDSOUND
	OS_OPT (13, aircond);    // PLAYCABINAIRCONDITIONING
	OS_OPT (14, ambience);   // PLAYCABINRANDOMAMBIANCE
	OS_OPT (15, landedwind); // PLAYWINDAMBIANCEWHENLANDED
	OS_OPT (16, atc);        // PLAYRADIOATC
	OS_OPT (21, retro);      // PLAYRETROTHRUST
	OS_OPT (22, user);       // PLAYUSERTHRUST
	OS_OPT (24, reentry);    // PLAYREENTRYAIRSPEED
	default: break;          // 1, 2, 17, 18, 19, 20, 23: no XRSound counterpart
	}
#undef OS_OPT
	for (size_t i = 0; i < n; i++) p->xr->SetDefaultSoundEnabled (ids[i], onoff != 0);
	return true;
}

// the engines the helper holds, by id (index + 1). XRSound destroys a vessel's after the vessel goes and every one at the
// simulation's end: an engine is dropped then, and its id stays dead
struct Xrs { XrsEngine *e; OBJHANDLE hVessel; };
std::vector<Xrs> *g_xrs;

// XRSound.dll's GetXRSoundEngineInstance or GetModuleXRSoundEngineInstance, as the XRSound SDK finds them; the same engine, the same id
uint32_t XrsEngineId (bool vessel, OBJHANDLE h, const std::string &name)
{
	void *so = dlopen ("XRSound.so", RTLD_NOW | RTLD_NOLOAD);
	if (!so) return 0;
	XrsEngine *e = nullptr;
	if (vessel) {
		auto f = reinterpret_cast<XrsEngine *(*)(OBJHANDLE)> (dlsym (so, "GetXRSoundEngineInstance"));
		if (f && h && oapiIsVessel (h)) e = f (h);
	} else {
		auto f = reinterpret_cast<XrsEngine *(*)(const char *)> (dlsym (so, "GetModuleXRSoundEngineInstance"));
		if (f && !name.empty ()) e = f (name.c_str ());
	}
	dlclose (so);
	if (!e) return 0;
	if (!g_xrs) g_xrs = new std::vector<Xrs>;
	for (size_t i = 0; i < g_xrs->size (); i++) if ((*g_xrs)[i].e == e) return (uint32_t)i + 1;
	g_xrs->push_back ({e, vessel ? h : nullptr});
	return (uint32_t)g_xrs->size ();
}

void Text (WbWriter &w, const char *s) { w.b (s != nullptr); if (s) w.str (s); }

// L_XrsEngine, L_Xrs; a dropped engine answers as XRSound's SDK does when XRSound is absent
void ServeXrs (uint32_t fn, WbReader &r, WbWriter &w)
{
	using X = XRSound;
	if (fn == L_XrsEngine) {
		bool vessel = r.b ();
		OBJHANDLE h = nullptr;
		std::string name;
		if (vessel) h = (OBJHANDLE)WbBridge::Get ().Ptr (r.u32 ());
		else name = Str (r);
		if (!r.bad) w.u32 (XrsEngineId (vessel, h, name));
		return;
	}
	uint32_t id = r.u32 ();
	int m = r.i32 ();
	if (r.bad) return;
	XrsEngine *e = g_xrs && id - 1 < g_xrs->size () ? (*g_xrs)[id - 1].e : nullptr;
	if (m == XR_GetVersion) { w.f64 (e ? e->GetVersion () : 0.0); return; }
	if (m == XR_GetEngineType) { w.i32 (e ? e->GetEngineType () : -1); return; }
	if (m == XR_GetLogID) { Text (w, e ? e->GetLogID () : nullptr); return; }
	int s = r.i32 ();
	switch (m) {
	case XR_LoadWav: {
		std::string file = Str (r);
		int type = r.i32 ();
		if (!r.bad) w.b (e && e->LoadWav (s, file.c_str (), (X::PlaybackType)type));
		break; }
	case XR_PlayWav: {
		bool loop = r.b ();
		double vol = r.f64 ();
		if (!r.bad) w.b (e && e->PlayWav (s, loop, (float)vol));
		break; }
	case XR_StopWav: if (!r.bad) w.b (e && e->StopWav (s)); break;
	case XR_IsWavPlaying: if (!r.bad) w.b (e && e->IsWavPlaying (s)); break;
	case XR_SetPaused: { bool p = r.b (); if (!r.bad) w.b (e && e->SetPaused (s, p)); break; }
	case XR_IsPaused: if (!r.bad) w.b (e && e->IsPaused (s)); break;
	case XR_SetDefaultSoundEnabled: { bool on = r.b (); if (!r.bad) w.b (e && e->SetDefaultSoundEnabled ((X::DefaultSoundID)s, on)); break; }
	case XR_GetDefaultSoundEnabled: if (!r.bad) w.b (e && e->GetDefaultSoundEnabled ((X::DefaultSoundID)s)); break;
	case XR_SetDefaultSoundGroupFolder: {
		std::string folder = Str (r);
		if (!r.bad) w.b (e && e->SetDefaultSoundGroupFolder ((X::DefaultSoundID)s, folder.c_str ()));
		break; }
	case XR_GetDefaultSoundGroupFolder: if (!r.bad) Text (w, e ? e->GetDefaultSoundGroupFolder ((X::DefaultSoundID)s) : nullptr); break;
	case XR_IsDefaultSound: if (!r.bad) w.b (e && e->IsDefaultSound (s)); break;
	case XR_IsDefaultSoundGroup: if (!r.bad) w.b (e && e->IsDefaultSoundGroup (s)); break;
	case XR_IsDefaultSoundOrGroup: if (!r.bad) w.b (e && e->IsDefaultSoundOrGroup (s)); break;
	case XR_SetPan: { double pan = r.f64 (); if (!r.bad) w.b (e && e->SetPan (s, (float)pan)); break; }
	case XR_GetPan: if (!r.bad) w.f64 (e ? e->GetPan (s) : 0.0); break;
	case XR_SetPlaybackSpeed: { double sp = r.f64 (); if (!r.bad) w.b (e && e->SetPlaybackSpeed (s, (float)sp)); break; }
	case XR_GetPlaybackSpeed: if (!r.bad) w.f64 (e ? e->GetPlaybackSpeed (s) : 0.0); break;
	case XR_SetPlayPosition: { uint32_t ms = r.u32 (); if (!r.bad) w.b (e && e->SetPlayPosition (s, ms)); break; }
	case XR_GetPlayPosition: if (!r.bad) w.i32 (e ? e->GetPlayPosition (s) : 0); break;
	default: r.bad = true; break;
	}
}

} // namespace

void WbXrsVesselGone (const void *h)
{
	if (g_xrs) for (Xrs &x : *g_xrs) if (x.hVessel == h) x.e = nullptr;
}

void WbXrsSessionEnd ()
{
	if (g_xrs) for (Xrs &x : *g_xrs) x.e = nullptr;
}

bool WbSoundPresent ()
{
	void *h = dlopen ("XRSound.so", RTLD_NOW | RTLD_NOLOAD); // as XRSound's own SDK finds the plugin
	if (h) dlclose (h);
	return h != nullptr;
}

void WbSoundVesselGone (const void *h)
{
	for (int id = 1; id < OS_MAXID; id++) {
		if (!g_vessel[id] || g_vessel[id]->hVessel != h) continue;
		if (id == g_radioId) { g_radioId = -1; g_radioWav = 0; }
		Drop (g_vessel[id]);
	}
}

void WbSoundClear ()
{
	g_radioId = -1;
	g_radioWav = 0;
	for (Proxy *&p : g_vessel) if (p) Drop (p);
	for (Proxy *&p : g_mfd) if (p) Drop (p);
	delete g_xrs;
	g_xrs = nullptr;
}

bool WbServeSound (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	if (fn == L_XrsEngine || fn == L_Xrs) {
		ServeXrs (fn, r, w);
		if (r.bad) b.BadArgs (fn);
		return true;
	}
	if (fn < L_OsSoundID || fn > L_OsMfdStop) return false;
	switch (fn) {
	case L_OsSoundID: {
		OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ());
		if (!r.bad) w.i32 (SoundId (h));
		break; }
	case L_OsLoad: {
		int id = r.i32 (), wav = r.i32 ();
		std::string file = Str (r);
		int extended = r.i32 ();
		if (r.bad) break;
		Proxy *p = VesselProxy (id);
		w.b (p && VesselWave (wav) && LoadWave (p, wav, file, PlaybackFor (extended)));
		break; }
	case L_OsSet3d: {
		int id = r.i32 (), wav = r.i32 ();
		if (!r.bad) w.b (VesselProxy (id) && VesselWave (wav)); // no 3D in XRSound: nothing to set
		break; }
	case L_OsPlay: {
		int id = r.i32 (), wav = r.i32 (), loop = r.i32 (), volume = r.i32 (), frequency = r.i32 ();
		if (r.bad) break;
		Proxy *p = VesselProxy (id);
		w.b (p && VesselWave (wav) && Play (p, wav, loop != 0, volume, frequency, 3000, 44100));
		break; }
	case L_OsPlayRadio: { // once, on the radio channel: the wave on it before stops
		int id = r.i32 (), wav = r.i32 (), volume = r.i32 (), frequency = r.i32 ();
		if (r.bad) break;
		Proxy *p = VesselProxy (id);
		bool ok = p && VesselWave (wav);
		if (ok) {
			RadioStop ();
			ok = Play (p, wav, false, volume, frequency, 3000, 44100);
			if (ok) { g_radioId = id; g_radioWav = wav; }
		}
		w.b (ok);
		break; }
	case L_OsStop: {
		int id = r.i32 (), wav = r.i32 ();
		if (r.bad) break;
		Proxy *p = VesselProxy (id);
		w.b (p && VesselWave (wav) && p->xr->StopWav (wav));
		break; }
	case L_OsIsPlaying: {
		int id = r.i32 (), wav = r.i32 ();
		if (r.bad) break;
		Proxy *p = VesselProxy (id);
		w.b (p && VesselWave (wav) && p->xr->IsWavPlaying (wav));
		break; }
	case L_OsIsPlayingRadio: {
		Proxy *p = VesselProxy (g_radioId);
		w.b (p && p->xr->IsWavPlaying (g_radioWav));
		break; }
	case L_OsOption: {
		int id = r.i32 (), option = r.i32 (), onoff = r.i32 ();
		if (!r.bad) w.b (Option (id, option, onoff));
		break; }
	case L_OsSetFrequency: RadioStop (); w.b (true); break; // the radio's frequency text: shown nowhere here
	case L_OsReplace: {
		int id = r.i32 ();
		std::string file = Str (r);
		int which = r.i32 ();
		if (!r.bad) w.b (Replace (id, file, which));
		break; }
	case L_OsMfdID: {
		std::string name = Str (r);
		if (!r.bad) w.i32 (MfdId (name));
		break; }
	case L_OsMfdLoad: {
		int id = r.i32 (), wav = r.i32 ();
		std::string file = Str (r);
		if (r.bad) break;
		Proxy *p = MfdProxy (id);
		w.b (p && MfdWave (wav) && LoadWave (p, wav, file, XRSound::PlaybackType::Global));
		break; }
	case L_OsMfdPlay: {
		int id = r.i32 (), wav = r.i32 (), loop = r.i32 (), volume = r.i32 (), frequency = r.i32 ();
		if (r.bad) break;
		Proxy *p = MfdProxy (id);
		w.b (p && MfdWave (wav) && Play (p, wav, loop != 0, volume, frequency, 4000, 22050));
		break; }
	case L_OsMfdStop: {
		int id = r.i32 (), wav = r.i32 ();
		if (r.bad) break;
		Proxy *p = MfdProxy (id);
		w.b (p && MfdWave (wav) && p->xr->StopWav (wav));
		break; }
	}
	if (r.bad) b.BadArgs (fn);
	return true;
}
