// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: screen-space reflection tracing maths, unit tested (design-ssr 16); the CPU mirror of shaders/Ssr.glsl

#ifndef __SSRMATH_H
#define __SSRMATH_H

#include "PostMath.h"
#include <vector>

namespace SsrMath {

inline int Version () { return 2; }

// display curves of the hosts and their inverses (y clamped to 0.985 in the planet inverses, 0.99 in the mesh curve's)
double Hdr (double x, double expo);           // Scatter.glsl HDR(): 1 - exp(-Expo x)
double HdrInv (double y, double expo);
double Filmic (double x, double expo);        // Scatter.glsl Filmic(Expo x), Narkowicz 2015 with the 0.568 scale
double FilmicInv (double y, double expo);     // the positive root of the rational curve
double MeshCurve (double x);                  // x (1 + x^4)^-1/4 of Metalness.fx and PbrMesh.fx
double MeshCurveInv (double y);
double SrgbToLinear (double c);               // PbrBRDF.glsl
double LinearToSrgb (double c);

// what a mesh host writes for a mirror of radiance L (curve: no _LIGHTGLOW; srgb: PbrMesh.fx), and the decode of a captured y
double MeshEncode (double L, double T, double I, double e, bool curve, bool srgb);
double MeshDecode (double y, double T, double I, double eSrc, bool curve, bool srgb);

// screen-space segment of a ray: points p and p + R L, row vector (x, y, z, w) times M
struct Seg { double X0, Y0, X1, Y1, s0, s1; bool clipped; };
bool Segment (const PostMath::Mat4 &M, const PostMath::Vec3 &P, const PostMath::Vec3 &R, double W, double H, double nearW, double L, Seg &out); // false: the start is behind nearW
bool Vanish (const PostMath::Mat4 &Msky, const PostMath::Vec3 &R, double W, double H, double *X, double *Y); // the pixel of direction R, false behind

// depth pyramid of s = 1/z (0 none), max of each 2x2, level 0 padded with 0 to multiples of 2^(L-1)
struct HiZ {
	int W = 0, H = 0, Wp = 0, Hp = 0, L = 0;
	std::vector<std::vector<float>> lv;
	float At (int k, int x, int y) const;     // the cell clamped into level k
};
int HiZLevels (int W, int H);                 // min(9, floor(log2 max) + 1)
void BuildHiZ (const std::vector<float> &s, int W, int H, HiZ &z);

struct WalkPrm { double jx = 0.0, jy = 0.0, startPx = 1.0, tau0 = 0.25, tauRel = 0.015; int maxIt = 40, finish = 16; }; // finish: level-0 point samples over the rest when maxIt runs out (0 none)
struct WalkOut { bool hit = false, end = false, finished = false; double X = 0.0, Y = 0.0, t = 0.0; int it = 0; }; // end: no hit and the segment's far end was reached; finished: by the point samples
WalkOut Walk (const HiZ &z, const Seg &g, const WalkPrm &p);        // the Hi-Z walk of Ssr.glsl (Uludag 2014, with the toward-camera rule)
WalkOut March (const HiZ &z, const Seg &g, const WalkPrm &p, double stepPx); // brute force: dense point samples, the same thickness rule
bool SkyAt (const HiZ &z, double X, double Y, const WalkPrm &p);   // level 0 is empty at the pixel

// directions and weights
PostMath::Vec3 Lift (const PostMath::Vec3 &P, const PostMath::Vec3 &Ng, double k); // the start raised off its surface by k times its distance (about 1.5 pixel angles: no self-hits from the pixel-centre depth)
PostMath::Vec3 KeepAbove (const PostMath::Vec3 &R, const PostMath::Vec3 &Ng, double c); // R bent to at least c above the geometric surface
PostMath::Vec3 Reflect (const PostMath::Vec3 &I, const PostMath::Vec3 &N);
PostMath::Vec3 Vndf (const PostMath::Vec3 &V, const PostMath::Vec3 &N, double a, double u1, double u2); // a half vector H (Heitz 2018), world frame
double Ign (double x, double y);              // interleaved gradient noise (Jimenez 2014)
double ConeLod (double dPx, double a, double k, double maxLod);
double EdgeFade (double u, double v, double aspect, double width);
double Smoothstep (double a, double b, double x);
double EnvBRDFApprox (double f0, double perc, double NoV); // Karis 2014, split-sum directional albedo fit
struct Wet { double alpha, f0, albedo; };
Wet WetModel (double w);                      // Lagarde 2013: rough dry concrete to a water film
bool JumpTest (double moved, double prevMoved, double turnRad); // true: the history is dropped

// camera-relative point now to the source clip space, rigid with a body (PostMath::Rigid), times the source view-projection
PostMath::Mat4 BodyMatrix (const PostMath::Mat3 &Rnow, const PostMath::Vec3 &oNow, const PostMath::Mat3 &Rprev, const PostMath::Vec3 &oPrev,
	const PostMath::Vec3 &camNow, const PostMath::Vec3 &camPrev, const PostMath::Mat4 &VPprev);

}

#endif // !__SSRMATH_H
