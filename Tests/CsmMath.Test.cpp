// not upstream: cascade fitting and texel snapping for the scene shadows (OVP/VulkanClient/CsmMath.cpp, design-scene-shadows 17)

#include <catch2/catch_test_macros.hpp>
#include "CsmMath.h"
#include <cmath>
#include <vector>

using namespace CsmMath;

namespace {

M3 RotY (double a) { M3 m = { { { std::cos (a), 0.0, std::sin (a) }, { 0.0, 1.0, 0.0 }, { -std::sin (a), 0.0, std::cos (a) } } }; return m; }
M3 RotX (double a) { M3 m = { { { 1.0, 0.0, 0.0 }, { 0.0, std::cos (a), -std::sin (a) }, { 0.0, std::sin (a), std::cos (a) } } }; return m; }
M3 MulMM (const M3 &a, const M3 &b)
{
	M3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
	return r;
}
V3 Up (double lat, double lng) { return { std::cos (lat) * std::cos (lng), std::sin (lat), std::cos (lat) * std::sin (lng) }; }
V3 East (double lng) { return { -std::sin (lng), 0.0, std::cos (lng) }; }

// an Earth-like body round a sun at the origin: 30 km/s on its orbit, spinning, axis tilted
struct World {
	double Re = 6371e3, au = 1.496e11, Om = 30e3 / 1.496e11, w = 7.292e-5, tilt = 23.44 * Pi / 180.0, t0 = 1234.5;
	V3 P (double t) const { double a = Om * (t + t0); return { au * std::cos (a), 0.0, au * std::sin (a) }; }
	M3 R (double t) const { return MulMM (RotX (tilt), RotY (w * (t + t0))); }
};

// the module's per-frame light frame: frozen until the sun turns 1/2048 rad (Rev 2 MUST 1)
struct Frame {
	bool have = false, alt = false;
	V3 Lref{ 0, 0, 0 };
	M3 B;
	bool Update (const V3 &Lp)
	{
		if (have && Angle (Lp, Lref) <= SunTurn) return false;
		Lref = Lp; B = Basis (Lp, alt); have = true;
		return true;
	}
};

// atlas u, v of a camera-relative point through the receiver matrix rounded to float, as the GPU gets both
void UvFloat (int i, const Xf &x, const V3 &v, double &u, double &vv)
{
	double M[4][4];
	ReceiverMatrix (i, x, M);
	float f[4][4], p[3] = { float(v.x), float(v.y), float(v.z) };
	for (int r = 0; r < 4; r++) for (int c = 0; c < 4; c++) f[r][c] = float(M[r][c]);
	u = double(p[0] * f[0][0] + p[1] * f[1][0] + p[2] * f[2][0] + f[3][0]);
	vv = double(p[0] * f[0][1] + p[1] * f[1][1] + p[2] * f[2][1] + f[3][1]);
}

struct Track { double tx, ty; bool rebase; };

// a static ground point g (planet frame) seen from a camera path cl(t) (planet frame): atlas texel coordinates plus the box's own texel shift
std::vector<Track> Follow (const World &W, int i, int frames, double dt, V3 (*cl) (double), const V3 &g)
{
	Frame F;
	std::vector<Track> out;
	for (int n = 0; n < frames; n++) {
		double t = n * dt;
		M3 Rp = W.R (t);
		V3 P = W.P (t), cp = cl (t);
		V3 C = Add (P, MulM (Rp, cp));
		V3 Lp = MulMT (Rp, Unit (Mul (C, -1.0)));
		bool rb = F.Update (Lp);
		Box b = Fit (i, MulM (F.B, cp));
		Xf x = Transform (F.B, Rp, cp, b);
		V3 v = Sub (Add (P, MulM (Rp, g)), C); // exact camera-relative position
		double u, vv;
		UvFloat (i, x, v, u, vv);
		double tt = Texel (i);
		out.push_back ({ u * AW + b.mx / tt, vv * AH - b.my / tt, rb });
	}
	return out;
}

const double LAT = 28.5 * Pi / 180.0, LNG = -80.6 * Pi / 180.0;
V3 Plane (double t) { return Add (Mul (Up (LAT, LNG), 6371e3 + 2000.0), Mul (East (LNG), 250.0 * t)); } // 2 km up, 250 m/s
V3 Car (double t) { return Add (Mul (Up (LAT, LNG), 6371e3 + 2.0), Mul (East (LNG), 10.0 * t)); }       // on the ground, 10 m/s
V3 Still (double) { return Mul (Up (LAT, LNG), 6371e3 + 2.0); }

}

