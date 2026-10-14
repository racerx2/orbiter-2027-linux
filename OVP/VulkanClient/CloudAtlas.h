// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: one seamless cloud texture for the far cloud layer and its shadows (design-cloud-seams)

#ifndef __CLOUDATLAS_H
#define __CLOUDATLAS_H

namespace CloudAtlas {

const int MAXLVL = 7, MINLVL = 5; // file levels the atlas is built from, finest complete one first
const int TILE = 512;           // source tile size, px
const int BAND = 8;             // px each side of a source tile joint that Unseam blends

inline int Rows (int lvl) { return 1 << (lvl - 4); } // source tiles of a file level, row 0 north
inline int Cols (int lvl) { return 1 << (lvl - 3); } // column 0 from -180 deg

struct Range { float x, y, z, w; }; // atlas uv = tile texUV * (z, w) + (x, y)

Range Quad (double minlat, double maxlat, double minlng, double maxlng, double rot = 0.0); // quadpatch tile, bounds in rad, rot added to lng
Range Hemi (int ilng, double rot = 0.0);  // hemisphere tile (internal level 0), ilng 0 west, 1 east
inline void Place (int r, int c, int &x0, int &y0) { x0 = c * TILE; y0 = r * TILE; } // source tile corner in the atlas
void Unseam (unsigned char *px, int w, int h, int ch, int tile, int band); // ch bytes per pixel; joints every tile px, x wraps; both sides meet at their mean
void Half (const unsigned char *src, int w, int h, unsigned char *dst); // 1 channel, 2x2 box to w/2 x h/2

}

#endif // !__CLOUDATLAS_H
