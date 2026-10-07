// not upstream: Poisson series fitted to JPL's ephemerides (PSR2 files), evaluated for any date; a port of seval.c's loader and state
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "the .psr files are little-endian");

class PsrSeries {
public:
	static constexpr double TWOPI = 6.283185307179586, INV2PI = 0.15915494309189535;

	// read and check a PSR2 file; false and the reason in *err on any failure
	bool Load (const char *path, const char **err)
	{
		kind = -1;
		std::vector<uint8_t> b;
		FILE *f = std::fopen (path, "rb");
		if (!f) { *err = "file not found"; return false; }
		bool ok = std::fseek (f, 0, SEEK_END) == 0;
		long n = ok ? std::ftell (f) : -1;
		ok = ok && n > 8 && n <= (16L << 20) && std::fseek (f, 0, SEEK_SET) == 0;
		if (ok) { b.resize ((size_t)n); ok = std::fread (b.data(), 1, b.size(), f) == b.size(); }
		std::fclose (f);
		if (!ok) { *err = "unreadable, or not 9 bytes to 16 MB"; return false; }
		Reader r = {b.data(), b.size(), 0, true};
		*err = Parse (r);
		if (!*err && r.o != r.n) *err = "bytes left after the last signal";
		if (*err) { kind = -1; return false; }
		return true;
	}

	bool Valid () const { return kind > 0; }

	// state wrt the parent, ecliptic J2000: pv[0..2] km, pv[3..5] km/s; t in TDB days from J2000
	void State (double t, double *pv) const
	{
		double P[33], D[33], tau = t/T0, v[3][4], X[6];
		int L = 8;
		for (int i = 0; i < ns; i++) L = std::max ({L, s[i].Dp, s[i].M, s[i].hi.empty () ? 0 : s[i].Mh});
		P[0] = 1; P[1] = tau; D[0] = 0; D[1] = 1;
		for (int m = 1; m < L; m++) { P[m+1] = ((2*m+1)*tau*P[m] - m*P[m-1])/(m+1); D[m+1] = D[m-1] + (2*m+1)*P[m]; }
		for (int m = 0; m <= L; m++) D[m] /= T0;
		for (int i = 0; i < ns; i++) Eval (s[i], t, P, D, v[i]);
		if (kind == 1) {
			double r = v[0][0], rd = v[0][2], Lg = v[1][0], Ld = v[1][2], B = v[2][0], Bd = v[2][2];
			double cB = std::cos (B), sB = std::sin (B), cL = std::cos (Lg), sL = std::sin (Lg);
			double u[3] = {cB*cL, cB*sL, sB}, uL[3] = {-cB*sL, cB*cL, 0}, uB[3] = {-sB*cL, -sB*sL, cB};
			for (int j = 0; j < 3; j++) { X[j] = r*u[j]; X[3+j] = rd*u[j] + r*Ld*uL[j] + r*Bd*uB[j]; }
		} else {
			X[0] = v[0][0]; X[1] = v[0][1]; X[2] = v[1][0]; X[3] = v[0][2]; X[4] = v[0][3]; X[5] = v[1][2];
			if (kind == 3) {
				const double h = 1e-3; // central difference step of the mean orbit's velocity [days], as seval.c
				double m0[6], m1[6], m2[6];
				MeanPos (t, m0); MeanPos (t + h, m1); MeanPos (t - h, m2);
				for (int j = 0; j < 3; j++) { X[j] += m0[j]; X[3+j] += (m1[j] - m2[j])/(2*h); }
			}
		}
		for (int j = 0; j < 3; j++) {
			pv[j] = R[j]*X[0] + R[3+j]*X[1] + R[6+j]*X[2];
			pv[3+j] = (R[j]*X[3] + R[3+j]*X[4] + R[6+j]*X[5])/86400.0;
		}
	}

private:
	struct Reader {
		const uint8_t *b; size_t n, o; bool ok;
		bool Get (void *d, size_t k) { if (!ok || k > n - o) return ok = false; std::memcpy (d, b + o, k); o += k; return true; }
	};
	struct Line { double nu = 0, beta = 0, a[18] = {}; };   // a: (re, im) per Poisson degree 0..8
	struct Sig { int real = 0, Dp = 0, M = 0, Mh = 0; double c[64] = {}; std::vector<Line> lo, hi; };

