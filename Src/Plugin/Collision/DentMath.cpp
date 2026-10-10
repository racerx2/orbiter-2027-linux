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
bool AddField (const DentParams &p, const DentVtx &r, DentVtx &c, const DentVCut *vc = nullptr)
{
	Vector rp = Pos (r);
	Vector d;
	if (p.mode == DENTM_VCUT) { // blast: vc's set of this record's kind
		if (!vc || !vc->s) return false;
		Vector cp = Pos (c);
		d = DentMath::VCutMap (*vc, (p.bits & DENTC_KEEP) != 0, rp, cp, true) - cp;
		if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) return false;
	} else if (p.mode == DENTM_CUT) {
		Vector cp = Pos (c);
		d = DentMath::CutMap (p, cp, true) - cp;
		if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) return false;
	} else if (DentMath::Legacy (p)) {
		double q = DentMath::Weight (p, rp);
		if (q == 0.0) return false;
		d = p.n * (-(p.h * q)); // the expression of DentMath::Displace
	} else {
		d = DentMath::Displace (p, rp);
		if (d.x == 0.0 && d.y == 0.0 && d.z == 0.0) return false;
	}
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

// blast: head + x y z triples in lines of <= lim chars; all but the last end in ','
void SiteLines (const std::string &head, const std::vector<Vector> &v, size_t lim, std::vector<std::string> &out)
{
	std::string l = head;
	bool any = false;
	for (const Vector &x : v) {
		std::string t;
		t += ' '; PutNum (t, x.x); t += ' '; PutNum (t, x.y); t += ' '; PutNum (t, x.z);
		if (any && l.size () + t.size () + 1 > lim) { out.push_back (l + ","); l = head; any = false; }
		l += t;
		any = true;
	}
	out.push_back (l);
}

// blast: x y z triples from t[i..]; more: the last token ends in ','
bool ParseSiteToks (const std::vector<Tok> &t, size_t i, std::vector<Vector> &v, bool &more)
{
	more = false;
	if (t.size () <= i || (t.size () - i) % 3) return false;
	for (size_t k = i; k < t.size (); k += 3) {
		double c[3];
		for (size_t j = 0; j < 3; j++) {
			Tok x = t[k + j];
			if (k + j + 1 == t.size () && x.n > 1 && x.p[x.n - 1] == ',') x.n--, more = true;
			if (!ParseD (x, c[j]) || !(std::fabs (c[j]) <= DENT_LIM_POS)) return false;
		}
		v.push_back (Vector (Q9 (c[0]), Q9 (c[1]), Q9 (c[2])));
	}
	return true;
}

// blast: head + b,b,... in lines of <= lim chars; all but the last end in ','
void BondLines (const std::string &head, const std::vector<uint32_t> &b, size_t lim, std::vector<std::string> &out)
{
	std::string l = head;
	bool first = true;
	for (uint32_t x : b) {
		std::string t;
		PutInt (t, x);
		if (!first && l.size () + 1 + t.size () + 1 > lim) { out.push_back (l + ","); l = head; first = true; }
		l += first ? ' ' : ',';
		l += t;
		first = false;
	}
	out.push_back (l);
}

// blast: "3,17,40" with a trailing ',' when continued (more)
bool ParseBondTok (const Tok &t, std::vector<uint32_t> &b, bool &more)
{
	b.clear ();
	more = false;
	const char *s = t.p, *e = t.p + t.n;
	if (t.n > 1 && e[-1] == ',') more = true, e--;
	while (s <= e) {
		const char *c = s;
		while (c < e && *c != ',') c++;
		uint32_t v;
		if (!ParseInt (s, c, v)) return false;
		b.push_back (v);
		s = c + 1;
	}
	return !b.empty ();
}

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

// dmg3 noise frame: u along t, w along n x t, in units of 0.5 R
void NoiseUW (const DentParams &p, const Vector &rest, double &u, double &w)
{
	double lam = 0.5 * p.R;
	Vector d = rest - p.c;
	u = Dot (d, p.t) / lam, w = Dot (d, Cross (p.n, p.t)) / lam;
}

// dmg3 lobed radius: R (1 - 0.06 (1 + T_m(u / rho))), m = 3 + seed % 5
double LobeR (const DentParams &p, const Vector &rest)
{
	double u, w;
	NoiseUW (p, rest, u, w);
	double rho = std::sqrt (u * u + w * w);
	if (!(rho >= 1e-9)) return p.R;
	double x = u / rho, t0 = 1.0, t1 = x;
	uint32_t m = 3 + p.seed % 5;
	for (uint32_t k = 1; k < m; k++) { double t2 = 2.0 * x * t1 - t0; t0 = t1, t1 = t2; }
	return p.R * (1.0 - 0.06 * (1.0 + t1));
}

// dmg3 bowl weight with noise J and lobes (full), plain kernel and slab otherwise
double BowlQ (const DentParams &p, const Vector &rest, bool full)
{
	if (!(p.R > 0.0)) return 0.0;
	double R = (full && (p.bits & DENTB_LOBES)) ? LobeR (p, rest) : p.R;
	double k = DentMath::Kernel (Len2 (rest - p.c) / (R * R));
	if (k == 0.0) return 0.0;
	if (p.T > 0.0) k *= 1.0 - Smooth (DENT_SLAB_LO * p.T, DENT_SLAB_HI * p.T, Dot (p.c - rest, p.n));
	if (full && p.seed) { double u, w; NoiseUW (p, rest, u, w); k *= 1.0 + DENT_NOISE_J * DentMath::Noise (p.seed, u, w); }
	return k;
}

// dmg3 crush: k_flat * max(0, P + delta N - max(s, 0)) / P, s = (c - x).n, lateral r < R
double CrushQ (const DentParams &p, const Vector &rest, bool full)
{
	if (!(p.R > 0.0) || !(p.P > 0.0)) return 0.0;
	Vector d = rest - p.c;
	double s = Dot (p.c - rest, p.n);
	double dn = 0.0;
	if (full && p.seed) {
		double u, w;
		NoiseUW (p, rest, u, w);
		dn = std::min (std::min (0.05 * p.R, 0.1 * p.P), DENT_CRUSH_NOISE) * DentMath::Noise (p.seed, u, w);
	}
	double a = p.P + dn - (s > 0.0 ? s : 0.0);
	if (!(a > 0.0)) return 0.0;
	double r2 = Len2 (d) - s * s;
	double r = r2 > 0.0 ? std::sqrt (r2) : 0.0;
	if (!(r < p.R)) return 0.0;
	double k = 1.0 - Smooth (DENT_CRUSH_CORE * p.R, p.R, r);
	return k * a / p.P;
}

double CrushWeight (const DentParams &p, const Vector &rest) { return CrushQ (p, rest, true); }

// dmg3 hinge: Cayley rotation by tau = P r^2 (3 - 2r) about the line h0 + s e, faded by |v.e| from R to 2R
Vector HingeD (const DentParams &p, const Vector &rest, double sign)
{
	if (!(p.R > 0.0) || p.P == 0.0) return Vector ();
	const Vector &a = p.t, &n = p.n;
	Vector e = Cross (n, a);
	Vector h0 = p.c - a * p.hd - n * p.hz;
	Vector v = rest - h0;
	double va = Dot (v, a), vn = Dot (v, n), ve = Dot (v, e);
	double l = std::max (2.0 * p.hz, 0.25 * p.R);
	double r = va / l;
	if (!(r > 0.0)) return Vector ();
	if (r > 1.0) r = 1.0;
	double tau = sign * p.P * r * r * (3.0 - 2.0 * r);
	double d = 1.0 + tau * tau, co = (1.0 - tau * tau) / d, si = 2.0 * tau / d;
	double w = 1.0 - Smooth (p.R, 2.0 * p.R, std::fabs (ve));
	if (w == 0.0) return Vector ();
	double va2 = va * co + vn * si, vn2 = -va * si + vn * co;
	return (a * (va2 - va) + n * (vn2 - vn)) * w;
}

Vector DisplaceAny (const DentParams &p, const Vector &rest, bool full)
{
	if (DentMath::IsCut (p.mode)) return Vector ();
	if (p.mode == DENTM_HINGE) return HingeD (p, rest, 1.0);
	double q = p.mode == DENTM_CRUSH ? CrushQ (p, rest, full) : BowlQ (p, rest, full);
	return p.n * (-(p.h * q));
}

uint32_t Lowbias32 (uint32_t x)
{
	x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
	return x;
}

double LatticeN (uint32_t seed, int32_t i, int32_t j)
{
	uint32_t h = Lowbias32 (seed ^ Lowbias32 ((uint32_t)i * 0x9e3779b9U ^ Lowbias32 ((uint32_t)j + 0x632be5abU)));
	return (double)(h >> 8) / 8388607.5 - 1.0;
}

void PutHexArg (std::string &s, uint32_t v) { s += ' '; PutHex8 (s, v); }
void PutNumArg (std::string &s, double v) { s += ' '; PutNum (s, v); }
void Put17 (std::string &s, double v)
{
	char b[48];
	auto r = std::to_chars (b, b + sizeof (b), v, std::chars_format::general, 17);
	s.append (b, r.ptr);
}

