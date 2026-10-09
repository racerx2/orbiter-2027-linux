// not upstream: dent math for collision damage (D4); Vecmat only, no logging, -ffp-contract=off

#include "DentMath.h"
#include <algorithm>
#include <array>
#include <cfloat>
#include <charconv>
#include <cmath>
#include <cstring>
#include <unordered_map>

namespace {

// small helpers; products written in Vecmat.h order, so -ffp-contract=off covers every one (D4 12)

inline double Dot (const Vector &a, const Vector &b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vector Cross (const Vector &a, const Vector &b) { return Vector (a.y * b.z - b.y * a.z, a.z * b.x - b.z * a.x, a.x * b.y - b.x * a.y); }
inline double Len2 (const Vector &a) { return a.x * a.x + a.y * a.y + a.z * a.z; }
inline double Len (const Vector &a) { return std::sqrt (Len2 (a)); }
inline Vector Pos (const DentVtx &v) { return Vector (v.x, v.y, v.z); }
inline Vector Nml (const DentVtx &v) { return Vector (v.nx, v.ny, v.nz); }
inline bool Finite (const Vector &v) { return std::isfinite (v.x) && std::isfinite (v.y) && std::isfinite (v.z); }
inline bool Within (const Vector &v, double lim) { return std::fabs (v.x) <= lim && std::fabs (v.y) <= lim && std::fabs (v.z) <= lim; } // false for NaN

double Smooth (double a, double b, double x)
{
	double t = (x - a) / (b - a);
	t = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
	return t * t * (3.0 - 2.0 * t);
}

// cur += field(rest), one float cast per record; true if position changed; none past float range
bool AddField (const DentParams &p, const DentVtx &r, DentVtx &c)
{
	Vector rp = Pos (r);
	double q = DentMath::Weight (p, rp);
	if (q == 0.0) return false;
	Vector d = p.n * (-(p.h * q)); // the expression of DentMath::Displace
	double dx = (double)c.x + d.x, dy = (double)c.y + d.y, dz = (double)c.z + d.z;
	if (!(std::fabs (dx) <= FLT_MAX && std::fabs (dy) <= FLT_MAX && std::fabs (dz) <= FLT_MAX)) return false;
	float x = (float)dx, y = (float)dy, z = (float)dz;
	bool ch = (x != c.x || y != c.y || z != c.z);
	c.x = x, c.y = y, c.z = z;
	return ch;
}

struct KeyHash { size_t operator() (uint64_t k) const { k ^= k >> 33; k *= 0xff51afd7ed558ccdULL; k ^= k >> 33; k *= 0xc4ceb9fe1a85ec53ULL; k ^= k >> 33; return (size_t)k; } };
struct Cell { int64_t x, y, z; bool operator== (const Cell &o) const { return x == o.x && y == o.y && z == o.z; } };
struct CellHash { size_t operator() (const Cell &c) const { KeyHash h; return h ((uint64_t)c.x * 0x9e3779b97f4a7c15ULL ^ h ((uint64_t)c.y + 0x632be59bd9b4e019ULL * (uint64_t)c.z)); } };

inline uint64_t EKey (uint32_t a, uint32_t b) { return a < b ? ((uint64_t)a << 32) | b : ((uint64_t)b << 32) | a; }

// area vector of a triangle (half the cross product)
inline Vector Area (const Vector &a, const Vector &b, const Vector &c) { return Cross (b - a, c - a) * 0.5; }

// grid cell of a position on the 0.1 mm weld grid; false beyond 1e12 m or non-finite
bool GridKey (const Vector &p, std::array<long long, 3> &k)
{
	if (!Finite (p) || !Within (p, 1e12)) return false;
	k = { std::llround (p.x / DENT_WELD), std::llround (p.y / DENT_WELD), std::llround (p.z / DENT_WELD) };
	return true;
}

// minimal rotation taking unit u to unit w, applied to x (Rodrigues)
Vector RotateMin (const Vector &x, const Vector &u, const Vector &w)
{
	Vector v = Cross (u, w);
	double s = Len (v), c = Dot (u, w);
	if (s < 1e-12) {
		if (c > 0.0) return x;
		Vector a = std::fabs (u.x) < 0.6 ? Vector (1, 0, 0) : std::fabs (u.y) < 0.6 ? Vector (0, 1, 0) : Vector (0, 0, 1);
		Vector k = Cross (u, a);
		k /= Len (k);
		return k * (2.0 * Dot (k, x)) - x; // half turn about an axis normal to u
	}
	Vector k = v / s;
	return x * c + Cross (k, x) * s + k * (Dot (k, x) * (1.0 - c));
}

// ASCII text helpers (no locale, no POSIX)

inline char Lower (char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

bool IEq (const char *a, size_t na, const char *b)
{
	size_t nb = std::strlen (b);
	if (na != nb) return false;
	for (size_t i = 0; i < na; i++) if (Lower (a[i]) != Lower (b[i])) return false;
	return true;
}

struct Tok { const char *p; size_t n; };

// tokens split on spaces and tabs; the line ends at NUL, CR or LF
void Split (const char *s, std::vector<Tok> &t)
{
	t.clear ();
	if (!s) return;
	for (;;) {
		while (*s == ' ' || *s == '\t') s++;
		if (!*s || *s == '\r' || *s == '\n') return;
		const char *b = s;
		while (*s && *s != ' ' && *s != '\t' && *s != '\r' && *s != '\n') s++;
		t.push_back ({ b, (size_t)(s - b) });
	}
}

// the line without leading blanks, cut at CR/LF, trailing blanks removed
std::string Trimmed (const char *s)
{
	while (*s == ' ' || *s == '\t') s++;
	size_t n = 0;
	while (s[n] && s[n] != '\r' && s[n] != '\n') n++;
	while (n && (s[n-1] == ' ' || s[n-1] == '\t')) n--;
	return std::string (s, n);
}

// rest of the line after token t (blanks trimmed)
std::string RestOf (const Tok &t)
{
	return Trimmed (t.p + t.n);
}

bool ParseD (const Tok &t, double &v)
{
	const char *b = t.p, *e = t.p + t.n;
	if (b < e && *b == '+' && ++b < e && *b == '-') return false; // "+-" is not a number
	if (b == e) return false;
	double x = 0.0;
	auto r = std::from_chars (b, e, x, std::chars_format::general);
	if (r.ec != std::errc () || r.ptr != e || !std::isfinite (x)) return false;
	v = x;
	return true;
}

template<class T> bool ParseInt (const char *b, const char *e, T &v, int base = 10)
{
	if (b < e && *b == '+' && ++b < e && *b == '-') return false; // "+-" is not a number
	if (b == e) return false;
	T x = 0;
	auto r = std::from_chars (b, e, x, base);
	if (r.ec != std::errc () || r.ptr != e) return false;
	v = x;
	return true;
}
template<class T> bool ParseInt (const Tok &t, T &v, int base = 10) { return ParseInt (t.p, t.p + t.n, v, base); }

void PutNum (std::string &s, double v)
{
	char b[48];
	auto r = std::to_chars (b, b + sizeof (b), v, std::chars_format::general, 9); // printf %.9g
	s.append (b, r.ptr);
}

template<class T> void PutInt (std::string &s, T v)
{
	char b[24];
	auto r = std::to_chars (b, b + sizeof (b), v);
	s.append (b, r.ptr);
}

void PutHex8 (std::string &s, uint32_t v)
{
	char b[16];
	auto r = std::to_chars (b, b + sizeof (b), v, 16);
	s.append (8 - (size_t)(r.ptr - b), '0');
	s.append (b, r.ptr);
}

double Q9 (double v)
{
	if (!std::isfinite (v)) return v;
	char b[48];
	auto r = std::to_chars (b, b + sizeof (b), v, std::chars_format::general, 9);
	double o = v;
	std::from_chars (b, r.ptr, o, std::chars_format::general);
	return o;
}

// a value a writer stores: NaN as 0, else clamped to [lo, hi]
double Clamp (double v, double lo, double hi)
{
	if (std::isnan (v)) return 0.0;
	return std::min (std::max (v, lo), hi);
}

// free text for a line: tabs kept; line breaks, NUL, other control chars replaced unless verbatim
std::string Clean (const std::string &s, bool verbatim)
{
	std::string o (s);
	for (char &c : o) {
		bool brk = c == '\r' || c == '\n' || c == '\0';
		bool ctl = ((unsigned char)c < 0x20 && c != '\t') || c == 0x7f;
		if (brk || (ctl && !verbatim)) c = '_';
	}
	return o;
}

// cut to n bytes without splitting a UTF-8 sequence
void CutUtf8 (std::string &s, size_t n)
{
	if (s.size () <= n) return;
	while (n > 0 && ((unsigned char)s[n] & 0xc0) == 0x80) n--;
	s.resize (n);
}

// 9.2 limits: finite, |c| <= LIM_POS, 0 < R <= LIM_R, 0 <= h <= LIM_H, 0 <= T <= LIM_T, |n| 0.5..2
bool ParamsOk (const DentParams &p)
{
	if (!Within (p.c, DENT_LIM_POS) || !Finite (p.n)) return false;
	if (!(p.R > 0.0 && p.R <= DENT_LIM_R && p.h >= 0.0 && p.h <= DENT_LIM_H && p.T >= 0.0 && p.T <= DENT_LIM_T)) return false;
	double n2 = Len2 (p.n);
	return n2 >= 0.25 && n2 <= 4.0;
}

// " cx cy cz nx ny nz R h T"
void PutParams (std::string &s, const DentParams &p)
{
	const double v[9] = { p.c.x, p.c.y, p.c.z, p.n.x, p.n.y, p.n.z, p.R, p.h, p.T };
	for (double x : v) { s += ' '; PutNum (s, x); }
}

// 9.2 skip rules on nine numbers from t[i]; n normalised when off unit length, then quantised
bool ParseParams (const std::vector<Tok> &t, size_t i, DentParams &p)
{
	if (t.size () < i + 9) return false;
	double v[9];
	for (size_t k = 0; k < 9; k++) if (!ParseD (t[i+k], v[k])) return false;
	DentParams q {};
	q.c = Vector (v[0], v[1], v[2]);
	q.n = Vector (v[3], v[4], v[5]);
	q.R = v[6], q.h = v[7], q.T = v[8];
	if (!ParamsOk (q)) return false;
	double n2 = Len2 (q.n);
	if (std::fabs (n2 - 1.0) > 1e-6) q.n /= std::sqrt (n2); // saved records (|n| = 1 within 1e-9) stay bitwise
	DentMath::Quantise (q);
	p = q;
	return true;
}

// "2,31,51" or "*" (empty list); a trailing ',' marks a list continued on the next line (more)
bool ParseGroups (const Tok &t, std::vector<uint16_t> &g, bool &more)
{
	g.clear ();
	more = false;
	if (t.n == 1 && t.p[0] == '*') return true;
	const char *s = t.p, *e = t.p + t.n;
	if (t.n > 1 && e[-1] == ',') more = true, e--;
	while (s <= e) {
		const char *c = s;
		while (c < e && *c != ',') c++;
		uint32_t v;
		if (!ParseInt (s, c, v) || v > 65535) return false;
		g.push_back ((uint16_t)v);
		s = c + 1;
	}
	return !g.empty ();
}

// head + groups in lines of <= lim chars; every line repeats head, all but the last end in ','
void GroupLines (const std::string &head, const std::vector<uint16_t> &grp, size_t lim, std::vector<std::string> &out)
{
	if (grp.empty ()) { out.push_back (head + " *"); return; }
	std::string s = head;
	bool first = true;
	for (uint16_t g : grp) {
		std::string t;
		PutInt (t, (unsigned)g);
		if (!first && s.size () + 1 + t.size () + 1 > lim) { out.push_back (s + ","); s = head; first = true; } // room for the marker kept
		s += first ? ' ' : ',';
		s += t;
		first = false;
	}
	out.push_back (s);
}

bool SameBits (double a, double b) { return std::memcmp (&a, &b, sizeof (double)) == 0; }

bool SameParams (const DentParams &a, const DentParams &b)
{
	return SameBits (a.c.x, b.c.x) && SameBits (a.c.y, b.c.y) && SameBits (a.c.z, b.c.z)
		&& SameBits (a.n.x, b.n.x) && SameBits (a.n.y, b.n.y) && SameBits (a.n.z, b.n.z)
		&& SameBits (a.R, b.R) && SameBits (a.h, b.h) && SameBits (a.T, b.T);
}

// materials (3.1; sigma_c in Pa)

const DentMaterial g_mat[] = {
	{ "al_structure",    0.5e6, 0.5,   1.0, 0.3, 1.0, 0.5, false },
	{ "al_skin",         0.2e6, 0.3,   1.0, 0.3, 1.0, 0.5, false },
	{ "al_honeycomb",    2.0e6, 0.035, 1.0, 0.1, 1.0, 0.5, false },
	{ "gear",            0.5e6, 0.3,   3.0, 0.1, 3.0, 0.5, false },
	{ "steel_structure", 1.0e6, 2.0,   1.0, 0.3, 1.0, 0.5, false },
	{ "steel_clad",      0.05e6, 2.0,  1.0, 0.3, 1.0, 0.5, false },
	{ "building_rc",     0.4e6, 3.0,   1.0, 0.3, 1.0, 0.5, false },
	{ "concrete",        30e6,  0.3,   1.0, 0.2, 1.0, 0.6, false },
	{ "glass",           0.5e6, 0.5,   1.0, 0.3, 1.0, 0.5, true  }, // host values replace these; al_structure as a fallback
};
enum { M_AL, M_SKIN, M_HONEY, M_GEAR, M_STEEL, M_CLAD, M_RC, M_CONCRETE, M_GLASS };

// LEPP refinement on a working copy (4.9)

struct RTri { uint32_t g, v[3]; };

struct Refiner {
	DentObject o;                                                   // working copy, committed only on success
	std::vector<RTri> T;
	std::vector<uint8_t> live, fixd;                                // fixd: invalid, degenerate or out of range, never chooses a split (degenerate ones follow their edges)
	std::unordered_map<uint64_t, std::vector<uint32_t>, KeyHash> edges; // welded edge -> live triangles
	std::unordered_map<uint64_t, uint32_t, KeyHash> wmid;           // welded edge -> weld id of its midpoint
	std::vector<std::unordered_map<uint64_t, uint32_t, KeyHash>> mid; // per group: local vertex pair -> midpoint vertex, so hard edges and UV seams keep their sides
	std::vector<Vector> P;                                          // per weld id: representative rest position
	std::vector<uint32_t> glim, gadd;                               // per group: WORD index room, vertices added
	std::vector<uint32_t> slot, room, sadd;                         // per group: budget slot; per slot: vertex room, vertices added
	std::vector<uint8_t> split;                                     // per weld id: on a split triangle
	const DentParams *prior = nullptr; size_t nprior = 0;
	size_t nlive = 0, ntot = 0;
	bool fail = false;

	void Mark (uint32_t w)
	{
		if (w >= split.size ()) split.resize ((size_t)w + 1, 0);
		split[w] = 1;
	}
	void Link (uint32_t t)
	{
		const RTri &r = T[t];
		for (int e = 0; e < 3; e++) {
			std::vector<uint32_t> &l = edges[EKey (o.weld[r.g][r.v[e]], o.weld[r.g][r.v[(e+1)%3]])];
			if (std::find (l.begin (), l.end (), t) == l.end ()) l.push_back (t); // a degenerate triangle has one welded edge twice
		}
	}
	void Unlink (uint32_t t)
	{
		const RTri &r = T[t];
		for (int e = 0; e < 3; e++) {
			std::vector<uint32_t> &l = edges[EKey (o.weld[r.g][r.v[e]], o.weld[r.g][r.v[(e+1)%3]])];
			auto it = std::find (l.begin (), l.end (), t);
			if (it != l.end ()) l.erase (it);
		}
	}
	// longest edge by (length in nm, welded key); ties resolve alike in every triangle; |x| < 1e9 m
	uint64_t Longest (uint32_t t, double &L2) const
	{
		const RTri &r = T[t];
		uint64_t best = 0; long long bl = -1;
		L2 = 0.0;
		for (int e = 0; e < 3; e++) {
			uint32_t a = o.weld[r.g][r.v[e]], b = o.weld[r.g][r.v[(e+1)%3]];
			double l2 = Len2 (P[a] - P[b]);
			long long ln = std::llround (std::sqrt (l2) * 1e9);
			uint64_t k = EKey (a, b);
			if (ln > bl || (ln == bl && k > best)) bl = ln, best = k, L2 = l2;
		}
		return best;
	}
	bool InBall (uint32_t t, const Vector &c, double R) const
	{
		const RTri &r = T[t];
		Vector p[3] = { Pos (o.rest[r.g][r.v[0]]), Pos (o.rest[r.g][r.v[1]]), Pos (o.rest[r.g][r.v[2]]) };
		Vector cen = (p[0] + p[1] + p[2]) / 3.0;
		double rad = std::max (std::max (Len (cen - p[0]), Len (cen - p[1])), Len (cen - p[2]));
		return Len (cen - c) <= R + rad;
	}
	uint32_t MidVertex (uint32_t g, uint32_t x, uint32_t y, uint32_t wm)
	{
		uint64_t key = EKey (x, y);
		auto f = mid[g].find (key);
		if (f != mid[g].end ()) return f->second;
		uint32_t s = slot[g];
		if (gadd[g] >= glim[g] || sadd[s] >= room[s]) { fail = true; return 0; }
		const DentVtx &a = o.rest[g][x], &b = o.rest[g][y];
		DentVtx r;
		r.x = (float)(0.5 * ((double)a.x + (double)b.x));
		r.y = (float)(0.5 * ((double)a.y + (double)b.y));
		r.z = (float)(0.5 * ((double)a.z + (double)b.z));
		Vector nm = (Nml (a) + Nml (b)) * 0.5;
		double l = Len (nm);
		if (l > 1e-12) nm /= l;
		r.nx = (float)nm.x, r.ny = (float)nm.y, r.nz = (float)nm.z;
		r.tu = (float)(0.5 * ((double)a.tu + (double)b.tu));
		r.tv = (float)(0.5 * ((double)a.tv + (double)b.tv));
		DentVtx c = r;
		for (size_t k = 0; k < nprior; k++) AddField (prior[k], r, c); // cur = rest + earlier records, as if it had always existed
		uint32_t i = (uint32_t)o.rest[g].size ();
		o.rest[g].push_back (r);
		o.cur[g].push_back (c);
		o.weld[g].push_back (wm);
		if (wm == P.size ()) P.push_back (Pos (r)); // first vertex of a new weld id represents it
		gadd[g]++, sadd[s]++;
		mid[g][key] = i;
		return i;
	}
	bool HasEdge (uint32_t t, uint64_t e) const
	{
		const RTri &r = T[t];
		for (int k = 0; k < 3; k++) if (EKey (o.weld[r.g][r.v[k]], o.weld[r.g][r.v[(k+1)%3]]) == e) return true;
		return false;
	}
	void Bisect (uint64_t e, std::vector<uint32_t> ts)
	{
		uint32_t wm;
		auto f = wmid.find (e);
		if (f != wmid.end ()) wm = f->second;
		else wmid[e] = wm = o.nweld++;
		for (size_t i = 0; i < ts.size (); i++) {
			uint32_t t = ts[i];
			RTri r = T[t];
			int k = 0;
			while (k < 3 && EKey (o.weld[r.g][r.v[k]], o.weld[r.g][r.v[(k+1)%3]]) != e) k++;
			uint32_t x = r.v[k], y = r.v[(k+1)%3], z = r.v[(k+2)%3];
			uint32_t m = MidVertex (r.g, x, y, wm);
			if (fail) return;
			Mark (o.weld[r.g][x]), Mark (o.weld[r.g][y]), Mark (o.weld[r.g][z]), Mark (wm);
			Unlink (t);
			live[t] = 0;
			uint8_t fx = fixd[t]; // children of a degenerate triangle follow splits only
			T.push_back ({ r.g, { x, m, z } }); live.push_back (1); fixd.push_back (fx); Link ((uint32_t)T.size () - 1);
			T.push_back ({ r.g, { m, y, z } }); live.push_back (1); fixd.push_back (fx); Link ((uint32_t)T.size () - 1);
			nlive++, ntot++;
			for (uint32_t c = (uint32_t)T.size () - 2; fx && c < T.size (); c++) if (HasEdge (c, e)) ts.push_back (c); // its second copy of e
		}
	}
	// LEPP: split longer neighbours until e is longest in all its triangles, then split them together
	void Lepp (uint64_t e0)
	{
		std::vector<uint64_t> st (1, e0);
		while (!st.empty () && !fail) {
			uint64_t e = st.back ();
			auto it = edges.find (e);
			if (it == edges.end () || it->second.empty ()) { st.pop_back (); continue; }
			std::vector<uint32_t> ts = it->second;
			std::sort (ts.begin (), ts.end ());
			bool pushed = false;
			ts.erase (std::unique (ts.begin (), ts.end ()), ts.end ());
			for (uint32_t t : ts) {
				if (fixd[t]) continue; // degenerate triangles never choose the edge
				double l2;
				uint64_t k = Longest (t, l2);
				if (k != e) { st.push_back (k); pushed = true; break; }
			}
			if (pushed) continue;
			Bisect (e, ts);
			st.pop_back ();
		}
	}
};

} // namespace

// field (4.1)

double DentMath::Kernel (double t2)
{
	if (!(t2 < 1.0)) return 0.0;
	double a = 1.0 - t2;
	return a * a;
}

double DentMath::Weight (const DentParams &p, const Vector &rest)
{
	if (!(p.R > 0.0)) return 0.0;
	double k = Kernel (Len2 (rest - p.c) / (p.R * p.R));
	if (k == 0.0 || !(p.T > 0.0)) return k;
	double s = Dot (p.c - rest, p.n);
	return k * (1.0 - Smooth (DENT_SLAB_LO * p.T, DENT_SLAB_HI * p.T, s));
}

Vector DentMath::Displace (const DentParams &p, const Vector &rest)
{
	return p.n * (-(p.h * Weight (p, rest)));
}

Vector DentMath::Field (const void *ctx, const Vector &rest)
{
	const DentRecord *r = (const DentRecord*)ctx;
	return r ? Displace (r->p, rest) : Vector ();
}

// radius, slab, depth, caps (4.2-4.5)

double DentMath::Radius (double E, double sigma_c, double a, double Rmax)
{
	double V = (E > 0.0 && sigma_c > 0.0) ? E / sigma_c : 0.0;
	double R0 = std::cbrt (3.0 * V / (Pi * DENT_KAPPA));
	return std::min (std::max (R0, a), Rmax);
}

double DentMath::LowPolyFloor (const DentMeshView &m, const Vector &c)
{
	std::vector<uint8_t> used (m.nv, 0);
	for (size_t i = 0; i < 3 * m.nt; i++) if (m.tri[i] < m.nv) used[m.tri[i]] = 1;
	std::vector<std::pair<std::array<long long, 3>, uint32_t>> k;
	for (size_t v = 0; v < m.nv; v++) {
		std::array<long long, 3> g;
		if (used[v] && GridKey (m.rest[v], g)) k.push_back ({ g, (uint32_t)v });
	}
	std::sort (k.begin (), k.end ());
	std::vector<double> d;
	for (size_t i = 0; i < k.size (); i++)
		if (i == 0 || k[i].first != k[i-1].first) d.push_back (Len (m.rest[k[i].second] - c)); // distinct positions on the weld grid
	if (d.empty ()) return 0.0;
	size_t i4 = std::min<size_t> (3, d.size () - 1);
	std::nth_element (d.begin (), d.begin () + (std::ptrdiff_t)i4, d.end ());
	return d[i4] / DENT_FLOOR;
}

bool DentMath::RayCast (const DentMeshView &m, const Vector &o, const Vector &d, double tmin, double tmax, double &t)
{
	bool hit = false;
	double best = tmax;
	for (size_t i = 0; i < m.nt; i++) {
		const uint32_t *v = m.tri + 3 * i;
		if (v[0] >= m.nv || v[1] >= m.nv || v[2] >= m.nv) continue;
		const Vector &p0 = m.rest[v[0]];
		Vector e1 = m.rest[v[1]] - p0, e2 = m.rest[v[2]] - p0;
		Vector pv = Cross (d, e2);
		double det = Dot (e1, pv);
		if (!(std::fabs (det) > 1e-12 * Len (Cross (e1, e2)) * Len (d))) continue; // parallel or degenerate
		double inv = 1.0 / det;
		Vector s = o - p0;
		double u = Dot (s, pv) * inv;
		if (u < 0.0 || u > 1.0) continue;
		Vector qv = Cross (s, e1);
		double w = Dot (d, qv) * inv;
		if (w < 0.0 || u + w > 1.0) continue;
		double tt = Dot (e2, qv) * inv;
		if (tt >= tmin && tt <= best && (!hit || tt < best)) best = tt, hit = true;
	}
	if (hit) t = best;
	return hit;
}

double DentMath::SlabT (double rayT, double R)
{
	return (rayT > 0.0 && rayT >= DENT_PLATE * R && rayT <= DENT_LIM_T) ? rayT : 0.0;
}

double DentMath::VolumeFactor (const DentParams &p, const DentMeshView &m)
{
	struct Ent { std::array<long long, 9> key; double s; };
	const Vector *cur = m.cur ? m.cur : m.rest;
	std::vector<double> q (m.nv);
	for (size_t v = 0; v < m.nv; v++) q[v] = Weight (p, m.rest[v]);
	std::vector<Ent> e;
	for (size_t i = 0; i < m.nt; i++) {
		const uint32_t *v = m.tri + 3 * i;
		if (v[0] >= m.nv || v[1] >= m.nv || v[2] >= m.nv) continue;
		double qs = q[v[0]] + q[v[1]] + q[v[2]];
		if (!(qs > 0.0)) continue;
		Vector A = Area (cur[v[0]], cur[v[1]], cur[v[2]]);
		double an = Dot (A, p.n);
		if (!(an > -1e-4 * Len (A))) continue; // front faces; edge-on faces (float-rounded walls) count signed, so the swept volume stays exact
		std::array<long long, 3> g[3];
		if (!GridKey (m.rest[v[0]], g[0]) || !GridKey (m.rest[v[1]], g[1]) || !GridKey (m.rest[v[2]], g[2])) continue;
		std::sort (g, g + 3);
		e.push_back ({ { g[0][0], g[0][1], g[0][2], g[1][0], g[1][1], g[1][2], g[2][0], g[2][1], g[2][2] }, an * (qs / 3.0) });
	}
	std::sort (e.begin (), e.end (), [] (const Ent &a, const Ent &b) { return a.key < b.key || (a.key == b.key && a.s < b.s); });
	double S = 0.0;
	for (size_t i = 0; i < e.size (); i++)
		if (i + 1 == e.size () || e[i+1].key != e[i].key) S += e[i].s; // coincident triangles on the weld grid (double-sided, duplicates, decals) count once, the largest
	return S;
}

double DentMath::Dmax (double tcap, double T, double R, double L)
{
	return std::min (std::min (tcap, T > 0.0 ? DENT_DMAX_T * T : DENT_DMAX_R * R), DENT_DMAX_L * L);
}

double DentMath::DmaxVessel (double T, double R, double L)
{
	double d = std::min (DENT_DMAX_R * R, DENT_DMAX_L * L);
	return T > 0.0 ? std::min (d, DENT_DMAX_T * T) : d;
}

double DentMath::DepthCap (const DentParams &p, const DentMeshView &m, double Dmax)
{
	return DepthCap (p, m, nullptr, Dmax);
}

double DentMath::DepthCap (const DentParams &p, const DentMeshView &m, const DentMeshView *extra, double Dmax)
{
	double hi = HUGE_VAL, all = HUGE_VAL;
	bool any = false;
	for (const DentMeshView *x : { &m, extra }) {
		if (!x || !x->rest) continue;
		const Vector *cur = x->cur ? x->cur : x->rest;
		for (size_t v = 0; v < x->nv; v++) {
			double q = Weight (p, x->rest[v]);
			if (!(q > 0.0)) continue;
			double c = (Dmax - Dot (x->rest[v] - cur[v], p.n)) / q;
			all = std::min (all, c);
			if (q > DENT_CAP_Q) hi = std::min (hi, c), any = true;
		}
	}
	return any ? hi : all; // no vertex above 0.05: every weighted vertex caps
}

int DentMath::Solve (const DentInput &in, const DentMeshView &m, DentParams &out)
{
	return Solve (in, m, nullptr, out);
}

int DentMath::Solve (const DentInput &in, const DentMeshView &m, const DentMeshView *capView, DentParams &out)
{
	const DentMaterial &mat = in.mat ? *in.mat : DefaultMaterial (-1);
	double ln = Len (in.n);
	out.c = in.c, out.n = in.n, out.R = 0.0, out.h = 0.0, out.T = 0.0;
	if (!(ln > 0.0) || !std::isfinite (ln) || !Finite (in.c)) return DENT_NOSURFACE;
	if (ln != 1.0) out.n = in.n / ln;
	double V = (in.E > 0.0 && mat.sigma_c > 0.0) ? in.E / mat.sigma_c : 0.0;
	double R = Radius (in.E, mat.sigma_c, in.a, in.Rmax);
	if (in.vessel) R = std::max (R, LowPolyFloor (m, in.c)); // low-poly floor (4.2 step 3), also above Rmax
	out.R = R;
	out.T = SlabT (in.rayT, R);
	if (!(R > 0.0) || R > DENT_LIM_R || !Within (in.c, DENT_LIM_POS)) return DENT_NOSURFACE; // beyond what a record can hold (9.2)
	double S = VolumeFactor (out, m);
	if (!(S > 0.0)) return DENT_NOSURFACE;
	double h = V / S;
	double cap = DepthCap (out, m, capView, in.vessel ? DmaxVessel (out.T, R, in.L) : Dmax (mat.t_cap, out.T, R, in.L));
	if (h > cap) h = cap;
	if (h > DENT_LIM_H) h = DENT_LIM_H;
	if (!(h > 0.0)) h = 0.0;
	out.h = h;
	double qmax = 0.0;
	for (size_t v = 0; v < m.nv; v++) qmax = std::max (qmax, Weight (out, m.rest[v]));
	return (h * qmax < DENT_MIN_DEPTH) ? DENT_SMALL : DENT_OK;
}

int DentMath::FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r)
{
	return FindCoalesce (rec, r, nullptr);
}

int DentMath::FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r, const std::vector<uint8_t> *key)
{
	const double cmin = std::cos (DENT_COALESCE_ANGLE * Pi / 180.0);
	auto keyed = [key] (const std::vector<uint16_t> &g) {
		std::vector<uint16_t> o;
		for (uint16_t x : g) if (x < key->size () && (*key)[x]) o.push_back (x);
		return o;
	};
	std::vector<uint16_t> rk;
	if (key) rk = keyed (r.grp);
	bool exact = !key || r.grp.empty () || rk.empty (); // all groups, or only unkeyed groups: the whole list compares
	int best = -1;
	double bd = 0.0;
	for (size_t i = 0; i < rec.size (); i++) {
		const DentRecord &o = rec[i];
		if (o.slot != r.slot || o.key != r.key || o.ngrp != r.ngrp || o.nvtx != r.nvtx) continue;
		if (exact ? o.grp != r.grp : (o.grp.empty () || keyed (o.grp) != rk)) continue;
		double d = Len (r.p.c - o.p.c);
		if (!(d < DENT_COALESCE_R * o.p.R) || !(Dot (r.p.n, o.p.n) > cmin)) continue;
		if (best < 0 || d < bd) best = (int)i, bd = d; // nearest centre, earliest on ties
	}
	return best;
}

