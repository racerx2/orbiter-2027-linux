// not upstream: height fog maths (OVP/VulkanClient/FogMath.cpp, design-height-fog 15 and Rev 2)

#include <catch2/catch_test_macros.hpp>
#include "FogMath.h"
#include <algorithm>
#include <cmath>
#include <random>

using namespace FogMath;

static const double PI_ = 3.14159265358979323846, RAD = PI_ / 180.0, RE = 6371e3;

static void SunVec (double decDeg, double lngDeg, double s[3]) // planet-local unit vector to a subsolar point
{
	double d = decDeg * RAD, l = lngDeg * RAD;
	s[0] = std::cos(d) * std::cos(l); s[1] = std::sin(d); s[2] = std::cos(d) * std::sin(l);
}

static void LocalPos (double latDeg, double lngDeg, double r, double p[3])
{
	double la = latDeg * RAD, lo = lngDeg * RAD;
	p[0] = r * std::cos(la) * std::cos(lo); p[1] = r * std::sin(la); p[2] = r * std::cos(la) * std::sin(lo);
}

// a ray from altitude hc above (lat, lng) heading north at pitch (deg, negative down); o and d planet-local
static void MakeRay (double hc, double pitchDeg, double o[3], double d[3])
{
	LocalPos(0.0, 0.0, RE + hc, o);
	double c = std::cos(pitchDeg * RAD), s = std::sin(pitchDeg * RAD);
	d[0] = s; d[1] = c; d[2] = 0.0; // up is +x at lat 0 lng 0, north is +y
}

// reference optical depth by a fine midpoint sum, noise off
static double RefTau (const Params &P, const FieldFn &fld, const double o[3], const double d[3], double t0, double t1)
{
	const int N = 200000;
	double ro = std::sqrt(o[0] * o[0] + o[1] * o[1] + o[2] * o[2]), hc = ro - RE, mu = (o[0] * d[0] + o[1] * d[1] + o[2] * d[2]) / ro;
	double dt = (t1 - t0) / N, tau = 0.0;
	for (int i = 0; i < N; i++) {
		double t = t0 + (i + 0.5) * dt, p[3] = { o[0] + d[0] * t, o[1] + d[1] * t, o[2] + d[2] * t };
		float f[4], ef;
		if (!fld(p, f, &ef)) continue;
		tau += Density(P, f, ef, RayAlt(hc, RE, mu, t), 0.5f) * dt;
	}
	return tau;
}

static FieldFn Flat (float F, float V, float m) // the same fields everywhere
{
	return [F, V, m](const double *, float f[4], float *ef) { f[0] = F; f[1] = V; f[2] = LIT; f[3] = m; *ef = 1.0f; return true; };
}

static double TfromLayout (double hc, double pitch, double t1, const Params &P, const FieldFn &fld, double j, int steps = 16)
{
	double o[3], d[3];
	MakeRay(hc, pitch, o, d);
	return Transmittance(P, fld, o, d, RE, 0.5, t1, steps, j, -1.0);
}

static double HitAlt (double hc, double pitch, double hTarget) // distance where the ray reaches an altitude (descending)
{
	double mu = std::sin(pitch * RAD), a = 0.0, b = 400e3;
	for (int i = 0; i < 200; i++) { double m = 0.5 * (a + b); (RayAlt(hc, RE, mu, m) > hTarget ? a : b) = m; }
	return 0.5 * (a + b);
}

