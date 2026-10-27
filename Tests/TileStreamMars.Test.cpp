
// not upstream (mars-stream): Cloud-Optimized GeoTIFF reads in the job: LZW, 16 bits, JPEG tables, placement, offset tables by range, sampling, a cog body's tiles

#include <catch2/catch_test_macros.hpp>
#include "TileStream.h"
#include "TileStreamMarsHeads.h"
#include <QBuffer>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <unistd.h>
#include <zlib.h>

namespace fs = std::filesystem;

namespace {

struct App {
	int argc = 1;
	char arg0[20] = "TileStreamMars.Test";
	char *argv[2] = {arg0, nullptr};
	QCoreApplication app {argc, argv};
};

struct Tmp {
	fs::path root;
	Tmp ()
	{
		static int n = 0;
		root = fs::temp_directory_path() / ("tilestream-mars-" + std::to_string (getpid()) + "-" + std::to_string (n++));
		fs::remove_all (root);
		fs::create_directories (root);
	}
	~Tmp () { std::error_code ec; fs::remove_all (root, ec); }
};

bool WaitFor (const std::function<bool ()> &f, double sec)
{
	auto end = std::chrono::steady_clock::now() + std::chrono::duration<double> (sec);
	while (std::chrono::steady_clock::now() < end) {
		if (f()) return true;
		std::this_thread::sleep_for (std::chrono::milliseconds (50));
	}
	return f();
}

std::string Read (const fs::path &p)
{
	std::ifstream f (p, std::ios::binary);
	return std::string (std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>());
}

void Write (const fs::path &p, const std::string &s)
{
	fs::create_directories (p.parent_path());
	std::ofstream (p, std::ios::binary) << s;
}

QByteArray B64 (const char *s) { return QByteArray::fromBase64 (QByteArray (s)); }

std::string Deflate (const std::string &raw)
{
	uLongf n = compressBound ((uLong)raw.size());
	std::string c (n, '\0');
	REQUIRE (compress ((Bytef*)&c[0], &n, (const Bytef*)raw.data(), (uLong)raw.size()) == Z_OK);
	c.resize (n);
	return c;
}

std::string Lcg (size_t n)
{
	std::string v (n, '\0');
	uint32_t s = 12345;
	for (size_t i = 0; i < n; i++) {
		s = (s * 1103515245u + 12345u) & 0x7fffffffu;
		v[i] = (char)((s >> 16) & 255);
	}
	return v;
}

// TIFF LZW as libtiff writes it: Clear first, codes MSB first, the width one code early, Clear when the table is full
std::string LzwEnc (const std::string &in)
{
	std::string out;
	uint32_t acc = 0;
	int nacc = 0, width = 9, next = 258, w = -1;
	auto put = [&] (int code) {
		acc = acc << width | (uint32_t)code;
		nacc += width;
		while (nacc >= 8) {
			out += (char)((acc >> (nacc - 8)) & 255);
			nacc -= 8;
		}
		acc &= (1u << nacc) - 1;
	};
	std::map<std::pair<int, int>, int> dict;
	put (256);
	for (unsigned char c : in) {
		if (w < 0) { w = c; continue; }
		auto it = dict.find ({w, c});
		if (it != dict.end()) { w = it->second; continue; }
		put (w);
		dict[{w, c}] = next++;
		if (next >= (1 << width) && width < 12) width++;
		if (next == 4093) {
			put (256);
			dict.clear();
			next = 258;
			width = 9;
		}
		w = c;
	}
	if (w >= 0) {
		put (w);
		next++;
		if (next >= (1 << width) && width < 12) width++;
	}
	put (257);
	if (nacc) out += (char)((acc << (8 - nacc)) & 255);
	return out;
}

// a little-endian tiled GeoTIFF: levels halved, deflate (predictor 2 when asked), placed in degrees or equirectangular metres
struct Spec {
	int bps = 8, spp = 1, sfmt = 1, pred = 1;
	uint32_t tile = 16;
	bool metres = false;
	double cm = 0.0, R = 3396190.0;
	double x0 = 0.0, y0 = 90.0, dx = 1.0;      // the west north corner and the pixel, degrees or metres
	std::string meta, nodata;
	uint64_t tablesAt = 0;                    // the offset tables from here (past the 64 KB head: read by range); 0 after the IFDs
	std::set<std::pair<int, uint64_t>> sparse; // (level, tile) without data
};
struct Lv { uint32_t w, h; std::vector<int> v; }; // spp values a pixel, rows from the north

std::string Cog (const Spec &s, const std::vector<Lv> &lv)
{
	size_t B = (size_t)s.bps / 8;
	std::vector<std::vector<std::string>> tiles (lv.size());
	for (size_t l = 0; l < lv.size(); l++) {
		const Lv &L = lv[l];
		uint32_t nx = (L.w + s.tile - 1) / s.tile, ny = (L.h + s.tile - 1) / s.tile;
		size_t row = (size_t)s.tile * s.spp;
		for (uint32_t ty = 0; ty < ny; ty++)
			for (uint32_t tx = 0; tx < nx; tx++) {
				if (s.sparse.count ({(int)l, (uint64_t)ty * nx + tx})) { tiles[l].push_back (""); continue; }
				std::vector<uint32_t> v (row * s.tile, 0);
				for (uint32_t y = 0; y < s.tile; y++)
					for (uint32_t x = 0; x < s.tile; x++) {
						uint32_t gx = tx * s.tile + x, gy = ty * s.tile + y;
						if (gx >= L.w || gy >= L.h) continue;
						for (int k = 0; k < s.spp; k++) v[y * row + x * s.spp + k] = (uint32_t)L.v[((size_t)gy * L.w + gx) * s.spp + k];
					}
				uint32_t mask = s.bps == 8 ? 0xFFu : 0xFFFFu;
				for (size_t i = 0; i < v.size(); i++) v[i] &= mask;
				if (s.pred == 2)
					for (uint32_t y = 0; y < s.tile; y++)
						for (size_t x = row; x-- > (size_t)s.spp; ) v[y * row + x] = (v[y * row + x] - v[y * row + x - s.spp]) & mask;
				std::string raw (v.size() * B, '\0');
				for (size_t i = 0; i < v.size(); i++) {
					raw[B * i] = (char)(v[i] & 255);
					if (B == 2) raw[B * i + 1] = (char)((v[i] >> 8) & 255);
				}
				tiles[l].push_back (Deflate (raw));
			}
	}
	auto shorts = [] (const std::vector<uint32_t> &v) { std::string b; for (uint32_t x : v) { b += (char)(x & 255); b += (char)((x >> 8) & 255); } return b; };
	auto longs = [] (const std::vector<uint32_t> &v) { std::string b; for (uint32_t x : v) for (int i = 0; i < 4; i++) b += (char)((x >> (8 * i)) & 255); return b; };
	auto dbls = [] (const std::vector<double> &v) { std::string b (8 * v.size(), '\0'); memcpy (&b[0], v.data(), b.size()); return b; };
	struct E { uint16_t tag, type; uint32_t count; std::string b; size_t at = 0; };
	std::vector<std::vector<E>> ifd (lv.size());
	for (size_t l = 0; l < lv.size(); l++) {
		std::vector<E> &e = ifd[l];
		size_t nt = tiles[l].size();
		std::vector<uint32_t> cnt;
		for (const std::string &t : tiles[l]) cnt.push_back ((uint32_t)t.size());
		if (l) e.push_back ({254, 4, 1, longs ({1})});
		e.push_back ({256, 4, 1, longs ({lv[l].w})});
		e.push_back ({257, 4, 1, longs ({lv[l].h})});
		e.push_back ({258, 3, (uint32_t)s.spp, shorts (std::vector<uint32_t> ((size_t)s.spp, (uint32_t)s.bps))});
		e.push_back ({259, 3, 1, shorts ({8})});
		e.push_back ({262, 3, 1, shorts ({s.spp == 3 ? 2u : 1u})});
		e.push_back ({277, 3, 1, shorts ({(uint32_t)s.spp})});
		e.push_back ({284, 3, 1, shorts ({1})});
		e.push_back ({317, 3, 1, shorts ({(uint32_t)s.pred})});
		e.push_back ({322, 4, 1, longs ({s.tile})});
		e.push_back ({323, 4, 1, longs ({s.tile})});
		e.push_back ({324, 4, (uint32_t)nt, std::string (4 * nt, '\0')});
		e.push_back ({325, 4, (uint32_t)nt, longs (cnt)});
		e.push_back ({339, 3, (uint32_t)s.spp, shorts (std::vector<uint32_t> ((size_t)s.spp, (uint32_t)s.sfmt))});
		if (!l) {
			e.push_back ({33550, 12, 3, dbls ({s.dx, s.dx, 0.0})});
			e.push_back ({33922, 12, 6, dbls ({0.0, 0.0, 0.0, s.x0, s.y0, 0.0})});
			std::vector<uint32_t> k = s.metres
				? std::vector<uint32_t> {1, 1, 0, 6, 1024, 0, 1, 1, 1025, 0, 1, 1, 2057, 34736, 1, 1, 3075, 0, 1, 17, 3076, 0, 1, 9001, 3088, 34736, 1, 0}
				: std::vector<uint32_t> {1, 1, 0, 4, 1024, 0, 1, 2, 1025, 0, 1, 1, 2054, 0, 1, 9102, 2057, 34736, 1, 1};
			e.push_back ({34735, 3, (uint32_t)k.size(), shorts (k)});
			e.push_back ({34736, 12, 2, dbls ({s.cm, s.R})});
			if (!s.meta.empty()) e.push_back ({42112, 2, (uint32_t)s.meta.size() + 1, s.meta + '\0'});
			if (!s.nodata.empty()) e.push_back ({42113, 2, (uint32_t)s.nodata.size() + 1, s.nodata + '\0'});
		}
	}
	auto table = [] (const E &x) { return x.tag == 324 || x.tag == 325; };
	size_t pos = 8;
	std::vector<size_t> at (lv.size());
	for (size_t l = 0; l < lv.size(); l++) {
		at[l] = pos;
		pos += 2 + 12 * ifd[l].size() + 4;
		for (E &x : ifd[l])
			if (x.b.size() > 4 && !table (x)) { x.at = pos; pos += x.b.size() + (x.b.size() & 1); }
	}
	if (s.tablesAt) pos = std::max<size_t> (pos, s.tablesAt);
	for (size_t l = 0; l < lv.size(); l++)
		for (E &x : ifd[l])
			if (x.b.size() > 4 && table (x)) { x.at = pos; pos += x.b.size(); }
	for (size_t l = 0; l < lv.size(); l++) {
		std::vector<uint32_t> off;
		for (const std::string &t : tiles[l]) { off.push_back (t.empty() ? 0 : (uint32_t)pos); pos += t.size(); }
		for (E &x : ifd[l]) if (x.tag == 324) x.b = longs (off);
	}
	std::string f (pos, '\0');
	auto p16 = [&f] (size_t a, uint32_t v) { f[a] = (char)(v & 255); f[a + 1] = (char)((v >> 8) & 255); };
	auto p32 = [&f] (size_t a, uint32_t v) { for (int i = 0; i < 4; i++) f[a + i] = (char)((v >> (8 * i)) & 255); };
	f[0] = 'I'; f[1] = 'I'; p16 (2, 42); p32 (4, (uint32_t)at[0]);
	for (size_t l = 0; l < lv.size(); l++) {
		size_t a = at[l];
		p16 (a, (uint32_t)ifd[l].size());
		for (size_t i = 0; i < ifd[l].size(); i++) {
			const E &x = ifd[l][i];
			size_t e = a + 2 + 12 * i;
			p16 (e, x.tag); p16 (e + 2, x.type); p32 (e + 4, x.count);
			if (x.b.size() <= 4) memcpy (&f[e + 8], x.b.data(), x.b.size());
			else { p32 (e + 8, (uint32_t)x.at); memcpy (&f[x.at], x.b.data(), x.b.size()); }
		}
		p32 (a + 2 + 12 * ifd[l].size(), l + 1 < lv.size() ? (uint32_t)at[l + 1] : 0);
	}
	for (size_t l = 0; l < lv.size(); l++) {
		size_t i = 0;
		for (const E &x : ifd[l]) if (x.tag == 324) {
			for (const std::string &t : tiles[l]) {
				uint32_t o = (uint8_t)x.b[4 * i] | (uint8_t)x.b[4 * i + 1] << 8 | (uint8_t)x.b[4 * i + 2] << 16 | (uint32_t)(uint8_t)x.b[4 * i + 3] << 24;
				if (!t.empty()) memcpy (&f[o], t.data(), t.size());
				i++;
			}
		}
	}
	return f;
}

// a level of one value everywhere, or of v (x, y, k)
Lv Fill (uint32_t w, uint32_t h, int spp, const std::function<int (uint32_t, uint32_t, int)> &v)
{
	Lv l {w, h, std::vector<int> ((size_t)w * h * spp)};
	for (uint32_t y = 0; y < h; y++)
		for (uint32_t x = 0; x < w; x++)
			for (int k = 0; k < spp; k++) l.v[((size_t)y * w + x) * spp + k] = v (x, y, k);
	return l;
}

// a fetch from a file in memory, counting its reads
struct Mem {
	std::map<std::string, std::string> files;
	std::vector<TsCog::Req> asked;
	bool fail = false;
	TsCog::Fetch Fn ()
	{
		return [this] (std::vector<TsCog::Req> &rq) {
			for (TsCog::Req &r : rq) {
				asked.push_back (r);
				auto it = files.find (r.url);
				if (fail) r.status = 0;
				else if (it == files.end()) r.status = 404;
				else {
					r.status = 200;
					r.data = std::make_shared<QByteArray> (QByteArray::fromStdString (it->second.substr (std::min<size_t> (r.off, it->second.size()), r.len)));
				}
			}
		};
	}
};

const double Deg = M_PI / 180.0;

}

