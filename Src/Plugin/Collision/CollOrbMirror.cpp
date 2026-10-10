// not upstream: collision addon E1 6.1, line-by-line mirror of Orbiter's step (Rigidbody.cpp:165-262, BodyIntegrator.cpp:59-425)
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <vector>
#include "CollOrbMirror.h"
#include "CollGeom.h"

namespace {

constexpr double RAD = 3.14159265358979323846/180.0;

bool CaseEq (const char *a, const char *b, size_t n)   // strncasecmp (a, b, n) == 0
{
	for (size_t i = 0; i < n; i++) if (std::tolower ((unsigned char)a[i]) != std::tolower ((unsigned char)b[i])) return false;
	return true;
}

// RK5-RK8 tables of BodyIntegrator.cpp:59-147
static const int RK5_n = 6;
static const double RK5_alpha[RK5_n-1] = {
	1.0/5.0, 3.0/10.0, 4.0/5.0, 8.0/9.0, 1.0
};
static const double RK5_beta[(RK5_n-1)*(RK5_n-1)] = {
	1.0/5.0, 0, 0, 0, 0,
	3.0/40.0, 9.0/40.0, 0, 0, 0,
	44.0/45.0, -56.0/15.0, 32.0/9.0, 0, 0,
	19372.0/6561.0, -25360.0/2187.0, 64448.0/6561.0, -212.0/729.0, 0,
	9017.0/3168.0, -355.0/33.0, 46732.0/5247.0, 49.0/176.0, -5103.0/18656.0
};
static const double RK5_gamma[RK5_n] = {
	35.0/384.0, 0, 500.0/1113.0, 125.0/192.0, -2187.0/6784.0, 11.0/84.0
};


static const int RK6_n = 8;
static const double RK6_alpha[RK6_n-1] = {
	1.0/6.0, 4.0/15.0, 2.0/3.0, 5.0/6.0, 1.0, 1.0/15.0, 1.0
};
static const double RK6_beta[(RK6_n-1)*(RK6_n-1)] = {
	1.0/6.0, 0, 0, 0, 0, 0, 0,
	4.0/75.0, 16.0/75.0, 0, 0, 0, 0, 0,
	5.0/6.0, -8.0/3.0, 5.0/2.0, 0, 0, 0, 0,
	-165.0/64.0, 55.0/6.0, -425.0/64.0, 85.0/96.0, 0, 0, 0,
	12.0/5.0, -8.0, 4015.0/612.0, -11.0/36.0, 88.0/255.0, 0, 0,
	-8263.0/15000.0, 124.0/75.0, -643.0/680.0, -81.0/250.0, 2484.0/10625.0, 0, 0,
	3501.0/1720.0, -300.0/43.0, 297275.0/52632.0, -319.0/2322.0, 24068.0/84065.0, 0, 3850.0/26703.0
};
static const double RK6_gamma[RK6_n] = {
	3.0/40.0, 0, 875.0/2244.0, 23.0/72.0, 264.0/1955.0, 0, 125.0/11592.0, 43.0/616.0
};


static const int RK7_n = 11;
static const double RK7_alpha[RK7_n-1] = {
	2.0/27.0, 1.0/9.0, 1.0/6.0, 5.0/12.0, 1.0/2.0, 5.0/6.0, 1.0/6.0, 2.0/3.0, 1.0/3.0, 1.0
};
static const double RK7_beta[(RK7_n-1)*(RK7_n-1)] = {
	2.0/27.0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1.0/36.0, 1.0/12.0, 0, 0, 0, 0, 0, 0, 0, 0,
	1.0/24.0, 0, 1.0/8.0, 0, 0, 0, 0, 0, 0, 0,
	5.0/12.0, 0, -25.0/16.0, 25.0/16.0, 0, 0, 0, 0, 0, 0,
	1.0/20.0, 0, 0, 1.0/4.0, 1.0/5.0, 0, 0, 0, 0, 0,
	-25.0/108.0, 0, 0, 125.0/108.0, -65.0/27.0, 125.0/54.0, 0, 0, 0, 0,
	31.0/300.0, 0, 0, 0, 61.0/225.0, -2.0/9.0, 13.0/900.0, 0, 0, 0,
	2.0, 0, 0, -53.0/6.0, 704.0/45.0, -107.0/9.0, 67.0/90.0, 3.0, 0, 0,
	-91.0/108.0, 0, 0, 23.0/108.0, -976.0/135.0, 311.0/54.0, -19.0/60.0, 17.0/6.0, -1.0/12.0, 0,
	2383.0/4100.0, 0, 0, -341.0/164.0, 4496.0/1025.0, -301.0/82.0, 2133.0/4100.0, 45.0/82.0, 45.0/164.0, 18.0/41.0
};
static const double RK7_gamma[RK7_n] = {
	41.0/840.0, 0, 0, 0, 0, 34.0/105.0, 9.0/35.0, 9.0/35.0, 9.0/280.0, 9.0/280.0, 41.0/840.0
};


static const int RK8_n = 13;
static const double RK8_alpha[RK8_n-1] = {
	2.0/27.0, 1.0/9.0, 1.0/6.0, 5.0/12.0, 1.0/2.0, 5.0/6.0, 1.0/6.0, 2.0/3.0, 1.0/3.0, 1.0, 0, 1.0
};
static const double RK8_beta[(RK8_n-1)*(RK8_n-1)] = {
	2.0/27.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1.0/36.0, 1.0/12.0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	1.0/24.0, 0, 1.0/8.0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	5.0/12.0, 0, -25.0/16.0, 25.0/16.0, 0, 0, 0, 0, 0, 0, 0, 0,
	1.0/20.0, 0, 0, 1.0/4.0, 1.0/5.0, 0, 0, 0, 0, 0, 0, 0,
	-25.0/108.0, 0, 0, 125.0/108.0, -65.0/27.0, 125.0/54.0, 0, 0, 0, 0, 0, 0,
	31.0/300.0, 0, 0, 0, 61.0/225.0, -2.0/9.0, 13.0/900.0, 0, 0, 0, 0, 0,
	2.0, 0, 0, -53.0/6.0, 704.0/45.0, -107.0/9.0, 67.0/90.0, 3.0, 0, 0, 0, 0,
	-91.0/108.0, 0, 0, 23.0/108.0, -976.0/135.0, 311.0/54.0, -19.0/60.0, 17.0/6.0, -1.0/12.0, 0, 0, 0,
	2383.0/4100.0, 0, 0, -341.0/164.0, 4496.0/1025.0, -301.0/82.0, 2133.0/4100.0, 45.0/82.0, 45.0/164.0, 18.0/41.0, 0, 0,
	3.0/205.0, 0, 0, 0, 0, -6.0/41.0, -3.0/205.0, -3.0/41.0, 3.0/41.0, 6.0/41.0, 0, 0,
	-1777.0/4100.0, 0, 0, -341.0/164.0, 4496.0/1025.0, -289.0/82.0, 2193.0/4100.0, 51.0/82.0, 33.0/164.0, 12.0/41.0, 0, 1.0
};
static const double RK8_gamma[RK8_n] = {
	0, 0, 0, 0, 0, 34.0/105.0, 9.0/35.0, 9.0/35.0, 9.0/280.0, 9.0/280.0, 0, 41.0/840.0, 41.0/840.0
};

struct RKTab { int n; const double *alpha, *beta, *gamma; };
RKTab Tab (int m)
{
	switch (m) {
	case COLLM_RK5: return { RK5_n, RK5_alpha, RK5_beta, RK5_gamma };
	case COLLM_RK6: return { RK6_n, RK6_alpha, RK6_beta, RK6_gamma };
	case COLLM_RK7: return { RK7_n, RK7_alpha, RK7_beta, RK7_gamma };
	default:        return { RK8_n, RK8_alpha, RK8_beta, RK8_gamma };
	}
}

// Vessel.cpp:917-925 without surface forces: a = aC + Q F/m, tau = M/m + unseen
void Moments (const CollOrbState &o, const StateVectors &st, Vector &a, Vector &tau)
{
	a = o.aC + mul (st.Q, o.Fadd/o.m);
	tau = o.Madd/o.m + o.tauU;
}

void RK2 (CollOrbState &o, double h)                 // BodyIntegrator.cpp:157-174
{
	StateVectors &s = o.s;
	double h05 = h*0.5;
	Vector acc1, tau;
	StateVectors st;
	st.pos = s.pos + s.vel*h05; st.vel = s.vel + o.acc*h05;
	st.SetRot (s.Q.Rot (s.omega*h05)); st.omega = s.omega + o.arot*h05;
	Moments (o, st, acc1, tau);
	s.pos += st.vel*h; s.vel += acc1*h;
	s.Q.Rotate (st.omega*h);
	o.arot = o.EulerInv (tau, st.omega); s.omega += o.arot*h;
}

void RK4 (CollOrbState &o, double h)                 // BodyIntegrator.cpp:180-212
{
	StateVectors &s = o.s;
	double h05 = h*0.5, hi6 = h/6.0;
	Vector tau, acc1, aacc1, acc2, aacc2, acc3, aacc3;
	StateVectors sa, sb, sc;
	sa.pos = s.pos + s.vel*h05; sa.vel = s.vel + o.acc*h05; sa.SetRot (s.Q.Rot (s.omega*h05)); sa.omega = s.omega + o.arot*h05;
	Moments (o, sa, acc1, tau); aacc1 = o.EulerInv (tau, sa.omega);
	sb.pos = s.pos + sa.vel*h05; sb.vel = s.vel + acc1*h05; sb.SetRot (s.Q.Rot (sa.omega*h05)); sb.omega = s.omega + aacc1*h05;
	Moments (o, sb, acc2, tau); aacc2 = o.EulerInv (tau, sb.omega);
	sc.pos = s.pos + sb.vel*h; sc.vel = s.vel + acc2*h; sc.SetRot (s.Q.Rot (sb.omega*h)); sc.omega = s.omega + aacc2*h;
	Moments (o, sc, acc3, tau); aacc3 = o.EulerInv (tau, sc.omega);
	Vector dv = (o.acc + (acc1 + acc2)*2.0 + acc3)*hi6, dp = (s.vel + (sa.vel + sb.vel)*2.0 + sc.vel)*hi6;
	s.Q.Rotate ((s.omega + (sa.omega + sb.omega)*2.0 + sc.omega)*hi6);
	s.omega += (o.arot + (aacc1 + aacc2)*2.0 + aacc3)*hi6;
	s.vel += dv; s.pos += dp;
}

void RKdrv (CollOrbState &o, double h, const RKTab &t) // BodyIntegrator.cpp:219-256
{
	StateVectors &s = o.s;
	std::vector<StateVectors> ss (t.n);
	std::vector<Vector> a (t.n), d (t.n);
	Vector tau;
	const double *beta = t.beta;
	ss[0].Set (s.vel, s.pos, s.omega, s.Q); a[0] = o.acc; d[0] = o.arot;
	for (int i = 1; i < t.n; i++) {
		ss[i].Set (s.vel, s.pos, s.omega, s.Q);
		for (int j = 0; j < i; j++) ss[i].Advance (beta[j]*h, a[j], ss[j].vel, d[j], ss[j].omega);
		Moments (o, ss[i], a[i], tau); d[i] = o.EulerInv (tau, ss[i].omega);
		beta += t.n - 1;
	}
	for (int i = 0; i < t.n; i++) {
		double bh = t.gamma[i]*h;
		s.vel += a[i]*bh; s.pos += ss[i].vel*bh; s.Q.Rotate (ss[i].omega*bh); s.omega += d[i]*bh;
	}
}

void SYdrv (CollOrbState &o, double h, const double *c, const double *dd, int nc) // BodyIntegrator.cpp:301-425
{
	StateVectors &s = o.s;
	StateVectors st;
	Vector tau;
	for (int i = 0; i < nc; i++) {
		double step = h*c[i];
		s.pos += s.vel*step;
		s.Q.Rotate (s.omega*step);
		if (i != nc - 1) {
			st.Set (s.vel, s.pos, s.omega, s.Q);
			Moments (o, st, o.acc, tau);
			o.arot = o.EulerInv (tau, st.omega);
			s.vel += o.acc*(h*dd[i]);
			s.omega += o.arot*(h*dd[i]);
		}
	}
}

void SY (CollOrbState &o, double h, int m)
{
	if (m == COLLM_SY2) { const double c[2] = { 0.5, 0.5 }, d[1] = { 1.0 }; SYdrv (o, h, c, d, 2); return; }
	if (m == COLLM_SY4) {
		const double b = 1.25992104989487319066654436028, a = 2 - b, x0 = -b/a, x1 = 1./a;
		const double d[3] = { x1, x0, x1 }, c[4] = { x1/2, (x0 + x1)/2, (x0 + x1)/2, x1/2 };
		SYdrv (o, h, c, d, 4); return;
	}
	if (m == COLLM_SY6) {
		const double w1 = -0.117767998417887E1, w2 = 0.235573213359357E0, w3 = 0.784513610477560E0, w0 = (1 - 2*(w1 + w2 + w3));
		const double d[7] = { w3, w2, w1, w0, w1, w2, w3 };
		const double c[8] = { w3/2, (w3 + w2)/2, (w2 + w1)/2, (w1 + w0)/2, (w1 + w0)/2, (w2 + w1)/2, (w3 + w2)/2, w3/2 };
		SYdrv (o, h, c, d, 8); return;
	}
	const double W1 = 0.311790812418427e0, W2 = -0.155946803821447e1, W3 = -0.167896928259640e1, W4 = 0.166335809963315e1;
	const double W5 = -0.106458714789183e1, W6 = 0.136934946416871e1, W7 = 0.629030650210433e0, W0 = (1 - 2*(W1 + W2 + W3 + W4 + W5 + W6 + W7));
	const double d[15] = { W7, W6, W5, W4, W3, W2, W1, W0, W1, W2, W3, W4, W5, W6, W7 };
	const double c[16] = { W7/2, (W7 + W6)/2, (W6 + W5)/2, (W5 + W4)/2, (W4 + W3)/2, (W3 + W2)/2, (W2 + W1)/2, (W1 + W0)/2,
		(W1 + W0)/2, (W2 + W1)/2, (W3 + W2)/2, (W4 + W3)/2, (W5 + W4)/2, (W6 + W5)/2, (W7 + W6)/2, W7/2 };
	SYdrv (o, h, c, d, 16);
}

} // namespace