TEST_CASE("FogMath: planes and tile ranges", "[fogmath]")
{
	Plane pl;
	pl.Set(46.0 * RAD, 7.0 * RAD);
	for (double x : { -150e3, 0.0, 99e3 })
		for (double y : { -120e3, 3e3, 150e3 }) {
			double la, lo, p[3], X, Y;
			pl.FromMap(x, y, RE, &la, &lo);
			LocalPos(la / RAD, lo / RAD, RE + 2000.0, p);
			REQUIRE(pl.ToMap(p, RE, &X, &Y));
			CHECK(std::fabs(X - x) < 0.01);
			CHECK(std::fabs(Y - y) < 0.01);
		}
	// every point of a box lies in the range, across the date line and near a pole
	for (double lat : { 0.0, 60.0, 85.0 })
		for (double lng : { 7.0, 179.9, -179.95 }) {
			int lvl = 7, ia, ib, ja, jb, nlng = 2 << lvl, nlat = 1 << lvl;
			double dl = 180e3 / RE, dg = dl / std::max(0.02, std::cos(lat * RAD));
			double la0 = lat * RAD - dl, la1 = std::min(PI_ / 2, lat * RAD + dl), lo0 = lng * RAD - dg, lo1 = lng * RAD + dg;
			TileRange(la0, la1, lo0, lo1, lvl, &ia, &ib, &ja, &jb);
			for (int s = 0; s <= 20; s++)
				for (int r = 0; r <= 20; r++) {
					double la = la0 + (la1 - la0) * s / 20.0, lo = lo0 + (lo1 - lo0) * r / 20.0;
					int i = std::min(nlat - 1, int(std::floor((0.5 - la / PI_) * nlat)));
					double lw = std::remainder(lo, 2.0 * PI_);
					int j = std::min(nlng - 1, int(std::floor(lw / (2.0 * PI_) * nlng + nlng / 2)));
					CHECK(i >= ia);
					CHECK(i <= ib);
					bool in = false;
					for (int q = ja; q <= jb; q++) in = in || ((q % nlng) + nlng) % nlng == j;
					CHECK(in);
				}
		}
	// the grid box holds every texel corner of both cascades, also next to and over a pole (review SHOULD 1)
	for (double lat : { 0.0, 45.0, 85.0, 89.0, 89.3, 89.5, 90.0, -89.7 })
		for (double lng : { 7.0, 179.9 })
			for (double half : { 25e3, 150e3 }) {
				double la0, la1, lo0, lo1;
				GridBox(lat * RAD, lng * RAD, half * 1.415, RE, &la0, &la1, &lo0, &lo1);
				Plane m;
				m.Set(lat * RAD, lng * RAD);
				for (int s = 0; s <= 32; s++)
					for (int r = 0; r <= 32; r++) {
						double la, lo;
						m.FromMap(-half + 2.0 * half * r / 32.0, half - 2.0 * half * s / 32.0, RE, &la, &lo);
						CHECK(la >= la0 - 1e-12);
						CHECK(la <= la1 + 1e-12);
						CHECK(std::fabs(std::remainder(lo - 0.5 * (lo0 + lo1), 2.0 * PI_)) <= 0.5 * (lo1 - lo0) + 1e-9);
					}
			}
}

TEST_CASE("FogMath: grids, the finest first and across the date line", "[fogmath]")
{
	auto Make = [](int lvl, double la0, double la1, double lo0, double lo1, float v) {
		auto g = std::make_shared<Horizon::Grid>();
		g->lvl = lvl;
		g->minlat = la0 * RAD; g->maxlat = la1 * RAD; g->minlng = lo0 * RAD; g->maxlng = lo1 * RAD;
		g->h.assign(size_t(Horizon::STRIDE) * Horizon::STRIDE, v);
		return g;
	};
	auto coarse = Make(5, 40.0, 50.0, 0.0, 20.0, 100.0f), fine = Make(8, 45.0, 46.0, 7.0, 8.0, 900.0f);
	auto east = Make(7, -10.0, 10.0, 170.0, 180.0, 50.0f), west = Make(7, -10.0, 10.0, -180.0, -170.0, 60.0f);
	std::vector<const Horizon::Grid*> g = { fine.get(), coarse.get(), east.get(), west.get() }; // finest first, as Bake sorts them
	float h = 0.0f;
	CHECK(GridHeight(g, 45.5 * RAD, 7.5 * RAD, &h)); CHECK(h == 900.0f);
	CHECK(GridHeight(g, 42.0 * RAD, 3.0 * RAD, &h)); CHECK(h == 100.0f);
	CHECK(GridHeight(g, 0.0, 179.9 * RAD, &h)); CHECK(h == 50.0f);
	CHECK(GridHeight(g, 0.0, -179.9 * RAD, &h)); CHECK(h == 60.0f);
	CHECK(GridHeight(g, 0.0, 180.0 * RAD + 0.05 * RAD, &h)); CHECK(h == 60.0f); // past +180 wraps to the west grid
	CHECK(!GridHeight(g, 60.0 * RAD, 3.0 * RAD, &h));
}

TEST_CASE("FogMath: sunrise and local time", "[fogmath]")
{
	double s[3];
	SunVec(0.0, 0.0, s);
	SunTime st = Sun(0.0, 0.0, s, 1);
	CHECK(std::fabs(st.dayLen - 12.0) < 1.0 / 60.0);
	CHECK(std::fabs(st.tau - 12.0) < 1e-6);
	CHECK(std::fabs(Sun(0.0, 90.0 * RAD, s, 1).tau - 18.0) < 1e-6);
	CHECK(std::fabs(Sun(0.0, 90.0 * RAD, s, -1).tau - 6.0) < 1e-6);
	SunVec(23.44, 0.0, s);
	CHECK(std::fabs(Sun(50.0 * RAD, 0.0, s, 1).dayLen - 16.15) < 0.1); // geometric, no refraction
	CHECK(Sun(70.0 * RAD, 0.0, s, 1).polarDay);
	CHECK(Sun(-70.0 * RAD, 0.0, s, 1).polarNight);
	CHECK(!Sun(60.0 * RAD, 0.0, s, 1).polarDay);
}