TEST_CASE ("TIFF LZW decodes libtiff's stream and our own, table clears and the KwKwK case included, into exactly its size", "[TileStreamMars]")
{
	QByteArray lt = B64 (MarsHeads::Lzw1000);
	std::string want = Lcg (1000);
	std::vector<uint8_t> out (1000);
	REQUIRE (TsCog::Lzw ((const uint8_t*)lt.constData(), (size_t)lt.size(), out.data(), out.size()));
	CHECK (std::string (out.begin(), out.end()) == want); // libtiff's: the width grows at 511 and 1023
	for (const std::string &in : {Lcg (30000), std::string (20000, 'a'), std::string ("ababababababababababcbcbcbcbcbcdedededede") + Lcg (9000)}) {
		std::string c = LzwEnc (in);
		std::vector<uint8_t> o (in.size());
		REQUIRE (TsCog::Lzw ((const uint8_t*)c.data(), c.size(), o.data(), o.size()));
		CHECK (std::string (o.begin(), o.end()) == in);
		CHECK_FALSE (TsCog::Lzw ((const uint8_t*)c.data(), c.size() / 2, o.data(), o.size())); // cut off: short
		std::vector<uint8_t> small (in.size() / 3);
		CHECK (TsCog::Lzw ((const uint8_t*)c.data(), c.size(), small.data(), small.size())); // never past the tile
	}
	uint8_t bad[3] = {0x80, 0x4B, 0x00}; // Clear, then 300: no such code yet
	std::vector<uint8_t> o (16);
	CHECK_FALSE (TsCog::Lzw (bad, 3, o.data(), o.size()));
}

