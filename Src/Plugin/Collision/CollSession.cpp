// not upstream: collision addon, the session object and its E1-E3 parts in the stage order of Design CA E4 11.2-11.4
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include "CollSession.h"
#include "CollBaseA.h"
#include "CollDamageA.h"
#include "CollDmgHost.h"
#include "CollSdkOrbiter.h"
#include "CollSourceA.h"
#include "CollWorldA.h"

namespace {

double Us (CollSession::Clock::duration d) { return std::chrono::duration<double, std::micro> (d).count (); }

Matrix QM (const Quaternion &q) { Matrix R; R.Set (q); return R; }

Vector Unit (const Vector &v) { double l = v.length (); return l > 0 ? v / l : v; }

CollOwnerKey VKey (uint32_t id) { return CollOwnerKey { COLLO_VESSEL, id, -1, -1, -1, -1 }; }

// E1's view of E2 (E1 4.3, 8.1): parts and poses, assemblies, base bodies, docking zones
class PhysGeomA : public CollPhysGeom {
public:
	PhysGeomA (CollGeomSession &g, CollSdk &s, std::function<CollH (uint32_t)> v) : geom (g), sdk (s), vessel (std::move (v)) {}
	std::vector<CollH> live;                       // this frame's vessels, for the base distance filter
	struct PlanetV { CollH h; double t; Vector v, a; };
	std::vector<PlanetV> pvel;                     // planet velocity at the last pre-step: its acceleration by difference (E1 4.3)
	bool Parts (uint32_t id, std::vector<CollPartRef> &fwd, std::vector<CollPartRef> &past, double &rmax) override
	{
		fwd.clear (); past.clear (); rmax = 0;
		const CollVesselGeom *vg = geom.Geom (id);
		if (!vg || !vg->shape) return false;
		const CollShape &sh = *vg->shape;
		for (uint32_t k = 0; k < sh.nPart (); k++) {
			const CollPart &p = sh.Part (k);
			CollPartRef r {};
			r.geom = &p.Geom ();
			r.skin = r.geom->skin;
			r.owner = VKey (id);
			r.partKey = (p.mesh << 16) | (p.rep & 0xFFFF);
			r.version = p.version;
			r.mesh = (uint16_t)p.mesh;
			r.mask = sh.GroupMask (k);
			r.P0 = p.pose[1];
			r.P1 = k < vg->next.size () ? vg->next[k] : p.pose[1];
			fwd.push_back (r);
			r.P0 = p.pose[0];
			r.P1 = p.pose[1];
			past.push_back (r);
			double s = std::max (p.sc[0].length () + p.sr[0], p.sc[1].length () + p.sr[1]);
			if (k < vg->nextSc.size ()) s = std::max (s, vg->nextSc[k].length () + vg->nextSr[k]);
			rmax = std::max (rmax, s + r.skin);
		}
		return !fwd.empty ();
	}
	void Assemblies (std::vector<CollPhysAsm> &out) override
	{
		out.clear ();
		for (const CollAssembly &a : geom.Assemblies ()) {
			CollPhysAsm x;
			x.member = a.member; x.root = a.root; x.memberHash = a.memberHash;
			x.stack = (a.flags & ASM_STACK) != 0; x.mixed = (a.flags & ASM_MIXED) != 0;
			out.push_back (x);
		}
	}
	void Bases (double h, std::vector<CollABody> &out) override
	{
		out.clear ();
		if (!geom.bases) return;
		std::vector<Vector> pos;
		for (CollH v : live) { Vector x, u; Matrix R; sdk.GlobalState (v, x, u, R); pos.push_back (x); }
		for (const auto &rp : geom.bases->rec) {
			const CollBaseRec &r = *rp;
			const CollBaseShape &sh = r.shape;
			if (!sh.nObj () || !r.hPlanet) continue;
			Vector pp, pv; Matrix pR;
			sdk.GlobalState (r.hPlanet, pp, pv, pR);
			Vector ap = PlanetAcc (r.hPlanet, pv, h);
			Vector xg = pp + mul (pR, r.rposP);
			bool inRange = false; // not "near": windows.h defines it as an empty macro
			for (const Vector &x : pos) inRange = inRange || (x - xg).length () < sh.rmax + 5000.0;
			if (!inRange) continue;
			Matrix Rg = pR * r.rrotP;
			double T = sdk.PlanetPeriod (r.hPlanet);
			Vector w = std::fabs (T) > 0 ? mul (pR, Vector (0, 1, 0)) * (2.0 * 3.14159265358979323846 / T) : Vector ();
			CollABody B;
			B.kind = COLLB_BASE;
			B.id = ((uint32_t)(r.planetIdx & 0xFFFF) << 16) | (uint32_t)(r.baseIdx & 0xFFFF);
			B.memberHash = B.id;
			B.m = 0;
			B.planet = r.planetIdx;
			B.x = xg; B.v = CollSurfaceVel (pp, pv, w, xg); B.q.Set (Rg); B.wb = tmul (Rg, w);
			B.rmax = sh.rmax + COLL_SKIN_MAX;
			CollKinMotion (B, CollSurfaceAcc (pp, ap, w, xg), w, h);
			for (uint32_t i = 0; i < sh.nObj (); i++) {
				const CollBaseObj &o = sh.Obj (i);
				CollPartRef p {};
				p.geom = &o.geom; p.skin = o.geom.skin;
				p.owner = CollOwnerKey { COLLO_BUILDING, 0, r.planetIdx, r.baseIdx, (int32_t)o.obj, 0 };
				p.partKey = 0; p.version = sh.version; p.mesh = (uint16_t)o.obj; p.mask = nullptr;
				B.parts.push_back (p);
			}
			out.push_back (B);
		}
	}
	Vector PlanetAcc (CollH p, const Vector &v, double h)
	{
		double t = sdk.SimTime ();
		for (PlanetV &x : pvel)
			if (x.h == p) {
				if (t > x.t) { x.a = t - x.t <= 2.5*h ? (v - x.v)/(t - x.t) : Vector (); x.t = t; x.v = v; }
				return x.a;
			}
		pvel.push_back (PlanetV { p, t, v, Vector () });
		return Vector ();
	}
	void Zones (const std::vector<CollABody> &b, double hs, std::vector<CollZone> &out) override // docking zones (E1 8.1); attachment zones are not wired
	{
		out.clear ();
		struct Port { int body; uint32_t id; CollPortAt at[2]; Vector cb; double rdz; };
		std::vector<Port> ports;
		const auto &pr = geom.Ports ();
		for (int i = 0; i < (int)b.size (); i++) {
			const CollABody &B = b[i];
			if (B.kind == COLLB_BASE) continue;
			Matrix Rb = QM (B.q);
			Vector wg = mul (Rb, B.wb);
			for (const CollPortRec &p : pr) {
				if (p.mate || std::find (B.member.begin (), B.member.end (), p.vessel) == B.member.end ()) continue;
				CollH h = vessel (p.vessel);
				if (!h) continue;
				Vector x, u; Matrix R;
				sdk.GlobalState (h, x, u, R);
				Port q;
				q.body = i; q.id = p.vessel;
				Vector g = x + mul (R, p.pos);
				q.cb = tmul (Rb, g - B.x);
				q.at[0].g = g; q.at[0].d = mul (R, p.dir); q.at[0].r = mul (R, p.rot); q.at[0].v = B.v + crossp (g - B.x, wg);
				q.at[1] = q.at[0];
				q.at[1].g = g + q.at[0].v * hs;
				q.rdz = geom.Keys (p.vessel).dockZoneRadius;
				ports.push_back (q);
			}
		}
		for (size_t a = 0; a < ports.size (); a++)
			for (size_t c = a + 1; c < ports.size (); c++) {
				const Port &A = ports[a], &C = ports[c];
				if (A.body == C.body) continue;
				double rdz = std::max (A.rdz, C.rdz);
				bool on = false;
				for (int t = 0; t < 2 && !on; t++) on = CollDockZoneActive (A.at[t], C.at[t], rdz, 15.0, 1.0);
				if (!on) continue;
				out.push_back (CollZone { A.body, C.body, VKey (A.id), VKey (C.id), A.cb, C.cb, rdz, rdz });
			}
	}
	CollH BaseHandle (int planet, int base) override
	{
		const CollBaseRec *r = geom.bases ? geom.BaseRec (planet, base) : nullptr;
		return r ? r->hBase : nullptr;
	}
	void PartToVessel (uint32_t id, int mesh, int grp, Vector &p, Vector &n) override
	{
		const CollVesselGeom *vg = geom.Geom (id);
		if (!vg || !vg->shape || mesh < 0) return;
		const CollShape &sh = *vg->shape;
		int k = grp >= 0 ? sh.PartOf ((uint32_t)mesh, (uint32_t)grp) : -1;
		for (uint32_t i = 0; k < 0 && i < sh.nPart (); i++) if (sh.Part (i).mesh == (uint32_t)mesh) k = (int)i;
		if (k < 0) return;
		const CollAffine &P = sh.Part ((uint32_t)k).pose[1];
		p = CollApply (P, p);
		n = Unit (CollApplyDir (P, n));
	}
private:
	CollGeomSession &geom;
	CollSdk &sdk;
	std::function<CollH (uint32_t)> vessel;
};

// the solver's lookups (CollSolve.h CollSolveHost): default materials, the render feature of a hit point for E3's dents
class SolveHostA : public CollSolveHost {
public:
	SolveHostA (CollGeomSession &g) : geom (g) {}
	CollPhysSession *phys = nullptr;
	CollSMat Material (const CollPairResult &, int, int) override { return CollSMat (); }
	void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) override
	{
		const CollDetect *d = phys ? phys->frame.FeatDet () : nullptr; // the detector whose results are being filled: never another frame's bodies
		if (!d) return;
		uint32_t tri = side ? r.pt[i].triB : r.pt[i].triA;
		uint16_t pi = side ? r.pt[i].partB : r.pt[i].partA;
		int bi = side ? r.bodyB : r.bodyA;
		if (bi < 0 || bi >= d->nBody ()) return;
		const CollBody &B = d->Body (bi);
		if (pi >= B.parts.size ()) return;
		const CollPartRef *pr = &B.parts[pi];
		CollOwnerRef o = CollOwnerRefOf (pr->owner);
		if (o.vesselId != s.owner.vesselId || o.planet != s.owner.planet || o.base != s.owner.base || o.obj != s.owner.obj) return;
		if (!pr->geom || tri >= pr->geom->tri.size ()) return;
		s.mesh = pr->mesh;
		s.tri = (int)tri;
		if (s.owner.vesselId == 0) return;
		const CollVesselGeom *vg = geom.Geom (s.owner.vesselId);
		if (!vg || !vg->shape) return;
		const CollShape &sh = *vg->shape;
		for (uint32_t k = 0; k < sh.nPart (); k++) {
			if (&sh.Part (k).Geom () != pr->geom) continue;
			uint32_t m = 0, g = 0, ot = 0;
			if (sh.RenderFeature (k, tri, m, g, ot)) {
				s.mesh = (int)m;
				s.grp = g == ~0u ? -1 : (int)g;
				s.tri = ot == ~0u ? -1 : (int)ot;
			}
			break;
		}
	}
private:
	CollGeomSession &geom;
};

