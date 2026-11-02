// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: the ground materials' maths, the tested copy of NewPlanet.glsl's _GROUNDMAT block (design-ground-detail 1)

#pragma once

namespace GroundGeom {

enum Mat { GRASS = 0, FOREST, SOIL, SAND, ROCK, GRAVEL, NMAT };	// a CLASS set's slots (the land class picks them)
enum Free { FINE = 0, COARSE, BRIGHT, FROCK, NFREE };			// a FREE set's slots (no land class: slope and patches)

const double HexK = 3.4641016151377544;		// 2 sqrt 3: the hex grid's scale per material repeat (Mikkelsen)
const float HexPeriod = 4096.0f;			// vertex ids wrap here
const float Near = 150.0f, Far = 600.0f;	// m: the materials full, gone

struct V3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

float Smooth (float e0, float e1, float x);	// GLSL smoothstep, e0 < e1
float Veg (V3 s);							// excess green of the satellite colour, 0..1
float Sandy (V3 s);							// bright and yellow, 0..1
void Weights (V3 cls, V3 s, float cosSlope, float W[NMAT]);	// class weights r g b, satellite colour, mesh normal . up; sum = min (1, cls sum) (1 - rock) + rock
void WeightsFree (float cosSlope, float patch, float third, float W[NMAT]); // slope, fine / coarse patches, the third material's patches, all 0..1; slots NFREE and up 0, sum 1
float Patches (float n32, float n128);						// two ClsNoise octaves (32 m, 128 m) to the fine / coarse share
float Third (float n64);										// a ClsNoise octave (64 m, another seed) to the third material's share
void Top3 (const float W[NMAT], int idx[3], float w[3]);	// the three strongest, scaled to W's sum
void HeightBlend (const float w[3], const float h[3], float out[3]); // sharpened by height, sum kept

// the tile origin in material repeats (exact integers) to the skewed grid's id base mod HexPeriod and fraction
void SkewSplit (double ou, double ov, float *cx, float *cy, float *fx, float *fy);
struct Tri {
	float w[3];				// barycentric
	float id[3][2];			// vertex ids mod HexPeriod
	float cu[3], cv[3];		// vertex centres, local repeats
};
void Grid (float u, float v, float fx, float fy, float cx, float cy, Tri &t); // u v: the tile's local material uv
void VertexXf (const float id[2], float *rot, float *ou, float *ov);		// a vertex's turn (rad) and offset
void TapUV (float u, float v, const Tri &t, int j, float *tu, float *tv);	// tap j's texture uv: turned about its vertex, offset
void HexWeights (const float w[3], const float h[3], float out[3]);		// w^7 by height (contrast 0.6), sum 1
void TurnNormal (float nx, float ny, float rot, float *wx, float *wy);		// a tap's map normal (x right, y image-up) to east, north

float Handover (float texels);				// a scan's texels per screen pixel to its share against the micro textures: 1 to 4, 0 from 32
float LambertRatio (V3 n, V3 m, V3 L);		// detail normal n against the mesh normal m: 1 when equal
void BSpline (float f, float *g0, float *g1, float *h0, float *h1); // 2 bilinear taps per axis: weights, offsets from floor(x - 0.5) in texels

} // namespace GroundGeom