double DentMath::CoalesceDepth (const DentParams &old, double V, const DentMeshView &m, double Dmax)
{
	return CoalesceDepth (old, V, m, nullptr, Dmax);
}

double DentMath::CoalesceDepth (const DentParams &old, double V, const DentMeshView &m, const DentMeshView *cap, double Dmax)
{
	if (!(V > 0.0)) return 0.0;
	double S = VolumeFactor (old, m);
	if (!(S > 0.0)) return 0.0;
	double dh = std::min (V / S, DepthCap (old, m, cap, Dmax));
	dh = std::min (dh, DENT_LIM_H - old.h); // the grown record stays within the text limits
	return dh > 0.0 ? dh : 0.0;
}

void DentMath::Quantise (DentParams &p)
{
	p.c = Vector (Q9 (p.c.x), Q9 (p.c.y), Q9 (p.c.z));
	p.n = Vector (Q9 (p.n.x), Q9 (p.n.y), Q9 (p.n.z));
	p.R = Q9 (p.R), p.h = Q9 (p.h), p.T = Q9 (p.T);
}

// geometry on plain arrays (4.8, 4.9, 13.3, 13.4)

DentMeshView DentViewData::View () const
{
	return { rest.data (), cur.size () == rest.size () ? cur.data () : rest.data (), rest.size (), tri.data (), tri.size () / 3 };
}

