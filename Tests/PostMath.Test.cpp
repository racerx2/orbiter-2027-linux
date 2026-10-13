// not upstream: TAA maths (OVP/VulkanClient/PostMath.cpp)

#include <catch2/catch_test_macros.hpp>
#include "PostMath.h"
#include <cmath>

using namespace PostMath;

namespace {

const double W = 1920.0, H = 1080.0, TANAP = std::tan(0.35), ASPECT = H / W, DT = 1.0 / 60.0;

Mat3 Columns (const Vec3 &x, const Vec3 &y, const Vec3 &z)
{
	return { { { x.x, y.x, z.x }, { x.y, y.y, z.y }, { x.z, y.z, z.z } } };
}

Vec3 Col (const Mat3 &m, int j) { return { m.m[0][j], m.m[1][j], m.m[2][j] }; }

Vec3 Cross (const Vec3 &a, const Vec3 &b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }

Mat3 LookAt (const Vec3 &from, const Vec3 &to, const Vec3 &up) // camera axes X right, Y up, Z forward (D3D LH)
{
	Vec3 z = Unit(Sub(to, from)), x = Unit(Cross(up, z)), y = Cross(z, x);
	return Columns(x, y, z);
}

struct Cam { Vec3 pos; Mat3 rot; };

// p now (camera-relative) to its pixel under jitter j (px, py), the ray rebuilt from that pixel as the shader does, back to a frame ago (q)
void Reproject (const Cam &now, const Vec3 &p, double w, const Mat4 &M, double jx, double jy, double *qx, double *qy, double *ppx = nullptr, double *ppy = nullptr)
{
	double j31 = 2.0 * jx / W, j32 = -2.0 * jy / H;
	Mat4 VPj = Mul(View(now.rot), Proj(TANAP, ASPECT, 0.5, 1e8, j31, j32));
	double px, py;
	REQUIRE(ToPixel(VPj, p, w, W, H, &px, &py));
	if (ppx) { *ppx = px; *ppy = py; }
	double tx = TANAP / ASPECT, ty = TANAP;
	Vec3 X = Col(now.rot, 0), Y = Col(now.rot, 1), Z = Col(now.rot, 2);
	Vec3 d = PixelRay(px / W, py / H, X, Y, JitterAxis(X, Y, Z, j31, j32, tx, ty), tx, ty);
	Vec3 P = d;
	if (w > 0.0) {
		float dist = float(std::sqrt(Dot(p, p))); // R32F
		P = Scale(d, dist);
		P = { double(float(P.x)), double(float(P.y)), double(float(P.z)) };
	}
	REQUIRE(ToPixel(M, P, w, W, H, qx, qy));
}

}

TEST_CASE("PostMath: Halton base 2 and 3", "[postmath]")
{
	CHECK(std::fabs(Halton(1, 2) - 0.5) < 1e-12);
	CHECK(std::fabs(Halton(2, 2) - 0.25) < 1e-12);
	CHECK(std::fabs(Halton(1, 3) - 1.0 / 3.0) < 1e-12);
	CHECK(std::fabs(Halton(4, 3) - 4.0 / 9.0) < 1e-12);
}

TEST_CASE("PostMath: jitter within half a pixel, centred over 8 phases", "[postmath]")
{
	double sx = 0.0, sy = 0.0;
	for (unsigned n = 0; n < 8; n++) {
		double jx, jy;
		Jitter(n, &jx, &jy);
		CHECK(std::fabs(jx) <= 0.5);
		CHECK(std::fabs(jy) <= 0.5);
		sx += jx; sy += jy;
		double kx, ky;
		Jitter(n + 8, &kx, &ky);
		CHECK(kx == jx);
		CHECK(ky == jy);
	}
	CHECK(std::fabs(sx / 8.0) < 0.07);
	CHECK(std::fabs(sy / 8.0) < 0.07);
}

