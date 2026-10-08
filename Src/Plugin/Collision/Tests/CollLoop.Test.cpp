// not upstream: closed-loop gate (D2 U23, D3 G1-G4, D4 U18): CollDetect + CollFrameSolver per frame
#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include "CollSolve.h"
#include "DentMath.h"
#include "CollTestMsh.h"

namespace {

const double G0 = 9.81;                                             // constant gravity [m/s^2]
const double GM_E = 3.986004418e14, R_E = 6.371e6, W_E = 7.2921159e-5; // Earth: GM [m^3/s^2], radius [m], rotation rate [rad/s]
const double SKIN2 = 2.0*COLL_SKIN_DEFAULT;                         // skin sum of a pair [m]
const int NFRAME = 600;                                             // frames per run (D2 U23, D3 15.2)
const double RGAP = 0.0;                                            // start gap of the resting scenes; a positive gap closes (fix1 R4)

Matrix QMat (const Quaternion &q) { Matrix R; R.Set (q); return R; }

// exact body increment of Quaternion::Rotate's generator: q.Rotate (phi) ~ q * QExp (phi)
Quaternion QExp (const Vector &phi)
{
	double a = phi.length ();
	if (a < 1e-300) return Quaternion ();
	double s = std::sin (0.5*a)/a;
	return Quaternion (phi.x*s, phi.y*s, phi.z*s, std::cos (0.5*a));
}

// log lines at INFO and above (GRACE, wake, check warnings, errors)
std::vector<std::string> g_log;
void LogSink (int level, const char *msg) { if (level >= COLLLOG_INFO) g_log.push_back (std::to_string (level) + " " + msg); }
struct LogCapture {
	LogCapture () { g_log.clear (); g_collLog = LogSink; }
	~LogCapture () { g_collLog = nullptr; }
	int Count (const char *s) const { int n = 0; for (const std::string &l : g_log) if (l.find (s) != std::string::npos) n++; return n; }
	int AtLeast (int level) const { int n = 0; for (const std::string &l : g_log) if (l[0] - '0' >= level) n++; return n; }
};

// closed box with outward faces (CollSolveFrame.Test layout)
CollGroupData BoxMesh (const Vector &h)
{
	CollGroupData g;
	for (int i = 0; i < 8; i++) g.vtx.push_back (CollVtx { (float)((i & 1) ? h.x : -h.x), (float)((i & 2) ? h.y : -h.y), (float)((i & 4) ? h.z : -h.z), 0, 0, 0, 0, 0 });
	const uint16_t f[36] = { 0,2,1, 1,2,3, 4,5,6, 5,7,6, 0,1,4, 1,5,4, 2,6,3, 3,6,7, 0,4,2, 2,4,6, 1,3,5, 3,7,5 };
	g.idx.assign (f, f + 36);
	return g;
}

// mass-normalised PMI of a solid box with half extents h
Vector BoxPmi (const Vector &h)
{
	return Vector (h.y*h.y + h.z*h.z, h.x*h.x + h.z*h.z, h.x*h.x + h.y*h.y)/3.0;
}

// corners of the box face at -h[k] (the support face), box centre at c in the body frame
std::vector<Vector> SupportFace (const Vector &h, int k, const Vector &c)
{
	std::vector<Vector> v;
	for (int i = 0; i < 4; i++) {
		Vector p;
		p.data[k] = -h.data[k];
		p.data[(k + 1)%3] = (i & 1) ? h.data[(k + 1)%3] : -h.data[(k + 1)%3];
		p.data[(k + 2)%3] = (i & 2) ? h.data[(k + 2)%3] : -h.data[(k + 2)%3];
		v.push_back (p + c);
	}
	return v;
}

// a collider built from mesh groups (D1 1.2); keeps its groups
struct Geo {
	std::vector<CollGroupData> grp;
	CollGeom geom;
	explicit Geo (std::vector<CollGroupData> g) : grp (std::move (g))
	{
		std::vector<CollSrcGroup> src;
		for (size_t i = 0; i < grp.size (); i++) src.push_back (CollSrcGroup { &grp[i], CollSrc { 0, (uint32_t)i, 0, 0, 0 } });
		CollBuildStats st {};
		REQUIRE (geom.Build (src.data (), src.size (), COLL_WELD_DEFAULT, &st));
	}
};

std::vector<CollGroupData> Box (const Vector &h) { return std::vector<CollGroupData> (1, BoxMesh (h)); }

// stock Atlantis orbiter mesh through the test loader (D1 9)
std::vector<CollGroupData> AtlantisMesh ()
{
	CollRestMesh m;
	std::string err;
	bool ok = CollTestLoadMsh (CollTestPath ("Src/Vessel/Atlantis/Atlantis/Meshes/Atlantis.msh").c_str (), m, &err);
	INFO (err);
	REQUIRE (ok);
	return m.grp;
}

CollOwnerKey VKey (uint32_t id) { return CollOwnerKey { COLLO_VESSEL, id, -1, -1, -1, -1 }; }
CollOwnerKey BKey (int obj) { return CollOwnerKey { COLLO_BUILDING, 0, 0, 0, obj, 0 }; }

// one rigid part at ofs in its body frame (P0 = P1)
CollPartRef Part (const CollGeom &g, const CollOwnerKey &o, const Vector &ofs, uint32_t key)
{
	CollPartRef p {};
	p.geom = &g; p.P0 = p.P1 = CollTranslate (ofs); p.skin = g.skin; p.owner = o; p.partKey = key; p.version = 1; p.mesh = 0; p.mask = nullptr;
	return p;
}

// one body of the loop: dynamic bodies are integrated here, buildings follow a planet-fixed path
struct LBody {
	uint32_t id = 0;
	uint8_t kind = COLLB_DYNAMIC;
	double m = 0.0; Vector pmi;                    // root mass and PMI (D3 2.3)
	Vector x, v, wb; Quaternion q;                 // committed state at t0
	Vector a1; bool aok = false;                   // acceleration at the last t1 (pose cache: a0 of the next frame)
	Vector thrust;                                 // body-frame thrust acceleration [m/s^2]
	Vector V, W, cl;                               // building: centre velocity, rotation rate, reference point in the rotating frame
	std::vector<CollPartRef> parts;
};

// building state at t: frame turned by W t about a centre moving at V (D3 2.5 planet-fixed field)
void KinAt (const LBody &b, double t, Vector &c, Vector &v, Quaternion &q)
{
	q.Set (QExp (b.W*t));
	Vector r = mul (QMat (q), b.cl);
	c = b.V*t + r;
	v = b.V + Xc (b.W, r);
}

// torque-free Euler rate (CollSolve.Test, BodyIntegrator pattern)
Vector EulerFree (const Vector &pmi, const Vector &w)
{
	return Vector (-(pmi.y - pmi.z)*w.y*w.z/pmi.x, -(pmi.z - pmi.x)*w.z*w.x/pmi.y, -(pmi.x - pmi.y)*w.x*w.y/pmi.z);
}

// stub of CollWorld's material/feature lookups: default hull material on both sides
struct Host : CollSolveHost {
	const CollDetect *det = nullptr;
	CollSMat Material (const CollPairResult &r, int i, int side) override { return CollSMat (); }
	void Feature (const CollPairResult &r, int i, int side, CollImpactSide &s) override
	{
		s.owner = CollOwnerRefOf (det->Owner (r, i, side));
		const CollBody &B = det->Body (side ? r.bodyB : r.bodyA);
		s.mesh = B.parts[side ? r.pt[i].partB : r.pt[i].partA].mesh;
		s.grp = 0; s.tri = (int)(side ? r.pt[i].triB : r.pt[i].triA);
	}
};

// P1 of one frame as CollWorld runs it: integrate, gather, Detect, CollFrameSolver::Run, write-back
class Loop {
public:
	std::vector<LBody> body;                       // bases first, then assemblies by id (D2 1.4 step 10)
	Vector g; double GM = 0.0;                     // constant gravity, central gravity about the origin
	int frame = 0; double t = 0.0;                 // frames done, committed sim time
	CollDetect det;
	CollFrameSolver fs;
	Host host;
	CollParams prm;
	CollSolveParams sp;
	std::vector<CollPairResult> found, solved;     // last frame: Detect output, results solved
	std::vector<CollImpactEvent> ev;               // last frame: impact events
	std::vector<CollBodyDelta> delta;              // last frame: write-back deltas
	std::vector<CollFrameBody> fb;                 // last frame: driver bodies
	CollFrameStats st {};
	Vector dP, dL; double jsum = 0.0, rsz = 0.0;   // last frame: momentum change of the solve deltas, sum m|dv|, lever size
	int npc = 0;                                   // last frame: position-correction deltas
	Loop () { host.det = &det; fs.check = true; }
	Vector Acc (const LBody &b, const Vector &x, const Quaternion &q) const
	{
		Vector a = g + mul (QMat (q), b.thrust);
		if (GM > 0.0) { double r = x.length (); a -= x*(GM/(r*r*r)); }
		return a;
	}
	// RK2 translation and torque-free Euler rotation in 20 substeps (BodyIntegrator pattern)
	void Integrate (const LBody &b, double h, Vector &x, Vector &v, Quaternion &q, Vector &wb) const
	{
		const int n = 20;
		double d = h/n;
		for (int i = 0; i < n; i++) {
			Vector a0 = Acc (b, x, q), xm = x + v*(0.5*d), vm = v + a0*(0.5*d), am = Acc (b, xm, q);
			x += vm*d; v += am*d;
			Vector wm = wb + EulerFree (b.pmi, wb)*(0.5*d);
			q.Rotate (wm*d);
			wb += EulerFree (b.pmi, wm)*d;
		}
	}
	// committed state of body i at t, global angular velocity
	void Pose (int i, Vector &x, Vector &v, Quaternion &q, Vector &wg) const
	{
		const LBody &b = body[i];
		if (b.kind == COLLB_DYNAMIC) { x = b.x; v = b.v; q.Set (b.q); wg = mul (QMat (b.q), b.wb); }
		else { KinAt (b, t, x, v, q); wg = b.W; }
	}
	void Step (double h);
};

void Loop::Step (double h)
{
	const double t0 = frame*h, t1 = (frame + 1)*h;
	const size_t n = body.size ();
	std::vector<Vector> x1 (n), v1 (n), w1 (n), a1 (n);
	std::vector<Quaternion> q1 (n);
	det.Begin (prm, h);
	for (size_t i = 0; i < n; i++) {
		const LBody &b = body[i];
		CollBody cb {};
		cb.parts = b.parts; cb.rmax = 0.0; cb.id = b.id; cb.kind = b.kind; cb.entry = frame ? 0 : COLLE_NEW; cb.jump1 = false; cb.planet = b.kind == COLLB_BASE ? 0 : -1;
		CollMotion &m = cb.m;
		m.h = h; m.ta = 0.0; m.tb = 1.0;
		if (b.kind == COLLB_DYNAMIC) {
			Vector x = b.x, v = b.v, wb = b.wb;
			Quaternion q (b.q);
			Integrate (b, h, x, v, q, wb);
			x1[i] = x; v1[i] = v; w1[i] = wb; q1[i].Set (q);
			a1[i] = Acc (b, x, q);
			m.c0 = b.x; m.v0 = b.v; m.q0.Set (b.q); m.w0g = mul (QMat (b.q), b.wb);
			m.c1 = x; m.v1 = v; m.q1.Set (q); m.w1g = mul (QMat (q), wb);
			m.a1 = a1[i]; m.a0 = b.aok ? b.a1 : a1[i]; m.a0ok = b.aok;
		} else {
			Vector c0, c1, u0, u1;
			Quaternion k0, k1;
			KinAt (b, t0, c0, u0, k0); KinAt (b, t1, c1, u1, k1);
			m.c0 = c0; m.v0 = u0; m.q0.Set (k0); m.c1 = c1; m.v1 = u1; m.q1.Set (k1);
			m.w0g = b.W; m.w1g = b.W; m.a0 = (u1 - u0)/h; m.a1 = m.a0; m.a0ok = true;  // D2 1.6
			x1[i] = c1; v1[i] = u1; q1[i].Set (k1); w1[i] = tmul (QMat (k1), b.W);
		}
		REQUIRE (det.AddBody (cb) == (int)i);
	}
	found.clear ();
	det.Detect (found, st);
	fb.clear ();
	for (size_t i = 0; i < n; i++) {
		const LBody &b = body[i];
		CollFrameBody f {};
		f.dyn = b.kind == COLLB_DYNAMIC; f.wakeable = false; f.id = b.id; f.m = b.m; f.pmi = b.pmi;
		f.x1 = x1[i]; f.v1 = v1[i]; f.wb1 = w1[i]; f.q1.Set (q1[i]);
		fb.push_back (f);
	}
	std::vector<CollPairResult> (found).swap (solved);
	fs.Run (det, solved, fb, h, t0, sp, COLL_TOI_ROUNDS, host, delta, ev);
	// write-back in CollApplyDelta order; momentum change of the deltas about first dynamic body at t1
	int o = -1;
	bool allDyn = true;
	for (size_t i = 0; i < n; i++) {
		allDyn = allDyn && body[i].kind == COLLB_DYNAMIC;
		if (body[i].kind == COLLB_DYNAMIC && (o < 0 || body[i].id < body[o].id)) o = (int)i;
	}
	const Vector O = o >= 0 ? x1[o] : Vector (), Vr = o >= 0 ? v1[o] : Vector ();
	dP = dL = Vector (); jsum = rsz = 0.0; npc = 0;
	for (const CollBodyDelta &d : delta) {
		const LBody &b = body[d.body];
		REQUIRE (b.kind == COLLB_DYNAMIC);
		Vector Ib = b.pmi*b.m, Ls0 = mul (QMat (q1[d.body]), Ib*w1[d.body]), xo = x1[d.body] - O, vo = v1[d.body] - Vr;
		REQUIRE (CollApplyDeltaState (x1[d.body], v1[d.body], q1[d.body], w1[d.body], Ib, d.d));
		if (d.poscorr) { npc++; continue; }
		Vector mdv = d.d.dv*b.m;
		dP += mdv;
		dL += Xc (xo, mdv) + Xc (d.d.dx, vo*b.m) + Xc (d.d.dx, mdv) + (mul (QMat (q1[d.body]), Ib*w1[d.body]) - Ls0);
		jsum += mdv.length ();
		rsz = std::max (rsz, xo.length ());
	}
	if (!allDyn) jsum = 0.0;                       // a kinematic partner takes momentum: no pair check
	for (size_t i = 0; i < n; i++) {
		LBody &b = body[i];
		if (b.kind != COLLB_DYNAMIC) continue;
		b.x = x1[i]; b.v = v1[i]; b.wb = w1[i]; b.q.Set (q1[i]);
		b.a1 = a1[i]; b.aok = true;
	}
	frame++;
	t = t1;
}

// rest of body r on support s: drift in s's frame, relative velocity, hop (D3 4.8 columns)
struct Rest {
	int r = 1, s = 0;
	Vector ns;                                     // support normal in s's frame
	std::vector<Vector> corner;                    // support-face corners of r, body frame of r
	std::vector<Vector> c0; Vector x0;             // corners and CG of r in s's frame at the start
	double dn = 0.0, dmean = 0.0, dt = 0.0;        // largest corner and mean corner drift along ns, CG drift across ns [m]
	double hop = 0.0, vnLate = 0.0, vLate = 0.0, vEnd = 0.0, vnEnd = 0.0; // largest separating speed, late |vn| and |v|, final |v| and |vn| over the corners [m/s]
	double gapEnd = 0.0;                           // smallest corner height along ns change at the end [m]
	void Sample (const Loop &L, std::vector<Vector> &c, Vector &xr, std::vector<Vector> &vc, Vector &n) const
	{
		Vector xs, vs, ws, x, v, w;
		Quaternion qs, qr;
		L.Pose (s, xs, vs, qs, ws); L.Pose (r, x, v, qr, w);
		Matrix Rs = QMat (qs), Rr = QMat (qr);
		xr = tmul (Rs, x - xs);
		n = mul (Rs, ns);
		c.clear (); vc.clear ();
		for (const Vector &k : corner) {
			Vector p = x + mul (Rr, k);
			c.push_back (tmul (Rs, p - xs));
			vc.push_back ((v + Xc (w, p - x)) - (vs + Xc (ws, p - xs)));
		}
	}
	void Start (const Loop &L)
	{
		std::vector<Vector> vc;
		Vector n;
		Sample (L, c0, x0, vc, n);
	}
	void Add (const Loop &L, bool late)
	{
		std::vector<Vector> c, vc;
		Vector xr, n;
		Sample (L, c, xr, vc, n);
		Vector dx = xr - x0;
		double dc = dotp (dx, ns), sum = 0.0, low = 1e300;
		dt = std::max (dt, (dx - ns*dc).length ());
		vEnd = vnEnd = 0.0;
		for (size_t k = 0; k < c.size (); k++) {
			double d = dotp (c[k] - c0[k], ns), vn = dotp (vc[k], n);
			dn = std::max (dn, std::fabs (d));
			sum += d; low = std::min (low, d);
			hop = std::max (hop, vn);
			vEnd = std::max (vEnd, vc[k].length ()); vnEnd = std::max (vnEnd, std::fabs (vn));
			if (late) { vnLate = std::max (vnLate, std::fabs (vn)); vLate = std::max (vLate, vc[k].length ()); }
		}
		dmean = std::max (dmean, std::fabs (sum/c.size ()));
		gapEnd = low;
	}
};

// what one run produced, frame by frame (D2 U23, D3 15.2, D4 U18)
struct Tally {
	int frames = 0, noContact = 0, notResting = 0, core = 0, grace = 0, poscorr = 0, load = 0, rounds = 0, resweeps = 0;
	std::vector<int> firstAt, evAt;                // frames with FIRST points, frames with events
	double dKE = 0.0, dent = 0.0, pl = -1.0;       // sum of event dKE, D4 damage energy, worst relative momentum residual of a pair solve (-1: none)
	struct Ev { int frame; double vn, dKE, Wf, E; bool first; };
	std::vector<Ev> ev;                            // every event with its D4 energy
	bool splitOk = true;                           // D4 SplitEnergy never saw energy on an event without FIRST
	DentMaterial ma = DentMath::DefaultMaterial (-1), mb = DentMath::DefaultMaterial (-1); // side A and B materials for D4
	void Add (const Loop &L)
	{
		int k = frames++;
		bool first = false, rest = !L.solved.empty ();
		for (const std::vector<CollPairResult> *v : { &L.found, &L.solved })
			for (const CollPairResult &r : *v) {
				for (int i = 0; i < r.npt; i++) first = first || (r.pt[i].flags & COLLP_FIRST);
				if (r.flags & COLLF_GRACE) grace++;
			}
		for (const CollPairResult &r : L.found) if (r.flags & COLLF_CORE) core++;
		for (const CollPairResult &r : L.solved) rest = rest && r.kind == COLL_RESTING;
		if (first) firstAt.push_back (k);
		if (L.st.graceScopes) grace++;
		if (L.solved.empty ()) noContact++;
		if (k >= 10 && !rest) notResting++;
		poscorr += L.npc;
		rounds += L.fs.stats.rounds; resweeps += L.fs.stats.resweeps;
		for (const CollFrameBody &f : L.fb) if (f.dyn && f.loadRest) { load++; break; }
		if (!L.ev.empty ()) evAt.push_back (k);
		for (const CollImpactEvent &e : L.ev) {
			double E[2], ea[2];
			dKE += e.dKE;
			splitOk = DentMath::SplitEnergy (e.dKE, e.Wf, e.vn, (e.flags & COLLEV_FIRST) != 0, ma, mb, E, ea) && splitOk;
			dent += E[0] + E[1];
			ev.push_back (Ev { k, e.vn, e.dKE, e.Wf, E[0] + E[1], (e.flags & COLLEV_FIRST) != 0 });
		}
		if (L.jsum > 0.0) pl = std::max (pl, std::max (L.dP.length ()/L.jsum, L.dL.length ()/(L.jsum*(1.0 + L.rsz))));
	}
};

std::string Frames (const std::vector<int> &v)
{
	std::string s;
	for (size_t i = 0; i < v.size () && i < 8; i++) s += (i ? "," : "") + std::to_string (v[i]);
	if (v.size () > 8) s += ",...";
	return s.empty () ? "-" : s;
}

// runs nfr frames of h, sampling rest metrics; kick: velocity added to kickBody before frame kickAt
void Run (Loop &L, Rest *rs, Tally &T, double h, int nfr, int kickAt = -1, int kickBody = 1, const Vector &kick = Vector ())
{
	if (rs) rs->Start (L);
	for (int k = 0; k < nfr; k++) {
		if (k == kickAt) L.body[kickBody].v += kick;
		L.Step (h);
		T.Add (L);
		if (rs) rs->Add (L, k >= nfr/2);
	}
}

void Print (const char *what, double h, const Rest &r, const Tally &T, const LogCapture &lc)
{
	std::printf ("%s h %.4f: drift corner %.2e mean %.2e across %.2e m; hop %.2e, late |vn| %.2e |v| %.2e, final |vn| %.2e |v| %.2e m/s; FIRST frames %s, event frames %s, dKE %.3e J, D4 %.3e J; "
		"CORE %d, not RESTING after 10: %d, no contact %d, GRACE %d, poscorr %d, load frames %d, rounds %d, resweeps %d, P/L residual %.1e, log lines %d (warn %d)\n",
		what, h, r.dn, r.dmean, r.dt, r.hop, r.vnLate, r.vLate, r.vnEnd, r.vEnd, Frames (T.firstAt).c_str (), Frames (T.evAt).c_str (), T.dKE, T.dent,
		T.core, T.notResting, T.noContact, T.grace, T.poscorr, T.load, T.rounds, T.resweeps, T.pl, (int)g_log.size (), lc.AtLeast (COLLLOG_WARN));
}

// U23 rest: full at h <= 0.1, drift and late normal speed at load-cap frames; creep: tang only
void RequireRest (const Rest &r, const Tally &T, const LogCapture &lc, double h, double tang, bool creep = false)
{
	CHECK (T.noContact == 0);
	CHECK (T.notResting == 0);                     // after frame 10 only RESTING results solved (CORE superseded by Resweep)
	CHECK (T.firstAt == std::vector<int> (1, 0));  // FIRST only at the placement frame
	CHECK ((T.evAt.empty () || T.evAt == std::vector<int> (1, 0))); // no event but the placement frame's FIRST one at approach 0 (G4: one FIRST frame)
	CHECK (std::fabs (T.dKE) < 1e-9);              // damage energy 0 (Y3')
	CHECK (T.dent == 0.0);
	CHECK (T.splitOk);
	CHECK (T.grace == 0);
	CHECK (lc.Count ("GRACE") == 0);
	CHECK (lc.AtLeast (COLLLOG_WARN) == 0);        // no check warning, no raw intersection, no error
	CHECK (r.dmean < 1e-4);
	CHECK (r.vnLate < 1e-6);
	if (h <= 0.1 + 1e-12) {
		CHECK (r.dn < 1e-4);
		CHECK (r.dt < tang);
		CHECK (r.hop < 1e-6);
		CHECK ((creep ? r.vnEnd : r.vEnd) < 1e-6);
	}
}

// G1, G4, D4 U18: box (half extents hb, mass m) gap above a plate moving at V, constant gravity
Rest PlateScene (Loop &L, const Geo &plate, const Geo &box, const Vector &hb, double m, const Vector &V, double gap)
{
	LBody s;
	s.id = 1; s.kind = COLLB_BASE; s.V = V;
	s.parts.push_back (Part (plate.geom, BKey (0), Vector (0, -0.5, 0), 0)); // top face at y = 0 of the base frame
	L.body.push_back (s);
	LBody b;
	b.id = 2; b.m = m; b.pmi = BoxPmi (hb);
	b.x = Vector (0, hb.y + SKIN2 + gap, 0); b.v = V;
	b.parts.push_back (Part (box.geom, VKey (2), Vector (), 1));
	L.body.push_back (b);
	L.g = Vector (0, -G0, 0);
	Rest r;
	r.ns = Vector (0, 1, 0); r.corner = SupportFace (hb, 1, Vector ());
	return r;
}

// G2: payload box in the Atlantis bay floor V (group 30), carrier thrust along floor normal
Rest BayScene (Loop &L, const Geo &atl, const Geo &pay, const Vector &hp, double thrust)
{
	double fl = -1e300;
	for (double x : { -hp.x, hp.x })
		for (double z : { -hp.z, 0.0, hp.z }) {
			CollRayHit hit;
			REQUIRE (CollRayCast (atl.geom, CollAffine (), Vector (x, 0, z), Vector (0, -1, 0), 0.0, 10.0, hit));
			fl = std::max (fl, -hit.t);
		}
	LBody c;
	c.id = 1; c.m = 1e5; c.pmi = Vector (78.2, 82.1, 10.7); c.thrust = Vector (0, thrust, 0);
	c.parts.push_back (Part (atl.geom, VKey (1), Vector (), 1));
	L.body.push_back (c);
	LBody p;
	p.id = 2; p.m = 5000; p.pmi = BoxPmi (hp);
	p.x = Vector (0, fl + SKIN2 + RGAP + hp.y, 0);
	p.parts.push_back (Part (pay.geom, VKey (2), Vector (), 1));
	L.body.push_back (p);
	Rest r;
	r.ns = Vector (0, 1, 0); r.corner = SupportFace (hp, 1, Vector ());
	return r;
}

// G3: box, CG 0.5 m off support centre, roof 20 m up at an equator base, Earth rotation, central g
Rest RoofScene (Loop &L, const Geo &roof, const Geo &ves, const Vector &hv, const Vector &cg)
{
	LBody s;
	s.id = 1; s.kind = COLLB_BASE; s.W = Vector (0, W_E, 0); s.cl = Vector (R_E, 0, 0);
	s.parts.push_back (Part (roof.geom, BKey (0), Vector (10, 0, 0), 0)); // block of half size 10 m, roof at base x = 20 (radial)
	L.body.push_back (s);
	LBody b;
	b.id = 2; b.m = 2000; b.pmi = Vector (1.2, 1.4, 1.0);
	b.x = Vector (R_E + 20 + SKIN2 + RGAP + hv.x, 0, 0) + cg; b.v = Xc (s.W, b.x); b.wb = s.W;
	b.parts.push_back (Part (ves.geom, VKey (2), -cg, 1));
	L.body.push_back (b);
	L.GM = GM_E;
	Rest r;
	r.ns = Vector (1, 0, 0); r.corner = SupportFace (hv, 0, -cg);
	return r;
}

const Vector HPLATE (20, 0.5, 20), HBOX (1, 1, 1), HDG (9, 2, 9); // plate, 2 m box (G1, G4), DG-size box (D4 U18)
const Vector HPAY (0.6, 0.6, 2.0), HROOF (10, 10, 10), HVES (1, 1.5, 1); // bay payload, roof block, vessel on the roof

} // namespace

