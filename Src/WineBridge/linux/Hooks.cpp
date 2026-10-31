// WineBridge: Orbiter's callbacks of Linux vessels whose class a Windows module hooked in the helper (vessel hooks). Orbiter asks
// wbVesselIntf for the object to call a vessel's callbacks on; for a hooked class it is a WbVessel that sends the hooked ones to the
// vessel's object in the helper (whose class vtable the module patched) and the others to the vessel's own.
#include "Orbitersdk.h"
#include "Bridge.h"
#include "WbVessel.h"
#include "Manual.h"
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

using Guard = std::lock_guard<std::recursive_mutex>;

namespace {
	// a Linux vessel class by its vtable: the callbacks hooked in the helper's copy of it
	struct Class { uint32_t mask[2] {}; int vessels = 0; bool helper = false; std::string name; };
	struct Rec { VESSEL *own = nullptr; uint32_t cls = 0; bool proxy = false; std::unique_ptr<WbVessel> hook; };

	// never destroyed: Orbiter may destroy vessels at exit, after static objects are gone
	std::unordered_map<const void *, Rec> &g_rec = *new std::unordered_map<const void *, Rec>; // Linux vessels (not bridged ones)
	std::unordered_map<const void *, uint32_t> &g_no = *new std::unordered_map<const void *, uint32_t>; // class numbers by vtable
	std::deque<Class> &g_cls = *new std::deque<Class> (1); // by number; 0: none
	std::atomic<bool> g_hooked {false}; // a class has a hooked callback

	bool Bridged (VESSEL *own) { return dynamic_cast<WbVessel *> (static_cast<VESSEL2 *> (own)) != nullptr; }

	uint32_t ClassOf (VESSEL *own)
	{
		const void *vt = *(const void *const *)static_cast<VESSEL2 *> (own);
		auto it = g_no.find (vt);
		if (it != g_no.end ()) return it->second;
		uint32_t no = (uint32_t)g_cls.size ();
		g_cls.emplace_back ();
		const char *n = own->GetClassName ();
		g_cls.back ().name = n && *n ? n : own->GetName ();
		g_no[vt] = no;
		return no;
	}

	Rec *Register (const void *h, VESSEL *own)
	{
		if (!own || own->Version () < 1 || Bridged (own)) return nullptr; // a bridged vessel's object in the helper is the module's
		auto it = g_rec.try_emplace (h);
		Rec &r = it.first->second;
		if (it.second) { r.own = own; r.cls = ClassOf (own); g_cls[r.cls].vessels++; }
		return &r;
	}

	void Recount ()
	{
		bool any = false;
		for (const Class &c : g_cls) if (c.mask[0] || c.mask[1]) any = true;
		g_hooked = any;
	}
}

DLLCLBK void wbVesselNew (void *hVessel, VESSEL *own)
{
	Guard g (WbBridge::Get ().Lock ());
	Register (hVessel, own);
}

DLLCLBK void wbVesselGone (void *hVessel)
{
	WbBridge &b = WbBridge::Get ();
	Guard g (b.Lock ());
	WbXrsVesselGone (hVessel);
	auto it = g_rec.find (hVessel);
	if (it == g_rec.end ()) return;
	Rec rec = std::move (it->second);
	g_rec.erase (it);
	Class &c = g_cls[rec.cls];
	bool last = --c.vessels == 0, copy = last && c.helper; // its module may be unloaded now: the helper's copy of its vtable goes, as the DLL's would
	if (b.Alive () && (rec.proxy || copy)) {
		WbWriter &w = b.Begin (H_VESSELGONE);
		w.u32 (rec.proxy ? b.Id (hVessel) : 0); w.u32 (copy ? rec.cls : 0);
		b.Call ();
	}
	if (last) {
		c.helper = false;
		c.mask[0] = c.mask[1] = 0;
		Recount ();
	}
}

DLLCLBK VESSEL *wbVesselIntf (void *hVessel, VESSEL *own)
{
	if (!g_hooked) return own;
	Guard g (WbBridge::Get ().Lock ());
	auto it = g_rec.find (hVessel);
	if (it == g_rec.end ()) return own;
	Rec &r = it->second;
	const Class &c = g_cls[r.cls];
	if (!c.mask[0] && !c.mask[1]) return own;
	if (!r.hook) r.hook.reset (new WbVessel ((OBJHANDLE)hVessel, own, c.mask));
	return r.hook.get ();
}

uint32_t WbHookClassOf (const void *hVessel, VESSEL *own)
{
	Rec *r = Register (hVessel, own);
	if (!r) return 0;
	r->proxy = true; // the helper makes its object now
	g_cls[r->cls].helper = true;
	return r->cls;
}

bool WbHookProxy (const void *hVessel)
{
	auto it = g_rec.find (hVessel);
	if (it == g_rec.end ()) return false;
	Rec &r = it->second;
	if (r.proxy) return true;
	WbBridge &b = WbBridge::Get ();
	b.Begin (H_PROXY).u32 (b.Id (hVessel));
	WbReader rd = b.Call ();
	bool ok = rd.b ();
	r.proxy = !rd.bad && ok;
	return r.proxy;
}

void WbHooksApply (WbReader &r)
{
	WbBridge &b = WbBridge::Get ();
	uint32_t n = r.u32 ();
	for (uint32_t i = 0; i < n && !r.bad; i++) {
		uint32_t vid = r.u32 (), m0 = r.u32 (), m1 = r.u32 ();
		if (WbVessel *v = r.bad ? nullptr : b.Bridged (vid)) v->SetMask (m0, m1);
	}
	n = r.u32 ();
	for (uint32_t i = 0; i < n && !r.bad; i++) {
		uint32_t no = r.u32 (), m0 = r.u32 (), m1 = r.u32 ();
		if (r.bad || !no || no >= g_cls.size ()) continue;
		Class &c = g_cls[no];
		if (c.mask[0] == m0 && c.mask[1] == m1) continue;
		c.mask[0] = m0; c.mask[1] = m1;
		std::string list;
		for (int k = 0; k < C_COUNT; k++)
			if (WbOverridden (c.mask, k)) list += std::string (list.empty () ? "" : ", ") + WbClbkName (k);
		oapiWriteLogV ("WineBridge: callbacks of %s a Windows module hooked: %s", c.name.c_str (), list.empty () ? "none now" : list.c_str ());
	}
	if (r.bad && b.Alive ()) oapiWriteLogV ("WineBridge: the helper's list of hooked callbacks is cut short");
	Recount ();
}

void WbHooksStopped ()
{
	for (Class &c : g_cls) { c.helper = false; c.mask[0] = c.mask[1] = 0; }
	for (auto &e : g_rec) e.second.proxy = false;
	g_hooked = false;
}
