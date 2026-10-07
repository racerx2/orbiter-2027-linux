// not upstream: .msh loader for the collision unit tests with Mesh.cpp:800-925 rules; header-only

#ifndef __COLLTESTMSH_H
#define __COLLTESTMSH_H

#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>
#include "CollGeom.h"

#ifndef COLL_TEST_SRC
#define COLL_TEST_SRC "."
#endif

namespace CollTestMshDetail {

// istream::getline (cbuf, 256) replica: fails at EOF and on lines over 255 chars, and stays failed
struct LineReader {
	std::string buf; size_t pos = 0; bool failed = false;
	bool Get (std::string &line)
	{
		if (failed || pos >= buf.size()) { failed = true; return false; }
		size_t e = buf.find ('\n', pos);
		size_t end = (e == std::string::npos ? buf.size() : e);
		if (end - pos > 255) { failed = true; return false; }
		line.assign (buf, pos, end - pos);
		size_t z = line.find ('\0');
		if (z != std::string::npos) line.resize (z);
		pos = (e == std::string::npos ? buf.size() : e + 1);
		return true;
	}
};

inline bool IsSpace (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }
inline char Upper (char c) { return (c >= 'a' && c <= 'z') ? char (c - 'a' + 'A') : c; }
inline bool IsHex (char c) { return (c >= '0' && c <= '9') || (Upper (c) >= 'A' && Upper (c) <= 'F'); }

// strncasecmp (s, key, strlen (key)) == 0 with ASCII folding
inline bool Prefix (const std::string &s, const char *key)
{
	size_t i = 0;
	for (; key[i]; i++)
		if (i >= s.size() || Upper (s[i]) != Upper (key[i])) return false;
	return true;
}

inline const char *SkipWs (const char *p, const char *e) { while (p < e && IsSpace (*p)) p++; return p; }

// sscanf "%d"/"%hd" step: integer with optional sign, saturated like strtol
inline bool ScanInt (const char *&p, const char *e, long long &v)
{
	const char *q = SkipWs (p, e);
	bool neg = false;
	if (q < e && (*q == '+' || *q == '-')) { neg = (*q == '-'); q++; }
	if (q >= e || *q < '0' || *q > '9') return false;
	unsigned long long u = 0;
	auto r = std::from_chars (q, e, u);
	if (r.ec == std::errc::result_out_of_range) u = std::numeric_limits<unsigned long long>::max();
	if (neg) v = (u > 9223372036854775808ull) ? std::numeric_limits<long long>::min() : (long long)(0ull - u);
	else v = (u > 9223372036854775807ull) ? std::numeric_limits<long long>::max() : (long long)u;
	p = r.ptr;
	return true;
}

// sscanf "%x" step: optional sign and 0x prefix
inline bool ScanHex (const char *&p, const char *e, uint32_t &v)
{
	const char *q = SkipWs (p, e);
	bool neg = false;
	if (q < e && (*q == '+' || *q == '-')) { neg = (*q == '-'); q++; }
	if (e - q >= 3 && q[0] == '0' && (q[1] == 'x' || q[1] == 'X') && IsHex (q[2])) q += 2;
	unsigned long long u = 0;
	auto r = std::from_chars (q, e, u, 16);
	if (r.ec == std::errc::invalid_argument) return false;
	if (r.ec == std::errc::result_out_of_range) u = std::numeric_limits<unsigned long long>::max();
	v = (uint32_t)(neg ? 0ull - u : u);
	p = r.ptr;
	return true;
}

// sscanf "%f" step: decimal float, locale-free, out-of-range values rounded through double
inline bool ScanFloat (const char *&p, const char *e, float &v)
{
	const char *q = SkipWs (p, e);
	bool plus = false;
	if (q < e && *q == '+') { plus = true; q++; }
	if (plus && q < e && *q == '-') return false;
	float f = 0.0f;
	auto r = std::from_chars (q, e, f, std::chars_format::general);
	if (r.ec == std::errc::invalid_argument) return false;
	if (r.ec == std::errc::result_out_of_range) {
		double d = 0.0;
		auto r2 = std::from_chars (q, e, d, std::chars_format::general);
		if (r2.ec == std::errc::result_out_of_range) {
			bool under = false;
			for (const char *c = q; c + 1 < r2.ptr; c++) if ((*c == 'e' || *c == 'E') && c[1] == '-') under = true;
			d = (*q == '-' ? -1.0 : 1.0) * (under ? 0.0 : std::numeric_limits<double>::infinity());
		}
		f = (float)d;
	}
	v = f;
	p = r.ptr;
	return true;
}

// sscanf "%f%f..." into consecutive fields; returns the number converted (-1 if the line is blank)
inline int ScanFloats (const std::string &s, float *const *dst, int n)
{
	const char *p = s.data(), *e = s.data() + s.size();
	if (SkipWs (p, e) == e) return -1;
	int k = 0;
	for (; k < n; k++)
		if (!ScanFloat (p, e, *dst[k])) break;
	return k;
}

// Mesh::CalcNormals (grp, true) (Mesh.cpp:636-695) in float; out-of-range triangles skipped
inline void CalcNormals (CollGroupData &g)
{
	const float eps = 1e-8f;
	size_t nv = g.vtx.size(), nt = g.idx.size() / 3;
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

} // namespace CollTestMshDetail

// COLL_TEST_SRC "/" rel
inline std::string CollTestPath (const char *rel)
{
	return std::string (COLL_TEST_SRC) + "/" + rel;
}

// in-memory CollTestLoadMsh (no test files needed); path names the mesh: base name without .msh
inline bool CollTestParseMsh (const std::string &text, const char *path, CollRestMesh &out, std::string *err = nullptr)
{
	using namespace CollTestMshDetail;
	out = CollRestMesh ();
	auto fail = [&](const std::string &m) { if (err) *err = std::string (path) + ": " + m; return false; };
	LineReader in;
	in.buf = text;

	std::string base (path);
	size_t sl = base.find_last_of ("/\\");
	if (sl != std::string::npos) base.erase (0, sl + 1);
	if (base.size() > 4 && Prefix (base.substr (base.size() - 4), ".MSH")) base.resize (base.size() - 4);
	out.name = base;

	std::string line;
	if (!in.Get (line)) return fail ("empty file");
	if (line != "MSHX1" && line != "MSHX1\r") return fail ("no MSHX1 header");
	long long ngrp = 0;
	for (;;) {
		if (!in.Get (line)) return fail ("no GROUPS line");
		if (Prefix (line, "GROUPS")) {
			const char *p = line.data() + 6, *e = line.data() + line.size();
			if (!ScanInt (p, e, ngrp)) return fail ("bad GROUPS line");
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
			const char *e = line.data() + line.size();
			if (Prefix (line, "MATERIAL") || Prefix (line, "TEXTURE") || Prefix (line, "ZBIAS") || Prefix (line, "TEXWRAP")) {
			} else if (Prefix (line, "NONORMAL")) {
				bnormal = false; calcnml = true;
			} else if (Prefix (line, "FLAG")) {
				const char *p = line.data() + 4;
				ScanHex (p, e, uflag);
			} else if (Prefix (line, "FLIP")) {
				flipidx = true;
			} else if (Prefix (line, "LABEL") || Prefix (line, "STATIC") || Prefix (line, "DYNAMIC")) {
			} else if (Prefix (line, "GEOM")) {
				const char *p = line.data() + 4;
				if (!ScanInt (p, e, nvtx) || !ScanInt (p, e, ntri)) { nvtx = ntri = 0; break; }
				if (nvtx < 0 || ntri < 0 || nvtx > 0x7fffffff / 32 || ntri > 0x7fffffff / 6) return fail ("bad GEOM counts");
				grp.vtx.assign ((size_t)nvtx, CollVtx {});
				for (long long i = 0; i < nvtx; i++) {
					CollVtx &v = grp.vtx[(size_t)i];
					if (!in.Get (line)) { grp.vtx.clear(); nvtx = 0; break; }
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
				grp.idx.assign ((size_t)ntri * 3, 0);
				for (long long i = 0; i < ntri; i++) {
					if (!in.Get (line)) { grp.vtx.clear(); grp.idx.clear(); nvtx = 0; break; }
					const char *q = line.data(), *qe = line.data() + line.size();
					for (int k = 0; k < 3; k++) {
						long long x;
						if (!ScanInt (q, qe, x)) break;
						grp.idx[(size_t)i*3+k] = (uint16_t)x;
					}
				}
				if (flipidx)
					for (size_t i = 0; i + 2 < grp.idx.size(); i += 3) std::swap (grp.idx[i+1], grp.idx[i+2]);
				have = true;
				break;
			}
		}
		if (have && nvtx && !grp.idx.empty()) {
			grp.usrflag = uflag;
			if (calcnml) CalcNormals (grp);
			out.nvtx += (uint32_t)grp.vtx.size();
			out.grp.push_back (std::move (grp));
		}
	}
	return true;
}

// header with optional \r, GROUPS, FLAG -> usrflag, FLIP, NONORMAL, GEOM, 8/6/3 numbers, %hd
inline bool CollTestLoadMsh (const char *path, CollRestMesh &out, std::string *err = nullptr)
{
	std::ifstream ifs (path, std::ios::binary);
	if (!ifs) { out = CollRestMesh (); if (err) *err = std::string (path) + ": cannot open"; return false; }
	std::ostringstream ss;
	ss << ifs.rdbuf();
	return CollTestParseMsh (ss.str(), path, out, err);
}

#endif // !__COLLTESTMSH_H
