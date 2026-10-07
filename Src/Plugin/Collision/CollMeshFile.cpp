// not upstream: collision addon, the own .msh parser, cfg readers, mesh paths and the mesh cache (design E2 4, 5)
#include <charconv>
#include <cmath>
#include <cstring>
#include <limits>
#include <system_error>
#include "CollMeshFile.h"
#include "CollSdk.h"

namespace {

// istream::getline (cbuf, n) replica: fails at EOF and on lines of n or more chars, and stays failed
struct LineReader {
	const std::string &buf; size_t pos = 0; bool failed = false; size_t n;
	bool eofLast = false; // the last line read ended at EOF without '\n' (eofbit set)
	LineReader (const std::string &b, size_t n): buf (b), n (n) {}
	bool Get (std::string &line)
	{
		if (failed || pos >= buf.size ()) { failed = true; return false; }
		size_t e = buf.find ('\n', pos);
		size_t end = (e == std::string::npos ? buf.size () : e);
		if (end - pos > n - 1) { failed = true; return false; }
		line.assign (buf, pos, end - pos);
		size_t z = line.find ('\0');
		if (z != std::string::npos) line.resize (z);
		eofLast = (e == std::string::npos);
		pos = (e == std::string::npos ? buf.size () : e + 1);
		return true;
	}
};

inline bool IsSpace (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
inline char Upper (char c) { return (c >= 'a' && c <= 'z') ? char (c - 'a' + 'A') : c; }
inline bool IsHex (char c) { return (c >= '0' && c <= '9') || (Upper (c) >= 'A' && Upper (c) <= 'F'); }
inline bool IsDigit (char c) { return c >= '0' && c <= '9'; }

bool Prefix (const std::string &s, const char *key)
{
	size_t i = 0;
	for (; key[i]; i++)
		if (i >= s.size () || Upper (s[i]) != Upper (key[i])) return false;
	return true;
}

bool EqNoCase (const char *a, const char *b)
{
	for (; *a && *b; a++, b++) if (Upper (*a) != Upper (*b)) return false;
	return *a == *b;
}

const char *SkipWs (const char *p, const char *e) { while (p < e && IsSpace (*p)) p++; return p; }

// sscanf "%d"/"%hd" step, saturated like strtol
bool ScanInt (const char *&p, const char *e, long long &v)
{
	const char *q = SkipWs (p, e);
	bool neg = false;
	if (q < e && (*q == '+' || *q == '-')) { neg = (*q == '-'); q++; }
	if (q >= e || !IsDigit (*q)) return false;
	unsigned long long u = 0;
	auto r = std::from_chars (q, e, u);
	if (r.ec == std::errc::result_out_of_range) u = std::numeric_limits<unsigned long long>::max ();
	if (neg) v = (u > 9223372036854775808ull) ? std::numeric_limits<long long>::min () : (long long)(0ull - u);
	else v = (u > 9223372036854775807ull) ? std::numeric_limits<long long>::max () : (long long)u;
	p = r.ptr;
	return true;
}

// sscanf "%x" step: optional sign and 0x prefix
bool ScanHex (const char *&p, const char *e, uint32_t &v)
{
	const char *q = SkipWs (p, e);
	bool neg = false;
	if (q < e && (*q == '+' || *q == '-')) { neg = (*q == '-'); q++; }
	if (e - q >= 3 && q[0] == '0' && (q[1] == 'x' || q[1] == 'X') && IsHex (q[2])) q += 2;
	unsigned long long u = 0;
	auto r = std::from_chars (q, e, u, 16);
	if (r.ec == std::errc::invalid_argument) return false;
	if (r.ec == std::errc::result_out_of_range) u = std::numeric_limits<unsigned long long>::max ();
	v = (uint32_t)(neg ? 0ull - u : u);
	p = r.ptr;
	return true;
}

int ScanFloats (const std::string &s, float *const *dst, int n)
{
	const char *p = s.data (), *e = s.data () + s.size ();
	if (SkipWs (p, e) == e) return -1;
	int k = 0;
	for (; k < n; k++)
		if (!CollScanFloat (p, e, *dst[k])) break;
	return k;
}

// Mesh::CalcNormals (grp, true) in float (Mesh.cpp:636-695)
void CalcNormals (CollGroupData &g)
{
	const float eps = 1e-8f;
	size_t nv = g.vtx.size (), nt = g.idx.size () / 3;
	std::vector<bool> calc (nv);
	for (size_t i = 0; i < nv; i++) {
		CollVtx &v = g.vtx[i];
		if (v.nx*v.nx + v.ny*v.ny + v.nz*v.nz > 0.1f) calc[i] = false;
		else { calc[i] = true; v.nx = v.ny = v.nz = 0.0f; }
	}
	for (size_t i = 0; i < nt; i++) {
		uint32_t i0 = g.idx[i*3], i1 = g.idx[i*3+1], i2 = g.idx[i*3+2];
		if (i0 >= nv || i1 >= nv || i2 >= nv) continue;
		if (!calc[i0] && !calc[i1] && !calc[i2]) continue;
		CollVtx &a = g.vtx[i0], &b = g.vtx[i1], &c = g.vtx[i2];
		float v01[3] = { b.x - a.x, b.y - a.y, b.z - a.z };
		float v02[3] = { c.x - a.x, c.y - a.y, c.z - a.z };
		float v12[3] = { c.x - b.x, c.y - b.y, c.z - b.z };
		float nm[3] = { v01[1]*v02[2] - v01[2]*v02[1], v01[2]*v02[0] - v01[0]*v02[2], v01[0]*v02[1] - v01[1]*v02[0] };
		float len = (float)std::sqrt (nm[0]*nm[0] + nm[1]*nm[1] + nm[2]*nm[2]);
		if (len < eps) continue;
		nm[0] /= len, nm[1] /= len, nm[2] /= len;
		float d01 = (float)std::sqrt (v01[0]*v01[0] + v01[1]*v01[1] + v01[2]*v01[2]);
		float d02 = (float)std::sqrt (v02[0]*v02[0] + v02[1]*v02[1] + v02[2]*v02[2]);
		float d12 = (float)std::sqrt (v12[0]*v12[0] + v12[1]*v12[1] + v12[2]*v12[2]);
		if (calc[i0]) { float w = std::acos ((d01*d01 + d02*d02 - d12*d12) / (2.0f*d01*d02)); a.nx += nm[0]*w, a.ny += nm[1]*w, a.nz += nm[2]*w; }
		if (calc[i1]) { float w = std::acos ((d01*d01 + d12*d12 - d02*d02) / (2.0f*d01*d12)); b.nx += nm[0]*w, b.ny += nm[1]*w, b.nz += nm[2]*w; }
		if (calc[i2]) { float w = std::acos ((d02*d02 + d12*d12 - d01*d01) / (2.0f*d02*d12)); c.nx += nm[0]*w, c.ny += nm[1]*w, c.nz += nm[2]*w; }
	}
	for (size_t i = 0; i < nv; i++)
		if (calc[i]) {
			CollVtx &v = g.vtx[i];
			float len = (float)std::sqrt (v.nx*v.nx + v.ny*v.ny + v.nz*v.nz);
			v.nx /= len, v.ny /= len, v.nz /= len;
		}
}

} // namespace

bool CollScanFloat (const char *&p, const char *e, float &v)
{
	const char *q = SkipWs (p, e);
	bool plus = false, neg = false;
	if (q < e && *q == '+') { plus = true; q++; }
	if (plus && q < e && *q == '-') return false;
	const char *m = q;
	if (m < e && *m == '-') { neg = true; m++; }
	if (e - m >= 2 && m[0] == '0' && (m[1] == 'x' || m[1] == 'X')) { // glibc reads hex floats; a bare 0x fails
		const char *h = m + 2;
		if (h >= e || !(IsHex (*h) || (*h == '.' && h + 1 < e && IsHex (h[1])))) return false;
		double d = 0;
		auto r = std::from_chars (h, e, d, std::chars_format::hex);
		if (r.ec == std::errc::invalid_argument) return false;
		v = (float)(neg ? -d : d);
		p = r.ptr;
		return true;
	}
	float f = 0.0f;
	auto r = std::from_chars (q, e, f, std::chars_format::general);
	if (r.ec == std::errc::invalid_argument) return false;
	if (r.ec == std::errc::result_out_of_range) {
		double d = 0.0;
		auto r2 = std::from_chars (q, e, d, std::chars_format::general);
		if (r2.ec == std::errc::result_out_of_range) {
			bool under = false;
			for (const char *c = q; c + 1 < r2.ptr; c++) if ((*c == 'e' || *c == 'E') && c[1] == '-') under = true;
			d = (*q == '-' ? -1.0 : 1.0) * (under ? 0.0 : std::numeric_limits<double>::infinity ());
		}
		f = (float)d;
	}
	const char *t = r.ptr;
	if (t < e && (*t == 'e' || *t == 'E') && t > q && (IsDigit (t[-1]) || t[-1] == '.')) { // glibc consumes "e", "e+" without digits and fails the conversion
		const char *u = t + 1;
		if (u < e && (*u == '+' || *u == '-')) u++;
		if (u >= e || !IsDigit (*u)) { p = u; return false; }
	}
	v = f;
	p = t;
	return true;
}

bool CollParseMsh (const std::string &text, const char *name, CollRestMesh &out)
{
	out = CollRestMesh ();
	std::string base (name ? name : "");
	size_t sl = base.find_last_of ("/\\");
	if (sl != std::string::npos) base.erase (0, sl + 1);
	if (base.size () > 4 && Prefix (base.substr (base.size () - 4), ".MSH")) base.resize (base.size () - 4);
	out.name = base;
	LineReader in (text, 256);
	std::string line;
	if (!in.Get (line)) return false;
	if (line != "MSHX1" && line != "MSHX1\r") return true;
	long long ngrp = 0;
	for (;;) {
		if (!in.Get (line)) return false;
		if (Prefix (line, "GROUPS")) {
			const char *p = line.data () + 6, *e = line.data () + line.size ();
			if (!ScanInt (p, e, ngrp)) return true;
			break;
		}
	}
	bool term = false;
	for (long long g = 0; g < ngrp && !term; g++) {
		CollGroupData grp;
		bool bnormal = true, calcnml = false, flipidx = false;
		uint32_t uflag = 0;
		long long nvtx = 0, ntri = 0;
		bool have = false;
		for (;;) {
			if (!in.Get (line)) { term = true; break; }
			const char *e = line.data () + line.size ();
			if (Prefix (line, "MATERIAL") || Prefix (line, "TEXTURE") || Prefix (line, "ZBIAS") || Prefix (line, "TEXWRAP")) {
			} else if (Prefix (line, "NONORMAL")) {
				bnormal = false; calcnml = true;
			} else if (Prefix (line, "FLAG")) {
				const char *p = line.data () + 4;
				ScanHex (p, e, uflag);
			} else if (Prefix (line, "FLIP")) {
				flipidx = true;
			} else if (Prefix (line, "LABEL") || Prefix (line, "STATIC") || Prefix (line, "DYNAMIC")) {
			} else if (Prefix (line, "GEOM")) {
				const char *p = line.data () + 4;
				if (!ScanInt (p, e, nvtx) || !ScanInt (p, e, ntri)) { nvtx = ntri = 0; break; }
				if (nvtx < 0 || ntri < 0 || nvtx > 0x7fffffff / 32 || ntri > 0x7fffffff / 6) { term = true; nvtx = 0; break; }
				grp.vtx.assign ((size_t)nvtx, CollVtx {});
				for (long long i = 0; i < nvtx; i++) {
					CollVtx &v = grp.vtx[(size_t)i];
					if (!in.Get (line)) { grp.vtx.clear (); nvtx = 0; term = true; break; }
					if (bnormal) {
						float *dst[8] = { &v.x, &v.y, &v.z, &v.nx, &v.ny, &v.nz, &v.tu, &v.tv };
						if (ScanFloats (line, dst, 8) < 6) {
							calcnml = true;
							v.tu = v.nx; v.tv = v.ny;
							v.nx = v.ny = v.nz = 0.0f;
						}
					} else {
						float *dst[5] = { &v.x, &v.y, &v.z, &v.tu, &v.tv };
						ScanFloats (line, dst, 5);
					}
				}
				if (term) break;
				grp.idx.assign ((size_t)ntri * 3, 0);
				for (long long i = 0; i < ntri; i++) {
					if (!in.Get (line)) { grp.vtx.clear (); grp.idx.clear (); nvtx = 0; term = true; break; }
					const char *q = line.data (), *qe = line.data () + line.size ();
					for (int k = 0; k < 3; k++) {
						long long x;
						if (!ScanInt (q, qe, x)) break;
						grp.idx[(size_t)i*3+k] = (uint16_t)x;
					}
				}
				if (flipidx)
					for (size_t i = 0; i + 2 < grp.idx.size (); i += 3) std::swap (grp.idx[i+1], grp.idx[i+2]);
				have = true;
				break;
			}
		}
		if (have && nvtx && !grp.idx.empty ()) {
			grp.usrflag = uflag;
			if (calcnml) CalcNormals (grp);
			out.nvtx += (uint32_t)grp.vtx.size ();
			out.grp.push_back (std::move (grp));
		}
	}
	return true;
}

bool CollOrbCfg::String (const char *key, std::string &val) const
{
	LineReader in (text, 512);
	std::string line;
	size_t kl = strlen (key);
	bool found = false;
	while (in.Get (line)) if (!strncmp (line.c_str (), key, kl)) { found = true; break; }
	if (!found || in.eofLast) return false; // the core checks is.good () after the match
	size_t i = 0;
	while (i < line.size () && line[i] != ';' && line[i] != '\r') i++;
	line.resize (i);
	size_t q = line.find ('=');
	if (q == std::string::npos) return false;
	q++;
	while (q < line.size () && (line[q] == ' ' || line[q] == '\t')) q++;
	if (line.size () - q >= 256) return false;
	val = line.substr (q);
	return true;
}

bool CollOrbCfg::Int (const char *key, int &val) const
{
	std::string s;
	if (!String (key, s)) return false;
	const char *p = s.c_str (), *e = p + s.size ();
	long long v;
	if (!ScanInt (p, e, v)) return false;
	val = (int)v;
	return true;
}

bool CollOrbCfg::Real (const char *key, double &val) const
{
	std::string s;
	if (!String (key, s)) return false;
	const char *p = SkipWs (s.c_str (), s.c_str () + s.size ()), *e = s.c_str () + s.size ();
	if (p < e && *p == '+') p++;
	double d;
	auto r = std::from_chars (p, e, d, std::chars_format::general);
	if (r.ec == std::errc::invalid_argument) return false;
	val = d;
	return true;
}

bool CollOrbCfg::Bool (const char *key, bool &val) const
{
	std::string s;
	if (!String (key, s)) return false;
	auto pre = [&](const char *k) { return Prefix (s, k); };
	if (pre ("true")) { val = true; return true; }
	if (pre ("false")) { val = false; return true; }
	return false;
}

void CollOrbCfg::Dirs (CollDirs &d, std::vector<std::string> *warn) const
{
	auto get = [&](const char *key, std::string &dir) {
		std::string v;
		if (!String (key, v) || v.empty ()) return;
		if (v.back () != '\\' && v.size () + 1 >= 256) { if (warn) warn->push_back (std::string ("Value of ") + key + " too long, ignored"); return; }
		if (v.back () != '\\') v += '\\';
		dir = v;
	};
	get ("ConfigDir", d.configDir);
	get ("MeshDir", d.meshDir);
}

bool CollItemString (const std::string &text, const char *label, std::string &val)
{
	LineReader in (text, 512);
	std::string line;
	while (in.Get (line)) {
		size_t c = line.find (';');
		if (c != std::string::npos) line.resize (c);
		while (!line.empty () && (line.back () == ' ' || line.back () == '\t' || line.back () == '\r')) line.pop_back ();
		size_t b = 0;
		while (b < line.size () && (line[b] == ' ' || line[b] == '\t')) b++;
		std::string cl = line.substr (b);
		if (EqNoCase (cl.c_str (), "END_PARSE")) return false;
		size_t i = cl.find ('=');
		std::string k = cl.substr (0, i), v = (i == std::string::npos ? std::string () : cl.substr (i + 1));
		while (!k.empty () && (k.back () == ' ' || k.back () == '\t')) k.pop_back ();
		if (!EqNoCase (k.c_str (), label)) continue;
		size_t q = 0;
		while (q < v.size () && (v[q] == ' ' || v[q] == '\t')) q++;
		if (q >= v.size () || v.size () - q >= 256) return false;
		val = v.substr (q);
		return true;
	}
	return false;
}

bool CollItemReal (const std::string &text, const char *label, double &val)
{
	std::string s;
	if (!CollItemString (text, label, s)) return false;
	const char *p = s.c_str (), *e = p + s.size ();
	if (*p == '+') p++;
	double d;
	auto r = std::from_chars (p, e, d, std::chars_format::general);
	if (r.ec == std::errc::invalid_argument) return false;
	val = d;
	return true;
}

bool CollItemInt (const std::string &text, const char *label, int &val)
{
	std::string s;
	if (!CollItemString (text, label, s)) return false;
	const char *p = s.c_str (), *e = p + s.size ();
	long long v;
	if (!ScanInt (p, e, v)) return false;
	val = (int)v;
	return true;
}

bool CollItemBool (const std::string &text, const char *label, bool &val)
{
	std::string s;
	if (!CollItemString (text, label, s)) return false;
	if (Prefix (s, "TRUE")) { val = true; return true; }
	if (Prefix (s, "FALSE")) { val = false; return true; }
	return false;
}

std::string CollMeshPath (const CollDirs &d, const std::string &name, const char *ext)
{
	std::string p = d.meshDir + name + ext;
	return p.size () >= 256 ? std::string () : p;
}

std::string CollCfgPath (const CollDirs &d, const std::string &name, const char *ext)
{
	std::string p = d.configDir + name + ext;
	return p.size () >= 256 ? std::string () : p;
}

std::string CollLower (const std::string &s)
{
	std::string r (s);
	for (char &c : r) { if (c >= 'A' && c <= 'Z') c = char (c - 'A' + 'a'); if (c == '\\') c = '/'; }
	return r;
}

uint64_t CollFnv (const void *data, size_t n, uint64_t h)
{
	const uint8_t *p = (const uint8_t *)data;
	for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ull; }
	return h;
}