Vector CollOrbState::SpinL () const
{
	return mul (s.R, Vector (pmi.x*s.omega.x, pmi.y*s.omega.y, pmi.z*s.omega.z)*m);
}

Vector CollOrbState::EulerInv (const Vector &tau, const Vector &w) const
{
	return Vector ((tau.x - (pmi.y - pmi.z)*w.y*w.z)/pmi.x, (tau.y - (pmi.z - pmi.x)*w.z*w.x)/pmi.y, (tau.z - (pmi.x - pmi.y)*w.x*w.y)/pmi.z);
}

CollOrbMirror::CollOrbMirror ()
{
	const double a[COLLM_LEVELS] = { 0.2*RAD, 2*RAD, 5*RAD, 20*RAD, 50*RAD }, l[COLLM_LEVELS] = { 1.0*RAD, 4.0*RAD, 10.0*RAD, 1e10, 1e10 };
	for (int i = 0; i < COLLM_LEVELS; i++) { atgt[i] = a[i]; alim[i] = l[i]; }
}

// Config.cpp:597-619: GetInt/GetReal/GetBool on the first matching line; the last level unlimited
void CollOrbMirror::ReadCfg (const std::function<bool (const char *key, std::string &val)> &str)
{
	const CollOrbMirror def;
	std::string v;
	if (str ("StabiliseOrbits", v)) {
		if (v.size () >= 4 && CaseEq (v.c_str (), "true", 4)) stabilise = true;
		else if (v.size () >= 5 && CaseEq (v.c_str (), "false", 5)) stabilise = false;
	}
	double r; int i;
	if (str ("StabilisePLimit", v) && std::sscanf (v.c_str (), "%lf", &r) == 1) pLimit = r;
	if (str ("StabiliseSLimit", v) && std::sscanf (v.c_str (), "%lf", &r) == 1) sLimit = r;
	if (str ("PropStages", v) && std::sscanf (v.c_str (), "%d", &i) == 1 && i >= 1 && i <= COLLM_LEVELS) nLevel = i;
	for (int k = 0; k < COLLM_LEVELS; k++) {
		char tag[32];
		std::snprintf (tag, sizeof (tag), "PropStage%d", k);
		if (!str (tag, v)) continue;
		int md; double tt, at, tl, al;
		int n = std::sscanf (v.c_str (), "%d%lf%lf%lf%lf", &md, &tt, &at, &tl, &al);
		if (n >= 1 && md >= 0 && md < COLLM_N) mode[k] = md;
		if (n >= 2 && std::fabs (tt - def.ttgt[k]) > 1e-6) ttgt[k] = tt;
		if (n >= 3 && std::fabs (at - def.atgt[k]) > 1e-6) atgt[k] = at;
		if (n >= 4 && std::fabs (tl - def.tlim[k]) > 1e-6) tlim[k] = tl;
		if (n >= 5 && std::fabs (al - def.alim[k]) > 1e-6) alim[k] = al;
	}
	tlim[nLevel - 1] = 1e10; alim[nLevel - 1] = 1e10;
	if (str ("PropSubsampling", v) && std::sscanf (v.c_str (), "%d", &i) == 1) {
		if (i < 1) CollLog (COLLLOG_WARN, "Collision prop: PropSubsampling %d is invalid (below 1), 1 used", i);
		subMax = std::max (1, i);                             // the core's min (PropSubMax, n) would take no substep (P4)
	}
}