	int kind = -1, ns = 0, Dm = 0;   // form: 1 sph (r, L, B), 2 rect (w, z), 3 mrect (mean orbit + rect)
	double T0 = 0, R[9] = {}, mu = 0, Cm[6*33] = {};
	Sig s[3];

	static bool Finite (const double *x, int n) { for (int i = 0; i < n; i++) if (!std::isfinite (x[i])) return false; return true; }

	const char *Parse (Reader &r)
	{
		char mg[4];
		uint8_t h[4];
		if (!r.Get (mg, 4) || std::memcmp (mg, "PSR2", 4)) return "not a PSR2 file";
		if (!r.Get (h, 4)) return "short header";
		kind = h[0]; ns = h[1];
		if (kind < 1 || kind > 3 || ns != (kind == 1 ? 3 : 2)) return "unsupported form";
		if (!r.Get (&T0, 8) || !r.Get (R, 72) || !r.Get (&mu, 8)) return "short header";
		if (!std::isfinite (T0) || !(T0 > 0) || !Finite (R, 9) || !std::isfinite (mu) || mu < 0 || (kind == 3 && !(mu > 0))) return "bad header values";
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				if (std::fabs (R[3*i]*R[3*j] + R[3*i+1]*R[3*j+1] + R[3*i+2]*R[3*j+2] - (i == j ? 1.0 : 0.0)) > 1e-12) return "frame not orthonormal";
		if (std::fabs (R[0]*(R[4]*R[8]-R[5]*R[7]) - R[1]*(R[3]*R[8]-R[5]*R[6]) + R[2]*(R[3]*R[7]-R[4]*R[6]) - 1.0) > 1e-12) return "frame not a rotation";
		if (kind == 3) {
			if (!r.Get (h, 4)) return "short mean orbit";
			Dm = h[0];
			if (Dm > 32) return "mean orbit degree above 32";
			if (!r.Get (Cm, 8*6*(Dm+1)) || !Finite (Cm, 6*(Dm+1))) return "bad mean orbit";
		}
		for (int i = 0; i < ns; i++) {
			Sig &g = s[i];
			if (!r.Get (h, 4)) return "short signal";
			g.real = h[0]; g.Dp = h[1]; g.M = h[2]; g.Mh = h[3];
			if (g.real > 1 || g.Dp > 31 || g.M > 8 || g.Mh > 8) return "signal degrees out of range";
			int nc = (g.Dp + 1)*(g.real ? 1 : 2);
			if (!r.Get (g.c, 8*nc) || !Finite (g.c, nc)) return "bad polynomial";
			for (int grp = 0; grp < 2; grp++) {
				std::vector<Line> &lines = grp ? g.hi : g.lo;
				int M = grp ? g.Mh : g.M;
				bool wide = grp || M + 1 > 8;
				uint32_t K;
				if (!r.Get (&K, 4) || K > 100000) return "bad line count";
				lines.assign (K, Line ());
				for (Line &q : lines) if (!r.Get (&q.nu, 8)) return "short lines";
				if (grp) for (Line &q : lines) if (!r.Get (&q.beta, 8)) return "short lines";
				for (Line &q : lines) {
					unsigned pr, dbl;
					if (wide) { uint16_t f2[2]; if (!r.Get (f2, 4)) return "short lines"; pr = f2[0]; dbl = f2[1]; }
					else { uint8_t f2[2]; if (!r.Get (f2, 2)) return "short lines"; pr = f2[0]; dbl = f2[1]; }
					if ((pr >> (M + 1)) || (dbl & ~pr)) return "bad line flags";
					for (int m = 0; m <= M; m++) {
						if (!(pr & (1u << m))) continue;
						if (dbl & (1u << m)) { if (!r.Get (q.a + 2*m, 16)) return "short amplitudes"; }
						else { float x[2]; if (!r.Get (x, 8)) return "short amplitudes"; q.a[2*m] = x[0]; q.a[2*m+1] = x[1]; }
					}
					if (!std::isfinite (q.nu) || !std::isfinite (q.beta) || !Finite (q.a, 18)) return "bad line values";
				}
			}
		}
		return nullptr;
	}

