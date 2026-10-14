// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the TAA pass, unit tested (design-visual-batch 13)

#ifndef __POSTMATH_H
#define __POSTMATH_H

namespace PostMath {

struct Vec3 { double x, y, z; };
struct Mat3 { double m[3][3]; }; // column vectors, v' = M v (Orbiter MATRIX3: m[0][1] is m12)
struct Mat4 { double m[4][4]; }; // row vectors, v' = v M (D3D)

double Halton (int i, int b); // radical inverse of i in base b, i >= 1
void Jitter (unsigned n, double *jx, double *jy); // pixels: Halton(2, 3) at n % 8 + 1, minus 0.5

Vec3 Add (const Vec3 &a, const Vec3 &b);
Vec3 Sub (const Vec3 &a, const Vec3 &b);
Vec3 Scale (const Vec3 &a, double s);
double Dot (const Vec3 &a, const Vec3 &b);
Vec3 Unit (const Vec3 &a);
Vec3 Mul (const Mat3 &m, const Vec3 &v);
Mat3 Mul (const Mat3 &a, const Mat3 &b);
Mat3 Transpose (const Mat3 &m);
Mat3 RotY (double a);
Mat3 RotX (double a);
Mat4 Identity4 ();
Mat4 Mul (const Mat4 &a, const Mat4 &b);
Mat4 ToFloat (const Mat4 &m); // each element rounded to float, as the GPU gets it

// camera-relative point now to camera-relative point a frame back, for a body at o with axes R (local to global)
Mat4 Rigid (const Mat3 &Rnow, const Vec3 &oNow, const Mat3 &Rprev, const Vec3 &oPrev, const Vec3 &camNow, const Vec3 &camPrev);
Mat4 View (const Mat3 &camRot); // camera axes (local to global) to the D3D view matrix, rotation only
Mat4 Proj (double tanAp, double aspect, double zn, double zf, double j31, double j32); // D3D LH, Scene::SetCameraAperture
bool ToPixel (const Mat4 &m, const Vec3 &p, double w, double W, double H, double *px, double *py); // (p, w) M to pixels; false behind
Vec3 PixelRay (double u, double v, const Vec3 &X, const Vec3 &Y, const Vec3 &Zj, double tx, double ty); // VolCloud.glsl PixelRay
Vec3 JitterAxis (const Vec3 &X, const Vec3 &Y, const Vec3 &Z, double j31, double j32, double tx, double ty); // Z with the projection's jitter folded in

}

#endif // !__POSTMATH_H
