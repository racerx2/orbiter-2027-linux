// not upstream: collision addon, dmg3 area F: flakes, venting cloud, sparks while scraping, dust; visual only, not saved, no random numbers (design-CA-dmg3-F)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "CollFxA.h"
#include "CollDamageA.h"
#include "CollSolve.h"

namespace {

const char *const kKind[CFX_KINDS] = { "sparks", "dust", "glass", "flakes", "vent" };

Vector U (const Vector &v) { double l = v.length (); return l > 1e-9 ? v / l : Vector (); }
double Clamp (double x, double a, double b) { return std::min (b, std::max (a, x)); }
bool Is (const DentMaterial *m, const char *id) { return m && m->id && std::strcmp (m->id, id) == 0; }
bool Pre (const DentMaterial *m, const char *p) { return m && m->id && std::strncmp (m->id, p, std::strlen (p)) == 0; }

}

CollFxA::CollFxA (CollSdk &s, const CollCfgValues &c, CollFxWorld wv) : sdk (s), cfg (c), w (std::move (wv)) {}

double CollFxA::SparkYield (const DentMaterial *m, bool gearOk)
{
	if (Pre (m, "steel_")) return 1.0;
	if (Is (m, "gear")) return gearOk ? 0.6 : 0;
	if (Is (m, "al_honeycomb") || Is (m, "glass")) return 0;
	if (Pre (m, "al_")) return 0.25;
	if (Is (m, "concrete") || Is (m, "building_rc")) return 0.15;
	return 0;
}

double CollFxA::Inten () const { return Clamp (cfg.fxIntensity, 0, 2); }

bool CollFxA::Debris (CollH h)
{
	auto it = debris.find (h);
	if (it != debris.end ()) return it->second;
	std::string c = sdk.ClassName (h);
	for (auto &ch : c) ch = (char)std::tolower ((unsigned char)ch);
	return debris[h] = (c == "colldebris");
}

bool CollFxA::Off (CollH h, bool playback)
{
	return quiet || !h || Inten () <= 0 || (playback && !cfg.fxPlayback) || Debris (h);
}

double CollFxA::Propellant (CollH h)
{
	double m = 0;
	uint32_t nt = sdk.TankCount (h);
	for (uint32_t i = 0; i < nt; i++) { CollH tk = sdk.Tank (h, i); if (tk) m += sdk.TankMass (h, tk); }
	return m;
}

int CollFxA::Live () const
{
	int k = 0;
	for (auto &s : slot) k += s.used;
	return k;
}

int CollFxA::Find (CollH h, uint8_t kind) const
{
	for (int i = 0; i < CFX_SLOTS; i++) if (slot[i].used && slot[i].h == h && slot[i].kind == kind) return i;
	return -1;
}

void CollFxA::LogLine (const CollFxSlot &s, bool del)
{
	if (cfg.logLevel < 2) return;
	char b[512];
	std::string nm = sdk.Name (s.h);
	if (del) std::snprintf (b, sizeof b, "Collision fx t=%.17g '%s' kind=%s del", sdk.SimTime (), nm.c_str (), kKind[s.kind]);
	else std::snprintf (b, sizeof b, "Collision fx t=%.17g '%s' kind=%s lvl=%.3g ps=%d", sdk.SimTime (), nm.c_str (), kKind[s.kind], s.lvl, s.ps ? 1 : 0);
	sdk.Log (2, b);
}

int CollFxA::Create (const Req &r)
{
	n.requests++;
	if (sdk.Warp () > CFX_MAX_WARP) { n.skipped++; return -1; }
	if (made >= CFX_PER_FRAME) { n.capped++; return -1; }
	int cap = std::clamp (cfg.fxMaxStreams, 0, CFX_SLOTS);
	if (Live () >= cap) {
		int v = -1;
		for (int i = 0; i < CFX_SLOTS; i++) {
			const CollFxSlot &s = slot[i];
			if (!s.used || s.kind >= r.kind) continue;
			if (v < 0 || s.kind < slot[v].kind || (s.kind == slot[v].kind && s.t0 < slot[v].t0)) v = i;
		}
		if (v < 0) { n.capped++; return -1; }
		Del (v);
		if (Live () >= cap) { n.capped++; return -1; }
	}
	int i = 0;
	while (i < CFX_SLOTS && slot[i].used) i++;
	if (i >= CFX_SLOTS) { n.capped++; return -1; }
	CollFxSlot &s = slot[i];
	s = CollFxSlot ();
	s.used = true; s.held = r.held; s.fresh = r.held; s.kind = r.kind; s.id = r.id; s.h = r.h;
	s.lvl = r.lvl; s.L0 = r.L0; s.tau = r.tau; s.hold = r.hold; s.t0 = s.tHit = r.t0; s.power = r.power; s.c = r.pos; s.dir = r.dir;
	s.ps = sdk.FxAdd (r.h, r.s, r.pos, r.dir, &s.lvl);
	if (s.ps) n.streams++; else n.nulls++;
	made++;
	LogLine (s, false);
	return i;
}

