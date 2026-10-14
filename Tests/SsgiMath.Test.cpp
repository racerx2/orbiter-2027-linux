
// not upstream: screen-space GI maths (OVP/VulkanClient/SsgiMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "SsgiMath.h"
#include <cmath>
#include <random>
#include <vector>

using namespace SsgiMath;

namespace {

const double DEG = PI / 180.0;

// the angle where bit k's test point sits (BitMask covers bit k when (k + 1 - jit) / 32 of the mass lies in the interval): the CDF inverted by bisection
double BitAngle (int k, double g, double jit)
{
	double M = SliceMass(g), f = (k + 1.0 - jit) / 32.0 * M, lo = g - 0.5 * PI, hi = g + 0.5 * PI;
	for (int i = 0; i < 60; i++) { double m = 0.5 * (lo + hi); if (SliceCdf(m, g) < f) lo = m; else hi = m; }
	return 0.5 * (lo + hi);
}

// the estimator of SSGI.glsl's march: mean over slices of |n_p| M (covered bits) / 32, slices at (k + 0.5) pi / n around V
template <class Covered> double Estimate (const V3 &N, const V3 &V, int nSlices, Covered covered)
{
	V3 a = std::fabs(V.z) < 0.9 ? V3{ 0, 0, 1 } : V3{ 1, 0, 0 };
	V3 X = Unit(Cross(a, V)), Y = Cross(V, X);
	double sum = 0.0;
	for (int s = 0; s < nSlices; s++) {
		double phi = (s + 0.5) * PI / nSlices;
		V3 T = Add(Scale(X, std::cos(phi)), Scale(Y, std::sin(phi)));
		double np, g = SliceGamma(N, V, T, &np), jit = std::fmod(0.5 + s * 0.6180339887, 1.0); // the shader's per-slice dither
		int bits = 0;
		for (int k = 0; k < 32; k++) {
			double t = BitAngle(k, g, jit);
			V3 d = Add(Scale(V, std::cos(t)), Scale(T, std::sin(t)));
			if (covered(d)) bits++;
		}
		sum += np * SliceMass(g) * bits / 32.0;
	}
	return sum / nSlices;
}

// (1/pi) integral of cos over the directions of N's hemisphere that are covered
template <class Covered> double Brute (const V3 &N, Covered covered)
{
	V3 a = std::fabs(N.z) < 0.9 ? V3{ 0, 0, 1 } : V3{ 1, 0, 0 };
	V3 X = Unit(Cross(a, N)), Y = Cross(N, X);
	const int nt = 400, np = 800;
	double sum = 0.0;
	for (int i = 0; i < nt; i++) {
		double th = (i + 0.5) / nt * 0.5 * PI, c = std::cos(th), s = std::sin(th);
		for (int j = 0; j < np; j++) {
			double ph = (j + 0.5) / np * 2.0 * PI;
			V3 d = Add(Add(Scale(X, s * std::cos(ph)), Scale(Y, s * std::sin(ph))), Scale(N, c));
			if (covered(d)) sum += c * s;
		}
	}
	return sum * (0.5 * PI / nt) * (2.0 * PI / np) / PI;
}

V3 Tilt (double a) { return { std::sin(a), 0.0, std::cos(a) }; }       // from up (z) toward x
V3 ViewDir (double b) { return { 0.0, std::sin(b), std::cos(b) }; }     // from up toward y

}

TEST_CASE("SsgiMath: curves invert", "[ssgimath]")
{
	for (int f = 0; f < 2; f++) {
		double last = -1.0;
		for (int i = 0; i <= 98; i++) {
			double y = i / 100.0, u = TwI(y, f == 1);
			CHECK(std::fabs(TwF(u, f == 1) - y) < 1e-4);
			CHECK(u > last);
			last = u;
		}
	}
	for (int i = 1; i <= 98; i++) { // the filmic root against bisection
		double y = i / 100.0, lo = 0.0, hi = 50.0;
		for (int k = 0; k < 80; k++) { double m = 0.5 * (lo + hi); if (TwF(m, true) < y) lo = m; else hi = m; }
		CHECK(std::fabs(TwI(y, true) - lo) < 1e-4 * std::max(1.0, lo));
	}
	for (int i = 0; i <= 98; i++) {
		double y = i / 100.0;
		CHECK(std::fabs(TmF(TmI(y, false), false) - y) < 1e-6);
		CHECK(std::fabs(TaF(TaI(y, 2.2), 2.2) - y) < 1e-9);
		CHECK(std::fabs(S2L(L2S(y)) - y) < 1e-9);
	}
	CHECK(TmF(0.7, true) == 0.7);
	CHECK(std::fabs(LightFXSq(0.25) - 1.8 * 0.5 / std::sqrt(2.25)) < 1e-12);
}