TEST_CASE("CsmMath: version", "[csmmath]")
{
	CHECK(Version() == 3);
}

TEST_CASE("CsmMath: light basis orthonormal, right-handed, z away from the sun", "[csmmath]")
{
	for (int k = 0; k < 200; k++) {
		double a = 0.37 * k, e = std::sin (0.11 * k) * 1.4;
		V3 L = { std::cos (e) * std::cos (a), std::sin (e), std::cos (e) * std::sin (a) };
		bool alt = false;
		M3 B = Basis (L, alt);
		V3 X = { B.m[0][0], B.m[0][1], B.m[0][2] }, Y = { B.m[1][0], B.m[1][1], B.m[1][2] }, Z = { B.m[2][0], B.m[2][1], B.m[2][2] };
		CHECK(std::fabs (Len (X) - 1.0) < 1e-12);
		CHECK(std::fabs (Len (Y) - 1.0) < 1e-12);
		CHECK(std::fabs (Dot (X, Y)) < 1e-12);
		CHECK(std::fabs (Dot (X, Z)) < 1e-12);
		CHECK(Len (Sub (Cross (X, Y), Z)) < 1e-12);
		CHECK(Len (Add (Z, L)) < 1e-12);
	}
}

TEST_CASE("CsmMath: basis continuous, pole fallback with hysteresis", "[csmmath]")
{
	bool alt = false;
	M3 prev = Basis (Unit (V3{ 1.0, 0.2, 0.3 }), alt);
	for (int k = 1; k <= 1000; k++) {
		double a = 0.001 * k;
		V3 L = Unit (V3{ std::cos (a), 0.2, 0.3 + std::sin (a) });
		M3 B = Basis (L, alt);
		double d = 0;
		for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) d = std::max (d, std::fabs (B.m[r][c] - prev.m[r][c]));
		CHECK(d < 0.01);
		CHECK_FALSE(alt);
		prev = B;
	}
	alt = false;
	Basis (Unit (V3{ 0.03, 1.0, 0.0 }), alt); // 0.03 rad from the pole
	CHECK(alt);
	Basis (Unit (V3{ 0.07, 1.0, 0.0 }), alt); // inside the hysteresis band: kept
	CHECK(alt);
	Basis (Unit (V3{ 0.2, 1.0, 0.0 }), alt);
	CHECK_FALSE(alt);
	Basis (Unit (V3{ 0.07, 1.0, 0.0 }), alt);
	CHECK_FALSE(alt);
}

TEST_CASE("CsmMath: snapped centre within half a texel", "[csmmath]")
{
	for (int i = 0; i < NC; i++)
		for (int k = 0; k < 500; k++) {
			V3 q = { 1e5 * std::sin (0.7 * k + i), -3e4 * std::cos (1.3 * k), 2e6 + k };
			Box b = Fit (i, q);
			double t = Texel (i);
			CHECK(std::fabs (q.x - b.mx) <= 0.5 * t + 1e-9);
			CHECK(std::fabs (q.y - b.my) <= 0.5 * t + 1e-9);
			CHECK(std::fabs (b.mx / t - std::round (b.mx / t)) < 1e-6);
			CHECK(std::fabs (b.z0 - (q.z - Radius[i] - Zext)) < 1e-6);
		}
}

TEST_CASE("CsmMath: static ground point stable from a flying camera (float matrices)", "[csmmath]")
{
	World W;
	V3 g = Add (Mul (Up (LAT + 3e-4, LNG + 2e-4), 6371e3), Mul (East (LNG), 0.0)); // ~2 km off the track
	for (int i : { 2, 3 }) {
		std::vector<Track> tr = Follow (W, i, 120, 1.0 / 60.0, Plane, g);
		for (size_t n = 0; n < tr.size(); n++) {
			const Track &k = tr[n];
			CHECK(k.rebase == (n == 0)); // the sun turns less than 1/2048 rad in 2 s
			CHECK(std::fabs (k.tx - tr[0].tx) < 1e-3);
			CHECK(std::fabs (k.ty - tr[0].ty) < 1e-3);
		}
	}
}