void CollMeshCache::Keep (const std::shared_ptr<const CollRestMesh> &m)
{
	size_t bytes = 0;
	for (auto &g : m->grp) bytes += g.vtx.size () * sizeof (CollVtx) + g.idx.size () * 2;
	lru.push_back (m);
	lruBytes += bytes;
	while (lru.size () > 16 || (lruBytes > (64u << 20) && lru.size () > 1)) {
		size_t b = 0;
		for (auto &g : lru.front ()->grp) b += g.vtx.size () * sizeof (CollVtx) + g.idx.size () * 2;
		lruBytes -= b;
		lru.pop_front ();
	}
}

CollMeshCache::NameMesh CollMeshCache::ByName (CollSdk &sdk, const CollDirs &d, const std::string &name)
{
	NameMesh r;
	r.key = "F:" + CollLower (name);
	auto it = rest.find (r.key);
	if (it != rest.end ())
		if (auto sp = it->second.lock ()) { r.rest = sp; r.present = presentOf[r.key]; return r; }
	if (missing.count (r.key)) return r;
	std::string path = CollMeshPath (d, name, ".msh");
	std::string text;
	if (path.empty () || !sdk.ReadText (sdk.Resolve (path), text)) {
		missing.insert (r.key);
		sdk.Log (1, ("Collision: mesh file not found: " + (path.empty () ? name : path)).c_str ());
		return r;
	}
	auto m = std::make_shared<CollRestMesh> ();
	r.present = CollParseMsh (text, name.c_str (), *m);
	nParsed++;
	if (!r.present) { missing.insert (r.key); return r; }
	m->name = name;
	r.rest = m;
	rest[r.key] = r.rest;
	presentOf[r.key] = r.present;
	Keep (r.rest);
	return r;
}