TEST_CASE ("16-bit and 8-bit tiles: deflate, LZW, none; the predictor after the byte order; signed values", "[TileStreamMars]")
{
	TsCog::Ifd f;
	f.w = f.h = f.tw = f.th = 16;
	f.bps = 16;
	f.sfmt = 2;
	f.comp = 8;
	f.pred = 2;
	std::vector<int16_t> v (256);
	for (int i = 0; i < 256; i++) v[i] = (int16_t)(-3000 + 37 * (i % 16) - 900 * (i / 16));
	auto enc = [&] (bool le) {
		std::string raw (512, '\0');
		for (int y = 0; y < 16; y++)
			for (int x = 0; x < 16; x++) {
				uint16_t d = (uint16_t)(x ? v[y * 16 + x] - v[y * 16 + x - 1] : v[y * 16]);
				raw[2 * (y * 16 + x)] = (char)(le ? d & 255 : d >> 8);
				raw[2 * (y * 16 + x) + 1] = (char)(le ? d >> 8 : d & 255);
			}
		return raw;
	};
	std::vector<float> out;
	for (bool le : {true, false}) {
		REQUIRE (TsCog::DecodeAny (f, le, QByteArray::fromStdString (Deflate (enc (le))), out));
		REQUIRE (out.size() == 256);
		bool same = true;
		for (int i = 0; i < 256; i++) same = same && out[i] == v[i];
		CHECK (same);
	}
	f.comp = 5;
	REQUIRE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (LzwEnc (enc (true))), out));
	CHECK (out[255] == v[255]);
	f.comp = 1;
	f.pred = 1;
	std::string plain ((const char*)v.data(), 512);
	REQUIRE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (plain), out));
	CHECK (out[17] == v[17]);
	CHECK_FALSE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (plain.substr (0, 500)), out)); // short
	f.sfmt = 1;
	REQUIRE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (plain), out));
	CHECK (out[0] == (float)(uint16_t)v[0]); // unsigned
	f.bps = 8;
	f.pred = 2;
	f.comp = 8;
	std::string b (256, '\x02');
	REQUIRE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (Deflate (b)), out));
	CHECK (out[15] == 32);
	CHECK_FALSE (TsCog::DecodeAny (f, true, QByteArray(), out)); // a sparse tile is the caller's
	f.pred = 3;
	CHECK_FALSE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (Deflate (b)), out)); // floating point predictor: no
	f.pred = 1;
	f.bps = 32;
	CHECK_FALSE (TsCog::DecodeAny (f, true, QByteArray::fromStdString (Deflate (std::string (1024, 'x'))), out));
}