class ShapeSinkA : public CollShapeSink {
public:
	explicit ShapeSinkA (CollDmgSession &d) : dmg (d) {}
	void ShapesUpdated (uint32_t id, CollShape *shape, const std::vector<CollSlotEvent> &ev) override { dmg.ShapesUpdated (id, shape, ev); }
private:
	CollDmgSession &dmg;
};

}

CollSession::CollSession (uint32_t s, const CollCfgValues &c) : serial (s), cfg (c), vessel (1, nullptr)
{
	sdk = CollSdkOrbiterCreate (false);
	sdk->logLevel = cfg.logLevel;
	geom = std::make_unique<CollGeomSession> (*sdk, cfg);
	geom->idOf = [this] (CollH h) { return IdOf ((OBJHANDLE)h); };
	geom->ReadOrbiterCfg ();
	pgeom = std::make_unique<PhysGeomA> (*geom, *sdk, [this] (uint32_t id) { return (CollH)Vessel (id); });
	auto sh = std::make_unique<SolveHostA> (*geom);
	SolveHostA *shp = sh.get ();
	shost = std::move (sh);
	phys = std::make_unique<CollPhysSession> (*sdk, *pgeom, cfg);
	shp->phys = phys.get ();
	dhost = std::make_unique<CollDmgHostOf<CollGeomSession, CollSession>> (*geom, *this);
	dmg = std::make_unique<CollDmgSession> (*sdk, *dhost, cfg);
	sink = std::make_unique<ShapeSinkA> (*dmg);
}

