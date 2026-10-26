// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport: DDS decoding on hand-made files (design 10)

#include <catch2/catch_test_macros.hpp>
#include "DdsDecode.h"
#include <cstring>

namespace {

void Put32 (std::vector<uint8_t> &b, size_t at, uint32_t v) { for (int i = 0; i < 4; i++) b[at + i] = (uint8_t)(v >> (8 * i)); }

std::vector<uint8_t> Header (int w, int h, uint32_t pf, const char *four, uint32_t bpp = 0, uint32_t r = 0, uint32_t g = 0, uint32_t bl = 0, uint32_t a = 0)
{
	std::vector<uint8_t> b (128, 0);
	memcpy (b.data (), "DDS ", 4);
	Put32 (b, 4, 124);
	Put32 (b, 8, 0x1007);
	Put32 (b, 12, h); Put32 (b, 16, w);
	Put32 (b, 76, 32);
	Put32 (b, 80, pf);
	if (four) memcpy (&b[84], four, 4);
	Put32 (b, 88, bpp); Put32 (b, 92, r); Put32 (b, 96, g); Put32 (b, 100, bl); Put32 (b, 104, a);
	return b;
}

void Bc1Block (std::vector<uint8_t> &b, uint16_t c0, uint16_t c1, uint32_t idx)
{
	b.push_back (c0 & 255); b.push_back (c0 >> 8); b.push_back (c1 & 255); b.push_back (c1 >> 8);
	for (int i = 0; i < 4; i++) b.push_back ((uint8_t)(idx >> (8 * i)));
}

const uint8_t *Px (const GxImage &im, int x, int y) { return &im.rgba[((size_t)y * im.w + x) * 4]; }

uint32_t Pattern () { uint32_t idx = 0; for (int t = 0; t < 16; t++) idx |= (uint32_t)(t & 3) << (2 * t); return idx; }

}

TEST_CASE ("BC1 four colours and punch-through", "[Dds]")
{
	auto b = Header (4, 4, 0x4, "DXT1");
	Bc1Block (b, 0xF800, 0x001F, Pattern ()); // red > blue: four colours
	GxImage im;
	std::string err;
	REQUIRE (GxDecodeDds (b.data (), b.size (), im, err));
	CHECK (Px (im, 0, 0)[0] == 255); CHECK (Px (im, 0, 0)[2] == 0);
	CHECK (Px (im, 1, 0)[0] == 0); CHECK (Px (im, 1, 0)[2] == 255);
	CHECK (Px (im, 2, 0)[0] == 170); CHECK (Px (im, 2, 0)[2] == 85);
	CHECK (Px (im, 3, 0)[0] == 85); CHECK (Px (im, 3, 0)[2] == 170);
	CHECK (GxImageAlpha (im) == GxAlpha::Opaque);

	auto c = Header (4, 4, 0x4, "DXT1");
	Bc1Block (c, 0x001F, 0xF800, Pattern ()); // c0 <= c1: three colours and transparent
	REQUIRE (GxDecodeDds (c.data (), c.size (), im, err));
	CHECK (Px (im, 2, 0)[0] == 127); CHECK (Px (im, 2, 0)[2] == 127);
	CHECK (Px (im, 3, 0)[3] == 0);
	CHECK (GxImageAlpha (im) == GxAlpha::Mask);
	CHECK (im.dxt1);
}

TEST_CASE ("BC2 nibbles and BC3 alpha modes", "[Dds]")
{
	auto b = Header (4, 4, 0x4, "DXT3");
	for (int i = 0; i < 8; i++) b.push_back ((uint8_t)((2 * i) | (2 * i + 1) << 4));
	Bc1Block (b, 0x001F, 0xF800, 0); // four colours in BC2 whatever the order
	GxImage im;
	std::string err;
	REQUIRE (GxDecodeDds (b.data (), b.size (), im, err));
	for (int t = 0; t < 16; t++) CHECK (Px (im, t & 3, t >> 2)[3] == t * 17);
	CHECK (Px (im, 0, 0)[2] == 255);
	CHECK (GxImageAlpha (im) == GxAlpha::Blend);

	auto c = Header (4, 4, 0x4, "DXT5");
	c.push_back (255); c.push_back (0);
	uint64_t bits = 0;
	for (int t = 0; t < 16; t++) bits |= (uint64_t)(t & 7) << (3 * t);
	for (int i = 0; i < 6; i++) c.push_back ((uint8_t)(bits >> (8 * i)));
	Bc1Block (c, 0xFFFF, 0, 0);
	REQUIRE (GxDecodeDds (c.data (), c.size (), im, err));
	CHECK (Px (im, 0, 0)[3] == 255); CHECK (Px (im, 1, 0)[3] == 0); CHECK (Px (im, 2, 0)[3] == 218); CHECK (Px (im, 3, 0)[3] == 182);

	auto d = Header (4, 4, 0x4, "DXT5");
	d.push_back (0); d.push_back (255); // six interpolated, then 0 and 255
	for (int i = 0; i < 6; i++) d.push_back ((uint8_t)(bits >> (8 * i)));
	Bc1Block (d, 0xFFFF, 0, 0);
	REQUIRE (GxDecodeDds (d.data (), d.size (), im, err));
	CHECK (Px (im, 2, 0)[3] == 51); CHECK (Px (im, 2, 1)[3] == 0); CHECK (Px (im, 3, 1)[3] == 255);
}

