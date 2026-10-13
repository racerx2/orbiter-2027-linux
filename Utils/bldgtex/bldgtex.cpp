// not upstream: draws Textures/Bldg for the streamed buildings: facade atlas with its night and normal maps, flat roof; our own procedural drawing, nothing downloaded

#include "BldgGeom.h"
#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#define STB_DXT_IMPLEMENTATION
#include "stb_dxt.h"

using namespace BldgGeom;

namespace {

uint32_t H (uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
uint32_t H3 (int a, int b, int c) { return H ((uint32_t)a * 0x9E3779B1u ^ H ((uint32_t)b * 0x85EBCA77u ^ H ((uint32_t)c * 0xC2B2AE3Du))); }
float F01 (uint32_t h) { return (h & 0xffffff) / 16777216.0f; }

// value noise on a lattice of period p (0: none)
float Noise (float x, float y, int seed, int p)
{
	int x0 = (int)std::floor (x), y0 = (int)std::floor (y);
	float fx = x - x0, fy = y - y0;
	fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
	auto g = [&] (int a, int b) { if (p) { a = ((a % p) + p) % p; b = ((b % p) + p) % p; } return F01 (H3 (a, b, seed)); };
	float a = g (x0, y0), b = g (x0 + 1, y0), c = g (x0, y0 + 1), d = g (x0 + 1, y0 + 1);
	return a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
}

float Fbm (float x, float y, int seed, int oct, int p = 0)
{
	float s = 0, amp = 0.5f, tot = 0;
	for (int i = 0; i < oct; i++) {
		s += amp * Noise (x, y, seed + i * 131, p);
		tot += amp;
		x *= 2; y *= 2; p *= 2; amp *= 0.5f;
	}
	return s / tot;
}

struct C { float r, g, b; };
C operator* (C a, float k) { return {a.r * k, a.g * k, a.b * k}; }
C operator+ (C a, C b) { return {a.r + b.r, a.g + b.g, a.b + b.b}; }
C Mix (C a, C b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
const C Black {0, 0, 0};

struct Px { C dif; float h; C emi; };

// one image of w x h with diffuse, height and night colour
struct Canvas {
	int w, h;
	std::vector<uint8_t> dif, emi; // RGBA
	std::vector<float> hgt;
	Canvas (int W, int Hh) : w(W), h(Hh), dif ((size_t)W * Hh * 4, 255), emi ((size_t)W * Hh * 4, 255), hgt ((size_t)W * Hh, 0.5f) {}
	void Set (int x, int y, const Px &p)
	{
		size_t i = (size_t)y * w + x;
		auto b = [] (float v) { return (uint8_t)std::clamp ((int)std::lround (v), 0, 255); };
		dif[i * 4] = b (p.dif.r); dif[i * 4 + 1] = b (p.dif.g); dif[i * 4 + 2] = b (p.dif.b);
		emi[i * 4] = b (p.emi.r); emi[i * 4 + 1] = b (p.emi.g); emi[i * 4 + 2] = b (p.emi.b);
		hgt[i] = p.h;
	}
	void Copy (int x, int y, int sx, int sy)
	{
		size_t i = (size_t)y * w + x, s = (size_t)sy * w + sx;
		memcpy (&dif[i * 4], &dif[s * 4], 4); memcpy (&emi[i * 4], &emi[s * 4], 4); hgt[i] = hgt[s];
	}
	// the gutter round the area x0..x1, y0..y1 from its edge pixels, gt above and gb below
	void Pad (int x0, int y0, int x1, int y1, int g, int gt, int gb)
	{
		for (int y = y0 - gt; y < y1 + gb; y++)
			for (int x = x0 - g; x < x1 + g; x++) {
				if (x >= x0 && x < x1 && y >= y0 && y < y1) continue;
				Copy (x, y, std::clamp (x, x0, x1 - 1), std::clamp (y, y0, y1 - 1));
			}
	}
};

// ---- wall materials, x y in region pixels (about 38 px a metre)

enum Mat { Brick, Render, Panel, Stone, Metal, Corrugated, Siding };

C Wall (Mat m, C base, int x, int y, int seed, float &h)
{
	h = 0.5f;
	float n = Fbm (x / 48.0f, y / 48.0f, seed, 4) - 0.5f;
	float stain = Fbm (x / 9.0f, y / 160.0f, seed + 7, 3);
	float k = 1.0f + 0.10f * n - 0.08f * std::max (0.0f, stain - 0.55f) * 2.0f;
	switch (m) {
	case Brick: {
		int course = y / 3, off = (course & 1) * 4, bi = (x + off) / 9;
		if (y % 3 == 2 || (x + off) % 9 == 8) { h = 0.44f; return Mix (base, C {190, 185, 175}, 0.55f) * (k * 0.95f); }
		return base * (k * (0.86f + 0.26f * F01 (H3 (bi, course, seed))));
	}
	case Render: return base * (k + 0.04f * (F01 (H3 (x, y, seed)) - 0.5f));
	case Panel: {
		if (((y - UpperY) % FloorPx + FloorPx) % FloorPx < 2 || ((x - Gutter) % BayPx + BayPx) % BayPx < 2) { h = 0.42f; return base * (k * 0.72f); }
		int pi = (x - Gutter) / BayPx, pj = (y - UpperY) / FloorPx;
		return base * (k * (0.95f + 0.1f * F01 (H3 (pi, pj, seed))));
	}
	case Stone: {
		int row = y / 19, off = (row & 1) * 23, bi = (x + off) / 46;
		if (y % 19 == 18 || (x + off) % 46 == 45) { h = 0.44f; return base * (k * 0.8f); }
		return base * (k * (0.92f + 0.14f * F01 (H3 (bi, row, seed))));
	}
	case Metal: {
		int pi = x / 46;
		if (x % 46 == 45) { h = 0.45f; return base * (k * 0.7f); }
		return base * (k * (0.9f + 0.15f * F01 (H3 (pi, y / 120, seed))));
	}
	case Corrugated: {
		float s = std::sin (x * 2.0f * (float)M_PI / 6.0f);
		h = 0.5f + 0.06f * s;
		return base * (k * (0.97f + 0.1f * s));
	}
	case Siding: {
		int bo = y % 7;
		h = 0.46f + 0.02f * bo;
		return base * (k * (bo == 6 ? 0.72f : 1.02f - 0.02f * bo));
	}
	}
	return base;
}

// ---- looks

enum Win { Punched, Curtain, Ribbon, Strip };
enum Ground { Shops, Door, Lobby, House, Industrial };

struct Look {
	Mat mat; C wall;
	Win win; int wx0, wx1, wy0, wy1;  // window in a 126 x 122 cell
	C frame; int fw; int bars;         // frame width; 0 none 1 vertical 2 cross 3 two by three 4 every 27/42 px
	C glass; bool sill, lintel, shutters, balcony;
	Ground ground; float lit; bool cool;
};

const C Cream {225, 220, 205}, White {235, 235, 232}, DarkFrame {45, 45, 48}, StoneC {200, 192, 175};

Look Looks (int s)
{
	switch (s) {
	case 0: return {Brick, {150, 70, 52}, Punched, 34, 92, 22, 96, White, 3, 2, {62, 74, 86}, true, true, false, false, Shops, 0.35f, false};
	case 1: return {Brick, {110, 72, 55}, Punched, 30, 96, 20, 98, {70, 50, 40}, 3, 1, {58, 66, 76}, true, false, false, false, Door, 0.35f, false};
	case 2: return {Brick, {196, 160, 110}, Punched, 34, 92, 22, 96, {70, 85, 75}, 3, 2, {60, 72, 84}, true, true, false, false, Shops, 0.35f, false};
	case 3: return {Render, {226, 222, 212}, Punched, 36, 90, 24, 94, White, 3, 2, {60, 72, 86}, true, false, false, false, Door, 0.35f, false};
	case 4: return {Render, {214, 180, 120}, Punched, 38, 88, 24, 94, Cream, 3, 1, {60, 70, 82}, true, false, true, false, Door, 0.35f, false};
	case 5: return {Panel, {165, 165, 160}, Punched, 30, 96, 26, 86, {200, 200, 200}, 2, 1, {58, 70, 82}, false, false, false, true, Door, 0.4f, false};
	case 6: return {Panel, {196, 184, 160}, Punched, 22, 104, 28, 88, DarkFrame, 3, 4, {60, 72, 84}, false, false, false, false, Door, 0.4f, false};
	case 7: return {Stone, {206, 194, 168}, Punched, 40, 86, 14, 100, {40, 60, 50}, 3, 3, {55, 66, 76}, true, true, false, false, Lobby, 0.4f, false};
	case 8: return {Metal, {70, 95, 100}, Curtain, 0, 126, 0, 96, {150, 160, 165}, 2, 1, {70, 110, 120}, false, false, false, false, Lobby, 0.55f, true};
	case 9: return {Metal, {40, 44, 50}, Curtain, 0, 126, 0, 96, {60, 62, 66}, 2, 4, {45, 55, 65}, false, false, false, false, Lobby, 0.55f, true};
	case 10: return {Panel, {200, 200, 195}, Ribbon, 0, 126, 30, 84, {80, 82, 85}, 2, 4, {60, 78, 92}, false, false, false, false, Lobby, 0.5f, true};
	case 11: return {Metal, {62, 64, 68}, Punched, 18, 108, 18, 98, {20, 20, 22}, 2, 1, {50, 60, 70}, false, false, false, false, Lobby, 0.5f, true};
	case 12: return {Siding, {222, 222, 214}, Punched, 40, 86, 30, 86, White, 4, 2, {58, 68, 78}, false, false, true, false, House, 0.35f, false};
	default: return {Corrugated, {130, 140, 150}, Strip, 10, 116, 6, 30, {90, 95, 100}, 2, 4, {80, 95, 105}, false, false, false, false, Industrial, 0.12f, true};
	}
}

// glass with its reflection, a curtain or blinds by window; t 0 top to 1 bottom of the pane
C Glass (const Look &L, float t, uint32_t wh, int x, int y)
{
	float k = 0.85f + 0.3f * F01 (wh);
	C g = L.glass * (k * (1.25f - 0.45f * t)) + C {18, 20, 24} * (Fbm (x / 30.0f, y / 30.0f, 91, 2) - 0.5f);
	uint32_t kind = (wh >> 8) % 10;
	if (kind < 2 && t < 0.35f) g = Mix (g, C {170, 160, 140}, 0.55f);
	else if (kind == 2 && (y % 5) < 2) g = Mix (g, C {190, 190, 185}, 0.35f);
	return g;
}

C NightLight (const Look &L, float t, uint32_t wh, int y)
{
	if (F01 (H (wh * 7u + 3u)) >= L.lit) return Black;
	float k = 0.45f + 0.55f * F01 (H (wh + 11u));
	uint32_t kind = (wh >> 8) % 10;
	C c = L.cool && F01 (H (wh + 5u)) < 0.75f ? C {225, 232, 255} : C {255, 196, 130};
	if (F01 (H (wh + 9u)) < 0.06f) c = C {120, 150, 255}; // a screen
	if (kind < 2 && t < 0.35f) k *= 1.2f;
	if (kind == 2 && (y % 5) < 2) k *= 0.5f;
	return c * (k * (0.8f + 0.2f * t));
}

// one pixel of an upper floor, cx 0..125 across the bay, cy 0..121 down the floor
Px Upper (const Look &L, int reg, int bay, int floor, int cx, int cy, int x, int y)
{
	Px p;
	p.emi = Black;
	p.dif = Wall (L.mat, L.wall, x, y, reg * 17, p.h);
	uint32_t wh = H3 (reg, bay, floor);
	auto inWin = [&] (int x0, int x1, int y0, int y1) { return cx >= x0 && cx < x1 && cy >= y0 && cy < y1; };
	if (L.win == Curtain) {
		bool mull = cx < L.fw || cx >= 126 - L.fw || (L.bars == 1 && std::abs (cx - 63) < L.fw) || (L.bars == 4 && (cx % 42) < L.fw);
		if (cy >= L.wy1) { p.dif = L.wall * (0.9f + 0.1f * F01 (H3 (reg, bay, floor + 77))); p.h = 0.45f; return p; } // spandrel
		if (mull || cy < 2) { p.dif = L.frame; p.h = 0.55f; return p; }
		float t = (float)cy / L.wy1;
		p.dif = Glass (L, t, wh, x, y); p.h = 0.4f;
		p.emi = NightLight (L, t, wh, cy);
		return p;
	}
	if (L.win == Ribbon || L.win == Strip) {
		if (!inWin (L.wx0, L.wx1, L.wy0, L.wy1)) return p;
		int lx = cx - L.wx0, ly = cy - L.wy0, w = L.wx1 - L.wx0, h = L.wy1 - L.wy0;
		if (lx < L.fw || ly < L.fw || ly >= h - L.fw || (lx % 42) < L.fw || lx >= w - L.fw) { p.dif = L.frame; p.h = 0.35f; return p; }
		float t = (float)ly / h;
		p.dif = Glass (L, t, wh + (uint32_t)(lx / 42), x, y); p.h = 0.15f;
		p.emi = NightLight (L, t, wh + (uint32_t)(lx / 42) * 131u, cy);
		return p;
	}
	// punched windows: lintel, sill, shutters, balcony
	if (L.lintel && inWin (L.wx0 - 4, L.wx1 + 4, L.wy0 - 7, L.wy0)) { p.dif = StoneC * (0.9f + 0.1f * Fbm (x / 6.0f, y / 6.0f, 5, 2)); p.h = 0.62f; return p; }
	if (L.sill && inWin (L.wx0 - 4, L.wx1 + 4, L.wy1, L.wy1 + 5)) { p.dif = (cy == L.wy1 + 4 ? StoneC * 0.6f : StoneC); p.h = 0.68f; return p; }
	if (L.shutters && (bay & 1) == 0 && (inWin (L.wx0 - 15, L.wx0 - 1, L.wy0, L.wy1) || inWin (L.wx1 + 1, L.wx1 + 15, L.wy0, L.wy1))) {
		p.dif = C {50, 80, 60} * ((cy % 4) < 1 ? 0.7f : 1.0f); p.h = 0.58f; return p;
	}
	if (L.balcony && (bay & 1) == 0 && cx >= 14 && cx < 112 && cy >= 80 && cy < 104) {
		p.dif = L.wall * (cy >= 101 ? 0.55f : 0.92f); p.h = 0.8f;
		return p;
	}
	if (!inWin (L.wx0, L.wx1, L.wy0, L.wy1)) {
		if (L.balcony && (bay & 1) == 0 && cx >= 14 && cx < 112 && cy >= 104 && cy < 110) p.dif = p.dif * 0.6f; // its shadow
		return p;
	}
	int lx = cx - L.wx0, ly = cy - L.wy0, w = L.wx1 - L.wx0, h = L.wy1 - L.wy0;
	bool frame = lx < L.fw || ly < L.fw || lx >= w - L.fw || ly >= h - L.fw;
	bool bar = (L.bars >= 1 && L.bars <= 3 && std::abs (lx - w / 2) < 1 + L.fw / 2) || (L.bars == 2 && std::abs (ly - h * 2 / 5) < 1 + L.fw / 2)
		|| (L.bars == 3 && (std::abs (ly - h / 3) < 1 || std::abs (ly - h * 2 / 3) < 1)) || (L.bars == 4 && (lx % 27) < 2);
	if (frame || bar) { p.dif = L.frame; p.h = 0.36f; return p; }
	float t = (float)ly / h;
	p.dif = Glass (L, t, wh, x, y); p.h = ly < 6 ? 0.14f : 0.2f;
	if (ly < 6) p.dif = p.dif * 0.65f; // the reveal's shadow under the lintel
	p.emi = NightLight (L, t, wh, cy);
	return p;
}

const C SignColours[6] = {{180, 40, 40}, {40, 70, 150}, {40, 120, 70}, {210, 170, 40}, {230, 230, 225}, {40, 40, 45}};

// one pixel of the ground floor, cy 0..163 down from its top
Px Ground1 (const Look &L, int reg, int bay, int cx, int cy, int x, int y)
{
	Px p;
	p.emi = Black;
	p.dif = Wall (L.mat, L.wall, x, y, reg * 17 + 3, p.h);
	uint32_t wh = H3 (reg, bay, 999);
	switch (L.ground) {
	case Shops: {
		if (cx < 10 || cx >= 116) return p; // piers
		if (cy < 28) {                      // sign band
			C s = SignColours[wh % 6];
			bool inner = cx > 18 && cx < 108 && cy > 6 && cy < 22;
			p.dif = inner ? Mix (s, White, 0.25f) : s * 0.85f; p.h = 0.6f;
			if ((wh >> 4) % 2) p.emi = inner ? s * 1.1f : s * 0.5f;
			return p;
		}
		if (cy >= 150) { p.dif = StoneC * 0.8f; p.h = 0.55f; return p; } // stall riser
		bool door = (bay % 4) == 2 && cx >= 40 && cx < 86;
		int lx = cx - 10, ly = cy - 28;
		if (lx < 3 || cx >= 113 || ly < 3 || (door && (cx < 43 || cx >= 83)) || (!door && std::abs (lx - 53) < 2)) { p.dif = DarkFrame; p.h = 0.38f; return p; }
		float t = (float)ly / 122;
		p.dif = Glass (L, t, wh, x, y) * (door ? 0.75f : 1.0f); p.h = 0.2f;
		if (F01 (H (wh + 1u)) < 0.72f) p.emi = C {255, 232, 190} * (0.6f + 0.2f * t);
		return p;
	}
	case Lobby: {
		if (cy < 10) { p.dif = DarkFrame * 1.4f; p.h = 0.55f; return p; }
		bool door = (bay == 3 || bay == 4) && cy >= 60;
		if ((cx % 42) < 3 || cy < 13 || (door && (cx % 63) < 4)) { p.dif = DarkFrame; p.h = 0.4f; return p; }
		float t = (float)(cy - 10) / 154;
		p.dif = Glass (L, t, wh + (uint32_t)(cx / 42), x, y) * (door ? 0.8f : 1.0f); p.h = 0.2f;
		if (door || F01 (H (wh + (uint32_t)(cx / 42) * 7u)) < 0.6f) p.emi = C {240, 230, 205} * (0.45f + 0.15f * F01 (H (wh + (uint32_t)(cx / 42))));
		return p;
	}
	case House: {
		if (bay == 5 || bay == 6) { // a double garage door across bays 5 and 6
			int gx = (bay - 5) * 126 + cx;
			if (gx >= 14 && gx < 238 && cy >= 34) { bool g = (cy - 34) % 16 < 2; p.dif = White * (g ? 0.75f : 0.95f); p.h = g ? 0.4f : 0.45f; }
			return p;
		}
		if (bay == 1 && cx >= 42 && cx < 84 && cy >= 30) {
			bool panel = (cx - 42) % 21 > 4 && (cx - 42) % 21 < 17 && (cy - 30) % 44 > 6;
			p.dif = C {120, 40, 35} * (panel ? 1.0f : 0.8f); p.h = 0.4f; return p;
		}
		break;
	}
	case Industrial: {
		bool roller = bay == 2 || bay == 3 || bay == 6 || bay == 7;
		int rx = (bay == 3 || bay == 7) ? cx + 126 : cx;
		if (roller && rx >= 14 && rx < 238 && cy >= 24) { bool g = (cy % 8) < 2; p.dif = C {150, 155, 160} * (g ? 0.7f : 1.0f); p.h = g ? 0.42f : 0.46f; return p; }
		if (bay == 5 && cx >= 46 && cx < 80 && cy >= 80) { p.dif = C {60, 90, 130}; p.h = 0.42f; return p; }
		if (bay == 5 && cx >= 40 && cx < 86 && cy >= 74 && cy < 80) { p.dif = DarkFrame; p.h = 0.6f; return p; }
		return p;
	}
	case Door: break;
	}
	// residential ground floor: windows like above, an entrance in bay 3 with a canopy
	if (bay == 3 && cx >= 36 && cx < 90) {
		if (cy >= 26 && cy < 34) { p.dif = DarkFrame * 1.2f; p.h = 0.8f; return p; }
		if (cy >= 34) {
			bool glass = cx >= 46 && cx < 80 && cy >= 44 && cy < 100;
			if (cx < 40 || cx >= 86 || cy < 38) { p.dif = DarkFrame; p.h = 0.38f; return p; }
			p.dif = glass ? Glass (L, (cy - 44) / 56.0f, wh, x, y) : C {95, 65, 45} * (0.9f + 0.1f * Fbm (x / 3.0f, y / 40.0f, 3, 2));
			p.h = 0.25f;
			if (glass && F01 (H (wh + 2u)) < 0.7f) p.emi = C {255, 200, 140} * 0.7f;
			return p;
		}
		return p;
	}
	return Upper (L, reg, bay, 50, cx, std::clamp (cy - 30, 0, 121), x, y);
}

// ---- roofs

const C RoofTiles[4] = {{160, 72, 52}, {72, 76, 86}, {122, 84, 60}, {98, 70, 62}};

Px Tiles (int var, int x, int y)
{
	Px p;
	p.emi = Black;
	int row = y / 27, ty = y % 27, off = (row & 1) * 10, ti = (x + off) / 21, tx = (x + off) % 21;
	float j = 0.86f + 0.24f * F01 (H3 (ti, row, var));
	float shade = 0.62f + 0.38f * (ty / 26.0f); // dark under the row above, light at the lower edge
	if (tx == 20) shade *= 0.75f;
	p.dif = RoofTiles[var] * (j * shade * (0.95f + 0.1f * Fbm (x / 40.0f, y / 40.0f, 13 + var, 3)));
	p.h = 0.3f + 0.4f * (ty / 26.0f) - (tx == 20 ? 0.05f : 0.0f);
	return p;
}

Px FlatRoof (int x, int y, int S)
{
	Px p;
	p.emi = Black;
	float n = Fbm (x / 64.0f, y / 64.0f, 21, 4, S / 64), st = Fbm (x / 128.0f, y / 128.0f, 33, 3, S / 128);
	float speck = 0.94f + 0.12f * F01 (H3 (x, y, 5));
	float k = (0.9f + 0.2f * n) * speck * (1.0f - 0.25f * std::max (0.0f, st - 0.55f) * 2.2f);
	p.h = 0.5f;
	if (x % 128 < 2 || y % 128 < 2) { k *= 0.84f; p.h = 0.58f; }
	p.dif = C {150, 150, 144} * k;
	return p;
}

// ---- images out

// the next mip level by 2 x 2 boxes: never mixes two 1024 regions
std::vector<uint8_t> Half (const std::vector<uint8_t> &s, int w, int h)
{
	int W = std::max (1, w / 2), Hh = std::max (1, h / 2);
	std::vector<uint8_t> d ((size_t)W * Hh * 4);
	for (int y = 0; y < Hh; y++)
		for (int x = 0; x < W; x++) {
			int sx = std::min (w - 1, 2 * x), sy = std::min (h - 1, 2 * y), sx1 = std::min (w - 1, sx + 1), sy1 = std::min (h - 1, sy + 1);
			for (int c = 0; c < 4; c++) {
				int v = s[((size_t)sy * w + sx) * 4 + c] + s[((size_t)sy * w + sx1) * 4 + c] + s[((size_t)sy1 * w + sx) * 4 + c] + s[((size_t)sy1 * w + sx1) * 4 + c];
				d[((size_t)y * W + x) * 4 + c] = (uint8_t)((v + 2) / 4);
			}
		}
	return d;
}

struct Area { int x0, y0, x1, y1, gt, gb; }; // gutter above and below: half where two bands share one
std::vector<Area> Areas () // every padded area of the atlas, level 0 pixels
{
	std::vector<Area> a;
	for (int r = 0; r < 16; r++) {
		int rx = (r % 4) * Region, ry = (r / 4) * Region;
		if (r < NFacade) {
			a.push_back ({rx + Gutter, ry + UpperY, rx + Region - Gutter, ry + UpperY + UpperFloors * FloorPx, Gutter, Gutter / 2});
			a.push_back ({rx + Gutter, ry + PlainY, rx + Region - Gutter, ry + PlainY + PlainPx, Gutter / 2, Gutter / 2});
			a.push_back ({rx + Gutter, ry + GroundY, rx + Region - Gutter, ry + GroundY + GroundPx, Gutter / 2, Gutter});
		} else
			for (int v = 0; v < 2; v++) a.push_back ({rx + Gutter, ry + RoofY[v], rx + Region - Gutter, ry + RoofY[v] + RoofPx, Gutter, Gutter});
	}
	return a;
}

// the gutters of every area again at a level that is 1/scale of the atlas
void RePad (std::vector<uint8_t> &img, int w, int scale, const std::vector<Area> &areas)
{
	int g = Gutter / scale;
	if (g < 1) return;
	for (const Area &A : areas) {
		int x0 = A.x0 / scale, y0 = A.y0 / scale, x1 = A.x1 / scale, y1 = A.y1 / scale, gt = A.gt / scale, gb = A.gb / scale;
		for (int y = y0 - gt; y < y1 + gb; y++)
			for (int x = x0 - g; x < x1 + g; x++) {
				if (x >= x0 && x < x1 && y >= y0 && y < y1) continue;
				int sx = std::clamp (x, x0, x1 - 1), sy = std::clamp (y, y0, y1 - 1);
				memcpy (&img[((size_t)y * w + x) * 4], &img[((size_t)sy * w + sx) * 4], 4);
			}
	}
}

void Put32 (std::string &s, size_t at, uint32_t v) { memcpy (&s[at], &v, 4); }

// DXT1 DDS with the full mip chain; the atlas's gutters re-padded at each level
bool WriteDds (const std::string &path, std::vector<uint8_t> img, int w, int h, const std::vector<Area> *areas, int scale)
{
	int levels = 1;
	while (std::max (w, h) >> levels) levels++;
	std::string out (128, '\0');
	memcpy (&out[0], "DDS ", 4);
	Put32 (out, 4, 124);
	Put32 (out, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
	Put32 (out, 12, (uint32_t)h); Put32 (out, 16, (uint32_t)w);
	Put32 (out, 20, (uint32_t)(std::max (1, (w + 3) / 4) * std::max (1, (h + 3) / 4) * 8));
	Put32 (out, 28, (uint32_t)levels);
	Put32 (out, 76, 32); Put32 (out, 80, 0x4);
	memcpy (&out[84], "DXT1", 4);
	Put32 (out, 108, 0x1000 | 0x8 | 0x400000);
	int lw = w, lh = h;
	for (int L = 0; L < levels; L++) {
		if (L) {
			img = Half (img, lw, lh);
			lw = std::max (1, lw / 2); lh = std::max (1, lh / 2); scale *= 2;
			if (areas) RePad (img, lw, scale, *areas);
		}
		int bw = std::max (1, (lw + 3) / 4), bh = std::max (1, (lh + 3) / 4);
		size_t at = out.size();
		out.resize (at + (size_t)bw * bh * 8);
		unsigned char blk[64];
		for (int by = 0; by < bh; by++)
			for (int bx = 0; bx < bw; bx++) {
				for (int i = 0; i < 16; i++) {
					int x = std::min (bx * 4 + (i & 3), lw - 1), y = std::min (by * 4 + (i >> 2), lh - 1);
					memcpy (blk + 4 * i, &img[((size_t)y * lw + x) * 4], 4);
					blk[4 * i + 3] = 255;
				}
				stb_compress_dxt_block ((unsigned char *)&out[at + ((size_t)by * bw + bx) * 8], blk, 0, STB_DXT_HIGHQUAL);
			}
	}
	FILE *f = fopen (path.c_str(), "wb");
	if (!f) return false;
	bool ok = fwrite (out.data(), 1, out.size(), f) == out.size();
	return fclose (f) == 0 && ok;
}

// tangent-space normals from heights: r g from -1..1, b the z as it is (PBR.fx remaps only rg)
std::vector<uint8_t> Normals (const std::vector<float> &h, int w, int hh, float k, bool wrap)
{
	std::vector<uint8_t> n ((size_t)w * hh * 4, 255);
	auto at = [&] (int x, int y) {
		if (wrap) { x = (x % w + w) % w; y = (y % hh + hh) % hh; }
		else { x = std::clamp (x, 0, w - 1); y = std::clamp (y, 0, hh - 1); }
		return h[(size_t)y * w + x];
	};
	for (int y = 0; y < hh; y++)
		for (int x = 0; x < w; x++) {
			float dx = (at (x + 1, y) - at (x - 1, y)) * k, dy = (at (x, y + 1) - at (x, y - 1)) * k;
			float nx = -dx, ny = -dy, l = std::sqrt (nx * nx + ny * ny + 1.0f);
			uint8_t *p = &n[((size_t)y * w + x) * 4];
			p[0] = (uint8_t)std::lround ((nx / l * 0.5f + 0.5f) * 255);
			p[1] = (uint8_t)std::lround ((ny / l * 0.5f + 0.5f) * 255);
			p[2] = (uint8_t)std::lround (1.0f / l * 255);
		}
	return n;
}

void Png (const std::string &path, const std::vector<uint8_t> &img, int w, int h, int down)
{
	QImage q ((const uchar *)img.data(), w, h, w * 4, QImage::Format_RGBA8888);
	q.scaled (w / down, h / down, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat (QImage::Format_RGB888).save (QString::fromStdString (path));
}

} // namespace

int main (int argc, char **argv)
{
	if (argc < 2) { fprintf (stderr, "usage: bldgtex <Textures/Bldg folder> [--png <preview folder>]\n"); return 2; }
	std::string out = argv[1], png = argc >= 4 && !strcmp (argv[2], "--png") ? argv[3] : "";

	Canvas A (Atlas, Atlas);
	for (int r = 0; r < 16; r++) {
		int rx = (r % 4) * Region, ry = (r / 4) * Region;
		if (r < NFacade) {
			Look L = Looks (r);
			for (int y = UpperY; y < UpperY + UpperFloors * FloorPx; y++)
				for (int x = Gutter; x < Region - Gutter; x++)
					A.Set (rx + x, ry + y, Upper (L, r, (x - Gutter) / BayPx, (y - UpperY) / FloorPx, (x - Gutter) % BayPx, (y - UpperY) % FloorPx, x, y));
			for (int y = PlainY; y < PlainY + PlainPx; y++)
				for (int x = Gutter; x < Region - Gutter; x++) {
					Px p;
					p.emi = Black;
					p.dif = Wall (L.win == Curtain ? Metal : L.mat, L.wall, x, y, r * 17 + 1, p.h);
					if (y >= PlainY + PlainPx - 4) p.dif = p.dif * 0.85f; // a line under the gable or parapet
					A.Set (rx + x, ry + y, p);
				}
			for (int y = GroundY; y < GroundY + GroundPx; y++)
				for (int x = Gutter; x < Region - Gutter; x++)
					A.Set (rx + x, ry + y, Ground1 (L, r, (x - Gutter) / BayPx, (x - Gutter) % BayPx, y - GroundY, x, y));
		} else
			for (int v = 0; v < 2; v++)
				for (int y = RoofY[v]; y < RoofY[v] + RoofPx; y++)
					for (int x = Gutter; x < Region - Gutter; x++)
						A.Set (rx + x, ry + y, Tiles ((r - RoofRegion) * 2 + v, x - Gutter, y - RoofY[v]));
	}
	std::vector<Area> areas = Areas();
	for (const Area &a : areas) A.Pad (a.x0, a.y0, a.x1, a.y1, Gutter, a.gt, a.gb);

	std::vector<uint8_t> nrm = Normals (A.hgt, Atlas, Atlas, 3.0f, false);
	std::vector<uint8_t> emiH = Half (A.emi, Atlas, Atlas), nrmH = Half (nrm, Atlas, Atlas);
	RePad (emiH, Atlas / 2, 2, areas); RePad (nrmH, Atlas / 2, 2, areas);
	bool ok = WriteDds (out + "/facade.dds", A.dif, Atlas, Atlas, &areas, 1)
		&& WriteDds (out + "/facade_emis.dds", emiH, Atlas / 2, Atlas / 2, &areas, 2)
		&& WriteDds (out + "/facade_norm.dds", nrmH, Atlas / 2, Atlas / 2, &areas, 2);

	const int S = 1024;
	Canvas Rf (S, S);
	for (int y = 0; y < S; y++)
		for (int x = 0; x < S; x++) Rf.Set (x, y, FlatRoof (x, y, S));
	std::vector<uint8_t> rn = Normals (Rf.hgt, S, S, 3.0f, true);
	ok = ok && WriteDds (out + "/roof.dds", Rf.dif, S, S, nullptr, 1) && WriteDds (out + "/roof_norm.dds", rn, S, S, nullptr, 1);

	if (!png.empty()) {
		Png (png + "/facade.png", A.dif, Atlas, Atlas, 2);
		Png (png + "/facade_emis.png", A.emi, Atlas, Atlas, 2);
		Png (png + "/facade_norm.png", nrm, Atlas, Atlas, 2);
		Png (png + "/roof.png", Rf.dif, S, S, 1);
	}
	if (!ok) { fprintf (stderr, "bldgtex: cannot write %s\n", out.c_str()); return 1; }
	return 0;
}