TEST_CASE ("JPEG tiles: the shared tables joined to each tile's frame, colour and grey", "[TileStreamMars]")
{
	for (int spp : {3, 1}) {
		QImage img (32, 32, spp == 3 ? QImage::Format_RGB888 : QImage::Format_Grayscale8);
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++) img.setPixelColor (x, y, spp == 3 ? QColor (8 * x, 8 * y, 128) : QColor (8 * x, 8 * x, 8 * x));
		QByteArray j;
		QBuffer buf (&j);
		buf.open (QIODevice::WriteOnly);
		REQUIRE (img.save (&buf, "JPG", 95));
		// as libtiff writes them: SOI, the quantisation and Huffman tables and EOI as JPEGTables; SOI, the frame and the scan as the tile
		QByteArray tables ("\xFF\xD8", 2), tile ("\xFF\xD8", 2);
		int at = 2;
		while (at + 4 <= j.size()) {
			uchar m = (uchar)j[at + 1];
			if (m == 0xDA) { tile += j.mid (at); break; }
			int len = ((uchar)j[at + 2] << 8) | (uchar)j[at + 3];
			QByteArray seg = j.mid (at, len + 2);
			if (m == 0xDB || m == 0xC4) tables += seg;
			else if (m < 0xE0 || m > 0xEF) tile += seg; // no APPn
			at += len + 2;
		}
		tables += QByteArray ("\xFF\xD9", 2);
		REQUIRE (tile.indexOf (QByteArray ("\xFF\xDB", 2)) < 0);
		TsCog::Ifd f;
		f.w = f.h = f.tw = f.th = 32;
		f.spp = (uint32_t)spp;
		f.comp = 7;
		f.jpegTables = tables;
		std::vector<float> out;
		REQUIRE (TsCog::DecodeAny (f, true, tile, out));
		REQUIRE (out.size() == (size_t)32 * 32 * spp);
		int worst = 0;
		for (int y = 0; y < 32; y++)
			for (int x = 0; x < 32; x++) {
				QColor c = img.pixelColor (x, y);
				int r = c.red(), g = c.green(), bl = c.blue();
				const float *o = &out[((size_t)y * 32 + x) * spp];
				worst = std::max (worst, std::abs ((int)o[0] - r));
				if (spp == 3) worst = std::max ({worst, std::abs ((int)o[1] - g), std::abs ((int)o[2] - bl)});
			}
		CHECK (worst <= 12);
		f.tw = f.th = 64;
		CHECK_FALSE (TsCog::DecodeAny (f, true, tile, out)); // not the tile's size
		f.tw = f.th = 32;
		f.jpegTables.clear();
		CHECK_FALSE (TsCog::DecodeAny (f, true, tile, out)); // the tile alone has no tables
	}
}

TEST_CASE ("The three USGS files parse from their first bytes and place their pixels", "[TileStreamMars]")
{
	std::vector<TsCog::Ifd> ifd;
	TsCog::Geo g;
	QByteArray col = B64 (MarsHeads::Colour), mola = B64 (MarsHeads::Mola), ctx = B64 (MarsHeads::Ctx);
	REQUIRE (TsCog::Parse (col, ifd, &g) == 0);
	REQUIRE (ifd.size() == 9);
	CHECK (ifd[0].w == 92160);
	CHECK (ifd[8].w == 360);
	CHECK (ifd[0].comp == 7);
	CHECK (ifd[0].spp == 3);
	CHECK (ifd[0].jpegTables.size() == 142);
	CHECK (ifd[0].off.empty()); // the tables lie past these bytes: read by range
	CHECK (ifd[0].offN == 16200);
	CHECK (ifd[0].offAt == 4160);
	CHECK (ifd[8].off.size() == 1);
	CHECK (g.deg);
	CHECK (g.x0 == 0.0);
	CHECK (g.y0 == 90.0);
	CHECK (g.dx == 1.0 / 256.0);
	CHECK (TsCog::Global (g, ifd[0].w));
	double x, y;
	REQUIRE (TsCog::ColX (g, ifd[0].w, 0.0, x));
	REQUIRE (TsCog::RowY (g, ifd[0].h, 90.0, y));
	CHECK (x == 0.0);
	CHECK (y == 0.0);
	REQUIRE (TsCog::ColX (g, ifd[0].w, -90.0, x));
	CHECK (x == -23040.0); // unwrapped: 69120 on the file
	REQUIRE (TsCog::RowY (g, ifd[0].h, -45.0, y));
	CHECK (y == 34560.0);

	REQUIRE (TsCog::Parse (mola, ifd, &g) == 0);
	REQUIRE (ifd.size() == 8);
	CHECK (ifd[0].w == 46081);
	CHECK (ifd[0].bps == 16);
	CHECK (ifd[0].sfmt == 2);
	CHECK (ifd[0].comp == 5);
	CHECK_FALSE (g.deg);
	CHECK (g.cm == 0.0);
	CHECK (g.R == 3396190.0);
	CHECK (g.hasNodata);
	CHECK (g.nodata == -32768.0);
	CHECK_FALSE (TsCog::Global (g, ifd[0].w)); // a column more than the planet: the last one is the first again
	REQUIRE (TsCog::ColX (g, ifd[0].w, 0.0, x));
	REQUIRE (TsCog::RowY (g, ifd[0].h, 0.0, y));
	CHECK (std::fabs (x - 23040.5) < 1e-2); // the centre of the pixel at (0, 0)
	CHECK (std::fabs (y - 11520.5) < 1e-2);
	REQUIRE (TsCog::ColX (g, ifd[0].w, -180.0, x));
	CHECK (std::fabs (x - 0.5) < 1e-2);
	REQUIRE (TsCog::ColX (g, ifd[0].w, 180.0, x));
	CHECK (std::fabs (x - 0.5) < 1e-2);
	CHECK_FALSE (TsCog::RowY (g, ifd[0].h, 90.2, y));

	REQUIRE (TsCog::Parse (ctx, ifd, &g) == 0);
	REQUIRE (ifd.size() == 11);
	CHECK (ifd[0].w == 444561);
	CHECK (ifd[0].offN == 503151); // 8 MB of tables: never read whole
	CHECK (ifd[0].offSz == 8);
	CHECK (ifd[0].cntSz == 4);
	CHECK (g.cm == 180.0);
	CHECK (std::fabs (g.scale - 0.00263350952653919985) < 1e-15);
	CHECK (std::fabs (g.offset - 88.6791068204489932) < 1e-12);
	REQUIRE (TsCog::ColX (g, ifd[0].w, -45.0, x)); // MC-19: 45W to 0, 30S to 0
	CHECK ((x >= 0.0 && x < 2.0));
	REQUIRE (TsCog::ColX (g, ifd[0].w, -1e-4, x));
	CHECK (x > ifd[0].w - 3.0);
	CHECK_FALSE (TsCog::ColX (g, ifd[0].w, -46.0, x));
	CHECK_FALSE (TsCog::ColX (g, ifd[0].w, 1.0, x));
	REQUIRE (TsCog::RowY (g, ifd[0].h, -30.0, y));
	CHECK (std::fabs (y - 3396190.0 * 30.0 * Deg / 6.0) < 1e-3);
	CHECK_FALSE (TsCog::RowY (g, ifd[0].h, 1.0, y));
	CHECK (TsCog::Parse (ctx.left (4000), ifd, &g) > 4000); // the IFDs past the bytes in hand
}