TEST_CASE("FogMath: diurnal strength", "[fogmath]")
{
	for (double lat : { 0.0, 45.0, 65.0, 66.0 })
		for (double dec : { -23.44, -10.0, 0.0, 23.44 }) {
			double s[3], prevF = -1.0, prevH = -1.0, maxF = 0.0, maxH = 0.0;
			SunVec(dec, 0.0, s);
			for (int m = 0; m <= 24 * 60; m++) {
				double lng = (m / 60.0 - 12.0) * 15.0 * RAD, H;
				double f = Diurnal(Sun(lat * RAD, lng, s, 1), 0.5, &H);
				CHECK(f >= 0.0);
				CHECK(f <= 1.0 + 1e-9);
				if (prevF >= 0.0) { maxF = std::max(maxF, std::fabs(f - prevF)); maxH = std::max(maxH, std::fabs(H - prevH)); }
				prevF = f; prevH = H;
			}
			CHECK(maxF < 0.01);
			CHECK(maxH < 25.0);
			CHECK(std::fabs(prevF - Diurnal(Sun(lat * RAD, -12.0 * 15.0 * RAD, s, 1), 0.5, nullptr)) < 1e-9); // 24 h periodic
		}
	double s[3];
	SunVec(0.0, 0.0, s);
	auto At = [&](double hour) { return Diurnal(Sun(45.0 * RAD, (hour - 12.0) * 15.0 * RAD, s, 1), 0.5, nullptr); };
	CHECK(std::fabs(At(6.0) - 1.0) < 1e-6);  // sunrise at the equinox
	CHECK(At(10.0) <= 0.12);
	CHECK(At(0.0) < 0.45);                   // midnight: the ^2 ramp
}

TEST_CASE("FogMath: floor and valley depth", "[fogmath]")
{
	const int n = MAPN;
	auto Terrain = [](double x, double y) { return 300.0 + 0.3 * std::fabs(x) + 0.0 * y; }; // V valley along y
	for (double step : { 195.3125, 1171.875 }) {
		std::vector<float> mean(size_t(n) * n), F, G;
		double half = 0.5 * n * step;
		for (int j = 0; j < n; j++)
			for (int i = 0; i < n; i++) mean[size_t(j) * n + i] = float(Terrain(-half + (i + 0.5) * step, 0.0));
		Floor(mean, n, step, F, G);
		bool below = true;
		for (size_t k = 0; k < mean.size(); k++) below = below && F[k] <= mean[k] + 1e-3f;
		CHECK(below);
		CHECK(std::fabs(F[size_t(n / 2) * n + n / 2] - 300.0f) < 5.0f + 0.3f * float(step)); // within the texel's own spread
	}
	std::vector<float> flat(size_t(n) * n, 250.0f), F, G;
	Floor(flat, n, 195.3125, F, G);
	for (size_t k = 0; k < flat.size(); k += 997) { CHECK(F[k] == 250.0f); CHECK(G[k] == 250.0f); }
	// near and far on the same terrain, in the blend band
	auto Hill = [](double x, double y) { return 800.0 + 400.0 * std::sin(x / 7000.0) * std::cos(y / 9000.0) + 0.01 * x; };
	std::vector<float> mN(size_t(n) * n), mF(size_t(n) * n), FN, GN, FF, GF;
	double sN = 50e3 / n, sF = 300e3 / n;
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			mN[size_t(j) * n + i] = float(Hill(-25e3 + (i + 0.5) * sN, 25e3 - (j + 0.5) * sN));
			mF[size_t(j) * n + i] = float(Hill(-150e3 + (i + 0.5) * sF, 150e3 - (j + 0.5) * sF));
		}
	Floor(mN, n, sN, FN, GN);
	Floor(mF, n, sF, FF, GF);
	double worst = 0.0;
	for (int k = 0; k < 64; k++) {
		double a = k * 2.0 * PI_ / 64.0, r = 21.5e3, x = r * std::cos(a), y = r * std::sin(a);
		int iN = int((x + 25e3) / sN), jN = int((25e3 - y) / sN), iF = int((x + 150e3) / sF), jF = int((150e3 - y) / sF);
		worst = std::max(worst, double(std::fabs(FN[size_t(jN) * n + iN] - FF[size_t(jF) * n + iF])));
	}
	CHECK(worst < 30.0);
}

// brute force: highest terrain along the sun ray from a point, with curvature, as a shadow height
static double BruteHs (const std::vector<float> &h, int n, double step, double x, double y, double sE, double sN, double sU, double R)
{
	double ch = std::sqrt(sE * sE + sN * sN), uE = sE / ch, uN = sN / ch, half = 0.5 * n * step, best = -1e30;
	for (double s = step * 0.5; s < 2.0 * half; s += step * 0.25) {
		double px = x + uE * s, py = y + uN * s;
		double fi = (px + half) / step - 0.5, fj = (half - py) / step - 0.5;
		if (fi < 0 || fj < 0 || fi > n - 1 || fj > n - 1) break;
		int i0 = std::min(int(fi), n - 2), j0 = std::min(int(fj), n - 2);
		double a = fi - i0, b = fj - j0;
		double hh = (h[size_t(j0) * n + i0] * (1 - a) + h[size_t(j0) * n + i0 + 1] * a) * (1 - b) + (h[size_t(j0 + 1) * n + i0] * (1 - a) + h[size_t(j0 + 1) * n + i0 + 1] * a) * b;
		double hpt = hh - (px * px + py * py) / (2.0 * R) + (x * x + y * y) / (2.0 * R); // the terrain on the tangent plane, back to the point
		best = std::max(best, hpt - s * sU / ch);
	}
	return best;
}