TEST_CASE("PostMath: a static ground point maps back to its pixel", "[postmath]")
{
	const double Rp = 6.371e6, om = 7.292e-5;
	Vec3 oNow = { 1.4e11, 1.2e7, 5.1e10 }, vel = { 2.98e4, 0.0, -1.1e4 }, oPrev = Sub(oNow, Scale(vel, DT));
	double th = 1.234;
	Mat3 Rnow = Mul(RotX(0.41), RotY(th)), Rprev = Mul(RotX(0.41), RotY(th - om * DT));
	double lat = 0.8, lng = 0.2;
	Vec3 up = { std::cos(lat) * std::cos(lng), std::sin(lat), std::cos(lat) * std::sin(lng) };
	Vec3 east = Unit(Cross(Vec3{ 0, 1, 0 }, up)), north = Cross(up, east);
	Vec3 l = Scale(up, Rp); // the ground point, planet frame

	for (int k = 0; k < 4; k++) {
		// camera in the planet frame: 2 km up, 3 km off, flying at 250 m/s, turning
		Vec3 lcNow = Add(Add(l, Scale(up, 2000.0)), Add(Scale(east, -3000.0 + 700.0 * k), Scale(north, 400.0 * k)));
		Vec3 lcPrev = Sub(lcNow, Scale(Add(Scale(east, 230.0), Scale(up, 40.0 * (k - 1.5))), DT));
		Vec3 lt = Add(l, Scale(north, 150.0 * k));
		Cam now, prev;
		now.pos = Add(Mul(Rnow, lcNow), oNow);
		prev.pos = Add(Mul(Rprev, lcPrev), oPrev);
		Vec3 gUp = Mul(Rnow, up);
		now.rot = LookAt(now.pos, Add(Mul(Rnow, lt), oNow), gUp);
		prev.rot = Mul(LookAt(prev.pos, Add(Mul(Rprev, lt), oPrev), gUp), RotY(0.004 * k)); // and a turn of the camera
		for (int i = -2; i <= 2; i++) {
			Vec3 lp = Add(l, Add(Scale(east, 300.0 * i), Scale(north, 200.0 * i - 100.0 * k)));
			Vec3 pNow = Sub(Add(Mul(Rnow, lp), oNow), now.pos);
			Vec3 pPrev = Sub(Add(Mul(Rprev, lp), oPrev), prev.pos);
			double rx, ry;
			REQUIRE(ToPixel(Mul(View(prev.rot), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)), pPrev, 1.0, W, H, &rx, &ry));
			Mat4 M = ToFloat(Mul(Mul(Rigid(Rnow, oNow, Rprev, oPrev, now.pos, prev.pos), View(prev.rot)), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)));
			for (unsigned n = 0; n < 8; n++) {
				double jx, jy, qx, qy;
				Jitter(n, &jx, &jy);
				Reproject(now, pNow, 1.0, M, jx, jy, &qx, &qy);
				CHECK(std::fabs(qx - rx) < 0.01);
				CHECK(std::fabs(qy - ry) < 0.01);
			}
		}
	}
}

TEST_CASE("PostMath: the sky turns with the camera only", "[postmath]")
{
	Vec3 camNow = { 1.4e11, 1.2e7, 5.1e10 }, camPrev = Sub(camNow, { 500.0, -80.0, 120.0 }); // moving fast: no parallax at infinity
	Vec3 f = Unit({ 0.3, 0.2, 1.0 });
	Cam now, prev;
	now.pos = camNow;
	prev.pos = camPrev;
	now.rot = LookAt({ 0, 0, 0 }, f, { 0, 1, 0 });
	prev.rot = Mul(now.rot, Mul(RotY(-0.01), RotX(0.006)));
	Mat4 M = ToFloat(Mul(View(prev.rot), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)));
	for (int i = -3; i <= 3; i++) {
		Vec3 d = Unit(Add(f, { 0.05 * i, -0.04 * i, 0.0 }));
		double rx, ry;
		REQUIRE(ToPixel(Mul(View(prev.rot), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)), d, 0.0, W, H, &rx, &ry));
		for (unsigned n = 0; n < 8; n++) {
			double jx, jy, qx, qy;
			Jitter(n, &jx, &jy);
			Reproject(now, d, 0.0, M, jx, jy, &qx, &qy);
			CHECK(std::fabs(qx - rx) < 0.01);
			CHECK(std::fabs(qy - ry) < 0.01);
		}
	}
}