void DentMath::MakeView (const DentObject &o, DentViewData &d)
{
	d.rest.clear (), d.cur.clear (), d.tri.clear ();
	for (size_t g = 0; g < o.rest.size (); g++) {
		const std::vector<DentVtx> &R = o.rest[g];
		const std::vector<DentVtx> &C = (g < o.cur.size () && o.cur[g].size () == R.size ()) ? o.cur[g] : R;
		uint32_t base = (uint32_t)d.rest.size ();
		for (size_t i = 0; i < R.size (); i++) d.rest.push_back (Pos (R[i])), d.cur.push_back (Pos (C[i]));
		if (g >= o.idx.size ()) continue;
		const std::vector<uint16_t> &I = o.idx[g];
		for (size_t j = 0; j + 2 < I.size (); j += 3) {
			if (I[j] >= R.size () || I[j+1] >= R.size () || I[j+2] >= R.size ()) continue;
			d.tri.push_back (base + I[j]), d.tri.push_back (base + I[j+1]), d.tri.push_back (base + I[j+2]);
		}
	}
}

size_t DentMath::Apply (const DentParams &p, const std::vector<std::vector<DentVtx>> &rest, std::vector<std::vector<DentVtx>> &cur,
	const uint16_t *grp, size_t ngrp, std::vector<std::vector<uint8_t>> *dirty)
{
	size_t ng = std::min (rest.size (), cur.size ());
	std::vector<uint8_t> sel;
	if (ngrp) { // listed groups once each, whatever the list order
		sel.assign (ng, 0);
		for (size_t k = 0; k < ngrp; k++) if (grp[k] < ng) sel[grp[k]] = 1;
	}
	if (dirty && dirty->size () < ng) dirty->resize (ng);
	size_t moved = 0;
	for (size_t g = 0; g < ng; g++) {
		if (ngrp && !sel[g]) continue;
		size_t n = std::min (rest[g].size (), cur[g].size ());
		if (dirty && (*dirty)[g].size () < cur[g].size ()) (*dirty)[g].resize (cur[g].size (), 0);
		for (size_t i = 0; i < n; i++)
			if (AddField (p, rest[g][i], cur[g][i])) {
				moved++;
				if (dirty) (*dirty)[g][i] = 1;
			}
	}
	return moved;
}