TEST_CASE("FogMath: shadow sweep", "[fogmath]")
{
	const int n = MAPN;
	double step = 195.3125, half = 0.5 * n * step;
	double e = 10.0 * RAD, sE = std::cos(e), sN = 0.0, sU = std::sin(e); // sun in the east
	std::vector<float> hp(size_t(n) * n, 0.0f), Hp;
	for (int j = 0; j < n; j++) hp[size_t(j) * n + 200] = 1000.0f;   // a ridge along north-south
	ShadowSweep(hp, n, step, sE, sN, sU, nullptr, Hp);
	for (int i : { 195, 190, 180 }) {
		double want = 1000.0 - (200 - i) * step * std::tan(e);
		CHECK(std::fabs(Hp[size_t(128) * n + i] - want) <= 0.05 * 1000.0);
	}
	CHECK(Hp[size_t(128) * n + 210] == 0.0f); // the sun side
	// fBm terrain against a brute-force ray test, sun at 8 deg from the south-east
	std::mt19937 rng(7);
	std::uniform_real_distribution<double> U(0.0, 2.0 * PI_);
	std::vector<float> h(size_t(n) * n, 0.0f);
	double ph[6][2];
	for (auto &p : ph) { p[0] = U(rng); p[1] = U(rng); }
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			double x = -half + (i + 0.5) * step, y = half - (j + 0.5) * step, v = 0.0, a = 900.0, k = 1.0 / 9000.0;
			for (auto &p : ph) { v += a * std::sin(x * k + p[0]) * std::cos(y * k * 1.3 + p[1]); a *= 0.5; k *= 2.0; }
			h[size_t(j) * n + i] = float(1000.0 + v);
		}
	double el = 8.0 * RAD, az = 135.0 * RAD;
	double tE = std::cos(el) * std::sin(az), tN = std::cos(el) * std::cos(az), tU = std::sin(el);
	std::vector<float> hpl(h.size());
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			double x = -half + (i + 0.5) * step, y = half - (j + 0.5) * step;
			hpl[size_t(j) * n + i] = float(h[size_t(j) * n + i] - (x * x + y * y) / (2.0 * RE));
		}
	ShadowSweep(hpl, n, step, tE, tN, tU, nullptr, Hp);
	double se = 0.0;
	int cnt = 0, agree = 0, tot = 0;
	for (int j = 40; j < n - 8; j += 7)
		for (int i = 8; i < n - 40; i += 7) {
			double x = -half + (i + 0.5) * step, y = half - (j + 0.5) * step;
			double ref = std::max(double(h[size_t(j) * n + i]), BruteHs(h, n, step, x, y, tE, tN, tU, RE));
			double got = Hp[size_t(j) * n + i] + (x * x + y * y) / (2.0 * RE);
			se += (got - ref) * (got - ref);
			cnt++;
			bool litRef = ref <= h[size_t(j) * n + i] + 1.0, litGot = got <= h[size_t(j) * n + i] + 1.0;
			agree += litRef == litGot;
			tot++;
		}
	CHECK(std::sqrt(se / cnt) <= 10.0);
	CHECK(agree >= 0.97 * tot);
}

TEST_CASE("FogMath: water", "[fogmath]")
{
	const int n = 64;
	double step = 195.3125;
	auto Run = [&](const std::vector<float> &m, bool wet, std::vector<float> &sea, std::vector<float> &lake) {
		std::vector<float> F, G, lo;
		Floor(m, n, step, F, G, &lo);
		Water(m, lo, n, step, wet, sea, lake);
	};
	std::vector<float> sea, lake;
	Run(std::vector<float>(size_t(n) * n, 0.0f), true, sea, lake);
	CHECK(sea[size_t(32) * n + 32] == 1.0f);
	CHECK(lake[size_t(32) * n + 32] == 0.0f);
	std::vector<float> basin(size_t(n) * n); // a flat lake at 400 m in hills
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) {
			double r = std::hypot(i - 32.0, j - 32.0);
			basin[size_t(j) * n + i] = r < 14.0 ? 400.0f : float(400.0 + 30.0 * (r - 14.0));
		}
	Run(basin, true, sea, lake);
	CHECK(lake[size_t(32) * n + 32] > 0.9f);
	CHECK(sea[size_t(32) * n + 32] == 0.0f);
	Run(basin, false, sea, lake);
	CHECK(lake[size_t(32) * n + 32] == 0.0f);
	std::vector<float> slope(size_t(n) * n); // 0.15 % planar, in 0.5 m steps
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) slope[size_t(j) * n + i] = float(std::floor((100.0 + 0.0015 * i * step) * 2.0) * 0.5);
	Run(slope, true, sea, lake);
	float mx = 0.0f;
	for (float v : lake) mx = std::max(mx, v);
	CHECK(mx == 0.0f);
	std::vector<float> mesa(size_t(n) * n); // a flat top above lower ground is no lake
	for (int j = 0; j < n; j++)
		for (int i = 0; i < n; i++) mesa[size_t(j) * n + i] = std::hypot(i - 32.0, j - 32.0) < 8.0 ? 400.0f : 200.0f;
	Run(mesa, true, sea, lake);
	CHECK(lake[size_t(32) * n + 32] == 0.0f);
}