TEST_CASE ("Uncompressed by masks, luminance, alpha only, DX10", "[Dds]")
{
	GxImage im;
	std::string err;
	auto a = Header (2, 1, 0x41, nullptr, 32, 0xff0000, 0xff00, 0xff, 0xff000000u);
	uint8_t px[8] = {0x30, 0x20, 0x10, 0x80, 1, 2, 3, 4}; // B G R A
	a.insert (a.end (), px, px + 8);
	REQUIRE (GxDecodeDds (a.data (), a.size (), im, err));
	CHECK (Px (im, 0, 0)[0] == 0x10); CHECK (Px (im, 0, 0)[1] == 0x20); CHECK (Px (im, 0, 0)[2] == 0x30); CHECK (Px (im, 0, 0)[3] == 0x80);

	auto b = Header (1, 1, 0x40, nullptr, 16, 0xF800, 0x07E0, 0x001F, 0);
	b.push_back (0x1F); b.push_back (0x00);
	REQUIRE (GxDecodeDds (b.data (), b.size (), im, err));
	CHECK (Px (im, 0, 0)[2] == 255); CHECK (Px (im, 0, 0)[0] == 0); CHECK (Px (im, 0, 0)[3] == 255);

	auto c = Header (1, 1, 0x40, nullptr, 24, 0xff0000, 0xff00, 0xff, 0);
	c.push_back (1); c.push_back (2); c.push_back (3);
	REQUIRE (GxDecodeDds (c.data (), c.size (), im, err));
	CHECK (Px (im, 0, 0)[0] == 3); CHECK (Px (im, 0, 0)[2] == 1);

	auto l = Header (1, 1, 0x20000, nullptr, 8, 0xff, 0, 0, 0);
	l.push_back (77);
	REQUIRE (GxDecodeDds (l.data (), l.size (), im, err));
	CHECK (Px (im, 0, 0)[1] == 77);

	auto al = Header (1, 1, 0x2, nullptr, 8, 0, 0, 0, 0xff);
	al.push_back (9);
	REQUIRE (GxDecodeDds (al.data (), al.size (), im, err));
	CHECK (Px (im, 0, 0)[3] == 9); CHECK (Px (im, 0, 0)[0] == 255);

	auto x = Header (4, 4, 0x4, "DX10");
	std::vector<uint8_t> dx (20, 0);
	Put32 (dx, 0, 71);
	x.insert (x.end (), dx.begin (), dx.end ());
	Bc1Block (x, 0xF800, 0x001F, 0);
	REQUIRE (GxDecodeDds (x.data (), x.size (), im, err));
	CHECK (Px (im, 3, 3)[0] == 255);
}

TEST_CASE ("Odd sizes, cut and unknown files", "[Dds]")
{
	GxImage im;
	std::string err;
	auto b = Header (5, 3, 0x4, "DXT1");
	Bc1Block (b, 0xF800, 0x001F, 0);
	REQUIRE_FALSE (GxDecodeDds (b.data (), b.size (), im, err)); // needs two blocks
	Bc1Block (b, 0x07E0, 0x001F, 0);
	REQUIRE (GxDecodeDds (b.data (), b.size (), im, err));
	CHECK (im.w == 5); CHECK (im.h == 3);
	CHECK (Px (im, 4, 2)[1] == 255);
	auto u = Header (4, 4, 0x4, "ATI2");
	u.resize (u.size () + 16);
	REQUIRE_FALSE (GxDecodeDds (u.data (), u.size (), im, err));
	CHECK (err.find ("ATI2") != std::string::npos);
	uint8_t junk[200] = {0};
	REQUIRE_FALSE (GxDecodeDds (junk, sizeof junk, im, err));
}