void CollFxA::Del (int i)
{
	CollFxSlot &s = slot[i];
	if (!s.used) return;
	s.lvl = 0;
	if (s.ps) {
		if (sdk.FxDel (s.h, s.ps)) n.dels++;
		else {
			n.stale++;
			if (!loggedStale) {
				loggedStale = true;
				char b[300];
				std::snprintf (b, sizeof b, "Collision fx: stale particle stream of '%s' (the module cleared its streams); counted, not deleted", sdk.Name (s.h).c_str ());
				sdk.Log (1, b);
			}
		}
	}
	LogLine (s, true);
	s = CollFxSlot ();
}

void CollFxA::Flakes (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double N, double v0, double t, double S, bool air)
{
	int i = Find (h, CFX_FLAKES);
	if (i >= 0) { if (slot[i].power >= N) return; Del (i); }
	Req r;
	r.kind = CFX_FLAKES; r.id = id; r.h = h; r.pos = pos; r.dir = dir; r.t0 = t; r.power = N;
	r.lvl = r.L0 = 1; r.hold = 0.5;
	r.s.size = Clamp (0.03 * S, 0.25, 0.8); r.s.rate = N / 0.4; r.s.v0 = v0; r.s.spread = 0.8;
	r.s.life = air ? 3 : 8; r.s.grow = 0; r.s.slow = 2.5;
	r.s.ltype = CollSdk::FX_DIFFUSE; r.s.lmap = CollSdk::FX_LVL_FLAT; r.s.lmin = r.s.lmax = 1; r.s.amin = r.s.amax = 1; r.s.tex = CollSdk::FX_TEX_FLAKE;
	Create (r);
}

void CollFxA::Glass (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double t, double S, bool air)
{
	double L = std::min (1.0, Inten ());
	if (L < CFX_LVL_MIN || Find (h, CFX_GLASS) >= 0) return;
	Req r;
	r.kind = CFX_GLASS; r.id = id; r.h = h; r.pos = pos; r.dir = dir; r.t0 = t; r.power = L;
	r.lvl = r.L0 = L; r.hold = 0.35;
	r.s.size = Clamp (0.02 * S, 0.1, 0.3); r.s.rate = 60; r.s.v0 = 6; r.s.spread = 1.0;
	r.s.life = air ? 1.0 : 2.0; r.s.grow = 0; r.s.slow = 2.5;
	r.s.ltype = CollSdk::FX_EMISSIVE; r.s.lmap = CollSdk::FX_LVL_LIN; r.s.amin = r.s.amax = 1; r.s.tex = CollSdk::FX_TEX_FLAKE;
	Create (r);
}

void CollFxA::Vent (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double L0, double t, double S, bool air)
{
	if (L0 < CFX_LVL_MIN || Propellant (h) <= 1) return;
	int i = Find (h, CFX_VENT);
	if (i >= 0) {
		CollFxSlot &s = slot[i];
		s.L0 = std::max (s.L0 * std::exp (-(t - s.t0) / s.tau), L0);
		s.t0 = t; s.lvl = s.L0;
		return;
	}
	Req r;
	r.kind = CFX_VENT; r.id = id; r.h = h; r.pos = pos; r.dir = dir; r.t0 = t; r.power = L0;
	r.lvl = r.L0 = L0; r.tau = 6;
	r.s.size = Clamp (0.1 * S, 0.3, 3); r.s.rate = 15; r.s.v0 = 8; r.s.spread = 0.6;
	r.s.life = air ? 5 : 2.5; r.s.grow = air ? 1 : 3; r.s.slow = 1.5;
	r.s.ltype = CollSdk::FX_DIFFUSE; r.s.lmap = CollSdk::FX_LVL_LIN; r.s.amin = r.s.amax = 1; r.s.tex = CollSdk::FX_TEX_VENT;
	Create (r);
}