void CollOrbMirror::Choose (double H, double wlen, bool ground, int &lv, int &ns) const
{
	if (ground) { lv = nLevel - 1; ns = std::max (1, subMax); return; }   // VesselBase::SetPropagator while bSurfaceContact
	for (lv = 0; lv < nLevel - 1; lv++) if (H < tlim[lv]) break;
	double astep = wlen*H;
	for (; lv < nLevel - 1; lv++) if (astep < alim[lv]) break;
	double d = 1.0;                                       // in double: targets <= 0 ignored, NaN and inf clamped to [1, subMax] before the int cast
	if (ttgt[lv] > 0.0) d = std::max (d, H/ttgt[lv]);
	if (atgt[lv] > 0.0) d = std::max (d, astep/atgt[lv]);
	d = std::ceil (d);
	if (!(d >= 1.0)) d = 1.0;
	if (d > (double)subMax) d = (double)subMax;
	ns = d >= 1.0 ? (int)d : 1;
}

void CollOrbMirror::Step (CollOrbState &o, double H, int forceLv, int forceN) const
{
	Choose (H, o.s.omega.length (), o.ground, o.lv, o.nsub);
	if (forceLv >= 0) { o.lv = std::min (forceLv, nLevel - 1); o.nsub = std::max (1, forceN); }
	o.method = mode[o.lv];
	double k = H/o.nsub;
	Vector tau;
	for (int i = 0; i < o.nsub; i++) {
		if (o.method == COLLM_RK2) RK2 (o, k);
		else if (o.method == COLLM_RK4) RK4 (o, k);
		else if (o.method <= COLLM_RK8) RKdrv (o, k, Tab (o.method));
		else SY (o, k, o.method);
		o.s.R.Set (o.s.Q);
		Moments (o, o.s, o.acc, tau);
		o.arot = o.EulerInv (tau, o.s.omega);
	}
	o.Fadd = o.Madd = Vector ();                          // Vessel.cpp:4937-4940
}