TEST_CASE ("U23 G1: 2 m box resting on a plate, static and moving at 465 m/s", "[CollLoop][U23][G1]")
{
	Geo plate (Box (HPLATE)), box (Box (HBOX));
	struct Row { double V, h; };
	const Row rows[] = { { 0, 1.0/60 }, { 0, 0.1 }, { 0, 0.25 }, { 465, 1.0/60 }, { 465, 0.1 } }; // 0.25 s: the load cap (Y6', D3 4.8 row A)
	for (const Row &w : rows) {
		Loop L;
		Rest r = PlateScene (L, plate, box, HBOX, 1000.0, Vector (w.V, 0, 0), RGAP);
		Tally T;
		T.ma = DentMath::DefaultMaterial (DENTB_BLOCK);
		LogCapture lc;
		Run (L, &r, T, w.h, NFRAME);
		char what[64];
		std::snprintf (what, sizeof (what), "U23 G1 plate at %g m/s", w.V);
		Print (what, w.h, r, T, lc);
		RequireRest (r, T, lc, w.h, 1e-4);
		CHECK (T.load >= NFRAME - 1);              // pressed under gravity: the 0.25 s load cap holds (D3 10.7)
	}
}

TEST_CASE ("U23 G2: payload resting on the Atlantis bay floor of a carrier thrusting 1 and 9.81 m/s^2", "[CollLoop][U23][G2]")
{
	Geo atl (AtlantisMesh ()), pay (Box (HPAY));
	struct Row { double a, h; };
	const Row rows[] = { { 1, 1.0/60 }, { 1, 0.1 }, { 9.81, 1.0/60 }, { 9.81, 0.1 }, { 9.81, 0.25 } }; // 0.25 s: D3 4.8 row C
	for (const Row &w : rows) {
		Loop L;
		Rest r = BayScene (L, atl, pay, HPAY, w.a);
		Tally T;
		LogCapture lc;
		Run (L, &r, T, w.h, NFRAME);
		char what[64];
		std::snprintf (what, sizeof (what), "U23 G2 bay, thrust %g m/s^2", w.a);
		Print (what, w.h, r, T, lc);
		RequireRest (r, T, lc, w.h, 1e-4);
		CHECK (T.pl < 1e-12);                      // pair P and L exact through the write-back (D3 5.2)
	}
}