uint32_t DentMath::WeldMap (const std::vector<std::vector<DentVtx>> &v, double tol, std::vector<std::vector<uint32_t>> &weld)
{
	return WeldMap (v, tol, weld, nullptr);
}

uint32_t DentMath::WeldMap (const std::vector<std::vector<DentVtx>> &v, double tol, std::vector<std::vector<uint32_t>> &weld, const std::vector<uint32_t> *cls)
{
	if (!(tol > 0.0)) tol = DENT_WELD;
	else if (tol < 1e-9) tol = 1e-9; // grid cells of positions within 1e9 m stay inside int64
	std::unordered_map<Cell, std::vector<uint32_t>, CellHash> cells;
	std::vector<Vector> rep;
	std::vector<uint32_t> repCls;
	weld.assign (v.size (), std::vector<uint32_t> ());
	uint32_t n = 0;
	for (size_t g = 0; g < v.size (); g++) {
		weld[g].resize (v[g].size ());
		uint32_t k = cls && g < cls->size () ? (*cls)[g] : 0;
		for (size_t i = 0; i < v[g].size (); i++) {
			Vector p = Pos (v[g][i]);
			if (!Within (p, 1e9)) { // own id, never welded (also non-finite)
				weld[g][i] = n++; rep.push_back (p); repCls.push_back (k);
				continue;
			}
			Cell c = { (int64_t)std::floor (p.x / tol), (int64_t)std::floor (p.y / tol), (int64_t)std::floor (p.z / tol) };
			uint32_t best = UINT32_MAX;
			for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) for (int dz = -1; dz <= 1; dz++) {
				auto f = cells.find ({ c.x + dx, c.y + dy, c.z + dz });
				if (f == cells.end ()) continue;
				for (uint32_t id : f->second) if (id < best && repCls[id] == k && Len2 (rep[id] - p) <= tol * tol) best = id; // first representative of this class within tol
			}
			if (best == UINT32_MAX) {
				best = n++;
				rep.push_back (p);
				repCls.push_back (k);
				cells[c].push_back (best);
			}
			weld[g][i] = best;
		}
	}
	return n;
}

