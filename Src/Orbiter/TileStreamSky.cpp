
// not upstream: tile stream clouds (DXT5), base flattening (Elev_mod) and ground-track keys (TileStream.h)

#include "TileStream.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>
#define STB_DXT_IMPLEMENTATION
#define STB_DXT_STATIC
#include "stb_dxt.h"

namespace TsSky {

static void Bounds (int L, int ilat, int ilng, double &w, double &n, double &dw, double &dh)
{
	if (L < 4) { w = -180.0; n = 90.0; dw = 360.0; dh = 180.0; return; }
	dh = 180.0 / (double)(1 << (L - 4));
	dw = 360.0 / (double)(1 << (L - 3));
	n = 90.0 - ilat * dh;
	w = -180.0 + ilng * dw;
}

static int TileSize (int L) { return L <= 1 ? 128 : L == 2 ? 256 : 512; }

// white, alpha = cloud density from the image's grey level; area average down, bilinear up
QImage Cloud (const QImage &src, int L, int ilat, int ilng)
{
	int S = TileSize (L);
	double w, n, dw, dh;
	Bounds (L, ilat, ilng, w, n, dw, dh);
	QImage g = src.convertToFormat (QImage::Format_Grayscale8);
	double W = g.width(), H = g.height();
	double x0 = (w + 180.0) / 360.0 * W, y0 = (90.0 - n) / 180.0 * H, sw = dw / 360.0 * W, sh = dh / 180.0 * H;
	QImage t;
	if (sw >= S) t = g.copy ((int)std::lround (x0), (int)std::lround (y0), (int)std::lround (sw), (int)std::lround (sh)).scaled (S, S, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	else {
		t = QImage (S, S, QImage::Format_Grayscale8);
		for (int y = 0; y < S; y++) {
			double sy = std::clamp (y0 + (y + 0.5) * sh / S - 0.5, 0.0, H - 1.0);
			int ya = (int)sy, yb = std::min (ya + 1, (int)H - 1);
			double fy = sy - ya;
			uchar *o = t.scanLine (y);
			for (int x = 0; x < S; x++) {
				double sx = x0 + (x + 0.5) * sw / S - 0.5;
				int xa = (int)std::floor (sx);
				double fx = sx - xa;
				int xb = ((xa + 1) % (int)W + (int)W) % (int)W;
				xa = (xa % (int)W + (int)W) % (int)W;
				const uchar *ra = g.constScanLine (ya), *rb = g.constScanLine (yb);
				double v = (ra[xa] * (1 - fx) + ra[xb] * fx) * (1 - fy) + (rb[xa] * (1 - fx) + rb[xb] * fx) * fy;
				o[x] = (uchar)std::lround (v);
			}
		}
	}
	QImage out (S, S, QImage::Format_RGBA8888);
	for (int y = 0; y < S; y++) {
		const uchar *s = t.constScanLine (y);
		uchar *o = out.scanLine (y);
		for (int x = 0; x < S; x++, o += 4) {
			o[0] = o[1] = o[2] = 255;
			o[3] = (uchar)std::clamp ((s[x] - 8) * 255 / 247, 0, 255); // the image's black floor is clear sky
		}
	}
	return out;
}

static void Put32 (std::string &s, size_t at, uint32_t v) { memcpy (&s[at], &v, 4); }

std::string Dds5 (const QImage &src)
{
	QImage img = src.convertToFormat (QImage::Format_RGBA8888);
	int w = img.width(), h = img.height(), levels = 1;
	while (std::max (w, h) >> levels) levels++;
	std::string out (128, '\0');
	memcpy (&out[0], "DDS ", 4);
	Put32 (out, 4, 124);
	Put32 (out, 8, 0x1 | 0x2 | 0x4 | 0x1000 | 0x20000 | 0x80000);
	Put32 (out, 12, (uint32_t)h);
	Put32 (out, 16, (uint32_t)w);
	Put32 (out, 20, (uint32_t)(std::max (1, (w + 3) / 4) * std::max (1, (h + 3) / 4) * 16));
	Put32 (out, 28, (uint32_t)levels);
	Put32 (out, 76, 32);
	Put32 (out, 80, 0x4);
	memcpy (&out[84], "DXT5", 4);
	Put32 (out, 108, 0x1000 | 0x8 | 0x400000);
	QImage lv = img;
	uchar blk[64];
	for (int i = 0; i < levels; i++) {
		if (i) lv = lv.scaled (std::max (1, lv.width() / 2), std::max (1, lv.height() / 2), Qt::IgnoreAspectRatio, Qt::SmoothTransformation).convertToFormat (QImage::Format_RGBA8888);
		int lw = lv.width(), lh = lv.height(), bw = (lw + 3) / 4, bh = (lh + 3) / 4;
		size_t at = out.size();
		out.resize (at + (size_t)bw * bh * 16);
		for (int by = 0; by < bh; by++)
			for (int bx = 0; bx < bw; bx++) {
				for (int k = 0; k < 16; k++) {
					int x = std::min (bx * 4 + (k & 3), lw - 1), y = std::min (by * 4 + (k >> 2), lh - 1);
					memcpy (blk + 4 * k, lv.constScanLine (y) + 4 * x, 4);
				}
				stb_compress_dxt_block ((uchar*)&out[at + ((size_t)by * bw + bx) * 16], blk, 1, STB_DXT_HIGHQUAL);
			}
	}
	return out;
}

// metres at a point of an .elv tile (bilinear over its nodes), NaN outside or unreadable
double ElvAt (const std::string &e, double lng, double lat)
{
	if (e.size() < 100 || e.compare (0, 3, "ELE")) return NAN;
	int32_t hs, dt, nx, ny;
	double sc, of, b[4];
	memcpy (&hs, &e[4], 4); memcpy (&dt, &e[8], 4); memcpy (&nx, &e[12], 4); memcpy (&ny, &e[16], 4);
	memcpy (&sc, &e[28], 8); memcpy (&of, &e[36], 8); memcpy (b, &e[44], 32);
	for (double v : b) if (!std::isfinite (v)) return NAN;
	if (!(b[1] > b[0] && b[3] > b[2] && std::isfinite (sc) && std::isfinite (of))) return NAN;
	if (!(lat >= b[0] && lat <= b[1] && lng >= b[2] && lng <= b[3]) || nx != 259 || ny != 259) return NAN;
	if (dt == 0) return of;
	if (dt != -16 || hs < 100 || e.size() < (size_t)hs + 259 * 259 * 2) return NAN;
	double fi = (lat - b[0]) / (b[1] - b[0]) * 256.0 + 1.0, fj = (lng - b[2]) / (b[3] - b[2]) * 256.0 + 1.0;
	int i0 = std::clamp ((int)fi, 0, 257), j0 = std::clamp ((int)fj, 0, 257);
	double u = fi - i0, v = fj - j0;
	auto z = [&] (int i, int j) { int16_t q; memcpy (&q, &e[(size_t)hs + ((size_t)i * 259 + j) * 2], 2); return q * sc + of; };
	return (z (i0, j0) * (1 - v) + z (i0, j0 + 1) * v) * (1 - u) + (z (i0 + 1, j0) * (1 - v) + z (i0 + 1, j0 + 1) * v) * u;
}

// Terrarium metres at a point of tile (z, row, col); lng, lat in degrees
double TerrAt (const QImage &tile, int z, int row, int col, double lng, double lat)
{
	if (tile.isNull()) return NAN;
	QImage t = tile.convertToFormat (QImage::Format_RGB888);
	double W = 256.0 * (1 << z), s = sin (std::clamp (lat, -85.05, 85.05) * M_PI / 180.0);
	double x = (lng + 180.0) / 360.0 * W - col * 256.0, y = (0.5 - log ((1 + s) / (1 - s)) / (4 * M_PI)) * W - row * 256.0;
	int px = std::clamp ((int)x, 0, t.width() - 1), py = std::clamp ((int)y, 0, t.height() - 1);
	const uchar *p = t.constScanLine (py) + 3 * px;
	return p[0] * 256.0 + p[1] + p[2] / 256.0 - 32768.0;
}

void TerrTile (double lng, double lat, int z, int &row, int &col)
{
	double W = 256.0 * (1 << z), s = sin (std::clamp (lat, -85.05, 85.05) * M_PI / 180.0);
	col = std::clamp ((int)((lng + 180.0) / 360.0 * (1 << z)), 0, (1 << z) - 1);
	row = std::clamp ((int)((0.5 - log ((1 + s) / (1 - s)) / (4 * M_PI)) * W / 256.0), 0, (1 << z) - 1);
}

// metres at node (i, j) of a 259x259 .elv; 0 when unreadable
static double ElvNode (const std::string &e, int i, int j)
{
	if (e.size() < 100 || e.compare (0, 3, "ELE")) return 0.0;
	int32_t hs, dt;
	double sc, of;
	memcpy (&hs, &e[4], 4); memcpy (&dt, &e[8], 4); memcpy (&sc, &e[28], 8); memcpy (&of, &e[36], 8);
	if (dt != -16) return dt == 0 ? of : 0.0;
	if (hs < 100 || e.size() < (size_t)hs + 259 * 259 * 2) return 0.0;
	int16_t q;
	memcpy (&q, &e[(size_t)hs + ((size_t)i * 259 + j) * 2], 2);
	return q * sc + of;
}

// distance in metres from a point to a base part's edge (negative inside)
static double PartDist (const TsBase::Part &p, double nn, double ee)
{
	double dn = p.n1 - p.n0, de = p.e1 - p.e0, l2 = dn * dn + de * de;
	double t = l2 > 0 ? std::clamp (((nn - p.n0) * dn + (ee - p.e0) * de) / l2, 0.0, 1.0) : 0.0;
	return std::hypot (nn - p.n0 - t * dn, ee - p.e0 - t * de) - p.r;
}

bool Hits (const TsBase &b, double R, int L, int ilat, int ilng, double blend)
{
	double w, n, dw, dh;
	Bounds (L, ilat, ilng, w, n, dw, dh);
	double r = (b.ext + blend) / R * 180.0 / M_PI, la = b.lat * 180.0 / M_PI, ln = b.lng * 180.0 / M_PI;
	double rl = r / std::max (0.05, cos (b.lat));
	return la + r >= n - dh && la - r <= n && std::fabs (std::remainder (ln - (w + dw / 2), 360.0)) <= dw / 2 + rl; // across the date line too
}

// elev-consistency: an Elev_mod file of tile (L, ilat, ilng): the header, then the 259x259 nodes
static std::string ModFile (int L, int ilat, int ilng, const std::vector<int16_t> &z, double scale, double emin, double emax, double emean)
{
	const int N = 259;
	double w, n, dw, dh;
	Bounds (L, ilat, ilng, w, n, dw, dh);
	std::string out (100, '\0');
	auto i32 = [&out] (size_t at, int32_t v) { memcpy (&out[at], &v, 4); };
	auto f64 = [&out] (size_t at, double v) { memcpy (&out[at], &v, 8); };
	out[0] = 'E'; out[1] = 'L'; out[2] = 'E'; out[3] = 1;
	i32 (4, 100); i32 (8, -16); i32 (12, N); i32 (16, N); i32 (20, 1); i32 (24, 1);
	f64 (28, scale); f64 (36, 0.0);
	f64 (44, (n - dh) * M_PI / 180.0); f64 (52, n * M_PI / 180.0);
	f64 (60, w * M_PI / 180.0); f64 (68, (w + dw) * M_PI / 180.0);
	f64 (76, emin); f64 (84, emax); f64 (92, emean);
	out.append ((const char *)z.data(), z.size() * sizeof (int16_t));
	return out;
}

// Elev_mod of a tile: bases flat at their height, cosine blend ring, SHRT_MAX elsewhere; "" if untouched
std::string ElevMod (int L, int ilat, int ilng, const std::vector<std::pair<const TsBase *, double>> &bases, double R, double scale, const std::string &elev, double blend)
{
	const int N = 259;
	double w, n, dw, dh;
	Bounds (L, ilat, ilng, w, n, dw, dh);
	std::vector<int16_t> z ((size_t)N * N, SHRT_MAX);
	bool any = false;
	double emin = 1e9, emax = -1e9, sum = 0.0, cnt = 0.0;
	for (int i = 0; i < N; i++) {
		double lat = (n - dh + (i - 1) * dh / 256.0) * M_PI / 180.0;
		for (int j = 0; j < N; j++) {
			double lng = (w + (j - 1) * dw / 256.0) * M_PI / 180.0;
			double best = 1.0, wt = 0.0, H = 0.0;
			for (const auto &bh : bases) {
				const TsBase &b = *bh.first;
				double dl = remainder (lng - b.lng, 2.0 * M_PI);
				double nn = (lat - b.lat) * R, ee = dl * R * cos (b.lat);
				if (std::fabs (nn) > b.ext + blend || std::fabs (ee) > b.ext + blend) continue;
				double d = 1e30;
				for (const TsBase::Part &p : b.parts) d = std::min (d, PartDist (p, nn, ee));
				double k = d <= 0 ? 1.0 : d < blend ? 0.5 * (1.0 + cos (M_PI * d / blend)) : 0.0;
				if (k > 0 && (wt == 0 || d < best)) { best = d; wt = k; H = bh.second; }
			}
			if (wt <= 0) continue;
			double e0 = wt < 1.0 ? ElvNode (elev, i, j) : H;
			double m = H * wt + e0 * (1.0 - wt);
			double lo = std::isfinite (e0) ? std::min (0.0, e0) : 0.0; // the Elev tile's own floor (FLOOR boxes), else sea level
			int16_t q = (int16_t)std::clamp (std::lround (std::max (lo, std::isfinite (m) ? m : lo) / scale), -32767L, 32766L);
			z[(size_t)i * N + j] = q;
			any = true;
			cnt++;
			emin = std::min (emin, q * scale);
			emax = std::max (emax, q * scale);
			sum += q * scale;
		}
	}
	if (!any) return std::string();
	return ModFile (L, ilat, ilng, z, scale, emin, emax, sum / cnt);
}

// elev-consistency: an Elev_mod header the core reads (elevmgr.cpp), and its node as metres, NaN where it leaves the Elev alone
static bool ModHead (const std::string &m, int32_t &hs, int32_t &dt, double &sc, double &of)
{
	if (m.size() < 100 || m.compare (0, 3, "ELE")) return false;
	int32_t nx, ny;
	memcpy (&hs, &m[4], 4); memcpy (&dt, &m[8], 4); memcpy (&nx, &m[12], 4); memcpy (&ny, &m[16], 4);
	memcpy (&sc, &m[28], 8); memcpy (&of, &m[36], 8);
	if (nx != 259 || ny != 259 || hs < 100 || !std::isfinite (sc) || !std::isfinite (of)) return false;
	if (dt == 0) return true;
	if (dt == 8) return m.size() >= (size_t)hs + 259 * 259;
	if (dt == -16) return m.size() >= (size_t)hs + 259 * 259 * 2;
	return false;
}

bool ModOk (const std::string &m)
{
	int32_t hs, dt;
	double sc, of;
	return ModHead (m, hs, dt, sc, of);
}

static double ModNode (const std::string &m, int32_t hs, int32_t dt, double sc, double of, int i, int j)
{
	size_t at = (size_t)i * 259 + j;
	if (dt == 0) return of;
	if (dt == 8) {
		uint8_t q = (uint8_t)m[(size_t)hs + at];
		return q == UCHAR_MAX ? NAN : q * sc + of;
	}
	int16_t q;
	memcpy (&q, &m[(size_t)hs + at * 2], 2);
	return q == SHRT_MAX ? NAN : q * sc + of;
}

static bool ElvReadable (const std::string &e)
{
	if (e.size() < 100 || e.compare (0, 3, "ELE")) return false;
	int32_t hs, dt;
	memcpy (&hs, &e[4], 4); memcpy (&dt, &e[8], 4);
	return dt == 0 || (dt == -16 && hs >= 100 && e.size() >= (size_t)hs + 259 * 259 * 2);
}

// elev-consistency: the Elev_mod of a tile from an add-on's Elev_mod of its ancestor, bilinear over the ancestor's nodes;
// a node partly under modified ones blends to the tile's own Elev; "" if no node is modified
std::string InheritMod (int L, int ilat, int ilng, int aL, int ailat, int ailng, const std::string &amod, const std::string &elev, double scale)
{
	const int N = 259;
	int32_t hs, dt;
	double sc, of;
	if (aL >= L || aL < 4 || L - aL > 20 || (ilat >> (L - aL)) != ailat || (ilng >> (L - aL)) != ailng) return std::string();
	if (!ModHead (amod, hs, dt, sc, of) || !(scale > 0.0)) return std::string();
	bool own = ElvReadable (elev);
	double w, n, dw, dh, aw, an, adw, adh;
	Bounds (L, ilat, ilng, w, n, dw, dh);
	Bounds (aL, ailat, ailng, aw, an, adw, adh);
	std::vector<int16_t> z ((size_t)N * N, SHRT_MAX);
	bool any = false;
	double emin = 1e9, emax = -1e9, sum = 0.0, cnt = 0.0;
	for (int i = 0; i < N; i++) {
		double fi = (n - dh + (i - 1) * dh / 256.0 - (an - adh)) / adh * 256.0 + 1.0; // the ancestor's row, from the south
		int i0 = std::clamp ((int)std::floor (fi), 0, N - 2);
		double u = std::clamp (fi - i0, 0.0, 1.0);
		for (int j = 0; j < N; j++) {
			double fj = (w + (j - 1) * dw / 256.0 - aw) / adw * 256.0 + 1.0; // its column, from the west
			int j0 = std::clamp ((int)std::floor (fj), 0, N - 2);
			double v = std::clamp (fj - j0, 0.0, 1.0);
			const double wt[4] = {(1 - u) * (1 - v), (1 - u) * v, u * (1 - v), u * v};
			const double h[4] = {ModNode (amod, hs, dt, sc, of, i0, j0), ModNode (amod, hs, dt, sc, of, i0, j0 + 1),
			                     ModNode (amod, hs, dt, sc, of, i0 + 1, j0), ModNode (amod, hs, dt, sc, of, i0 + 1, j0 + 1)};
			double W = 0.0, S = 0.0;
			for (int q = 0; q < 4; q++)
				if (!std::isnan (h[q])) { W += wt[q]; S += wt[q] * h[q]; }
			if (W <= 1e-9) continue;
			double m;
			if (W >= 1.0 - 1e-9) m = S / W;
			else if (own) m = S + (1.0 - W) * ElvNode (elev, i, j);
			else continue; // the Elev unreadable: no blend toward 0 m
			int16_t q = (int16_t)std::clamp (std::lround (m / scale), -32767L, 32766L);
			z[(size_t)i * N + j] = q;
			any = true;
			cnt++;
			emin = std::min (emin, q * scale);
			emax = std::max (emax, q * scale);
			sum += q * scale;
		}
	}
	if (!any) return std::string();
	return ModFile (L, ilat, ilng, z, scale, emin, emax, sum / cnt);
}

} // namespace TsSky

// a base cfg: Location, objects as discs (POS, SCALE), runways as capsules; relpos x south, z east (Base::Rel_EquPos)
bool TsReadBase (const std::string &file, TsBase &b)
{
	std::ifstream f (file);
	if (!f) return false;
	std::string line, obj;
	bool loc = false, inlist = false;
	double px = 0, pz = 0, sx = 0, sz = 0, sc = 1, e1[3] = {0, 0, 0}, e2[3] = {0, 0, 0}, wid = 0;
	bool pos = false, rw1 = false, rw2 = false;
	auto close = [&] () {
		if (obj == "RUNWAY" && rw1 && rw2) b.parts.push_back ({-e1[0], e1[2], -e2[0], e2[2], std::max (wid, 30.0) * 0.5 + 20.0});
		else if (pos && obj != "RUNWAYLIGHTS" && obj.compare (0, 5, "TRAIN") && obj != "BEACONARRAY") {
			double r = sx > 0 ? 0.5 * std::hypot (sx, sz) : obj.compare (0, 4, "LPAD") == 0 ? 40.0 * sc : obj.compare (0, 6, "HANGAR") == 0 ? 60.0 * sc : 30.0 * sc;
			b.parts.push_back ({-px, pz, -px, pz, std::min (r, 500.0) + 15.0});
		}
		obj.clear(); pos = rw1 = rw2 = false; sx = sz = 0; sc = 1; wid = 0;
	};
	while (std::getline (f, line)) {
		size_t c = line.find (';');
		if (c != std::string::npos) line.erase (c);
		std::istringstream is (line);
		std::string w;
		if (!(is >> w)) continue;
		if (!inlist) {
			if (w == "Location") { std::string eq; double lng, lat; if (is >> eq >> lng >> lat) { b.lng = lng * M_PI / 180.0; b.lat = lat * M_PI / 180.0; loc = true; } }
			else if (w == "BEGIN_OBJECTLIST") inlist = true;
			continue;
		}
		if (w == "END_OBJECTLIST") break;
		if (obj.empty()) { obj = w; continue; }
		if (w == "END") { close (); continue; }
		if (w == "POS") { double y; pos = (bool)(is >> px >> y >> pz); }
		else if (w == "SCALE") { double a, y, z; if (is >> a) { if (is >> y >> z) { sx = a; sz = z; } else sc = a; } }
		else if (w == "END1") rw1 = (bool)(is >> e1[0] >> e1[1] >> e1[2]);
		else if (w == "END2") rw2 = (bool)(is >> e2[0] >> e2[1] >> e2[2]);
		else if (w == "WIDTH") is >> wid;
	}
	if (!loc) return false;
	b.parts.push_back ({0, 0, 0, 0, 100.0}); // the base's own point: the core takes its height there
	b.ext = 0.0;
	for (const TsBase::Part &p : b.parts) b.ext = std::max (b.ext, std::max (std::hypot (p.n0, p.e0), std::hypot (p.n1, p.e1)) + p.r);
	return true;
}

namespace TsTrack {

// two-body motion from r0, v0 (planet frame at t0, y = spin axis), lat/lng under it as the planet turns at omega
std::vector<Pt> Propagate (const double r0[3], const double v0[3], double mu, double omega, double horizon, int n)
{
	std::vector<Pt> out;
	n = std::max (1, n);
	double r = std::sqrt (r0[0] * r0[0] + r0[1] * r0[1] + r0[2] * r0[2]);
	double v2 = v0[0] * v0[0] + v0[1] * v0[1] + v0[2] * v0[2];
	if (r <= 0 || mu <= 0) return out;
	double a = 1.0 / (2.0 / r - v2 / mu), sig = (r0[0] * v0[0] + r0[1] * v0[1] + r0[2] * v0[2]) / std::sqrt (mu);
	double p[3] = {r0[0], r0[1], r0[2]}, v[3] = {v0[0], v0[1], v0[2]};
	for (int i = 0; i <= n; i++) {
		double t = horizon * i / n;
		if (std::isfinite (a) && a > 0) { // elliptic: Kepler's equation in the eccentric anomaly change; parabolic steps
			double sa = std::sqrt (a), M = std::sqrt (mu / (a * a * a)) * t, E = M;
			for (int it = 0; it < 50; it++) {
				double f = E + sig / sa * (1 - cos (E)) - (1 - r / a) * sin (E) - M;
				double fp = 1 + sig / sa * sin (E) - (1 - r / a) * cos (E);
				double d = std::clamp (f / fp, -1.0, 1.0);
				E -= d;
				if (std::fabs (d) < 1e-12) break;
			}
			double F = 1 - a / r * (1 - cos (E)), G = t - std::sqrt (a * a * a / mu) * (E - sin (E));
			for (int k = 0; k < 3; k++) p[k] = F * r0[k] + G * v0[k];
		} else if (i) { // escape: small leapfrog steps
			double h = horizon / n / 8;
			for (int s = 0; s < 8; s++) {
				double q = std::sqrt (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), g = -mu / (q * q * q);
				for (int k = 0; k < 3; k++) v[k] += 0.5 * h * g * p[k];
				for (int k = 0; k < 3; k++) p[k] += h * v[k];
				q = std::sqrt (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]), g = -mu / (q * q * q);
				for (int k = 0; k < 3; k++) v[k] += 0.5 * h * g * p[k];
			}
		}
		double q = std::sqrt (p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
		out.push_back ({remainder (atan2 (p[2], p[0]) - omega * t, 2.0 * M_PI), asin (p[1] / q), t});
	}
	return out;
}

// tiles under the track, nearest first: half tiles to each side at level L (>= 4)
std::vector<std::array<int, 3>> Keys (const std::vector<Pt> &pts, int L, int half)
{
	std::vector<std::array<int, 3>> out;
	if (L < 4) return out;
	std::unordered_set<uint64_t> seen;
	int nlat = 1 << (L - 4), nlng = 1 << (L - 3);
	double span = M_PI / nlat;
	for (const Pt &p : pts) {
		if (!std::isfinite (p.lat) || !std::isfinite (p.lng)) continue;
		int ia = std::clamp ((int)((M_PI / 2 - p.lat) / span), 0, nlat - 1), ib = std::clamp ((int)((p.lng + M_PI) / span), 0, nlng - 1);
		for (int da = -half; da <= half; da++)
			for (int db = -half; db <= half; db++) {
				int a = ia + da, c = ((ib + db) % nlng + nlng) % nlng;
				if (a < 0 || a >= nlat || !seen.insert ((uint64_t)a << 32 | (uint32_t)c).second) continue;
				out.push_back ({L, a, c});
			}
	}
	return out;
}

// the surface level the client shows from this altitude, give or take one
int Level (double alt, int lmin, int lmax)
{
	int L = 4 + (int)std::lround (std::log2 (5e7 / std::max (alt, 1000.0)));
	return std::clamp (L, lmin, lmax);
}

} // namespace TsTrack