CollSession::~CollSession () = default;

uint32_t CollSession::IdOf (OBJHANDLE h)
{
	auto it = idOf.find (h);
	if (it != idOf.end ()) return it->second;
	uint32_t id = (uint32_t)vessel.size ();
	vessel.push_back (h);
	idOf.emplace (h, id);
	return id;
}

OBJHANDLE CollSession::Vessel (uint32_t id) const { return id < vessel.size () ? vessel[id] : nullptr; }

void CollSession::AddVessel (uint32_t id, OBJHANDLE h)
{
	if (!Vessel (id)) return; // deleted again inside the same pre-step
	geom->NewVessel (id, (CollH)h);
	phys->OnNewVessel (id);
	dmg->OnNewVessel ((CollH)h);
}

void CollSession::PurgeVessel (uint32_t id)
{
	geom->DeleteVessel (id);
	phys->OnDeleteVessel (id);
	dmg->OnDeleteVessel (id);
}

void CollSession::NewVessel (OBJHANDLE h, bool inStep)
{
	uint32_t id = IdOf (h);
	if (inStep) queued.push_back (Op { false, id, h });
	else AddVessel (id, h);
}

void CollSession::DeleteVessel (OBJHANDLE h, bool inStep)
{
	auto it = idOf.find (h);
	if (it == idOf.end ()) return;
	uint32_t id = it->second;
	vessel[id] = nullptr;
	idOf.erase (it);
	if (inStep) queued.push_back (Op { true, id, h });
	else PurgeVessel (id);
}

