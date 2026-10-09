// not upstream: collision addon, ground impacts: collider vertices against the terrain under a vessel make impact events for the damage commit (design CA-ground 1)
#include <algorithm>
#include <cctype>
#include <cmath>
#include "CollGroundA.h"

namespace {
constexpr int OBJTP_PLANET_G = 4;                          // OBJTP_PLANET (OrbiterAPI.h:1773)
constexpr double PI_G = 3.14159265358979323846;

bool IEq (const std::string &a, const char *b)
{
	size_t i = 0;
	for (; i < a.size () && b[i]; i++) if (std::tolower ((unsigned char)a[i]) != std::tolower ((unsigned char)b[i])) return false;
	return i == a.size () && !b[i];
}

Vector Equ (double lng, double lat) { return Vector (std::cos (lat) * std::cos (lng), std::sin (lat), std::cos (lat) * std::sin (lng)); } // planet frame, as Orbiter's EquatorialToLocal

double Lat (const Vector &loc, double r) { return std::asin (std::max (-1.0, std::min (1.0, r > 0 ? loc.y / r : 0.0))); }

bool Live (const CollGeom &G, const uint8_t *mask, uint32_t i) // the vertex is on a group that is not hidden
{
	if (!mask || i + 1 >= G.refOfs.size ()) return true;
	for (uint32_t j = G.refOfs[i]; j < G.refOfs[i + 1]; j++) if (G.ref[j].src < G.srcTab.size () && !mask[G.ref[j].src]) return true;
	return false;
}
}

double CollGroundMeff (double m, const Vector &pmi, const Vector &r, const Vector &n)
{
	if (!(m > 0)) return 0;
	Vector a = Xc (r, n);
	double s = 1.0 / m;
	for (int k = 0; k < 3; k++) if (pmi.data[k] > 0) s += a.data[k] * a.data[k] / (m * pmi.data[k]);
	return 1.0 / s;
}

CollGroundA::CollGroundA (CollSdk &s, const CollCfgValues &c) : sdk (s), cfg (c) {}

void CollGroundA::Frame (double simt, double simdt, const std::vector<CollGroundVessel> &v, std::vector<CollImpactEvent> &ev, std::vector<CollFxContact> &fx)
{
	if (!cfg.ground || cfg.model == 0 || !(simdt > 0.0)) return;
	for (const CollGroundVessel &x : v) {
		CollImpactEvent e {};
		CollFxContact c;
		int r = Vessel (x, simt, simdt, e, c);
		if (r == 0) locks.erase (x.id);                    // a frame with no candidate re-arms every region
		if (r != 2) continue;
		ev.push_back (e);
		fx.push_back (c);
		events++;
	}
	pass++;
}

bool CollGroundA::Locked (const std::vector<Lock> &l, const Cand &c, double rb)
{
	for (const Lock &x : l) if (x.part == c.part && (x.p - c.p).length () < COLL_GROUND_REGION * rb && !(c.vn > COLL_GROUND_REARM * x.vn)) return true;
	return false;
}

void CollGroundA::Kick (CollH h, const CollVesselRead &rd, const Vector &p, const Vector &J, double simdt)
{
	if (!(rd.m > 0)) return;                                // the hit point leaves the ground at COLL_GROUND_E of its approach: the event's impulse goes to the vessel
	Vector dH = crossp (J, p);                              // Orbiter convention: H = m crossp (v, r) + m pmi w
	Vector dw (rd.pmi.x > 0 ? dH.x / (rd.m * rd.pmi.x) : 0, rd.pmi.y > 0 ? dH.y / (rd.m * rd.pmi.y) : 0, rd.pmi.z > 0 ? dH.z / (rd.m * rd.pmi.z) : 0);
	kicks++;
	if (rd.sv) { sdk.AddForce (h, J * (1.0 / (simdt > 0 ? simdt : 0.02)), p); return; } // docked stack: the stack takes it over one step
	CollStateWrite st {};
	st.rbody = rd.gref;
	Vector rp, rv;
	sdk.RelState (h, st.rbody, rp, rv);
	st.rpos = rp; st.rvel = rv + mul (rd.R, J) * (1.0 / rd.m); st.vrot = rd.w + dw;
	st.arot = Vector (std::atan2 (rd.R (1, 2), rd.R (2, 2)), -std::asin (std::max (-1.0, std::min (1.0, rd.R (0, 2)))), std::atan2 (rd.R (0, 1), rd.R (0, 0))); // inverse of Vessel::SetGlobalOrientation
	sdk.SetState (h, st);
	sdk.SetAttitude (h, rd.R);
	sdk.SetSpin (h, rd.w + dw);
}