void DentMath::FaceNormalSums (const std::vector<std::vector<DentVtx>> &v, const std::vector<std::vector<uint16_t>> &idx,
	const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, std::vector<Vector> &sum)
{
	sum.assign (nweld, Vector ());
	for (size_t g = 0; g < v.size () && g < idx.size () && g < weld.size (); g++) {
		const std::vector<uint16_t> &I = idx[g];
		for (size_t j = 0; j + 2 < I.size (); j += 3) {
			uint32_t a = I[j], b = I[j+1], c = I[j+2];
			if (a >= v[g].size () || b >= v[g].size () || c >= v[g].size () || a >= weld[g].size () || b >= weld[g].size () || c >= weld[g].size ()) continue;
			uint32_t wa = weld[g][a], wb = weld[g][b], wc = weld[g][c];
			if (wa == wb || wb == wc || wc == wa || wa >= nweld || wb >= nweld || wc >= nweld) continue; // collapsed by the weld
			Vector A = Area (Pos (v[g][a]), Pos (v[g][b]), Pos (v[g][c]));
			if (!Finite (A)) continue;
			sum[wa] += A, sum[wb] += A, sum[wc] += A;
		}
	}
}

void DentMath::Normals (const std::vector<std::vector<DentVtx>> &rest, const std::vector<Vector> &restSum, const std::vector<std::vector<uint16_t>> &idx,
	const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, const std::vector<uint8_t> &touched, std::vector<std::vector<DentVtx>> &cur)
{
	size_t ng = std::min (std::min (rest.size (), cur.size ()), std::min (idx.size (), weld.size ()));
	auto valid = [&] (size_t g, uint32_t a) { return a < rest[g].size () && a < cur[g].size () && a < weld[g].size () && weld[g][a] < nweld; };
	auto tch = [&] (uint32_t w) { return w < touched.size () && touched[w]; };
	// update set: every weld id on a triangle with a touched (moved) id
	std::vector<uint8_t> upd (nweld, 0);
	for (uint32_t w = 0; w < nweld; w++) if (tch (w)) upd[w] = 1;
	for (size_t g = 0; g < ng; g++)
		for (size_t j = 0; j + 2 < idx[g].size (); j += 3) {
			uint32_t a = idx[g][j], b = idx[g][j+1], c = idx[g][j+2];
			if (!valid (g, a) || !valid (g, b) || !valid (g, c)) continue;
			uint32_t wa = weld[g][a], wb = weld[g][b], wc = weld[g][c];
			if (tch (wa) || tch (wb) || tch (wc)) upd[wa] = upd[wb] = upd[wc] = 1;
		}
	// current face-normal sums of the update set, same terms and order as FaceNormalSums
	std::vector<Vector> sum (nweld);
	for (size_t g = 0; g < ng; g++)
		for (size_t j = 0; j + 2 < idx[g].size (); j += 3) {
			uint32_t a = idx[g][j], b = idx[g][j+1], c = idx[g][j+2];
			if (!valid (g, a) || !valid (g, b) || !valid (g, c)) continue;
			uint32_t wa = weld[g][a], wb = weld[g][b], wc = weld[g][c];
			if (wa == wb || wb == wc || wc == wa || !(upd[wa] || upd[wb] || upd[wc])) continue;
			Vector A = Area (Pos (cur[g][a]), Pos (cur[g][b]), Pos (cur[g][c]));
			if (!Finite (A)) continue;
			sum[wa] += A, sum[wb] += A, sum[wc] += A;
		}
	for (size_t g = 0; g < ng; g++)
		for (size_t i = 0; i < rest[g].size () && i < cur[g].size () && i < weld[g].size (); i++) {
			uint32_t w = weld[g][i];
			if (w >= nweld || !upd[w] || w >= restSum.size ()) continue;
			Vector nr = Nml (rest[g][i]);
			const Vector &u = restSum[w], &s = sum[w];
			if (Len2 (nr) < 0.01 || !(Len2 (u) > 0.0) || !Finite (u)) continue; // degenerate rest normal or sum: stays
			DentVtx &cv = cur[g][i];
			if (SameBits (u.x, s.x) && SameBits (u.y, s.y) && SameBits (u.z, s.z)) { // faces as at rest
				cv.nx = rest[g][i].nx, cv.ny = rest[g][i].ny, cv.nz = rest[g][i].nz;
				continue;
			}
			if (!(Len2 (s) > 0.0)) continue;
			Vector x = RotateMin (nr, u / Len (u), s / Len (s));
			double l = Len (x);
			if (!(l > 0.0) || !std::isfinite (l)) continue;
			x /= l;
			cv.nx = (float)x.x, cv.ny = (float)x.y, cv.nz = (float)x.z;
		}
}

int DentMath::Refine (DentObject &o, const Vector &c, double R, const DentParams *prior, size_t nprior, const uint32_t *vtxRoom)
{
	return Refine (o, c, R, prior, nprior, vtxRoom, nullptr, nullptr);
}