void CollSession::ApplyQueued ()
{
	std::vector<Op> q;
	q.swap (queued);
	for (const Op &o : q) {
		if (o.del) PurgeVessel (o.id);
		else AddVessel (o.id, o.h);
	}
}

void CollSession::Load (FILEHANDLE scn)
{
	CollStoreBlock b;
	if (CollDmgSession::Parse (*sdk, (CollH)scn, b)) pending = std::move (b);
}

void CollSession::Save (FILEHANDLE scn)
{
	if (started) { dmg->Save ((CollH)scn); return; }
	std::vector<std::string> out { "COLLA " + std::to_string (COLL_STORE_VERSION) }; // before the start: the pending block as read
	for (const CollStoreVessel &v : pending.vessel)
		for (size_t i = 0; i < v.raw.size (); i++) {
			std::string s = (i == 0 || i + 1 == v.raw.size ()) ? v.raw[i] : "  " + v.raw[i];
			if (CollStore::Line200 (s)) out.push_back (s);
		}
	DentMath::FormatBases (pending.base, out);
	for (const std::string &u : pending.unknown) out.push_back (u);
	for (const std::string &l : out) sdk->ScnWrite ((CollH)scn, l);
}

void CollSession::Start (int renderMode)
{
	Clock::time_point b = Clock::now ();
	uint32_t nv = CollVesselCount ();
	for (uint32_t i = 0; i < nv; i++) IdOf (CollVesselByIndex (i));
	CollSdkOrbiterBindClient (*sdk);
	geom->SimulationStart (renderMode, renderMode != 0);
	phys->Start ([this] (const char *k, std::string &v) { return geom->orbCfg.String (k, v); });
	dmg->Begin (std::move (pending));
	pending = CollStoreBlock ();
	started = true;
	t.build = Us (Clock::now () - b);
	CollLogF ("Collision: active version=%s session=%u model=%d response=%d check=%d damage=%d log=%d render=%d",
		COLL_ADDON_VERSION, serial, cfg.model, (int)cfg.response, (int)cfg.check, CollDamageModel (), cfg.logLevel, renderMode);
}