void CollFxA::Held (uint8_t kind, uint32_t id, CollH h, const Vector &pos, const Vector &dir, double L, double t, double S, const CollSdk::FxSpec &sp)
{
	int i = Find (h, kind);
	if (i >= 0) {
		CollFxSlot &s = slot[i];
		bool moved = ((dir & s.dir) < std::cos (Rad (30)) || (pos - s.c).length () > 0.25 * S);
		if (!s.held) return;                                  // a burst runs out first
		if (!moved) {
			s.L0 = s.fresh ? std::max (s.L0, L) : L;
			s.fresh = true; s.lvl = s.L0;
			return;
		}
		if (s.fresh && s.L0 >= L) return;                    // a stronger contact of this frame keeps the stream
		Del (i);
	}
	Req r;
	r.kind = kind; r.id = id; r.h = h; r.pos = pos; r.dir = dir; r.t0 = t; r.power = L; r.held = true;
	r.lvl = r.L0 = L; r.s = sp;
	if (kind == CFX_SPARKS) { r.tau = 0.1; r.hold = 0.3; } else { r.tau = 0.3; r.hold = 0.9; }
	Create (r);
}

CollSdk::FxSpec CollFxA::DustSpec (double v, double S, bool air)
{
	CollSdk::FxSpec s;
	s.size = Clamp (0.08 * S, 0.5, 4); s.rate = 10; s.v0 = Clamp (0.3 * v, 1, 10); s.spread = 1.0;
	if (air) { s.life = 6; s.grow = 1.5; s.slow = 3; } else { s.life = 2.5; s.grow = 0; s.slow = 0; }
	s.ltype = CollSdk::FX_DIFFUSE; s.lmap = CollSdk::FX_LVL_LIN; s.amin = s.amax = 1; s.tex = CollSdk::FX_TEX_DUST;
	return s;
}

void CollFxA::DustBurst (uint32_t id, CollH h, const Vector &pos, const Vector &dir, double v, double t, double S, bool air)
{
	double L = std::min (1.0, v / 20) * Inten ();
	if (L < CFX_LVL_MIN) return;
	int i = Find (h, CFX_DUST);
	if (i >= 0) { if (!slot[i].held && slot[i].power >= L) return; Del (i); }
	Req r;
	r.kind = CFX_DUST; r.id = id; r.h = h; r.pos = pos; r.dir = dir; r.t0 = t; r.power = L;
	r.lvl = r.L0 = L; r.hold = 0.6; r.s = DustSpec (v, S, air);
	Create (r);
}

// hooks

void CollFxA::Hit (const CollDamageHit &x)
{
	if (Off (x.h, x.playback)) return;
	last[x.id] = { x.c, x.n };
	bool fl = (cfg.fxMask & 1) && x.vn >= 3 && x.E >= 2000;
	double e = x.eSpec + (x.E > 0 ? x.Esurplus * x.eSpec / x.E : 0); // surplus per kg of the same mass
	bool vt = (cfg.fxMask & 2) && e >= cfg.fxVentSpec;
	if (!fl && !vt) return;
	double S = sdk.Size (x.h), I = Inten ();
	bool air = sdk.FxAtm (x.h) > 1e-3;
	Vector pos = x.c - x.n * x.depth + x.n * (0.05 * S);
	if (fl) {
		double k = std::min (1.0, 0.5 * x.vt / std::max (x.vn, 0.1));
		Vector d = U (x.n - x.tdir * k);
		Flakes (x.id, x.h, pos, d.length () > 0 ? d : x.n, Clamp (I * x.E / 2000, 3, 25), Clamp (0.25 * x.vn, 1, 25), x.simt, S, air);
	}
	if (vt) Vent (x.id, x.h, pos, x.n, Clamp (I * e / 600, 0.3, 1), x.simt, S, air);
}