bool CollOrbMirror::Encke (const Vector &r, const Vector &v, double H) const
{
	double rl = r.length ();
	return stabilise && rl > 0.0 && v.length ()*H/(2.0*3.14159265358979323846*rl) > sLimit;
}

double CollOrbMirror::Gamma0 (int m)
{
	if (m == COLLM_RK2 || m >= COLLM_SY2) return 0.0;
	if (m == COLLM_RK4) return 1.0/6.0;
	return Tab (m).gamma[0];
}

double CollOrbMirror::DxCoef (int m)
{
	if (m == COLLM_RK2) return 0.5;
	if (m == COLLM_RK4 || m >= COLLM_SY2) return 0.0;
	RKTab t = Tab (m);
	double s = 0;
	for (int i = 1; i < t.n; i++) s += t.gamma[i]*t.beta[(i - 1)*(t.n - 1)];
	return s - t.gamma[0];
}

// 7.4: scan steps of 0.1 ms; displacement by a new 1 g force that is absent from the cached first stage
double CollOrbMirror::HRest (double g, double tol) const
{
	double best = 0.0;
	for (int i = 1; i <= 20000; i++) {
		double h = i/10000.0;
		CollOrbState a;
		a.s.R = IMatrix (); a.s.Q.Set (a.s.R);
		CollOrbState b = a;                                    // no force at all
		a.aC = Vector (0, g, 0);                               // the force is new: not in the cached acc
		Step (a, h); Step (b, h);
		if ((a.s.pos - b.s.pos).length () > tol) break;
		best = h;
	}
	return best;
}

std::string CollOrbMirror::Describe () const
{
	static const char *nm[COLLM_N] = { "RK2", "RK4", "RK5", "RK6", "RK7", "RK8", "SY2", "SY4", "SY6", "SY8" };
	std::string s;
	char b[160];
	for (int i = 0; i < nLevel; i++) {
		std::snprintf (b, sizeof (b), "%s%s %g %g %g %g", i ? ", " : "", nm[mode[i]], ttgt[i], atgt[i], tlim[i], alim[i]);
		s += b;
	}
	std::snprintf (b, sizeof (b), "; sub %d; stabilise %d %g %g; h_rest %.4f s", subMax, stabilise ? 1 : 0, sLimit, pLimit, HRest ());
	return s + b;
}
