// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: DDS top mip to RGBA8 (claude/design-gltf-export.md 6)

#include "DdsDecode.h"
#include <cstring>

static uint32_t U32 (const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

static void Rgb565 (uint16_t c, int rgb[3])
{
	rgb[0] = ((c >> 11) & 31) * 255 / 31;
	rgb[1] = ((c >> 5) & 63) * 255 / 63;
	rgb[2] = (c & 31) * 255 / 31;
}

// colour block; four colours always for BC2 and BC3
static void ColourBlock (const uint8_t *b, bool bc1, uint8_t out[16][4])
{
	uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
	int p[4][4];
	Rgb565 (c0, p[0]); Rgb565 (c1, p[1]);
	p[0][3] = p[1][3] = 255;
	if (!bc1 || c0 > c1) {
		for (int i = 0; i < 3; i++) { p[2][i] = (2 * p[0][i] + p[1][i]) / 3; p[3][i] = (p[0][i] + 2 * p[1][i]) / 3; }
		p[2][3] = p[3][3] = 255;
	} else {
		for (int i = 0; i < 3; i++) { p[2][i] = (p[0][i] + p[1][i]) / 2; p[3][i] = 0; }
		p[2][3] = 255; p[3][3] = 0;
	}
	uint32_t idx = U32 (b + 4);
	for (int t = 0; t < 16; t++) {
		int k = (idx >> (2 * t)) & 3;
		for (int i = 0; i < 4; i++) out[t][i] = (uint8_t)p[k][i];
	}
}

static void AlphaBc3 (const uint8_t *b, uint8_t out[16][4])
{
	int a[8];
	a[0] = b[0]; a[1] = b[1];
	if (a[0] > a[1]) for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
	else { for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5; a[6] = 0; a[7] = 255; }
	uint64_t bits = 0;
	for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
	for (int t = 0; t < 16; t++) out[t][3] = (uint8_t)a[(bits >> (3 * t)) & 7];
}

static void AlphaBc2 (const uint8_t *b, uint8_t out[16][4])
{
	for (int t = 0; t < 16; t++) {
		int v = (b[t / 2] >> ((t & 1) * 4)) & 15;
		out[t][3] = (uint8_t)(v * 17);
	}
}

static bool Blocks (const uint8_t *d, size_t n, int w, int h, int kind, GxImage &out, std::string &err)
{
	size_t bw = (size_t)(w + 3) / 4, bh = (size_t)(h + 3) / 4, bs = kind == 1 ? 8 : 16;
	if (n < bw * bh * bs) { err = "DDS data shorter than its size"; return false; }
	out.w = w; out.h = h;
	out.rgba.assign ((size_t)w * h * 4, 0);
	uint8_t px[16][4];
	for (size_t by = 0; by < bh; by++)
		for (size_t bx = 0; bx < bw; bx++) {
			const uint8_t *b = d + (by * bw + bx) * bs;
			if (kind == 1) ColourBlock (b, true, px);
			else { ColourBlock (b + 8, false, px); if (kind == 2) AlphaBc2 (b, px); else AlphaBc3 (b, px); }
			for (int t = 0; t < 16; t++) {
				size_t x = bx * 4 + (t & 3), y = by * 4 + (t >> 2);
				if (x >= (size_t)w || y >= (size_t)h) continue;
				memcpy (&out.rgba[(y * w + x) * 4], px[t], 4);
			}
		}
	out.dxt1 = kind == 1;
	return true;
}

static int Shift (uint32_t m) { int s = 0; if (!m) return 0; while (!(m & 1)) { m >>= 1; s++; } return s; }

static uint8_t Chan (uint32_t v, uint32_t m)
{
	if (!m) return 0;
	int s = Shift (m);
	uint32_t mx = m >> s;
	return (uint8_t)(((v & m) >> s) * 255u / mx);
}

static bool Masked (const uint8_t *d, size_t n, int w, int h, int bpp, uint32_t pitch, const uint32_t mask[4], bool lum, bool alphaOnly, GxImage &out, std::string &err)
{
	if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) { err = "DDS bit count " + std::to_string (bpp) + " not supported"; return false; }
	size_t row = ((size_t)w * bpp + 7) / 8;
	if (pitch > row) row = pitch;
	if (n < row * (h - 1) + ((size_t)w * bpp + 7) / 8) { err = "DDS data shorter than its size"; return false; }
	out.w = w; out.h = h;
	out.rgba.assign ((size_t)w * h * 4, 255);
	int B = bpp / 8;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			const uint8_t *p = d + y * row + (size_t)x * B;
			uint32_t v = 0;
			for (int i = 0; i < B; i++) v |= (uint32_t)p[i] << (8 * i);
			uint8_t *o = &out.rgba[((size_t)y * w + x) * 4];
			if (alphaOnly) { o[0] = o[1] = o[2] = 255; o[3] = Chan (v, mask[3]); continue; }
			if (lum) { o[0] = o[1] = o[2] = Chan (v, mask[0]); }
			else { o[0] = Chan (v, mask[0]); o[1] = Chan (v, mask[1]); o[2] = Chan (v, mask[2]); }
			o[3] = mask[3] ? Chan (v, mask[3]) : 255;
		}
	return true;
}