TEST_CASE ("Sample reads only the tiles under the points, their table entries by range, through scale, offset and no data", "[TileStreamMars]")
{
	// metres, central meridian 180, 0 to 1.6E and 0.8N to 0 at 0.01 degrees; v = 3 x - 2 y - 100, no data in a block
	Spec s;
	s.bps = 16;
	s.sfmt = 2;
	s.pred = 2;
	s.metres = true;
	s.cm = 180.0;
	double px = s.R * 0.01 * Deg;
	s.dx = px;
	s.x0 = s.R * (0.0 - 180.0) * Deg;
	s.y0 = s.R * 0.8 * Deg;
	s.meta = "<GDALMetadata>\n  <Item name=\"OFFSET\" sample=\"0\" role=\"offset\">10</Item>\n  <Item name=\"SCALE\" sample=\"0\" role=\"scale\">0.5</Item>\n</GDALMetadata>";
	s.nodata = "-32768";
	s.tablesAt = 70000;
	auto v = [] (uint32_t x, uint32_t y, int) { return x >= 100 && x < 120 && y >= 10 && y < 20 ? -32768 : (int)(3 * x) - (int)(2 * y) - 100; };
	Lv l0 = Fill (160, 80, 1, v);
	Lv l1 = Fill (80, 40, 1, [&] (uint32_t x, uint32_t y, int) { return (int)(6 * x) - (int)(4 * y) - 100; });
	s.sparse = {{0, 1}}; // the tile at (16, 0) to (32, 16) px has no data
	Mem m;
	m.files["d"] = Cog (s, {l0, l1});
	REQUIRE (m.files["d"].size() > 70000);
	std::vector<float> out;
	std::vector<uint8_t> ok;
	int spp = 0;
	// pixel centres of x 40..43, y 4..5: exact
	TsCog::Pts p {0.8 - 0.045, 0.405, -0.01, 0.01, 4, 2, 0.001};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok, &spp));
	CHECK (spp == 1);
	REQUIRE (out.size() == 8);
	for (int iy = 0; iy < 2; iy++)
		for (int ix = 0; ix < 4; ix++) {
			CHECK (ok[iy * 4 + ix] == 1);
			CHECK (std::fabs (out[iy * 4 + ix] - (0.5 * (3 * (40 + ix) - 2 * (4 + iy) - 100) + 10)) < 1e-3);
		}
	size_t ranged = 0;
	for (const TsCog::Req &r : m.asked) ranged += r.off >= 70000 && r.off < 70000 + 4 * 60;
	CHECK (ranged == 2); // one page of offsets, one of counts
	CHECK (m.asked.size() == 4); // the head, the two pages, one tile
	// the same points again: nothing read
	m.asked.clear();
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok, &spp));
	CHECK (m.asked.empty());
	// between pixel centres: bilinear
	p = {0.8 - 0.05, 0.41, -0.01, 0.01, 1, 1, 0.001};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok));
	CHECK (std::fabs (out[0] - (0.5 * (3 * 40.5 - 2 * 4.5 - 100) + 10)) < 1e-3);
	// no data block and the sparse tile: not valid; their edges from the valid side
	p = {0.8 - 0.155, 1.105, -0.01, 0.01, 1, 1, 0.001};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok));
	CHECK (ok[0] == 0);
	p = {0.8 - 0.105, 0.245, -0.01, 0.01, 1, 1, 0.001};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok));
	CHECK (ok[0] == 0);
	// coarser points: the overview, its area mean
	p = {0.8 - 0.04, 0.4, -0.04, 0.04, 2, 1, 0.02};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok));
	REQUIRE (ok[0] == 1);
	CHECK (std::fabs (out[0] - (0.5 * (6 * 19.5 - 4 * 1.5 - 100) + 10)) < 1e-3); // the box over overview x 19 and 20, y 1 and 2
	// outside the file
	p = {0.5, 2.0, -0.01, 0.01, 1, 1, 0.001};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok));
	CHECK (ok[0] == 0);
	p = {0.9, 0.5, -0.01, 0.01, 1, 1, 0.001};
	REQUIRE (TsCog::Sample ("d", p, m.Fn(), out, ok));
	CHECK (ok[0] == 0);
	// no file: none valid, read; a failed read: false
	REQUIRE (TsCog::Sample ("none", p, m.Fn(), out, ok, &spp));
	CHECK (spp == 0);
	CHECK (ok.size() == 1);
	CHECK (ok[0] == 0);
	Mem bad;
	bad.fail = true;
	CHECK_FALSE (TsCog::Sample ("e", p, bad.Fn(), out, ok));
	CHECK_FALSE (TsCog::Sample ("d", {0, 0, 0, 0, 5000, 1, 0}, m.Fn(), out, ok)); // too many points
}

TEST_CASE ("A file round the planet wraps across its seam; its pixel and the level for a footprint", "[TileStreamMars]")
{
	Spec s; // degrees, 0 to 360, 64 x 32 px of 5.625 degrees, RGB
	s.spp = 3;
	s.dx = 5.625;
	s.x0 = 0.0;
	s.y0 = 90.0;
	Lv l0 = Fill (64, 32, 3, [] (uint32_t x, uint32_t y, int k) { return k == 0 ? (int)(2 * x) : k == 1 ? (int)(4 * y) : 100; });
	Lv l1 = Fill (32, 16, 3, [] (uint32_t, uint32_t, int k) { return k == 2 ? 50 : 0; });
	Mem m;
	m.files["c"] = Cog (s, {l0, l1});
	std::vector<float> out;
	std::vector<uint8_t> ok;
	int spp = 0;
	// at 0: the first column's west edge, half way between the last column's centre and the first's
	TsCog::Pts p {90.0 - 2.8125, 0.0, 0.0, 0.0, 1, 1, 1.0};
	REQUIRE (TsCog::Sample ("c", p, m.Fn(), out, ok, &spp));
	REQUIRE (spp == 3);
	REQUIRE (ok[0] == 1);
	CHECK (std::fabs (out[0] - 0.5 * (2 * 63 + 2 * 0)) < 1e-3);
	CHECK (std::fabs (out[1] - 0.0) < 1e-3);
	CHECK (std::fabs (out[2] - 100.0) < 1e-3);
	p.lng0 = -180.0 + 2.8125; // west of 0: the file's east half
	REQUIRE (TsCog::Sample ("c", p, m.Fn(), out, ok));
	CHECK (std::fabs (out[0] - 2 * 32) < 1e-3);
	p.pix = 11.25; // as coarse as the overview: its pixels
	REQUIRE (TsCog::Sample ("c", p, m.Fn(), out, ok));
	CHECK (std::fabs (out[2] - 50.0) < 1e-3);
	CHECK (TsCog::Pixel ("c", m.Fn()) == 5.625);
	CHECK (TsCog::Pixel ("none", m.Fn()) == 0.0);
}