std::string CollSession::Who (const CollOwnerRef &o)
{
	if (o.vesselId) {
		OBJHANDLE h = Vessel (o.vesselId);
		return h ? sdk->Name ((CollH)h) : "#" + std::to_string (o.vesselId);
	}
	const CollBaseObjView *v = geom->bases ? geom->BaseObject (o.planet, o.base, o.obj) : nullptr;
	if (v) return v->planet + ":" + v->base + " " + v->type + " #" + std::to_string (o.obj);
	return "building " + std::to_string (o.planet) + ":" + std::to_string (o.base) + " #" + std::to_string (o.obj);
}

void CollSession::PreStep (double simt, double simdt)
{
	// PS1 BeginFrame: E2 PreStep, E1 snapshot (reads only)
	Clock::time_point a = Clock::now ();
	geom->BeginFrame (simt, simdt);
	Clock::time_point b = Clock::now ();
	std::vector<CollPhysVessel> v;
	PhysGeomA &pg = static_cast<PhysGeomA &> (*pgeom);
	pg.live.clear ();
	for (uint32_t id = 1; id < vessel.size (); id++)
		if (vessel[id]) { v.push_back (CollPhysVessel { id, (CollH)vessel[id] }); pg.live.push_back ((CollH)vessel[id]); }
	phys->PS1Snapshot (simt, simdt, v);
	Clock::time_point c = Clock::now ();
	// PS2 ShapesUpdated, PS2b PrePhysics
	geom->Deliver (*sink);
	Clock::time_point d = Clock::now ();
	dmg->PrePhysics ();
	Clock::time_point e = Clock::now ();
	// PS3 physics and every state write
	phys->PS3Physics (*shost);
	Clock::time_point f = Clock::now ();
	// PS4 damage
	const std::vector<CollImpactEvent> &ev = phys->Events ();
	for (const CollImpactEvent &x : ev) {
		n.events++;
		CollLogF ("Collision impact t=%.6f '%s' '%s' vn=%.4f m/s vsep=%.4f m/s E=%.6g J J=%.6g N s", x.t, Who (x.s[0].owner).c_str (), Who (x.s[1].owner).c_str (),
			x.vn, x.vn_post, x.dKE, x.Jn);
	}
	dmg->Commit (ev, simt);
	Clock::time_point g = Clock::now ();
	// PS5 notices: E1 CONTACT, then E3
	phys->PS5Notices ();
	Clock::time_point h = Clock::now ();
	dmg->SendNotices ();
	Clock::time_point i = Clock::now ();
	// PS6 warp
	phys->PS6Warp ();
	Clock::time_point j = Clock::now ();
	// PS7 thrust cut
	dmg->EndFrame ();
	Clock::time_point k = Clock::now ();
	t.e2 += Us (b - a) + Us (d - c);
	t.e1 += Us (c - b) + Us (f - e) + Us (h - g) + Us (j - i);
	t.e3 += Us (e - d) + Us (g - f) + Us (i - h) + Us (k - j);
}