double CollGroundA::StackMass (CollH h)
{
	std::vector<CollH> seen { h }; // docked stack: every vessel reachable through the docks
	double m = 0;
	for (size_t k = 0; k < seen.size () && k < 64; k++) {
		CollVesselRead r {};
		sdk.ReadVessel (seen[k], r, CVR_NOWEIGHT);
		m += r.m;
		for (uint32_t i = 0, n = sdk.DockCount (seen[k]); i < n; i++) { CollPortInfo pi {}; if (sdk.Dock (seen[k], i, pi) && pi.mate && std::find (seen.begin (), seen.end (), pi.mate) == seen.end ()) seen.push_back (pi.mate); }
	}
	return m;
}

int CollGroundA::Vessel (const CollGroundVessel &x, double simt, double simdt, CollImpactEvent &e, CollFxContact &c)
{
	if (!x.h || !x.shape || !x.shape->nPart ()) return 0;
	CollVesselRead rd {};
	sdk.ReadVessel (x.h, rd, CVR_NOWEIGHT);
	CollH sr = rd.sref;                                     // the body under the vessel, not its gravity reference
	if (rd.playback || !sr || sdk.ObjType (sr) != OBJTP_PLANET_G) return 0;
	const CollShape &sh = *x.shape;
	const Matrix &R = rd.R;
	Vector pp, pv; Matrix Rp;
	sdk.GlobalState (sr, pp, pv, Rp);
	double T = sdk.PlanetPeriod (sr);
	Vector wp = std::fabs (T) > 0 ? mul (Rp, Vector (0, 1, 0)) * (2.0 * PI_G / T) : Vector ();
	auto surf = [&] (const Vector &g) { return pv + crossp (g - pp, wp); }; // planet-fixed point g, as CollSurfaceVel
	double rp = sdk.Size (sr), rb = 0, hz = std::min (simdt, COLL_GROUND_HZ);
	Vector cb;
	sh.Bound (1, cb, rb);
	double vmax = (rd.v - surf (rd.x)).length () + (rd.w - tmul (R, wp)).length () * rb; // fastest point relative to the surface, at most
	if (vmax < cfg.groundMinSpeed) return 0;                // landed or slow: no point can strike, no terrain query
	// near test: a loose vertical gate, then the distance to the terrain plane or to the highest sample
	Vector loc = tmul (Rp, rd.x - pp);
	double rl = loc.length ();
	if (!(rl > 0)) return 0;
	double reach = rb + vmax * hz + COLL_GROUND_NEAR, loose = reach + 2 * rb;
	if (!(rl - rp < loose + COLL_GROUND_REACH) || IEq (sdk.ClassName (x.h), "CollDebris")) return 0;
	double lng = std::atan2 (loc.z, loc.x), lat = Lat (loc, rl);
	double e0 = sdk.Elevation (sr, lng, lat), emax = e0;
	if (!(rl - rp - e0 < loose)) return 0;
	double r0 = rp + e0, cl = std::cos (lat), dlat = r0 > 0 ? rb / r0 : 0;
	Vector up = mul (Rp, loc / rl), nG = up, p0 = pp + up * r0;
	if (rb > 0 && cl * r0 > rb && std::fabs (lat) + dlat < 0.5 * PI_G) {
		double dlng = rb / (r0 * cl);
		auto at = [&] (double lo, double la) { double h = sdk.Elevation (sr, lo, la); emax = std::max (emax, h); return pp + mul (Rp, Equ (lo, la) * (rp + h)); };
		Vector de = at (lng + dlng, lat) - at (lng - dlng, lat), dn = at (lng, lat + dlat) - at (lng, lat - dlat);
		Vector n = crossp (de, dn);
		double l = n.length ();
		if (l > 0) { n /= l; nG = dotp (n, up) < 0 ? -n : n; }
	}
	if (!(dotp (rd.x - p0, nG) < reach || rl - rp - emax < reach)) return 0;
	// collider vertices at the current pose: a turning stride and each part's lowest along -nG; candidates approach at the event speed within the horizon
	uint32_t nall = 0, base = 0;
	for (uint32_t k = 0; k < sh.nPart (); k++) nall += (uint32_t)sh.Part (k).Geom ().vtx.size ();
	uint32_t stride = std::max (1u, (nall + COLL_GROUND_VTX - 1) / COLL_GROUND_VTX), start = pass % stride;
	Vector upv = tmul (R, nG);                              // terrain up in the vessel frame
	std::vector<Cand> cand;
	tested = 0;
	for (uint32_t k = 0; k < sh.nPart (); k++) {
		const CollPart &P = sh.Part (k);
		const CollGeom &G = P.Geom ();
		const uint8_t *mask = sh.GroupMask (k);
		uint32_t n = (uint32_t)G.vtx.size (), lo = n;
		Vector d = tmul (P.pose[1].A, upv);
		double dmin = 0;
		for (uint32_t i = 0; i < n; i++) {
			double s = dotp (G.Pos (i), d);
			if ((lo == n || s < dmin) && Live (G, mask, i)) lo = i, dmin = s;
		}
		auto test = [&] (uint32_t i) {
			tested++;
			Vector p = CollApply (P.pose[1], G.Pos (i)), g = rd.x + mul (R, p);
			Vector vr = rd.v + mul (R, crossp (p, rd.w)) - surf (g);
			double vn = -dotp (vr, nG), hp = dotp (g - p0, nG) - vn * hz;
			if (hp <= 0 && vn >= cfg.groundMinSpeed) cand.push_back (Cand { k, i, p, g, vr, vn, hp });
		};
		uint32_t first = (start + stride - base % stride) % stride;
		for (uint32_t i = first; i < n; i += stride) if (Live (G, mask, i)) test (i); else tested++;
		if (lo < n && (lo < first || (lo - first) % stride)) test (lo);
		base += n;
	}
	if (cand.empty ()) return 0;
	std::stable_sort (cand.begin (), cand.end (), [] (const Cand &a, const Cand &b) { return a.vn > b.vn || (a.vn == b.vn && a.hp < b.hp); });
	int pidx = -1;
	for (uint32_t i = 0, n = sdk.GbodyCount (); i < n && pidx < 0; i++) if (sdk.Gbody (i) == sr) pidx = (int)i;
	if (pidx < 0) return 1;
	std::vector<Lock> &lk = locks[x.id];
	lk.erase (std::remove_if (lk.begin (), lk.end (), [&] (const Lock &l) { return simt - l.t >= COLL_GROUND_GAP; }), lk.end ());
	int tries = 0;
	for (const Cand &q : cand) {
		if (Locked (lk, q, rb)) continue;
		if (tries++ >= COLL_GROUND_TRIES) break;
		// refine at the vertex: its own terrain height, the terrain normal from samples a few metres east and north
		Vector lb = tmul (Rp, q.g - pp);
		double rlb = lb.length (), lv = std::atan2 (lb.z, lb.x), av = Lat (lb, rlb), hv = sdk.Elevation (sr, lv, av);
		if (rlb - rp - hv > q.vr.length () * hz + COLL_GROUND_REFINE) continue;
		double rv = rp + hv, cv = std::cos (av);
		Vector uv = mul (Rp, Equ (lv, av)), nL = uv;
		Vector east = mul (Rp, Vector (-std::sin (lv), 0, std::cos (lv))), north = mul (Rp, Vector (-std::sin (av) * std::cos (lv), std::cos (av), -std::sin (av) * std::sin (lv)));
		if (cv * rv > COLL_GROUND_DN && std::fabs (av) + COLL_GROUND_DN / rv < 0.5 * PI_G) { // slopes from radial heights: no curvature bias
			double se = (sdk.Elevation (sr, lv + COLL_GROUND_DN / (rv * cv), av) - hv) / COLL_GROUND_DN, sn = (sdk.Elevation (sr, lv, av + COLL_GROUND_DN / rv) - hv) / COLL_GROUND_DN;
			nL = (uv - east * se - north * sn).unit ();
		}
		double vn = -dotp (q.vr, nL);
		if (!(vn >= cfg.groundMinSpeed)) continue;
		// the event: vessel side in the hit part's rest frame, ground side in the planet frame
		const CollPart &P = sh.Part (q.part);
		const CollGeom &G = P.Geom ();
		const uint8_t *mask = sh.GroupMask (q.part);
		Vector nv = tmul (R, -nL), vt = q.vr + nL * vn;      // outward normal of the vessel side, vessel frame; tangential relative velocity, global
		CollH mb = x.h;                                     // the body that moves: an attached child's root (the core carries children), a docked stack as a whole
		for (int k = 0; k < 16; k++) {
			CollH up = nullptr;
			for (uint32_t i = 0, na = sdk.AttachCount (mb, true); i < na && !up; i++) { CollAttInfo ai {}; if (sdk.Attach (mb, true, i, ai) && ai.mate) up = ai.mate; }
			if (!up || up == x.h) break;
			mb = up;
		}
		CollVesselRead tr = rd;
		if (mb != x.h) sdk.ReadVessel (mb, tr, CVR_NOWEIGHT);
		Vector pt = tmul (tr.R, q.g - tr.x), nt = tmul (tr.R, nL); // the hit point and terrain up in that body's frame
		double lt = vt.length (), m = CollGroundMeff (tr.sv ? StackMass (mb) : tr.m, tr.pmi, pt, nt);
		Vector tg = lt >= 1e-6 ? vt / lt : Vector ();
		CollImpactSide &s = e.s[0], &o = e.s[1];
		s.owner = CollOwnerRef { x.id, -1, -1, -1, -1 };
		s.mesh = (int)P.mesh; s.grp = s.tri = -1;
		for (uint32_t t = 0; t < G.tri.size (); t++) {
			const CollTri &tr = G.tri[t];
			if ((tr.v[0] != q.vtx && tr.v[1] != q.vtx && tr.v[2] != q.vtx) || (mask && tr.src < G.srcTab.size () && mask[tr.src])) continue;
			uint32_t mm = 0, gg = 0, ot = 0;
			if (sh.RenderFeature (q.part, t, mm, gg, ot)) { s.mesh = (int)mm; s.grp = gg == ~0u ? -1 : (int)gg; s.tri = ot == ~0u ? -1 : (int)ot; }
			break;
		}
		s.c = G.Pos (q.vtx);
		Vector np = tmul (P.pose[1].A, nv);
		double ln = np.length ();
		s.n = ln > 0 ? np / ln : np;
		s.a = COLL_GROUND_PATCH;
		if (lt >= 1e-6) {
			Vector t = tmul (P.pose[1].A, tmul (R, tg));
			t -= s.n * dotp (t, s.n);
			double l = t.length ();
			s.tdir = l > 0 ? t / l : Vector ();
		}
		o.owner = CollOwnerRef { 0, pidx, -1, -1, -1 };
		o.mesh = o.grp = o.tri = -1;
		o.c = lb; o.n = tmul (Rp, nL); o.a = COLL_GROUND_PATCH; o.tdir = tmul (Rp, -tg);
		e.t = simt;
		e.vn = vn; e.vn_post = COLL_GROUND_E * vn; e.vt = lt;
		e.dKE = 0.5 * m * vn * vn * (1 - COLL_GROUND_E * COLL_GROUND_E); e.Wf = 0;
		e.Jn = m * vn * (1 + COLL_GROUND_E); e.Jt = 0; e.meff = m;
		e.flags = COLLEV_FIRST;
		// the dust of a building contact: nOther in the local horizon frame at the vertex (y up), block material
		c.id = x.id; c.h = x.h;
		c.c = q.p; c.n = nv; c.tdir = tmul (R, tg);
		c.vn = vn; c.vt = lt; c.Jn = e.Jn; c.Jt = 0; c.dt = simdt; c.flags = COLLEV_FIRST;
		c.building = true; c.playback = false;
		c.nOther = Vector (dotp (nL, east), dotp (nL, uv), dotp (nL, north));
		c.matOther = &DentMath::DefaultMaterial (DENTB_BLOCK);
		lk.push_back (Lock { q.part, q.p, simt, vn });
		Kick (mb, tr, pt, nt * e.Jn, simdt);
		return 2;
	}
	return 1;
}
