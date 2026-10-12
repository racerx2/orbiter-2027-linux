// not upstream: tile pixels for the tile stream: source grids, paste, resample, water mask, DXT1 DDS with mips (TileStream.h)

#include "TileStream.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "stb_dxt.h"

namespace TsImg {

int Size (int L)
{
	return L <= 1 ? 128 : L == 2 ? 256 : 512;
}

// degrees: west edge, north edge, width, height; L1-L3 cover the globe
static void Bounds (int L, int ilat, int ilng, double &w, double &n, double &dw, double &dh)
{
	if (L < 4) { w = -180.0; n = 90.0; dw = 360.0; dh = 180.0; return; }
	dh = 180.0 / (double)(1 << (L - 4));
	dw = 360.0 / (double)(1 << (L - 3));
	n = 90.0 - ilat * dh;
	w = -180.0 + ilng * dw;
}

Grid EoxGrid (int L, int ilat, int ilng)
{
	if (L < 4) return {1, 0, 0, 2, 4};
	return {L - 3, 2 * ilat, 2 * ilng, 2, 2};
}

int GibsLevel (int L, int nmax)
{
	return std::max (0, std::min (L - 3, nmax));
}

// GIBS EPSG:4326: level n tiles span 288/2^n degrees from (-180, 90), 512 px; the globe is 640*2^n x 320*2^n px
struct Sampler {
	double west, north, dw, dh, pix, W, H;
	int S;
	Sampler (int L, int ilat, int ilng, int gn) {
		Bounds (L, ilat, ilng, west, north, dw, dh);
		pix = 288.0 / (double)(1 << gn) / 512.0;
		W = 640.0 * (1 << gn);
		H = 320.0 * (1 << gn);
		S = Size (L);
	}
	double X (int px) const { return std::clamp ((west + (px + 0.5) * dw / S + 180.0) / pix - 0.5, 0.0, W - 1.0); }
	double Y (int py) const { return std::clamp ((90.0 - (north - (py + 0.5) * dh / S)) / pix - 0.5, 0.0, H - 1.0); }
};

Grid GibsGrid (int L, int ilat, int ilng, int nmax)
{
	int gn = GibsLevel (L, nmax);
	Sampler s (L, ilat, ilng, gn);
	int x0 = (int)s.X (0), x1 = std::min ((int)s.X (s.S - 1) + 1, (int)s.W - 1);
	int y0 = (int)s.Y (0), y1 = std::min ((int)s.Y (s.S - 1) + 1, (int)s.H - 1);
	return {gn, y0 / 512, x0 / 512, y1 / 512 - y0 / 512 + 1, x1 / 512 - x0 / 512 + 1};
}

Grid SrcGrid (const TsSource &s, int L, int ilat, int ilng)
{
	return s.kind == "eox" ? EoxGrid (L, ilat, ilng) : GibsGrid (L, ilat, ilng, s.nmax);
}

std::string Url (const std::string &tmpl, int z, int row, int col)
{
	std::string u = tmpl;
	auto rep = [&u] (const std::string &k, int v) {
		for (size_t p; (p = u.find (k)) != std::string::npos; ) u.replace (p, k.size(), std::to_string (v));
	};
	rep ("{z}", z);
	rep ("{n}", z);
	rep ("{row}", row);
	rep ("{col}", col);
	return u;
}

bool IsFill (const QImage &img, const int rgb[3])
{
	if (img.isNull()) return false;
	QImage c = img.convertToFormat (QImage::Format_RGB888);
	for (int y = 0; y < c.height(); y++) {
		const uchar *p = c.constScanLine (y);
		for (int x = 0; x < c.width(); x++, p += 3)
			if (std::abs (p[0] - rgb[0]) > 1 || std::abs (p[1] - rgb[1]) > 1 || std::abs (p[2] - rgb[2]) > 1) return false;
	}
	return true;
}

QImage Eox (const std::vector<QImage> &tiles, const Grid &g, int L)
{
	QImage out (g.ncol * 256, g.nrow * 256, QImage::Format_RGB888);
	out.fill (Qt::black);
	for (int r = 0; r < g.nrow; r++)
		for (int c = 0; c < g.ncol; c++) {
			size_t i = (size_t)(r * g.ncol + c);
			if (i >= tiles.size() || tiles[i].isNull()) continue;
			QImage t = tiles[i].convertToFormat (QImage::Format_RGB888);
			if (t.width() != 256 || t.height() != 256) t = t.scaled (256, 256, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
			for (int y = 0; y < 256; y++)
				memcpy (out.scanLine (r * 256 + y) + c * 256 * 3, t.constScanLine (y), 256 * 3);
		}
	int S = Size (L);
	if (out.width() != S || out.height() != S) out = out.scaled (S, S, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	return out;
}

QImage Gibs (int L, int ilat, int ilng, const Grid &g, const std::vector<QImage> &tiles)
{
	static const uchar zero[3] = {0, 0, 0};
	std::vector<QImage> t (tiles.size());
	for (size_t i = 0; i < tiles.size(); i++)
		if (!tiles[i].isNull()) t[i] = tiles[i].convertToFormat (QImage::Format_RGB888);
	Sampler s (L, ilat, ilng, g.z);
	auto px = [&] (int x, int y) -> const uchar* {
		int c = x / 512 - g.col0, r = y / 512 - g.row0;
		if (c < 0 || r < 0 || c >= g.ncol || r >= g.nrow) return zero;
		const QImage &im = t[(size_t)(r * g.ncol + c)];
		int lx = x % 512, ly = y % 512;
		if (im.isNull() || lx >= im.width() || ly >= im.height()) return zero;
		return im.constScanLine (ly) + lx * 3;
	};
	std::vector<double> xs (s.S);
	for (int i = 0; i < s.S; i++) xs[i] = s.X (i);
	QImage out (s.S, s.S, QImage::Format_RGB888);
	for (int py = 0; py < s.S; py++) {
		double gy = s.Y (py);
		int y0 = (int)gy, y1 = std::min (y0 + 1, (int)s.H - 1);
		double fy = gy - y0;
		uchar *o = out.scanLine (py);
		for (int ox = 0; ox < s.S; ox++, o += 3) {
			double gx = xs[ox];
			int x0 = (int)gx, x1 = std::min (x0 + 1, (int)s.W - 1);
			double fx = gx - x0;
			const uchar *a = px (x0, y0), *b = px (x1, y0), *c = px (x0, y1), *e = px (x1, y1);
			for (int k = 0; k < 3; k++)
				o[k] = (uchar)std::lround ((a[k] * (1 - fx) + b[k] * fx) * (1 - fy) + (c[k] * (1 - fx) + e[k] * fx) * fy);
		}
	}
	return out;
}

QImage Build (const TsSource &s, int L, int ilat, int ilng, const Grid &g, const std::vector<QImage> &tiles)
{
	return s.kind == "eox" ? Eox (tiles, g, L) : Gibs (L, ilat, ilng, g, tiles);
}

QImage Mask (const QImage &lights, const QImage &water, const int black[3], double grey)
{
	QImage l = lights.convertToFormat (QImage::Format_RGB888), w = water.convertToFormat (QImage::Format_RGB888);
	int S = l.width();
	QImage out (S, S, QImage::Format_RGBA8888);
	for (int y = 0; y < S; y++) {
		const uchar *lp = l.constScanLine (y), *wp = w.constScanLine (y);
		uchar *o = out.scanLine (y);
		for (int x = 0; x < S; x++, lp += 3, wp += 3, o += 4) {
			if (wp[0] >= grey) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
			for (int k = 0; k < 3; k++) o[k] = (uchar)std::max (0, lp[k] - black[k]);
			o[3] = 255;
		}
	}
	return out;
}

QImage Region (const QImage &anc, int depth, int ilat, int ilng)
{
	if (anc.isNull() || depth < 1) return QImage();
	int m = (1 << depth) - 1;
	double sub = (double)anc.width() / (1 << depth);
	int x = (int)((ilng & m) * sub), y = (int)((ilat & m) * sub), n = std::max (1, (int)std::ceil (sub));
	return anc.copy (x, y, n, n).convertToFormat (QImage::Format_RGB888).scaled (512, 512, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

static void Put32 (std::string &s, size_t at, uint32_t v)
{
	for (int i = 0; i < 4; i++) s[at + i] = (char)((v >> (8 * i)) & 0xFF);
}

static uint16_t To565 (const int *p)
{
	return (uint16_t)(((p[0] * 31 + 127) / 255) << 11 | ((p[1] * 63 + 127) / 255) << 5 | ((p[2] * 31 + 127) / 255));
}

static void From565 (uint16_t c, int *p)
{
	int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	p[0] = (r << 3) | (r >> 2);
	p[1] = (g << 2) | (g >> 4);
	p[2] = (b << 3) | (b >> 2);
}

// a block with water: 3 colours + transparent, colour0 <= colour1
static void Punch (const uchar *blk, uchar *dst)
{
	int lo[3] = {255, 255, 255}, hi[3] = {0, 0, 0}, nop = 0;
	for (int i = 0; i < 16; i++) {
		const uchar *p = blk + 4 * i;
		if (p[3] < 128) continue;
		nop++;
		for (int k = 0; k < 3; k++) { lo[k] = std::min (lo[k], (int)p[k]); hi[k] = std::max (hi[k], (int)p[k]); }
	}
	uint16_t c0 = 0, c1 = 0;
	if (nop) {
		uint16_t a = To565 (lo), b = To565 (hi);
		c0 = std::min (a, b);
		c1 = std::max (a, b);
	}
	int pal[3][3];
	From565 (c0, pal[0]);
	From565 (c1, pal[1]);
	for (int k = 0; k < 3; k++) pal[2][k] = (pal[0][k] + pal[1][k]) / 2;
	uint32_t idx = 0;
	for (int i = 0; i < 16; i++) {
		const uchar *p = blk + 4 * i;
		uint32_t s = 3;
		if (p[3] >= 128) {
			int best = 1 << 30;
			for (uint32_t j = 0; j < 3; j++) {
				int d = 0;
				for (int k = 0; k < 3; k++) d += (p[k] - pal[j][k]) * (p[k] - pal[j][k]);
				if (d < best) { best = d; s = j; }
			}
		}
		idx |= s << (2 * i);
	}
	dst[0] = (uchar)(c0 & 0xFF); dst[1] = (uchar)(c0 >> 8);
	dst[2] = (uchar)(c1 & 0xFF); dst[3] = (uchar)(c1 >> 8);
	for (int i = 0; i < 4; i++) dst[4 + i] = (uchar)((idx >> (8 * i)) & 0xFF);
}

// 2x2 box; with alpha a pixel is water when half or more of its four are
static QImage Half (const QImage &src, bool alpha)
{
	int w = std::max (1, src.width() / 2), h = std::max (1, src.height() / 2);
	QImage out (w, h, QImage::Format_RGBA8888);
	for (int y = 0; y < h; y++) {
		const uchar *r0 = src.constScanLine (std::min (2 * y, src.height() - 1));
		const uchar *r1 = src.constScanLine (std::min (2 * y + 1, src.height() - 1));
		uchar *o = out.scanLine (y);
		for (int x = 0; x < w; x++, o += 4) {
			int x0 = std::min (2 * x, src.width() - 1), x1 = std::min (2 * x + 1, src.width() - 1);
			const uchar *q[4] = {r0 + 4 * x0, r0 + 4 * x1, r1 + 4 * x0, r1 + 4 * x1};
			int sum[3] = {0, 0, 0}, n = 0;
			for (const uchar *p : q)
				if (!alpha || p[3] >= 128) { for (int k = 0; k < 3; k++) sum[k] += p[k]; n++; }
			if (alpha && n <= 2) { o[0] = o[1] = o[2] = o[3] = 0; continue; }
			for (int k = 0; k < 3; k++) o[k] = (uchar)((sum[k] + n / 2) / n);
			o[3] = 255;
		}
	}
	return out;
}

static void Blocks (const QImage &img, bool alpha, std::string &out)
{
	int w = img.width(), h = img.height(), bw = (w + 3) / 4, bh = (h + 3) / 4;
	size_t at = out.size();
	out.resize (at + (size_t)bw * bh * 8);
	uchar blk[64];
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++) {
			bool water = false;
			for (int i = 0; i < 16; i++) {
				int x = std::min (bx * 4 + (i & 3), w - 1), y = std::min (by * 4 + (i >> 2), h - 1);
				memcpy (blk + 4 * i, img.constScanLine (y) + 4 * x, 4);
				if (!alpha) blk[4 * i + 3] = 255;
				else if (blk[4 * i + 3] < 128) water = true;
			}
			uchar *dst = (uchar*)&out[at + ((size_t)by * bw + bx) * 8];
			if (water) Punch (blk, dst);
			else stb_compress_dxt_block (dst, blk, 0, STB_DXT_HIGHQUAL);
		}
}

std::string Dds (const QImage &src, bool alpha)
{
	QImage img = src.convertToFormat (QImage::Format_RGBA8888);
	int w = img.width(), h = img.height(), levels = 1;
	while (std::max (w, h) >> levels) levels++;
	std::string out (128, '\0');
	memcpy (&out[0], "DDS ", 4);
	Put32 (out, 4, 124);
	Put32 (out, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000); // caps, height, width, pixel format, mip count, linear size
	Put32 (out, 12, (uint32_t)h);
	Put32 (out, 16, (uint32_t)w);
	Put32 (out, 20, (uint32_t)(std::max (1, (w + 3) / 4) * std::max (1, (h + 3) / 4) * 8));
	Put32 (out, 28, (uint32_t)levels);
	Put32 (out, 76, 32);
	Put32 (out, 80, 0x4); // FourCC
	memcpy (&out[84], "DXT1", 4);
	Put32 (out, 108, 0x1000 | 0x8 | 0x400000); // texture, complex, mipmap
	QImage lv = img;
	for (int i = 0; i < levels; i++) {
		if (i) lv = Half (lv, alpha);
		Blocks (lv, alpha, out);
	}
	return out;
}

QImage DdsImage (const QByteArray &d)
{
	if (d.size() < 128 || memcmp (d.constData(), "DDS ", 4) || memcmp (d.constData() + 84, "DXT1", 4)) return QImage();
	auto u32 = [&d] (int at) {
		const uchar *p = (const uchar*)d.constData() + at;
		return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
	};
	int h = (int)u32 (12), w = (int)u32 (16);
	if (w <= 0 || h <= 0 || w > 8192 || h > 8192) return QImage();
	int bw = (w + 3) / 4, bh = (h + 3) / 4;
	if (d.size() < 128 + (qsizetype)bw * bh * 8) return QImage();
	QImage out (w, h, QImage::Format_RGB888);
	const uchar *b = (const uchar*)d.constData() + 128;
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++, b += 8) {
			uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
			int pal[4][3];
			From565 (c0, pal[0]);
			From565 (c1, pal[1]);
			for (int k = 0; k < 3; k++) {
				if (c0 > c1) { pal[2][k] = (2 * pal[0][k] + pal[1][k]) / 3; pal[3][k] = (pal[0][k] + 2 * pal[1][k]) / 3; }
				else { pal[2][k] = (pal[0][k] + pal[1][k]) / 2; pal[3][k] = 0; }
			}
			uint32_t idx = (uint32_t)b[4] | (uint32_t)b[5] << 8 | (uint32_t)b[6] << 16 | (uint32_t)b[7] << 24;
			for (int i = 0; i < 16; i++) {
				int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
				if (x >= w || y >= h) continue;
				const int *p = pal[(idx >> (2 * i)) & 3];
				uchar *o = out.scanLine (y) + 3 * x;
				o[0] = (uchar)p[0]; o[1] = (uchar)p[1]; o[2] = (uchar)p[2];
			}
		}
	return out;
}

} // namespace TsImg
