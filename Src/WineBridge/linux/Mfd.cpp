// WineBridge: the Windows modules' MFD modes, MFDs, external MFDs and module objects. Orbiter gets a proxy for each object of
// the helper's: the proxy's virtual methods call the object's, and the object's calls to Orbiter's side come back to the proxy.
#include "Orbitersdk.h"
#include "DrawAPI.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <array>
#include <cstring>
#include <list>
#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using Guard = std::lock_guard<std::recursive_mutex>;

static std::string Str (WbReader &r) { uint32_t n; const char *s = r.str (&n); return s ? std::string (s, n) : std::string (); }

// the helper the object came from is still the one running
static bool Live (uint64_t session) { WbBridge &b = WbBridge::Get (); return b.Alive () && session == b.Session (); }

static VESSEL4 *Vessel4 (uint32_t vid)
{
	VESSEL *v = WbBridge::Get ().Vessel (vid);
	return v && v->Version () >= 3 ? static_cast<VESSEL4 *> (v) : nullptr;
}

static MFDSPEC GetSpec (WbReader &r)
{
	MFDSPEC s {};
	s.pos.left = r.i32 (); s.pos.top = r.i32 (); s.pos.right = r.i32 (); s.pos.bottom = r.i32 ();
	s.nbt_left = r.i32 (); s.nbt_right = r.i32 (); s.bt_yofs = r.i32 (); s.bt_ydist = r.i32 ();
	return s;
}

// ---------------------------------------------------------------------
// modes: Orbiter's spec of a bridged mode has the bridge's record as its context and one message function

namespace {
	struct WbMode { int num; uint64_t session; std::string name; };
	std::unordered_set<const WbMode *> &g_modes = *new std::unordered_set<const WbMode *>; // never deleted: Orbiter's copies point at them
	std::set<int> &g_toldEmpty = *new std::set<int>;

	// what an MFD proxy keeps for its life: the helper's object, the labels (a buffer per button, as a module's own) and menus
	struct WbMfdLink {
		MFD *mfd = nullptr;
		const WbMode *mode = nullptr;
		UINT id = 0;      // the instrument's
		uint32_t obj = 0;
		uint64_t session = 0;
		bool mfd2 = true;
		std::map<int, std::array<char, 64>> labels;
		std::set<std::string> strs; // the menus' lines
		std::list<std::vector<MFDBUTTONMENU>> menus;
		const char *Intern (WbReader &r)
		{
			uint32_t n;
			const char *s = r.str (&n);
			return s && !r.bad ? strs.insert (std::string (s, n)).first->c_str () : nullptr;
		}
	};
	std::unordered_set<const WbMfdLink *> &g_links = *new std::unordered_set<const WbMfdLink *>;
	// the proxies by instrument id, oldest first: Orbiter makes an instrument's new one before it closes the old one, which
	// gets OAPI_MSG_MFD_CLOSED; a proxy Orbiter deletes without closing it (ReadParams) leaves at once
	std::unordered_map<UINT, std::list<const WbMfdLink *>> &g_open = *new std::unordered_map<UINT, std::list<const WbMfdLink *>>;