TEST_CASE ("U23 G3: vessel with an off-centre CG on a roof at the equator, Earth rotation, central gravity", "[CollLoop][U23][G3]")
{
	Geo roof (Box (HROOF)), ves (Box (HVES));
	for (double h : { 1.0/60, 0.1, 0.25 }) {         // 0.25 s: the load cap (Y6', D3 4.8 row B)
		Loop L;
		Rest r = RoofScene (L, roof, ves, HVES, Vector (0, 0.4, -0.3));
		Tally T;
		T.ma = DentMath::DefaultMaterial (DENTB_BLOCK);
		LogCapture lc;
		Run (L, &r, T, h, NFRAME);
		Print ("U23 G3 roof, Earth rotation", h, r, T, lc);
		RequireRest (r, T, lc, h, 1e-3, true);     // |v_rel . n| < 1e-6 m/s; tangential creep (D3 4.8 B) bounded by the drift < 1e-3 m (D3 15.2 v2.2)
		CHECK (T.load >= NFRAME - 1);
	}
}

TEST_CASE ("G4 Y3' settling: box resting on a roof at h = 0.17 s; box dropped 0.1 m at 1/60 and 0.17 s", "[CollLoop][G4]")
{
	Geo plate (Box (HPLATE)), box (Box (HBOX));
	const double m = 1000.0, drop = 0.1;
	{
		Loop L;
		Rest r = PlateScene (L, plate, box, HBOX, m, Vector (), RGAP);
		Tally T;
		T.ma = DentMath::DefaultMaterial (DENTB_BLOCK);
		LogCapture lc;
		Run (L, &r, T, 0.17, NFRAME);
		Print ("G4 resting on a roof", 0.17, r, T, lc);
		RequireRest (r, T, lc, 0.17, 1e-4);        // one FIRST frame (frame 0) and damage energy < 1e-9 J
	}
	for (double h : { 1.0/60, 0.17 }) {
		Loop L;
		Rest r = PlateScene (L, plate, box, HBOX, m, Vector (), drop);
		Tally T;
		T.ma = DentMath::DefaultMaterial (DENTB_BLOCK);
		LogCapture lc;
		r.Start (L);
		CollImpactEvent hit {};
		int nhit = 0, kimp = -1;
		for (int k = 0; k < NFRAME; k++) {
			L.Step (h);
			T.Add (L);
			r.Add (L, k >= NFRAME/2);
			for (const CollImpactEvent &e : L.ev) if (e.flags & COLLEV_FIRST) { hit = e; nhit++; kimp = k; }
		}
		double u = hit.vn, e = CollRestitution (u, COLL_E0, COLL_VY, L.sp), want = 0.5*m*u*u*(1.0 - e*e), gapEnd = drop + r.gapEnd;
		std::printf ("G4 drop %.2f m, h %.4f: FIRST frames %s (impact frame %d, kind %s), event frames %s; vn %.6f m/s (free fall g t %.6f), dKE %.3f J (0.5 m vn^2 (1 - e^2) %.3f, m g d %.1f), Wf %.2f J, D4 %.1f J; "
			"final gap %.4f m, final |v| %.2e m/s, CORE %d, resweeps %d, GRACE %d, log warn %d\n",
			drop, h, Frames (T.firstAt).c_str (), kimp, (hit.flags & COLLEV_RESTING) ? "RESTING" : "TOI", Frames (T.evAt).c_str (), u, G0*hit.t, hit.dKE, want, m*G0*drop, hit.Wf, T.dent,
			gapEnd, r.vEnd, T.core, T.resweeps, T.grace, lc.AtLeast (COLLLOG_WARN));
		CHECK (nhit == 1);
		CHECK (T.firstAt == std::vector<int> (1, kimp)); // FIRST only in the impact frame
		CHECK (std::fabs (u - G0*hit.t) < 1e-9);    // approach = free-fall speed at the solved time
		CHECK (u > std::sqrt (2.0*G0*(drop - COLL_DELTA_CT)));
		CHECK (u <= std::sqrt (2.0*G0*drop));
		CHECK (std::fabs (hit.dKE - want) < 1e-6*want); // first-touch energy of a central impact
		CHECK (std::fabs (hit.Wf) <= 1e-9*hit.dKE);  // and no slip: no friction work (Y3' point work)
		CHECK (std::fabs (T.dKE - hit.dKE) < 1e-9); // later contacts add nothing (Y3')
		CHECK (T.dent > 0.0);                      // vn > 1 m/s: a real impact dents (positive control of the gate)
		CHECK (T.splitOk);
		CHECK (T.grace == 0);
		CHECK (lc.AtLeast (COLLLOG_WARN) == 0);
		CHECK (r.vEnd < 1e-6);                      // settled
		CHECK (gapEnd > -COLL_SLOP);
		CHECK (gapEnd < COLL_DELTA_CT);
	}
}

