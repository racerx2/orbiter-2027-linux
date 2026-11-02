
// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: ray-traced probe GI maths, unit tested; DDGI.glsl mirrors it (design-ddgi 4, 6; Rev 2)
// after Majercik et al. 2019 (DDGI) and 2021 (probe states, relocation, hysteresis), octahedral maps after Cigolle et al. 2014

#ifndef __DDGIMATH_H
#define __DDGIMATH_H

namespace DdgiMath {

const int NCASC = 6;			// cascades: 1, 4, 16, 64, 256, 1024 m
const int GX = 24, GY = 12, GZ = 24;	// probes per cascade: x east, y up, z north
const int NPC = GX * GY * GZ;	// probes per cascade
const int NWORLD = NPC * NCASC;	// the world's probes
const int VG = 16;				// cockpit volume probes per side (design-ddgi-cockpit 2)
const int NVC = VG * VG * VG;
const int VCASC = NCASC;		// the cockpit volume's cascade index
const int NPROBE = NWORLD + NVC;
static_assert (NPROBE < 65536);	// the schedule keeps a probe in 16 bits
const double VSMIN = 0.08, VSMAX = 0.5;	// cockpit volume spacing limits [m]
const int IRR = 6;				// irradiance texels per probe side (no border)
const int DST = 14;				// distance texels per probe side
const int RAYS = 96;			// rays per probe update
const int ROW = GX * GZ;		// probes per atlas row
const double CPI = 3.14159265358979323846;

struct V3 { double x = 0.0, y = 0.0, z = 0.0; };
struct I3 { int x = 0, y = 0, z = 0; };
V3 Add (const V3 &a, const V3 &b);
V3 Sub (const V3 &a, const V3 &b);
V3 Scale (const V3 &a, double s);
double Dot (const V3 &a, const V3 &b);
V3 Unit (const V3 &a);

double Spacing (int c);						// 4^c m
int Weight (int c);							// the world cascades' update shares 8:4:2:1:1:1
int CockpitTake (int left);					// the cockpit volume's probes of what the fresh ones left: half, at least 1, at most NVC
I3 Dims ();									// GX, GY, GZ
int Cascade (int probe);					// 0..5 the world's, VCASC the cockpit volume's

// the cockpit volume: VG^3 probes in the vessel frame over the cockpit meshes' box, or a VG x VSMAX cube on the eye when the box is larger
struct VGrid { V3 o; double s = 0.0; bool eye = false; };	// lowest cell corner, spacing, centred on the eye
VGrid CockpitGrid (const V3 &lo, const V3 &hi, const V3 &eye);
bool SameGrid (const VGrid &a, const VGrid &b);	// spacing and edges within a quarter cell
bool EyeKept (const VGrid &g, const V3 &eye);	// an eye-centred grid holds while the eye is in its middle half
V3 VProbePos (const VGrid &g, int slot);		// cell centre, vessel frame; slot x fastest, then z, then y

// the grid: a cascade's lowest cell (absolute cells of the anchor frame); the camera's cell sets x, z; y stands on the ground under it (a quarter of the height below), or is around the camera when that grid wouldn't reach it
I3 Origin (int c, const V3 &cam, double ground);
int Slot (const I3 &cell);					// the probe slot of an absolute cell: each axis mod its size (a torus, the grid scrolls)
I3 CellOf (int slot, const I3 &origin);	// the cell in [origin, origin + dims) that holds the slot
V3 ProbePos (int c, const I3 &cell);		// cell centre, anchor frame
bool Inside (int c, const I3 &origin, const V3 &p, double margin); // within the cascade, margin cells in from its faces
double Blend (int c, const I3 &origin, const V3 &p);	// 1 deep inside, 0 at the outer two cells (to the next cascade)

// octahedral maps
void OctEncode (const V3 &d, double *u, double *v);	// unit vector to [-1, 1]^2
V3 OctDecode (double u, double v);
V3 TexelDir (int i, int j, int n);			// texel centre of an n x n map
void WrapTexel (int *i, int *j, int n);		// a texel off the map to the one it continues on (the octahedral fold)
struct Tap { int i[4], j[4]; double w[4]; };
Tap OctBilinear (const V3 &d, int n);		// the four texels and weights of a bilinear read across the folds

// rays
V3 Fibonacci (int i, int n);				// spherical Fibonacci, unit
void Rotation (unsigned seed, double R[9]);	// a uniform random rotation (row major)

// visibility
double Chebyshev (double mean, double mean2, double r);	// 1 in front of the mean distance, falling behind it (cubed)

// the update budget
int Budget (int n, double ms, double target, int nmin, int nmax); // probes next frame from this frame's time

// atlas
void AtlasTexel (int probe, int n, int *x, int *y);	// the probe's first texel in an atlas of n x n blocks
void AtlasSize (int n, int *w, int *h);

// light beyond the traced region (light units): sky above the horizon, the far ground below it
V3 MissRadiance (const V3 &dir, const V3 &up, const V3 &sky, const V3 &ground);
V3 Irradiance (const V3 &n, const V3 &up, const V3 &sky, const V3 &ground); // what MissRadiance gives an open receiver: (1/pi) int L cos

}

#endif