	template <class Base> class WbMfdT : public Base {
	public:
		WbMfdT (DWORD w, DWORD h, VESSEL *v, WbMfdLink *l) : Base (w, h, v), link (l) { l->mfd = this; g_links.insert (l); }
		~WbMfdT () override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (Up ()) { Begin (M_Delete); b.Call (); } // Orbiter deletes its instrument's object: the module's goes too
			auto it = g_open.find (link->id);
			if (it != g_open.end ()) { it->second.remove (link); if (it->second.empty ()) g_open.erase (it); }
			g_links.erase (link);
			delete link;
		}
		bool ConsumeKeyBuffered (DWORD key) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return false;
			Begin (M_KeyBuffered).u32 (key);
			WbReader r = WbBridge::Get ().Call ();
			return There (r) && r.b ();
		}
		bool ConsumeKeyImmediate (char *kstate) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return false;
			Begin (M_KeyImmediate).raw (kstate, 256);
			WbReader r = WbBridge::Get ().Call ();
			if (!There (r)) return false;
			bool res = r.b ();
			char ks[256];
			r.raw (ks, 256);
			if (!r.bad) memcpy (kstate, ks, 256);
			return res;
		}
		bool ConsumeButton (int bt, int event) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return false;
			WbWriter &w = Begin (M_Button); w.i32 (bt); w.i32 (event);
			WbReader r = WbBridge::Get ().Call ();
			return There (r) && r.b ();
		}
		char *ButtonLabel (int bt) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return nullptr;
			Begin (M_Label).i32 (bt);
			WbReader r = WbBridge::Get ().Call ();
			if (!There (r)) return nullptr;
			uint32_t n;
			const char *s = r.str (&n);
			if (!s || r.bad) return nullptr;
			auto &buf = link->labels[bt];
			n = std::min<uint32_t> (n, (uint32_t)buf.size () - 1);
			memcpy (buf.data (), s, n);
			buf[n] = 0;
			return buf.data ();
		}
		int ButtonMenu (const MFDBUTTONMENU **menu) const override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return 0;
			Begin (M_Menu);
			WbReader r = WbBridge::Get ().Call ();
			if (!There (r)) return 0;
			int n = r.i32 ();
			if (r.bad || n < 0 || n > 64) return 0;
			std::vector<MFDBUTTONMENU> v ((size_t)n);
			for (auto &e : v) { e.line1 = link->Intern (r); e.line2 = link->Intern (r); uint8_t c = 0; r.raw (&c, 1); e.selchar = (char)c; }
			if (r.bad) return 0;
			auto same = [&] (const std::vector<MFDBUTTONMENU> &m) {
				if (m.size () != v.size ()) return false;
				for (size_t i = 0; i < m.size (); i++)
					if (m[i].line1 != v[i].line1 || m[i].line2 != v[i].line2 || m[i].selchar != v[i].selchar) return false;
				return true;
			};
			const std::vector<MFDBUTTONMENU> *kept = nullptr;
			for (auto &m : link->menus) if (same (m)) kept = &m;
			if (!kept) { link->menus.push_back (std::move (v)); kept = &link->menus.back (); } // valid while the proxy is
			if (menu) *menu = n ? kept->data () : nullptr;
			return n;
		}
		void WriteStatus (FILEHANDLE scn) const override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return;
			Begin (M_WriteStatus).u32 (WbBridge::Get ().Id (scn));
			WbBridge::Get ().Call ();
		}
		void ReadStatus (FILEHANDLE scn) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return;
			Begin (M_ReadStatus).u32 (WbBridge::Get ().Id (scn));
			WbBridge::Get ().Call ();
		}
		void StoreStatus () const override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return;
			Begin (M_StoreStatus);
			WbBridge::Get ().Call ();
		}
		void RecallStatus () override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Up ()) return;
			Begin (M_RecallStatus);
			WbBridge::Get ().Call ();
		}
	protected:
		WbMfdLink *link;
		bool Up () const { return link->obj && Live (link->session); }
		// the call, with the character size Orbiter set after the mode made the object
		WbWriter &Begin (int m) const
		{
			WbWriter &w = WbBridge::Get ().Begin (H_MFD);
			w.u32 (link->obj); w.i32 (m); w.u32 (this->cw); w.u32 (this->ch);
			return w;
		}
		static bool There (WbReader &r) { bool t = r.b (); return t && !r.bad; }
	};

	// an MFD2: Orbiter draws it through a sketchpad
	class WbMfdSkp final : public WbMfdT<MFD2> {
	public:
		using WbMfdT<MFD2>::WbMfdT;
		using MFD2::Update;
		bool Update (oapi::Sketchpad *skp) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Up () || !skp) return false;
			WbSkpBegin (skp); // the module draws on Orbiter's sketchpad during the call
			WbWriter &w = Begin (M_Update); w.u32 (b.Id (skp->GetSurface ())); w.u32 (b.Id (skp));
			WbReader r = b.Call ();
			WbSkpEnd (skp);
			return There (r) && r.b ();
		}
	};

	// an MFD: Orbiter draws it through a DC; GDI draws in the helper on a copy of the image under Orbiter's painter
	class WbMfdDC final : public WbMfdT<MFD> {
	public:
		using WbMfdT<MFD>::WbMfdT;
		void Update (QPainter *hDC) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Up () || !hDC) return;
			QImage *img = WbPainterImage (hDC);
			if (!img) {
				static bool said;
				if (!said) oapiWriteLogV ("WineBridge: a Windows MFD drawing with GDI needs a surface in memory; it shows nothing");
				said = true;
				return;
			}
			WbWriter &w = Begin (M_UpdateDC);
			QColor c = hDC->pen ().color (); // Orbiter's painter draws text with its pen
			w.u32 (RGB (c.red (), c.green (), c.blue ()));
			WbPutImage (w, *img);
			WbReader r = b.Call ();
			if (There (r)) WbDrawChanged (r, hDC);
		}
	};

	// the mode's object, through the helper; when there is none, a proxy that shows nothing, so Orbiter never gets null
	MFD *Open (const WbMode *m, UINT id, int W, int H, DWORD key, VESSEL *v)
	{
		WbBridge &b = WbBridge::Get ();
		WbMfdLink *l = new WbMfdLink;
		l->mode = m;
		l->id = id;
		if (m && g_modes.count (m) && Live (m->session)) {
			l->session = m->session;
			WbWriter &w = b.Begin (H_MFDOPEN);
			w.u32 ((uint32_t)m->num); w.u32 (id); w.i32 (W); w.i32 (H); w.u32 (key);
			w.u32 (b.Id (v ? v->GetHandle () : nullptr)); w.u32 (b.Id (l));
			WbReader r = b.Call ();
			uint32_t obj = r.u32 ();
			bool two = r.b ();
			if (!r.bad) { l->obj = obj; l->mfd2 = two; }
		}
		if (!l->obj && m && g_toldEmpty.insert (m->num).second)
			oapiWriteLogV ("WineBridge: MFD mode %s: the Windows module made no MFD; it shows nothing", m->name.c_str ());
		g_open[id].push_back (l);
		MFD *p = l->mfd2 ? static_cast<MFD *> (new WbMfdSkp ((DWORD)W, (DWORD)H, v, l)) : static_cast<MFD *> (new WbMfdDC ((DWORD)W, (DWORD)H, v, l));
		return p;
	}

	OAPI_MSGTYPE MsgProc (UINT msg, UINT mfd, WPARAM wparam, LPARAM lparam)
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		switch (msg) {
		case OAPI_MSG_MFD_OPENEDEX: { // Orbiter's copy of the spec has the record as context
			const MFDMODEOPENSPEC *os = (const MFDMODEOPENSPEC *)wparam;
			if (!os || !os->spec) return 0;
			const WbMode *m = (const WbMode *)os->spec->context;
			return (OAPI_MSGTYPE)Open (g_modes.count (m) ? m : nullptr, mfd, os->w, os->h, os->spec->key, (VESSEL *)lparam);
		}
		case OAPI_MSG_MFD_CLOSED: { // the instrument's oldest proxy: Orbiter deletes it next
			auto it = g_open.find (mfd);
			if (it == g_open.end () || it->second.empty ()) return 0;
			const WbMode *m = it->second.front ()->mode;
			if (m && Live (m->session)) { WbWriter &w = b.Begin (H_MFDCLOSED); w.u32 ((uint32_t)m->num); w.u32 (mfd); b.Call (); }
			return 0;
		}
		default: // OAPI_MSG_MFD_OPENED comes only after OPENEDEX gave null, which it never does
			return 0;
		}
	}

	// ---------------------------------------------------------------------
	// module objects: the callbacks the object's class overrides go to it, the others are Orbiter's defaults (the module's
	// opc functions through its slot)

	class WbModule final : public oapi::Module {
	public:
		WbModule (void *slot, uint32_t num, int module, uint32_t over, bool old, uint64_t session)
		: oapi::Module (slot), num (num), module (module), over (over), old (old), session (session) {}
		uint32_t num;
		int module;     // the helper's module number of the object's hModule (0: none)
		uint32_t over;  // WbModuleCall bits
		bool old, gone = false;
		uint64_t session;

		void clbkSimulationStart (RenderMode mode) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_SimulationStart)) { oapi::Module::clbkSimulationStart (mode); return; }
			Begin (O_SimulationStart).i32 (mode);
			WbBridge::Get ().Call ();
		}
		void clbkSimulationEnd () override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_SimulationEnd)) { oapi::Module::clbkSimulationEnd (); return; }
			Begin (O_SimulationEnd);
			WbBridge::Get ().Call ();
		}
		void clbkPreStep (double simt, double simdt, double mjd) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (WbBridge::Get ().Alive ()) WbBridge::Get ().Frame (simt); // frames counted with Windows plugins only, as for legacy ones
			if (!Fw (O_PreStep)) { oapi::Module::clbkPreStep (simt, simdt, mjd); return; }
			WbWriter &w = Begin (O_PreStep); w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
			WbBridge::Get ().Call ();
		}
		void clbkPostStep (double simt, double simdt, double mjd) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_PostStep)) { oapi::Module::clbkPostStep (simt, simdt, mjd); return; }
			WbWriter &w = Begin (O_PostStep); w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
			WbBridge::Get ().Call ();
		}
		void clbkTimeJump (double simt, double simdt, double mjd) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_TimeJump)) { oapi::Module::clbkTimeJump (simt, simdt, mjd); return; }
			WbWriter &w = Begin (O_TimeJump); w.f64 (simt); w.f64 (simdt); w.f64 (mjd);
			WbBridge::Get ().Call ();
		}
		void clbkFocusChanged (OBJHANDLE new_focus, OBJHANDLE old_focus) override
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (O_FocusChanged)) { oapi::Module::clbkFocusChanged (new_focus, old_focus); return; }
			WbWriter &w = Begin (O_FocusChanged); w.u32 (b.Id (new_focus)); w.u32 (b.Id (old_focus));
			b.Call ();
		}
		void clbkTimeAccChanged (double new_warp, double old_warp) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_TimeAccChanged)) { oapi::Module::clbkTimeAccChanged (new_warp, old_warp); return; }
			WbWriter &w = Begin (O_TimeAccChanged); w.f64 (new_warp); w.f64 (old_warp);
			WbBridge::Get ().Call ();
		}
		void clbkNewVessel (OBJHANDLE hVessel) override { Vessel (O_NewVessel, hVessel); }
		void clbkDeleteVessel (OBJHANDLE hVessel) override { Vessel (O_DeleteVessel, hVessel); }
		void clbkVesselJump (OBJHANDLE hVessel) override { Vessel (O_VesselJump, hVessel); }
		void clbkPause (bool pause) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_Pause)) { oapi::Module::clbkPause (pause); return; }
			Begin (O_Pause).b (pause);
			WbBridge::Get ().Call ();
		}
		bool clbkProcessMouse (UINT event, DWORD state, DWORD x, DWORD y) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_ProcessMouse)) return oapi::Module::clbkProcessMouse (event, state, x, y);
			WbWriter &w = Begin (O_ProcessMouse); w.u32 (event); w.u32 (state); w.u32 (x); w.u32 (y);
			WbReader r = WbBridge::Get ().Call ();
			bool there = r.b ();
			return there && r.b () && !r.bad;
		}
		bool clbkProcessKeyboardImmediate (char kstate[256], bool simRunning) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_ProcessKeyboardImmediate)) return oapi::Module::clbkProcessKeyboardImmediate (kstate, simRunning);
			WbWriter &w = Begin (O_ProcessKeyboardImmediate); w.raw (kstate, 256); w.b (simRunning);
			return Keys (kstate);
		}
		bool clbkProcessKeyboardBuffered (DWORD key, char kstate[256], bool simRunning) override
		{
			Guard g (WbBridge::Get ().Lock ());
			if (!Fw (O_ProcessKeyboardBuffered)) return oapi::Module::clbkProcessKeyboardBuffered (key, kstate, simRunning);
			WbWriter &w = Begin (O_ProcessKeyboardBuffered); w.u32 (key); w.raw (kstate, 256); w.b (simRunning);
			return Keys (kstate);
		}
	private:
		bool Fw (int c) const { return !gone && (over >> c & 1u) && Live (session); }
		WbWriter &Begin (int c) { WbWriter &w = WbBridge::Get ().Begin (H_MODULE); w.u32 (num); w.i32 (c); return w; }
		void Vessel (int c, OBJHANDLE h)
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Fw (c)) {
				if (c == O_NewVessel) oapi::Module::clbkNewVessel (h);
				else if (c == O_DeleteVessel) { oapi::Module::clbkDeleteVessel (h); b.Retire (oapiGetVesselInterface (h)); b.Retire (h); } // not upstream (F2)
				else oapi::Module::clbkVesselJump (h);
				return;
			}
			Begin (c).u32 (b.Id (h));
			b.Call ();
			if (c == O_DeleteVessel) { b.Retire (oapiGetVesselInterface (h)); b.Retire (h); } // not upstream (F2): after the module's own
		}
		bool Keys (char *kstate)
		{
			WbReader r = WbBridge::Get ().Call ();
			bool there = r.b ();
			if (!there || r.bad) return false;
			bool res = r.b ();
			char ks[256];
			r.raw (ks, 256);
			if (r.bad) return false;
			memcpy (kstate, ks, 256);
			return res;
		}
	};
	std::map<uint32_t, WbModule *> &g_moduleObjs = *new std::map<uint32_t, WbModule *>; // by the helper's number

	// Orbiter's default of a callback for the module (as GetProcAddress of its opc function)
	void ModuleDefault (int module, int c, WbReader &r)
	{
		WbBridge &b = WbBridge::Get ();
		double d[3] = {};
		uint32_t u[2] = {};
		int mode = 0;
		bool p = false;
		switch (c) {
		case O_SimulationStart: mode = r.i32 (); break;
		case O_SimulationEnd: break;
		case O_PreStep: case O_PostStep: d[0] = r.f64 (); d[1] = r.f64 (); d[2] = r.f64 (); break;
		case O_FocusChanged: u[0] = r.u32 (); u[1] = r.u32 (); break;
		case O_TimeAccChanged: d[0] = r.f64 (); d[1] = r.f64 (); break;
		case O_DeleteVessel: u[0] = r.u32 (); break;
		case O_Pause: p = r.b (); break;
		default: return;
		}
		void *slot = WbSlotFor (module);
		if (r.bad || !slot) return;
		oapi::Module t (slot);
		switch (c) {
		case O_SimulationStart: t.oapi::Module::clbkSimulationStart ((oapi::Module::RenderMode)mode); break;
		case O_SimulationEnd: t.oapi::Module::clbkSimulationEnd (); break;
		case O_PreStep: t.oapi::Module::clbkPreStep (d[0], d[1], d[2]); break;
		case O_PostStep: t.oapi::Module::clbkPostStep (d[0], d[1], d[2]); break;
		case O_FocusChanged: t.oapi::Module::clbkFocusChanged ((OBJHANDLE)b.Ptr (u[0]), (OBJHANDLE)b.Ptr (u[1])); break;
		case O_TimeAccChanged: t.oapi::Module::clbkTimeAccChanged (d[0], d[1]); break;
		case O_DeleteVessel: t.oapi::Module::clbkDeleteVessel ((OBJHANDLE)b.Ptr (u[0])); break;
		case O_Pause: t.oapi::Module::clbkPause (p); break;
		}
	}

	// ---------------------------------------------------------------------
	// external MFDs: Orbiter's object (with the instrument) is the proxy; the module's object is the helper's

	class WbEmfd final : public ExternMFD {
	public:
		WbEmfd (const MFDSPEC &spec, uint32_t num, uint64_t session) : ExternMFD (spec), num (num), session (session) {}
		~WbEmfd () override;
		uint32_t num;
		uint64_t session;
		bool byHelper = false, registered = false;

		void PutMembers (WbWriter &w) const
		{
			w.u32 (WbBridge::Get ().Id (hVessel));
			w.i32 (DW); w.i32 (DH); w.i32 (pmode); w.i32 (nbt1); w.i32 (nbt2); w.i32 (bty0); w.i32 (btdy); w.i32 (btpressed);
			w.b (instr != nullptr);
		}
		void GetMembers (WbReader &r) // Orbiter's instrument stays Orbiter's
		{
			uint32_t hv = r.u32 ();
			int dw = r.i32 (), dh = r.i32 (), pm = r.i32 (), n1 = r.i32 (), n2 = r.i32 (), y0 = r.i32 (), dy = r.i32 (), bp = r.i32 ();
			r.b ();
			if (r.bad) return;
			hVessel = (OBJHANDLE)WbBridge::Get ().Ptr (hv);
			DW = dw; DH = dh; pmode = pm; nbt1 = n1; nbt2 = n2; bty0 = y0; btdy = dy; btpressed = bp;
		}

		void SetVessel (OBJHANDLE hV) override { if (!Fw (X_SetVessel, WbBridge::Get ().Id (hV), true)) ExternMFD::SetVessel (hV); }
		void clbkUpdate () override { if (!Fw (X_Update, 0, false)) ExternMFD::clbkUpdate (); }
		void clbkRefreshDisplay (SURFHANDLE hSurf) override { if (!Fw (X_RefreshDisplay, WbBridge::Get ().Id (hSurf), true)) ExternMFD::clbkRefreshDisplay (hSurf); }
		void clbkRefreshButtons () override { if (!Fw (X_RefreshButtons, 0, false)) ExternMFD::clbkRefreshButtons (); }
		void clbkFocusChanged (OBJHANDLE hFocus) override { if (!Fw (X_FocusChanged, WbBridge::Get ().Id (hFocus), true)) ExternMFD::clbkFocusChanged (hFocus); }

		// the module's object's method; false: there is none, Orbiter's own runs
		bool Fw (int c, uint32_t arg, bool hasArg)
		{
			WbBridge &b = WbBridge::Get ();
			Guard g (b.Lock ());
			if (!Live (session)) return false;
			WbWriter &w = b.Begin (H_EMFD); w.u32 (num); w.i32 (c);
			PutMembers (w);
			if (hasArg) w.u32 (arg);
			WbReader r = b.Call ();
			bool there = r.b ();
			if (!there || r.bad) return false;
			if (Alive (this)) GetMembers (r); // unless the module unregistered it meanwhile
			return true;
		}
		static bool Alive (const WbEmfd *p);
	};
	std::unordered_set<const WbEmfd *> &g_emfds = *new std::unordered_set<const WbEmfd *>;
	bool WbEmfd::Alive (const WbEmfd *p) { return g_emfds.count (p) != 0; }

	WbEmfd::~WbEmfd ()
	{
		WbBridge &b = WbBridge::Get ();
		Guard g (b.Lock ());
		g_emfds.erase (this);
		if (!byHelper && Live (session)) { // Orbiter deletes it (oapiUnregisterExternMFD, the pane's end): the module's goes too
			WbWriter &w = b.Begin (H_EMFD); w.u32 (num); w.i32 (X_Delete);
			PutMembers (w);
			b.Call ();
		}
	}

	void ServeEmfd (WbReader &r, WbWriter &w)
	{
		WbBridge &b = WbBridge::Get ();
		int y = r.i32 ();
		if (y == Y_New) {
			uint32_t num = r.u32 ();
			MFDSPEC spec = GetSpec (r);
			if (r.bad) { b.BadArgs (L_Emfd); return; }
			WbEmfd *p = new WbEmfd (spec, num, b.Session ());
			g_emfds.insert (p);
			w.u32 (b.Id (p));
			p->PutMembers (w);
			return;
		}
		WbEmfd *p = (WbEmfd *)b.Ptr (r.u32 ());
		if (r.bad || !g_emfds.count (p)) { b.BadArgs (L_Emfd); return; }
		if (y == Y_Gone) { // the module deleted its object
			p->byHelper = true;
			bool done = p->registered && oapiUnregisterExternMFD (p); // Orbiter deletes it
			if (!done) delete p;
			w.b (true);
			return;
		}
		if (y == Y_Unregister) { w.b (oapiUnregisterExternMFD (p)); return; } // Orbiter deletes it, and the module's with it
		p->GetMembers (r);
		uint32_t u = 0;
		bool res = false, isU = false, isB = false;
		std::string label;
		bool hasLabel = false;
		switch (y) {
		case Y_Register: { MFDSPEC spec = GetSpec (r); if (r.bad) break; p->registered = true; oapiRegisterExternMFD (p, spec); break; }
		case Y_Id: u = (uint32_t)p->ExternMFD::Id (); isU = true; break; // as the mode's msgproc gets it
		case Y_Active: res = p->ExternMFD::Active (); isB = true; break;
		case Y_GetVessel: u = b.Id (p->ExternMFD::GetVessel ()); isU = true; break;
		case Y_DisplaySurface: u = b.Id (p->ExternMFD::GetDisplaySurface ()); isU = true; break;
		case Y_ButtonLabel: { int bt = r.i32 (); if (r.bad) break; const char *s = p->ExternMFD::GetButtonLabel (bt); hasLabel = s != nullptr; if (s) label = s; break; }
		case Y_ProcessButton: { int bt = r.i32 (), ev = r.i32 (); if (!r.bad) res = p->ExternMFD::ProcessButton (bt, ev); isB = true; break; }
		case Y_SendKey: { DWORD k = r.u32 (); if (!r.bad) res = p->ExternMFD::SendKey (k); isB = true; break; }
		case Y_Resize: { MFDSPEC spec = GetSpec (r); if (!r.bad) res = p->ExternMFD::Resize (spec); isB = true; break; }
		case Y_SetMode: { int m = r.i32 (); if (!r.bad) res = p->ExternMFD::SetMode (m); isB = true; break; }
		case Y_OpenModeHelp: res = p->ExternMFD::OpenModeHelp (); isB = true; break;
		case Y_SetVessel: { OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ()); if (!r.bad) p->ExternMFD::SetVessel (h); break; }
		case Y_Update: p->ExternMFD::clbkUpdate (); break;
		case Y_RefreshDisplay: { SURFHANDLE s = (SURFHANDLE)b.Ptr (r.u32 ()); if (!r.bad) p->ExternMFD::clbkRefreshDisplay (s); break; }
		case Y_RefreshButtons: p->ExternMFD::clbkRefreshButtons (); break;
		case Y_FocusChanged: { OBJHANDLE h = (OBJHANDLE)b.Ptr (r.u32 ()); if (!r.bad) p->ExternMFD::clbkFocusChanged (h); break; }
		default: b.BadArgs (L_Emfd); return;
		}
		if (!WbEmfd::Alive (p)) return; // the module unregistered it meanwhile: no members
		p->PutMembers (w);
		if (isU) w.u32 (u);
		if (isB) w.b (res);
		if (y == Y_ButtonLabel) w.str (hasLabel ? label.c_str () : nullptr);
	}
}