TEST_CASE ("D3 U16 end to end: Y3' impact energy through Detect and Run is the same at every frame length", "[CollLoop][U16]")
{
	Geo plate (Box (HPLATE)), box (Box (HBOX));
	const double m = 1000.0;
	for (double u : { 0.5, 1.4, 3.0, 5.0 }) {
		double e = CollRestitution (u, COLL_E0, COLL_VY, CollSolveParams ()), want = 0.5*m*u*u*(1.0 - e*e);
		for (double h : { 1.0/200.0, 1.0/60.0, 0.1 }) {
			Loop L;
			PlateScene (L, plate, box, HBOX, m, Vector (), 0.3);
			L.g = Vector ();                             // no gravity: one approach at u, one bounce
			L.body[1].v = Vector (0, -u, 0);
			Tally T;
			LogCapture lc;
			Run (L, nullptr, T, h, (int)std::lround (2.0/h));
			bool first = false;
			for (const Tally::Ev &x : T.ev) first = first || x.first;
			std::printf ("D3 U16 end to end: u %.1f m/s, h %.4f: FIRST frames %s, events %zu, dKE %.6f J (0.5 m u^2 (1 - e^2) %.6f J), Wf %.2e J\n",
				u, h, Frames (T.firstAt).c_str (), T.ev.size (), T.dKE, want, T.ev.empty () ? 0.0 : T.ev[0].Wf);
			CHECK (first);
			CHECK (T.firstAt.size () == 1);              // the hit, whether it arrives as RESTING or TOI, and nothing after
			CHECK (std::fabs (T.dKE - want) < 1e-6*want);
			CHECK (lc.AtLeast (COLLLOG_WARN) == 0);
		}
	}
}

