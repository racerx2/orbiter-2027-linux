// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the look passes (flare, haze, sharpen, filmic), unit tested (design-visual-batch 13)

#ifndef __LOOKMATH_H
#define __LOOKMATH_H

namespace LookMath {

double Filmic (double x); // Scatter.glsl's _FILMIC curve: ACES fit with the input scaled by 0.568

double Smoothstep (double e0, double e1, double x);

struct V2 { double x, y; };

// lens flare: screen heights from the centre; t = 0 the centre, 1 the sun's mirror, -1 the sun
const int GHOSTS = 6;
extern const double GhostT[GHOSTS];
V2 GhostPos (V2 sun, double t);
double HaloWeight (double sunDist);          // halo ring strength by the sun's distance from the centre (screen heights)
double EdgeFade (double ndcx, double ndcy);  // flare fade as the sun leaves the screen

// heat haze: a camera-relative segment through a row-vector view-projection (D3D, row major)
struct Seg {
	double x0, y0, x1, y1; // pixels, y down
	double w0, w1;         // clip w (view depth) of the kept ends
	double t0, t1;         // the kept part of the original segment
};
bool ProjectSegment (const double vp[16], const double a[3], const double b[3], double W, double H, double wmin, Seg &out); // false when wholly behind wmin
double SegDist (double px, double py, double x0, double y0, double x1, double y1, double *t); // pixel to segment, t 0..1 along it
double HazeStrength (double level, double d, double w, double t); // PSHaze's s before the near fade

// contrast-adaptive sharpening (one channel)
double SharpenWeight (double mn, double mx, double a); // in -a..0
double Sharpen (double c, const double n[4], double a); // 5-tap cross, not below 0

double Vignette (double u, double v, double aspect); // factor at uv, fixed radius

}

#endif // !__LOOKMATH_H