// new vertices: rest attributes lerped in double, cur = rest + prior fields; normals: caller
int DentMath::Refine (DentObject &o, const Vector &c0, double R0, const DentParams *prior, size_t nprior, const uint32_t *vtxRoom,
	const uint32_t *roomOf, std::vector<uint8_t> *split)
{
	if (split) split->assign (o.nweld, 0);
	const Vector c (Q9 (c0.x), Q9 (c0.y), Q9 (c0.z)); // the values the record stores, so live and replay refine alike (13.4)
	const double R = Q9 (R0);
	if (!(R > 0.0) || !Finite (c)) return 0;
	Refiner f;
	f.o = o;
	DentObject &w = f.o;
	size_t ng = w.rest.size ();
	w.cur.resize (ng), w.idx.resize (ng);
	for (size_t g = 0; g < ng; g++) if (w.cur[g].size () != w.rest[g].size ()) w.cur[g] = w.rest[g];
	bool wok = w.weld.size () == ng;
	for (size_t g = 0; wok && g < ng; g++) {
		wok = w.weld[g].size () == w.rest[g].size ();
		for (size_t i = 0; wok && i < w.weld[g].size (); i++) wok = w.weld[g][i] < w.nweld;
	}
	if (!wok) w.nweld = WeldMap (w.rest, DENT_WELD, w.weld);
	f.P.assign (w.nweld, Vector ());
	f.split.assign (w.nweld, 0);
	std::vector<uint8_t> seen (w.nweld, 0);
	for (size_t g = 0; g < ng; g++)
		for (size_t i = 0; i < w.rest[g].size (); i++)
			if (!seen[w.weld[g][i]]) seen[w.weld[g][i]] = 1, f.P[w.weld[g][i]] = Pos (w.rest[g][i]);
	f.prior = prior, f.nprior = prior ? nprior : 0;
	f.mid.resize (ng);
	// budgets: WORD index room per group, vertex room per slot (a merged group's groups share a slot)
	f.glim.resize (ng), f.gadd.assign (ng, 0), f.slot.resize (ng);
	std::vector<uint32_t> ids (ng);
	for (size_t g = 0; g < ng; g++) {
		size_t nv = w.rest[g].size ();
		f.glim[g] = nv < DENT_MERGED_VTX ? DENT_MERGED_VTX - (uint32_t)nv : 0;
		ids[g] = roomOf ? roomOf[g] : (uint32_t)g;
	}
	std::vector<uint32_t> uniq (ids);
	std::sort (uniq.begin (), uniq.end ());
	uniq.erase (std::unique (uniq.begin (), uniq.end ()), uniq.end ());
	f.room.resize (uniq.size ()), f.sadd.assign (uniq.size (), 0);
	for (size_t s = 0; s < uniq.size (); s++) f.room[s] = vtxRoom ? vtxRoom[uniq[s]] : UINT32_MAX;
	for (size_t g = 0; g < ng; g++) f.slot[g] = (uint32_t)(std::lower_bound (uniq.begin (), uniq.end (), ids[g]) - uniq.begin ());
	auto inRange = [&] (const Vector &p) { return Within (p, 1e9); }; // edge lengths in nm stay inside int64 (Longest)
	for (uint32_t g = 0; g < ng; g++) {
		const std::vector<uint16_t> &I = w.idx[g];
		for (size_t j = 0; j + 2 < I.size (); j += 3) {
			RTri r = { g, { I[j], I[j+1], I[j+2] } };
			bool bad = r.v[0] >= w.rest[g].size () || r.v[1] >= w.rest[g].size () || r.v[2] >= w.rest[g].size ();
			for (int k = 0; !bad && k < 3; k++) bad = !inRange (Pos (w.rest[g][r.v[k]])) || !inRange (f.P[w.weld[g][r.v[k]]]);
			bool deg = false;
			if (!bad) {
				uint32_t a = w.weld[g][r.v[0]], b = w.weld[g][r.v[1]], cc = w.weld[g][r.v[2]];
				deg = a == b || b == cc || cc == a;
			}
			f.T.push_back (r), f.live.push_back (1), f.fixd.push_back (bad || deg ? 1 : 0);
			f.ntot++;
			if (!bad) f.Link ((uint32_t)f.T.size () - 1), f.nlive++; // degenerate triangles are split with their edges (no T-junction)
		}
	}
	const size_t nlive0 = f.nlive;
	const double e2 = (DENT_REFINE_EDGE * R) * (DENT_REFINE_EDGE * R);
	for (bool changed = true; changed && !f.fail; ) {
		changed = false;
		size_t n = f.T.size (); // triangles made in this pass wait for the next one
		for (size_t t = 0; t < n && !f.fail; t++) {
			if (!f.live[t] || f.fixd[t] || !f.InBall ((uint32_t)t, c, R)) continue;
			double l2;
			uint64_t k = f.Longest ((uint32_t)t, l2);
			if (l2 <= e2) continue;
			f.Lepp (k);
			changed = true;
			if (f.nlive - nlive0 > DENT_REFINE_NEW || f.ntot > DENT_REFINE_MAX) f.fail = true;
		}
	}
	if (f.fail) return -1;
	size_t added = f.nlive - nlive0;
	if (!added) return 0;
	for (uint32_t g = 0; g < ng; g++) w.idx[g].clear ();
	for (size_t t = 0; t < f.T.size (); t++) {
		if (!f.live[t]) continue;
		const RTri &r = f.T[t];
		for (int k = 0; k < 3; k++) w.idx[r.g].push_back ((uint16_t)r.v[k]);
	}
	if (split) {
		f.split.resize (w.nweld, 0);
		*split = std::move (f.split);
	}
	o = std::move (w);
	return (int)added;
}

// energy and destroyed state (2.1, 5.1)

double DentMath::PlasticFactor (double vn, double v_el)
{
	if (!(vn > 0.0)) return 0.0;
	double r = v_el / vn;
	return std::max (0.0, 1.0 - r * r);
}

bool DentMath::SplitEnergy (double dKE, double Wf, double vn, bool first, const DentMaterial &a, const DentMaterial &b,
	double E[2], double eabs[2])
{
	E[0] = E[1] = eabs[0] = eabs[1] = 0.0;
	bool ok = first || (dKE == 0.0 && Wf == 0.0); // D3 fills FIRST-only energy (Y3'); D4 adds no flag test of its own
	if (!(vn > DENT_VN_GATE)) return ok;          // gate (Y3', R7: strict)
	double En = std::max (0.0, dKE - Wf);
	double ss = a.sigma_c + b.sigma_c;
	double w[2];
	w[0] = ss > 0.0 ? b.sigma_c / ss : 0.5;
	w[1] = 1.0 - w[0];
	const DentMaterial *m[2] = { &a, &b };
	for (int i = 0; i < 2; i++) {
		double fpl = PlasticFactor (vn, m[i]->v_el);
		if (!(fpl > 0.0)) continue; // vn <= v_el: nothing for this side
		E[i] = En * w[i] * fpl;
		eabs[i] = E[i] + DENT_WF_SHARE * std::max (0.0, Wf);
	}
	return ok;
}

bool DentMath::Destroyed (double eabs, double mEmpty, double mTotal, double epsD)
{
	double m = mEmpty > 0.0 ? mEmpty : mTotal;
	return m > 0.0 && epsD > 0.0 && eabs / m >= epsD;
}

double DentMath::BuildingMass (int cls, const Vector &size)
{
	double rho, f;
	switch (cls) {
	case DENTB_BLOCK:   rho = 300.0, f = 1.0;   break;
	case DENTB_HANGAR:  rho = 40.0,  f = 0.785; break;
	case DENTB_HANGAR2: rho = 40.0,  f = 0.9;   break;
	case DENTB_HANGAR3: rho = 40.0,  f = 0.9;   break;
	case DENTB_TANK:    rho = 150.0, f = 0.785; break;
	case DENTB_MESH:    rho = 100.0, f = 0.5;   break;
	default: return 0.0;
	}
	return rho * std::fabs (size.x) * std::fabs (size.y) * std::fabs (size.z) * f;
}

const DentMaterial *DentMath::FindMaterial (const char *id)
{
	if (!id) return nullptr;
	size_t n = std::strlen (id);
	for (const DentMaterial &m : g_mat) if (IEq (id, n, m.id)) return &m;
	return nullptr;
}

const DentMaterial &DentMath::DefaultMaterial (int cls)
{
	switch (cls) {
	case DENTB_BLOCK: return g_mat[M_RC];
	case DENTB_HANGAR: case DENTB_HANGAR2: case DENTB_HANGAR3: return g_mat[M_CLAD];
	case DENTB_TANK: case DENTB_MESH: return g_mat[M_STEEL];
	default: return cls < 0 ? g_mat[M_AL] : g_mat[M_STEEL];
	}
}

// keys and text (7.3, 9, 10)

uint32_t DentMath::Fnv1a (const char *s, size_t n)
{
	uint32_t h = 2166136261u;
	for (size_t i = 0; i < n; i++) h = (h ^ (unsigned char)s[i]) * 16777619u;
	return h;
}