// dmg3 extension fields from tokens t[i..i+8]: mode P seed8 tx ty tz bits hd hz
bool ParseExtFields (const std::vector<Tok> &t, size_t i, DentParams &e)
{
	if (t.size () < i + 9) return false;
	uint32_t mode, seed, bits;
	double P, tx, ty, tz, hd, hz;
	if (!ParseInt (t[i], mode) || mode > DENTM_VCUT || !ParseD (t[i+1], P) || t[i+2].n > 8 || !ParseInt (t[i+2], seed, 16) || !ParseD (t[i+3], tx) || !ParseD (t[i+4], ty)
		|| !ParseD (t[i+5], tz) || !ParseInt (t[i+6], bits) || !ParseD (t[i+7], hd) || !ParseD (t[i+8], hz)) return false;
	if (!(P >= 0.0 && P <= (mode == DENTM_VCUT ? 65535.0 : DENT_LIM_H)) || std::fabs (hd) > DENT_LIM_R || std::fabs (hz) > DENT_LIM_R || std::fabs (tx) > 2.0 || std::fabs (ty) > 2.0 || std::fabs (tz) > 2.0) return false;
	e.mode = mode, e.P = Q9 (P), e.seed = seed, e.t = Vector (Q9 (tx), Q9 (ty), Q9 (tz)), e.bits = bits, e.hd = Q9 (hd), e.hz = Q9 (hz);
	return true;
}

void PutExtFields (std::string &s, const DentParams &p)
{
	s += ' '; PutInt (s, p.mode);
	PutNumArg (s, p.P);
	PutHexArg (s, p.seed);
	PutNumArg (s, p.t.x); PutNumArg (s, p.t.y); PutNumArg (s, p.t.z);
	s += ' '; PutInt (s, p.bits);
	PutNumArg (s, p.hd); PutNumArg (s, p.hz);
}

std::string NameTok (const std::string &n);
std::string NameUntok (const Tok &t);

// "<kind> <slot> <key8> <ngrp> <nvtx> <simt> <debris>"; room: the line left for the head, its first group and the ',' mark
std::string TornHead (const DentTorn &t, size_t room)
{
	std::string s;
	PutInt (s, (unsigned)t.kind); s += ' ';
	PutInt (s, t.slot);
	PutHexArg (s, t.key);
	s += ' '; PutInt (s, (unsigned)t.ngrp);
	s += ' '; PutInt (s, t.nvtx);
	s += ' '; Put17 (s, t.simt);
	std::string nm = NameTok (Clean (t.debris, false)), g0 = "*"; // dmg3 m4: %-escaped, round-trips blanks
	if (!t.grp.empty ()) g0.clear (), PutInt (g0, (unsigned)t.grp[0]);
	if (t.debris != "-" && s.size () + 1 + nm.size () + 1 + g0.size () + 1 > room) { nm = "#"; PutHex8 (nm, DentMath::Fnv1a (t.debris.data (), t.debris.size ())); } // as FormatDebris
	s += ' '; s += nm;
	return s;
}

// blast: "3,17,40"
void PutList (std::string &s, const std::vector<uint32_t> &v)
{
	for (size_t i = 0; i < v.size (); i++) { if (i) s += ','; PutInt (s, v[i]); }
}

// blast: cell debris tokens after K: C=<cells> P=<pieces> X=<cx>,<cy>,<cz> F=<crushed>; others skipped
void ParseCellTok (const Tok &t, DentTorn &r)
{
	if (t.n < 3 || t.p[1] != '=') return;
	Tok v { t.p + 2, t.n - 2 };
	char k = Lower (t.p[0]);
	bool more = false;
	std::vector<uint32_t> l;
	if (k == 'c' || k == 'p') { if (ParseBondTok (v, l, more) && !more && l.size () <= 65536) (k == 'c' ? r.cells : r.pieces) = l; }
	else if (k == 'x') {
		double c[3];
		const char *s = v.p, *e = v.p + v.n;
		int n = 0;
		while (n < 3 && s <= e) {
			const char *x = s;
			while (x < e && *x != ',') x++;
			if (!ParseD (Tok { s, (size_t)(x - s) }, c[n]) || !(std::fabs (c[n]) <= DENT_LIM_POS)) return;
			n++; s = x + 1;
		}
		if (n == 3 && s > e) r.c = Vector (c[0], c[1], c[2]);
	} else if (k == 'f') { uint32_t f; if (ParseInt (v, f)) r.crushed = f != 0; }
}

bool ParseTornTok (const std::vector<Tok> &t, size_t i, DentTorn &o, bool &more)
{
	if (t.size () < i + 8) return false;
	uint32_t kind, ngrp;
	DentTorn r;
	if (!ParseInt (t[i], kind) || kind > 255 || !ParseInt (t[i+1], r.slot) || t[i+2].n > 8 || !ParseInt (t[i+2], r.key, 16) || !ParseInt (t[i+3], ngrp) || ngrp > 65535
		|| !ParseInt (t[i+4], r.nvtx) || !ParseD (t[i+5], r.simt) || !ParseGroups (t[i+7], r.grp, more)) return false;
	r.kind = (uint8_t)kind, r.ngrp = (uint16_t)ngrp;
	r.debris = NameUntok (t[i+6]);
	double kv[7];
	if (!more && t.size () >= i + 16 && IEq (t[i+8].p, t[i+8].n, "K")) { // dmg3 tear: kinematics
		bool ok = true;
		for (int j = 0; j < 7 && ok; j++) ok = ParseD (t[i+9+j], kv[j]) && std::fabs (kv[j]) <= 1e9;
		if (ok) r.kin = true, r.dv = Vector (kv[0], kv[1], kv[2]), r.dw = Vector (kv[3], kv[4], kv[5]), r.mass = kv[6];
		if (ok) for (size_t j = i + 16; j < t.size (); j++) ParseCellTok (t[j], r);
	}
	o = r;
	return true;
}

// debris name as one token: '%' escapes of blank, '%', ',' and control characters
std::string NameTok (const std::string &n)
{
	if (n.empty ()) return "-";
	static const char *hx = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : n) {
		if (c <= ' ' || c == '%' || c == ',' || c == 127) o += '%', o += hx[c >> 4], o += hx[c & 15];
		else o += (char)c;
	}
	return o;
}

std::string NameUntok (const Tok &t)
{
	std::string o, r (t.p, t.n);
	if (r == "-") return o;
	for (size_t i = 0; i < r.size (); i++) {
		auto hv = [] (char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; };
		if (r[i] == '%' && i + 2 < r.size () && hv (r[i+1]) >= 0 && hv (r[i+2]) >= 0) { o += (char)(hv (r[i+1]) * 16 + hv (r[i+2])); i += 2; }
		else o += r[i];
	}
	return o;
}

std::string PoseHead (const DentDebrisPose &q)
{
	std::string s;
	const double v[7] = { q.p.x, q.p.y, q.p.z, q.q[0], q.q[1], q.q[2], q.q[3] };
	for (double x : v) { s += ' '; PutNum (s, x); }
	return s;
}

bool SameTornHead (const DentTorn &a, const DentTorn &b)
{
	return a.kind == b.kind && a.slot == b.slot && a.key == b.key && a.ngrp == b.ngrp && a.nvtx == b.nvtx && SameBits (a.simt, b.simt) && a.debris == b.debris;
}

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
	if (IsCut (p.mode)) return 0.0;
	if (p.mode == DENTM_CRUSH) return CrushWeight (p, rest);
	if (p.mode == DENTM_HINGE) return p.h > 0.0 ? std::min (1.0, Len (HingeD (p, rest, 1.0)) / p.h) : 0.0;
	if (p.seed) return BowlQ (p, rest, true);
	if (!(p.R > 0.0)) return 0.0;
	double k = Kernel (Len2 (rest - p.c) / (p.R * p.R));
	if (k == 0.0 || !(p.T > 0.0)) return k;
	double s = Dot (p.c - rest, p.n);
	return k * (1.0 - Smooth (DENT_SLAB_LO * p.T, DENT_SLAB_HI * p.T, s));
}

Vector DentMath::Displace (const DentParams &p, const Vector &rest)
{
	if (!Legacy (p)) return DisplaceAny (p, rest, true);
	return p.n * (-(p.h * Weight (p, rest)));
}

Vector DentMath::DisplaceLow (const DentParams &p, const Vector &rest)
{
	if (!Legacy (p)) return DisplaceAny (p, rest, false);
	return p.n * (-(p.h * Weight (p, rest)));
}

Vector DentMath::FieldLow (const void *ctx, const Vector &rest)
{
	const DentRecord *r = (const DentRecord*)ctx;
	return r ? DisplaceLow (r->p, rest) : Vector ();
}

double DentMath::Noise (uint32_t seed, double u, double w)
{
	const double lim = 1073741824.0;
	u = Clamp (u, -lim, lim), w = Clamp (w, -lim, lim);
	double fu = std::floor (u), fw = std::floor (w);
	int32_t i = (int32_t)fu, j = (int32_t)fw;
	double x = u - fu, y = w - fw;
	x = x * x * (3.0 - 2.0 * x), y = y * y * (3.0 - 2.0 * y);
	double a = LatticeN (seed, i, j), b = LatticeN (seed, i + 1, j), c = LatticeN (seed, i, j + 1), d = LatticeN (seed, i + 1, j + 1);
	double ab = a + (b - a) * x, cd = c + (d - c) * x;
	return ab + (cd - ab) * y;
}

Vector DentMath::Field (const void *ctx, const Vector &rest)
{
	const DentRecord *r = (const DentRecord*)ctx;
	return r ? Displace (r->p, rest) : Vector ();
}

// bilinear lattice noise with linear weights (sharp ridges)
double LatticeL (uint32_t seed, double u, double w)
{
	const double lim = 1073741824.0;
	u = Clamp (u, -lim, lim), w = Clamp (w, -lim, lim);
	double fu = std::floor (u), fw = std::floor (w);
	int32_t i = (int32_t)fu, j = (int32_t)fw;
	double x = u - fu, y = w - fw;
	double a = LatticeN (seed, i, j), b = LatticeN (seed, i + 1, j), c = LatticeN (seed, i, j + 1), d = LatticeN (seed, i + 1, j + 1);
	double ab = a + (b - a) * x, cd = c + (d - c) * x;
	return ab + (cd - ab) * y;
}