TEST_CASE ("The detail over its low-pass sets the colour's brightness; no detail leaves the colour", "[TileStreamMars]")
{
	const int n = 4;
	std::vector<float> col (3 * n * n, 100.0f), hi (n * n, 50.0f), lo (n * n, 50.0f);
	std::vector<uint8_t> cv (n * n, 1), dv (n * n, 1);
	hi[1] = 100.0f;      // twice the low-pass
	hi[2] = 1000.0f;     // clamped to 4 times
	dv[3] = 0;           // no detail
	hi[3] = 0.0f;
	cv[4] = 0;           // no colour: black
	lo[5] = 0.0f;        // no low-pass: the colour
	hi[5] = 80.0f;
	QImage img = TsImg::Detail (n, col, cv, hi, lo, dv);
	REQUIRE (img.width() == n);
	CHECK (img.pixelColor (0, 0) == QColor (100, 100, 100));
	CHECK (img.pixelColor (1, 0) == QColor (200, 200, 200));
	CHECK (img.pixelColor (2, 0) == QColor (255, 255, 255));
	CHECK (img.pixelColor (3, 0) == QColor (100, 100, 100));
	CHECK (img.pixelColor (0, 1) == QColor (0, 0, 0));
	CHECK (img.pixelColor (1, 1) == QColor (100, 100, 100));
	img = TsImg::Detail (n, col, cv, {}, {}, {});
	CHECK (img.pixelColor (1, 0) == QColor (100, 100, 100));
}

TEST_CASE ("The palette gives a brightness its colour: at its points, linear between them, held past its ends", "[TileStreamMars]")
{
	std::vector<TsBodyCfg::Pal> p {{50, 10, 20, 30}, {150, 210, 120, 80}, {250, 250, 200, 100}};
	float c[3];
	auto is = [&] (double v, float r, float g, float b) {
		TsImg::Paint (p, v, c);
		return std::fabs (c[0] - r) < 1e-4 && std::fabs (c[1] - g) < 1e-4 && std::fabs (c[2] - b) < 1e-4;
	};
	CHECK (is (50, 10, 20, 30));
	CHECK (is (150, 210, 120, 80));
	CHECK (is (100, 110, 70, 55));
	CHECK (is (200, 230, 160, 90));
	CHECK (is (0, 10, 20, 30));
	CHECK (is (255, 250, 200, 100));
	TsImg::Paint ({}, 77, c); // no palette: grey
	CHECK ((c[0] == 77 && c[1] == 77 && c[2] == 77));
}

TEST_CASE ("A cog body's Elev: 259 x 259 nodes in the tile's bounds, gaps from their neighbours, sea level flat", "[TileStreamMars]")
{
	const size_t N = 259;
	std::vector<float> h (N * N);
	std::vector<uint8_t> ok (N * N, 1);
	for (size_t i = 0; i < N; i++)
		for (size_t j = 0; j < N; j++) h[i * N + j] = (float)(-4000.0 + 3.0 * j - 2.0 * i);
	ok[100 * N + 100] = 0;
	h[100 * N + 100] = 30000.0f;
	std::string e = TsImg::ElevGrid (6, 1, 3, h, ok, 1.0);
	REQUIRE (e.size() == 100 + N * N * 2);
	int32_t dt, nx;
	double sc, b[4], emin, emax;
	memcpy (&dt, &e[8], 4);
	memcpy (&nx, &e[12], 4);
	memcpy (&sc, &e[28], 8);
	memcpy (b, &e[44], 32);
	memcpy (&emin, &e[76], 8);
	memcpy (&emax, &e[84], 8);
	CHECK (dt == -16);
	CHECK (nx == 259);
	CHECK (sc == 1.0);
	double w, n, dw, dh;
	TsImg::Bounds (6, 1, 3, w, n, dw, dh);
	CHECK (std::fabs (b[0] - (n - dh) * Deg) < 1e-12);
	CHECK (std::fabs (b[1] - n * Deg) < 1e-12);
	CHECK (std::fabs (b[2] - w * Deg) < 1e-12);
	CHECK (std::fabs (b[3] - (w + dw) * Deg) < 1e-12);
	CHECK (emin == -4000.0 - 2.0 * 258);
	CHECK (emax == -4000.0 + 3.0 * 258);
	auto at = [&] (size_t i, size_t j) { int16_t v; memcpy (&v, &e[100 + 2 * (i * N + j)], 2); return v; };
	CHECK (at (0, 0) == -4000);
	CHECK (at (258, 258) == -4000 + 3 * 258 - 2 * 258);
	CHECK (at (100, 100) == -4000 + 300 - 200); // the gap: its neighbours' mean
	std::vector<uint8_t> none (N * N, 0);
	CHECK (TsImg::ElevGrid (6, 1, 3, h, none, 1.0).empty());
	std::vector<float> zero (N * N, 0.0f);
	CHECK (TsImg::ElevGrid (6, 1, 3, zero, ok, 1.0).size() == 100);
}