TEST_CASE("CsmMath: static ground point stable from a driving camera (C0, C1)", "[csmmath]")
{
	World W;
	V3 g = Add (Mul (Up (LAT, LNG), 6371e3), Mul (East (LNG), 20.0));
	for (int i : { 0, 1 }) {
		std::vector<Track> tr = Follow (W, i, 120, 1.0 / 60.0, Car, g);
		for (const Track &k : tr) {
			CHECK(std::fabs (k.tx - tr[0].tx) < 1e-3);
			CHECK(std::fabs (k.ty - tr[0].ty) < 1e-3);
		}
	}
}

TEST_CASE("CsmMath: spinning planet, still camera: uv kept except on re-base frames", "[csmmath]")
{
	World W;
	V3 g = Add (Mul (Up (LAT, LNG), 6371e3), Mul (East (LNG), 25.0));
	for (int i : { 0, 1, 2 }) {
		std::vector<Track> tr = Follow (W, i, 600, 1.0 / 60.0, Still, g);
		int rebases = 0;
		for (size_t n = 1; n < tr.size(); n++) {
			if (tr[n].rebase) { rebases++; continue; }
			CHECK(std::fabs (tr[n].tx - tr[n - 1].tx) < 1e-3);
			CHECK(std::fabs (tr[n].ty - tr[n - 1].ty) < 1e-3);
		}
		CHECK(rebases >= 1);
		CHECK(rebases <= 2);
	}
}

TEST_CASE("CsmMath: a kept tile read 3 frames later gives the static point its old uv", "[csmmath]")
{
	World W;
	bool alt = false;
	for (int i = 0; i < NC; i++) {
		V3 (*path) (double) = i < 2 ? Car : Plane; // each cascade with points inside its box
		V3 g = i < 2 ? Add (Mul (Up (LAT, LNG), 6371e3), Mul (East (LNG), 30.0)) : Mul (Up (LAT + 2e-4, LNG), 6371e3);
		double t0 = 0.0, t3 = 3.0 / 60.0;
		M3 R0 = W.R (t0), R3 = W.R (t3);
		V3 P0 = W.P (t0), P3 = W.P (t3), c0 = path (t0), c3 = path (t3);
		V3 C0 = Add (P0, MulM (R0, c0)), C3 = Add (P3, MulM (R3, c3));
		M3 B = Basis (MulMT (R0, Unit (Mul (C0, -1.0))), alt);
		Box b = Fit (i, MulM (B, c0));
		double u0, v0, u3, v3;
		UvFloat (i, Transform (B, R0, c0, b), Sub (Add (P0, MulM (R0, g)), C0), u0, v0);
		UvFloat (i, Transform (B, R3, c3, b), Sub (Add (P3, MulM (R3, g)), C3), u3, v3);
		CHECK(std::fabs (u3 - u0) * AW < 1e-3);
		CHECK(std::fabs (v3 - v0) * AH < 1e-3);
	}
}

