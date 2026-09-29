// Copyright (c) Martin Schweiger
// Licensed under the MIT License

// ====================================================================================
// File: D3d7util.h
// Desc: Helper functions and typing shortcuts for Direct3D programming.
// ====================================================================================

// ------------------------------------------------------------------------------------
// Vertex formats
// ------------------------------------------------------------------------------------

#ifndef __D3DUTIL_H
#define __D3DUTIL_H

// d3d.h left out: Direct3D 7 data types become float, DWORD, oapi::FVECTOR3 and oapi::FMATRIX4
#include "OrbiterAPI.h"
#include "DrawAPI.h"

struct VECTOR2D     { float x, y; };

struct VERTEX_XYZ   { float x, y, z; };                   // transformed vertex
struct VERTEX_XYZH  { float x, y, z, h; };                // untransformed vertex
struct VERTEX_XYZC  { float x, y, z; DWORD col; };     // untransformed vertex with single colour component
struct VERTEX_XYZHC { float x, y, z, h; DWORD col; };  // transformed vertex with single colour component

// untransformed unlit vertex with two sets of texture coordinates
struct VERTEX_2TEX  {
	float x, y, z, nx, ny, nz;
	float tu0, tv0, tu1, tv1;
	inline VERTEX_2TEX() {}
	inline VERTEX_2TEX (oapi::FVECTOR3 p, oapi::FVECTOR3 n, float u0, float v0, float u1, float v1)
	{ x = p.x, y = p.y, z = p.z, nx = n.x, ny = n.y, nz = n.z;
  	  tu0 = u0, tv0 = v0, tu1 = u1, tv1 = v1; }
};
// FVF_2TEX left out: Direct3D 7 flexible vertex format code

// transformed lit vertex with 1 colour definition and one set of texture coordinates
struct VERTEX_TL1TEX {
	float x, y, z, rhw;
	DWORD col;
	float tu, tv;
};
// FVF_TL1TEX left out: Direct3D 7 flexible vertex format code

// transformed lit vertex with two sets of texture coordinates
struct VERTEX_TL2TEX {
	float x, y, z, rhw;
	DWORD diff, spec;
	float tu0, tv0, tu1, tv1;
};
// FVF_TL2TEX left out: Direct3D 7 flexible vertex format code

VERTEX_XYZ  *GetVertexXYZ  (DWORD n);
VERTEX_XYZC *GetVertexXYZC (DWORD n);
// Return pointer to static vertex buffer of given type of at least size n

inline void MATRIX4toD3DMATRIX (const MATRIX4 &M, oapi::FMATRIX4 &D)
{
	D.m11 = (float)M.m11;  D.m12 = (float)M.m12;  D.m13 = (float)M.m13;  D.m14 = (float)M.m14;
	D.m21 = (float)M.m21;  D.m22 = (float)M.m22;  D.m23 = (float)M.m23;  D.m24 = (float)M.m24;
	D.m31 = (float)M.m31;  D.m32 = (float)M.m32;  D.m33 = (float)M.m33;  D.m34 = (float)M.m34;
	D.m41 = (float)M.m41;  D.m42 = (float)M.m42;  D.m43 = (float)M.m43;  D.m44 = (float)M.m44;
}

// ------------------------------------------------------------------------------------
// Miscellaneous helper functions
// ------------------------------------------------------------------------------------

#define SAFE_DELETE(p)  { if(p) { delete (p);     (p)=NULL; } }
// SAFE_RELEASE left out: COM reference counting

#endif // !__D3DUTIL_H
