
// not upstream: Cloud-Optimized GeoTIFF reads for the tile stream: ESA WorldCover classes over an Orbiter tile (TileStream.h)

#include "TileStream.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <set>

namespace TsCog {

namespace {

const uint64_t MaxBody = 8u << 20;  // one tile's compressed bytes
const uint64_t MaxTile = 16u << 20; // one tile's pixels

uint64_t Across (const Ifd &f) { return ((uint64_t)f.w + f.tw - 1) / f.tw; }
uint64_t Down (const Ifd &f) { return ((uint64_t)f.h + f.th - 1) / f.th; }

// the limits Parse and Decode hold a level to
bool Sane (const Ifd &f)
{
	if (!f.w || !f.h || !f.tw || !f.th || !f.spp) return false;
	if (f.w > (1u << 20) || f.h > (1u << 20) || f.tw > 4096 || f.th > 4096 || f.tw % 16 || f.th % 16 || f.spp > 4) return false;
	if ((uint64_t)f.tw * f.th * f.spp > MaxTile) return false;
	return f.bps == 8 && (f.comp == 8 || f.comp == 32946) && (f.pred == 1 || f.pred == 2);
}

// not upstream (mars-stream): the wider limits of a geo parse: 8 or 16 bits, none, LZW, deflate or JPEG
bool SaneAny (const Ifd &f)
{
	if (!f.w || !f.h || !f.tw || !f.th || !f.spp) return false;
	if (f.w > (1u << 20) || f.h > (1u << 20) || f.tw > 4096 || f.th > 4096 || f.tw % 16 || f.th % 16 || f.spp > 4) return false;
	if ((f.bps != 8 && f.bps != 16) || (f.sfmt != 1 && f.sfmt != 2) || (f.planar == 2 && f.spp > 1)) return false;
	if ((uint64_t)f.tw * f.th * f.spp * (f.bps / 8) > MaxTile) return false;
	if (f.comp == 7) return f.bps == 8 && (f.spp == 1 || f.spp == 3) && f.pred == 1;
	return (f.comp == 1 || f.comp == 5 || f.comp == 8 || f.comp == 32946) && (f.pred == 1 || f.pred == 2);
}

// not upstream (mars-stream): a GeoTIFF's placement (degrees, or equirectangular metres) and GDAL's scale, offset and no data
bool Place (const std::vector<double> &scl, const std::vector<double> &tie, const std::vector<double> &dbl, const std::vector<uint64_t> &key, const std::string &meta, const std::string &nod, bool le, Geo &g)
{
	g = Geo();
	g.le = le;
	if (scl.size() < 2 || tie.size() < 6 || !(scl[0] > 0.0) || !(scl[1] > 0.0) || key.size() < 4) return false;
	std::map<uint64_t, double> k;
	for (uint64_t i = 4; i + 3 < key.size() && i / 4 <= key[3]; i += 4) {
		if (key[i + 1] == 0) k[key[i]] = (double)key[i + 3];
		else if (key[i + 1] == 34736 && key[i + 3] < dbl.size()) k[key[i]] = dbl[key[i + 3]];
	}
	auto K = [&k] (uint64_t id, double d) { auto it = k.find (id); return it == k.end() ? d : it->second; };
	double model = K (1024, 0.0);
	g.deg = model == 2.0;
	if (g.deg) { if (K (2054, 9102) != 9102) return false; }
	else if (model != 1.0 || K (3075, 0) != 17 || K (3076, 9001) != 9001) return false; // equirectangular metres
	g.R = K (2057, 0.0);
	g.cm = K (3088, K (3080, 0.0));
	g.Rx = g.R * std::cos (K (3078, 0.0) * M_PI / 180.0);
	if (!g.deg && !(g.R > 0.0 && g.Rx > 0.0)) return false;
	g.dx = scl[0];
	g.dy = scl[1];
	g.x0 = tie[3] - tie[0] * g.dx - (g.deg ? 0.0 : K (3082, 0.0));
	g.y0 = tie[4] + tie[1] * g.dy - (g.deg ? 0.0 : K (3083, 0.0));
	if (K (1025, 1) == 2) { g.x0 -= 0.5 * g.dx; g.y0 += 0.5 * g.dy; } // the tie point on a pixel's centre
	auto item = [&meta] (const char *role, double &v) {
		size_t at = meta.find (std::string ("role=\"") + role + "\">");
		if (at != std::string::npos) v = std::strtod (meta.c_str() + meta.find ('>', at) + 1, nullptr);
	};
	item ("scale", g.scale);
	item ("offset", g.offset);
	if (!nod.empty()) {
		char *e = nullptr;
		g.nodata = std::strtod (nod.c_str(), &e);
		g.hasNodata = e != nod.c_str();
	}
	return std::isfinite (g.x0) && std::isfinite (g.y0) && std::isfinite (g.scale) && std::isfinite (g.offset);
}

struct Head {
	bool missing = false;
	std::vector<Ifd> ifd;
	Geo geo; // not upstream (mars-stream)
};

std::mutex cmx;
std::list<std::pair<std::string, std::shared_ptr<const Head>>> heads;               // parsed headers, newest first
std::list<std::pair<std::string, std::shared_ptr<const std::vector<uint8_t>>>> dec; // decoded tiles, newest first
std::list<std::pair<std::string, std::shared_ptr<const std::vector<uint64_t>>>> pages; // not upstream (mars-stream): offset table pages
std::list<std::pair<std::string, std::shared_ptr<const std::vector<float>>>> fdec;     // not upstream (mars-stream): decoded tiles of a geo read
const uint64_t Page = 1024; // table entries a range read

template<class T> std::shared_ptr<const T> Find (std::list<std::pair<std::string, std::shared_ptr<const T>>> &l, const std::string &k)
{
	std::lock_guard<std::mutex> lk (cmx);
	for (auto it = l.begin(); it != l.end(); ++it)
		if (it->first == k) {
			l.splice (l.begin(), l, it);
			return l.front().second;
		}
	return nullptr;
}

template<class T> void Put (std::list<std::pair<std::string, std::shared_ptr<const T>>> &l, const std::string &k, std::shared_ptr<const T> v, size_t cap)
{
	std::lock_guard<std::mutex> lk (cmx);
	l.remove_if ([&] (const auto &p) { return p.first == k; });
	l.emplace_front (k, std::move (v));
	while (l.size() > cap) l.pop_back();
}

// not upstream (mars-stream): a COG's levels and placement, read once; null when a read failed, missing on a 404
std::shared_ptr<const Head> GeoHead (const std::string &url, const Fetch &fetch)
{
	std::string k = "g|" + url;
	if (auto h = Find (heads, k)) return h;
	uint64_t len = 65536;
	for (int round = 0; round < 2; round++) {
		std::vector<Req> rq {{url, 0, len}};
		fetch (rq);
		auto h = std::make_shared<Head>();
		if (rq[0].status == 404) h->missing = true;
		else if (rq[0].status != 200 || !rq[0].data) return nullptr;
		else {
			int64_t r = Parse (*rq[0].data, h->ifd, &h->geo);
			if (r < 0) return nullptr;
			if (r > 0) {
				if (round || r > (1 << 20) || (uint64_t)rq[0].data->size() >= (uint64_t)r) return nullptr;
				len = (uint64_t)r + 65536;
				continue;
			}
		}
		Put<Head> (heads, k, h, 64);
		return h;
	}
	return nullptr;
}

// not upstream (mars-stream): tiles' offsets and byte counts at a level, from the IFD or from the tables by range (pages kept)
bool Entries (const std::string &url, const Head &h, int lv, const std::vector<uint64_t> &ts, const Fetch &fetch, std::vector<uint64_t> &off, std::vector<uint64_t> &cnt)
{
	const Ifd &f = h.ifd[lv];
	off.assign (ts.size(), 0);
	cnt.assign (ts.size(), 0);
	auto key = [&] (bool o, uint64_t pg) { return url + "|" + std::to_string (lv) + (o ? "|o|" : "|c|") + std::to_string (pg); };
	std::map<std::string, std::shared_ptr<const std::vector<uint64_t>>> got;
	std::vector<Req> rq;
	std::vector<std::string> keys;
	std::vector<bool> ofs;
	for (bool o : {true, false}) {
		if (!(o ? f.off : f.cnt).empty()) continue;
		uint64_t at = o ? f.offAt : f.cntAt, n = o ? f.offN : f.cntN, sz = o ? f.offSz : f.cntSz;
		for (uint64_t t : ts) {
			if (t >= n) return false;
			std::string k = key (o, t / Page);
			if (got.count (k)) continue;
			if ((got[k] = Find (pages, k))) continue;
			uint64_t a = t / Page * Page, e = std::min (n, a + Page);
			rq.push_back ({url, at + a * sz, (e - a) * sz});
			keys.push_back (k);
			ofs.push_back (o);
		}
	}
	if (!rq.empty()) fetch (rq);
	for (size_t i = 0; i < rq.size(); i++) {
		if (rq[i].status != 200 || !rq[i].data || (uint64_t)rq[i].data->size() < rq[i].len) return false;
		uint64_t sz = ofs[i] ? f.offSz : f.cntSz;
		auto pg = std::make_shared<std::vector<uint64_t>> (rq[i].len / sz);
		const uint8_t *d = (const uint8_t*)rq[i].data->constData();
		for (size_t e = 0; e < pg->size(); e++) {
			uint64_t v = 0;
			for (uint64_t b = 0; b < sz; b++) v = h.geo.le ? v | (uint64_t)d[e * sz + b] << (8 * b) : v << 8 | d[e * sz + b];
			(*pg)[e] = v;
		}
		Put<std::vector<uint64_t>> (pages, keys[i], pg, 256);
		got[keys[i]] = pg;
	}
	for (size_t i = 0; i < ts.size(); i++)
		for (bool o : {true, false}) {
			const std::vector<uint64_t> &v = o ? f.off : f.cnt;
			uint64_t &r = o ? off[i] : cnt[i];
			if (!v.empty()) {
				if (ts[i] >= v.size()) return false;
				r = v[ts[i]];
				continue;
			}
			auto pg = got[key (o, ts[i] / Page)];
			if (!pg || ts[i] % Page >= pg->size()) return false;
			r = (*pg)[ts[i] % Page];
		}
	return true;
}

}

int64_t Parse (const QByteArray &head, std::vector<Ifd> &ifds, Geo *geo)
{
	ifds.clear();
	const uint8_t *p = (const uint8_t*)head.constData();
	uint64_t n = (uint64_t)head.size(), need = 0;
	if (n < 16) return 16;
	bool le = p[0] == 'I' && p[1] == 'I';
	if (!le && !(p[0] == 'M' && p[1] == 'M')) return -1;
	auto rd = [&] (uint64_t at, int sz) -> uint64_t {
		if (at > n || (uint64_t)sz > n - at) { need = at > UINT64_MAX - sz ? UINT64_MAX : std::max (need, at + sz); return 0; }
		uint64_t v = 0;
		for (int i = 0; i < sz; i++) v = le ? v | (uint64_t)p[at + i] << (8 * i) : v << 8 | p[at + i];
		return v;
	};
	auto fits = [&] (uint64_t at, uint64_t len) { // else the bytes asked for
		if (at <= n && len <= n - at) return true;
		need = at > UINT64_MAX - len ? UINT64_MAX : std::max (need, at + len);
		return false;
	};
	uint64_t ver = rd (2, 2);
	bool big = ver == 43;
	if (ver != 42 && !big) return -1;
	uint64_t off = big ? rd (8, 8) : rd (4, 4);
	std::vector<double> scl, tie, gdbl; // not upstream (mars-stream): the first level's GeoTIFF tags
	std::vector<uint64_t> gkey;
	std::string gmeta, gnod;
	for (int guard = 0; off && guard < 64; guard++) {
		uint64_t cnt = big ? rd (off, 8) : rd (off, 2);
		uint64_t es = big ? 20 : 12, base = off + (big ? 8 : 2);
		if (need || cnt > 4096) return need ? (int64_t)need : -1;
		Ifd f;
		uint64_t subfile = 0;
		for (uint64_t i = 0; i < cnt; i++) {
			uint64_t e = base + i * es;
			uint64_t tag = rd (e, 2), type = rd (e + 2, 2), count = big ? rd (e + 4, 8) : rd (e + 4, 4);
			int sz = type == 1 || type == 2 || type == 7 ? 1 : type == 3 ? 2 : type == 4 ? 4 : type == 16 || type == 12 ? 8 : 0;
			if (!sz || !count || count > (1u << 24)) continue;
			uint64_t vo = e + (big ? 12 : 8);
			uint64_t at = count * sz <= (uint64_t)(big ? 8 : 4) ? vo : (big ? rd (vo, 8) : rd (vo, 4));
			auto val = [&] (uint64_t k) { return rd (at + k * sz, sz); };
			auto dbls = [&] (std::vector<double> &v) {
				if (!fits (at, count * 8)) return;
				v.resize (count);
				for (uint64_t k = 0; k < count; k++) { uint64_t b = val (k); memcpy (&v[k], &b, 8); }
			};
			bool geo0 = geo && guard == 0;
			switch (tag) {
			case 254: subfile = val (0); break;
			case 256: f.w = (uint32_t)val (0); break;
			case 257: f.h = (uint32_t)val (0); break;
			case 258: f.bps = (uint32_t)val (0); break;
			case 259: f.comp = (uint32_t)val (0); break;
			case 277: f.spp = (uint32_t)val (0); break;
			case 284: f.planar = (uint32_t)val (0); break;
			case 317: f.pred = (uint32_t)val (0); break;
			case 322: f.tw = (uint32_t)val (0); break;
			case 323: f.th = (uint32_t)val (0); break;
			case 339: f.sfmt = (uint32_t)val (0); break;
			case 324: case 325: {
				uint64_t len = count * sz;
				if (at > n || len > n - at) {
					if (geo && sz >= 2) { // not upstream (mars-stream): a big file's table, read by range when needed
						(tag == 324 ? f.offAt : f.cntAt) = at;
						(tag == 324 ? f.offN : f.cntN) = count;
						(tag == 324 ? f.offSz : f.cntSz) = (uint32_t)sz;
						break;
					}
					need = at > UINT64_MAX - len ? UINT64_MAX : std::max (need, at + len); // fits before the resize
					break;
				}
				std::vector<uint64_t> &v = tag == 324 ? f.off : f.cnt;
				v.resize (count);
				for (uint64_t k = 0; k < count; k++) v[k] = val (k);
				break;
			}
			case 347: if (geo && sz == 1 && fits (at, count)) f.jpegTables = head.mid ((qsizetype)at, (qsizetype)count); break;
			case 33550: if (geo0 && type == 12) dbls (scl); break;
			case 33922: if (geo0 && type == 12) dbls (tie); break;
			case 34736: if (geo0 && type == 12) dbls (gdbl); break;
			case 34735:
				if (geo0 && type == 3 && fits (at, count * 2)) {
					gkey.resize (count);
					for (uint64_t k = 0; k < count; k++) gkey[k] = val (k);
				}
				break;
			case 42112: case 42113:
				if (geo0 && type == 2 && fits (at, count)) (tag == 42112 ? gmeta : gnod).assign ((const char*)p + at, count);
				break;
			}
		}
		uint64_t next = big ? rd (base + cnt * es, 8) : rd (base + cnt * es, 4);
		if (need) return (int64_t)need;
		if (!(subfile & 4)) ifds.push_back (f);
		off = next;
	}
	for (const Ifd &f : ifds) {
		if (!(geo ? SaneAny (f) : Sane (f))) return -1;
		uint64_t nt = Across (f) * Down (f) * (f.planar == 2 ? f.spp : 1);
		if ((f.off.empty() ? f.offN : f.off.size()) != nt || (f.cnt.empty() ? f.cntN : f.cnt.size()) != nt) return -1;
	}
	std::stable_sort (ifds.begin(), ifds.end(), [] (const Ifd &a, const Ifd &b) { return a.w > b.w; });
	if (ifds.empty()) return -1;
	if (geo && !Place (scl, tie, gdbl, gkey, gmeta, gnod, le, *geo)) return -1;
	return 0;
}

bool Decode (const Ifd &f, const QByteArray &data, std::vector<uint8_t> &out)
{
	if (!Sane (f) || (f.planar == 2 && f.spp > 1) || (uint64_t)data.size() > MaxBody) return false;
	size_t row = (size_t)f.tw * f.spp, sz = row * f.th;
	if (data.isEmpty()) { out.assign (sz, 0); return true; } // a sparse tile: no data
	out.assign (sz, 0);
	z_stream s {};
	if (inflateInit (&s) != Z_OK) return false;
	s.next_in = (Bytef*)data.constData();
	s.avail_in = (uInt)data.size();
	s.next_out = out.data();
	s.avail_out = (uInt)sz;
	int r = inflate (&s, Z_FINISH); // into exactly the tile: never more
	uLong got = s.total_out;
	inflateEnd (&s);
	if (got != sz || (r != Z_STREAM_END && r != Z_BUF_ERROR)) return false;
	if (f.pred == 2)
		for (size_t y = 0; y < f.th; y++)
			for (size_t x = f.spp; x < row; x++) out[y * row + x] += out[y * row + x - f.spp];
	else if (f.pred != 1) return false;
	return true;
}

// not upstream (mars-stream): TIFF LZW (codes MSB first, 9 to 12 bits, the width growing a code early)
bool Lzw (const uint8_t *in, size_t n, uint8_t *out, size_t size)
{
	std::vector<uint16_t> prev (4096);
	std::vector<uint8_t> last (4096), first (4096);
	std::vector<uint32_t> len (4096);
	for (int i = 0; i < 256; i++) { prev[i] = 0; last[i] = first[i] = (uint8_t)i; len[i] = 1; }
	size_t bit = 0, o = 0;
	int next = 258, width = 9, old = -1;
	auto code = [&] () -> int {
		if (bit + width > n * 8) return 257;
		size_t b = bit >> 3;
		uint32_t v = (uint32_t)in[b] << 16 | (b + 1 < n ? (uint32_t)in[b + 1] << 8 : 0) | (b + 2 < n ? in[b + 2] : 0);
		v = (v >> (24 - (bit & 7) - width)) & ((1u << width) - 1);
		bit += width;
		return (int)v;
	};
	auto put = [&] (int c) {
		uint32_t l = len[c];
		for (uint32_t i = l; i-- > 0; c = prev[c])
			if (o + i < size) out[o + i] = last[c];
		o += l;
	};
	while (o < size) {
		int c = code();
		if (c == 257) break;
		if (c == 256) { next = 258; width = 9; old = -1; continue; }
		if (old < 0) {
			if (c > 255) return false;
			put (c);
			old = c;
			continue;
		}
		if (c > next || (c == next && next >= 4096)) return false;
		if (next < 4096) {
			prev[next] = (uint16_t)old;
			last[next] = c < next ? first[c] : first[old];
			first[next] = first[old];
			len[next] = len[old] + 1;
			next++;
		}
		put (c);
		old = c;
		if (next + 1 >= (1 << width) && width < 12) width++;
	}
	return o >= size;
}

bool DecodeAny (const Ifd &f, bool le, const QByteArray &data, std::vector<float> &out)
{
	if (!SaneAny (f) || data.isEmpty() || (uint64_t)data.size() > MaxBody) return false;
	size_t row = (size_t)f.tw * f.spp, n = row * f.th, B = f.bps / 8;
	out.assign (n, 0.0f);
	if (f.comp == 7) {
		QByteArray j = f.jpegTables.size() > 4 && data.size() > 2 ? f.jpegTables.left (f.jpegTables.size() - 2) + data.mid (2) : data; // the tables, then the tile's frame
		QImage im;
		if (!im.loadFromData (j, "JPG") || im.width() != (int)f.tw || im.height() != (int)f.th) return false;
		im = im.convertToFormat (f.spp == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
		for (uint32_t y = 0; y < f.th; y++) {
			const uchar *s = im.constScanLine ((int)y);
			for (size_t x = 0; x < row; x++) out[y * row + x] = s[x];
		}
		return true;
	}
	std::vector<uint8_t> raw (n * B);
	if (f.comp == 1) {
		if ((size_t)data.size() < raw.size()) return false;
		memcpy (raw.data(), data.constData(), raw.size());
	} else if (f.comp == 5) {
		if (!Lzw ((const uint8_t*)data.constData(), (size_t)data.size(), raw.data(), raw.size())) return false;
	} else {
		z_stream s {};
		if (inflateInit (&s) != Z_OK) return false;
		s.next_in = (Bytef*)data.constData();
		s.avail_in = (uInt)data.size();
		s.next_out = raw.data();
		s.avail_out = (uInt)raw.size();
		int r = inflate (&s, Z_FINISH);
		uLong got = s.total_out;
		inflateEnd (&s);
		if (got != raw.size() || (r != Z_STREAM_END && r != Z_BUF_ERROR)) return false;
	}
	bool pred = f.pred == 2, sig = f.sfmt == 2;
	if (B == 1) {
		if (pred)
			for (size_t y = 0; y < f.th; y++)
				for (size_t x = f.spp; x < row; x++) raw[y * row + x] += raw[y * row + x - f.spp];
		for (size_t i = 0; i < n; i++) out[i] = sig ? (float)(int8_t)raw[i] : (float)raw[i];
		return true;
	}
	std::vector<uint16_t> v (n);
	for (size_t i = 0; i < n; i++) v[i] = le ? (uint16_t)(raw[2 * i] | raw[2 * i + 1] << 8) : (uint16_t)(raw[2 * i] << 8 | raw[2 * i + 1]);
	if (pred) // on the numbers, after the byte order
		for (size_t y = 0; y < f.th; y++)
			for (size_t x = f.spp; x < row; x++) v[y * row + x] = (uint16_t)(v[y * row + x] + v[y * row + x - f.spp]);
	for (size_t i = 0; i < n; i++) out[i] = sig ? (float)(int16_t)v[i] : (float)v[i];
	return true;
}

bool Global (const Geo &g, uint32_t w)
{
	double pxd = g.deg ? g.dx : g.dx / g.Rx * 180.0 / M_PI;
	return pxd > 0.0 && std::fabs (360.0 / pxd - w) < 0.5;
}

bool ColX (const Geo &g, uint32_t w, double lng, double &x)
{
	const double k = 180.0 / M_PI;
	double pxd = g.deg ? g.dx : g.dx / g.Rx * k;
	if (!(pxd > 0.0)) return false;
	x = ((g.deg ? lng : g.Rx * (lng - g.cm) / k) - g.x0) / g.dx;
	if (Global (g, w)) return true;
	double W360 = 360.0 / pxd; // the one place of this longitude on the file, or none
	x = std::fmod (x, W360);
	if (x < 0.0) x += W360;
	return x <= w;
}

bool RowY (const Geo &g, uint32_t h, double lat, double &y)
{
	y = (g.y0 - (g.deg ? lat : g.R * lat * M_PI / 180.0)) / g.dy;
	return y >= 0.0 && y <= h;
}

bool Sample (const std::string &url, const Pts &p, const Fetch &fetch, std::vector<float> &out, std::vector<uint8_t> &valid, int *spp)
{
	out.clear();
	valid.clear();
	if (spp) *spp = 0;
	if (p.nx <= 0 || p.ny <= 0 || p.nx > 4096 || p.ny > 4096) return false;
	size_t np = (size_t)p.nx * p.ny;
	valid.assign (np, 0);
	std::shared_ptr<const Head> h = GeoHead (url, fetch);
	if (!h) return false;
	if (h->missing) return true;
	const Geo &g = h->geo;
	const Ifd &f0 = h->ifd[0];
	const double k = 180.0 / M_PI;
	double pxd = g.deg ? g.dx : g.dx / g.Rx * k, pyd = g.deg ? g.dy : g.dy / g.R * k; // level 0's pixel in degrees
	if (!(pxd > 0.0) || !(pyd > 0.0)) return false;
	int lv = 0;
	for (int i = 0; i < (int)h->ifd.size(); i++)
		if (pyd * f0.h / h->ifd[i].h <= p.pix * 1.0001) lv = i; // the coarsest level still as fine as the points
	const Ifd &f = h->ifd[lv];
	const int S = (int)f.spp;
	if (spp) *spp = S;
	double sx = (double)f.w / f0.w, sy = (double)f.h / f0.h;
	bool global = Global (g, f0.w);
	struct Tap { int64_t at; double w; };
	// a footprint of at least a pixel as a box over the level's pixels: bilinear when enlarging, the area mean when shrinking
	auto taps = [] (double v, double hw, int64_t n, bool wrap, std::vector<Tap> &t) {
		double a = v - hw, b = v + hw;
		for (int64_t i = (int64_t)std::floor (a); i <= (int64_t)std::floor (b); i++) {
			double w = std::min ((double)i + 1.0, b) - std::max ((double)i, a);
			if (w > 1e-9) t.push_back ({wrap ? i : std::clamp<int64_t> (i, 0, n - 1), w});
		}
	};
	double hx = std::max (0.5, 0.5 * std::fabs (p.dlng) / pxd * sx), hy = std::max (0.5, 0.5 * std::fabs (p.dlat) / pyd * sy);
	std::vector<std::vector<Tap>> tx (p.nx), ty (p.ny);
	int64_t i0 = INT64_MAX, i1 = INT64_MIN, j0 = INT64_MAX, j1 = INT64_MIN;
	for (int ix = 0; ix < p.nx; ix++) {
		double x;
		if (!ColX (g, f0.w, p.lng0 + ix * p.dlng, x)) continue;
		taps (x * sx, hx, f.w, global, tx[ix]);
		for (const Tap &t : tx[ix]) { i0 = std::min (i0, t.at); i1 = std::max (i1, t.at); }
	}
	for (int iy = 0; iy < p.ny; iy++) {
		double y;
		if (!RowY (g, f0.h, p.lat0 + iy * p.dlat, y)) continue;
		taps (y * sy, hy, f.h, false, ty[iy]);
		for (const Tap &t : ty[iy]) { j0 = std::min (j0, t.at); j1 = std::max (j1, t.at); }
	}
	if (i0 > i1 || j0 > j1) return true; // no point on the file
	int64_t W = i1 - i0 + 1, H = j1 - j0 + 1;
	if (W * H * S > ((int64_t)16 << 20)) return false;
	uint64_t A = Across (f);
	std::vector<uint32_t> colT ((size_t)W), colL ((size_t)W);
	std::set<uint32_t> cols;
	for (int64_t c = 0; c < W; c++) {
		int64_t im = global ? ((i0 + c) % (int64_t)f.w + f.w) % f.w : i0 + c;
		colT[c] = (uint32_t)(im / f.tw);
		colL[c] = (uint32_t)(im % f.tw);
		cols.insert (colT[c]);
	}
	std::vector<uint64_t> ts, to, tc;
	for (int64_t r = j0 / f.th; r <= j1 / f.th; r++)
		for (uint32_t c : cols) ts.push_back ((uint64_t)r * A + c);
	if (!Entries (url, *h, lv, ts, fetch, to, tc)) return false;
	auto key = [&] (uint64_t t) { return "g|" + url + "|" + std::to_string (lv) + "|" + std::to_string (t); };
	std::map<uint64_t, std::shared_ptr<const std::vector<float>>> tl;
	std::vector<Req> rq;
	std::vector<uint64_t> rt;
	for (size_t i = 0; i < ts.size(); i++) {
		if ((tl[ts[i]] = Find (fdec, key (ts[i])))) continue;
		if (!tc[i]) continue; // a sparse tile: no data
		if (tc[i] > MaxBody) return false;
		rq.push_back ({url, to[i], tc[i]});
		rt.push_back (ts[i]);
	}
	if (!rq.empty()) fetch (rq);
	for (size_t i = 0; i < rq.size(); i++) {
		if (rq[i].status != 200 || !rq[i].data || (uint64_t)rq[i].data->size() < rq[i].len) return false;
		auto v = std::make_shared<std::vector<float>>();
		if (!DecodeAny (f, g.le, *rq[i].data, *v)) return false;
		Put<std::vector<float>> (fdec, key (rt[i]), v, 24);
		tl[rt[i]] = v;
	}
	std::vector<float> win ((size_t)(W * H * S));
	std::vector<uint8_t> ok ((size_t)(W * H));
	for (int64_t r = 0; r < H; r++) {
		int64_t j = j0 + r;
		uint64_t row = (uint64_t)(j / f.th) * A, last = UINT64_MAX;
		size_t ly = (size_t)(j % f.th);
		const float *src = nullptr;
		for (int64_t c = 0; c < W; c++) {
			uint64_t t = row + colT[c];
			if (t != last) {
				last = t;
				auto it = tl.find (t);
				src = it != tl.end() && it->second ? it->second->data() : nullptr;
			}
			if (!src) continue;
			const float *s = src + (ly * f.tw + colL[c]) * S;
			float *d = &win[(size_t)(r * W + c) * S];
			bool good = true;
			for (int q = 0; q < S; q++) {
				if (g.hasNodata && s[q] == (float)g.nodata) good = false;
				d[q] = (float)(s[q] * g.scale + g.offset);
			}
			ok[(size_t)(r * W + c)] = good;
		}
	}
	out.assign (np * S, 0.0f);
	std::vector<double> acc (S);
	for (int iy = 0; iy < p.ny; iy++) {
		if (ty[iy].empty()) continue;
		for (int ix = 0; ix < p.nx; ix++) {
			if (tx[ix].empty()) continue;
			std::fill (acc.begin(), acc.end(), 0.0);
			double wv = 0.0, wt = 0.0;
			for (const Tap &a : ty[iy])
				for (const Tap &b : tx[ix]) {
					double w = a.w * b.w;
					wt += w;
					size_t c = (size_t)((a.at - j0) * W + (b.at - i0));
					if (!ok[c]) continue;
					wv += w;
					for (int q = 0; q < S; q++) acc[q] += w * win[c * S + q];
				}
			if (wv <= 0.0 || wv < 0.5 * wt) continue; // mostly no data
			size_t o = (size_t)iy * p.nx + ix;
			valid[o] = 1;
			for (int q = 0; q < S; q++) out[o * S + q] = (float)(acc[q] / wv);
		}
	}
	return true;
}

double Pixel (const std::string &url, const Fetch &fetch)
{
	std::shared_ptr<const Head> h = GeoHead (url, fetch);
	if (!h || h->missing) return 0.0;
	return h->geo.deg ? h->geo.dy : h->geo.dy / h->geo.R * 180.0 / M_PI;
}

std::string CellName (int lat0, int lng0)
{
	char b[32];
	snprintf (b, sizeof b, "%c%02d%c%03d", lat0 < 0 ? 'S' : 'N', std::abs (lat0), lng0 < 0 ? 'W' : 'E', std::abs (lng0));
	return b;
}

bool Classes (const std::string &tmpl, int L, int ilat, int ilng, int n, const Fetch &fetch, std::vector<uint8_t> &out)
{
	out.assign ((size_t)n * n, 255);
	if (L < 4 || n <= 0) return false;
	double dh = 180.0 / (1 << (L - 4)), dw = 360.0 / (1 << (L - 3));
	double north = 90.0 - ilat * dh, west = -180.0 + ilng * dw, pix = dh / n;
	auto lat = [&] (int py) { return north - (py + 0.5) * dh / n; };
	auto lng = [&] (int px) { return west + (px + 0.5) * dw / n; };
	auto cellOf = [] (double v) { return (int)std::floor (v / 3.0) * 3; };
	auto inside = [] (int lat0) { return lat0 >= -60 && lat0 <= 81; }; // WorldCover's cells
	std::set<std::pair<int, int>> cells;
	for (int py = 0; py < n; py++) {
		int la = cellOf (lat (py));
		if (!inside (la)) continue;
		for (int px = 0; px < n; px++) cells.insert ({la, cellOf (lng (px))});
	}
	auto url = [&] (const std::pair<int, int> &c) {
		std::string u = tmpl;
		size_t p = u.find ("{tile}");
		if (p != std::string::npos) u.replace (p, 6, CellName (c.first, c.second));
		return u;
	};
	std::map<std::pair<int, int>, std::shared_ptr<const Head>> hd;
	std::vector<Req> rq;
	std::vector<std::pair<int, int>> rc;
	for (const auto &c : cells)
		if (!(hd[c] = Find (heads, url (c)))) { rq.push_back ({url (c), 0, 65536}); rc.push_back (c); }
	for (int round = 0; round < 2 && !rq.empty(); round++) {
		fetch (rq);
		std::vector<Req> again;
		std::vector<std::pair<int, int>> ac;
		for (size_t i = 0; i < rq.size(); i++) {
			auto h = std::make_shared<Head>();
			if (rq[i].status == 404) h->missing = true;
			else if (rq[i].status != 200 || !rq[i].data) return false;
			else {
				int64_t r = Parse (*rq[i].data, h->ifd);
				if (r < 0) return false;
				if (r > 0) {
					if (round || r > (8 << 20) || (uint64_t)rq[i].data->size() >= (uint64_t)r) return false;
					again.push_back ({rq[i].url, 0, (uint64_t)r + 65536});
					ac.push_back (rc[i]);
					continue;
				}
			}
			Put<Head> (heads, rq[i].url, h, 64);
			hd[rc[i]] = h;
		}
		rq.swap (again);
		rc.swap (ac);
	}
	if (!rq.empty()) return false;
	std::map<std::pair<int, int>, int> lev; // overview per cell: the coarsest one still at least as fine as an output pixel
	for (auto &c : hd) {
		if (c.second->missing) continue;
		const std::vector<Ifd> &v = c.second->ifd;
		int best = 0;
		for (int i = 0; i < (int)v.size(); i++)
			if (3.0 / v[i].w <= pix * 1.0001) best = i;
		lev[c.first] = best;
	}
	auto tileKey = [&] (const std::pair<int, int> &c, size_t t) { return url (c) + "|" + std::to_string (lev[c]) + "|" + std::to_string (t); };
	auto locate = [&] (int py, int px, std::pair<int, int> &c, size_t &t, size_t &o) {
		double a = lat (py), g = lng (px);
		c = {cellOf (a), cellOf (g)};
		auto h = hd.find (c);
		if (h == hd.end()) return 0; // outside WorldCover
		if (h->second->missing) return 1; // no cell: open sea
		const Ifd &f = h->second->ifd[lev[c]];
		int x = std::clamp ((int)std::floor ((g - c.second) / 3.0 * f.w), 0, (int)f.w - 1);
		int y = std::clamp ((int)std::floor ((c.first + 3.0 - a) / 3.0 * f.h), 0, (int)f.h - 1);
		t = (size_t)(y / f.th) * Across (f) + x / f.tw;
		if (t >= f.off.size() || t >= f.cnt.size()) return 0;
		o = ((size_t)(y % f.th) * f.tw + x % f.tw) * f.spp;
		return 2;
	};
	std::map<std::string, std::shared_ptr<const std::vector<uint8_t>>> tl;
	rq.clear();
	std::vector<std::pair<std::pair<int, int>, size_t>> want;
	std::pair<int, int> lc {999, 999};
	size_t lt = 0;
	for (int py = 0; py < n; py++)
		for (int px = 0; px < n; px++) {
			std::pair<int, int> c;
			size_t t, o;
			if (locate (py, px, c, t, o) != 2 || (c == lc && t == lt)) continue;
			lc = c;
			lt = t;
			std::string k = tileKey (c, t);
			if (tl.count (k)) continue;
			if ((tl[k] = Find (dec, k))) continue;
			const Ifd &f = hd[c]->ifd[lev[c]];
			if (f.cnt[t] > MaxBody) return false;
			rq.push_back ({url (c), f.off[t], f.cnt[t]});
			want.push_back ({c, t});
		}
	std::vector<Req> net;
	for (const Req &r : rq) if (r.len) net.push_back (r);
	if (!net.empty()) fetch (net);
	for (size_t i = 0, j = 0; i < rq.size(); i++) {
		const Ifd &f = hd[want[i].first]->ifd[lev[want[i].first]];
		QByteArray body;
		if (rq[i].len) {
			const Req &r = net[j++];
			if (r.status != 200 || !r.data || (uint64_t)r.data->size() < r.len) return false;
			body = *r.data;
		}
		auto v = std::make_shared<std::vector<uint8_t>>();
		if (!Decode (f, body, *v)) return false;
		std::string k = tileKey (want[i].first, want[i].second);
		Put<std::vector<uint8_t>> (dec, k, v, 12);
		tl[k] = v;
	}
	const std::vector<uint8_t> *lv = nullptr;
	lc = {999, 999};
	for (int py = 0; py < n; py++)
		for (int px = 0; px < n; px++) {
			std::pair<int, int> c;
			size_t t, o;
			int r = locate (py, px, c, t, o);
			uint8_t &d = out[(size_t)py * n + px];
			if (r == 1) d = 0;
			else if (r == 2) {
				if (!lv || c != lc || t != lt) { lv = tl[tileKey (c, t)].get(); lc = c; lt = t; }
				d = (*lv)[o];
			}
		}
	return true;
}

bool IsWater (uint8_t c) { return c == 0 || c == 80; }

QImage Water (const std::vector<uint8_t> &cls, int n, const QImage &gibs)
{
	QImage g = gibs.convertToFormat (QImage::Format_RGB888);
	QImage out (n, n, QImage::Format_RGB888);
	for (int y = 0; y < n; y++) {
		uchar *o = out.scanLine (y);
		const uchar *s = y < g.height() ? g.constScanLine (y) : nullptr;
		for (int x = 0; x < n; x++, o += 3) {
			uint8_t c = cls[(size_t)y * n + x];
			uchar v = c == 255 ? (s && x < g.width() ? s[3 * x] : 0) : IsWater (c) ? 255 : 0;
			o[0] = o[1] = o[2] = v;
		}
	}
	return out;
}

// RGB weights: R trees, G grass / crops / wetland / moss, B bare / built-up; water, snow, no data black
QImage ClassImage (const std::vector<uint8_t> &cls, int n, int size)
{
	QImage full (n, n, QImage::Format_RGB888);
	for (int y = 0; y < n; y++) {
		uchar *o = full.scanLine (y);
		for (int x = 0; x < n; x++, o += 3) {
			uint8_t r = 0, g = 0, b = 0;
			switch (cls[(size_t)y * n + x]) {
			case 10: case 95: r = 255; break;
			case 20: r = 110; g = 145; break;
			case 30: case 40: case 90: case 100: g = 255; break;
			case 60: b = 255; break;
			case 50: b = 160; break;
			}
			o[0] = r; o[1] = g; o[2] = b;
		}
	}
	return n == size ? full : full.scaled (size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

}