uint32_t DentMath::MeshKey (const char *name)
{
	if (!name || !*name) return Fnv1a ("#", 1);
	std::string s (name);
	for (char &c : s) c = (c == '\\') ? '/' : Lower (c);
	return Fnv1a (s.data (), s.size ());
}

void DentMath::FormatVessel (const DentVesselText &v, const char *indent, std::vector<std::string> &lines)
{
	std::string ind (indent ? indent : "");
	if (ind.size () > 20) ind.resize (20); // keeps the fixed part of every line inside 200
	const size_t lim = (size_t)DENT_LINE_MAX;
	for (const std::string &l : v.verbatim) { // unknown-version sections first, each with its own XDMG line
		std::string s = Clean (l, true);
		if (ind.size () + s.size () <= lim) lines.push_back (ind + s);
		else if (s.size () <= lim) lines.push_back (s);
	}
	if (v.rec.empty () && v.eabs == 0.0 && v.flags == 0) return;
	std::string s = ind + "XDMG ";
	PutInt (s, DENT_VERSION);
	s += ' ';
	PutNum (s, Clamp (v.eabs, 0.0, DENT_LIM_E));
	s += ' ';
	PutInt (s, v.flags);
	lines.push_back (s);
	// records the parser accepts; mesh keys k in order of first use
	std::vector<size_t> ok, first, kof;
	for (size_t i = 0; i < v.rec.size (); i++) if (ParamsOk (v.rec[i].p)) ok.push_back (i);
	for (size_t i : ok) {
		const DentRecord &r = v.rec[i];
		size_t k = 0;
		while (k < first.size ()) {
			const DentRecord &o = v.rec[first[k]];
			if (o.slot == r.slot && o.key == r.key && o.ngrp == r.ngrp && o.nvtx == r.nvtx) break;
			k++;
		}
		if (k == first.size ()) first.push_back (i);
		kof.push_back (k);
	}
	for (size_t k = 0; k < first.size (); k++) {
		const DentRecord &r = v.rec[first[k]];
		s = ind + "XDMGM ";
		PutInt (s, k); s += ' ';
		PutInt (s, r.slot); s += ' ';
		PutHex8 (s, r.key); s += ' ';
		PutInt (s, (unsigned)r.ngrp); s += ' ';
		PutInt (s, r.nvtx);
		if (r.slot < v.slotName.size ()) {
			std::string nm = Trimmed (Clean (v.slotName[r.slot], false).c_str ());
			if (!nm.empty () && s.size () + 1 < lim) {
				CutUtf8 (nm, lim - s.size () - 1);
				if (!nm.empty ()) s += ' ', s += nm;
			}
		}
		lines.push_back (s);
	}
	for (size_t j = 0; j < ok.size (); j++) {
		const DentRecord &r = v.rec[ok[j]];
		s = ind + "XDMGD ";
		PutInt (s, kof[j]);
		PutParams (s, r.p);
		GroupLines (s, r.grp, lim, lines);
	}
}

void DentMath::FormatBases (const std::vector<DentBaseText> &b, std::vector<std::string> &lines)
{
	if (b.empty ()) return;
	const size_t lim = (size_t)DENT_LINE_MAX;
	lines.push_back ("BEGIN_XDMG_BASES");
	for (const DentBaseText &bt : b) {
		std::string pl = Trimmed (Clean (bt.planet, false).c_str ()), nm = Clean (bt.name, false);
		if (pl.empty ()) continue; // no planet: the parser could not place it
		std::string s = "BASE " + pl + ":" + nm;
		if (nm.empty () || s.size () > lim) {
			s = "BASEH ";
			PutHex8 (s, bt.nameHash ? bt.nameHash : Fnv1a (bt.name.data (), bt.name.size ()));
			s += ' ';
			s += pl; // rest of the line: planet names may hold spaces
			if (s.size () > lim) continue; // planet name beyond any line
		}
		lines.push_back (s);
		for (const DentBaseObjText &o : bt.obj) {
			std::string ty = Clean (o.type, false);
			for (char &c : ty) if (c == ' ' || c == '\t') c = '_';
			if (ty.empty ()) ty = "?";
			if (ty.size () > 64) ty.resize (64);
			s = "OBJ ";
			PutInt (s, o.index); s += ' ';
			s += ty; s += ' ';
			PutNum (s, std::round (Clamp (o.x, -DENT_LIM_POS, DENT_LIM_POS) * 10.0) / 10.0); s += ' ';
			PutNum (s, std::round (Clamp (o.z, -DENT_LIM_POS, DENT_LIM_POS) * 10.0) / 10.0); s += ' ';
			PutNum (s, Clamp (o.eabs, 0.0, DENT_LIM_E)); s += ' ';
			PutInt (s, o.flags);
			lines.push_back (s);
		}
		for (const DentRecord &r : bt.rec) {
			if (!ParamsOk (r.p)) continue;
			s = "ODENT ";
			PutInt (s, r.slot);
			PutParams (s, r.p);
			if (r.flags) s += ' ', PutInt (s, r.flags); // refinement outcome (4.9 item 5); absent = 0
			lines.push_back (s);
		}
		lines.push_back ("END_BASE");
	}
	lines.push_back ("END_XDMG_BASES");
}

void DentMath::FormatDentEvent (const DentRecord &r, std::vector<std::string> &payload)
{
	if (!ParamsOk (r.p)) return;
	std::string s;
	PutInt (s, r.slot);
	PutParams (s, r.p);
	GroupLines (s, r.grp, (size_t)DENT_EVENT_MAX, payload);
}

bool DentMath::ParseDentEvent (const char *payload, DentRecord &r)
{
	bool more;
	return ParseDentEvent (payload, r, more);
}

bool DentMath::ParseDentEvent (const char *payload, DentRecord &r, bool &more)
{
	std::vector<Tok> t;
	Split (payload, t);
	DentRecord o {};
	bool m = false;
	if (t.size () < 11 || !ParseInt (t[0], o.slot) || !ParseParams (t, 1, o.p) || !ParseGroups (t[10], o.grp, m)) return false;
	o.key = 0, o.ngrp = 0, o.nvtx = 0;
	r = o;
	more = m;
	return true;
}

std::string DentMath::FormatBaseDentEvent (int planet, int base, int obj, const DentParams &p)
{
	return FormatBaseDentEvent (planet, base, obj, p, 0);
}

std::string DentMath::FormatBaseDentEvent (int planet, int base, int obj, const DentParams &p, uint32_t flags)
{
	std::string s;
	PutInt (s, planet); s += ':';
	PutInt (s, base); s += ':';
	PutInt (s, obj);
	PutParams (s, p);
	if (flags) s += ' ', PutInt (s, flags);
	return s;
}

bool DentMath::ParseBaseDentEvent (const char *payload, int &planet, int &base, int &obj, DentParams &p)
{
	uint32_t flags;
	return ParseBaseDentEvent (payload, planet, base, obj, p, flags);
}

bool DentMath::ParseBaseDentEvent (const char *payload, int &planet, int &base, int &obj, DentParams &p, uint32_t &flags)
{
	std::vector<Tok> t;
	Split (payload, t);
	if (t.size () < 10) return false;
	const char *s = t[0].p, *e = t[0].p + t[0].n;
	const char *c1 = std::find (s, e, ':');
	if (c1 == e) return false;
	const char *c2 = std::find (c1 + 1, e, ':');
	if (c2 == e) return false;
	int a, b, c;
	uint32_t f = 0;
	DentParams q {};
	if (!ParseInt (s, c1, a) || !ParseInt (c1 + 1, c2, b) || !ParseInt (c2 + 1, e, c) || a < 0 || b < 0 || c < 0 || !ParseParams (t, 1, q)) return false;
	if (t.size () > 10 && !ParseInt (t[10], f)) return false;
	planet = a, base = b, obj = c, p = q, flags = f;
	return true;
}

std::string DentMath::FormatStateEvent (double eabs, uint32_t flags)
{
	std::string s;
	PutNum (s, Clamp (eabs, 0.0, DENT_LIM_E));
	s += ' ';
	PutInt (s, flags);
	return s;
}

bool DentMath::ParseStateEvent (const char *payload, double &eabs, uint32_t &flags)
{
	std::vector<Tok> t;
	Split (payload, t);
	double e;
	uint32_t f;
	if (t.size () < 2 || !ParseD (t[0], e) || e < 0.0 || e > DENT_LIM_E || !ParseInt (t[1], f)) return false;
	eabs = Q9 (e), flags = f;
	return true;
}

// DentVesselParser (9.2, 9.3)

