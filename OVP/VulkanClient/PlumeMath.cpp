// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: plume, smoke and plasma maths (design-volumetric-plumes 17)

#include "PlumeMath.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <strings.h>

namespace PlumeMath {

static const double PI = 3.14159265358979323846;

double Smoothstep (double e0, double e1, double x)
{
	double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
	return t * t * (3.0 - 2.0 * t);
}

double Condensate (double pa, double pe)
{
	return Smoothstep(-4.0, -5.0, std::log10(std::max(pa / std::max(pe, 1.0), 1e-12))); // [V] T falls as (pa/pe)^((g-1)/g): ~1100 K to ~200 K at 1e-4 (g 1.2)
}

double PressureRatio (double M, double g)
{
	return std::pow(1.0 + 0.5 * (g - 1.0) * M * M, g / (g - 1.0));
}

double AreaRatio (double M, double g)
{
	double k = (g + 1.0) / (2.0 * (g - 1.0));
	return std::pow((2.0 / (g + 1.0)) * (1.0 + 0.5 * (g - 1.0) * M * M), k) / M;
}

double MachFromPressureRatio (double r, double g)
{
	if (r <= 1.0) return 0.0;
	return std::sqrt(2.0 / (g - 1.0) * (std::pow(r, (g - 1.0) / g) - 1.0));
}

double PrandtlMeyer (double M, double g)
{
	if (M <= 1.0) return 0.0;
	double k = std::sqrt((g + 1.0) / (g - 1.0)), m = std::sqrt(M * M - 1.0);
	return k * std::atan(m / k) - std::atan(m);
}

double PrandtlMeyerMax (double g)
{
	return 0.5 * PI * (std::sqrt((g + 1.0) / (g - 1.0)) - 1.0);
}

double PressureFraction (double isp0, double ispRef)
{
	if (isp0 <= 0.0 || ispRef <= 0.0) return 0.0;
	return std::max(0.0, 1.0 - ispRef / isp0);
}

// Orbiter: F(p) = F0 (1 - pfac p), pfac = (Isp0 - Isp_ref)/(p_ref Isp0); the loss is A_e p
double ExitArea (double F0, double isp0, double ispRef, double pRef)
{
	if (pRef <= 0.0) return 0.0;
	return F0 * PressureFraction(isp0, ispRef) / pRef;
}

// [D] ideal nozzles put 3-7 % of the vacuum thrust in the pressure term pe Ae
double ExitPressure (double phi, double F0)
{
	if (phi > 0.02) return std::clamp(0.05 * PSL / phi, 3e3, 120e3);
	return F0 > 300e3 ? 40e3 : 8e3; // [V]
}

Jet JetState (double pe, double pa, double pc, double g)
{
	Jet j;
	pe = std::max(pe, 1.0);
	j.pe = pe;
	j.pa = std::max(pa, 0.0);
	j.n = pe / std::max(j.pa, 1e-9);
	j.watm = Smoothstep(0.03, 0.3, j.pa / pe);
	j.Me = MachFromPressureRatio(std::max(pc / pe, 1.0001), g);
	double pac = std::max(j.pa, 0.01 * pe); // finite in vacuum (design review S3)
	j.Mj = MachFromPressureRatio(std::max(pc / pac, 1.0001), g);
	j.thInf = std::max(0.05, PrandtlMeyerMax(g) - PrandtlMeyer(j.Me, g));
	j.jet = j.watm >= 1e-3 && j.Mj > 1.0 && j.Me > 1.0;
	if (j.jet) {
		j.rj = std::sqrt(AreaRatio(j.Mj, g) / AreaRatio(j.Me, g));
		j.Ls = 1.306 * std::sqrt(std::max(j.Mj * j.Mj - 1.0, 0.01)) * 2.0 * j.rj; // [P] Pack 1950
		j.xmd = (j.n < 0.4 || j.n > 4.0) ? 0.55 * j.Ls : -1.0;
	}
	else { j.rj = 1.0; j.Ls = 0.0; j.xmd = -1.0; }
	return j;
}

double PlumeRadius (const Jet &j, double x)
{
	x = std::max(x, 0.0);
	if (!j.jet || j.Ls <= 0.0) return 1.0 + 0.094 * x;
	double Lc = 4.0 * j.Ls; // [V] visible cells
	return j.rj + (1.0 - j.rj) * std::exp(-x / (0.5 * j.Ls)) + 0.06 * j.rj * std::sin(2.0 * PI * x / j.Ls) * std::exp(-x / (4.0 * j.Ls)) + 0.094 * std::max(0.0, x - Lc); // [P] Pope 2000 spreading
}

double Simons (double d, double theta, double thInf, double g)
{
	double a = std::min(PI * std::fabs(theta) / (2.0 * thInf), 0.5 * PI);
	double c = std::max(0.0, std::cos(a));
	double r = 1.0 / (1.0 + std::max(d, 0.0));
	return r * r * std::pow(c, 2.0 / (g - 1.0));
}

Class Classify (bool hasThruster, double F0, double isp0)
{
	if (!hasThruster) return TEXTURE;
	if (isp0 > 5000.0 || isp0 < 2300.0) return TEXTURE;
	if (F0 < 50e3 && isp0 < 3500.0) return HYPERGOLIC;
	if (isp0 >= 4000.0) return HYDROLOX;
	if (isp0 >= 3450.0) return METHALOX;
	if (isp0 >= 2900.0) return KEROLOX;
	return SOLID;
}

static const char *names[NCLASS] = { "hydrolox", "methalox", "kerolox", "hypergolic", "solid", "texture" };

const char *ClassName (int c) { return (c >= 0 && c < NCLASS) ? names[c] : "?"; }

int ClassFromName (const char *s)
{
	for (int i = 0; i < NCLASS; i++) if (s && std::strcmp(s, names[i]) == 0) return i;
	return -1;
}

int StyleFromName (const char *s)
{
	if (!s) return STYLE_NONE;
	while (*s == ' ' || *s == '\t') s++;
	size_t n = std::strcspn(s, " \t;\r\n");
	if (n == 6 && strncasecmp(s, "legacy", 6) == 0) return STYLE_LEGACY;
	if (n == 10 && strncasecmp(s, "volumetric", 10) == 0) return STYLE_VOLUMETRIC;
	return STYLE_NONE;
}

bool Volumetric (int style, int cls)
{
	if (style == STYLE_LEGACY) return false;
	if (style == STYLE_VOLUMETRIC) return true;
	return cls != TEXTURE;
}

// Wyman, Sloan and Shirley 2013, piecewise Gaussian fit of the CIE 1931 2-degree observer
static double G (double x, double mu, double s1, double s2)
{
	double t = (x - mu) / (x < mu ? s1 : s2);
	return std::exp(-0.5 * t * t);
}

static void PlanckXYZ (double T, double &X, double &Y, double &Z)
{
	X = Y = Z = 0.0;
	const double c2 = 1.4387769e-2; // m K
	for (double l = 380.0; l <= 780.0; l += 1.0) {
		double lm = l * 1e-9;
		double B = 1.0 / (std::pow(lm, 5.0) * (std::exp(c2 / (lm * T)) - 1.0));
		double x = 1.056 * G(l, 599.8, 37.9, 31.0) + 0.362 * G(l, 442.0, 16.0, 26.7) - 0.065 * G(l, 501.1, 20.4, 26.2);
		double y = 0.821 * G(l, 568.8, 46.9, 40.5) + 0.286 * G(l, 530.9, 16.3, 31.1);
		double z = 1.217 * G(l, 437.0, 11.8, 36.0) + 0.681 * G(l, 459.0, 26.0, 13.8);
		X += B * x; Y += B * y; Z += B * z;
	}
}

void BlackbodyXY (double T, double &x, double &y)
{
	double X, Y, Z;
	PlanckXYZ(T, X, Y, Z);
	double s = X + Y + Z;
	x = X / s; y = Y / s;
}

void Blackbody (double T, double rgb[3])
{
	double X, Y, Z;
	PlanckXYZ(T, X, Y, Z);
	double r = 3.2406 * X - 1.5372 * Y - 0.4986 * Z;
	double g = -0.9689 * X + 1.8758 * Y + 0.0415 * Z;
	double b = 0.0557 * X - 0.2040 * Y + 1.0570 * Z;
	r = std::max(r, 0.0); g = std::max(g, 0.0); b = std::max(b, 0.0);
	double m = std::max(r, std::max(g, b));
	if (m <= 0.0) m = 1.0;
	rgb[0] = r / m; rgb[1] = g / m; rgb[2] = b / m;
}

void PlasmaColour (double V, double rgb[3])
{
	static const double tab[7][4] = { // [U] km/s, linear RGB
		{ 5.00, 1.00, 0.19, 0.00 }, { 7.00, 1.00, 0.26, 0.00 }, { 7.62, 1.00, 0.28, 0.00 }, { 9.00, 1.00, 0.36, 0.06 },
		{ 10.0, 1.00, 0.47, 0.55 }, { 11.0, 0.75, 0.39, 1.00 }, { 12.0, 0.44, 0.25, 1.00 } };
	if (V <= tab[0][0]) { for (int c = 0; c < 3; c++) rgb[c] = tab[0][c + 1]; return; }
	if (V >= tab[6][0]) { for (int c = 0; c < 3; c++) rgb[c] = tab[6][c + 1]; return; }
	int i = 0;
	while (i < 5 && V > tab[i + 1][0]) i++;
	double t = (V - tab[i][0]) / (tab[i + 1][0] - tab[i][0]);
	for (int c = 0; c < 3; c++) rgb[c] = tab[i][c + 1] + (tab[i + 1][c + 1] - tab[i][c + 1]) * t;
}

double PlasmaIntensity (double q, double V)
{
	double phi = q * V; // free-stream kinetic energy flux, W/m^2
	if (phi <= 1.0 || V <= 0.0) return 0.0;
	return Smoothstep(6.0, 7.0, std::log10(phi)) * Smoothstep(3000.0, 6000.0, V); // [V], onset near 85 km at 7.6 km/s
}

double ChordIntegral (double R, double d, double u0, double u1)
{
	if (R <= 0.0 || d >= R) return 0.0;
	double h = std::sqrt(R * R - d * d);
	u0 = std::clamp(u0, -h, h); u1 = std::clamp(u1, -h, h);
	if (u1 <= u0) return 0.0;
	return (u1 - u0) * (1.0 - d * d / (R * R)) - (u1 * u1 * u1 - u0 * u0 * u0) / (3.0 * R * R);
}

double ChordFull (double R, double rho)
{
	if (R <= 0.0 || rho >= R) return 0.0;
	double h = std::sqrt(R * R - rho * rho);
	return 4.0 / 3.0 * h * h * h / (R * R);
}

double HG (double c, double g)
{
	double g2 = g * g;
	return (1.0 - g2) / (4.0 * PI * std::pow(std::max(1e-9, 1.0 + g2 - 2.0 * g * c), 1.5));
}

double BandIntegral (double lambda, double cosA, double cap)
{
	double c = std::fabs(cosA);
	if (lambda <= 0.0) return 0.0;
	if (c * cap <= 1.0) return cap * lambda;
	return lambda / c;
}

static void Cross (const double a[3], const double b[3], double o[3])
{
	o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}

static double Dot (const double a[3], const double b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

static void Norm (double a[3])
{
	double l = std::sqrt(Dot(a, a));
	if (l > 0.0) { a[0] /= l; a[1] /= l; a[2] /= l; }
}

void DeepOpacity (const std::vector<Puff> &pf, const double sunIn[3], int N, std::vector<double> &tau)
{
	size_t n = pf.size();
	tau.assign(n, 0.0);
	if (!n || N < 2) return;
	double s[3] = { sunIn[0], sunIn[1], sunIn[2] };
	Norm(s);
	double ref[3] = { 1, 0, 0 };
	if (std::fabs(s[0]) > 0.9) { ref[0] = 0; ref[1] = 1; }
	double e1[3], e2[3];
	Cross(s, ref, e1); Norm(e1);
	Cross(s, e1, e2);
	struct P { double u, v, z, r, sg; size_t i; };
	std::vector<P> q(n);
	double u0 = 1e300, u1 = -1e300, v0 = 1e300, v1 = -1e300;
	for (size_t i = 0; i < n; i++) {
		q[i] = { Dot(pf[i].p, e1), Dot(pf[i].p, e2), Dot(pf[i].p, s), pf[i].r, pf[i].sigma, i };
		u0 = std::min(u0, q[i].u - q[i].r); u1 = std::max(u1, q[i].u + q[i].r);
		v0 = std::min(v0, q[i].v - q[i].r); v1 = std::max(v1, q[i].v + q[i].r);
	}
	double cell = std::max(std::max(u1 - u0, v1 - v0) / N, 1e-6);
	std::sort(q.begin(), q.end(), [](const P &a, const P &b) { // toward the sun first; ties by position, so the input order never matters
		if (a.z != b.z) return a.z > b.z;
		if (a.u != b.u) return a.u < b.u;
		if (a.v != b.v) return a.v < b.v;
		if (a.r != b.r) return a.r < b.r;
		return a.sg < b.sg;
	});
	std::vector<double> grid(size_t(N) * N, 0.0);
	for (const P &p : q) {
		int cx = std::clamp(int((p.u - u0) / cell), 0, N - 1), cy = std::clamp(int((p.v - v0) / cell), 0, N - 1);
		tau[p.i] = grid[size_t(cy) * N + cx];
		if (p.r <= 0.0 || p.sg <= 0.0) continue;
		int rc = int(std::ceil(p.r / cell)) + 1;
		for (int y = std::max(0, cy - rc); y <= std::min(N - 1, cy + rc); y++)
			for (int x = std::max(0, cx - rc); x <= std::min(N - 1, cx + rc); x++) {
				double du = u0 + (x + 0.5) * cell - p.u, dv = v0 + (y + 0.5) * cell - p.v;
				double rho = std::sqrt(du * du + dv * dv);
				if (rho < p.r) grid[size_t(y) * N + x] += p.sg * ChordFull(p.r, rho);
			}
	}
}

void FlowViewProj (const double c[3], const double fIn[3], double ext, double eyeDist, double zn, double zf, double vp[16], double X[3], double Y[3])
{
	double f[3] = { fIn[0], fIn[1], fIn[2] };
	Norm(f);
	double eye[3] = { c[0] - f[0] * eyeDist, c[1] - f[1] * eyeDist, c[2] - f[2] * eyeDist };
	double up[3] = { 0, 1, 0 };
	if (std::fabs(f[1]) > 0.9) { up[0] = 1; up[1] = 0; }
	double Z[3] = { -f[0], -f[1], -f[2] }; // eye - at
	Cross(up, Z, X); Norm(X);
	Cross(Z, X, Y);
	double V[16] = { X[0], Y[0], Z[0], 0, X[1], Y[1], Z[1], 0, X[2], Y[2], Z[2], 0, -Dot(X, eye), -Dot(Y, eye), -Dot(Z, eye), 1 };
	double l = -ext, r = ext, b = -ext, t = ext;
	double P[16] = { 2.0 / (r - l), 0, 0, 0, 0, 2.0 / (t - b), 0, 0, 0, 0, 1.0 / (zn - zf), 0, (l + r) / (l - r), (t + b) / (b - t), zn / (zn - zf), 1 };
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			double s = 0.0;
			for (int k = 0; k < 4; k++) s += V[i * 4 + k] * P[k * 4 + j];
			vp[i * 4 + j] = s;
		}
}

struct UF {
	std::vector<int> p;
	int n;
	explicit UF (int k) : p(k), n(k) { std::iota(p.begin(), p.end(), 0); }
	int Find (int i) { while (p[i] != i) { p[i] = p[p[i]]; i = p[i]; } return i; }
	bool Join (int a, int b) { a = Find(a); b = Find(b); if (a == b) return false; p[std::max(a, b)] = std::min(a, b); n--; return true; }
};

// touching nozzles in groups of more than TOUCH_MAX; engines below the volume LOD within 3 radii; then nearest pairs while over the budget (design review M4, design-plume-nozzles 1)
void Cluster (const std::vector<Nozzle> &nz, int budget, std::vector<std::vector<int>> &groups)
{
	int n = int(nz.size());
	UF uf(n), touch(n);
	struct Pair { double k; int a, b; };
	std::vector<Pair> pairs, touching;
	for (int i = 0; i < n; i++)
		for (int j = i + 1; j < n; j++) {
			const Nozzle &A = nz[i], &B = nz[j];
			if (A.vessel != B.vessel || A.cls != B.cls || Dot(A.axis, B.axis) <= 0.98) continue;
			double d[3] = { A.ref[0] - B.ref[0], A.ref[1] - B.ref[1], A.ref[2] - B.ref[2] };
			double dist = std::sqrt(Dot(d, d)), rs = A.re + B.re, k = dist / std::max(rs, 1e-6);
			if (dist < 1.2 * rs) { touch.Join(i, j); touching.push_back({ k, i, j }); }
			else if (A.rpx < 3.0 && B.rpx < 3.0 && dist < 3.0 * std::max(A.re, B.re)) uf.Join(i, j);
			else pairs.push_back({ k, i, j });
		}
	std::vector<int> cnt(n, 0);
	for (int i = 0; i < n; i++) cnt[touch.Find(i)]++;
	for (const Pair &p : touching) {
		if (cnt[touch.Find(p.a)] > TOUCH_MAX) uf.Join(p.a, p.b); // a big cluster: one volume, the exits per nozzle
		else pairs.push_back(p); // apart, first to join when over the budget
	}
	if (uf.n > budget) {
		std::sort(pairs.begin(), pairs.end(), [](const Pair &p, const Pair &q) { return p.k < q.k; });
		for (const Pair &p : pairs) { if (uf.n <= budget) break; uf.Join(p.a, p.b); }
	}
	groups.clear();
	std::vector<int> slot(n, -1);
	for (int i = 0; i < n; i++) {
		int r = uf.Find(i);
		if (slot[r] < 0) { slot[r] = int(groups.size()); groups.emplace_back(); }
		groups[slot[r]].push_back(i);
	}
}

void FrustumPlanes (const double m[16], double pl[6][4])
{
	auto col = [&](int c, double s, int c2, double o[4]) {
		for (int r = 0; r < 4; r++) o[r] = m[r * 4 + c] + s * (c2 >= 0 ? m[r * 4 + c2] : 0.0);
	};
	col(3, 1.0, 0, pl[0]);  // left: w + x
	col(3, -1.0, 0, pl[1]); // right: w - x
	col(3, 1.0, 1, pl[2]);  // bottom: w + y
	col(3, -1.0, 1, pl[3]); // top: w - y
	col(2, 0.0, -1, pl[4]); // near: z
	col(3, -1.0, 2, pl[5]); // far: w - z
	for (int i = 0; i < 6; i++) {
		double l = std::sqrt(pl[i][0] * pl[i][0] + pl[i][1] * pl[i][1] + pl[i][2] * pl[i][2]);
		if (l > 0.0) for (int k = 0; k < 4; k++) pl[i][k] /= l;
	}
}

double ExitSheet ()
{
	return 0.15 * std::sqrt(PI) / 0.27; // the chord's 1/back cancels the slab's back
}

double MachSheet (double cosA)
{
	return 0.5 * 0.06 * std::sqrt(PI) / 0.12 / std::max(std::fabs(cosA), 0.25); // [V] 0.25: an edge-on sheet as a chord of 4 thicknesses
}

Light PlumeLight (int cls, double I, double re, const double c0[3], const double c1[3], double scale)
{
	const double K = 1.0, X = 1.0, R = 1.5; // [V] strength, metres past the exit and fall-off radius, in exit radii
	Light L = {};
	double c[3] = { 1.0, 0.7, 0.45 }, k = K * I * scale; // a texture flame: the puffs' orange
	if (cls != TEXTURE) {
		for (int i = 0; i < 3; i++) c[i] = c0[i] + c1[i]; // the exit's colour
		double mx = std::max(c[0], std::max(c[1], c[2]));
		if (!(mx > 0.0)) return L;
		for (int i = 0; i < 3; i++) c[i] /= mx;
		k = K * 2.0 * I * mx * scale; // the exit's brightness, 2 I (C0 + C1)
	}
	if (!(k > 0.0) || !(re > 0.0)) return L;
	double rf = R * re;
	for (int i = 0; i < 3; i++) L.rgb[i] = c[i];
	L.k = k;
	L.x = X * re;
	L.att2 = 1.0 / (rf * rf);
	L.range = LightRange(k, rf * rf);
	L.flux = k * rf * rf;
	return L;
}

double SphereMean (double k, double a, double r, double d)
{
	double A = 1.0 + a * (r * r + d * d), B = 2.0 * a * r * d;
	if (B < 1e-9 * A) return k / A;
	return k * std::log((A + B) / (A - B)) / (2.0 * B); // A - B = 1 + a (r - d)^2 > 0
}

double SphereMeanAtt (double I, double c, double b, double a, double r, double d)
{
	c = std::max(c, 1e-6);
	auto f = [&](double s) { return I * s / (c + b * s + a * s * s); };
	if (r <= 0.0 || d < 1e-6 * r) return I / (c + b * r + a * r * r);
	double s0 = std::fabs(r - d), s1 = r + d, h = (s1 - s0) / 64.0, sum = f(s0) + f(s1);
	for (int i = 1; i < 64; i++) sum += f(s0 + i * h) * (i & 1 ? 4.0 : 2.0);
	return sum * h / 3.0 / (2.0 * r * d); // the mean over the sphere in s: du = -s ds/(r d)
}

double LightRange (double k, double rf2)
{
	return std::min(std::sqrt(rf2 * std::max(0.0, 100.0 * k - 1.0)), 300.0);
}

Spot MergeSpots (const std::vector<Spot> &s)
{
	Spot m = {};
	double F = 0.0, P[3] = {}, C[3] = {}, M = 0.0;
	for (const Spot &a : s) {
		double f = a.k * a.rf2;
		if (!(f > 0.0)) continue;
		F += f;
		for (int i = 0; i < 3; i++) { P[i] += f * a.p[i]; C[i] += f * a.rgb[i]; }
		M += f * (a.rf2 + Dot(a.p, a.p));
	}
	if (!(F > 0.0)) return m;
	for (int i = 0; i < 3; i++) m.p[i] = P[i] / F;
	m.rf2 = std::max(M / F - Dot(m.p, m.p), 1e-6); // the second moment about the centre
	m.k = F / m.rf2;
	double mx = std::max(C[0], std::max(C[1], C[2]));
	for (int i = 0; i < 3; i++) m.rgb[i] = mx > 0.0 ? C[i] / mx : 0.0;
	return m;
}

bool CapsuleVisible (const double pl[6][4], const double a[3], const double b[3], double r)
{
	for (int i = 0; i < 6; i++) {
		double da = pl[i][0] * a[0] + pl[i][1] * a[1] + pl[i][2] * a[2] + pl[i][3];
		double db = pl[i][0] * b[0] + pl[i][1] * b[1] + pl[i][2] * b[2] + pl[i][3];
		if (da < -r && db < -r) return false;
	}
	return true;
}

// ---- base surfaces under a plume (design-pad-plume)

static const double CellM = 16.0;
static long long CellKey (double x, double z) { return (long long)(((unsigned long long)(long long)std::floor(x / CellM) << 32) ^ (unsigned long long)(uint32_t)(int32_t)std::floor(z / CellM)); }

void SurfaceGrid::Add (const double a[3], const double b[3], const double c[3], const double nrm[3])
{
	double e1[3] = { b[0] - a[0], b[1] - a[1], b[2] - a[2] }, e2[3] = { c[0] - a[0], c[1] - a[1], c[2] - a[2] };
	double n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
	double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]), ln = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
	if (!(l > 1e-9) || std::fabs(n[1]) < 0.7071 * l) return; // steep or degenerate
	if (ln > 0.1 && nrm[1] < 0.7071 * ln) return; // faces down by its normals: an underside
	double x0 = std::min({ a[0], b[0], c[0] }), x1 = std::max({ a[0], b[0], c[0] }), z0 = std::min({ a[2], b[2], c[2] }), z1 = std::max({ a[2], b[2], c[2] });
	if (x1 - x0 > 1024.0 || z1 - z0 > 1024.0) return; // ground sheets and runways: the terrain stands for them
	unsigned k = unsigned(tris.size());
	tris.push_back({ { a[0], b[0], c[0] }, { a[1], b[1], c[1] }, { a[2], b[2], c[2] } });
	ymax = std::max({ ymax, a[1], b[1], c[1] });
	for (double x = std::floor(x0 / CellM) * CellM; x <= x1; x += CellM)
		for (double z = std::floor(z0 / CellM) * CellM; z <= z1; z += CellM)
			cells[CellKey(x + 0.5 * CellM, z + 0.5 * CellM)].push_back(k);
}