double DentMath::CutJag (const DentParams &p, const Vector &x, bool full)
{
	if (!full || !(p.hd > 0.0) || p.P == 0.0) return 0.0;
	Vector d = x - p.c, e = Cross (p.n, p.t);
	double u = Dot (d, p.t) / p.hd, w = Dot (d, e) / p.hd;
	return p.P * (0.65 * LatticeL (p.seed, u, w) + 0.35 * LatticeL (p.seed ^ 0x5bd1e995U, 2.0 * u + 17.0, 2.0 * w + 31.0));
}

Vector DentMath::CutMap (const DentParams &p, const Vector &cur, bool full)
{
	if (p.mode != DENTM_CUT) return cur;
	Vector d = cur - p.c;
	double s = Dot (d, p.n);
	Vector dp = d - p.n * s;
	double J = CutJag (p, cur, full), hl = p.hz > 0.0 ? p.hz : 0.0;
	if (p.bits & DENTC_KEEP) {
		if (!(s < J)) return cur;
		double l2 = Len2 (dp);
		if (l2 > p.R * p.R) dp = dp * (p.R / std::sqrt (l2)); // M1: behind vertices land inside the rim
		double sg = (J - s) / (J - s + hl);
		return p.c + dp * (1.0 - DENT_CUT_PINCH * sg) + p.n * (J + hl * sg);
	}
	if (!(s > J) || !(Len2 (dp) < p.R * p.R)) return cur;
	double sg = (s - J) / (s - J + hl);
	return p.c + dp * (1.0 - DENT_CUT_PINCH * sg) + p.n * (J - hl * sg);
}

Vector DentMath::Fold (const DentParams *const *rec, size_t n, const Vector &rest, bool full, bool *cut)
{
	return Fold (rec, n, rest, full, cut, (const DentVCut *)nullptr);
}

Vector DentMath::Fold (const DentParams *const *rec, size_t n, const Vector &rest, bool full, bool *cut, const DentSites *sites)
{
	DentVCut vc;
	if (sites) VCutSet (rec, n, sites, vc);
	return Fold (rec, n, rest, full, cut, sites ? &vc : nullptr);
}

Vector DentMath::Fold (const DentParams *const *rec, size_t n, const Vector &rest, bool full, bool *cut, const DentVCut *vc)
{
	Vector acc;
	bool doneRm = false, doneKeep = false;
	if (cut) *cut = false;
	for (size_t i = 0; i < n; i++) {
		const DentParams *p = rec[i];
		if (!p) continue;
		if (p->mode == DENTM_VCUT) { // blast: each set once, at its first record
			bool k = (p->bits & DENTC_KEEP) != 0;
			bool &done = k ? doneKeep : doneRm;
			if (done || !vc || !vc->s) continue;
			done = true;
			Vector x = rest + acc, y = VCutMap (*vc, k, rest, x, full);
			if (cut && (y.x != x.x || y.y != x.y || y.z != x.z)) *cut = true;
			acc = y - rest;
		}
		else if (p->mode == DENTM_CUT) {
			Vector x = rest + acc, y = CutMap (*p, x, full);
			if (cut && (y.x != x.x || y.y != x.y || y.z != x.z)) *cut = true;
			acc = y - rest;
		}
		else if (full) acc += Displace (*p, rest);
		else acc += DisplaceLow (*p, rest);
	}
	return acc;
}

Vector DentMath::Fold (const std::vector<DentRecord> &rec, int g, const Vector &rest, bool full)
{
	std::vector<const DentParams *> l;
	for (const DentRecord &r : rec)
		if (g < 0 || r.grp.empty () || std::find (r.grp.begin (), r.grp.end (), (uint16_t)g) != r.grp.end ()) l.push_back (&r.p);
	return Fold (l.data (), l.size (), rest, full);
}

Vector DentMath::Fold (const std::vector<DentRecord> &rec, int g, const Vector &rest, bool full, const DentSites *sites)
{
	std::vector<const DentParams *> l;
	for (const DentRecord &r : rec)
		if (g < 0 || r.grp.empty () || std::find (r.grp.begin (), r.grp.end (), (uint16_t)g) != r.grp.end ()) l.push_back (&r.p);
	return Fold (l.data (), l.size (), rest, full, nullptr, sites);
}

bool DentMath::IsCut (uint32_t mode) { return mode == DENTM_CUT || mode == DENTM_VCUT; }

void DentMath::VCutSet (const DentParams *const *rec, size_t n, const DentSites *s, DentVCut &o)
{
	o.s = s, o.rm.clear (), o.keep.clear (), o.nrm = o.nkeep = 0;
	if (!s || s->s.empty ()) return;
	size_t ns = s->s.size ();
	o.rm.assign (ns, nullptr), o.keep.assign (ns, nullptr);
	for (size_t i = 0; i < n; i++) {
		const DentParams *p = rec[i];
		if (!p || p->mode != DENTM_VCUT || p->seed != ns || !(p->P >= 0.0) || !(p->P < (double)ns)) continue;
		size_t c = (size_t)p->P;
		if ((double)c != p->P) continue;
		bool k = (p->bits & DENTC_KEEP) != 0;
		std::vector<const DentParams *> &v = k ? o.keep : o.rm;
		if (v[c]) continue;
		v[c] = p;
		(k ? o.nkeep : o.nrm)++;
	}
}

void DentMath::VCutSet (const std::vector<const DentRecord *> &rec, const DentSites *s, DentVCut &o)
{
	std::vector<const DentParams *> l;
	for (const DentRecord *r : rec) if (r) l.push_back (&r->p);
	VCutSet (l.data (), l.size (), s, o);
}

size_t DentMath::Nearest (const DentSites &s, const Vector &x)
{
	size_t b = 0;
	double bd = 0.0;
	for (size_t i = 0; i < s.s.size (); i++) {
		double d = Len2 (x - s.s[i]);
		if (i == 0 || d < bd) b = i, bd = d;
	}
	return b;
}

Vector DentMath::VCutMap (const DentVCut &v, bool keep, const Vector &rest, const Vector &cur, bool full)
{
	if (!v.s || (keep ? v.nkeep : v.nrm) == 0) return cur;
	const std::vector<Vector> &S = v.s->s;
	const std::vector<const DentParams *> &set = keep ? v.keep : v.rm;
	size_t ns = S.size ();
	if (set.size () != ns) return cur;
	size_t a = Nearest (*v.s, rest); // cells by rest position
	if (keep == (set[a] != nullptr)) return cur; // removed set: a vertex outside it stays; keep set: a vertex inside it stays
	size_t b = ns;
	double bd = 0.0;
	for (size_t i = 0; i < ns; i++) {
		if (keep == (set[i] == nullptr)) continue; // removed set: nearest kept site; keep set: nearest site of the set
		double d = Len2 (rest - S[i]);
		if (b == ns || d < bd) b = i, bd = d;
	}
	if (b == ns) return S[a]; // every cell removed: the vertex goes to its site
	{ // the face of b's region the line from the vertex to S[b] crosses last: far vertices land on b's own boundary, not halfway across the hull
		Vector dl = S[b] - rest;
		double tb = -1.0;
		for (size_t j = 0; j < ns; j++) {
			if (j == b || keep != (set[j] == nullptr)) continue; // the other side: outside the keep set, or inside the removed set
			Vector e = S[j] - S[b];
			double c0 = Dot (e, rest) - 0.5 * (Len2 (S[j]) - Len2 (S[b])), c1 = Dot (e, dl);
			if (c0 > 0.0 && c1 < 0.0) { double t = -c0 / c1; if (t > tb) tb = t, a = j; }
		}
	}
	const DentParams &p = keep ? *set[b] : *set[a];
	size_t lo = a < b ? a : b, hi = a < b ? b : a;
	Vector nf = S[hi] - S[lo];
	double l = Len (nf);
	if (!(l > 0.0)) return cur;
	nf = nf / l;
	DentParams f;
	f.mode = DENTM_CUT, f.c = (S[lo] + S[hi]) * 0.5, f.n = nf, f.t = Tangent (nf, Vector (), 0.0);
	double hh = 0.5 * l, rc = 1.2 * hh; // bisector half-distance; rc about 0.6 sqrt(cell area)
	f.hd = p.hd, f.P = std::min (0.4 * p.hd, 0.1 * hh), f.hz = 0.0; // jag amplitude from the cell scale, never from P (the cell index)
	f.seed = Lowbias32 ((uint32_t)lo * 0x9e3779b9U ^ Lowbias32 ((uint32_t)hi + 0x632be5abU) ^ (uint32_t)ns) | 1u;
	double sgn = (keep ? b : a) == hi ? 1.0 : -1.0; // n into the removed cell a, or for keep into the kept cell b (mode-3 KEEP sense)
	Vector n = nf * sgn;
	Vector d = cur - f.c;
	double s = Dot (d, n);
	Vector dp = d - n * s;
	double J = CutJag (f, cur, full) * sgn, hl = p.hz > 0.0 ? std::min (p.hz, 0.05 * hh) : 0.0;
	double l2 = Len2 (dp);
	bool far = l2 > rc * rc;
	if (far) dp = dp * (rc / std::sqrt (l2)); // M8: no hull-sized sheet on the face
	if (keep) {
		if (!(s < J)) return far ? f.c + dp + n * s : cur; // a jag tooth stays, inside the radius
		double sg = (J - s) / (J - s + hl);
		return f.c + dp * (1.0 - DENT_CUT_PINCH * sg) + n * (J + hl * sg);
	}
	if (!(s > J)) return far ? f.c + dp + n * s : cur;
	double sg = (s - J) / (s - J + hl);
	return f.c + dp * (1.0 - DENT_CUT_PINCH * sg) + n * (J - hl * sg);
}

Vector DentMath::MapVCut (const void *ctx, const Vector &rest, const Vector &cur)
{
	const DentVCut *v = (const DentVCut*)ctx;
	return v ? VCutMap (*v, v->on, rest, cur, false) : cur;
}

