// not upstream: copies between the CPU copies of 32-bit B8G8R8A8 surfaces, giving what oapiBlt's GPU paths give (gdi-cpu-copy).
// A pixel is a uint32_t 0xAARRGGBB in memory order B, G, R, A; pitches are in bytes.
#pragma once
#include <cstdint>

namespace GdiCopy {

// the sketchpad shader's colour key: skipped when red, green and blue are each within 0.01 of the key's (2 of 255)
bool Keyed (uint32_t px, uint32_t key);

// alpha 255 in the whole rectangle
bool Opaque (const uint8_t *src, int spitch, int sx, int sy, int w, int h);

void Copy (uint8_t *dst, int dpitch, int dx, int dy, const uint8_t *src, int spitch, int sx, int sy, int w, int h);
void CopyKeyed (uint8_t *dst, int dpitch, int dx, int dy, const uint8_t *src, int spitch, int sx, int sy, int w, int h, uint32_t key);

// the bytes a clear with the D3DCOLOR argb leaves in a B8G8R8A8 image
void Fill (uint8_t *dst, int dpitch, int x, int y, int w, int h, uint32_t argb);

}