TEST_CASE ("D4 U18 settling under Y3': DG-size box at h = 0.17 s; positive control after a 10 cm hop at 1/60 s", "[CollLoop][U18]")
{
	Geo plate (Box (HPLATE)), dg (Box (HDG));
	const double m = 24500.0;
	for (double gap : { 0.02, 0.05 }) {              // 2 cm above the plate's skin (inside the touching band) and 2 cm above the band (a real drop)
		Loop L;
		Rest r = PlateScene (L, plate, dg, HDG, m, Vector (), gap);
		Tally T;
		T.ma = DentMath::DefaultMaterial (DENTB_BLOCK);
		LogCapture lc;
		Run (L, &r, T, 0.17, NFRAME);
		char what[64];
		std::snprintf (what, sizeof (what), "D4 U18 DG box released %.2f m up", gap);
		Print (what, 0.17, r, T, lc);
		CHECK (T.firstAt == std::vector<int> (1, 0));
		CHECK (T.dent == 0.0);                     // sum of D4 damage energy 0 J
		CHECK (T.splitOk);
		CHECK (T.noContact == 0);
		CHECK (T.grace == 0);
		CHECK (lc.AtLeast (COLLLOG_WARN) == 0);
		CHECK (r.vEnd < 1e-6);
	}
	{
		Loop L;
		Rest r = PlateScene (L, plate, dg, HDG, m, Vector (), 0.005);
		Tally T;
		T.ma = DentMath::DefaultMaterial (DENTB_BLOCK);
		LogCapture lc;
		Run (L, &r, T, 1.0/60, NFRAME, 20, 1, Vector (0, 1.4, 0)); // upward kick of 1.4 m/s at frame 20: a 10 cm hop
		Print ("D4 U18 DG box, 10 cm hop", 1.0/60, r, T, lc);
		for (const Tally::Ev &e : T.ev) std::printf ("D4 U18 hop: event frame %d, FIRST %d, vn %.4f m/s, dKE %.1f J, Wf %.1f J, D4 %.1f J\n", e.frame, (int)e.first, e.vn, e.dKE, e.Wf, e.E);
		REQUIRE (T.firstAt.size () >= 2);
		CHECK (T.firstAt[0] == 0);
		CHECK (T.firstAt[1] > 20);                 // the re-touch after the pair went APART
		CHECK (T.noContact > 0);
		CHECK (T.dent > 0.0);                      // positive control (prototype 9.6 kJ)
		for (const Tally::Ev &e : T.ev) {
			if (e.frame == T.firstAt[1]) { CHECK (e.vn > DENT_VN_GATE); CHECK (e.E == T.dent); } // all of it from the re-touch
			else CHECK (e.E == 0.0);               // later band-edge re-touches (D2 3.6) stay below the gate
		}
		CHECK (T.splitOk);
		CHECK (lc.AtLeast (COLLLOG_WARN) == 0);
	}
}