bool DentVesselParser::Line (const char *line)
{
	std::vector<Tok> t;
	Split (line, t);
	if (t.empty ()) return false;
	int kw = IEq (t[0].p, t[0].n, "XDMG") ? 0 : IEq (t[0].p, t[0].n, "XDMGM") ? 1 : IEq (t[0].p, t[0].n, "XDMGD") ? 2 : -1;
	if (kw < 0) return false;
	std::string raw = Trimmed (line);
	if (kw > 0) { // in the section of the XDMG line before it
		if (m_sec == 0) m_pending.push_back (raw);
		else if (m_sec == 1) V1 (raw);
		else Keep (raw);
		return true;
	}
	Close (); // an XDMG line ends an open group list
	int ver;
	if (t.size () < 2 || !ParseInt (t[1], ver)) { m_skipped++; return true; } // no section change
	bool first = m_sec == 0;
	if (ver != DENT_VERSION) {
		m_sec = raw.size () <= (size_t)DENT_LINE_MAX ? 2 : 3; // a section whose XDMG line cannot be kept is dropped whole
		if (first) for (const std::string &l : m_pending) Keep (l);
		Keep (raw);
	} else {
		m_sec = 1;
		if (first) {
			for (const std::string &l : m_pending) V1 (l);
			Close ();
		}
		double e;
		uint32_t f;
		if (m_head || t.size () < 4 || !ParseD (t[2], e) || e < 0.0 || e > DENT_LIM_E || !ParseInt (t[3], f)) m_skipped++; // a second XDMG 1 line too
		else m_head = true, m_eabs = Q9 (e), m_flags = f; // as a save writes it, so a reload of a reload is exact
	}
	if (first) m_pending.clear ();
	return true;
}

void DentVesselParser::Close ()
{
	if (!m_open) return;
	if (m_meshIdx.count (m_dent.back ().first)) m_nKnown--;
	m_dent.pop_back ();
	m_skipped++;
	m_open = m_over = false;
}

void DentVesselParser::Keep (const std::string &line)
{
	Close ();
	if (m_sec == 2 && line.size () <= (size_t)DENT_LINE_MAX) m_lines.push_back (line);
	else m_long++; // never written back
}

void DentVesselParser::V1 (const std::string &line)
{
	std::vector<Tok> t;
	Split (line.c_str (), t);
	if (!IEq (t[0].p, t[0].n, "XDMGD")) { // XDMGM
		Close ();
		uint32_t k, ngrp;
		DentRecord r {};
		if (t.size () < 6 || !ParseInt (t[1], k) || !ParseInt (t[2], r.slot) || t[3].n > 8 || !ParseInt (t[3], r.key, 16)
			|| !ParseInt (t[4], ngrp) || ngrp > 65535 || !ParseInt (t[5], r.nvtx)) { m_skipped++; return; }
		r.ngrp = (uint16_t)ngrp;
		if (!m_meshIdx.emplace (k, m_mesh.size ()).second) { m_skipped++; return; } // a key is defined once
		m_mesh.push_back ({ k, r });
		if (t.size () > 6) m_names.push_back ({ r.slot, RestOf (t[5]) });
		return;
	}
	uint32_t k;
	DentRecord r {};
	bool more = false;
	if (t.size () < 12 || !ParseInt (t[1], k) || !ParseParams (t, 2, r.p) || !ParseGroups (t[11], r.grp, more)) { Close (); m_skipped++; return; }
	if (m_open) { // the previous line's list ended in ',': this line continues it, or the record dangles
		DentRecord &p = m_dent.back ().second;
		if (m_dent.back ().first == k && SameParams (p.p, r.p) && !r.grp.empty ()) {
			if (p.grp.size () + r.grp.size () > DENT_MAX_GRPLIST) m_over = m_capped = true; // no more groups stored; the record is dropped at its end
			else if (!m_over) p.grp.insert (p.grp.end (), r.grp.begin (), r.grp.end ());
			m_open = more;
			if (!m_open && m_over) { if (m_meshIdx.count (k)) m_nKnown--; m_dent.pop_back (); m_skipped++; m_over = false; }
			return;
		}
		Close ();
	}
	bool known = m_meshIdx.count (k) != 0;                // orphans do not count toward the cap; Finish skips them
	if ((known && m_nKnown >= DENT_MAX_VESSEL) || r.grp.size () > DENT_MAX_GRPLIST) { // past the live cap (R7) or the group limit: not stored
		m_capped = true;
		if (!more) { m_skipped++; return; }
		r.grp.clear (), m_over = true; // its continuation lines are read and dropped with it
	}
	m_dent.push_back ({ k, r });
	if (known) m_nKnown++;
	m_open = more;
}

void DentVesselParser::Finish (DentVesselText &out)
{
	out = DentVesselText ();
	if (m_sec == 0) { // no XDMG line: version 1
		m_sec = 1;
		for (const std::string &l : m_pending) V1 (l);
		m_pending.clear ();
	}
	Close ();
	int skipped = m_skipped + m_long;
	out.verbatim = m_lines;
	out.eabs = m_eabs, out.flags = m_flags;
	for (const auto &d : m_dent) {
		auto mi = m_meshIdx.find (d.first);
		if (mi == m_meshIdx.end ()) { skipped++; continue; } // unknown key
		const DentRecord *m = &m_mesh[mi->second].second;
		if (out.rec.size () >= DENT_MAX_VESSEL) { skipped++, m_capped = true; continue; } // the live cap (R7); the rest counts as skipped
		DentRecord r = d.second;
		r.slot = m->slot, r.key = m->key, r.ngrp = m->ngrp, r.nvtx = m->nvtx;
		out.rec.push_back (r);
	}
	for (const auto &n : m_names)
		if (n.first < 4096) { // readers only; no huge tables from a bad slot
			if (out.slotName.size () <= n.first) out.slotName.resize (n.first + 1);
			out.slotName[n.first] = n.second;
		}
	m_result = skipped;
}

int DentVesselParser::Skipped () const
{
	return m_result >= 0 ? m_result : m_skipped + m_long;
}

bool DentVesselParser::Capped () const { return m_capped; }

// DentBasesParser (9.4)

bool DentBasesParser::Line (const char *line)
{
	if (m_done) return false;
	std::vector<Tok> t;
	Split (line, t);
	if (t.empty ()) return true;
	auto kw = [&] (const char *k) { return IEq (t[0].p, t[0].n, k); };
	if (kw ("END_XDMG_BASES")) { m_inBase = false; m_done = true; return false; }
	if (kw ("BASE")) {
		m_inBase = false;
		std::string s = RestOf (t[0]);
		size_t c = s.find (':');
		if (c == std::string::npos || c == 0 || c + 1 >= s.size ()) { m_skipped++; return true; }
		DentBaseText b {};
		b.planet = s.substr (0, c), b.name = s.substr (c + 1);
		m_base.push_back (b);
		m_nrec.clear ();
		m_inBase = true;
	} else if (kw ("BASEH")) { // BASEH <hash8> <planet, rest of the line>
		m_inBase = false;
		DentBaseText b {};
		if (t.size () < 3 || t[1].n > 8 || !ParseInt (t[1], b.nameHash, 16)) { m_skipped++; return true; }
		b.planet = RestOf (t[1]);
		m_base.push_back (b);
		m_nrec.clear ();
		m_inBase = true;
	} else if (kw ("OBJ")) {
		DentBaseObjText o {};
		if (!m_inBase || t.size () < 7 || !ParseInt (t[1], o.index) || !ParseD (t[3], o.x) || !ParseD (t[4], o.z) || !ParseD (t[5], o.eabs)
			|| std::fabs (o.x) > DENT_LIM_POS || std::fabs (o.z) > DENT_LIM_POS || o.eabs < 0.0 || o.eabs > DENT_LIM_E || !ParseInt (t[6], o.flags)) { m_skipped++; return true; }
		o.type.assign (t[2].p, t[2].n);
		o.eabs = Q9 (o.eabs);
		m_base.back ().obj.push_back (o);
	} else if (kw ("ODENT")) { // ODENT <index> <c> <n> <R> <h> <T> [<flags>]
		DentRecord r {};
		if (!m_inBase || t.size () < 11 || !ParseInt (t[1], r.slot) || !ParseParams (t, 2, r.p) || (t.size () > 11 && !ParseInt (t[11], r.flags))) { m_skipped++; return true; }
		if (++m_nrec[r.slot] > DENT_MAX_OBJECT) { m_skipped++; return true; } // records per object (R7)
		r.key = 0, r.ngrp = 0, r.nvtx = 0;
		m_base.back ().rec.push_back (r);
	} else if (kw ("END_BASE")) {
		if (!m_inBase) m_skipped++;
		m_inBase = false;
	} else {
		m_skipped++;
	}
	return true;
}

void DentBasesParser::Finish (std::vector<DentBaseText> &out)
{
	out = m_base;
}

int DentBasesParser::Skipped () const
{
	return m_skipped;
}
