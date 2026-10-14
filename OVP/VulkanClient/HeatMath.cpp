// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: re-entry hull heating maths (design-reentry-heat 2, 3)

#include "HeatMath.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace HeatMath {

// Sutton and Graves, NASA TR R-376 (1971): k for air, CO2 and H2-He mixtures
double SuttonGravesK (double R)
{
	if (R < 230.0) return 1.9027e-4;
	if (R > 1000.0) return 0.6556e-4;
	return 1.7415e-4;
}

double SuttonGraves (double rho, double Rn, double V, double k)
{
	if (!(rho > 0.0) || !(V > 0.0) || !(Rn > 0.0)) return 0.0;
	double q = k * std::sqrt(rho / Rn) * V * V * V;
	return std::min(q, 0.5 * rho * V * V * V); // free-molecular limit, full accommodation
}

double HotWallFactor (double T, double cp, double h0)
{
	if (!(h0 > 0.0)) return 0.0;
	return std::min(1.0, 1.0 - cp * T / h0);
}

// Tauber, Menees and Adelman (1987): laminar flat plate 2.53e-5 rho^0.5 V^3.2 x^-0.5 over Sutton-Graves
double KFlatPlate (double V)
{
	return (2.53e-5 / 1.7415e-4) * std::pow(std::max(V, 0.0), 0.2);
}

// Lees (1956) hemisphere distribution as 0.55 + 0.45 cos 2theta; continued past 90 deg to 0 at c = -0.25
double AngleFactor (double c)
{
	if (c >= 0.0) return 0.1 + 0.9 * c * c;
	double u = std::max(0.0, 1.0 + 4.0 * c);
	return 0.1 * u * u;
}

static double Smooth (double e0, double e1, double x)
{
	double u = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return u * u * (3.0 - 2.0 * u);
}

// stagnation region, swept attachment lines (Tauber, Menees and Adelman 1987: sqrt(q_cyl^2 cos^2 L + q_fp^2 sin^2 L)), laminar flat plate, lee floor
double LocalRatio (double c, double x, double k1, double k2, double fd2, double Rn, double kfp)
{
	c = std::clamp(c, -1.0, 1.0);
	double r = x / (1.5 * Rn);
	double Gs = AngleFactor(c) * std::exp(-r * r);
	double Gf = 0.0;
	if (c > 0.0) Gf = kfp * std::sqrt(std::sqrt(std::max(0.0, 1.0 - c * c))) * c * std::sqrt(Rn / std::max(x, Rn));
	double an = (k1 + k2) > 0.0 ? (k1 - k2) / (k1 + k2) : 0.0;
	double sL = std::clamp(an * fd2, 0.0, 1.0), cL = std::sqrt(1.0 - sL * sL);
	double cn = std::clamp(c / std::max(cL, 1e-3), -1.0, 1.0);
	double Ge = Tune::edge * AngleFactor(cn) * cL;
	double Gc = Smooth(Tune::gate0, Tune::gate1, Rn * k1) * Smooth(Tune::attach0, Tune::attach1, cn) * std::sqrt(Ge * Ge + Gf * Gf * sL * sL);
	return std::clamp(std::max(Gs, std::max(Gf, Gc)) + Tune::lee, 0.0, 2.0);
}

