// not upstream: a celestial body from PSR2 series parts (psrseries.h): JPL's accuracy inside their span, two-body motion outside it
#pragma once
#include "celbody.h"
#include "psrseries.h"

// a series file relative to the Orbiter root, and the GM [km^3/s^2] that moves it outside the span
struct PsrPart { const char *file; double mu; };

class PsrModule : public CELBODY {
	const PsrPart *parts;      // one part, or two summed (the first is the barycentre)
	int np;
	const PsrPart *ref;        // outside the span only: the parent's offset from the system barycentre, or null
	double t0, t1, h;          // span and cache step [days from J2000]
	PsrSeries ser[3];          // the parts, then ref
	bool valid = false, refok = false;
	double end[2][3][6];       // states at t0 and t1 of the parts and ref [km, km/s]
	bool cached = false;
	long kc = 0;
	double ca[12], cb[12];     // cache: exact states at kc*h and (kc+1)*h

	int Flags () const { return np == 1 ? EPHEM_TRUEPOS|EPHEM_TRUEVEL|EPHEM_BARYPOS|EPHEM_BARYVEL|EPHEM_BARYISTRUE : EPHEM_TRUEPOS|EPHEM_TRUEVEL|EPHEM_BARYPOS|EPHEM_BARYVEL; }

	// ecliptic km, km/s -> Orbiter's x, z, y in m, m/s
	static void ToOrbiter (const double *s, double *ret)
	{
		ret[0] = s[0]*1e3; ret[1] = s[2]*1e3; ret[2] = s[1]*1e3;
		ret[3] = s[3]*1e3; ret[4] = s[5]*1e3; ret[5] = s[4]*1e3;
	}

	// two-body motion from s0 over dt [s] (Lagrange f and g with the eccentric anomaly); not elliptic: a straight line
	static void TwoBody (const double *s0, double dt, double mu, double *s)
	{
		const double *r0 = s0, *v0 = s0 + 3;
		double R0 = std::sqrt (r0[0]*r0[0] + r0[1]*r0[1] + r0[2]*r0[2]), V2 = v0[0]*v0[0] + v0[1]*v0[1] + v0[2]*v0[2];
		double a = 1.0/(2.0/R0 - V2/mu);
		if (!(mu > 0) || !(R0 > 0) || !(a > 0) || !std::isfinite (a)) {
			for (int i = 0; i < 3; i++) { s[i] = r0[i] + v0[i]*dt; s[i+3] = v0[i]; }
			return;
		}
		double n = std::sqrt (mu/(a*a*a)), P = PsrSeries::TWOPI/n;
		dt -= P*std::nearbyint (dt/P);
		double sig = (r0[0]*v0[0] + r0[1]*v0[1] + r0[2]*v0[2])/std::sqrt (mu), sa = std::sqrt (a), x = n*dt;
		for (int it = 0; it < 50; it++) {
			double f = x - (1 - R0/a)*std::sin (x) + sig/sa*(1 - std::cos (x)) - n*dt;
			double dx = f/(1 - (1 - R0/a)*std::cos (x) + sig/sa*std::sin (x));
			x -= dx;
			if (std::fabs (dx) < 1e-14) break;
		}
		double cx = std::cos (x), sx = std::sin (x), r = a + (R0 - a)*cx + sig*sa*sx;
		double F = 1 - a/R0*(1 - cx), G = dt + (sx - x)/n, Fd = -std::sqrt (mu*a)*sx/(r*R0), Gd = 1 - a/r*(1 - cx);
		for (int i = 0; i < 3; i++) { s[i] = F*r0[i] + G*v0[i]; s[i+3] = Fd*r0[i] + Gd*v0[i]; }
	}

	// true state in ret[0..5], barycentre in ret[6..11], Orbiter units
	void Exact (double t, double *ret) const
	{
		double s[6], sum[6] = {0, 0, 0, 0, 0, 0};
		for (int i = 0; i < np; i++) {
			ser[i].State (t, s);
			for (int k = 0; k < 6; k++) sum[k] += s[k];
			if (i == 0) ToOrbiter (s, ret + 6);
		}
		ToOrbiter (sum, ret);
	}