TEST_CASE("SsgiMath: sender radiance per family", "[ssgimath]")
{
	Curve c;
	c.e = 1.7; c.expo = 0.9 * c.e; c.s = 0.9; c.sTree = 1.1; c.g = 1.8;
	double A = 0.8, Iraw = 0.05, Iobj = 1.0 - std::exp(-c.expo * Iraw), x = 0.2;
	for (int f = 0; f < 2; f++) {
		c.filmic = f == 1;
		CHECK(std::fabs(SenderV(TwF(c.expo * (x * A + Iraw), c.filmic), A, Iraw, Iobj, TERRAIN, LEGACY, c) - c.e * x) < 1e-4); // NewPlanet with air
		CHECK(std::fabs(SenderV(TwF(c.sTree * c.e * x, c.filmic) * A + Iobj, A, Iraw, Iobj, TREE, LEGACY, c) - c.e * x) < 1e-4);
	}
	c.filmic = false;
	CHECK(std::fabs(SenderV(c.e * x * A + Iobj, A, Iraw, Iobj, VESSEL, LEGACY, c) - c.e * x) < 1e-9);    // PBR.fx, Vessel.fx
	CHECK(std::fabs(SenderV(TmF(c.e * x, false) * A + Iobj, A, Iraw, Iobj, VESSEL, METAL, c) - c.e * x) < 1e-6); // Metalness.fx
	CHECK(std::fabs(SenderV(TmF(c.e * L2S(x), false) * A + Iobj, A, Iraw, Iobj, VESSEL, PBR, c) - c.e * L2S(x)) < 1e-6); // PbrMesh, sRGB-encoded
	c.air = false;
	CHECK(std::fabs(SenderV(TaF(c.s * c.e * x, c.g), 1.0, 0.0, 0.0, TERRAIN, LEGACY, c) - c.e * x) < 1e-9); // airless
}

TEST_CASE("SsgiMath: composite is additive, bounded and finite", "[ssgimath]")
{
	Curve c;
	c.e = 1.3; c.expo = 1.1 * c.e; c.s = 1.1; c.sTree = 1.1; c.g = 1.5;
	const int cls[4] = { TERRAIN, TREE, VESSEL, BASE };
	const int fam[4] = { LEGACY, LEGACY, LEGACY, TILE };
	for (int air = 0; air < 2; air++)
		for (int k = 0; k < 4; k++) {
			c.air = air == 1;
			double A = c.air ? 0.85 : 1.0, Iraw = c.air ? 0.03 : 0.0, Iobj = c.air ? 0.02 : 0.0, rho = 0.4;
			for (int i = 0; i <= 20; i++) {
				double y = 0.02 + i * 0.04, a = 0.05, b = 0.08;
				double d1 = CompositeDy(y, A, Iraw, Iobj, rho, a, cls[k], fam[k], c);
				double d2 = CompositeDy(y + d1, A, Iraw, Iobj, rho, b, cls[k], fam[k], c);
				double d12 = CompositeDy(y, A, Iraw, Iobj, rho, a + b, cls[k], fam[k], c);
				CHECK(d1 >= 0.0);
				CHECK(CompositeDy(y, A, Iraw, Iobj, rho, 0.0, cls[k], fam[k], c) == 0.0);
				if (cls[k] == TERRAIN) CHECK(y + d12 <= 1.0 + 1e-12);
				if (y + d12 < 0.95 && !(cls[k] == TERRAIN && !c.air && y > 0.9)) CHECK(std::fabs(d1 + d2 - d12) < 1e-4);
			}
		}
	c.air = true;
	for (int k = 0; k < 4; k++) // bad inputs: I above y, A near 0, y at 1
		for (int f = 0; f < 4; f++) {
			const double ys[3] = { 0.1, 1.0, 1.2 }, As[2] = { 0.01, 1.0 }, Is[2] = { 0.5, 2.0 };
			for (double y : ys) for (double A : As) for (double I : Is)
				for (int fl = 0; fl < 2; fl++) {
					c.filmic = fl == 1;
					double v = SenderV(y, A, I, I, k, f, c), dy = CompositeDy(y, A, I, I, 0.5, 0.3, k, f, c);
					CHECK(std::isfinite(v));
					CHECK(v >= 0.0);
					CHECK(v <= c.uMax);
					CHECK(std::isfinite(dy));
					CHECK(dy >= 0.0);
				}
		}
}