Vector DentMath::MapLow (const void *ctx, const Vector &rest, const Vector &cur)
{
	const DentRecord *r = (const DentRecord*)ctx;
	return r ? CutMap (r->p, cur, false) : cur;
}

bool DentMath::HasCut (const std::vector<DentRecord> &rec, int g)
{
	for (const DentRecord &r : rec)
		if (IsCut (r.p.mode) && (g < 0 || r.grp.empty () || std::find (r.grp.begin (), r.grp.end (), (uint16_t)g) != r.grp.end ())) return true;
	return false;
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
	out = DentParams ();
	out.c = in.c, out.n = in.n, out.R = 0.0, out.h = 0.0, out.T = 0.0;
	if (!(ln > 0.0) || !std::isfinite (ln) || !Finite (in.c)) return DENT_NOSURFACE;
	if (ln != 1.0) out.n = in.n / ln;
	double V = (in.E > 0.0 && mat.sigma_c > 0.0) ? in.E / mat.sigma_c : 0.0;
	double R = Radius (in.E, mat.sigma_c, in.a, in.Rmax);
	if (in.vessel) R = std::max (R, LowPolyFloor (m, in.c)); // low-poly floor (4.2 step 3), also above Rmax
	out.R = R;
	out.T = SlabT (in.rayT, R);
	if (!(R > 0.0) || R > DENT_LIM_R || !Within (in.c, DENT_LIM_POS)) return DENT_NOSURFACE; // beyond what a record can hold (9.2)
	double hcap = HUGE_VAL;
	uint32_t mode = DENTM_BOWL;
	double H = 0.0;
	Vector ax;
	if (in.vessel && (in.modes & DENTI_NOISE)) { // seed from the hit record text, t from the slip
		DentParams q = out;
		Quantise (q);
		out.seed = ParamsHash (q) ^ in.salt;
		if (!out.seed) out.seed = 1;
		out.t = Tangent (out.n, in.tdir, in.vt);
		if (LobeGate (out, m)) out.bits |= DENTB_LOBES;
	}
	if (in.vessel && (in.modes & (DENTI_CRUSH | DENTI_HINGE))) {
		uint32_t cls = Classify (out, m, H, ax);
		mode = in.force >= 0 ? (uint32_t)in.force : cls;
		if (mode == DENTM_CRUSH && !(in.modes & DENTI_CRUSH)) mode = DENTM_BOWL;
		if (mode == DENTM_HINGE && (!(in.modes & DENTI_HINGE) || !(H > 0.0) || !(Len2 (ax) > 0.0))) mode = DENTM_BOWL;
		if (in.x) in.x->cls = cls, in.x->H = H;
		if (mode == DENTM_CRUSH) {
			out.bits &= ~DENTB_LOBES;
			int r = SolveCrush (in, m, capView, out);
			if (in.x && r == DENT_OK) { in.x->S = VolumeFactor (out, m); in.x->Esurplus = std::max (0.0, in.E - mat.sigma_c * out.h * in.x->S); }
			return r;
		}
		if (mode == DENTM_HINGE) hcap = DENT_HINGE_D * H;
	}
	double S = VolumeFactor (out, m);
	if (!(S > 0.0)) return DENT_NOSURFACE;
	double h = V / S;
	double cap = DepthCap (out, m, capView, std::min (hcap, in.vessel ? DmaxVessel (out.T, R, in.L) : Dmax (mat.t_cap, out.T, R, in.L)));
	if (h > cap) h = cap;
	if (h > DENT_LIM_H) h = DENT_LIM_H;
	if (!(h > 0.0)) h = 0.0;
	out.h = h;
	double qmax = 0.0;
	for (size_t v = 0; v < m.nv; v++) qmax = std::max (qmax, Weight (out, m.rest[v]));
	if (in.x) {
		in.x->S = S;
		double Eh = in.E - mat.sigma_c * h * S;
		in.x->Esurplus = std::max (0.0, Eh);
		if (mode == DENTM_HINGE && Eh > 0.0) { // overflow of a plate bowl: the rest folds the plate (S design 4)
			DentParams hp = out;
			hp.mode = DENTM_HINGE, hp.t = ax, hp.hd = 0.5 * R, hp.hz = 0.5 * H, hp.bits = 0, hp.T = 0.0;
			double Mp = HingeMp (hp, mat);
			if (Mp > 0.0) {
				hp.P = std::min (Eh / (2.0 * Mp), DENT_HINGE_TMAX);
				hp.h = 1.0;
				hp.h = std::min (MaxDisplace (hp, m), hp.hz);
				in.x->hinge = hp.h > 0.0, in.x->hp = hp, in.x->Mp = Mp;
				in.x->Esurplus = std::max (0.0, Eh - 2.0 * Mp * hp.P);
			}
		}
	}
	return (h * qmax < DENT_MIN_DEPTH) ? DENT_SMALL : DENT_OK;
}

uint32_t DentMath::Classify (const DentParams &p, const DentMeshView &m, double &H, Vector &a)
{
	H = 0.0, a = Vector ();
	if (!(p.R > 0.0)) return DENTM_BOWL;
	const double R2 = p.R * p.R;
	std::vector<double> q1 (m.nv, 0.0), q5 (m.nv, 0.0);
	for (size_t v = 0; v < m.nv; v++) {
		double s = Dot (p.c - m.rest[v], p.n);
		double t2 = (Len2 (m.rest[v] - p.c) - s * s) / R2;
		double k = Kernel (t2 < 0.0 ? 0.0 : t2);
		if (std::fabs (s) < DENT_CRUSH_BAND * p.R) q1[v] = k;
		if (std::fabs (s) < 0.5 * p.R) q5[v] = k;
	}
	double S1 = 0.0, S5 = 0.0;
	Vector G;
	for (size_t i = 0; i < m.nt; i++) {
		const uint32_t *v = m.tri + 3 * i;
		if (v[0] >= m.nv || v[1] >= m.nv || v[2] >= m.nv) continue;
		double an = Dot (Area (m.rest[v[0]], m.rest[v[1]], m.rest[v[2]]), p.n);
		if (!(an > 0.0)) continue;
		double a1 = an * (q1[v[0]] + q1[v[1]] + q1[v[2]]) / 3.0;
		S1 += a1, S5 += an * (q5[v[0]] + q5[v[1]] + q5[v[2]]) / 3.0;
		G += ((m.rest[v[0]] + m.rest[v[1]] + m.rest[v[2]]) / 3.0 - p.c) * a1;
	}
	const double disk = Pi * R2 / 3.0;
	double rho = S1 / disk, rho5 = S5 / disk;
	H = PlateThickness (m, p.c, p.n, p.R);
	bool plate = H > 0.0 && H < 0.5 * p.R && rho5 <= 1.25 * rho + 0.05;
	if (plate && rho >= 0.05 && rho <= 0.6 && S1 > 0.0) {
		Vector g = G / S1;
		g = g - p.n * Dot (g, p.n);
		double lg = Len (g);
		if (lg / p.R >= 0.15) { a = g / (-lg); return DENTM_HINGE; }
	}
	return rho < DENT_CRUSH_RHO ? DENTM_CRUSH : DENTM_BOWL;
}

double DentMath::PlateThickness (const DentMeshView &m, const Vector &c, const Vector &n, double R)
{
	double tmin = std::max (0.05, 0.02 * R), best = HUGE_VAL;
	Vector d = n * -1.0;
	for (size_t i = 0; i < m.nt; i++) {
		const uint32_t *v = m.tri + 3 * i;
		if (v[0] >= m.nv || v[1] >= m.nv || v[2] >= m.nv) continue;
		Vector A = Cross (m.rest[v[1]] - m.rest[v[0]], m.rest[v[2]] - m.rest[v[0]]);
		double la = Len (A);
		if (!(la > 0.0) || !(Dot (A, d) / la > 0.5)) continue; // back faces only
		DentMeshView one = m;
		one.tri = v, one.nt = 1;
		double t;
		if (RayCast (one, c, d, tmin, best, t) && t < best) best = t;
	}
	return best < HUGE_VAL ? best : 0.0;
}

double DentMath::HingeMp (const DentParams &p, const DentMaterial &mat)
{
	double b = 2.0 * p.R, H = 2.0 * p.hz;
	return DENT_HINGE_K * mat.sigma_c * b * H * H / 4.0;
}

bool DentMath::LobeGate (const DentParams &p, const DentMeshView &m)
{
	std::vector<std::pair<double, double>> e; // edge length, area weight
	double tot = 0.0;
	for (size_t i = 0; i < m.nt; i++) {
		const uint32_t *v = m.tri + 3 * i;
		if (v[0] >= m.nv || v[1] >= m.nv || v[2] >= m.nv) continue;
		const Vector &a = m.rest[v[0]], &b = m.rest[v[1]], &c = m.rest[v[2]];
		if (!(Len2 ((a + b + c) / 3.0 - p.c) < p.R * p.R)) continue;
		double A = Len (Area (a, b, c));
		if (!(A > 0.0)) continue;
		for (double l : { Len (b - a), Len (c - b), Len (a - c) }) e.push_back ({ l, A }), tot += A;
	}
	if (e.empty ()) return false;
	std::sort (e.begin (), e.end ());
	double acc = 0.0;
	for (const auto &x : e) { acc += x.second; if (acc >= 0.5 * tot) return x.first < DENT_LOBE_EDGE; }
	return e.back ().first < DENT_LOBE_EDGE;
}