std::shared_ptr<const CollRestMesh> CollMeshCache::ByTemplate (CollSdk &sdk, const void *tpl, std::string &key)
{
	auto m = std::make_shared<CollRestMesh> ();
	const char *nm = sdk.TplName (tpl);
	uint32_t ng = sdk.TplGroups (tpl);
	uint64_t h = CollFnv (nullptr, 0);
	for (uint32_t g = 0; g < ng; g++) {
		CollTplGroup tg {};
		if (!sdk.TplGroup (tpl, g, tg)) continue;
		CollGroupData gd;
		if (tg.vtx && tg.nvtx) gd.vtx.assign (tg.vtx, tg.vtx + tg.nvtx);
		if (tg.idx && tg.nidx) gd.idx.assign (tg.idx, tg.idx + tg.nidx);
		gd.usrflag = tg.usrflag;
		h = CollFnv (gd.vtx.data (), gd.vtx.size () * sizeof (CollVtx), h);
		h = CollFnv (gd.idx.data (), gd.idx.size () * 2, h);
		h = CollFnv (&gd.usrflag, 4, h);
		m->nvtx += (uint32_t)gd.vtx.size ();
		m->grp.push_back (std::move (gd));
	}
	if (nm) m->name = nm;
	char hx[24];
	snprintf (hx, sizeof hx, "%016llx", (unsigned long long)h);
	key = std::string ("H:") + (nm ? CollLower (nm) : std::string ()) + ":" + hx;
	auto it = rest.find (key);
	if (it != rest.end ())
		if (auto sp = it->second.lock ()) return sp;
	std::shared_ptr<const CollRestMesh> r = m;
	rest[key] = r;
	Keep (r);
	return r;
}