TEST_CASE("FogMath: step layout and transmittance", "[fogmath]")
{
	Params P;
	// a constant slab
	P.sh = 1e-3f; P.Hh = 1e9f; P.Hinv = 1e9f;
	double o[3], d[3];
	MakeRay(100.0, 0.0, o, d);
	double T = Transmittance(P, Flat(-1e4f, 0.0f, 0.0f), o, d, RE, 0.0, 5000.0, 16, 0.5, -1.0);
	CHECK(std::fabs(T - std::exp(-5.0)) < 0.01 * std::exp(-5.0));
	// an exponential layer, slant and horizontal with curvature
	P.sh = 2e-5f; P.Hh = 350.0f; P.Hinv = 1e9f; P.Gmean = 0.0f;
	for (double pitch : { 10.0, 0.0 }) {
		MakeRay(20.0, pitch, o, d);
		double t1 = 150e3, ref = RefTau(P, Flat(0.0f, 0.0f, 0.0f), o, d, 0.5, t1);
		for (double j : { 0.1, 0.5, 0.9 }) {
			double tau = -std::log(Transmittance(P, Flat(0.0f, 0.0f, 0.0f), o, d, RE, 0.5, t1, 16, j, -1.0));
			CHECK(std::fabs(tau - ref) < 0.02 * ref);
		}
	}
	// review MUST 1: 2.5 km above a valley, the floor 10 km off at 14.5 deg down, a 150 m lid over the last 600 m
	Params V;
	V.D0 = 150.0f; V.kv = 0.0f; V.Hh = 350.0f; V.Hinv = 400.0f; V.Gmean = 500.0f;
	FieldFn valley = Flat(500.0f, 0.0f, 1.0f);
	for (float sf : { 0.008f, 0.0005f }) {
		V.sf = sf;
		double t1 = HitAlt(3000.0, -14.5, 500.0);
		CHECK(std::fabs(t1 - 10e3) < 150.0);
		MakeRay(3000.0, -14.5, o, d);
		double aRef = 1.0 - std::exp(-RefTau(V, valley, o, d, 0.5, t1));
		for (double j : { 0.0, 0.25, 0.5, 0.75, 0.99 }) {
			double a = 1.0 - TfromLayout(3000.0, -14.5, t1, V, valley, j);
			CHECK(std::fabs(a - aRef) <= 0.03 * aRef);
		}
	}
	// a fog sea 30 km away from a peak
	for (float sf : { 0.002f, 0.0003f }) {
		V.sf = sf;
		double pitch = -3.5, t1 = HitAlt(2500.0, pitch, 450.0);
		FieldFn sea = Flat(450.0f, 0.0f, 1.0f);
		MakeRay(2500.0, pitch, o, d);
		double aRef = 1.0 - std::exp(-RefTau(V, sea, o, d, 0.5, t1));
		CHECK(t1 > 25e3);
		for (double j : { 0.0, 0.25, 0.5, 0.75, 0.99 }) {
			double a = 1.0 - TfromLayout(2500.0, pitch, t1, V, sea, j);
			CHECK(std::fabs(a - aRef) <= 0.03 * aRef);
		}
	}
	// the edges tile the ray and stay ordered
	Layout L;
	MakeRay(3000.0, -14.5, o, d);
	V.sf = 0.008f;
	MakeLayout(V, valley, o, d, RE, 0.5, 10e3, L);
	double e[17];
	Steps(L.t, L.w, L.n, 16, e);
	CHECK(e[0] == 0.5);
	CHECK(e[16] == 10e3);
	for (int i = 0; i < 16; i++) CHECK(e[i + 1] >= e[i]);
	CHECK(L.n <= MAXP);
	// a floor that jumps at every few hundred metres: the crossings never overflow the layout
	FieldFn comb = [](const double p[3], float f[4], float *ef) { bool hi = int(std::floor(p[1] / 300.0)) & 1; f[0] = hi ? 2600.0f : -SEAENC; f[1] = hi ? 0.0f : SEAENC; f[2] = LIT; f[3] = 1.0f; *ef = 1.0f; return true; }; // ridges and open sea
	V.ss = 0.004f; V.Ds = 200.0f;
	for (double pitch : { -1.0, -14.5 }) {
		MakeRay(3000.0, pitch, o, d);
		MakeLayout(V, comb, o, d, RE, 0.5, 20e3, L);
		CHECK(L.n <= MAXP);
		CHECK(L.n >= SCAN + 1);
		Steps(L.t, L.w, L.n, 16, e);
		for (int i = 0; i < 16; i++) CHECK(e[i + 1] >= e[i]);
		CHECK(e[16] == 20e3);
	}
}

