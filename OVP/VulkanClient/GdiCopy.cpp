// not upstream: copies between the CPU copies of 32-bit B8G8R8A8 surfaces (gdi-cpu-copy)
#include "GdiCopy.h"
#include <cstring>

namespace GdiCopy {

static inline int Diff (uint32_t a, uint32_t b, int shift) { int d = (int)((a >> shift) & 0xFF) - (int)((b >> shift) & 0xFF); return d < 0 ? -d : d; }

// in the shader: abs (c - gKey) < 0.01 with c and gKey the bytes over 255; 2/255 = 0.0078 passes, 3/255 = 0.0118 doesn't
bool Keyed (uint32_t px, uint32_t key)
{
	return Diff (px, key, 16) <= 2 && Diff (px, key, 8) <= 2 && Diff (px, key, 0) <= 2;
}

bool Opaque (const uint8_t *src, int spitch, int sx, int sy, int w, int h)
{
	for (int y = 0; y < h; y++) {
		const uint32_t *s = (const uint32_t *)(src + (size_t)(sy + y) * spitch) + sx;
		for (int x = 0; x < w; x++) if ((s[x] >> 24) != 0xFF) return false;
	}
	return true;
}

void Copy (uint8_t *dst, int dpitch, int dx, int dy, const uint8_t *src, int spitch, int sx, int sy, int w, int h)
{
	for (int y = 0; y < h; y++)
		memmove (dst + (size_t)(dy + y) * dpitch + (size_t)dx * 4, src + (size_t)(sy + y) * spitch + (size_t)sx * 4, (size_t)w * 4);
}

void CopyKeyed (uint8_t *dst, int dpitch, int dx, int dy, const uint8_t *src, int spitch, int sx, int sy, int w, int h, uint32_t key)
{
	for (int y = 0; y < h; y++) {
		uint32_t *d = (uint32_t *)(dst + (size_t)(dy + y) * dpitch) + dx;
		const uint32_t *s = (const uint32_t *)(src + (size_t)(sy + y) * spitch) + sx;
		for (int x = 0; x < w; x++) if (!Keyed (s[x], key)) d[x] = s[x];
	}
}

void Fill (uint8_t *dst, int dpitch, int x, int y, int w, int h, uint32_t argb)
{
	for (int r = 0; r < h; r++) {
		uint32_t *d = (uint32_t *)(dst + (size_t)(y + r) * dpitch) + x;
		for (int c = 0; c < w; c++) d[c] = argb;
	}
}

}