TEST_CASE ("fix1 R4: a box lowered at 1 mm/s inside the touching band settles to gap <= slop under load", "[CollLoop]")
{
	Geo plate (Box (HPLATE)), box (Box (HBOX));
	const double gap = 0.025;                                        // inside delta_ct: RESTING from the first frame, FIRST there only
	for (double h : { 1.0/60, 0.1, 0.17 }) {
		Loop L;
		Rest r = PlateScene (L, plate, box, HBOX, 1000.0, Vector (), gap);
		L.body[1].v = Vector (0, -0.001, 0);
		Tally T;
		LogCapture lc;
		Run (L, &r, T, h, (int)std::lround (10.0/h));
		double gapEnd = gap + r.gapEnd;
		std::printf ("fix1 R4 lowered at 1 mm/s under g, h %.4f: final gap %.6f m, final |v| %.2e m/s, FIRST frames %s, event frames %s\n", h, gapEnd, r.vEnd, Frames (T.firstAt).c_str (), Frames (T.evAt).c_str ());
		CHECK (gapEnd <= COLL_SLOP + 1e-9);
		CHECK (gapEnd >= -COLL_SLOP);
		CHECK (r.vEnd < 1e-6);
		CHECK (T.firstAt == std::vector<int> (1, 0));
		CHECK (T.dent == 0.0);
		CHECK (lc.AtLeast (COLLLOG_WARN) == 0);
	}
}