TEST_CASE("FogMath: sun transmittance through a flat fog", "[fogmath]")
{
	Params V;
	V.sf = 0.004f; V.D0 = 200.0f; V.kv = 0.0f; V.Hh = 350.0f; V.Hinv = 400.0f; V.Gmean = 100.0f;
	FieldFn fld = Flat(100.0f, 600.0f, 1.0f); // a deep valley: the full D0, lid at 300 m
	double o[3], d[3];
	MakeRay(150.0, 10.0, o, d);
	double T = Transmittance(V, fld, o, d, RE, 0.0, 30e3, 24, 0.5, -1.0);
	double want = std::exp(-0.004 * (300.0 - 150.0) / std::sin(10.0 * RAD));
	CHECK(std::fabs(T - want) < 0.02 * want);
}

TEST_CASE("FogMath: fog days", "[fogmath]")
{
	for (uint32_t seed : { 1u, 2u, 3u }) {
		double prev = DayNoise(0.8, 0.12, 51000.0, seed, 0.05, 1.0);
		double worst = 0.0;
		for (int m = 1; m <= 2 * 24 * 60; m++) {
			double v = DayNoise(0.8, 0.12, 51000.0 + m / 1440.0, seed, 0.05, 1.0);
			CHECK(v >= 0.0);
			CHECK(v <= 1.0);
			worst = std::max(worst, std::fabs(v - prev));
			prev = v;
		}
		CHECK(worst < 0.01);
		double sp = 0.0, a = DayNoise(0.8, 0.12, 51000.3, seed, 0.05, 1.0);
		for (int k = 1; k <= 200; k++) { double b = DayNoise(0.8, 0.12 + k * 1e-4, 51000.3, seed, 0.05, 1.0); sp = std::max(sp, std::fabs(b - a)); a = b; }
		CHECK(sp < 0.02); // 640 m steps
	}
	CHECK(std::fabs(DayNoise(0.8, 0.12, 51000.25, 1u, 0.05, 1.0) - 0.721927) < 1e-5); // fixed values: all platforms agree
	CHECK(std::fabs(DayNoise(-0.3, 2.0, 52000.75, 2u, 0.05, 2.0) - 0.051395) < 1e-5);
	CHECK(std::fabs(DayNoise(1.2, -1.0, 53000.5, 3u, 0.05, 1.0) - 0.456137) < 1e-5);
	for (double lat : { 0.2, 0.8, 1.0 }) {
		int free = 0;
		for (int day = 0; day < 365; day++) free += FogDay(lat, 0.3, 51000.25 + day, 0.3) <= 0.0;
		CHECK(free >= 0.55 * 365);
		CHECK(free <= 0.80 * 365);
	}
	CHECK(FogDay(0.8, 0.1, 51000.0, 0.3) <= 1.35);
	CHECK(SeaDay(0.8, 0.1, 51000.0, 0.0) == 0.0);
}

TEST_CASE("FogMath: frame parameters", "[fogmath]")
{
	FrameIn in;
	in.lat = 46.0 * RAD; in.lng = 0.0;
	SunVec(-20.0, 90.0 * 1.0, in.sun); // 6 h before local noon: dawn in winter
	in.wFar = 0.1;
	Params P = Frame(in);
	CHECK(P.sf > 0.004f);
	CHECK(P.sf < 0.0095f);
	CHECK(P.D0 > 100.0f);
	in.wet = false;
	Params D = Frame(in);
	CHECK(D.sf < 0.3f * P.sf);
	CHECK(D.ss == 0.0f);
	in.wet = true;
	SunVec(-20.0, 0.0, in.sun); // noon
	Params N = Frame(in);
	CHECK(N.sf < 0.2f * P.sf);
	CHECK(N.Hinv > P.Hinv);
}