TEST_CASE("CsmMath: caster and receiver matrices agree and stay in their tile", "[csmmath]")
{
	bool alt = false;
	M3 B = Basis (Unit (V3{ 0.3, 0.5, -0.8 }), alt), Rp = RotY (0.4);
	V3 cp = { 1e6, 6e6, -2e5 };
	for (int i = 0; i < NC; i++) {
		Box b = Fit (i, MulM (B, cp));
		Xf x = Transform (B, Rp, cp, b);
		double Mc[4][4], Mr[4][4];
		CasterMatrix (i, x, Mc);
		ReceiverMatrix (i, x, Mr);
		for (int k = 0; k < 50; k++) {
			double r = Radius[i];
			V3 v = Mul (Unit (V3{ std::sin (k * 1.1), std::cos (k * 0.7), std::sin (k * 0.3) + 0.01 }), 0.9 * r * (0.1 + 0.9 * std::fabs (std::sin (k * 2.3))));
			double c[3], q[3];
			for (int j = 0; j < 3; j++) {
				c[j] = v.x * Mc[0][j] + v.y * Mc[1][j] + v.z * Mc[2][j] + Mc[3][j];
				q[j] = v.x * Mr[0][j] + v.y * Mr[1][j] + v.z * Mr[2][j] + Mr[3][j];
			}
			CHECK(std::fabs ((c[0] + 1.0) * 0.5 - q[0]) < 1e-12); // viewport x
			CHECK(std::fabs ((1.0 - c[1]) * 0.5 - q[1]) < 1e-12); // y down
			CHECK(std::fabs (c[2] - q[2]) < 1e-12);
			CHECK(q[0] >= double(Col (i) * N) / AW);
			CHECK(q[0] <= double(Col (i) * N + N) / AW);
			CHECK(q[1] >= double(Row (i) * N) / AH);
			CHECK(q[1] <= double(Row (i) * N + N) / AH);
			CHECK(q[2] > 0.0);
			CHECK(q[2] < 1.0);
		}
	}
}

TEST_CASE("CsmMath: target tile round its casters, quarter-octave texels, off when no finer than C0", "[csmmath]")
{
	std::vector<V3> p; // a DG-sized box in planet-frame light space
	for (int k = 0; k < 8; k++) p.push_back ({ 6.4e6 + ((k & 1) ? 9.0 : -9.0) + 0.37, -1.2e6 + ((k & 2) ? 8.5 : -8.5), 3.1e5 + ((k & 4) ? 4.0 : -2.0) });
	Target f;
	REQUIRE(FitTarget (p.data (), 8, 0.0, 0.0, f));
	CHECK(std::fabs (std::log2 (f.t) * 4.0 - std::round (std::log2 (f.t) * 4.0)) < 1e-9);
	CHECK(f.t < 0.25 * Texel (0)); // the contact shadow 4x finer than C0
	CHECK(f.r == 0.5 * N * f.t);
	CHECK(std::fabs (f.box.mx / f.t - std::round (f.box.mx / f.t)) < 1e-6);
	CHECK(std::fabs (f.box.my / f.t - std::round (f.box.my / f.t)) < 1e-6);
	for (const V3 &q : p) {
		CHECK(std::fabs (q.x - f.box.mx) + (TgtMargin - 0.5) * f.t <= f.r);
		CHECK(std::fabs (q.y - f.box.my) + (TgtMargin - 0.5) * f.t <= f.r);
		CHECK((q.z - f.box.z0) / f.D > 0.0);
		CHECK((q.z - f.box.z0) / f.D < 1.0);
	}
	Target g; // a wide penumbra sets the texel: at most 4 texels of PCF radius
	REQUIRE(FitTarget (p.data (), 8, 0.3, 0.0, g));
	CHECK(0.3 / (2.0 * g.t) <= TgtKernel + 1e-9);
	CHECK(g.t < 0.3 / (2.0 * TgtKernel) * std::exp2 (0.25));
	CHECK_FALSE(FitTarget (p.data (), 8, 2.0 * TgtKernel * Texel (0) * 1.1, 0.0, g)); // no finer than C0
	std::vector<V3> big = p;
	big[0].x -= 400.0;
	CHECK_FALSE(FitTarget (big.data (), 8, 0.0, 0.0, g)); // a large cluster stays in the cascades
	double T = std::exp2 (-4.5), Tc = std::exp2 (-4.25);
	REQUIRE(FitTarget (p.data (), 8, 2.0 * TgtKernel * 0.99 * T, Tc, g));
	CHECK(g.t == Tc); // just under a step: the coarser one kept
	REQUIRE(FitTarget (p.data (), 8, 2.0 * TgtKernel * 0.96 * T, Tc, g));
	CHECK(std::fabs (g.t - T) < 1e-12);
	double Tm = std::exp2 (-3.75); // the coarsest step under C0's texel
	CHECK(Tm <= Texel (0));
	CHECK(Tm * std::exp2 (0.25) > Texel (0));
	CHECK_FALSE(FitTarget (p.data (), 8, 2.0 * TgtKernel * 0.99 * Tm, 0.0, g)); // back on only below 0.97 of it
	CHECK(FitTarget (p.data (), 8, 2.0 * TgtKernel * 0.99 * Tm, Tm, g));
	CHECK(FitTarget (p.data (), 8, 2.0 * TgtKernel * 0.96 * Tm, 0.0, g));
	bool alt = false; // their matrices land in slots (2, 0) and (2, 1)
	M3 B = Basis (Unit (V3{ 0.3, 0.5, -0.8 }), alt), Rp = RotY (0.4);
	V3 cp = { 1e6, 6e6, -2e5 };
	Xf x0 = Transform (B, Rp, cp, Box{ 0.0, 0.0, 0.0 });
	std::vector<V3> pl;
	for (int k = 0; k < 8; k++) pl.push_back (Apply (x0, V3{ ((k & 1) ? 9.0 : -9.0) + 20.0, (k & 2) ? 3.0 : -1.0, ((k & 4) ? 8.0 : -8.0) - 5.0 }));
	REQUIRE(FitTarget (pl.data (), 8, 0.05, 0.0, f));
	Xf xt = Transform (B, Rp, cp, f.box);
	CHECK(NT == FT + NTgt);
	for (int ti = FT; ti < NT; ti++) {
		CHECK(Col (ti) == 2);
		CHECK(Row (ti) == ti - FT);
		double Mc[4][4], Mr[4][4];
		CasterMatrix (ti, f.r, f.D, xt, Mc);
		ReceiverMatrix (ti, f.r, f.D, xt, Mr);
		for (int k = 0; k < 8; k++) {
			V3 v = { ((k & 1) ? 9.0 : -9.0) + 20.0, (k & 2) ? 3.0 : -1.0, ((k & 4) ? 8.0 : -8.0) - 5.0 };
			double c[3], q[3];
			for (int j = 0; j < 3; j++) {
				c[j] = v.x * Mc[0][j] + v.y * Mc[1][j] + v.z * Mc[2][j] + Mc[3][j];
				q[j] = v.x * Mr[0][j] + v.y * Mr[1][j] + v.z * Mr[2][j] + Mr[3][j];
			}
			CHECK(std::fabs ((c[0] + 1.0) * 0.5 - q[0]) < 1e-12);
			CHECK(std::fabs ((1.0 - c[1]) * 0.5 - q[1]) < 1e-12);
			CHECK(q[0] * AW >= 2.0 * N + TgtMargin - 0.5);
			CHECK(q[0] * AW <= 3.0 * N - TgtMargin + 0.5);
			CHECK(q[1] * AH >= Row (ti) * N + TgtMargin - 0.5);
			CHECK(q[1] * AH <= Row (ti) * N + N - TgtMargin + 0.5);
			CHECK(q[2] > 0.0);
			CHECK(q[2] < 1.0);
		}
	}
}