static double Dot (const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
static double Len (const double a[3]) { return std::sqrt(Dot(a, a)); }

// shape operator in an orthonormal tangent basis from the screen derivatives: W = J_N J_P^-1, symmetrised
bool Weingarten (const double N[3], const double dNx[3], const double dNy[3], const double dPx[3], const double dPy[3],
	double &k1, double &k2, double d2[3])
{
	k1 = k2 = 0.0;
	d2[0] = d2[1] = d2[2] = 0.0;
	double lx = Len(dPx), ly = Len(dPy);
	if (!(lx > 0.0) || !(ly > 0.0)) return false;
	double e1[3], np = Dot(N, dPx);
	for (int i = 0; i < 3; i++) e1[i] = dPx[i] - N[i] * np;
	if (Len(e1) < 0.1 * lx) { np = Dot(N, dPy); for (int i = 0; i < 3; i++) e1[i] = dPy[i] - N[i] * np; } // dP/dx along the normal: edge-on
	double l1 = Len(e1);
	if (!(l1 > 0.0)) return false;
	for (double &v : e1) v /= l1;
	double e2[3] = { N[1] * e1[2] - N[2] * e1[1], N[2] * e1[0] - N[0] * e1[2], N[0] * e1[1] - N[1] * e1[0] };
	double p11 = Dot(e1, dPx), p21 = Dot(e2, dPx), p12 = Dot(e1, dPy), p22 = Dot(e2, dPy);
	double n11 = Dot(e1, dNx), n21 = Dot(e2, dNx), n12 = Dot(e1, dNy), n22 = Dot(e2, dNy);
	double det = p11 * p22 - p12 * p21;
	if (!(std::fabs(det) > 1e-4 * lx * ly)) return false;
	double i11 = p22 / det, i12 = -p12 / det, i21 = -p21 / det, i22 = p11 / det;
	double w11 = n11 * i11 + n12 * i21, w12 = n11 * i12 + n12 * i22, w21 = n21 * i11 + n22 * i21, w22 = n21 * i12 + n22 * i22;
	double a = w11, d = w22, b = 0.5 * (w12 + w21);
	double m = 0.5 * (a + d), rr = std::sqrt(0.25 * (a - d) * (a - d) + b * b);
	k1 = std::max(0.0, m + rr);
	k2 = std::max(0.0, m - rr);
	double v0, v1, lam = m - rr;
	if (std::fabs(b) > 1e-12 * (std::fabs(a) + std::fabs(d) + 1e-30)) {
		double ax = lam - d, ay = lam - a;
		if (std::fabs(ax) > std::fabs(ay)) { v0 = ax; v1 = b; } else { v0 = b; v1 = ay; }
	}
	else if (a <= d) { v0 = 1.0; v1 = 0.0; }
	else { v0 = 0.0; v1 = 1.0; }
	double lv = std::sqrt(v0 * v0 + v1 * v1);
	if (lv > 0.0) for (int i = 0; i < 3; i++) d2[i] = (v0 * e1[i] + v1 * e2[i]) / lv;
	return true;
}

double HeatCapacity (double lag, double eps)
{
	return lag * 4.0 * eps * SIGMA * Tune::Tref * Tune::Tref * Tune::Tref;
}

// C (T' - T)/dt = G q hw(T') - eps sigma (T'^4 - Ts^4): increasing in T', one root, Newton with a bracket
static double Solve (double T, double rate, double qG, double cp, double h0, const Wall &w)
{
	double es = w.eps * SIGMA, Ts4 = w.Tsink * w.Tsink * w.Tsink * w.Tsink;
	auto F = [&](double x) { return rate * (x - T) - qG * HotWallFactor(x, cp, h0) + es * (x * x * x * x - Ts4); };
	auto dF = [&](double x) {
		double d = rate + 4.0 * es * x * x * x;
		if (h0 > 0.0) d += qG * cp / h0; // the hot-wall factor is linear in T, also past the recovery temperature (review NIT 1)
		return d;
	};
	double lo = 1.0, hi = std::max(T, std::pow((qG + es * Ts4) / es, 0.25)) + 1.0;
	double x = std::clamp(T, lo, hi);
	for (int i = 0; i < 60; i++) {
		double fx = F(x);
		if (fx > 0.0) hi = x; else lo = x;
		double d = dF(x);
		double nx = d > 0.0 ? x - fx / d : 0.5 * (lo + hi);
		if (!(nx > lo && nx < hi)) nx = 0.5 * (lo + hi);
		if (std::fabs(nx - x) < 1e-5) return nx;
		x = nx;
	}
	return x;
}

double EquilibriumWall (double qcw, double G, double cp, double h0, const Wall &w)
{
	return Solve(w.Tsink, 0.0, std::max(0.0, qcw * G), cp, h0, w);
}

double StepWall (double T, double dt, double qcw, double G, double cp, double h0, const Wall &w)
{
	if (!(dt > 0.0)) return T;
	double rate = w.C / dt;
	if (!(rate > 1e-9)) return EquilibriumWall(qcw, G, cp, h0, w);
	return Solve(T, rate, std::max(0.0, qcw * G), cp, h0, w);
}

double FlowTau (double lag, double T0)
{
	double r = Tune::Tref / std::max(T0, 300.0);
	return std::clamp(lag * r * r * r / 3.0, 0.2, lag);
}

double LowPass (double dt, double tau)
{
	if (!(dt > 0.0)) return 0.0;
	if (!(tau > 0.0)) return 1.0;
	return 1.0 - std::exp(-dt / tau);
}

double BinRatio (int k)
{
	return Tune::G0 * std::pow(Tune::Gr, double(k));
}

double BinCoord (double G)
{
	if (!(G > 0.0)) return double(NBIN + 1);
	return std::clamp(std::log(G / Tune::G0) / std::log(Tune::Gr), 0.0, double(NBIN + 1));
}

// Wyman, Sloan and Shirley, JCGT 2(2) (2013): multi-lobe Gaussian fit of the CIE 1931 2 deg functions
static double Lobe (double x, double m, double s1, double s2)
{
	double t = (x - m) / (x < m ? s1 : s2);
	return std::exp(-0.5 * t * t);
}

void BlackbodyXYZ (double T, double xyz[3])
{
	const double h = 6.62607015e-34, c = 2.99792458e8, k = 1.380649e-23;
	xyz[0] = xyz[1] = xyz[2] = 0.0;
	if (!(T > 0.0)) return;
	for (int l = 360; l <= 830; l++) {
		double lm = l * 1e-9;
		double e = h * c / (lm * k * T);
		if (e > 700.0) continue;
		double B = 2.0 * h * c * c / (lm * lm * lm * lm * lm * std::expm1(e)) * 1e-9; // W/m2/sr per nm (Planck)
		double xb = 1.056 * Lobe(l, 599.8, 37.9, 31.0) + 0.362 * Lobe(l, 442.0, 16.0, 26.7) - 0.065 * Lobe(l, 501.1, 20.4, 26.2);
		double yb = 0.821 * Lobe(l, 568.8, 46.9, 40.5) + 0.286 * Lobe(l, 530.9, 16.3, 31.1);
		double zb = 1.217 * Lobe(l, 437.0, 11.8, 36.0) + 0.681 * Lobe(l, 459.0, 26.0, 13.8);
		xyz[0] += B * xb; xyz[1] += B * yb; xyz[2] += B * zb;
	}
	for (int i = 0; i < 3; i++) xyz[i] *= 683.0; // lm/W
}

// IEC 61966-2-1: XYZ to linear sRGB (D65)
void XYZToLinearSrgb (const double xyz[3], double rgb[3])
{
	rgb[0] = 3.2406 * xyz[0] - 1.5372 * xyz[1] - 0.4986 * xyz[2];
	rgb[1] = -0.9689 * xyz[0] + 1.8758 * xyz[1] + 0.0415 * xyz[2];
	rgb[2] = 0.0557 * xyz[0] - 0.2040 * xyz[1] + 1.0570 * xyz[2];
}

void Blackbody (double T, double rgb[3])
{
	double xyz[3];
	BlackbodyXYZ(T, xyz);
	XYZToLinearSrgb(xyz, rgb);
	for (int i = 0; i < 3; i++) rgb[i] = std::max(0.0, rgb[i]);
}

void ColourTable::Build (int n, double t0, double t1)
{
	T0 = t0; T1 = t1;
	n = std::max(n, 2);
	ln.assign(size_t(n) * 3, -60.0f);
	for (int i = 0; i < n; i++) {
		double u = 1.0 / T0 + (1.0 / T1 - 1.0 / T0) * double(i) / double(n - 1);
		double rgb[3];
		Blackbody(1.0 / u, rgb);
		for (int j = 0; j < 3; j++) ln[size_t(i) * 3 + j] = rgb[j] > 0.0 ? float(std::max(-60.0, std::log(rgb[j]))) : -60.0f;
	}
}

void ColourTable::Lookup (double T, double rgb[3]) const
{
	rgb[0] = rgb[1] = rgb[2] = 0.0;
	int n = int(ln.size() / 3);
	if (n < 2 || !(T >= T0)) return;
	double s = (1.0 / std::min(T, T1) - 1.0 / T0) / (1.0 / T1 - 1.0 / T0) * double(n - 1);
	int i = std::clamp(int(s), 0, n - 2);
	double fr = std::clamp(s - double(i), 0.0, 1.0);
	for (int j = 0; j < 3; j++) {
		double a = ln[size_t(i) * 3 + j], b = ln[size_t(i + 1) * 3 + j];
		double v = a + (b - a) * fr;
		rgb[j] = v > -59.0 ? std::exp(v) : 0.0;
	}
}

void SupportSet (const std::vector<std::array<double, 3>> &pts, std::vector<std::array<double, 3>> &out, int dirs)
{
	out.clear();
	if (pts.empty()) return;
	std::vector<size_t> idx;
	const double ga = HPI * (3.0 - std::sqrt(5.0)); // golden angle
	for (int i = 0; i < dirs; i++) {
		double z = 1.0 - (2.0 * i + 1.0) / dirs, r = std::sqrt(std::max(0.0, 1.0 - z * z)), p = ga * i;
		double d[3] = { r * std::cos(p), z, r * std::sin(p) };
		size_t best = 0;
		double bv = -1e300;
		for (size_t k = 0; k < pts.size(); k++) {
			double v = pts[k][0] * d[0] + pts[k][1] * d[1] + pts[k][2] * d[2];
			if (v > bv) { bv = v; best = k; }
		}
		idx.push_back(best);
	}
	std::sort(idx.begin(), idx.end());
	idx.erase(std::unique(idx.begin(), idx.end()), idx.end());
	for (size_t k : idx) out.push_back(pts[k]);
}

std::array<double, 3> SupportQuery (const std::vector<std::array<double, 3>> &set, const double d[3])
{
	std::array<double, 3> best = { 0.0, 0.0, 0.0 };
	double bv = -1e300;
	for (const auto &p : set) {
		double v = p[0] * d[0] + p[1] * d[1] + p[2] * d[2];
		if (v > bv) { bv = v; best = p; }
	}
	return best;
}

void ShaderBins (const double lnB[NBIN][3], double G, double out[3])
{
	double t = std::clamp(std::log(std::max(G, 1e-30) * 0.5) * (1.0 / std::log(0.4)), 0.0, 9.0);
	int k = std::min(int(t), NBIN - 2);
	double f = t - double(k);
	for (int j = 0; j < 3; j++) out[j] = std::exp(std::max(lnB[k][j] + f * (lnB[k + 1][j] - lnB[k][j]), -60.0));
}

bool ParseHeatLine (const std::string &line0, HeatCfg &c)
{
	std::string line = line0.substr(0, line0.find(';'));
	std::istringstream is(line);
	std::string key;
	if (!(is >> key)) return false;
	for (char &ch : key) ch = char(std::toupper((unsigned char)ch));
	if (key != "HEAT") return false;
	std::string tok;
	while (is >> tok) {
		std::string k = tok, v;
		size_t e = tok.find('=');
		if (e != std::string::npos) { k = tok.substr(0, e); v = tok.substr(e + 1); }
		for (char &ch : k) ch = char(std::tolower((unsigned char)ch));
		char *end = nullptr;
		double x = v.empty() ? 0.0 : std::strtod(v.c_str(), &end);
		bool num = !v.empty() && end && *end == 0 && std::isfinite(x);
		if (k == "off" && v.empty()) c.off = true;
		else if (k == "on" && v.empty()) c.off = false;
		else if (k == "nose" && num) c.nose = std::clamp(x, 0.05, 20.0);
		else if (k == "emis" && num) c.emis = std::clamp(x, 0.05, 1.0);
		else if (k == "lag" && num) c.lag = std::clamp(x, 1.0, 600.0);
		else if (k == "glow" && num) c.glow = std::clamp(x, 0.0, 10.0);
		else c.bad++;
	}
	return true;
}

}