TEST_CASE("FogMath: half floats", "[fogmath]")
{
	CHECK(ToHalf(0.0f) == 0x0000);
	CHECK(ToHalf(-0.0f) == 0x8000);
	CHECK(ToHalf(1.0f) == 0x3c00);
	CHECK(ToHalf(-1.0f) == 0xbc00);
	CHECK(ToHalf(65504.0f) == 0x7bff);
	CHECK(ToHalf(1e6f) == 0x7bff);
	CHECK(ToHalf(std::ldexp(1.0f, -14)) == 0x0400);
	CHECK(ToHalf(std::ldexp(1.0f, -24)) == 0x0001);
	CHECK(ToHalf(1.0f + std::ldexp(1.0f, -11)) == 0x3c00);                     // halfway: to even
	CHECK(ToHalf(1.0f + 3.0f * std::ldexp(1.0f, -11)) == 0x3c02);              // halfway: to even, up
	CHECK(ToHalf(std::ldexp(1.0f, -25)) == 0x0000);
	CHECK((ToHalf(3.0e38f) & 0x7c00) != 0x7c00);                                // no inf or NaN from finite input
	CHECK(ToHalf(5000.0f) == 0x6ce2);
}

TEST_CASE("FogMath: rebuild thresholds", "[fogmath]")
{
	Rebuild r;
	r.valid = r.sameObj = true;
	CHECK(!NeedRebuild(r, false));
	CHECK(NeedRebuild(Rebuild(), false));
	r.dist = 21e3; CHECK(NeedRebuild(r, false)); CHECK(NeedRebuild(r, true));
	r.dist = 4e3; CHECK(!NeedRebuild(r, false)); CHECK(NeedRebuild(r, true));
	r.dist = 0.0;
	r.sunDeg = 0.7; CHECK(!NeedRebuild(r, false)); CHECK(NeedRebuild(r, true));
	r.sunElevDeg = 3.0; r.sunDeg = 0.35; CHECK(NeedRebuild(r, false)); // 0.3 deg at a 3 deg sun
	r.sunDeg = 0.25; CHECK(!NeedRebuild(r, false));
	r.sunDeg = 0.0; r.age = 121.0; CHECK(NeedRebuild(r, false));
	r.age = 3.0; r.sigChanged = true; CHECK(!NeedRebuild(r, false)); CHECK(NeedRebuild(r, true));
}

TEST_CASE("FogMath: drift noise", "[fogmath]")
{
	CHECK(Hash3(0, 0, 0) == 0u);
	CHECK(Hash3(1, 2, 3) == Hash3(1, 2, 3));
	CHECK(Hash3(1, 2, 3) != Hash3(3, 2, 1));
	for (double x : { -1234.0, 0.0, 5e6 }) {
		float a = Drift(x, 777.0, -3e6, 0.0), b = Drift(x + 64.0 * 700.0, 777.0, -3e6, 0.0);
		CHECK(std::fabs(a - b) < 1e-5f);  // periodic in x
		CHECK(a >= 0.0f);
		CHECK(a <= 1.0f);
	}
	CHECK(std::fabs(Drift(10.0, 20.0, 30.0, 0.0) - Drift(10.5, 20.0, 30.0, 0.0)) < 0.01f);
}

TEST_CASE("FogMath: bake on a synthetic valley", "[fogmath]")
{
	auto g = std::make_shared<Horizon::Grid>(); // one grid around (46 N, 7 E) with a valley along the meridian
	g->lvl = 7;
	g->minlat = 45.0 * RAD; g->maxlat = 47.0 * RAD; g->minlng = 5.5 * RAD; g->maxlng = 8.5 * RAD;
	g->h.assign(size_t(Horizon::STRIDE) * Horizon::STRIDE, 0.0f);
	for (int i = -1; i <= 257; i++)
		for (int j = -1; j <= 257; j++) {
			double lng = g->minlng + (g->maxlng - g->minlng) * j / 256.0, x = (lng - 7.0 * RAD) * RE * std::cos(46.0 * RAD);
			g->h[size_t(i + 1) * Horizon::STRIDE + j + 1] = float(500.0 + std::min(1500.0, 0.3 * std::fabs(x)));
		}
	BakeIn in;
	in.lat = 46.0 * RAD; in.lng = 7.0 * RAD; in.half = 25e3; in.R = RE;
	in.grids.push_back(g);
	SunVec(-20.0, 7.0 + 60.0, in.sun); // morning sun from the east, about 20 deg up at the centre
	in.mjd = 51000.25;
	Map m;
	Bake(in, m);
	CHECK(m.cover > 0.99f);
	const float *c = &m.px[(size_t(128) * MAPN + 128) * 4];
	CHECK(std::fabs(c[0] - 500.0f) < 60.0f); // the floor on the axis
	CHECK(c[1] > 200.0f);                   // a deep valley
	CHECK(m.Fmin >= 480.0f);
	CHECK(m.Gmax <= 2100.0f);
	CHECK(m.Ltop > m.Fmin);
	CHECK(c[2] > c[0] + 500.0f);            // a low sun from the east: the floor lies in the east wall's shadow
	const float *e = &m.px[(size_t(128) * MAPN + 240) * 4]; // the east plateau is lit
	CHECK(e[2] < 2001.0f);
	double p[3];
	LocalPos(46.0, 7.0, RE + 600.0, p);
	float s[4];
	double r;
	REQUIRE(m.Sample(p, s, &r));
	CHECK(r < 0.01);
	CHECK(std::fabs(s[0] - c[0]) < 1.0f);
}