TEST_CASE("CsmMath: depth precision and box margins", "[csmmath]")
{
	CHECK(std::ldexp (1.0, -24) * Depth (0) < 1.3e-3); // D32 near 1 over C0's range
	for (int i = 0; i < NC; i++) {
		double t = Texel (i), r = Radius[i];
		CHECK(SelectK * r + MoveK * r + 5.5 * t <= r); // select radius, camera drift, 4 texel kernel and the gather
		CHECK(BandK < SelectK);
	}
	CHECK(SelectK * Radius[NC - 1] > FadeEnd);
	CHECK(FadeEnd + MoveK * Radius[NC - 1] + 5.5 * Texel (NC - 1) <= Radius[NC - 1]);
}

TEST_CASE("CsmMath: sphere cull", "[csmmath]")
{
	bool alt = false;
	M3 B = Basis (Unit (V3{ 0.0, 0.6, 0.8 }), alt), Rp = RotY (0.0);
	V3 cp = { 0.0, 0.0, 0.0 };
	for (int i = 0; i < NC; i++) {
		Box b = Fit (i, MulM (B, cp));
		Xf x = Transform (B, Rp, cp, b);
		double r = Radius[i];
		V3 X = { B.m[0][0], B.m[0][1], B.m[0][2] }, Y = { B.m[1][0], B.m[1][1], B.m[1][2] }, L = { -B.m[2][0], -B.m[2][1], -B.m[2][2] };
		CHECK(CullSphere (i, x, V3{ 0, 0, 0 }, 1.0));
		CHECK(CullSphere (i, x, Mul (X, r + 5.0), 10.0));   // straddles +x
		CHECK_FALSE(CullSphere (i, x, Mul (X, r + 20.0), 10.0));
		CHECK(CullSphere (i, x, Mul (Y, -(r + 5.0)), 10.0)); // straddles -y
		CHECK_FALSE(CullSphere (i, x, Mul (Y, -(r + 20.0)), 10.0));
		CHECK(CullSphere (i, x, Mul (L, 0.9 * Zext), 10.0)); // toward the sun within 20 km
		CHECK_FALSE(CullSphere (i, x, Mul (L, r + Zext + 50.0), 10.0));
		CHECK_FALSE(CullSphere (i, x, Mul (L, -(r + 50.0)), 10.0)); // behind the receivers
		CHECK(CullSphere (i, x, Mul (L, -(r + 5.0)), 10.0));
	}
	CHECK_FALSE(CastsIn (3, 18.0)); // a DG in C3
	CHECK(CastsIn (2, 18.0));
}

