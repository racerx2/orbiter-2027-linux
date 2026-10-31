// WineBridge: the Windows modules' Launchpad items, custom commands, external camera control, camera modes and help. Orbiter holds a
// proxy of each item and camera control, and a function of the bridge for each command; they call the module's objects and functions
#include "Orbitersdk.h"
#include "CamAPI.h"
#include "Bridge.h"
#include "Manual.h"
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

using Guard = std::lock_guard<std::recursive_mutex>;

static_assert (sizeof (ExternalCameraControl::VCMode) == 32 && sizeof (ExternalCameraControl::TrackMode) == 24 && sizeof (DWORD) == 4,
	"as in the 32-bit SDK");

static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }
static bool Live (uint64_t session) { WbBridge &b = WbBridge::Get (); return b.Alive () && session == b.Session (); }

namespace {
	// ---------------------------------------------------------------------
	// Launchpad items

	class WbLpadItem final : public LaunchpadItem {
	public:
		WbLpadItem (uint32_t obj, int module, uint64_t session) : obj (obj), module (module), session (session) {}
		uint32_t obj;
		int module;
		uint64_t session;
		bool gone = false; // its module is freed

		char *Name () override { return Text (P_Name, name, hasName); }
		char *Description () override { return Text (P_Description, desc, hasDesc); }
		bool clbkOpen (QWidget *hLaunchpad) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw ()) return false;
			Begin (P_Open);
			WbReader r = b.Call ();
			bool there = r.b (), res = r.b ();
			return there && res && !r.bad;
		}
		int clbkWriteConfig () override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw ()) return 0;
			Begin (P_WriteConfig);
			WbReader r = b.Call ();
			bool there = r.b ();
			int res = r.i32 ();
			return there && !r.bad ? res : 0;
		}
	private:
		std::string name, desc; // the module's strings, kept for Orbiter's pointer
		bool hasName = false, hasDesc = false;
		bool Fw () const { return !gone && Live (session); }
		void Begin (int c) { WbWriter &w = WbBridge::Get ().Begin (H_LPAD); w.u32 (obj); w.i32 (c); }
		char *Text (int c, std::string &s, bool &has)
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (Fw ()) {
				Begin (c);
				WbReader r = b.Call ();
				bool there = r.b ();
				uint32_t n;
				const char *p = r.str (&n);
				if (there && !r.bad) { has = p != nullptr; s = p ? std::string (p, n) : std::string (); }
			}
			return has ? s.data () : nullptr;
		}
	};
	std::unordered_map<uint32_t, WbLpadItem *> &g_items = *new std::unordered_map<uint32_t, WbLpadItem *>; // by the helper's object

	// ---------------------------------------------------------------------
	// custom commands: Orbiter keeps the description's pointer and calls WbCustomFunc with the record

	struct Cmd { DWORD id = 0; std::string desc; uint32_t fn = 0, ctx = 0; int module = 0; uint64_t session = 0; };
	std::list<Cmd> &g_cmds = *new std::list<Cmd>; // records stay where they are

	void WbCustomFunc (void *context)
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		const Cmd *c = (const Cmd *)context;
		if (!Live (c->session)) return;
		WbWriter &w = b.Begin (H_CUSTOMCMD);
		w.u32 (c->fn); w.u32 (c->ctx);
		b.Call ();
	}

	// ---------------------------------------------------------------------
	// external camera control: Orbiter polls the module's object; its modes follow the object's (live items)

	class WbEcc final : public ExternalCameraControl {
	public:
		WbEcc (uint32_t obj, int module, uint64_t session) : ExternalCameraControl (0, 0), obj (obj), module (module), session (session) {}
		~WbEcc () { for (uint32_t id : live) WbLiveRelease (id); }
		uint32_t obj;
		int module;
		uint64_t session;
		bool ReadModes (WbReader &r)
		{
			void *dst[4] = {&datamode, &cameramode, &vcmode, &trkmode};
			const uint32_t n[4] = {4, 4, sizeof (VCMode), sizeof (TrackMode)};
			for (int i = 0; i < 4; i++) live[i] = WbLiveBytes (r, dst[i], n[i]);
			return !r.bad;
		}
		bool clbkPoll (CamData *data) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Live (session)) return false;
			b.Begin (H_ECC).u32 (obj);
			WbReader r = b.Call ();
			bool ok = r.b ();
			CamData d;
			d.x = r.f64 (); d.y = r.f64 (); d.z = r.f64 (); d.yaw = r.f64 (); d.pitch = r.f64 (); d.roll = r.f64 ();
			if (r.bad || !ok) return false;
			*data = d;
			return true;
		}
	private:
		uint32_t live[4] = {};
	};
	WbEcc *g_ecc; // the proxy Orbiter's camera holds; dropped with that camera (wbCameraControlGone)

	void EccGone ()
	{
		oapiUnregisterExternalCameraControl ();
		delete g_ecc;
		g_ecc = nullptr;
	}

	// ---------------------------------------------------------------------
	// camera modes: Orbiter's classes with the module's fields

	struct Cockpit : CameraMode_Cockpit {
		void Set (int m, int p, int l, bool s) { cmode = (CockpitMode)m; pos = p; lean = l; lean_smooth = s; }
	};
	struct Ground : CameraMode_Ground {
		void Set (OBJHANDLE r, double lo, double la, double al, double ph, double th, bool above, bool lock)
		{
			ref = r; lng = lo; lat = la; alt = al; phi = ph; theta = th; alt_above_ground = above; tgtlock = lock;
		}
	};
}