bool GxDecodeDds (const uint8_t *d, size_t n, GxImage &out, std::string &err)
{
	out = GxImage ();
	if (n < 128 || memcmp (d, "DDS ", 4) || U32 (d + 4) != 124) { err = "not a DDS file"; return false; }
	uint32_t flags = U32 (d + 8);
	int h = (int)U32 (d + 12), w = (int)U32 (d + 16);
	uint32_t pitch = U32 (d + 20);
	uint32_t pf = U32 (d + 80), four = U32 (d + 84), bpp = U32 (d + 88);
	uint32_t mask[4] = {U32 (d + 92), U32 (d + 96), U32 (d + 100), U32 (d + 104)};
	if (w <= 0 || h <= 0 || w > 32768 || h > 32768) { err = "DDS size out of range"; return false; }
	if (!(flags & 0x8)) pitch = 0; // DDSD_PITCH
	const uint8_t *p = d + 128;
	size_t left = n - 128;
	auto fcc = [](const char *s) { return (uint32_t)s[0] | (uint32_t)s[1] << 8 | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24; };
	if (pf & 0x4) { // DDPF_FOURCC
		if (four == fcc ("DXT1")) return Blocks (p, left, w, h, 1, out, err);
		if (four == fcc ("DXT2") || four == fcc ("DXT3")) return Blocks (p, left, w, h, 2, out, err);
		if (four == fcc ("DXT4") || four == fcc ("DXT5")) return Blocks (p, left, w, h, 3, out, err);
		if (four == fcc ("DX10")) {
			if (n < 148) { err = "DDS DX10 header cut"; return false; }
			uint32_t f = U32 (d + 128);
			p = d + 148; left = n - 148;
			switch (f) {
			case 71: case 72: return Blocks (p, left, w, h, 1, out, err);
			case 74: case 75: return Blocks (p, left, w, h, 2, out, err);
			case 77: case 78: return Blocks (p, left, w, h, 3, out, err);
			case 28: case 29: { uint32_t m[4] = {0xff, 0xff00, 0xff0000, 0xff000000u}; return Masked (p, left, w, h, 32, pitch, m, false, false, out, err); }
			case 87: case 91: { uint32_t m[4] = {0xff0000, 0xff00, 0xff, 0xff000000u}; return Masked (p, left, w, h, 32, pitch, m, false, false, out, err); }
			case 88: case 93: { uint32_t m[4] = {0xff0000, 0xff00, 0xff, 0}; return Masked (p, left, w, h, 32, pitch, m, false, false, out, err); }
			default: err = "DDS DXGI format " + std::to_string (f) + " not supported"; return false;
			}
		}
		char s[5] = {(char)(four & 255), (char)(four >> 8 & 255), (char)(four >> 16 & 255), (char)(four >> 24 & 255), 0};
		err = std::string ("DDS format ") + s + " not supported";
		return false;
	}
	bool alphaPx = (pf & 0x1) != 0, alphaOnly = (pf & 0x2) != 0, rgb = (pf & 0x40) != 0, lum = (pf & 0x20000) != 0;
	if (!alphaPx && !alphaOnly) mask[3] = 0;
	if (!rgb && !lum && !alphaOnly) { err = "DDS pixel format not supported"; return false; }
	return Masked (p, left, w, h, (int)bpp, pitch, mask, lum && !rgb, alphaOnly && !rgb && !lum, out, err);
}

GxAlpha GxImageAlpha (const GxImage &img)
{
	bool below = false, partial = false;
	for (size_t i = 3; i < img.rgba.size (); i += 4) {
		uint8_t a = img.rgba[i];
		if (a < 255) { below = true; if (a > 0) { partial = true; break; } }
	}
	if (!below) return GxAlpha::Opaque;
	return partial ? GxAlpha::Blend : GxAlpha::Mask;
}