void CollFxA::Break (const CollBreakEvent &e)
{
	if (e.kind != CBRK_GLASS && e.kind != CBRK_PART) return;   // interior: nothing
	CollH h = w.handle ? w.handle (e.id) : nullptr;
	if (Off (h, e.playback)) return;
	double S = sdk.Size (h), I = Inten (), t = sdk.SimTime ();
	bool air = sdk.FxAtm (h) > 1e-3;
	Vector pos = e.c + e.n * (0.05 * S);
	if (e.kind == CBRK_GLASS) { if (cfg.fxMask & 1) Glass (e.id, h, pos, e.n, t, S, air); return; }
	if (cfg.fxMask & 1) Flakes (e.id, h, pos, e.n, Clamp (I * 10 * e.r, 3, 25), 5, t, S, air);
	if (cfg.fxMask & 2) Vent (e.id, h, pos, e.n, Clamp (0.8 * I, 0.3, 1), t, S, air);
}

void CollFxA::Destroyed (uint32_t id)
{
	if (!(cfg.fxMask & 2)) return;
	CollH h = w.handle ? w.handle (id) : nullptr;
	if (!h || Off (h, sdk.Playback (h))) return;
	auto it = last.find (id);
	Vector c, nn (0, 1, 0);
	if (it != last.end ()) c = it->second.c, nn = it->second.n;
	double S = sdk.Size (h);
	Vent (id, h, c + nn * (0.05 * S), nn, Clamp (Inten (), 0.3, 1), sdk.SimTime (), S, sdk.FxAtm (h) > 1e-3);
}

void CollFxA::Contact (const CollFxContact &x)
{
	if (!(cfg.fxMask & (4 | 8)) || Off (x.h, x.playback)) return;
	double t = sdk.SimTime ();
	if ((cfg.fxMask & 4) && x.vt >= 3 && x.dt > 0) {
		double P = std::fabs (x.Jt) * x.vt / x.dt;
		bool gearOk = x.vn > 3 || (w.destroyed && w.destroyed (x.id));
		double y = std::max (SparkYield (x.mat, gearOk), SparkYield (x.matOther, gearOk));
		if (P >= 5000 && y > 0) {
			double L = Inten () * Clamp (0.3 + 0.5 * std::log10 (P / 5000), 0, 1);
			if (L >= CFX_LVL_MIN) {
				double S = sdk.Size (x.h);
				bool air = sdk.FxAtm (x.h) > 1e-3;
				CollSdk::FxSpec s;
				s.size = Clamp (0.004 * S, 0.04, 0.12); s.rate = (air ? 60 : 25) * y * (air ? 1 : 0.3); s.v0 = Clamp (0.5 * x.vt, 2, 10); s.spread = 0.6;
				s.life = air ? 0.25 : 0.5; s.grow = 0; s.slow = 1;
				s.ltype = CollSdk::FX_EMISSIVE; s.lmap = CollSdk::FX_LVL_LIN; s.amin = s.amax = 1; s.tex = CollSdk::FX_TEX_SPARK;
				Vector d = U (x.n * 0.3 - x.tdir);
				Held (CFX_SPARKS, x.id, x.h, x.c + x.n * (0.05 * S), d.length () > 0 ? d : x.n, L, t, S, s);
			}
		}
	}
	if ((cfg.fxMask & 8) && x.building && (Is (x.matOther, "concrete") || Is (x.matOther, "building_rc")) && x.nOther.y >= 0.7) {
		bool dest = w.destroyed && w.destroyed (x.id);
		double S = sdk.Size (x.h);
		bool air = sdk.FxAtm (x.h) > 1e-3;
		Vector up = -x.n, d = U (up * -0.4 - x.tdir);
		if (d.length () <= 0) d = -up;
		if ((x.flags & COLLEV_FIRST) && (x.vn >= 5 || (dest && x.vn >= 2))) DustBurst (x.id, x.h, x.c, d, x.vn, t, S, air);
		else if (x.vt >= 2 && (dest || cfg.fxRollDust)) {
			double L = std::min (1.0, x.vt / 20) * Inten ();
			if (L >= CFX_LVL_MIN) Held (CFX_DUST, x.id, x.h, x.c, d, L, t, S, DustSpec (x.vt, S, air));
		}
	}
}