TEST_CASE ("The shipped Mars body: the colour to L11, CTX quads to L16 inside 30S to 30N, MOLA heights, no Mask", "[TileStreamMars]")
{
	std::vector<TsBodyCfg> b;
	std::string err;
	REQUIRE (TsReadCfg ("Config/TileStream.cfg", b, err));
	const TsBodyCfg *m = nullptr;
	for (const TsBodyCfg &c : b) if (c.name == "Mars") m = &c;
	REQUIRE (m);
	CHECK (m->Cog());
	CHECK_FALSE (m->Masks());
	CHECK (m->surf.lmin == 1);
	CHECK (m->surf.lmax == 11);
	CHECK (m->SurfMax() == 16);
	REQUIRE (m->detail.size() == 16);
	double area = 0.0;
	for (const TsSource &d : m->detail) {
		CHECK (d.lmin == 12);
		CHECK (d.lmax == 16);
		CHECK (d.box[2] - d.box[0] == 45.0);
		CHECK (d.box[3] - d.box[1] == 30.0);
		area += (d.box[2] - d.box[0]) * (d.box[3] - d.box[1]);
	}
	CHECK (area == 360.0 * 60.0); // 30S to 30N, once
	CHECK (m->elev.kind == "cog");
	CHECK (m->elev.lmin == 4);
	CHECK (m->elev.lmax == 11);
	CHECK (m->elevScale == 1.0);
	CHECK (m->retire == "Mars_retire.txt");
	CHECK (m->credit.size() == 3);
	REQUIRE (m->palette.size() == 4); // design Rev 3: natural colour from the mosaic's brightness
	CHECK (m->palette.front().v == 0.0);
	CHECK (m->palette.back().v == 255.0);
	for (const TsBodyCfg::Pal &q : m->palette) CHECK ((q.r > q.g && q.g > q.b)); // never blue or white: red over green over blue
	CHECK (TsCfgElev ("Config/TileStream.cfg", "Mars") == 11);
	// L12 tiles: 0.703 degrees; row 85 is 30.23N to 29.53N, row 84 north of 30N
	CHECK (m->Covers (11, 0, 0));
	CHECK (m->Covers (12, 85, 10));
	CHECK_FALSE (m->Covers (12, 84, 10));
	CHECK (m->Covers (12, 170, 511));
	CHECK_FALSE (m->Covers (12, 171, 0));
	CHECK (m->Covers (16, 2047, 0));
	CHECK_FALSE (m->Covers (17, 32767, 0));
	std::ifstream list ("Config/TileStream/Mars_retire.txt");
	std::string line;
	int files = 0;
	while (std::getline (list, line)) files += !line.empty() && line[0] != ';';
	CHECK (files == 2733 + 2730);
}

TEST_CASE ("cog sources go with cog sources only; DETAIL lines need their bounds", "[TileStreamMars]")
{
	Tmp t;
	auto bad = [&] (const std::string &text, const std::string &what) {
		std::string f = (t.root / "bad.cfg").string();
		std::ofstream (f) << text;
		std::vector<TsBodyCfg> b;
		std::string e;
		CHECK_FALSE (TsReadCfg (f, b, e));
		CHECK (e.find (what) != std::string::npos);
	};
	bad ("BODY X\nSURF cog u 1 11\nDETAIL eox u 12 16 0 0 45 30\nEND\n", "bad DETAIL");
	bad ("BODY X\nSURF cog u 1 11\nDETAIL cog u 12 16 0 0 45\nEND\n", "bad DETAIL");
	bad ("BODY X\nSURF cog u 1 11\nDETAIL cog u 12 16 45 0 0 30\nEND\n", "bad DETAIL");
	bad ("BODY X\nSURF cog u 1 11\nELEV cog u 1 11\nEND\n", "bad ELEV");
	bad ("BODY X\nSURF cog u 1 11\nRETIRE ../x.txt\nEND\n", "bad RETIRE");
	bad ("BODY X\nSURF cog u 1 11\nWATER gibs u 8\nLIGHTS gibs u 7\nEND\n", "cog sources go with cog sources only");
	bad ("BODY X\nSURF cog u 1 11\nELEV terrarium u 15 16\nEND\n", "cog sources go with cog sources only");
	bad ("BODY X\nSURF eox u 1 16\nLIGHTS gibs u 7\nWATER gibs u 8\nDETAIL cog u 12 16 0 0 45 30\nEND\n", "cog sources go with cog sources only");
	bad ("BODY X\nSURF eox u 1 16\nLIGHTS gibs u 7\nWATER gibs u 8\nELEV cog u 4 11\nEND\n", "cog sources go with cog sources only");
	bad ("BODY X\nSURF eox u 1 16\nLIGHTS gibs u 7\nWATER gibs u 8\nPALETTE 0 1 1 1\nPALETTE 255 2 2 2\nEND\n", "cog sources go with cog sources only");
	bad ("BODY X\nSURF cog u 1 11\nPALETTE 100 1 1 1\nPALETTE 50 2 2 2\nEND\n", "bad PALETTE"); // brightness must rise
	bad ("BODY X\nSURF cog u 1 11\nPALETTE 0 1 1 300\nEND\n", "bad PALETTE");
	bad ("BODY X\nSURF cog u 1 11\nPALETTE 0 1 1 1\nEND\n", "PALETTE needs two points or more");
	std::string f = (t.root / "ok.cfg").string();
	std::ofstream (f) << "BODY X\nSURF cog u 1 11\nELEV cog e 4 11\nELEV_SCALE 1\nEND\n";
	std::vector<TsBodyCfg> b;
	std::string e;
	REQUIRE (TsReadCfg (f, b, e));
	CHECK_FALSE (b[0].Covers (12, 100, 100)); // no DETAIL: the colour's levels only
}