void CollSession::TimeJump ()
{
	static_cast<PhysGeomA &> (*pgeom).pvel.clear ();
	phys->OnTimeJump ();
	geom->TimeJump ();
}

void CollSession::VesselJump (OBJHANDLE h)
{
	auto it = idOf.find (h);
	if (it != idOf.end ()) phys->OnVesselJump (it->second);
}

void CollSession::TimeAccChanged (double newWarp) { phys->OnTimeAccChanged (newWarp); }

void CollSession::Pause (bool pause) { if (pause) dmg->PausePass (); }

void CollSession::KeyPass () { dmg->KeyPass (); }

void CollSession::PostStep ()
{
	Clock::time_point a = Clock::now ();
	geom->PostStepPoll ();                  // PO1
	dmg->PostStep ();                       // PO2
	if (cfg.testSlotCheck) {                // PO3, tests only
		for (uint32_t id = 1; id < vessel.size (); id++) {
			if (!vessel[id]) continue;
			const std::vector<CollSlotView> &sl = geom->Slots (id);
			for (uint32_t m = 0; m < sl.size (); m++) {
				if (!sl[m].present) continue;
				uint32_t s = 0;
				bool ok = geom->SlotNow (id, m, s);
				CollLogF ("Collision slot check: id=%u mesh=%u ok=%d serial=%u", id, m, (int)ok, s);
			}
		}
	}
	sdk->UiTick ();                         // PO4
	t.post += Us (Clock::now () - a);
	t.posts++;
}

void CollSession::FrameBegin () { t0 = Clock::now (); }

void CollSession::FrameEnd ()
{
	double us = Us (Clock::now () - t0);
	if (!n.frames) t.firstFrame = us;
	n.frames++;
	t.prestep += us;
	t.prestepMax = std::max (t.prestepMax, us);
}

void CollSession::Close ()
{
	dmg->End ();
	const CollSdkCount &c = sdk->Count ();
	const CollAStats &st = phys->Stats ();
	n.writes = c.Writes (); n.probes = c.n[CSK_PROBE]; n.vtx = c.n[CSK_VTX]; n.matrix = c.n[CSK_MATRIX]; n.notices = c.n[CSK_NOTICE];
	n.contacts = (uint64_t)std::max (0, st.real); n.spec = (uint64_t)std::max (0, st.spec); n.free = (uint64_t)std::max (0, st.freePath); n.missed = (uint64_t)std::max (0, st.missed);
	auto u = [] (uint64_t v) { return (unsigned long long)v; };
	double f = n.frames ? (double)n.frames : 1.0, p = t.posts ? (double)t.posts : 1.0;
	CollLogF ("Collision summary: frames=%llu contacts=%llu events=%llu writes=%llu probes=%llu vtx=%llu matrix=%llu notices=%llu spec=%llu free=%llu missed=%llu",
		u (n.frames), u (n.contacts), u (n.events), u (n.writes), u (n.probes), u (n.vtx), u (n.matrix), u (n.notices), u (n.spec), u (n.free), u (n.missed));
	CollLogF ("Collision perf: prestep_mean_us=%.3f prestep_max_us=%.3f e2_us=%.3f e1_us=%.3f e3_us=%.3f post_us=%.3f build_ms=%.3f first_frame_ms=%.3f",
		t.prestep / f, t.prestepMax, t.e2 / f, t.e1 / f, t.e3 / f, t.post / p, t.build * 1e-3, t.firstFrame * 1e-3);
	phys->End ();
	geom->EndSession ();
	sdk->Annotation ("", 0);
}

void CollSession::Abort ()
{
	CollGuard ("session end", [&] { dmg->End (); });
	sdk->Annotation ("", 0);
}