TEST_CASE("CsmMath: refresh schedule", "[csmmath]")
{
	int n[4] = { 0, 0, 0, 0 };
	for (unsigned f = 0; f < 64; f++) {
		int k = 0;
		for (int i = 0; i < NC; i++) if (Due (i, f)) { n[i]++; k++; }
		CHECK(k <= 3);
		CHECK(k >= 2);
	}
	CHECK(n[0] == 64);
	CHECK(n[1] == 64);
	CHECK(n[2] == 32);
	CHECK(n[3] == 16);
	V3 q = { 10.0, 20.0, 30.0 };
	for (int i = 0; i < NC; i++) {
		double k = MoveK * Radius[i];
		CHECK_FALSE(MovedOut (i, q, Add (q, V3{ 0.9 * k, -0.9 * k, 0.9 * k })));
		CHECK(MovedOut (i, q, Add (q, V3{ 1.1 * k, 0, 0 })));
		CHECK(MovedOut (i, q, Add (q, V3{ 0, 0, -1.1 * k })));
	}
}

TEST_CASE("CsmMath: receiver cascade selection and fade", "[csmmath]")
{
	double band = 0;
	CHECK(Select (10.0, &band) == 0);
	CHECK(band == 0.0);
	CHECK(Select (0.93 * Radius[0], &band) == 0);
	CHECK(band > 0.9);
	CHECK(Select (0.95 * Radius[0], &band) == 1);
	CHECK(band == 0.0);
	CHECK(Select (40e3, &band) == 3);
	CHECK(Select (43e3, &band) == -1);
	CHECK(FadeWeight (10e3) == 1.0);
	CHECK(FadeWeight (30e3) == 1.0);
	CHECK(FadeWeight (42e3) == 0.0);
	CHECK(FadeWeight (50e3) == 0.0);
	CHECK(FadeWeight (36e3) > 0.0);
	CHECK(FadeWeight (36e3) < 1.0);
}

TEST_CASE("CsmMath: penumbra and filter radius monotonic, hand-over continuous", "[csmmath]")
{
	double tan2w = 0.0093;
	double prevW = -1, prevR = -1;
	for (double dz = 0; dz < 5e3; dz += 1.0) {
		double w = Penumbra (dz, tan2w);
		CHECK(w >= prevW);
		double R = FilterRadius (w, Texel (0));
		CHECK(R >= prevR);
		prevW = w; prevR = R;
	}
	for (int i = 0; i < NC - 1; i++) {
		double t = Texel (i);
		CHECK(HandOver (8.0 * t, t) == 0.0);  // R = 4 texels: still all this cascade
		CHECK(HandOver (8.0 * t + 1e-6, t) > 0.0);
		CHECK(HandOver (16.0 * t, t) == 1.0);
		double prev = EffectiveRadius (i, 0.0), maxJump = 0;
		const double wEnd = 40.0 * Texel (std::min (i + 1, NC - 1)); // across the hand-overs into the next two cascades
		for (double w = 0; w < wEnd; w += 0.001 * t) {
			double e = EffectiveRadius (i, w);
			maxJump = std::max (maxJump, std::fabs (e - prev));
			prev = e;
		}
		CHECK(maxJump < 0.05 * t); // no step along a long shadow edge
		CHECK(EffectiveRadius (i, wEnd) <= 4.0 * Texel (NC - 1) + 1e-9); // never wider than 4 texels of the coarsest cascade
	}
}