// ---------------------------------------------------------------------

bool WbServeExtra (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	switch (fn) {
	case L_LpadRegister: {
		uint32_t obj = r.u32 ();
		int module = r.i32 ();
		LAUNCHPADITEM_HANDLE parent = (LAUNCHPADITEM_HANDLE)b.Ptr (r.u32 ());
		if (r.bad || !obj) { b.BadArgs (fn); return true; }
		auto it = g_items.find (obj);
		WbLpadItem *p = it != g_items.end () ? it->second : new WbLpadItem (obj, module, b.Session ());
		g_items[obj] = p;
		// as RegisterExtraParam: an item of the name there already is the one Orbiter keeps
		const char *name = p->Name ();
		LAUNCHPADITEM_HANDLE there = name ? oapiFindLaunchpadItem (name, parent) : nullptr;
		LAUNCHPADITEM_HANDLE h = there ? there : oapiRegisterLaunchpadItem (p, parent);
		if (there && it == g_items.end ()) { g_items.erase (obj); delete p; }
		w.u32 (b.Id (h)); w.b (!there);
		return true; }
	case L_LpadUnregister: {
		uint32_t obj = r.u32 ();
		auto it = r.bad ? g_items.end () : g_items.find (obj);
		bool ok = false;
		if (it != g_items.end ()) {
			WbLpadItem *p = it->second;
			ok = oapiUnregisterLaunchpadItem (p); // Orbiter calls its clbkWriteConfig
			g_items.erase (obj);
			delete p;
		}
		w.b (ok);
		return true; }
	case L_CustomCmd: {
		std::string label = Str (r), desc = Str (r);
		uint32_t f = r.u32 (), ctx = r.u32 ();
		int module = r.i32 ();
		if (r.bad || !f) { b.BadArgs (fn); w.u32 (0); return true; }
		Cmd &c = g_cmds.emplace_back ();
		c.desc = desc; c.fn = f; c.ctx = ctx; c.module = module; c.session = b.Session ();
		c.id = oapiRegisterCustomCmd (label.c_str (), c.desc.c_str (), WbCustomFunc, &c);
		w.u32 (c.id);
		return true; }
	case L_CustomCmdGone: {
		DWORD id = r.u32 ();
		bool ok = false;
		for (auto it = g_cmds.begin (); !r.bad && it != g_cmds.end (); ++it)
			if (it->id == id && it->session == b.Session ()) { ok = oapiUnregisterCustomCmd ((int)id); g_cmds.erase (it); break; }
		w.b (ok);
		return true; }
	case L_EccRegister: {
		uint32_t obj = r.u32 ();
		int module = r.i32 ();
		if (r.bad) { b.BadArgs (fn); w.b (false); return true; }
		if (!obj) { w.b (oapiRegisterExternalCameraControl (nullptr)); return true; } // Orbiter's answer: true when its camera holds none
		WbEcc *p = new WbEcc (obj, module, b.Session ());
		if (!p->ReadModes (r)) { delete p; b.BadArgs (fn); w.b (false); return true; }
		bool ok = oapiRegisterExternalCameraControl (p);
		if (ok) { delete g_ecc; g_ecc = p; } // a proxy still here held by no camera (an Orbiter that doesn't tell): dropped
		else delete p;
		w.b (ok);
		return true; }
	case L_EccUnregister: {
		w.b (oapiUnregisterExternalCameraControl ());
		delete g_ecc; // the camera holds none now
		g_ecc = nullptr;
		return true; }
	case L_SetCameraMode: {
		int m = r.i32 ();
		OBJHANDLE target = (OBJHANDLE)b.Ptr (r.u32 ());
		double fov = r.f64 ();
		bool ok = false;
		switch (m) {
		case CameraMode::CM_COCKPIT: {
			int cm = r.i32 (), pos = r.i32 (), lean = r.i32 ();
			bool smooth = r.b ();
			if (r.bad) break;
			Cockpit c;
			c.SetTarget (target); c.SetFOV (fov); c.Set (cm, pos, lean, smooth);
			ok = oapiSetCameraMode (c);
			break; }
		case CameraMode::CM_TRACK: {
			int tm = r.i32 ();
			double rd = r.f64 (), ph = r.f64 (), th = r.f64 ();
			OBJHANDLE ref = (OBJHANDLE)b.Ptr (r.u32 ());
			if (r.bad) break;
			CameraMode_Track t;
			t.SetTarget (target); t.SetFOV (fov); t.SetTrackMode ((CameraMode_Track::TrackMode)tm, ref); t.SetPosition (rd, ph, th);
			ok = oapiSetCameraMode (t);
			break; }
		case CameraMode::CM_GROUND: {
			OBJHANDLE ref = (OBJHANDLE)b.Ptr (r.u32 ());
			double lng = r.f64 (), lat = r.f64 (), alt = r.f64 (), ph = r.f64 (), th = r.f64 ();
			bool above = r.b (), lock = r.b ();
			if (r.bad) break;
			Ground gm;
			gm.SetTarget (target); gm.SetFOV (fov); gm.Set (ref, lng, lat, alt, ph, th, above, lock);
			ok = oapiSetCameraMode (gm);
			break; }
		default: r.bad = true; break;
		}
		if (r.bad) b.BadArgs (fn);
		w.b (ok);
		return true; }
	case L_OpenHelp: {
		bool lp = r.b ();
		std::string s[4];
		bool has[4];
		for (int i = 0; i < 4; i++) { uint32_t n; const char *p = r.str (&n); has[i] = p != nullptr; if (p) s[i].assign (p, n); }
		if (r.bad) { b.BadArgs (fn); w.b (false); return true; }
		HELPCONTEXT hc = {has[0] ? s[0].data () : nullptr, has[1] ? s[1].data () : nullptr, has[2] ? s[2].data () : nullptr,
			has[3] ? s[3].data () : nullptr};
		w.b (lp ? oapiOpenLaunchpadHelp (&hc) : oapiOpenHelp (&hc));
		return true; }
	default:
		return false;
	}
}