TEST_CASE("SsgiMath: composite follows the lit shaders", "[ssgimath]")
{
	Curve c;
	c.e = 1.4;
	double rho = 0.6, A = 0.9, I = 0.03, E = 0.15;
	// Metalness: x = rho LightFXSq(sun^2 + amb^2), the bounce one more light in the sum
	double S = 0.4 * 0.4 + 0.1 * 0.1, x = rho * LightFXSq(S), x1 = rho * LightFXSq(S + (E / c.e) * (E / c.e));
	double y = TmF(c.e * x, false) * A + I, y1 = TmF(c.e * x1, false) * A + I;
	CHECK(std::fabs(CompositeDy(y, A, 0.0, I, rho, E, VESSEL, METAL, c) - (y1 - y)) < 1e-6);
	// PbrMesh: linear light, shown through sRGB and Tm; the legacy light units are sRGB-encoded
	double cl = 0.05, cl1 = cl + rho * S2L(E / c.e);
	y = TmF(L2S(cl) * c.e, false) * A + I; y1 = TmF(L2S(cl1) * c.e, false) * A + I;
	CHECK(std::fabs(CompositeDy(y, A, 0.0, I, rho, E, VESSEL, PBR, c) - (y1 - y)) < 1e-6);
	// airless terrain: rho LightFX(L), the slope 2 at L = 0
	c.air = false; c.s = 1.0; c.g = 1.0;
	double L0 = 0.0;
	y = rho * LightFX(L0) * c.e;
	CHECK(std::fabs(CompositeDy(y, 1.0, 0.0, 0.0, rho, 0.01, TERRAIN, LEGACY, c) - c.e * rho * LightFX(0.01 / c.e)) < 1e-9);
	L0 = 0.3; y = rho * LightFX(L0) * c.e;
	CHECK(std::fabs(CompositeDy(y, 1.0, 0.0, 0.0, rho, E, TERRAIN, LEGACY, c) - c.e * rho * (LightFX(L0 + E / c.e) - LightFX(L0))) < 1e-9);
	// legacy: the light is clamped at 1 before the texture, so a fully lit white stays white
	c.air = true;
	CHECK(CompositeDy(c.e * rho * A + I, A, 0.0, I, rho, E, VESSEL, LEGACY, c) == 0.0);
}

TEST_CASE("SsgiMath: octahedral normals", "[ssgimath]")
{
	std::mt19937 rng(7);
	std::normal_distribution<double> nd;
	double worst = 0.0, mean = 0.0;
	const int n = 10000;
	for (int i = 0; i < n; i++) {
		V3 v = Unit({ nd(rng), nd(rng), nd(rng) });
		double u, w;
		OctEncode(v, &u, &w);
		double a = std::acos(std::min(1.0, Dot(v, OctDecode(u, w)))) / DEG;
		worst = std::max(worst, a);
		mean += a / n;
	}
	CHECK(worst < 0.25);
	CHECK(mean < 0.1);
}