TEST_CASE("CsmMath: bias and normal offset bounded", "[csmmath]")
{
	for (double th = 0; th <= 89.9; th += 0.1) {
		double c = std::cos (th * Pi / 180.0);
		for (int i = 0; i < NC; i++) {
			double t = Texel (i);
			CHECK(NormalOffset (t, c) <= t + 1e-12);
			CHECK(NormalOffset (t, c) >= 0.0);
		}
		CHECK(PlaneSlope (c) <= 5.0);
		CHECK(PlaneSlope (c) >= 0.0);
		double tanT = std::tan (th * Pi / 180.0);
		if (tanT <= 50.0) CHECK(PlaneSlope (c) + CapBias (1.0, c) / 1.5 >= tanT - 1e-6); // the cap's missing slope is back as bias, one texel along the light
		if (c >= 0.2) CHECK(CapBias (Texel (0), c) == 0.0);
	}
	CHECK(ConstBias (Texel (0), 3.0) == 3.0 * ConstBias (Texel (0), 1.0));
	for (int i = 1; i < NC; i++) CHECK(TerrainBias[i] > TerrainBias[i - 1]);
}

TEST_CASE("CsmMath: stable noise index follows the ground, not the snap", "[csmmath]")
{
	V3 g = { 37.3, -12.1, 5.0 }; // a static point in light space
	for (int i = 0; i < NC; i++) {
		double t = Texel (i), first = -1;
		for (int k = 0; k < 300; k++) {
			V3 q = { -3.0 * Radius[i] + 0.021 * Radius[i] * k, 1.7 + 0.013 * Radius[i] * k, 0.0 }; // camera crossing many snaps
			Box b = Fit (i, q);
			double ox, oy;
			WorldTexel (i, b, ox, oy);
			double col = std::floor ((g.x - (b.mx - Radius[i])) / t);
			if (col < 0 || col >= N) continue;
			double row = std::floor ((b.my + Radius[i] - g.y) / t);
			double wx = std::fmod (ox + col, double(HashP)), wy = std::fmod (oy - row + HashP, double(HashP));
			double id = wx * HashP + wy;
			if (first < 0) first = id;
			CHECK(id == first);
		}
		CHECK(first >= 0);
	}
}

TEST_CASE("CsmMath: best-candidate disk, the shader's table", "[csmmath]")
{
	double d[8][2];
	Disk (8, d);
	double mind = 1e9;
	for (int i = 0; i < 8; i++) {
		CHECK(std::hypot (d[i][0], d[i][1]) <= 1.0);
		CHECK(std::fabs (d[i][0] - Disk8[i][0]) < 1e-8);
		CHECK(std::fabs (d[i][1] - Disk8[i][1]) < 1e-8);
		for (int j = 0; j < i; j++) mind = std::min (mind, std::hypot (d[i][0] - d[j][0], d[i][1] - d[j][1]));
	}
	CHECK(mind >= 0.45);
	for (int i = 0; i < 4; i++) CHECK(std::hypot (Disk8[i][0], Disk8[i][1]) > 0.8); // the first four, the blocker taps, lie on the rim
}

TEST_CASE("CsmMath: stencil shadows replaced only where a cascade draws the caster", "[csmmath]")
{
	CHECK(Covers (20.0, 18.0));         // a DG near the camera
	CHECK(Covers (3000.0, 18.0));       // C2
	CHECK_FALSE(Covers (8000.0, 18.0)); // C3 skips it: the stencil stays
	CHECK(Covers (8000.0, 60.0));       // a large structure in C3
	CHECK_FALSE(Covers (31e3, 200.0));  // past 30 km
}