Vector DentMath::Tangent (const Vector &n, const Vector &tdir, double vt)
{
	Vector t = tdir - n * Dot (tdir, n);
	double l = Len (t);
	if (vt > 0.5 && l > 1e-6) return t / l;
	double ax = std::fabs (n.x), ay = std::fabs (n.y), az = std::fabs (n.z);
	Vector e = (ax <= ay && ax <= az) ? Vector (1, 0, 0) : (ay <= az ? Vector (0, 1, 0) : Vector (0, 0, 1));
	t = Cross (n, e);
	l = Len (t);
	return l > 0.0 ? t / l : Vector (1, 0, 0);
}

void DentMath::MapToRest (const std::vector<const DentParams *> &rec, Vector &c, Vector &n)
{
	std::vector<const DentParams *> r;
	for (const DentParams *p : rec) if (p && p->mode >= DENTM_CRUSH && !IsCut (p->mode)) r.push_back (p);
	if (r.empty ()) return;
	Vector x = c;
	for (int it = 0; it < 6; it++) {
		Vector d;
		for (const DentParams *p : r) d += DisplaceLow (*p, x); // dmg3 m3: the collider's field
		x = c - d;
	}
	const DentParams *hb = nullptr;
	double best = 0.0;
	for (const DentParams *p : r) {
		if (p->mode != DENTM_HINGE) continue;
		double l = Len (DisplaceLow (*p, x));
		if (l > best) best = l, hb = p;
	}
	if (hb) { // n by the inverse Cayley rotation (-tau) at x
		const Vector &a = hb->t, &nn = hb->n;
		Vector v = x - (hb->c - a * hb->hd - nn * hb->hz);
		double l = std::max (2.0 * hb->hz, 0.25 * hb->R), r = Clamp (Dot (v, a) / l, 0.0, 1.0);
		double tau = -hb->P * r * r * (3.0 - 2.0 * r), d = 1.0 + tau * tau, co = (1.0 - tau * tau) / d, si = 2.0 * tau / d;
		double na = Dot (n, a), nb = Dot (n, nn);
		Vector m = n + a * (na * co + nb * si - na) + nn * (-na * si + nb * co - nb);
		double lm = Len (m);
		if (lm > 0.0) n = m / lm;
	}
	c = x;
}

int DentMath::FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r)
{
	return FindCoalesce (rec, r, nullptr);
}

int DentMath::FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r, const std::vector<uint8_t> *key)
{
	return FindCoalesce (rec, r, key, false);
}