std::shared_ptr<const CollSidecar> CollMeshCache::Sidecar (CollSdk &sdk, const CollDirs &d, const std::string &name, uint32_t ngrp)
{
	if (name.empty ()) return nullptr;
	std::string key = CollLower (name) + "#" + std::to_string (ngrp);
	if (noSide.count (key)) return nullptr;
	auto it = side.find (key);
	if (it != side.end ()) return it->second;
	std::string path = CollMeshPath (d, name, ".col"), text;
	if (path.empty () || !sdk.ReadText (sdk.Resolve (path), text)) { noSide.insert (key); return nullptr; }
	auto sc = std::make_shared<CollSidecar> ();
	std::vector<std::string> warn;
	if (!CollParseSidecar (text.data (), text.size (), path.c_str (), *sc, warn)) {
		for (auto &w : warn) sdk.Log (1, ("Collision sidecar: " + w).c_str ());
		noSide.insert (key);
		return nullptr;
	}
	if (sc->needNames) {
		std::string mtext;
		CollMeshTags tags;
		bool ok = sdk.ReadText (sdk.Resolve (CollMeshPath (d, name, ".msh")), mtext) && CollScanMeshTags (mtext.data (), mtext.size (), tags, warn);
		CollResolveNames (*sc, ok ? &tags : nullptr, ngrp, warn);
	}
	for (auto &w : warn) sdk.Log (1, ("Collision sidecar: " + w).c_str ());
	side[key] = sc;
	return sc;
}