TEST_CASE("FogMath: coast and flat ground", "[fogmath]")
{
	auto g = std::make_shared<Horizon::Grid>(); // the sea east of 80.6 W, flat land 3 m high west of it
	g->lvl = 7;
	g->minlat = 27.5 * RAD; g->maxlat = 29.5 * RAD; g->minlng = -82.0 * RAD; g->maxlng = -79.0 * RAD;
	g->h.assign(size_t(Horizon::STRIDE) * Horizon::STRIDE, 0.0f);
	for (int i = -1; i <= 257; i++)
		for (int j = -1; j <= 257; j++) {
			double lng = g->minlng + (g->maxlng - g->minlng) * j / 256.0;
			g->h[size_t(i + 1) * Horizon::STRIDE + j + 1] = lng < -80.6 * RAD ? 3.0f : 0.0f;
		}
	BakeIn in;
	in.lat = 28.6 * RAD; in.lng = -80.6 * RAD; in.half = 25e3; in.R = RE;
	in.grids.push_back(g);
	SunVec(-20.0, -80.6 + 90.0, in.sun); // dawn
	in.mjd = 51565.51;
	Map m;
	Bake(in, m);
	REQUIRE(m.cover > 0.99f);
	auto At = [&](double xm) { int i = std::min(MAPN - 1, std::max(0, int((xm + 25e3) / (50e3 / MAPN)))); return &m.px[(size_t(128) * MAPN + i) * 4]; };
	CHECK(SeaW(At(10e3), 1.0f) > 0.99f);  // open sea: the full bank
	CHECK(SeaKL(At(10e3)) > 0.99f);
	CHECK(At(10e3)[3] == 0.0f);           // no radiation fog over it
	CHECK(SeaW(At(300.0), 1.0f) > 0.8f);  // the shore: dense, but the bank is lower
	CHECK(SeaKL(At(300.0)) > 0.15f);
	CHECK(SeaKL(At(300.0)) < 0.6f);
	CHECK(SeaW(At(-800.0), 1.0f) > 0.0f); // it reaches a little inland, low
	CHECK(SeaKL(At(-800.0)) < 0.3f);
	CHECK(SeaW(At(-3e3), 1.0f) == 0.0f);  // and stops
	int gap = 0, fog = 0, land = 0; // flat land: fog patches and clear gaps
	for (int j = 0; j < MAPN; j += 4)
		for (int i = 0; i < 64; i += 2) { float mm = m.px[(size_t(j) * MAPN + i) * 4 + 3]; land++; gap += mm == 0.0f; fog += mm > 1.0f; }
	CHECK(gap > land / 20); // about 70 cells: the fractions vary with the day's pattern
	CHECK(fog > land / 6);
	for (double x : { 10e3, 300.0, -800.0, -3e3, -10e3 }) {
		const float *o = At(x);
		CHECK(std::fabs(o[0] + o[1] - (x > 0.0 ? 0.0f : 3.0f)) < 2.0f); // F + V stays the mean ground
	}
	CHECK(m.Ltop >= 1.5f * 260.0f + 5.0f - 1e-3f);
	Params P;
	P.D0 = 190.0f; P.kv = 0.25f; P.sf = 0.008f;
	float flat[4] = { 3.0f, 0.0f, LIT, 1.0f }, vale[4] = { 3.0f, 600.0f, LIT, 1.0f };
	float basin[4] = { -86.0f, 0.0f, LIT, 1.0f }, open[4] = { -SEAENC, SEAENC, LIT, 0.0f }; // a dry basin below sea level is no sea (review r2 NIT 3)
	CHECK(SeaW(basin, 1.0f) == 0.0f);
	CHECK(SeaKL(basin) == 0.0f);
	CHECK(SeaW(open, 1.0f) == 1.0f);
	CHECK(SeaKL(open) == 1.0f);
	std::vector<float> low(size_t(64) * 64, -86.0f), F, G, lo, sea, lake;
	Floor(low, 64, 195.3125, F, G, &lo);
	Water(low, lo, 64, 195.3125, true, sea, lake);
	CHECK(sea[size_t(32) * 64 + 32] == 0.0f);
	CHECK(lake[size_t(32) * 64 + 32] > 0.9f); // a flat dry basin reads as a lake, as salt flats did before (design 13.3)
	CHECK(std::fabs(LidDepth(P, flat) - 57.0f) < 0.5f);   // shallow on flat ground
	CHECK(std::fabs(LidDepth(P, vale) - 340.0f) < 0.5f);  // the valley pool keeps its depth
	CHECK(Density(P, flat, 1.0f, 30.0, 0.5f) > 0.007f);
	CHECK(Density(P, flat, 1.0f, 120.0, 1.0f) < 1e-6f);   // a 300 m camera is far above it
}