// the module is freed (all modules: the helper stopped): Orbiter calls none of its items, commands or camera control any more
static void Gone (bool all, int module)
{
	WbBridge &b = WbBridge::Get ();
	auto mine = [&] (int m, uint64_t session) { return all || (m == module && session == b.Session ()); };
	for (auto it = g_items.begin (); it != g_items.end (); ) {
		WbLpadItem *p = it->second;
		if (!mine (p->module, p->session)) { ++it; continue; }
		p->gone = true;
		oapiUnregisterLaunchpadItem (p);
		delete p;
		it = g_items.erase (it);
	}
	for (auto it = g_cmds.begin (); it != g_cmds.end (); ) {
		if (!mine (it->module, it->session)) { ++it; continue; }
		oapiUnregisterCustomCmd ((int)it->id);
		it = g_cmds.erase (it);
	}
	if (g_ecc && mine (g_ecc->module, g_ecc->session)) EccGone ();
}

void WbExtraFreed (int module) { Gone (false, module); }
void WbExtraStopped () { Gone (true, 0); }

// Orbiter's camera is destroyed with the control it held (the session ends): the helper reads that object no more
DLLCLBK void wbCameraControlGone (ExternalCameraControl *ecc)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	if (!g_ecc || ecc != g_ecc) return;
	if (Live (g_ecc->session)) { b.Begin (H_ECCGONE).u32 (g_ecc->obj); b.Call (); }
	delete g_ecc;
	g_ecc = nullptr;
}