TEST_CASE("SsgiMath: slice measure", "[ssgimath]")
{
	for (int gd = -80; gd <= 80; gd += 10) {
		double g = gd * DEG, a = g - 0.5 * PI, b = g + 0.5 * PI, M = SliceMass(g);
		CHECK(std::fabs(SliceCdf(a, g)) < 1e-12);
		CHECK(std::fabs(SliceCdf(b, g) - M) < 1e-12);
		double num = 0.0, last = -1.0;
		const int n = 20000;
		for (int i = 0; i < n; i++) {
			double t = a + (i + 0.5) / n * PI;
			num += std::cos(t - g) * std::fabs(std::sin(t)) * PI / n;
			double t1 = a + (i + 1.0) / n * PI, F = SliceCdf(t1, g);
			if (i % 500 == 499) { CHECK(std::fabs(F - num) < 1e-4); CHECK(F >= last); last = F; }
		}
	}
	for (int td = 0; td <= 85; td += 5) { // a fully covered hemisphere gives E = L: (1/pi) integral of |n_p| M over the slice angle is 1
		V3 N = Tilt(td * DEG), V = { 0, 0, 1 };
		double E = Estimate(N, V, 720, [](const V3 &) { return true; });
		CHECK(std::fabs(E - 1.0) < 2e-3);
	}
	CHECK(BitMask(-0.5 * PI, 0.5 * PI, 0.0) == 0xFFFFFFFFu);
	CHECK(BitMask(0.2, 0.2, 0.0) == 0u);
	CHECK(PopCount(BitMask(0.0, 0.5 * PI, 0.0)) == 16);
}

TEST_CASE("SsgiMath: estimator against brute force", "[ssgimath]")
{
	const double walls[3] = { 10.0, 30.0, 60.0 }, views[3] = { 0.0, 45.0, 70.0 };
	for (double al : walls) { // a receiver at the bottom of a pit: E / L = sin^2 of the wall's elevation
		V3 up = { 0, 0, 1 };
		double sa = std::sin(al * DEG);
		auto pit = [&](const V3 &d) { return Dot(d, up) < sa; };
		for (double vb : views) {
			double E = Estimate(up, ViewDir(vb * DEG), 512, pit);
			CHECK(std::fabs(E / (sa * sa) - 1.0) < 0.03);
		}
		for (double vb : views) { // tilted normals against the brute-force integral
			V3 N = Tilt(30.0 * DEG);
			double E = Estimate(N, Unit(Add(ViewDir(vb * DEG), Scale(N, 0.2))), 512, pit), R = Brute(N, pit);
			CHECK(std::fabs(E - R) < 0.03 * std::max(R, 0.05));
		}
	}
	{ // a half-space wall facing the receiver: the wall at x > 0 fills half the hemisphere above the horizon
		V3 N = { 0, 0, 1 };
		auto wall = [&](const V3 &d) { return d.x > 0.0; };
		double E = Estimate(N, ViewDir(30.0 * DEG), 512, wall), R = Brute(N, wall);
		CHECK(std::fabs(R - 0.5) < 2e-3);
		CHECK(std::fabs(E - R) < 0.03 * R);
	}
}

TEST_CASE("SsgiMath: thickness lets sectors behind thin objects through", "[ssgimath]")
{
	V3 P = { 0, 0, -100 }, V = { 0, 0, 1 }, T = { 1, 0, 0 };
	double np, g = SliceGamma({ 0, 0, 1 }, V, T, &np);
	CHECK(std::fabs(g) < 1e-12);
	V3 post = Add(P, { 2, 0, 1 }), wall = Add(P, { 5, 0, 3 });
	unsigned thin = SampleBits(P, post, 0.5, V, T, g), thick = SampleBits(P, post, 1e6, V, T, g);
	unsigned far = SampleBits(P, wall, 1e6, V, T, g);
	int behindThin = PopCount(far & ~thin), behindThick = PopCount(far & ~thick);
	CHECK(PopCount(thin) > 0);
	CHECK(behindThin > behindThick);
	CHECK(behindThick > 0);           // the wall's sectors in front of the post
	CHECK((thick & far) == thick);    // a heightfield post hides every sector of the wall behind it
}

