// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the TAA pass (design-visual-batch 13)

#include "PostMath.h"
#include <cmath>

namespace PostMath {

double Halton (int i, int b)
{
	double f = 1.0, r = 0.0;
	for (; i > 0; i /= b) { f /= b; r += f * (i % b); }
	return r;
}

void Jitter (unsigned n, double *jx, double *jy)
{
	int i = int(n % 8u) + 1;
	*jx = Halton(i, 2) - 0.5;
	*jy = Halton(i, 3) - 0.5;
}

Vec3 Add (const Vec3 &a, const Vec3 &b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 Sub (const Vec3 &a, const Vec3 &b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 Scale (const Vec3 &a, double s) { return { a.x * s, a.y * s, a.z * s }; }
double Dot (const Vec3 &a, const Vec3 &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Unit (const Vec3 &a) { return Scale(a, 1.0 / std::sqrt(Dot(a, a))); }

Vec3 Mul (const Mat3 &m, const Vec3 &v)
{
	return { m.m[0][0] * v.x + m.m[0][1] * v.y + m.m[0][2] * v.z,
		m.m[1][0] * v.x + m.m[1][1] * v.y + m.m[1][2] * v.z,
		m.m[2][0] * v.x + m.m[2][1] * v.y + m.m[2][2] * v.z };
}

Mat3 Mul (const Mat3 &a, const Mat3 &b)
{
	Mat3 r = {};
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++)
			for (int k = 0; k < 3; k++) r.m[i][j] += a.m[i][k] * b.m[k][j];
	return r;
}

Mat3 Transpose (const Mat3 &m)
{
	Mat3 r;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) r.m[i][j] = m.m[j][i];
	return r;
}

Mat3 RotY (double a)
{
	double c = std::cos(a), s = std::sin(a);
	return { { { c, 0, s }, { 0, 1, 0 }, { -s, 0, c } } };
}

Mat3 RotX (double a)
{
	double c = std::cos(a), s = std::sin(a);
	return { { { 1, 0, 0 }, { 0, c, -s }, { 0, s, c } } };
}

Mat4 Identity4 ()
{
	Mat4 r = {};
	for (int i = 0; i < 4; i++) r.m[i][i] = 1.0;
	return r;
}

Mat4 Mul (const Mat4 &a, const Mat4 &b)
{
	Mat4 r = {};
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			for (int k = 0; k < 4; k++) r.m[i][j] += a.m[i][k] * b.m[k][j];
	return r;
}

Mat4 ToFloat (const Mat4 &m)
{
	Mat4 r;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) r.m[i][j] = double(float(m.m[i][j]));
	return r;
}

// p' = A p + b with A = Rprev Rnow^T, b = A (camNow - oNow) + (oPrev - camPrev); the large offsets cancel in double
Mat4 Rigid (const Mat3 &Rnow, const Vec3 &oNow, const Mat3 &Rprev, const Vec3 &oPrev, const Vec3 &camNow, const Vec3 &camPrev)
{
	Mat3 A = Mul(Rprev, Transpose(Rnow));
	Vec3 b = Add(Mul(A, Sub(camNow, oNow)), Sub(oPrev, camPrev));
	Mat4 r = Identity4();
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) r.m[i][j] = A.m[j][i];
	r.m[3][0] = b.x; r.m[3][1] = b.y; r.m[3][2] = b.z;
	return r;
}

Mat4 View (const Mat3 &camRot)
{
	Mat4 r = Identity4();
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) r.m[i][j] = camRot.m[i][j];
	return r;
}

Mat4 Proj (double tanAp, double aspect, double zn, double zf, double j31, double j32)
{
	Mat4 r = {};
	r.m[0][0] = aspect / tanAp;
	r.m[1][1] = 1.0 / tanAp;
	r.m[2][2] = zf / (zf - zn);
	r.m[3][2] = -zn * r.m[2][2];
	r.m[2][3] = 1.0;
	r.m[2][0] = j31;
	r.m[2][1] = j32;
	return r;
}

bool ToPixel (const Mat4 &m, const Vec3 &p, double w, double W, double H, double *px, double *py)
{
	double c[4];
	for (int j = 0; j < 4; j++) c[j] = p.x * m.m[0][j] + p.y * m.m[1][j] + p.z * m.m[2][j] + w * m.m[3][j];
	if (c[3] <= 0.0) return false;
	*px = (c[0] / c[3] * 0.5 + 0.5) * W;
	*py = (0.5 - c[1] / c[3] * 0.5) * H;
	return true;
}

Vec3 PixelRay (double u, double v, const Vec3 &X, const Vec3 &Y, const Vec3 &Zj, double tx, double ty)
{
	return Unit(Add(Add(Scale(X, (u * 2.0 - 1.0) * tx), Scale(Y, (1.0 - v * 2.0) * ty)), Zj));
}

// ndc = view / z * scale + j: the ray through ndc takes off j / scale along each axis
Vec3 JitterAxis (const Vec3 &X, const Vec3 &Y, const Vec3 &Z, double j31, double j32, double tx, double ty)
{
	return Sub(Sub(Z, Scale(X, j31 * tx)), Scale(Y, j32 * ty));
}

}