// ---------------------------------------------------------------------

bool WbServeMfd (uint32_t fn, WbReader &r, WbWriter &w)
{
	WbBridge &b = WbBridge::Get ();
	switch (fn) {
	case L_RegisterMFDMode: {
		int num = (int)r.u32 ();
		std::string name = Str (r);
		DWORD key = r.u32 ();
		uint32_t vid = r.u32 ();
		if (r.bad) { b.BadArgs (fn); return true; }
		WbMode *m = new WbMode {num, b.Session (), name};
		g_modes.insert (m);
		MFDMODESPECEX spec {};
		spec.name = const_cast<char *> (m->name.c_str ());
		spec.key = key;
		spec.context = m;
		spec.msgproc = MsgProc;
		int id = MFD_NONE;
		if (vid) { if (VESSEL4 *v = Vessel4 (vid)) id = v->RegisterMFDMode (spec); }
		else id = oapiRegisterMFDMode (spec);
		MFDMODESPECEX *os = nullptr; // Orbiter's copy: its key, when another mode had the module's
		if (!vid && oapiGetMFDModeSpecEx (spec.name, &os) != MFD_NONE && os && os->context == m) key = os->key;
		w.i32 (id); w.u32 (key);
		return true; }
	case L_UnregisterMFDMode: {
		int id = r.i32 ();
		uint32_t vid = r.u32 ();
		bool ok = false;
		if (!r.bad) {
			if (vid) { if (VESSEL4 *v = Vessel4 (vid)) ok = v->UnregisterMFDMode (id); }
			else ok = oapiUnregisterMFDMode (id);
		}
		w.b (ok);
		return true; }
	case L_MFDModeSpec: {
		std::string name = Str (r);
		MFDMODESPECEX *s = nullptr;
		int id = r.bad ? MFD_NONE : oapiGetMFDModeSpecEx (const_cast<char *> (name.c_str ()), &s);
		bool found = id != MFD_NONE && s;
		w.i32 (id); w.b (found);
		if (found) {
			const WbMode *m = s->msgproc == MsgProc && g_modes.count ((const WbMode *)s->context) ? (const WbMode *)s->context : nullptr;
			w.str (s->name); w.u32 (s->key); w.b (s->msgproc != nullptr);
			w.u32 (m && m->session == b.Session () ? (uint32_t)m->num : 0);
		}
		return true; }
	case L_Mfd: {
		const WbMfdLink *l = (const WbMfdLink *)b.Ptr (r.u32 ());
		int m = r.i32 ();
		if (r.bad || !g_links.count (l) || !l->mfd) { b.BadArgs (fn); return true; }
		MFD *mfd = l->mfd;
		MFD2 *mfd2 = l->mfd2 ? static_cast<MFD2 *> (mfd) : nullptr;
		switch (m) {
		case B_InvalidateDisplay: mfd->InvalidateDisplay (); break;
		case B_InvalidateButtons: mfd->InvalidateButtons (); break;
		case B_Title: {
			oapi::Sketchpad *skp = WbSkpOf (r.u32 ());
			std::string t = Str (r);
			if (mfd2 && skp && !r.bad) mfd2->Title (skp, t.c_str ());
			break; }
		case B_Pen: { DWORD c = r.u32 (), i = r.u32 (), s = r.u32 (); w.u32 (mfd2 && !r.bad ? WbToolId (mfd2->GetDefaultPen (c, i, s)) : 0); break; }
		case B_Font: { DWORD i = r.u32 (); w.u32 (mfd2 && !r.bad ? WbToolId (mfd2->GetDefaultFont (i)) : 0); break; }
		case B_Colour: { DWORD c = r.u32 (), i = r.u32 (); w.u32 (mfd2 && !r.bad ? mfd2->GetDefaultColour (c, i) : 0); break; }
		default: b.BadArgs (fn); break;
		}
		return true; }
	case L_RegisterModule: {
		uint32_t num = r.u32 ();
		int module = r.i32 ();
		uint32_t over = r.u32 ();
		bool old = r.b ();
		if (r.bad) { b.BadArgs (fn); return true; }
		WbModule *&p = g_moduleObjs[num];
		if (!p) p = new WbModule (WbSlotFor (module), num, module, over, old, b.Session ());
		else { p->over = over; p->old = old; p->gone = false; }
		oapiRegisterModule (p); // during the module's load, Orbiter takes it as the module's
		return true; }
	case L_ModuleGone: {
		auto it = g_moduleObjs.find (r.u32 ());
		if (!r.bad && it != g_moduleObjs.end ()) it->second->gone = true; // Orbiter may hold it until the module is freed
		return true; }
	case L_ModuleDefault: {
		int module = r.i32 (), c = r.i32 ();
		if (!r.bad) ModuleDefault (module, c, r);
		return true; }
	case L_Emfd: ServeEmfd (r, w); return true;
	case L_BroadcastMFDMessage: {
		int mode = r.i32 (), msg = r.i32 ();
		void *data = b.Ptr (r.u32 ()); // a handle of Orbiter's, else null
		w.i32 (r.bad ? 0 : oapiBroadcastMFDMessage (mode, msg, data));
		return true; }
	case L_RegisterPanelMFDGeometry: {
		VESSEL *v = b.Vessel (r.u32 ());
		PANELHANDLE hp = (PANELHANDLE)b.Ptr (r.u32 ());
		int mfd = r.i32 (), nmesh = r.i32 (), ngroup = r.i32 ();
		int res = 0;
		if (!r.bad && v && v->Version () >= 2) res = static_cast<VESSEL3 *> (v)->RegisterPanelMFDGeometry (hp, mfd, nmesh, ngroup);
		w.i32 (res);
		return true; }
	default:
		return false;
	}
}

// the module is freed: Orbiter holds its module objects no more
void WbModuleObjectsFreed (int module)
{
	for (auto it = g_moduleObjs.begin (); it != g_moduleObjs.end (); ) {
		if (it->second->module == module) { delete it->second; it = g_moduleObjs.erase (it); }
		else ++it;
	}
}