TEST_CASE("PostMath: a point on the chased vessel maps back by the vessel's motion", "[postmath]")
{
	Vec3 oNow = { 1.4e11 + 6.7e6, 1.2e7, 5.1e10 }, vel = { 0.0, 7.7e3, 120.0 }, oPrev = Sub(oNow, Scale(vel, DT));
	Mat3 Rnow = Mul(RotY(0.3), RotX(0.2)), Rprev = Mul(RotY(0.3 - 0.1 * DT), RotX(0.2 + 0.05 * DT)); // rolling and pitching
	Cam now, prev;
	now.pos = Add(oNow, Mul(Rnow, Vec3{ 0.0, 5.0, -30.0 }));
	prev.pos = Add(oPrev, Mul(Rprev, Vec3{ 0.0, 5.2, -30.4 })); // the chase camera lags a little
	now.rot = LookAt(now.pos, oNow, Mul(Rnow, Vec3{ 0, 1, 0 }));
	prev.rot = LookAt(prev.pos, oPrev, Mul(Rprev, Vec3{ 0, 1, 0 }));
	Mat4 M = ToFloat(Mul(Mul(Rigid(Rnow, oNow, Rprev, oPrev, now.pos, prev.pos), View(prev.rot)), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)));
	for (int i = -2; i <= 2; i++) {
		Vec3 lv = { 3.0 * i, 0.5 * i, 8.0 - 2.0 * i }; // vessel frame
		Vec3 pNow = Sub(Add(Mul(Rnow, lv), oNow), now.pos), pPrev = Sub(Add(Mul(Rprev, lv), oPrev), prev.pos);
		double rx, ry;
		REQUIRE(ToPixel(Mul(View(prev.rot), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)), pPrev, 1.0, W, H, &rx, &ry));
		for (unsigned n = 0; n < 8; n++) {
			double jx, jy, qx, qy;
			Jitter(n, &jx, &jy);
			Reproject(now, pNow, 1.0, M, jx, jy, &qx, &qy);
			CHECK(std::fabs(qx - rx) < 0.01);
			CHECK(std::fabs(qy - ry) < 0.01);
		}
	}
}

TEST_CASE("PostMath: a static camera keeps every pixel", "[postmath]")
{
	Cam now;
	now.pos = { 1.4e11, 1.2e7, 5.1e10 };
	now.rot = LookAt({ 0, 0, 0 }, { 0.2, -0.3, 1.0 }, { 0, 1, 0 });
	Mat3 I = RotY(0.0);
	Mat4 M = ToFloat(Mul(Mul(Rigid(I, now.pos, I, now.pos, now.pos, now.pos), View(now.rot)), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)));
	Vec3 p = Mul(now.rot, Vec3{ 40.0, -25.0, 900.0 });
	double rx, ry;
	REQUIRE(ToPixel(Mul(View(now.rot), Proj(TANAP, ASPECT, 0.5, 1e8, 0, 0)), p, 1.0, W, H, &rx, &ry));
	for (unsigned n = 0; n < 8; n++) {
		double jx, jy, qx, qy, px, py;
		Jitter(n, &jx, &jy);
		Reproject(now, p, 1.0, M, jx, jy, &qx, &qy, &px, &py);
		CHECK(std::fabs(qx - rx) < 0.01);
		CHECK(std::fabs(qy - ry) < 0.01);
		CHECK(std::fabs(qx + jx - px) < 0.01); // PSTaa adds the jitter: the history is read at the pixel itself
		CHECK(std::fabs(qy + jy - py) < 0.01);
	}
	double x, y;
	CHECK_FALSE(ToPixel(M, Mul(now.rot, Vec3{ 0.0, 0.0, -10.0 }), 1.0, W, H, &x, &y)); // behind the camera
}