void CollFxA::GroundPass (double simt)
{
	std::vector<std::pair<uint32_t, CollH>> wr;
	if (w.wrecks) w.wrecks (wr);
	uint32_t nv = sdk.VesselCount ();
	for (uint32_t k = 0; k < nv; k++) {
		CollH h = sdk.Vessel (k);
		if (!h || Debris (h)) continue;
		Vector vLoc, up; double alt = 0;
		bool g = sdk.FxGround (h, vLoc, up, alt);
		Ground &st = ground[h];
		bool was = st.was; double vd = st.vd;
		st.was = g; st.vd = -(vLoc & up);
		if (!g || (!cfg.fxPlayback && sdk.Playback (h))) continue;
		uint32_t id = 0; bool dest = false;
		for (auto &p : wr) if (p.second == h) { id = p.first; dest = true; }
		Vector hv = vLoc - up * (vLoc & up);
		double vh = hv.length (), S = 0;
		bool burst = !was && (vd >= 5 || (dest && vd >= 2));
		bool scrape = vh >= 2 && (dest || cfg.fxRollDust);
		if (!burst && !scrape) continue;
		S = sdk.Size (h);
		bool air = sdk.FxAtm (h) > 1e-3;
		Vector tdir = U (vLoc), pos = up * -alt, d = U (up * -0.4 - tdir);
		if (burst) DustBurst (id, h, pos, d, vd, simt, S, air);
		else {
			double L = std::min (1.0, vh / 20) * Inten ();
			if (L >= CFX_LVL_MIN) Held (CFX_DUST, id, h, pos, d, L, simt, S, DustSpec (vh, S, air));
		}
	}
}

void CollFxA::Post (double simt, double)
{
	if (quiet) return;
	made = 0;
	for (int i = 0; i < CFX_SLOTS; i++) {
		CollFxSlot &s = slot[i];
		if (!s.used) continue;
		if (s.held) {
			if (s.fresh) { s.fresh = false; s.tHit = simt; s.lvl = s.L0; continue; }
			double age = simt - s.tHit;
			s.lvl = s.L0 * std::exp (-age / s.tau);
			if (age > s.hold || s.lvl < CFX_LVL_MIN) Del (i);
		} else if (s.kind == CFX_VENT) {
			s.lvl = s.L0 * std::exp (-(simt - s.t0) / s.tau);
			if (s.lvl < CFX_LVL_MIN) Del (i);
		} else if (simt - s.t0 >= s.hold) Del (i);
	}
	if ((cfg.fxMask & 8) && Inten () > 0) GroundPass (simt);
}

void CollFxA::DropVessel (uint32_t id, CollH h)
{
	for (int i = 0; i < CFX_SLOTS; i++) if (slot[i].used && ((h && slot[i].h == h) || (id && slot[i].id == id))) Del (i);
	if (h) { ground.erase (h); debris.erase (h); }
	last.erase (id);
}

void CollFxA::TimeJump ()
{
	for (int i = 0; i < CFX_SLOTS; i++) Del (i);
	ground.clear ();
}

void CollFxA::End ()
{
	for (auto &s : slot) s = CollFxSlot ();                   // the scene is gone: forget the handles, no SDK call
	ground.clear (); debris.clear (); last.clear ();
}

void CollFxA::Quiet ()
{
	for (auto &s : slot) s.lvl = 0;
	quiet = true;
}

std::unique_ptr<CollDmgSink> CollMakeFx (CollSdk &sdk, CollDmgSession &s, const CollCfgValues &cfg)
{
	CollDmgSession *p = &s;
	CollFxWorld w;
	w.handle = [p] (uint32_t id) { return p->VesselHandle (id); };
	w.destroyed = [p] (uint32_t id) { const VesselDamageA *d = p->Damage (id); return d && (d->d.flags & XDMG_DESTROYED); };
	w.wrecks = [p] (std::vector<std::pair<uint32_t, CollH>> &out) {
		out.clear ();
		for (const auto &kv : p->Vessels ()) if (kv.second.d.flags & XDMG_DESTROYED) { CollH h = p->VesselHandle (kv.first); if (h) out.push_back ({ kv.first, h }); }
	};
	return std::make_unique<CollFxA> (sdk, cfg, std::move (w));
}
