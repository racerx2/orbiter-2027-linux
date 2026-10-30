// not upstream: copies between the CPU copies of 2D surfaces (OVP/VulkanClient/GdiCopy.cpp, design gdi-cpu-copy)

#include <catch2/catch_test_macros.hpp>
#include "GdiCopy.h"
#include <cstring>
#include <vector>

using namespace GdiCopy;

struct Img {
	int w, h, pitch;
	std::vector<uint32_t> px;
	Img (int _w, int _h, uint32_t v = 0) : w (_w), h (_h), pitch (_w * 4), px ((size_t)_w * _h, v) {}
	uint8_t *b () { return (uint8_t *)px.data (); }
	uint32_t &at (int x, int y) { return px[(size_t)y * w + x]; }
};

TEST_CASE("GdiCopy: colour key as the sketchpad shader's 0.01", "[gdicopy]")
{
	uint32_t key = 0xFF102030;
	CHECK(Keyed (0xFF102030, key));
	CHECK(Keyed (0x00102030, key));                 // alpha is not compared
	CHECK(Keyed (0xFF122030, key));                 // red +2
	CHECK(Keyed (0xFF0E2032, key));                 // red -2, blue +2
	CHECK_FALSE(Keyed (0xFF132030, key));           // red +3
	CHECK_FALSE(Keyed (0xFF101D30, key));           // green -3
	CHECK_FALSE(Keyed (0xFF102033, key));           // blue +3
	CHECK(Keyed (0xFFFFFFFF, 0xFFFDFEFF));
	CHECK_FALSE(Keyed (0xFF000000, 0xFF000300));
}

TEST_CASE("GdiCopy: copies a rectangle, keyed or not", "[gdicopy]")
{
	Img src (8, 6), dst (10, 10, 0xFF555555);
	for (int y = 0; y < 6; y++) for (int x = 0; x < 8; x++) src.at (x, y) = 0xFF000000 | (uint32_t)(y * 16 + x);
	src.at (3, 2) = 0xFFFF00FF; // the key
	Copy (dst.b (), dst.pitch, 4, 5, src.b (), src.pitch, 2, 1, 4, 3);
	for (int y = 0; y < 10; y++)
		for (int x = 0; x < 10; x++) {
			bool in = x >= 4 && x < 8 && y >= 5 && y < 8;
			CHECK(dst.at (x, y) == (in ? src.at (x - 2, y - 4) : 0xFF555555));
		}
	Img d2 (10, 10, 0xFF555555);
	CopyKeyed (d2.b (), d2.pitch, 4, 5, src.b (), src.pitch, 2, 1, 4, 3, 0xFFFF00FF);
	CHECK(d2.at (5, 6) == 0xFF555555);            // (3, 2) of the source: keyed
	CHECK(d2.at (4, 6) == src.at (2, 2));
	CHECK(d2.at (7, 7) == src.at (5, 3));
}

TEST_CASE("GdiCopy: overlap within one image", "[gdicopy]")
{
	Img a (6, 1);
	for (int x = 0; x < 6; x++) a.at (x, 0) = x;
	Copy (a.b (), a.pitch, 1, 0, a.b (), a.pitch, 0, 0, 5, 1); // the client copies the source rectangle first; a row overlapping itself still moves
	for (int x = 1; x < 6; x++) CHECK(a.at (x, 0) == (uint32_t)(x - 1));
}

TEST_CASE("GdiCopy: opaque rectangles and fills", "[gdicopy]")
{
	Img a (4, 4, 0xFF000000);
	a.at (3, 3) = 0x80FFFFFF;
	CHECK(Opaque (a.b (), a.pitch, 0, 0, 3, 3));
	CHECK_FALSE(Opaque (a.b (), a.pitch, 0, 0, 4, 4));
	Fill (a.b (), a.pitch, 1, 1, 2, 2, 0x11223344);
	CHECK(a.at (1, 1) == 0x11223344);
	CHECK(a.at (2, 2) == 0x11223344);
	CHECK(a.at (0, 0) == 0xFF000000);
	CHECK(a.at (3, 2) == 0xFF000000);
	const uint8_t *b = a.b () + (size_t)1 * a.pitch + 4; // memory order B, G, R, A: what a clear of the colour leaves in B8G8R8A8
	CHECK((b[0] == 0x44 && b[1] == 0x33 && b[2] == 0x22 && b[3] == 0x11));
}