TEST_CASE("SsgiMath: radius and sender falloff", "[ssgimath]")
{
	double R0 = 25.0, k = 0.06, d0 = R0 / k;
	CHECK(std::fabs(Radius(d0 * (1 - 1e-9), R0, k) - Radius(d0 * (1 + 1e-9), R0, k)) < 1e-6);
	double tanY = std::tan(30.0 * DEG), H2 = 540.0;
	for (double d : { 1.0, 20.0, 400.0, 5000.0, 30000.0 }) {
		double px = PixelRadius(Radius(d, R0, k), d, tanY, H2, 4.0, 0.35 * H2);
		CHECK(px >= 4.0);
		CHECK(px <= 0.35 * H2);
		CHECK(std::fabs(PixelRadius(RadiusFromPixels(px, d, tanY, H2), d, tanY, H2, 4.0, 0.35 * H2) - px) < 1e-9);
	}
	double R = 10.0, last = 2.0;
	CHECK(Falloff(0.0, R) == 1.0);
	CHECK(Falloff(0.6 * R, R) == 1.0);
	CHECK(Falloff(R, R) == 0.0);
	for (int i = 0; i <= 100; i++) { double f = Falloff(0.012 * i * R, R); CHECK(f <= last); last = f; }
	// r3-gi2: a hull 17 m away (70 deg aperture) and the sunlit ground 9.5 m in front of it; the screen cap shortens the march to about 8 m, the falloff keeps the world 25 m
	double d = 17.0, t70 = std::tan(35.0 * DEG), Rw = Radius(d, R0, k), Rc = RadiusFromPixels(PixelRadius(Rw, d, t70, H2, 4.0, 0.35 * H2), d, t70, H2);
	CHECK(Rc < 9.0);
	CHECK(Falloff(9.5, Rc) == 0.0);
	CHECK(Falloff(9.5, Rw) == 1.0);
}

TEST_CASE("SsgiMath: probe-lit hulls take only the excess over their probe", "[ssgimath]")
{
	// IrradianceInteg.glsl PSInteg on a uniform cube: sqrt(sum y w glow 0.7 / N), squared by Metalness.fx; the kernel's r uniform, z = sqrt(1 - r^2), w = sqrt(z)
	std::mt19937 rng(7);
	std::uniform_real_distribution<double> U(0.0, 1.0);
	const int n = 200000;
	double y = 0.4, glow = 1.3, sum = 0.0;
	for (int i = 0; i < n; i++) { double r = U(rng); sum += y * std::sqrt(std::sqrt(1.0 - r * r)); }
	double amb = std::pow(std::sqrt(sum * glow * 0.7 / n), 2.0);
	CHECK(std::fabs(amb - ProbeGain(glow) * y) < 0.005 * amb);
	CHECK(std::fabs(ProbeGain(1.0) - 0.6118) < 1e-3);
	double pp[4];
	ProbeParams(0, true, true, 1.0, pp);
	CHECK((pp[0] == 0.0 && pp[1] == 0.0 && pp[2] == 0.0 && pp[3] == 0.0));
	ProbeParams(1, true, true, 1.0, pp);
	CHECK((pp[0] == ProbeGain(1.0) && pp[1] == 1.0 && pp[2] == 1.0 && pp[3] == 0.0));
	ProbeParams(1, false, true, 1.0, pp);
	CHECK((pp[0] == 0.0 && pp[1] == 1.0 && pp[2] == 1.0));
	ProbeParams(2, true, true, 1.0, pp);
	CHECK((pp[0] == ProbeGain(1.0) && pp[3] == 1.0));
	// r3g1: a Metalness hull beside the sunlit apron (display 0.553, v 1.343), sky ambient 0.207, e 1, mode 1
	ProbeParams(1, true, true, 1.0, pp);
	double h = ProbeHeld(VESSEL, METAL, TERRAIN, 0.553, 1.0, pp);
	CHECK(std::fabs(h - ProbeGain(1.0) * 0.553) < 1e-12);
	CHECK(std::fabs(SectorExcess(1.343, 0.207, h) - (1.343 - h)) < 1e-12);
	CHECK(SectorExcess(1.343, 0.207, h) < SectorExcess(1.343, 0.207, 0.0));
	CHECK(ProbeHeld(VESSEL, METAL, TERRAIN, 0.553, 2.0, pp) == 2.0 * h);          // exposed units
	CHECK(ProbeHeld(VESSEL, PBR, TREE, 0.553, 1.0, pp) == 0.553);                // the IBL cube holds the decoded display value
	CHECK(ProbeHeld(VESSEL, METAL, BASE, 0.553, 1.0, pp) == 0.0);                // mode 1 draws no bases
	CHECK(ProbeHeld(VESSEL, METAL, VESSEL, 0.9, 1.0, pp) == 0.0);                // nor vessels
	CHECK(ProbeHeld(VESSEL, LEGACY, TERRAIN, 0.553, 1.0, pp) == 0.0);            // legacy hulls have no probe
	CHECK(ProbeHeld(BASE, METAL, TERRAIN, 0.553, 1.0, pp) == 0.0);               // nor do buildings
	ProbeParams(2, true, true, 1.0, pp);
	CHECK(ProbeHeld(VESSEL, METAL, BASE, 0.553, 1.0, pp) > 0.0);                 // mode 2 draws the bases
	CHECK(SectorExcess(0.3, 0.207, 0.5) == 0.0);
	CHECK(SectorExcess(0.9, 0.207, 0.1) == 0.9 - 0.207);
}