double SurfaceGrid::Top (double x, double z, double ceil) const
{
	auto it = cells.find(CellKey(x, z));
	if (it == cells.end()) return NONE;
	double best = NONE;
	for (unsigned i : it->second) {
		const T &t = tris[i];
		double d = (t.z[1] - t.z[2]) * (t.x[0] - t.x[2]) + (t.x[2] - t.x[1]) * (t.z[0] - t.z[2]);
		if (std::fabs(d) < 1e-12) continue;
		double l1 = ((t.z[1] - t.z[2]) * (x - t.x[2]) + (t.x[2] - t.x[1]) * (z - t.z[2])) / d;
		double l2 = ((t.z[2] - t.z[0]) * (x - t.x[2]) + (t.x[0] - t.x[2]) * (z - t.z[2])) / d;
		double l3 = 1.0 - l1 - l2;
		if (l1 < -1e-9 || l2 < -1e-9 || l3 < -1e-9) continue;
		double h = l1 * t.y[0] + l2 * t.y[1] + l3 * t.y[2];
		if (h <= ceil && h > best) best = h;
	}
	return best;
}

double SurfaceGrid::Hit (const double o[3], const double d[3], double tmax) const
{
	if (!(tmax > 0.0) || cells.empty()) return NONE;
	double t0 = o[1] > ymax && d[1] < 0.0 ? (o[1] - ymax) / -d[1] : 0.0; // above every surface until the ray comes down to the top
	if (t0 >= tmax) return NONE;
	int n = std::clamp(int(std::ceil((tmax - t0) / 2.0)), 1, 64);
	double dt = (tmax - t0) / n, y0 = o[1] + d[1] * t0;
	for (int k = 1; k <= n; k++) {
		double t = t0 + k * dt, x = o[0] + d[0] * t, y = o[1] + d[1] * t, z = o[2] + d[2] * t;
		double h = Top(x, z, std::max(y0, y)); // a surface the step from y0 to y can reach
		if (h > NONE && h >= y) return y0 > y ? t - dt + dt * std::clamp((y0 - h) / (y0 - y), 0.0, 1.0) : t - dt;
		y0 = y;
	}
	return NONE;
}

double SurfaceGrid::Deck (double x, double z, double r, double ceil, int &ring) const
{
	double best = Top(x, z, ceil), outer[8];
	for (int k = 0; k < 8; k++) {
		double c = std::cos(k * PI / 4.0), s = std::sin(k * PI / 4.0);
		best = std::max(best, Top(x + 0.5 * r * c, z + 0.5 * r * s, ceil));
		outer[k] = Top(x + r * c, z + r * s, ceil);
		best = std::max(best, outer[k]);
	}
	ring = 0;
	if (best > NONE) for (double h : outer) if (h > NONE && h >= best - 3.0) ring++;
	return best;
}

}
