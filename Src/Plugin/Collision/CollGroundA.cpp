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
		if (!Vessel (x, simt, simdt, e, c)) continue;
		ev.push_back (e);
		fx.push_back (c);
		last[x.id] = simt;
		events++;
	}
}

bool CollGroundA::Vessel (const CollGroundVessel &x, double simt, double simdt, CollImpactEvent &e, CollFxContact &c)
{
	if (!x.h || !x.shape || !x.shape->nPart ()) return false;
	auto lt = last.find (x.id);
	if (lt != last.end () && simt - lt->second < COLL_GROUND_GAP) return false;
	CollVesselRead rd {};
	sdk.ReadVessel (x.h, rd, CVR_NOWEIGHT);
	if (rd.playback || !rd.gref || sdk.ObjType (rd.gref) != OBJTP_PLANET_G || IEq (sdk.ClassName (x.h), "CollDebris")) return false;
	const CollShape &sh = *x.shape;
	Vector pp, pv; Matrix Rp;
	sdk.GlobalState (rd.gref, pp, pv, Rp);
	double T = sdk.PlanetPeriod (rd.gref);
	Vector wp = std::fabs (T) > 0 ? mul (Rp, Vector (0, 1, 0)) * (2.0 * PI_G / T) : Vector ();
	auto surf = [&] (const Vector &g) { return pv + crossp (g - pp, wp); }; // planet-fixed point g, as CollSurfaceVel
	// near test: CG altitude over the terrain under it
	double rp = sdk.Size (rd.gref), rb = 0;
	Vector cb;
	sh.Bound (1, cb, rb);
	Vector loc = tmul (Rp, rd.x - pp);
	double rl = loc.length ();
	if (!(rl > 0)) return false;
	double reach = rb + (rd.v - surf (rd.x)).length () * simdt + COLL_GROUND_NEAR;
	if (!(rl - rp < reach + COLL_GROUND_REACH)) return false;
	double lng = std::atan2 (loc.z, loc.x), lat = Lat (loc, rl);
	double e0 = sdk.Elevation (rd.gref, lng, lat);
	if (!(rl - rp - e0 < reach)) return false;
	// terrain plane through the footprint, normal from the elevations +-rb east and north
	double r0 = rp + e0, cl = std::cos (lat), dlat = r0 > 0 ? rb / r0 : 0;
	Vector up = mul (Rp, loc / rl), nG = up, p0 = pp + up * r0;
	if (rb > 0 && cl * r0 > rb && std::fabs (lat) + dlat < 0.5 * PI_G) {
		double dlng = rb / (r0 * cl);
		auto at = [&] (double lo, double la) { return pp + mul (Rp, Equ (lo, la) * (rp + sdk.Elevation (rd.gref, lo, la))); };
		Vector de = at (lng + dlng, lat) - at (lng - dlng, lat), dn = at (lng, lat + dlat) - at (lng, lat - dlat);
		Vector n = crossp (de, dn);
		double l = n.length ();
		if (l > 0) { n /= l; nG = dotp (n, up) < 0 ? -n : n; }
	}
	// collider vertices at the current pose: the deepest predicted at the end of the step
	const Matrix &R = rd.R;
	uint32_t nall = 0, base = 0;
	for (uint32_t k = 0; k < sh.nPart (); k++) nall += (uint32_t)sh.Part (k).Geom ().vtx.size ();
	uint32_t stride = std::max (1u, (nall + COLL_GROUND_VTX - 1) / COLL_GROUND_VTX);
	int bk = -1; uint32_t bi = 0; double bh = 0; Vector bp, bg, bv;
	tested = 0;
	for (uint32_t k = 0; k < sh.nPart (); k++) {
		const CollPart &P = sh.Part (k);
		const CollGeom &G = P.Geom ();
		const uint8_t *mask = sh.GroupMask (k);
		uint32_t n = (uint32_t)G.vtx.size ();
		for (uint32_t i = (stride - base % stride) % stride; i < n; i += stride) {
			tested++;
			if (!Live (G, mask, i)) continue;
			Vector p = CollApply (P.pose[1], G.Pos (i)), g = rd.x + mul (R, p);
			Vector vr = rd.v + mul (R, crossp (p, rd.w)) - surf (g);
			double hp = dotp (g - p0, nG) + dotp (vr, nG) * simdt;
			if (hp <= 0 && (bk < 0 || hp < bh)) bk = (int)k, bi = i, bh = hp, bp = p, bg = g, bv = vr;
		}
		base += n;
	}
	if (bk < 0) return false;
	double vn = -dotp (bv, nG);
	if (!(vn >= cfg.groundMinSpeed)) return false;
	Vector lb = tmul (Rp, bg - pp);
	double rlb = lb.length ();
	if (rlb - rp - sdk.Elevation (rd.gref, std::atan2 (lb.z, lb.x), Lat (lb, rlb)) > bv.length () * simdt + COLL_GROUND_REFINE) return false; // above its own terrain
	int pidx = -1;
	for (uint32_t i = 0, n = sdk.GbodyCount (); i < n && pidx < 0; i++) if (sdk.Gbody (i) == rd.gref) pidx = (int)i;
	if (pidx < 0) return false;
	// the event: vessel side in the hit part's rest frame, ground side in the planet frame
	const CollPart &P = sh.Part ((uint32_t)bk);
	const CollGeom &G = P.Geom ();
	const uint8_t *mask = sh.GroupMask ((uint32_t)bk);
	Vector nv = tmul (R, -nG), vt = bv + nG * vn;              // outward normal of the vessel side, vessel frame; tangential relative velocity, global
	double lv = vt.length (), m = CollGroundMeff (rd.m, rd.pmi, bp, nv);
	Vector tg = lv >= 1e-6 ? vt / lv : Vector ();
	CollImpactSide &s = e.s[0], &o = e.s[1];
	s.owner = CollOwnerRef { x.id, -1, -1, -1, -1 };
	s.mesh = (int)P.mesh; s.grp = s.tri = -1;
	for (uint32_t t = 0; t < G.tri.size (); t++) {
		const CollTri &tr = G.tri[t];
		if ((tr.v[0] != bi && tr.v[1] != bi && tr.v[2] != bi) || (mask && tr.src < G.srcTab.size () && mask[tr.src])) continue;
		uint32_t mm = 0, gg = 0, ot = 0;
		if (sh.RenderFeature ((uint32_t)bk, t, mm, gg, ot)) { s.mesh = (int)mm; s.grp = gg == ~0u ? -1 : (int)gg; s.tri = ot == ~0u ? -1 : (int)ot; }
		break;
	}
	s.c = G.Pos (bi);
	Vector np = tmul (P.pose[1].A, nv);
	double ln = np.length ();
	s.n = ln > 0 ? np / ln : np;
	s.a = COLL_GROUND_PATCH;
	if (lv >= 1e-6) {
		Vector t = tmul (P.pose[1].A, tmul (R, tg));
		t -= s.n * dotp (t, s.n);
		double l = t.length ();
		s.tdir = l > 0 ? t / l : Vector ();
	}
	o.owner = CollOwnerRef { 0, pidx, -1, -1, -1 };
	o.mesh = o.grp = o.tri = -1;
	o.c = tmul (Rp, bg - pp); o.n = tmul (Rp, nG); o.a = COLL_GROUND_PATCH; o.tdir = tmul (Rp, -tg);
	e.t = simt;
	e.vn = vn; e.vn_post = COLL_GROUND_E * vn; e.vt = lv;
	e.dKE = 0.5 * m * vn * vn * (1 - COLL_GROUND_E * COLL_GROUND_E); e.Wf = 0;
	e.Jn = m * vn * (1 + COLL_GROUND_E); e.Jt = 0; e.meff = m;
	e.flags = COLLEV_FIRST;
	// the dust of a building contact: nOther in the local horizon frame (y up), block material
	c.id = x.id; c.h = x.h;
	c.c = bp; c.n = nv; c.tdir = tmul (R, tg);
	c.vn = vn; c.vt = lv; c.Jn = e.Jn; c.Jt = 0; c.dt = simdt; c.flags = COLLEV_FIRST;
	c.building = true; c.playback = false;
	Vector east = mul (Rp, Vector (-std::sin (lng), 0, std::cos (lng))), north = mul (Rp, Vector (-std::sin (lat) * std::cos (lng), std::cos (lat), -std::sin (lat) * std::sin (lng)));
	c.nOther = Vector (dotp (nG, east), dotp (nG, up), dotp (nG, north));
	c.matOther = &DentMath::DefaultMaterial (DENTB_BLOCK);
	return true;
}
