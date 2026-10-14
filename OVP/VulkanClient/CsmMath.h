// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: cascade fitting and texel snapping for the scene shadows, unit tested (design-scene-shadows 4, 6, 17, Rev 2)

#ifndef __CSMMATH_H
#define __CSMMATH_H

namespace CsmMath {

inline int Version () { return 3; }

const int NC = 4;                // cascades
const int FT = 4;                // the first target tile (the camera's target vessel), after the cascades
const int NTgt = 2;              // target tiles: the camera's target, the nearest other vessel within C0's reach
const int NT = NC + NTgt;        // atlas tiles
const int N = 2048;              // texels a side of a tile
const int AW = 6144, AH = 4096;  // atlas texels: the cascades 2 x 2 on the left, the target tiles at (2, 0) and (2, 1)
const int HashP = 4096;          // period of the stable noise index, world texels
const double Zext = 20e3;        // casters this far toward the sun, m
const double SunTurn = 1.0 / 2048.0; // the light frame is re-based past this turn, rad
const double MoveK = 0.05;       // a kept tile is drawn again once the camera moved this share of r in its light space
const double SelectK = 0.94;     // receivers use the first cascade with d < 0.94 r
const double BandK = 0.80;       // and blend into the next one from 0.80 r
const double FadeStart = 30e3, FadeEnd = 42e3; // the cascades fade into the horizon maps, m
const double CoverEnd = 30e3;    // stencil shadows are replaced up to here, m
const double MaxAgl = 42e3;      // camera height over the ground for the cascades, m
const double MinSunSin = -0.0523359562; // sin(-3 deg): sun elevation at the camera's ground point
const double TgtMargin = 8.0;    // target tile: texels kept free round its casters (kernel, gather, normal offset, snap)
const double TgtMinT = 0.005;    // target tile: finest texel, m
const double TgtKernel = 4.0;    // target tile: the widest penumbra it holds, PCF radius in texels
const double Pi = 3.14159265358979323846;

extern const double Radius[NC];      // box half-size r, m
extern const double TerrainCut[NC];  // terrain caster grid spacing cut, m (0: the deepest loaded tiles)
extern const double TerrainBias[NC]; // depth bias of terrain receivers for the caster LOD, m
extern const double Disk8[8][2];     // SceneShadow.glsl's CsmDisk, Disk(8)

struct V3 { double x, y, z; };
struct M3 { double m[3][3]; };       // rows

V3 Add (const V3 &a, const V3 &b);
V3 Sub (const V3 &a, const V3 &b);
V3 Mul (const V3 &a, double s);
double Dot (const V3 &a, const V3 &b);
V3 Cross (const V3 &a, const V3 &b);
double Len (const V3 &a);
V3 Unit (const V3 &a);
V3 MulM (const M3 &A, const V3 &v);  // A v
V3 MulMT (const M3 &A, const V3 &v); // A^T v
double Angle (const V3 &a, const V3 &b); // unit vectors

double Texel (int i);                // 2 r / N, m
double Depth (int i);                // depth range 2 r + Zext, m
double Smoothstep (double e0, double e1, double x);

// light frame rows X, Y, Z = -L in the planet frame, right-handed (Y = X x L); alt: the pole fallback, switched with hysteresis
M3 Basis (const V3 &L, bool &alt);

// a cascade's box in light space: snapped centre and the depth start
struct Box { double mx, my, z0; };
Box Fit (int i, const V3 &q);        // q: the camera in light space

// s = A v + T: camera-relative world (global axes) to box metres (x, y about the centre, z from z0 away from the sun)
struct Xf { M3 A; V3 T; };
Xf Transform (const M3 &B, const M3 &Rp, const V3 &cp, const Box &b); // Rp: planet rotation (local to global), cp: camera in the planet frame
V3 Apply (const Xf &x, const V3 &v);

// atlas slot of tile i (column, row, in tiles)
int Col (int i);
int Row (int i);

// row-vector 4x4 matrices (D3D order, m[row][col]); tile i of half-size r and depth range D, cascades take Radius[i] and Depth(i)
void CasterMatrix (int i, double r, double D, const Xf &x, double out[4][4]);   // clip space, the tile folded into the whole-atlas viewport
void ReceiverMatrix (int i, double r, double D, const Xf &x, double out[4][4]); // atlas u, v (down) and depth 0..1
void CasterMatrix (int i, const Xf &x, double out[4][4]);
void ReceiverMatrix (int i, const Xf &x, double out[4][4]);
bool CullSphere (int i, const Xf &x, const V3 &c, double rho);
bool CastsIn (int i, double rho);    // not below half a texel

// the target tile round its casters' points (planet-frame light space) holding a penumbra up to wMax m; quarter-octave texels, finer than prevT only below 0.97 of a step; false when coarser than C0
struct Target { double t, r, D; Box box; };
bool FitTarget (const V3 *p, int n, double wMax, double prevT, Target &f);

// refresh
bool Due (int i, unsigned frame);    // C0, C1 every frame, C2 on even frames, C3 on frame % 4 == 1
bool MovedOut (int i, const V3 &qDrawn, const V3 &qNow);

// receiver side, as SceneShadow.glsl
int Select (double d, double *band); // cascade by camera distance and the weight of the next; -1 beyond
double FadeWeight (double d);
double Penumbra (double dz, double tan2w);   // blocker to receiver along the light (m) to penumbra width (m)
double FilterRadius (double w, double t);    // PCF radius of a cascade, texels, 1..4 (one texel at least: the texel grid never shows)
double HandOver (double w, double t);        // weight of cascade i+1 for a wide penumbra
double EffectiveRadius (int i, double w);    // m, the kernel the hand-over blend gives
double NormalOffset (double t, double cosT); // m along the normal
double PlaneSlope (double cosT);             // depth change per metre across the light, capped
double ConstBias (double t, double kb);      // m
double CapBias (double t, double cosT);      // m, 1.5 texels of the slope PlaneSlope's cap leaves out (SceneShadow.glsl CsmSetup)
void WorldTexel (double t, const Box &b, double &ox, double &oy); // the tile's first texel in world texels mod HashP, texel t
void WorldTexel (int i, const Box &b, double &ox, double &oy);
void Disk (int n, double out[][2]);          // best-candidate points in the unit disk (Mitchell 1991), fixed seed

// the stencil shadow of a caster of radius rho whose shadow lands dShadow from the camera is replaced by the cascades
bool Covers (double dShadow, double rho);

}

#endif // !__CSMMATH_H