	// value and d/dt of one signal: P, D = Legendre values and d/dt at tau = t/T0
	static void Eval (const Sig &g, double t, const double *P, const double *D, double *v)
	{
		double xr = 0, xi = 0, yr = 0, yi = 0;
		if (g.real) for (int d = 0; d <= g.Dp; d++) { xr += g.c[d]*P[d]; yr += g.c[d]*D[d]; }
		else for (int d = 0; d <= g.Dp; d++) { xr += g.c[2*d]*P[d]; xi += g.c[2*d+1]*P[d]; yr += g.c[2*d]*D[d]; yi += g.c[2*d+1]*D[d]; }
		for (int grp = 0; grp < 2; grp++) {
			int M = grp ? g.Mh : g.M;
			for (const Line &q : grp ? g.hi : g.lo) {
				double th = q.nu*t, w = q.nu, ar = 0, ai = 0, br = 0, bi = 0;
				th -= TWOPI*std::nearbyint (th*INV2PI);
				if (grp) { th += q.beta*P[1]*P[1]; w += 2*q.beta*P[1]*D[1]; }
				double sn = std::sin (th), cs = std::cos (th);
				for (int m = 0; m <= M; m++) { ar += q.a[2*m]*P[m]; ai += q.a[2*m+1]*P[m]; br += q.a[2*m]*D[m]; bi += q.a[2*m+1]*D[m]; }
				double re = ar*cs - ai*sn, im = ar*sn + ai*cs;
				xr += re; xi += im;
				yr += br*cs - bi*sn - w*im; yi += br*sn + bi*cs + w*re;
			}
		}
		v[0] = xr; v[1] = xi; v[2] = yr; v[3] = yi;
	}

	// mean Kepler orbit of an mrect series: elements (a, lambda, k, h, q, p) as Legendre series in tau
	void MeanPos (double t, double *X) const
	{
		double P[33], tau = t/T0, E[6];
		P[0] = 1; P[1] = tau;
		for (int m = 1; m < Dm; m++) P[m+1] = ((2*m+1)*tau*P[m] - m*P[m-1])/(m+1);
		for (int j = 0; j < 6; j++) { E[j] = 0; for (int m = 0; m <= Dm; m++) E[j] += Cm[j*(Dm+1) + m]*P[m]; }
		Kepler (E[0], E[1], E[2], E[3], E[4], E[5], mu, X);
	}

	static void Kepler (double a, double lam, double k, double h, double q, double p, double mu, double *X)
	{
		lam -= TWOPI*std::nearbyint (lam*INV2PI);
		double F = lam;
		for (int it = 0; it < 30; it++) {
			double cF = std::cos (F), sF = std::sin (F), g = F - k*sF + h*cF - lam, dF = g/(1 - k*cF - h*sF);
			F -= dF;
			if (std::fabs (dF) < 1e-15) break;
		}
		double cF = std::cos (F), sF = std::sin (F), b = std::sqrt (1 - h*h - k*k), be = 1/(1 + b);
		double x = a*((1 - h*h*be)*cF + h*k*be*sF - k), y = a*((1 - k*k*be)*sF + h*k*be*cF - h);
		double n = std::sqrt (mu/(a*a*a)), r = a*(1 - k*cF - h*sF);
		double xd = a*a*n/r*(h*k*be*cF - (1 - h*h*be)*sF), yd = a*a*n/r*((1 - k*k*be)*cF - h*k*be*sF);
		double c = std::sqrt (1 - p*p - q*q);
		double fx[3] = {1 - 2*p*p, 2*p*q, -2*p*c}, gy[3] = {2*p*q, 1 - 2*q*q, 2*q*c};
		for (int i = 0; i < 3; i++) { X[i] = x*fx[i] + y*gy[i]; X[3+i] = xd*fx[i] + yd*gy[i]; }
	}
};