int DentMath::FindCoalesce (const std::vector<DentRecord> &rec, const DentRecord &r, const std::vector<uint8_t> *key, bool anyMode)
{
	const double cmin = std::cos (DENT_COALESCE_ANGLE * Pi / 180.0);
	auto keyed = [key] (const std::vector<uint16_t> &g) {
		std::vector<uint16_t> o;
		for (uint16_t x : g) if (x < key->size () && (*key)[x]) o.push_back (x);
		return o;
	};
	if (IsCut (r.p.mode)) return -1; // dmg3 tear: a cut never grows
	auto shares = [] (const std::vector<uint16_t> &a, const std::vector<uint16_t> &b) {
		if (a.empty () || b.empty ()) return true;
		for (uint16_t x : a) if (std::find (b.begin (), b.end (), x) != b.end ()) return true;
		return false;
	};
	std::vector<uint16_t> rk;
	if (key) rk = keyed (r.grp);
	bool exact = !key || r.grp.empty () || rk.empty (); // all groups, or only unkeyed groups: the whole list compares
	int best = -1;
	double bd = 0.0;
	for (size_t i = 0; i < rec.size (); i++) {
		const DentRecord &o = rec[i];
		if (o.slot != r.slot || o.key != r.key || o.ngrp != r.ngrp || o.nvtx != r.nvtx) continue;
		if (!anyMode && o.p.mode != r.p.mode) continue; // dmg3: never across modes
		if (IsCut (o.p.mode)) continue;
		bool below = false; // dmg3 tear: no growth of a record below a cut on a shared group
		for (size_t j = i + 1; j < rec.size () && !below; j++)
			below = IsCut (rec[j].p.mode) && rec[j].slot == o.slot && rec[j].key == o.key && shares (rec[j].grp, o.grp);
		if (below) continue;
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
	if (Legacy (p) && p.P == 0.0 && p.hd == 0.0 && p.hz == 0.0) return; // legacy records: no new fields to round
	p.t = Vector (Q9 (p.t.x), Q9 (p.t.y), Q9 (p.t.z));
	p.P = Q9 (p.P), p.hd = Q9 (p.hd), p.hz = Q9 (p.hz);
}

// dmg3 crush (S design 2-3)

bool DentMath::Legacy (const DentParams &p) { return p.mode == 0 && p.seed == 0; }

double DentMath::DmaxCrush (double L) { return std::min (DENT_CRUSH_L * L, DENT_LIM_H); }

double DentMath::Coverage (const DentParams &p, const DentMeshView &m)
{
	if (!(p.R > 0.0)) return 0.0;
	const double band = DENT_CRUSH_BAND * p.R;
	std::vector<double> q (m.nv, 0.0);
	for (size_t v = 0; v < m.nv; v++) {
		double s = Dot (p.c - m.rest[v], p.n);
		double r2 = (Len2 (m.rest[v] - p.c) - s * s) / (p.R * p.R);
		if (std::fabs (s) < band) q[v] = Kernel (r2);
	}
	double S = 0.0;
	for (size_t i = 0; i < m.nt; i++) {
		const uint32_t *v = m.tri + 3 * i;
		if (v[0] >= m.nv || v[1] >= m.nv || v[2] >= m.nv) continue;
		double qs = q[v[0]] + q[v[1]] + q[v[2]];
		if (!(qs > 0.0)) continue;
		double an = Dot (Area (m.rest[v[0]], m.rest[v[1]], m.rest[v[2]]), p.n);
		if (an > 0.0) S += an * (qs / 3.0);
	}
	return S / (Pi * p.R * p.R / 3.0);
}

double DentMath::CrushW (const DentParams &p, const DentMeshView &m)
{
	return p.P * VolumeFactor (p, m);
}

int DentMath::SolveCrush (const DentInput &in, const DentMeshView &m, const DentMeshView *capView, DentParams &out)
{
	const DentMaterial &mat = in.mat ? *in.mat : DefaultMaterial (-1);
	double V = (in.E > 0.0 && mat.sigma_c > 0.0) ? in.E / mat.sigma_c : 0.0;
	double R = out.R, Pcap = DmaxCrush (in.L);
	if (!(Pcap > 0.0)) return DENT_NOSURFACE;
	double want = 0.0, core = DENT_CRUSH_CORE * R;
	for (size_t v = 0; v < m.nv; v++) { // the most protruding rest vertex of the flat core
		double s = Dot (out.c - m.rest[v], out.n);
		double r2 = Len2 (m.rest[v] - out.c) - s * s;
		if (r2 < core * core && -s > want) want = -s;
	}
	const DentParams base = out;
	auto at = [&] (double P) {
		DentParams q = base;
		q.mode = DENTM_CRUSH, q.P = P;
		double mv = std::min (std::min (want, DENT_CRUSH_MOVE * R), 0.5 * P);
		q.c = base.c + base.n * mv;
		return q;
	};
	double P = Pcap;
	if (!(CrushW (at (Pcap), m) < V)) {
		double lo = 0.0, hi = Pcap;
		for (int i = 0; i < DENT_CRUSH_STEPS; i++) {
			double mid = 0.5 * (lo + hi);
			if (CrushW (at (mid), m) < V) lo = mid;
			else hi = mid;
		}
		P = hi;
	}
	out = at (P);
	if (!(P > 0.0) || !Within (out.c, DENT_LIM_POS)) return DENT_NOSURFACE;
	double S = VolumeFactor (out, m);
	if (!(S > 0.0)) return DENT_NOSURFACE;
	double h = std::min (P, V / S);
	double cap = DepthCap (out, m, capView, DmaxCrush (in.L));
	if (h > cap) h = cap;
	if (h > DENT_LIM_H) h = DENT_LIM_H;
	if (!(h > 0.0)) h = 0.0;
	out.h = h;
	double qmax = 0.0;
	for (size_t v = 0; v < m.nv; v++) qmax = std::max (qmax, Weight (out, m.rest[v]));
	return (h * qmax < DENT_MIN_DEPTH) ? DENT_SMALL : DENT_OK;
}

bool DentMath::CoalesceCrush (const DentParams &old, double V, const DentMeshView &m, const DentMeshView *cap, double L, double &P, double &h)
{
	P = old.P, h = old.h;
	if (!(V > 0.0) || old.mode != DENTM_CRUSH) return false;
	double target = CrushW (old, m) + V, Pcap = DmaxCrush (L); // dmg3 m2: same measure as the bisection
	if (!(Pcap > old.P)) return false;
	auto at = [&] (double x) { DentParams q = old; q.P = x; return q; };
	double Pn = Pcap;
	if (!(CrushW (at (Pcap), m) < target)) {
		double lo = old.P, hi = Pcap;
		for (int i = 0; i < DENT_CRUSH_STEPS; i++) {
			double mid = 0.5 * (lo + hi);
			if (CrushW (at (mid), m) < target) lo = mid;
			else hi = mid;
		}
		Pn = hi;
	}
	DentParams q = at (Pn);
	double S = VolumeFactor (q, m);
	if (!(S > 0.0)) return false;
	double hn = std::min (Pn, target / S);
	double dc = DepthCap (q, m, cap, DmaxCrush (L));
	hn = std::min (hn, old.h + std::max (0.0, dc));
	hn = std::min (hn, DENT_LIM_H);
	if (hn < old.h) hn = old.h;
	if (!(Pn - old.P > 1e-9 * Pcap)) return false; // dmg3 m2: no P growth, no record growth
	P = Pn, h = hn;
	return true;
}

double DentMath::MaxDisplace (const DentParams &p, const DentMeshView &m)
{
	double dmax = 0.0;
	for (size_t v = 0; v < m.nv; v++) dmax = std::max (dmax, Len (Displace (p, m.rest[v])));
	return dmax;
}

uint32_t DentMath::ParamsHash (const DentParams &p)
{
	std::string s;
	const double v[7] = { p.c.x, p.c.y, p.c.z, p.n.x, p.n.y, p.n.z, p.R };
	for (double x : v) { s += ' '; PutNum (s, x); }
	return Fnv1a (s.data (), s.size ());
}

void DentMath::ApplyExt (DentParams &p, const DentParams &e)
{
	p.mode = e.mode, p.seed = e.seed, p.t = e.t, p.P = e.P, p.hd = e.hd, p.hz = e.hz, p.bits = e.bits;
}

std::string DentMath::FormatExtEvent (uint32_t recidx, const DentParams &p, double E, double vn, double vt, int hit, uint32_t evflags)
{
	std::string s;
	PutInt (s, recidx);
	PutHexArg (s, ParamsHash (p));
	PutExtFields (s, p);
	PutNumArg (s, Clamp (E, 0.0, DENT_LIM_E)); PutNumArg (s, Clamp (vn, -1e9, 1e9)); PutNumArg (s, Clamp (vt, -1e9, 1e9));
	if (hit >= 0) { s += ' '; PutInt (s, (uint32_t)(hit ? 1 : 0)); PutHexArg (s, evflags); } // dmg3 M3
	return s;
}

bool DentMath::ParseExtEvent (const char *payload, uint32_t &recidx, uint32_t &h8, DentParams &ext, double &E, double &vn, double &vt, int *hit, uint32_t *evflags)
{
	std::vector<Tok> t;
	Split (payload, t);
	DentParams e {};
	uint32_t k, h;
	double a, b, c;
	if (t.size () < 14 || !ParseInt (t[0], k) || t[1].n > 8 || !ParseInt (t[1], h, 16) || !ParseExtFields (t, 2, e) || !ParseD (t[11], a) || !ParseD (t[12], b) || !ParseD (t[13], c)) return false;
	uint32_t hk = 0, ef = 0;
	bool flag = t.size () >= 16 && ParseInt (t[14], hk) && hk <= 1 && t[15].n <= 8 && ParseInt (t[15], ef, 16); // dmg3 M3: optional hit flag and event flags
	recidx = k, h8 = h, ext = e, E = a, vn = b, vt = c;
	if (hit) *hit = flag ? (int)hk : -1;
	if (evflags) *evflags = flag ? ef : 0u;
	return true;
}

void DentMath::FormatTornEvent (const DentTorn &t, std::vector<std::string> &payload)
{
	const size_t lim = (size_t)DENT_EVENT_MAX;
	if (t.kin) { // dmg3 tear: live debris kinematics after the groups of the last payload
		std::string k = " K";
		const double v[7] = { t.dv.x, t.dv.y, t.dv.z, t.dw.x, t.dw.y, t.dw.z, t.mass };
		for (double x : v) PutNumArg (k, Clamp (x, -1e9, 1e9));
		if (!t.cells.empty () || !t.pieces.empty ()) { // blast: cell debris (recorder only): cells, pieces, centroid, crushed
			if (!t.cells.empty ()) k += " C=", PutList (k, t.cells);
			if (!t.pieces.empty ()) k += " P=", PutList (k, t.pieces);
			k += " X="; PutNum (k, Clamp (t.c.x, -DENT_LIM_POS, DENT_LIM_POS)); k += ','; PutNum (k, Clamp (t.c.y, -DENT_LIM_POS, DENT_LIM_POS)); k += ','; PutNum (k, Clamp (t.c.z, -DENT_LIM_POS, DENT_LIM_POS));
			if (t.crushed) k += " F=1";
		}
		size_t room = k.size () < lim ? lim - k.size () : 0;
		GroupLines (TornHead (t, room), t.grp, room, payload);
		payload.back () += k;
		return;
	}
	GroupLines (TornHead (t, lim), t.grp, lim, payload);
}

bool DentMath::ParseTornEvent (const char *payload, DentTorn &t, bool &more)
{
	std::vector<Tok> k;
	Split (payload, k);
	return ParseTornTok (k, 0, t, more);
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
	return Apply (p, rest, cur, grp, ngrp, dirty, nullptr);
}

size_t DentMath::Apply (const DentParams &p, const std::vector<std::vector<DentVtx>> &rest, std::vector<std::vector<DentVtx>> &cur,
	const uint16_t *grp, size_t ngrp, std::vector<std::vector<uint8_t>> *dirty, const DentVCut *vc)
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
			if (AddField (p, rest[g][i], cur[g][i], vc)) {
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
	const std::vector<std::vector<uint32_t>> &weld, uint32_t nweld, const std::vector<uint8_t> &touched, std::vector<std::vector<DentVtx>> &cur,
	const std::vector<uint8_t> *facet)
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
	struct FaceN { Vector r, c; };
	std::vector<std::vector<FaceN>> fl; // dmg3 crease rule: unit rest and current face normals per faceted weld id
	auto fac = [&] (uint32_t w) { return facet && w < facet->size () && (*facet)[w]; };
	if (facet) {
		fl.resize (nweld);
		for (size_t g = 0; g < ng; g++)
			for (size_t j = 0; j + 2 < idx[g].size (); j += 3) {
				uint32_t a = idx[g][j], b = idx[g][j+1], c = idx[g][j+2];
				if (!valid (g, a) || !valid (g, b) || !valid (g, c)) continue;
				uint32_t wa = weld[g][a], wb = weld[g][b], wc = weld[g][c];
				if (wa == wb || wb == wc || wc == wa || !(fac (wa) || fac (wb) || fac (wc))) continue;
				Vector Ar = Area (Pos (rest[g][a]), Pos (rest[g][b]), Pos (rest[g][c])), Ac = Area (Pos (cur[g][a]), Pos (cur[g][b]), Pos (cur[g][c]));
				double lr = Len (Ar), lc = Len (Ac);
				if (!(lr > 0.0) || !(lc > 0.0) || !Finite (Ac)) continue;
				FaceN f { Ar / lr, Ac / lc };
				for (uint32_t w : { wa, wb, wc }) if (fac (w)) fl[w].push_back (f);
			}
	}
	auto crease = [&] (uint32_t w, const Vector &nr, Vector &x) {
		if (!facet || w >= fl.size () || fl[w].size () < 2) return false;
		const std::vector<FaceN> &F = fl[w];
		double dmin = 1.0;
		for (size_t i = 0; i < F.size (); i++) for (size_t j = i + 1; j < F.size (); j++) dmin = std::min (dmin, Dot (F[i].c, F[j].c));
		if (!(dmin < 0.866)) return false;
		size_t k = 0;
		double turn = 2.0;
		for (size_t i = 0; i < F.size (); i++) { double d = Dot (F[i].r, F[i].c); if (d < turn) turn = d, k = i; }
		x = RotateMin (nr, F[k].r, F[k].c);
		return true;
	};
	for (size_t g = 0; g < ng; g++)
		for (size_t i = 0; i < rest[g].size () && i < cur[g].size () && i < weld[g].size (); i++) {
			uint32_t w = weld[g][i];
			if (w >= nweld || !upd[w] || w >= restSum.size ()) continue;
			Vector nr = Nml (rest[g][i]);
			Vector cx;
			if (Len2 (nr) >= 0.01 && crease (w, nr, cx)) {
				double l = Len (cx);
				if (l > 0.0 && std::isfinite (l)) { cx /= l; cur[g][i].nx = (float)cx.x, cur[g][i].ny = (float)cx.y, cur[g][i].nz = (float)cx.z; continue; }
			}
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

bool DentMath::NameHash (const std::string &name, uint32_t &h)
{
	if (name.size () != 9 || name[0] != '#') return false;
	for (size_t i = 1; i < 9; i++) { char c = Lower (name[i]); if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false; }
	return ParseInt (name.data () + 1, name.data () + 9, h, 16);
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
	if (v.rec.empty () && v.eabs == 0.0 && v.flags == 0 && v.torn.empty () && v.debris.empty () && v.sites.empty () && v.brokenBonds.empty () && v.weakBonds.empty ()) return;
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
	std::vector<size_t> ext; // dmg3 extension section: written ordinals of new-build records
	for (size_t j = 0; j < ok.size (); j++) if (!Legacy (v.rec[ok[j]].p)) ext.push_back (j);
	if (ext.empty () && v.torn.empty () && v.debris.empty () && v.sites.empty () && v.brokenBonds.empty () && v.weakBonds.empty ()) return;
	s = ind + "XDMG ";
	PutInt (s, DENT_VERSION_X);
	s += ' '; PutInt (s, ok.size ());
	PutHexArg (s, ok.empty () ? 0u : ParamsHash (v.rec[ok.back ()].p));
	PutNumArg (s, Clamp (v.eabs, 0.0, DENT_LIM_E));
	lines.push_back (s);
	for (size_t j : ext) {
		const DentParams &p = v.rec[ok[j]].p;
		s = ind + "XDMGD ";
		PutInt (s, j);
		PutHexArg (s, ParamsHash (p));
		PutExtFields (s, p);
		lines.push_back (s);
	}
	for (const DentTorn &t : v.torn) { std::string th = ind + "XDMGM T "; GroupLines (th + TornHead (t, lim - th.size ()), t.grp, lim, lines); }
	for (const DentDebris &d : v.debris) FormatDebris (d, ind, lines);
	for (const DentSites &x : v.sites) FormatSites (x, ind, lines);
	for (const auto &b : v.brokenBonds) FormatBonds (b.first, b.second, ind, lines);
	for (const auto &b : v.weakBonds) FormatBonds (b.first, b.second, ind, lines, "W");
}

void DentMath::FormatSites (const DentSites &x, const std::string &ind, std::vector<std::string> &lines)
{
	if (x.s.empty () || x.s.size () > 65535) return;
	std::string h = ind + "XDMGM S ";
	PutInt (h, x.slot); h += ' ';
	PutHex8 (h, x.key); h += ' ';
	PutInt (h, x.s.size ());
	SiteLines (h, x.s, (size_t)DENT_LINE_MAX, lines);
}

void DentMath::FormatBonds (uint32_t slot, const std::vector<uint32_t> &b, const std::string &ind, std::vector<std::string> &lines, const char *tag)
{
	if (b.empty ()) return;
	std::string h = ind + "XDMGM " + tag + " ";
	PutInt (h, slot);
	BondLines (h, b, (size_t)DENT_LINE_MAX, lines);
}

void DentMath::FormatSitesEvent (const DentSites &x, std::vector<std::string> &payload)
{
	const size_t lim = (size_t)DENT_EVENT_MAX;
	size_t i = 0;
	while (i < x.s.size ()) {
		std::string l;
		PutInt (l, x.slot); l += ' ';
		PutHex8 (l, x.key); l += ' ';
		PutInt (l, x.s.size ()); l += ' ';
		PutInt (l, i);
		size_t k = 0;
		while (i < x.s.size ()) {
			std::string t;
			PutNumArg (t, x.s[i].x); PutNumArg (t, x.s[i].y); PutNumArg (t, x.s[i].z);
			if (k && l.size () + t.size () > lim) break;
			l += t, i++, k++;
		}
		payload.push_back (l);
	}
}

bool DentMath::ParseSitesEvent (const char *payload, DentSites &x, uint32_t &n, uint32_t &first)
{
	std::vector<Tok> t;
	Split (payload, t);
	x = DentSites ();
	bool more = false;
	if (t.size () < 7 || !ParseInt (t[0], x.slot) || t[1].n > 8 || !ParseInt (t[1], x.key, 16) || !ParseInt (t[2], n) || !ParseInt (t[3], first) || n == 0 || n > 65535 || first >= n) return false;
	if (!ParseSiteToks (t, 4, x.s, more) || more) return false;
	return first + x.s.size () <= n;
}

void DentMath::FormatBondsEvent (uint32_t slot, const std::vector<uint32_t> &b, std::vector<std::string> &payload)
{
	std::string h;
	PutInt (h, slot);
	BondLines (h, b, (size_t)DENT_EVENT_MAX, payload);
}

bool DentMath::ParseBondsEvent (const char *payload, uint32_t &slot, std::vector<uint32_t> &b, bool &more)
{
	std::vector<Tok> t;
	Split (payload, t);
	return t.size () == 2 && ParseInt (t[0], slot) && ParseBondTok (t[1], b, more);
}

void DentMath::FormatDebris (const DentDebris &d, const std::string &ind, std::vector<std::string> &lines)
{
	const size_t lim = (size_t)DENT_LINE_MAX - 2;
	std::string s = ind + "XDMGM B ";
	PutInt (s, d.id); s += ' ';
	PutInt (s, d.slot);
	PutHexArg (s, d.key);
	s += ' '; PutInt (s, (unsigned)d.ngrp);
	s += ' '; PutInt (s, d.nvtx);
	s += ' '; Put17 (s, d.simt);
	std::string nm = NameTok (Clean (d.name, false)), ot;
	size_t ms = d.mass > 0.0 ? 17 : 0;
	if (!d.other.empty ()) ot = " O=" + NameTok (Clean (d.other, false)); // the impactor: its pair filter comes back on load
	std::string hn = "#", ho = " O=#";
	PutHex8 (hn, Fnv1a (d.name.data (), d.name.size ())); PutHex8 (ho, Fnv1a (d.other.data (), d.other.size ()));
	auto fits = [&] (const std::string &n, const std::string &o) { return s.size () + 1 + n.size () + ms + o.size () <= lim; };
	if (!fits (nm, ot)) { // too long: the impactor's hash, else the name's, else both
		if (!ot.empty () && fits (nm, ho)) ot = ho;
		else if (fits (hn, ot)) nm = hn;
		else { nm = hn; if (!ot.empty ()) ot = ho; }
	}
	s += " " + nm;
	if (d.mass > 0.0) PutNumArg (s, d.mass); // dmg3 tear: section mass, reused on reload
	s += ot;
	lines.push_back (s);
	for (const DentDebrisPose &q : d.pose) {
		std::string h = ind + "XDMGM Q ";
		PutInt (h, d.id);
		GroupLines (h + PoseHead (q), q.grp, lim, lines);
	}
	uint32_t j = 0; // dmg3 m4: ordinal among written rows
	for (size_t jr = 0; jr < d.rec.size (); jr++) {
		const DentRecord &r = d.rec[jr];
		if (!ParamsOk (r.p)) continue;
		std::string h = ind + "XDMGD B ";
		PutInt (h, d.id); h += ' ';
		PutInt (h, j++);
		std::string x = h;
		PutParams (h, r.p);
		GroupLines (h, r.grp, lim, lines);
		if (Legacy (r.p)) continue;
		x += " X";
		PutHexArg (x, ParamsHash (r.p));
		PutExtFields (x, r.p);
		lines.push_back (x);
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
		else if (m_sec == 4) V2 (raw);
		else Keep (raw);
		return true;
	}
	Close (); // an XDMG line ends an open group list
	m_tornOpen = false, m_qOpen = 0, m_sOpen = 0;
	int ver;
	if (t.size () < 2 || !ParseInt (t[1], ver)) { m_skipped++; return true; } // no section change
	bool first = m_sec == 0;
	uint32_t xn = 0, xh = 0;
	double xe = 0;
	bool x2 = ver == DENT_VERSION_X && !m_x2 && t.size () == 5 && ParseInt (t[2], xn) && t[3].n == 8 && ParseInt (t[3], xh, 16) && ParseD (t[4], xe) && xe >= 0.0;
	if (x2) { // dmg3 extension section: XDMG 2 <n> <h8last> <eabs>; any other version-2 line is an unknown section
		m_sec = 4;
		m_x2 = true, m_xn = xn, m_xh = xh, m_xe = Q9 (xe);
		if (first) for (const std::string &l : m_pending) V2 (l);
	} else if (ver != DENT_VERSION) {
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
	m_ord.pop_back ();
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
	if (t.size () < 12 || !ParseInt (t[1], k) || !ParseParams (t, 2, r.p) || !ParseGroups (t[11], r.grp, more)) { if (!m_open) m_nord++; Close (); m_skipped++; return; } // a bad continuation line: its record's ordinal is counted already
	if (m_open) { // the previous line's list ended in ',': this line continues it, or the record dangles
		DentRecord &p = m_dent.back ().second;
		if (m_dent.back ().first == k && SameParams (p.p, r.p) && !r.grp.empty ()) {
			if (p.grp.size () + r.grp.size () > DENT_MAX_GRPLIST) m_over = m_capped = true; // no more groups stored; the record is dropped at its end
			else if (!m_over) p.grp.insert (p.grp.end (), r.grp.begin (), r.grp.end ());
			m_open = more;
			if (!m_open && m_over) { if (m_meshIdx.count (k)) m_nKnown--; m_dent.pop_back (); m_ord.pop_back (); m_skipped++; m_over = false; }
			return;
		}
		Close ();
	}
	uint32_t ord = m_nord++;                              // dmg3: written ordinal of this record
	bool known = m_meshIdx.count (k) != 0;                // orphans do not count toward the cap; Finish skips them
	if ((known && m_nKnown >= DENT_MAX_VESSEL) || r.grp.size () > DENT_MAX_GRPLIST) { // past the live cap (R7) or the group limit: not stored
		m_capped = true;
		if (!more) { m_skipped++; return; }
		r.grp.clear (), m_over = true; // its continuation lines are read and dropped with it
	}
	m_dent.push_back ({ k, r });
	m_ord.push_back (ord);
	if (known) m_nKnown++;
	m_open = more;
}

void DentVesselParser::V2 (const std::string &line)
{
	std::vector<Tok> t;
	Split (line.c_str (), t);
	int so = m_sOpen;
	m_sOpen = 0;
	if (t.size () >= 2 && IEq (t[0].p, t[0].n, "XDMGM") && (IEq (t[1].p, t[1].n, "S") || IEq (t[1].p, t[1].n, "K") || IEq (t[1].p, t[1].n, "W"))) { // blast: sites, broken and weakened bonds
		m_tornOpen = false, m_qOpen = 0;
		bool more = false;
		bool w = IEq (t[1].p, t[1].n, "W");
		if (w || IEq (t[1].p, t[1].n, "K")) { // XDMGM K <slot> <b,b,...>; XDMGM W <slot> <pair,h,pair,h,...>
			uint32_t slot;
			std::vector<uint32_t> b;
			auto &L = w ? m_weak : m_bonds;
			int open = w ? 3 : 2;
			if (t.size () != 4 || !ParseInt (t[2], slot) || !ParseBondTok (t[3], b, more)) { m_skipped++; return; }
			if (so == open && !L.empty () && L.back ().first == slot) {
				std::vector<uint32_t> &o = L.back ().second;
				if (o.size () + b.size () <= (w ? 2 : 1) * (size_t)DENT_MAX_GRPLIST) o.insert (o.end (), b.begin (), b.end ());
			} else {
				for (size_t i = 0; i < L.size (); i++) if (L[i].first == slot) { L.erase (L.begin () + i); break; }
				L.push_back ({ slot, b });
			}
			m_sOpen = more ? open : 0;
			return;
		}
		DentSites x; // XDMGM S <slot> <key8> <n> x y z ...
		uint32_t n;
		if (t.size () < 8 || !ParseInt (t[2], x.slot) || t[3].n > 8 || !ParseInt (t[3], x.key, 16) || !ParseInt (t[4], n) || n == 0 || n > 65535 || !ParseSiteToks (t, 5, x.s, more)) {
			if (so == 1 && m_sLeft) m_sites.pop_back (), m_sLeft = 0;
			m_skipped++;
			return;
		}
		if (so == 1 && m_sLeft && m_sites.back ().slot == x.slot && m_sites.back ().key == x.key && m_sites.back ().s.size () + m_sLeft == n && x.s.size () <= m_sLeft) {
			std::vector<Vector> &o = m_sites.back ().s;
			o.insert (o.end (), x.s.begin (), x.s.end ());
			m_sLeft -= (uint32_t)x.s.size ();
		} else {
			if (m_sLeft) m_sites.pop_back (), m_sLeft = 0, m_skipped++; // the open list never ended
			if (x.s.size () > n) { m_skipped++; return; }
			for (size_t i = 0; i < m_sites.size (); i++) if (m_sites[i].slot == x.slot) { m_sites.erase (m_sites.begin () + i); break; }
			m_sLeft = n - (uint32_t)x.s.size ();
			m_sites.push_back (x);
		}
		if (m_sLeft && !more) m_sites.pop_back (), m_sLeft = 0, m_skipped++; // short list
		else if (!m_sLeft && more) more = false;
		m_sOpen = more ? 1 : 0;
		return;
	}
	if (m_sLeft) m_sites.pop_back (), m_sLeft = 0, m_skipped++;
	if (t.size () >= 2 && IEq (t[1].p, t[1].n, "B")) { // debris rows: XDMGM B header, XDMGD B parent record copy and its X extension
		m_tornOpen = false;
		uint32_t id;
		if (t.size () < 4 || !ParseInt (t[2], id)) { m_qOpen = 0; m_skipped++; return; }
		DentDebris *d = nullptr;
		for (DentDebris &x : m_debris) if (x.id == id) d = &x;
		if (IEq (t[0].p, t[0].n, "XDMGM")) {
			m_qOpen = 0;
			DentDebris n;
			uint32_t ngrp;
			n.id = id;
			if (d || t.size () < 9 || !ParseInt (t[3], n.slot) || t[4].n > 8 || !ParseInt (t[4], n.key, 16) || !ParseInt (t[5], ngrp) || ngrp > 65535 || !ParseInt (t[6], n.nvtx)
				|| !ParseD (t[7], n.simt)) { m_skipped++; return; }
			n.ngrp = (uint16_t)ngrp, n.name = NameUntok (t[8]);
			double ms;
			if (t.size () >= 10 && ParseD (t[9], ms) && ms > 0.0 && ms < 1e12) n.mass = Q9 (ms);
			for (size_t k = 9; k < t.size (); k++) if (t[k].n > 2 && Lower (t[k].p[0]) == 'o' && t[k].p[1] == '=') n.other = NameUntok (Tok { t[k].p + 2, t[k].n - 2 }); // optional impactor
			m_debris.push_back (n);
			return;
		}
		uint32_t j;
		if (!d || !ParseInt (t[3], j)) { m_qOpen = 0; m_skipped++; return; }
		if (t.size () >= 6 && IEq (t[4].p, t[4].n, "X")) { // XDMGD B <id> <j> X <h8> <ext>
			m_qOpen = 0;
			uint32_t h8;
			DentParams e {};
			if (j >= d->rec.size () || t.size () < 15 || t[5].n > 8 || !ParseInt (t[5], h8, 16) || !ParseExtFields (t, 6, e) || DentMath::ParamsHash (d->rec[j].p) != h8) { m_skipped++; return; }
			DentMath::ApplyExt (d->rec[j].p, e);
			return;
		}
		DentRecord r {};
		bool more = false;
		if (t.size () < 14 || !ParseParams (t, 4, r.p) || !ParseGroups (t[13], r.grp, more)) { m_qOpen = 0; m_skipped++; return; }
		if (m_qOpen == 2 && j + 1 == d->rec.size () && SameParams (d->rec.back ().p, r.p) && !r.grp.empty ()) {
			std::vector<uint16_t> &g = d->rec.back ().grp;
			if (g.size () + r.grp.size () <= DENT_MAX_GRPLIST) g.insert (g.end (), r.grp.begin (), r.grp.end ());
		} else if (j == d->rec.size () && d->rec.size () < DENT_MAX_VESSEL) {
			r.slot = d->slot, r.key = d->key, r.ngrp = d->ngrp, r.nvtx = d->nvtx;
			d->rec.push_back (r);
		} else { m_qOpen = 0; m_skipped++; return; }
		m_qOpen = more ? 2 : 0;
		return;
	}
	if (t.size () >= 2 && IEq (t[0].p, t[0].n, "XDMGM") && IEq (t[1].p, t[1].n, "Q")) { // XDMGM Q <id> <px> <py> <pz> <qx> <qy> <qz> <qw> <groups>
		m_tornOpen = false;
		uint32_t id;
		DentDebrisPose q;
		double v[7];
		bool more = false, ok = t.size () >= 11 && ParseInt (t[2], id);
		for (int k = 0; ok && k < 7; k++) ok = ParseD (t[3 + k], v[k]);
		ok = ok && ParseGroups (t[10], q.grp, more);
		DentDebris *d = nullptr;
		if (ok) for (DentDebris &x : m_debris) if (x.id == id) d = &x;
		if (!d) { m_qOpen = 0; m_skipped++; return; }
		q.p = Vector (Q9 (v[0]), Q9 (v[1]), Q9 (v[2]));
		for (int k = 0; k < 4; k++) q.q[k] = Q9 (v[3 + k]);
		if (m_qOpen == 1 && !d->pose.empty () && d == &m_debris.back () && SameBits (d->pose.back ().p.x, q.p.x) && SameBits (d->pose.back ().p.y, q.p.y) && SameBits (d->pose.back ().p.z, q.p.z)
			&& std::memcmp (d->pose.back ().q, q.q, sizeof q.q) == 0 && !q.grp.empty ()) {
			std::vector<uint16_t> &g = d->pose.back ().grp;
			if (g.size () + q.grp.size () <= DENT_MAX_GRPLIST) g.insert (g.end (), q.grp.begin (), q.grp.end ());
		} else d->pose.push_back (q);
		m_qOpen = more ? 1 : 0;
		return;
	}
	m_qOpen = 0;
	if (IEq (t[0].p, t[0].n, "XDMGD")) { // XDMGD <j> <h8> <mode> <P> <seed8> <tx> <ty> <tz> <bits> <hd> <hz>
		m_tornOpen = false;
		Ext e {};
		if (t.size () < 12 || !ParseInt (t[1], e.j) || t[2].n > 8 || !ParseInt (t[2], e.h8, 16) || !ParseExtFields (t, 3, e.p)) { m_skipped++; return; }
		m_ext.push_back (e);
		return;
	}
	DentTorn r; // XDMGM T <kind> <slot> <key8> <ngrp> <nvtx> <simt> <debris> <groups>
	bool more = false;
	if (t.size () < 10 || !IEq (t[1].p, t[1].n, "T") || !ParseTornTok (t, 2, r, more)) { m_tornOpen = false; m_skipped++; return; }
	if (m_tornOpen && SameTornHead (m_torn.back (), r) && !r.grp.empty ()) {
		std::vector<uint16_t> &g = m_torn.back ().grp;
		if (g.size () + r.grp.size () <= DENT_MAX_GRPLIST) g.insert (g.end (), r.grp.begin (), r.grp.end ());
		m_tornOpen = more;
		return;
	}
	m_torn.push_back (r);
	m_tornOpen = more;
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
	std::unordered_map<uint32_t, size_t> ordRec; // dmg3: written ordinal -> out.rec index
	for (size_t i = 0; i < m_dent.size (); i++) {
		const auto &d = m_dent[i];
		auto mi = m_meshIdx.find (d.first);
		if (mi == m_meshIdx.end ()) { skipped++; continue; } // unknown key
		const DentRecord *m = &m_mesh[mi->second].second;
		if (out.rec.size () >= DENT_MAX_VESSEL) { skipped++, m_capped = true; continue; } // the live cap (R7); the rest counts as skipped
		DentRecord r = d.second;
		r.slot = m->slot, r.key = m->key, r.ngrp = m->ngrp, r.nvtx = m->nvtx;
		if (i < m_ord.size ()) ordRec[m_ord[i]] = out.rec.size ();
		out.rec.push_back (r);
	}
	for (const Ext &e : m_ext) { // bound by written ordinal and the hash of c, n, R; else the record stays a bowl
		auto f = ordRec.find (e.j);
		if (f == ordRec.end () || DentMath::ParamsHash (out.rec[f->second].p) != e.h8) { skipped++; continue; }
		DentMath::ApplyExt (out.rec[f->second].p, e.p);
	}
	if (m_x2 && !m_torn.empty ()) { // torn rows hold while the saved record set does
		bool ok = m_xn == 0 ? out.eabs >= m_xe : (out.rec.size () >= m_xn && DentMath::ParamsHash (out.rec[m_xn - 1].p) == m_xh);
		if (ok) out.torn = m_torn;
		else skipped += (int)m_torn.size ();
	}
	if (m_x2 && !m_debris.empty ()) { // debris rows: same binding as torn rows
		bool ok = m_xn == 0 ? out.eabs >= m_xe : (out.rec.size () >= m_xn && DentMath::ParamsHash (out.rec[m_xn - 1].p) == m_xh);
		if (ok) out.debris = m_debris;
		else skipped += (int)m_debris.size ();
	}
	if (m_sLeft) m_sites.pop_back (), m_sLeft = 0, skipped++;
	out.sites = m_sites, out.brokenBonds = m_bonds, out.weakBonds = m_weak; // blast
	for (auto &x : out.weakBonds) if (x.second.size () & 1) x.second.pop_back (); // pairs only
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