TEST_CASE("SsgiMath: mesh ambient as GetObjectAtmoParams", "[ssgimath]")
{
	Atmo a;
	a.cfg[0] = a.cfg[1] = a.cfg[2] = 0.02;
	V3 sun = Unit({ 1, 0, 1 });
	CHECK(std::fabs(RayLength(-1.0, a.PlanetRad, a.AtmoRad) - (a.AtmoRad - a.PlanetRad)) < 1e-6); // cos -1: straight up
	for (double cd : { 0.5, 0.0, -0.3, -1.0 }) // Gauss7 (Scatter.glsl's nodes and weights) against a fine integral of the density, 1 km paths
		for (double alt : { 0.0, 2000.0, 15000.0 }) {
			double r = a.PlanetRad + alt, d = 1000.0, g[2], ref[2] = { 0, 0 };
			Gauss7(cd, r, d, a.iH, a, g);
			const int n = 20000;
			for (int i = 0; i < n; i++) {
				double t = (i + 0.5) / n * d, h = std::sqrt(r * r + t * t - 2.0 * r * t * cd) - a.PlanetRad;
				for (int k = 0; k < 2; k++) ref[k] += std::exp(-h * a.iH[k]) * d / n;
			}
			CHECK(std::fabs(g[0] / ref[0] - 1.0) < 1e-3); // Rayleigh, 8 km scale height
			CHECK(std::fabs(g[1] / ref[1] - 1.0) < 1e-2); // Mie, 1.2 km
		}
	double out[3];
	MeshAmbient(a, { 0, 0, a.PlanetRad + 0.6 * a.AtmoAlt }, sun, out); // in space: the config level
	CHECK((out[0] == 0.02 && out[1] == 0.02 && out[2] == 0.02));
	MeshAmbient(a, Scale(Unit({ -1, 0, -1 }), a.PlanetRad), sun, out); // deep night: the twilight weight is 0
	CHECK((std::fabs(out[0] - 0.02) < 1e-12 && std::fabs(out[2] - 0.02) < 1e-12));
	double lo[3], hi[3];
	V3 up = Unit({ 0.2, 0, 1 });
	MeshAmbient(a, Scale(up, a.PlanetRad), sun, lo);
	MeshAmbient(a, Scale(up, a.PlanetRad + 3000.0), sun, hi);
	CHECK(lo[1] > hi[1]);   // thinner air above
	CHECK(lo[2] > lo[0]);   // the sky colour (cAmbient blue)
	a.twBld = 0.0;
	MeshAmbient(a, Scale(up, a.PlanetRad), sun, lo);
	CHECK(std::fabs(lo[1] - 0.02) < 1e-12);
}