TEST_CASE ("A cog body streams from file:// COGs: Surf with its detail, Elev, no Mask; an old pack's exact files retire once", "[TileStreamMars]")
{
	App app;
	Tmp t;
	fs::path srv = t.root / "srv", tex = t.root / "Textures" / "Mars", cfgdir = t.root / "Config";
	// colour: grey 100 round the planet, 1 degree; detail: 200 at 0.01 degrees, 100 in its overviews (0 to 1.6E, 0 to 0.8N); heights 10 x - 5 y
	Spec sc;
	sc.spp = 3;
	sc.tile = 32;
	sc.dx = 1.0;
	Write (srv / "col.tif", Cog (sc, {Fill (360, 180, 3, [] (uint32_t, uint32_t, int) { return 100; }), Fill (180, 90, 3, [] (uint32_t, uint32_t, int) { return 100; })}));
	Spec sd;
	sd.bps = 16;
	sd.pred = 2;
	sd.metres = true;
	sd.cm = 180.0;
	sd.dx = sd.R * 0.01 * Deg;
	sd.x0 = sd.R * -180.0 * Deg;
	sd.y0 = sd.R * 0.8 * Deg;
	sd.tablesAt = 70000;
	Write (srv / "det.tif", Cog (sd, {Fill (160, 80, 1, [] (uint32_t, uint32_t, int) { return 200; }), Fill (80, 40, 1, [] (uint32_t, uint32_t, int) { return 100; }),
		Fill (40, 20, 1, [] (uint32_t, uint32_t, int) { return 100; }), Fill (20, 10, 1, [] (uint32_t, uint32_t, int) { return 100; })}));
	Spec se;
	se.bps = 16;
	se.sfmt = 2;
	se.metres = true;
	se.tile = 64;
	se.dx = se.R * Deg;
	se.x0 = -se.R * M_PI - 0.5 * se.dx;
	se.y0 = se.R * M_PI / 2 + 0.5 * se.dx;
	se.nodata = "-32768";
	Write (srv / "elev.tif", Cog (se, {Fill (361, 181, 1, [] (uint32_t x, uint32_t y, int) { return 10 * (int)x - 5 * (int)y; })}));
	// an old pack: an exact file, a changed one, one the list doesn't name
	std::string old = "an old pack's tile", mine = "a tile someone changed";
	fs::path o1 = tex / "Surf" / "09" / "000100" / "000200.dds", o2 = tex / "Elev" / "09" / "000100" / "000200.elv", o3 = tex / "Surf" / "09" / "000100" / "000201.dds";
	Write (o1, old);
	Write (o2, mine);
	Write (o3, old);
	char crc[16];
	snprintf (crc, sizeof crc, "%08lx", crc32 (crc32 (0L, Z_NULL, 0), (const Bytef*)old.data(), (uInt)old.size()));
	Write (cfgdir / "TileStream" / "old.txt", "; a test list\nSurf/09/000100/000200.dds " + std::to_string (old.size()) + " " + crc + "\nElev/09/000100/000200.elv " + std::to_string (mine.size()) + " " + crc + "\n../escape 1 0\n");
	std::string base = "file://" + srv.string();
	std::string cfg = (cfgdir / "TileStream.cfg").string();
	std::ofstream (cfg) << "BODY Mars\n"
		"SURF cog " << base << "/col.tif 1 11\n"
		"DETAIL cog " << base << "/det.tif 12 16 0 0 45 30\n"
		"ELEV cog " << base << "/elev.tif 4 11\n"
		"ELEV_SCALE 1\nPALETTE 0 0 0 0\nPALETTE 255 255 127.5 63.75\nRETIRE old.txt\nCREDIT test\nEND\n"; // grey 100: 100 50 25
	TileStream::Options opt;
	opt.cfgfile = cfg;
	opt.bodyDir = [&] (const std::string &) { return tex.string(); };
	opt.bodies = {"Mars"};
	opt.capMB = 0;
	opt.sky = false;
	{
		TileStream ts (opt);
		REQUIRE (ts.Enabled ("Mars"));
		CHECK (ts.Layers ("Mars") == 0);
		CHECK_FALSE (fs::exists (o1)); // retired before the planets look
		CHECK (fs::exists (o2));
		CHECK (fs::exists (o3));
		CHECK (fs::exists (tex / "TileStream" / "retired-old.txt"));
		auto surf = [&] (int L, int a, int b) { char s[64]; snprintf (s, sizeof s, "%02d/%06d/%06d", L, a, b); return tex / "Surf" / (std::string (s) + ".dds"); };
		auto elev = [&] (int L, int a, int b) { char s[64]; snprintf (s, sizeof s, "%02d/%06d/%06d", L, a, b); return tex / "Elev" / (std::string (s) + ".elv"); };
		REQUIRE (WaitFor ([&] { return fs::exists (surf (1, 0, 0)) && fs::exists (surf (4, 0, 1)); }, 30.0));
		// L12 at 0 to 0.7E, 0 to 0.7N: in the DETAIL box and its file
		CHECK (ts.Want ("Mars", 12, 127, 256, 0) == 0);
		CHECK (ts.Want ("Mars", 12, 50, 256, 0) == -1); // north of 30N: past the colour's levels, no DETAIL
		CHECK (ts.Want ("Mars", 17, 0, 0, 0) == -1);
		CHECK (ts.Want ("Mars", 12, 127, 258, 0) == 0);  // 1.4E to 2.1E: in the box, the file ends at 1.6E
		REQUIRE (WaitFor ([&] { return fs::exists (surf (12, 127, 256)) && fs::exists (surf (12, 127, 258)); }, 30.0));
		std::string d = Read (surf (12, 127, 256));
		REQUIRE (d.size() == 174904);
		QImage img = TsImg::DdsImage (QByteArray (d.data(), (qsizetype)d.size()));
		REQUIRE (img.width() == 512);
		QColor px = img.pixelColor (256, 256); // the palette's colour of the file's brightness, times the detail over its low-pass
		CHECK (std::abs (px.red() - 200) <= 8);
		CHECK (std::abs (px.green() - 100) <= 8);
		CHECK (std::abs (px.blue() - 50) <= 8);
		d = Read (surf (12, 127, 258));
		img = TsImg::DdsImage (QByteArray (d.data(), (qsizetype)d.size()));
		CHECK (std::abs (img.pixelColor (50, 256).red() - 200) <= 8);  // 1.47E
		CHECK (std::abs (img.pixelColor (400, 256).red() - 100) <= 8); // 1.95E: past the file, the colour alone
		CHECK (std::abs (img.pixelColor (400, 256).green() - 50) <= 8);
		CHECK_FALSE (fs::exists (tex / "Mask" / "12" / "000127" / "000256.dds"));
		// L5 0 to 90E, 0 to 90N: its Elev follows; node (1, 1) is (0, 0), the pixel at x 180, y 90
		CHECK (ts.Want ("Mars", 5, 0, 2, 0) >= 0);
		REQUIRE (WaitFor ([&] { return fs::exists (elev (5, 0, 2)); }, 30.0));
		std::string e = Read (elev (5, 0, 2));
		REQUIRE (e.size() == 100 + 259 * 259 * 2);
		int16_t h;
		memcpy (&h, &e[100 + 2 * (259 + 1)], 2);
		CHECK (h == 10 * 180 - 5 * 90);
		// the whole planet to L7 comes unasked, Elev with it
		CHECK (WaitFor ([&] { return fs::exists (surf (7, 7, 15)) && fs::exists (elev (7, 7, 15)); }, 60.0));
	}
	Write (o1, old); // put back: not retired again
	{
		TileStream ts (opt);
		CHECK (fs::exists (o1));
	}
	fs::remove (tex / "TileStream" / "retired-old.txt");
	opt.offline = true;
	{
		TileStream ts (opt); // offline: nothing retires, the old tiles are all there is
		CHECK (fs::exists (o1));
	}
}
