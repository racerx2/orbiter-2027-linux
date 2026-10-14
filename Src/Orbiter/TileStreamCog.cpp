
// not upstream: Cloud-Optimized GeoTIFF reads for the tile stream: ESA WorldCover classes over an Orbiter tile (TileStream.h)

#include "TileStream.h"
#include <zlib.h>
#include <algorithm>
#include <cmath>
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

struct Head {
	bool missing = false;
	std::vector<Ifd> ifd;
};

std::mutex cmx;
std::list<std::pair<std::string, std::shared_ptr<const Head>>> heads;               // parsed headers, newest first
std::list<std::pair<std::string, std::shared_ptr<const std::vector<uint8_t>>>> dec; // decoded tiles, newest first

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

}

int64_t Parse (const QByteArray &head, std::vector<Ifd> &ifds)
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
	uint64_t ver = rd (2, 2);
	bool big = ver == 43;
	if (ver != 42 && !big) return -1;
	uint64_t off = big ? rd (8, 8) : rd (4, 4);
	for (int guard = 0; off && guard < 64; guard++) {
		uint64_t cnt = big ? rd (off, 8) : rd (off, 2);
		uint64_t es = big ? 20 : 12, base = off + (big ? 8 : 2);
		if (need || cnt > 4096) return need ? (int64_t)need : -1;
		Ifd f;
		uint64_t subfile = 0;
		for (uint64_t i = 0; i < cnt; i++) {
			uint64_t e = base + i * es;
			uint64_t tag = rd (e, 2), type = rd (e + 2, 2), count = big ? rd (e + 4, 8) : rd (e + 4, 4);
			int sz = type == 1 ? 1 : type == 3 ? 2 : type == 4 ? 4 : type == 16 ? 8 : 0;
			if (!sz || !count || count > (1u << 24)) continue;
			uint64_t vo = e + (big ? 12 : 8);
			uint64_t at = count * sz <= (uint64_t)(big ? 8 : 4) ? vo : (big ? rd (vo, 8) : rd (vo, 4));
			auto val = [&] (uint64_t k) { return rd (at + k * sz, sz); };
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
			case 324: case 325: {
				uint64_t len = count * sz;
				if (at > n || len > n - at) { need = at > UINT64_MAX - len ? UINT64_MAX : std::max (need, at + len); break; } // fits before the resize
				std::vector<uint64_t> &v = tag == 324 ? f.off : f.cnt;
				v.resize (count);
				for (uint64_t k = 0; k < count; k++) v[k] = val (k);
				break;
			}
			}
		}
		uint64_t next = big ? rd (base + cnt * es, 8) : rd (base + cnt * es, 4);
		if (need) return (int64_t)need;
		if (!(subfile & 4)) ifds.push_back (f);
		off = next;
	}
	for (const Ifd &f : ifds) {
		if (!Sane (f)) return -1;
		uint64_t nt = Across (f) * Down (f) * (f.planar == 2 ? f.spp : 1);
		if (f.off.size() != nt || f.cnt.size() != nt) return -1;
	}
	std::stable_sort (ifds.begin(), ifds.end(), [] (const Ifd &a, const Ifd &b) { return a.w > b.w; });
	return ifds.empty() ? -1 : 0;
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
			Put<Head> (heads, rq[i].url, h, 32);
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