	void Outside (double t, double *ret) const
	{
		int e = t < t0 ? 0 : 1;
		double dt = (t - (e ? t1 : t0))*86400.0, s[6], b[6], m[6], sum[6] = {0, 0, 0, 0, 0, 0};
		for (int i = 0; i < np; i++) {
			if (ref && refok) {
				for (int k = 0; k < 6; k++) m[k] = end[e][i][k] + end[e][2][k];
				TwoBody (m, dt, parts[i].mu, s);
				TwoBody (end[e][2], dt, ref->mu, b);
				for (int k = 0; k < 6; k++) s[k] -= b[k];
			} else TwoBody (end[e][i], dt, parts[i].mu, s);
			for (int k = 0; k < 6; k++) sum[k] += s[k];
			if (i == 0) ToOrbiter (s, ret + 6);
		}
		ToOrbiter (sum, ret);
	}

	bool LoadPart (int k, const char *file)
	{
		const char *err = nullptr;
		if (ser[k].Load (oapiResolvePath (file).c_str (), &err)) return true;
		oapiWriteLogError ("%s: %s", file, err);
		return false;
	}

public:
	PsrModule (const PsrPart *p, int n, const PsrPart *r, double mjd0, double mjd1, double step)
		: parts (p), np (n), ref (r), t0 (mjd0 - 51544.5), t1 (mjd1 - 51544.5), h (step) {}

	void clbkInit (FILEHANDLE cfg) override
	{
		CELBODY::clbkInit (cfg);
		valid = true;
		for (int i = 0; i < np; i++) if (!LoadPart (i, parts[i].file)) valid = false;
		refok = ref && LoadPart (2, ref->file);
		for (int k = 0; k < 3; k++) {
			if (k < np ? !valid : (k != 2 || !refok)) continue;
			ser[k].State (t0, end[0][k]);
			ser[k].State (t1, end[1][k]);
		}
	}

	bool bEphemeris () const override { return true; }

	int clbkEphemeris (double mjd, int, double *ret) override
	{
		if (!std::isfinite (mjd)) return 0;
		if (!valid) { for (int i = 0; i < 12; i++) ret[i] = 0.0; return Flags (); }
		double t = mjd - 51544.5;
		if (t >= t0 && t <= t1) Exact (t, ret);
		else Outside (t, ret);
		return Flags ();
	}

	// cubic Hermite curve between exact states on a grid of h; a cell past either end of the span takes the exact path
	int clbkFastEphemeris (double simt, int req, double *ret) override
	{
		double mjd = oapiTime2MJD (simt), t = mjd - 51544.5;
		if (!valid || !std::isfinite (t)) return clbkEphemeris (mjd, req, ret);
		double k = std::floor (t/h), ta = k*h, tb = ta + h;
		if (ta < t0 || tb > t1) { cached = false; return clbkEphemeris (mjd, req, ret); }
		long kk = (long)k;
		if (!cached || kk != kc) {
			if (cached && kk == kc + 1) std::memcpy (ca, cb, sizeof (ca));
			else Exact (ta, ca);
			Exact (tb, cb);
			kc = kk; cached = true;
		}
		double u = (t - ta)/h, H = h*86400.0;
		double h00 = (1 + 2*u)*(1 - u)*(1 - u), h10 = u*(1 - u)*(1 - u), h01 = u*u*(3 - 2*u), h11 = u*u*(u - 1);
		double d00 = 6*u*(u - 1)/H, d10 = (1 - u)*(1 - 3*u), d11 = u*(3*u - 2);
		for (int b = 0; b < 12; b += 6)
			for (int i = 0; i < 3; i++) {
				ret[b+i] = h00*ca[b+i] + h10*H*ca[b+3+i] + h01*cb[b+i] + h11*H*cb[b+3+i];
				ret[b+3+i] = d00*(ca[b+i] - cb[b+i]) + d10*ca[b+3+i] + d11*cb[b+3+i];
			}
		return Flags ();
	}
};
