// not upstream: vessel and building colliders (D1 3-5, 7): parts, sidecar, tags, templates, damage

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <system_error>
#include "CollShape.h"

// ASCII helpers: own case folding, no locale

static char Lower (char c) { return (c >= 'A' && c <= 'Z') ? char (c - 'A' + 'a') : c; }

static std::string Fold (const std::string &s)
{
	std::string r (s);
	for (char &c : r) c = Lower (c);
	return r;
}

static bool EqNoCase (const std::string &a, const char *b)
{
	size_t n = strlen (b);
	if (a.size() != n) return false;
	for (size_t i = 0; i < n; i++) if (Lower (a[i]) != Lower (b[i])) return false;
	return true;
}

static bool IsBlank (char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\v' || c == '\f' || c == '\0'; }
static bool IsSpace (char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r'; }

// material names (R18): process-global, id 0 = vessel default; a deque keeps c_str pointers valid

struct CollMatTable { std::deque<std::string> name { std::string () }; std::map<std::string, uint16_t> id; };
static CollMatTable &Mats () { static CollMatTable t; return t; }

uint16_t CollInternMaterial (const char *name)
{
	if (!name || !*name) return 0;
	std::string k = Fold (name);
	CollMatTable &t = Mats ();
	auto it = t.id.find (k);
	if (it != t.id.end()) return it->second;
	if (t.name.size() > 0xFFFF) { CollLog (COLLLOG_WARN, "Collider: material table full, '%s' uses the default", name); return 0; }
	uint16_t id = (uint16_t)t.name.size();
	t.name.push_back (k);
	t.id[k] = id;
	return id;
}

const char *CollMaterialName (uint16_t id)
{
	CollMatTable &t = Mats ();
	return id < t.name.size() ? t.name[id].c_str() : "";
}

// sidecar parser (D1 4.2, 4.4)

static void Tokens (const char *s, size_t n, std::vector<std::string> &tok)
{
	tok.clear ();
	size_t i = 0;
	while (i < n) {
		while (i < n && IsBlank (s[i])) i++;
		size_t b = i;
		while (i < n && !IsBlank (s[i])) i++;
		if (i > b) tok.emplace_back (s + b, i - b);
	}
}

static bool ParseU32 (const std::string &t, uint32_t &v)
{
	if (t.empty()) return false;
	for (char c : t) if (c < '0' || c > '9') return false;
	auto r = std::from_chars (t.data(), t.data() + t.size(), v);
	return r.ec == std::errc () && r.ptr == t.data() + t.size();
}

static bool ParseNum (const std::string &t, double &v)
{
	const char *b = t.data(), *e = t.data() + t.size();
	if (b < e && *b == '+') b++;
	if (b == e) return false;
	auto r = std::from_chars (b, e, v);
	return r.ec == std::errc () && r.ptr == e && std::isfinite (v);
}

// N or A-B; reversed ranges are swapped
static bool ParseRange (const std::string &t, std::pair<uint32_t,uint32_t> &r, bool &swapped)
{
	swapped = false;
	size_t d = t.find ('-');
	uint32_t a, b;
	if (d == std::string::npos) {
		if (!ParseU32 (t, a)) return false;
		r = std::make_pair (a, a);
		return true;
	}
	if (!ParseU32 (t.substr (0, d), a) || !ParseU32 (t.substr (d+1), b)) return false;
	if (a > b) { std::swap (a, b); swapped = true; }
	r = std::make_pair (a, b);
	return true;
}

// selector from tok[i..]; err: bad selector (line skipped); notes: warnings that keep the line
static bool ParseSelector (const std::vector<std::string> &tok, size_t i, CollSelector &sel, std::string &err, std::vector<std::string> &notes)
{
	sel = CollSelector ();
	if (i >= tok.size()) { err = "missing selector"; return false; }
	const std::string &k = tok[i];
	if (EqNoCase (k, "ALL")) {
		if (tok.size() > i+1) { err = "unexpected '" + tok[i+1] + "' after ALL"; return false; }
		sel.kind = CollSelector::ALL;
		return true;
	}
	if (EqNoCase (k, "GROUP")) {
		sel.kind = CollSelector::GROUP;
		if (tok.size() == i+1) { err = "GROUP without indices"; return false; }
		for (size_t j = i+1; j < tok.size(); j++) {
			std::pair<uint32_t,uint32_t> r;
			bool sw;
			if (!ParseRange (tok[j], r, sw)) { err = "bad group index '" + tok[j] + "'"; return false; }
			if ((uint64_t)r.second - r.first + 1 > COLL_RANGE_MAX) { err = "range '" + tok[j] + "' has more than 65536 entries"; return false; }
			if (sw) notes.push_back ("reversed range '" + tok[j] + "' swapped");
			sel.range.push_back (r);
		}
		return true;
	}
	if (EqNoCase (k, "LABEL")) sel.kind = CollSelector::LABEL;
	else if (EqNoCase (k, "MATERIAL")) sel.kind = CollSelector::MATERIAL;
	else if (EqNoCase (k, "TEXTURE")) sel.kind = CollSelector::TEXTURE;
	else { err = "unknown selector '" + k + "'"; return false; }
	if (tok.size() == i+1) { err = k + " without names"; return false; }
	for (size_t j = i+1; j < tok.size(); j++) {
		size_t s = tok[j].find ('*');
		if (s != std::string::npos && s != tok[j].size()-1) { err = "'*' is allowed only at the end of a name: '" + tok[j] + "'"; return false; }
		sel.pat.push_back (Fold (tok[j]));
	}
	return true;
}

bool CollParseSidecar (const char *text, size_t len, const char *fname, CollSidecar &out, std::vector<std::string> &warn)
{
	out = CollSidecar ();
	std::string fn = (fname && *fname) ? fname : "sidecar";
	auto Warn = [&] (uint32_t line, const std::string &msg) {
		warn.push_back (line ? fn + ":" + std::to_string (line) + ": " + msg : fn + ": " + msg);
	};
	if (!text) len = 0;
	if (len > COLL_SIDECAR_MAX) { Warn (0, "larger than 1 MB, ignored"); return false; }

	size_t p = 0;
	if (len >= 3 && (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) p = 3; // UTF-8 BOM
	bool header = false;
	uint32_t lineNo = 0;
	uint64_t nfollow = 0;  // FOLLOW groups of the file so far (stored expanded, so capped like one GROUP range)
	std::vector<std::string> tok, notes;
	while (p < len) {
		const char *nl = (const char*)memchr (text + p, '\n', len - p);
		size_t end = nl ? (size_t)(nl - text) : len;
		size_t n = end - p;
		const char *s = text + p;
		p = nl ? end + 1 : len;
		lineNo++;
		const char *sc = (const char*)memchr (s, ';', n);
		if (sc) n = (size_t)(sc - s);
		Tokens (s, n, tok);
		if (tok.empty()) continue;

		if (!header) {
			if (!EqNoCase (tok[0], COLL_SIDECAR_HEADER)) {
				Warn (lineNo, std::string ("first line is not ") + COLL_SIDECAR_HEADER + ", file ignored");
				out = CollSidecar ();
				return false;
			}
			if (tok.size() > 1) Warn (lineNo, "unexpected '" + tok[1] + "' after the header, ignored");
			header = true;
			continue;
		}

		const std::string &kw = tok[0];
		std::string err;
		notes.clear ();
		if (EqNoCase (kw, "EXCLUDE") || EqNoCase (kw, "INCLUDE")) {
			CollSideRule r;
			r.op = EqNoCase (kw, "EXCLUDE") ? CollSideRule::EXCLUDE : CollSideRule::INCLUDE;
			r.mat = 0; r.line = lineNo;
			if (!ParseSelector (tok, 1, r.sel, err, notes)) { Warn (lineNo, err + ", line skipped"); continue; }
			if (r.sel.kind >= CollSelector::LABEL) out.needNames = true;
			out.rule.push_back (std::move (r));
		} else if (EqNoCase (kw, "MAT")) {
			if (tok.size() < 2) { Warn (lineNo, "MAT without material name, line skipped"); continue; }
			CollSideRule r;
			r.op = CollSideRule::MAT; r.line = lineNo;
			if (!ParseSelector (tok, 2, r.sel, err, notes)) { Warn (lineNo, err + ", line skipped"); continue; }
			r.mat = CollInternMaterial (tok[1].c_str());
			if (r.sel.kind >= CollSelector::LABEL) out.needNames = true;
			out.rule.push_back (std::move (r));
		} else if (EqNoCase (kw, "SKIN") || EqNoCase (kw, "WELD")) {
			bool skin = EqNoCase (kw, "SKIN");
			double v;
			if (tok.size() != 2 || !ParseNum (tok[1], v)) { Warn (lineNo, "bad number for " + kw + ", line skipped"); continue; }
			double lo = skin ? COLL_SKIN_MIN : 0.0, hi = skin ? COLL_SKIN_MAX : COLL_WELD_MAX;
			if (v < lo || v > hi) {
				char buf[96];
				snprintf (buf, sizeof (buf), " %g outside %g-%g m, line skipped", v, lo, hi);
				Warn (lineNo, kw + buf);
				continue;
			}
			(skin ? out.skin : out.weld) = v;
		} else if (EqNoCase (kw, "MESH")) {
			// the rest of the line is the mesh name (MeshName rules)
			size_t b = 0;
			while (b < n && IsBlank (s[b])) b++;
			b += 4;
			while (b < n && IsBlank (s[b])) b++;
			size_t e = n;
			while (e > b && IsBlank (s[e-1])) e--;
			if (e <= b) { Warn (lineNo, "MESH without a name, line skipped"); continue; }
			out.mesh.assign (s + b, e - b);
		} else if (EqNoCase (kw, "FOLLOW")) {
			if (tok.size() < 3) { Warn (lineNo, "FOLLOW needs collision groups and one visual group, line skipped"); continue; }
			uint32_t vg;
			if (!ParseU32 (tok.back(), vg)) { Warn (lineNo, "bad visual group '" + tok.back() + "', line skipped"); continue; }
			std::vector<std::pair<uint32_t,uint32_t>> rg;
			uint64_t cnt = 0;
			bool bad = false;
			for (size_t j = 1; j + 1 < tok.size() && !bad; j++) {
				std::pair<uint32_t,uint32_t> r;
				bool sw;
				if (!ParseRange (tok[j], r, sw)) { Warn (lineNo, "bad group index '" + tok[j] + "', line skipped"); bad = true; break; }
				if ((uint64_t)r.second - r.first + 1 > COLL_RANGE_MAX) { Warn (lineNo, "range '" + tok[j] + "' has more than 65536 entries, line skipped"); bad = true; break; }
				if (sw) notes.push_back ("reversed range '" + tok[j] + "' swapped");
				cnt += (uint64_t)r.second - r.first + 1;
				rg.push_back (r);
			}
			if (bad) continue;
			if (nfollow + cnt > COLL_RANGE_MAX) { Warn (lineNo, "FOLLOW lists more than 65536 groups in this file, line skipped"); continue; }
			nfollow += cnt;
			std::vector<uint32_t> cg;
			cg.reserve ((size_t)cnt);
			for (auto &r : rg)
				for (uint64_t g = r.first; g <= r.second; g++) cg.push_back ((uint32_t)g);
			out.follow.push_back (std::make_pair (std::move (cg), vg));
		} else {
			Warn (lineNo, "unknown keyword '" + kw + "', line skipped");
			continue;
		}
		for (const std::string &m : notes) Warn (lineNo, m);
	}
	if (!header) { Warn (0, std::string ("no ") + COLL_SIDECAR_HEADER + " header, file ignored"); out = CollSidecar (); return false; }
	if (!out.follow.empty() && out.mesh.empty()) { Warn (0, "FOLLOW without MESH, ignored"); out.follow.clear (); }
	return true;
}

// mesh tag scanner (D1 4.3): the group rules of operator>> (Src/Orbiter/Mesh.cpp:800-975)

namespace {

// istream::getline (cbuf, 256): fails at the end and on lines over 255 characters, and stays failed
struct MshLines {
	const char *s; size_t n, pos = 0; bool failed = false;
	MshLines (const char *text, size_t len) : s (text), n (text ? len : 0) {}
	bool Get (std::string &line)
	{
		line.clear ();
		if (failed || pos >= n) { failed = true; return false; }
		const char *e = (const char*)memchr (s + pos, '\n', n - pos);
		size_t end = e ? (size_t)(e - s) : n;
		if (end - pos > 255) { failed = true; return false; }
		line.assign (s + pos, end - pos);
		size_t z = line.find ('\0');
		if (z != std::string::npos) line.resize (z);
		pos = e ? end + 1 : n;
		return true;
	}
};

// strncasecmp (s, key, strlen (key)) == 0
bool Prefix (const std::string &s, const char *key)
{
	size_t i = 0;
	for (; key[i]; i++) if (i >= s.size() || Lower (s[i]) != Lower (key[i])) return false;
	return true;
}

// sscanf (s + ofs, "%d", &v): false if no number, v unchanged then
bool ScanInt (const std::string &s, size_t ofs, int32_t &v)
{
	size_t i = ofs;
	while (i < s.size() && IsSpace (s[i])) i++;
	bool neg = false;
	if (i < s.size() && (s[i] == '+' || s[i] == '-')) { neg = (s[i] == '-'); i++; }
	if (i >= s.size() || s[i] < '0' || s[i] > '9') return false;
	int64_t u = 0;
	for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; i++) u = std::min<int64_t> (u * 10 + (s[i] - '0'), (int64_t)1 << 32);
	if (neg) u = -u;
	v = (int32_t)std::max<int64_t> (INT32_MIN, std::min<int64_t> (INT32_MAX, u));
	return true;
}

// sscanf (s + ofs, "%<max>s", buf)
std::string FirstToken (const std::string &s, size_t ofs, size_t max)
{
	size_t i = ofs;
	while (i < s.size() && IsSpace (s[i])) i++;
	size_t b = i;
	while (i < s.size() && !IsSpace (s[i]) && i - b < max) i++;
	return s.substr (std::min (b, s.size()), i - std::min (b, s.size()));
}

} // namespace

bool CollScanMeshTags (const char *text, size_t len, CollMeshTags &out, std::vector<std::string> &warn)
{
	const uint32_t SPEC_DEFAULT = 0xFFFFFFFFu, SPEC_INHERIT = 0xFFFFFFFEu; // Src/Orbiter/Mesh.h:20-21
	out = CollMeshTags ();
	MshLines in (text, len);
	std::string line;
	if (!in.Get (line) || (line != "MSHX1" && line != "MSHX1\r")) { warn.push_back ("mesh tags: no MSHX1 header"); return false; }
	int32_t ngrp = 0;
	for (;;) {
		if (!in.Get (line)) { warn.push_back ("mesh tags: no GROUPS line"); return false; }
		if (Prefix (line, "GROUPS")) {
			if (!ScanInt (line, 6, ngrp)) { warn.push_back ("mesh tags: bad GROUPS line"); return false; }
			break;
		}
	}

	struct Tag { std::string label; uint32_t mtrl, tex; };
	std::vector<Tag> core;
	bool term = false;
	for (int32_t g = 0; g < ngrp && !term; g++) {
		Tag t;
		t.mtrl = SPEC_INHERIT; t.tex = SPEC_INHERIT;
		int32_t nvtx = 0, ntri = 0;
		int64_t nidx = 0;  // 64 bits: upstream's int ntri*3 overflows on a corrupt GEOM line
		for (;;) {
			if (!in.Get (line)) { term = true; break; }
			if (Prefix (line, "MATERIAL")) {
				int32_t v = (int32_t)t.mtrl;
				ScanInt (line, 8, v);
				t.mtrl = (uint32_t)v - 1u;
			} else if (Prefix (line, "TEXTURE")) {
				int32_t v = (int32_t)t.tex;
				ScanInt (line, 7, v);
				t.tex = (uint32_t)v - 1u;
			} else if (Prefix (line, "LABEL")) {
				t.label = FirstToken (line, 5, 250);
			} else if (Prefix (line, "GEOM")) {
				size_t i = 4;
				int32_t a = 0, b = 0;
				if (!ScanInt (line, i, a)) break;
				while (i < line.size() && IsSpace (line[i])) i++;
				if (i < line.size() && (line[i] == '+' || line[i] == '-')) i++;
				while (i < line.size() && line[i] >= '0' && line[i] <= '9') i++;
				if (!ScanInt (line, i, b)) break;  // parse error: group skipped
				nvtx = a; ntri = b; nidx = (int64_t)ntri * 3;
				for (int32_t k = 0; k < nvtx; k++) if (!in.Get (line)) { nvtx = 0; break; }
				for (int32_t k = 0; k < ntri; k++) if (!in.Get (line)) { nvtx = nidx = 0; break; }
				break;
			}
		}
		if (nvtx > 0 && nidx > 0) core.push_back (t); // AddGroup only for groups with geometry (Mesh.cpp:918)
	}

	// names after a failed read stay unset (empty) as in the core, so the loops stop there; counts capped by the lines left
	std::vector<std::string> mname, tname;
	int32_t nmtrl = 0, ntex = 0;
	if (in.Get (line) && line.compare (0, 9, "MATERIALS") == 0 && ScanInt (line, 9, nmtrl)) {
		int64_t n = std::min<int64_t> (nmtrl, (int64_t)(in.n - in.pos));
		for (int64_t i = 0; i < n && in.Get (line); i++) mname.push_back (FirstToken (line, 0, 255));
		for (int64_t i = 0; i < n * 5 && in.Get (line); i++) {}
	}
	if (in.Get (line) && line.compare (0, 8, "TEXTURES") == 0 && ScanInt (line, 8, ntex)) {
		int64_t n = std::min<int64_t> (ntex, (int64_t)(in.n - in.pos));
		for (int64_t i = 0; i < n && in.Get (line); i++) tname.push_back (FirstToken (line, 0, 255));
	}

	// client inherit rule (OVP/VulkanClient/Mesh.cpp:542-563): group 0 inherits "default"
	uint32_t pm = SPEC_DEFAULT, pt = SPEC_DEFAULT;
	for (size_t i = 0; i < core.size(); i++) {
		uint32_t m = core[i].mtrl, t = core[i].tex;
		if (m == SPEC_INHERIT) m = (i ? pm : SPEC_DEFAULT);
		if (t == SPEC_INHERIT) t = (i ? pt : SPEC_DEFAULT);
		pm = m; pt = t;
		out.label.push_back (core[i].label);
		out.material.push_back (m == SPEC_DEFAULT ? std::string ("default") : m < mname.size() ? mname[m] : std::string ());
		out.texture.push_back (t == SPEC_DEFAULT ? std::string ("default") : t < tname.size() ? tname[t] : std::string ());
	}
	return true;
}

// name resolution and group rules (D1 3.2, 4.2, 4.3)

static bool Match (const std::string &name, const std::string &pat)
{
	size_t n = pat.size();
	bool star = n && pat[n-1] == '*';
	if (star) n--;
	if (star ? name.size() < n : name.size() != n) return false;
	for (size_t i = 0; i < n; i++) if (Lower (name[i]) != pat[i]) return false;
	return true;
}

bool CollResolveNames (CollSidecar &sc, const CollMeshTags *tags, uint32_t ngrp, std::vector<std::string> &warn)
{
	bool usable = tags && tags->label.size() == ngrp && tags->material.size() == ngrp && tags->texture.size() == ngrp;
	bool ok = true;
	if (sc.needNames && !usable) {
		warn.push_back (tags ? "mesh tags give " + std::to_string (tags->label.size()) + " groups, the mesh has " + std::to_string (ngrp) + ": name selectors ignored"
		                     : std::string ("no mesh tags: name selectors ignored"));
		ok = false;
	}
	std::vector<CollSideRule> rules;
	for (CollSideRule &r : sc.rule) {
		CollSelector &s = r.sel;
		if (s.kind >= CollSelector::LABEL) {
			if (!usable) continue;
			const std::vector<std::string> &names = s.kind == CollSelector::LABEL ? tags->label : s.kind == CollSelector::MATERIAL ? tags->material : tags->texture;
			std::vector<std::pair<uint32_t,uint32_t>> range;
			for (uint32_t g = 0; g < ngrp; g++) {
				bool hit = false;
				for (const std::string &p : s.pat) if (Match (names[g], p)) { hit = true; break; }
				if (!hit) continue;
				if (!range.empty() && range.back().second + 1 == g) range.back().second = g;
				else range.push_back (std::make_pair (g, g));
			}
			if (range.empty()) warn.push_back ("line " + std::to_string (r.line) + ": no group matches");
			s.kind = CollSelector::GROUP;
			s.range = std::move (range);
			s.pat.clear ();
		} else if (s.kind == CollSelector::GROUP) {
			for (auto &q : s.range)
				if (q.second >= ngrp) { warn.push_back ("line " + std::to_string (r.line) + ": group " + std::to_string (q.second) + " beyond the mesh's " + std::to_string (ngrp) + " groups, the groups in range are kept"); break; }
		}
		rules.push_back (std::move (r));
	}
	sc.rule = std::move (rules);
	sc.needNames = false;
	return ok;
}

void CollGroupRules (const CollSidecar *sc, const CollRestMesh &rm, std::vector<uint8_t> &on, std::vector<uint16_t> &mat)
{
	size_t n = rm.grp.size();
	on.assign (n, 1);
	mat.assign (n, 0);
	for (size_t g = 0; g < n; g++) if (rm.grp[g].usrflag & COLL_FLAG_NORENDER) on[g] = 0;
	if (!sc) return;
	for (const CollSideRule &r : sc->rule) {
		auto Apply = [&] (size_t g) {
			if (r.op == CollSideRule::EXCLUDE) on[g] = 0;
			else if (r.op == CollSideRule::INCLUDE) on[g] = 1;
			else mat[g] = r.mat;
		};
		if (r.sel.kind == CollSelector::ALL) { for (size_t g = 0; g < n; g++) Apply (g); }
		else if (r.sel.kind == CollSelector::GROUP) {
			for (auto &q : r.sel.range)
				for (uint64_t g = q.first; g <= q.second && g < n; g++) Apply ((size_t)g);
		}
	}
}

// vessel collider (D1 3.3-3.6)

static bool SameAffine (const CollAffine &a, const CollAffine &b)
{
	for (int k = 0; k < 9; k++) if (a.A.data[k] != b.A.data[k]) return false;
	return a.t.x == b.t.x && a.t.y == b.t.y && a.t.z == b.t.z;
}

static double Frobenius (const Matrix &A)
{
	double s = 0;
	for (int k = 0; k < 9; k++) s += A.data[k] * A.data[k];
	return sqrt (s);
}

static double Det (const Matrix &A)
{
	return A.m11 * (A.m22*A.m33 - A.m23*A.m32) - A.m12 * (A.m21*A.m33 - A.m23*A.m31) + A.m13 * (A.m21*A.m32 - A.m22*A.m31);
}

static const double SINGULAR_DET = 1e-12;  // |det| of a part matrix at or below this (or NaN): singular, all groups masked (D1 8)
static const uint32_t INDEX_MAX = COLL_RANGE_MAX; // SetGroupHidden and SetObject indices from here on are rejected (no resize wrap)

static uint64_t PartitionHash (const std::vector<uint32_t> &part)
{
	uint64_t h = 0xCBF29CE484222325ull;
	for (uint32_t v : part)
		for (int k = 0; k < 4; k++) { h ^= (v >> (8*k)) & 0xFF; h *= 0x100000001B3ull; }
	return h;
}

// one CollGeom per part, weld scope per (mesh, part) (D1 2)
static std::shared_ptr<const CollMeshTpl> MakeTemplate (const CollRestMesh &geo, const CollSidecar *sc, const CollPartition &map,
	const std::vector<uint8_t> &on, const std::vector<uint16_t> &mat)
{
	auto t = std::make_shared<CollMeshTpl> ();
	t->map = map; t->on = on; t->mat = mat;
	double weld = (sc && sc->weld >= 0) ? sc->weld : COLL_WELD_DEFAULT;
	double skin = (sc && sc->skin >= 0) ? sc->skin : COLL_SKIN_DEFAULT;
	t->part.resize (map.rep.size());
	std::vector<CollSrcGroup> src;
	uint32_t dropIndex = 0, dropInvalid = 0;
	for (uint32_t p = 0; p < map.rep.size(); p++) {
		src.clear ();
		for (uint32_t g = 0; g < map.part.size(); g++)
			if (map.part[g] == p) src.push_back (CollSrcGroup { &geo.grp[g], CollSrc { 0, g, mat[g], 0, p } });
		CollBuildStats st;
		t->part[p].Build (src.data(), src.size(), weld, &st);
		t->part[p].skin = skin;
		dropIndex += st.dropIndex; dropInvalid += st.dropInvalid;
	}
	// one line per mesh (D1 1.2), not per part
	if (dropIndex || dropInvalid)
		CollLog (COLLLOG_WARN, "Collider: mesh '%s': %u triangles with an index out of range, %u with an invalid vertex (non-finite or beyond 1e6 m): dropped",
			geo.name.c_str(), dropIndex, dropInvalid);
	return t;
}

// pose classes of the visual groups: equal signature and equal current matrix (3.3)
void CollShape::Classify (MeshEntry &e, uint32_t mesh, const CollAnim &anim, const ANIMATION *a, uint32_t na, const uint8_t *pr, uint32_t nmesh)
{
	uint32_t nv = e.rest ? (uint32_t)e.rest->grp.size() : 0;
	std::vector<uint64_t> sig, csig;
	anim.Signatures (a, na, mesh, nv, pr, nmesh, sig);
	e.cls.assign (nv, 0);
	e.clsRep.clear ();
	e.clsF.clear ();
	for (uint32_t g = 0; g < nv; g++) {
		CollAffine F;
		anim.GroupTransform (mesh, g, F);
		uint32_t c = 0;
		for (; c < e.clsRep.size(); c++)
			if (csig[c] == sig[g] && SameAffine (e.clsF[c], F)) break;
		if (c == e.clsRep.size()) { e.clsRep.push_back (g); e.clsF.push_back (F); csig.push_back (sig[g]); }
		e.cls[g] = c;
	}
	if (e.coll) {
		CollAffine F;
		anim.GroupTransform (mesh, COLL_STATIC_REP, F);
		e.clsRep.push_back (COLL_STATIC_REP);
		e.clsF.push_back (F);
	}
}

// collider partition of geometry groups (canonical by first appearance) and each part's pose class
bool CollShape::Partition (const MeshEntry &e, CollPartition &map, std::vector<uint8_t> &on, std::vector<uint16_t> &mat, std::vector<uint32_t> &partCls) const
{
	const CollRestMesh *geo = e.coll ? e.coll.get() : e.rest.get();
	map = CollPartition ();
	partCls.clear ();
	if (!geo) { on.clear (); mat.clear (); return false; }
	CollGroupRules (e.side.get(), *geo, on, mat);
	uint32_t n = (uint32_t)geo->grp.size();
	uint32_t staticCls = e.coll ? (uint32_t)e.clsRep.size() - 1 : ~0u;
	map.part.assign (n, ~0u);
	for (uint32_t g = 0; g < n; g++) {
		if (!on[g] || geo->grp[g].idx.size() < 3) continue;
		uint32_t c;
		if (e.coll) {
			uint32_t vg = g < e.follow.size() ? e.follow[g] : ~0u;
			c = vg < e.cls.size() ? e.cls[vg] : staticCls;
		} else c = e.cls[g];
		uint32_t p = 0;
		while (p < partCls.size() && partCls[p] != c) p++;
		if (p == partCls.size()) { partCls.push_back (c); map.rep.push_back (g); }
		map.part[g] = p;
	}
	map.hash = PartitionHash (map.part);
	return !partCls.empty();
}

uint32_t CollShape::Update (const CollMeshInfo *mi, uint32_t nmesh, const CollAnim &anim, const ANIMATION *a, uint32_t na,
	CollTemplateCache &cache)
{
	enum { CARRY, INHERIT, FRESH };
	uint32_t flags = 0;
	std::vector<uint8_t> pr (nmesh);
	for (uint32_t m = 0; m < nmesh; m++) pr[m] = mi[m].present ? 1 : 0;
	bool reclass = anim.Version () != animVersion || pr != present;
	animVersion = anim.Version ();
	present = pr;

	// step 1: slot diff in place; same slot, new partition: remember the old template for 3.3
	std::vector<uint8_t> rebuilt (nmesh, 0), retpl (nmesh, 0);
	std::vector<std::shared_ptr<const CollMeshTpl>> prevTpl (nmesh);
	std::vector<std::vector<uint32_t>> prevPart (nmesh);
	bool restructure = false;
	std::vector<uint8_t> had (nmesh, 0);
	for (uint32_t m = 0; m < nmesh && m < slots.size(); m++) had[m] = slots[m].part.empty() ? 0 : 1;
	for (uint32_t m = nmesh; m < slots.size(); m++) if (!slots[m].part.empty()) restructure = true;
	slots.resize (nmesh);
	if (hidden.size() > nmesh) hidden.resize (nmesh);

	for (uint32_t m = 0; m < nmesh; m++) {
		const CollMeshInfo &in = mi[m];
		MeshEntry &e = slots[m];
		bool same = e.valid && e.collide == in.collide && e.serial == in.serial && e.key == in.key
			&& e.rest == in.rest && e.coll == in.coll && e.side == in.side;
		if (same) e.dofs = in.ofs - e.ofs;
		else {
			if (e.valid && e.serial != in.serial && m < hidden.size()) hidden[m].clear (); // INSMESH: a fresh client mesh has no hidden groups
			if (!e.part.empty()) restructure = true;
			e = MeshEntry ();
			e.valid = true; e.collide = in.collide; e.serial = in.serial; e.key = in.key;
			e.rest = in.rest; e.coll = in.coll; e.side = in.side;
			if (e.coll) {
				e.follow.assign (e.coll->grp.size(), ~0u);
				if (e.side)
					for (auto &f : e.side->follow)
						for (uint32_t cg : f.first) if (cg < e.follow.size()) e.follow[cg] = f.second;
			}
			rebuilt[m] = 1;
		}
		e.ofs = in.ofs;
		if (e.dofs.x != 0 || e.dofs.y != 0 || e.dofs.z != 0) flags |= COLLSH_OFFSET;

		if (!e.collide || !(e.coll ? e.coll : e.rest)) {
			if (!e.part.empty()) restructure = true;
			e.tpl.reset (); e.part.clear (); e.cls.clear (); e.clsRep.clear (); e.clsF.clear (); e.partCls.clear ();
			continue;
		}
		if (!rebuilt[m] && !reclass) continue;

		Classify (e, m, anim, a, na, pr.data(), nmesh);
		CollPartition map;
		std::vector<uint8_t> on;
		std::vector<uint16_t> mat;
		Partition (e, map, on, mat, e.partCls);
		if (!rebuilt[m] && e.tpl && e.tpl->map.part == map.part) continue; // same parts, classes renumbered only

		std::shared_ptr<const CollMeshTpl> tpl;
		std::string full;
		if (!e.key.empty()) {
			char h[24];
			snprintf (h, sizeof (h), "#%016llx", (unsigned long long)map.hash);
			full = e.key + (e.coll ? "|M:" + e.coll->name : std::string ()) + h;
			auto it = cache.tpl.find (full);
			if (it != cache.tpl.end()) tpl = it->second.lock ();
			if (tpl && tpl->map.part != map.part) { tpl.reset (); full.clear (); } // hash collision: private template
		}
		if (!tpl) {
			tpl = MakeTemplate (e.coll ? *e.coll : *e.rest, e.side.get(), map, on, mat);
			if (!full.empty()) {
				if (cache.tpl.size() > 256)
					for (auto it = cache.tpl.begin(); it != cache.tpl.end(); ) it = it->second.expired () ? cache.tpl.erase (it) : std::next (it);
				cache.tpl[full] = tpl;
			}
		}
		prevTpl[m] = std::move (e.tpl);
		prevPart[m].swap (e.part);
		e.tpl = tpl;
		e.part.clear ();
		retpl[m] = 1;
		restructure = true;
	}

	// parts in slot, then template part order; unchanged slots keep parts (versions, poses, copies)
	std::vector<uint8_t> mode;
	std::vector<CollAffine> inh;
	if (restructure) {
		std::vector<CollPart> np;
		bool anyNew = false;
		for (uint32_t m = 0; m < nmesh; m++) {
			MeshEntry &e = slots[m];
			if (!e.tpl) continue;
			if (!retpl[m]) {
				for (uint32_t &ip : e.part) {
					if (ip == ~0u) continue;
					np.push_back (std::move (parts[ip]));
					mode.push_back (CARRY); inh.push_back (CollAffine ());
					ip = (uint32_t)np.size() - 1;
				}
				continue;
			}
			e.part.assign (e.tpl->part.size(), ~0u);
			for (uint32_t p = 0; p < e.tpl->part.size(); p++) {
				if (e.tpl->part[p].tri.empty()) continue;
				CollPart P;
				P.mesh = m;
				P.rep = e.tpl->map.rep[p];
				P.tpl = std::shared_ptr<const CollGeom> (e.tpl, &e.tpl->part[p]);
				P.version = ++partSerial;
				P.motion = 0;
				uint8_t md = FRESH;
				CollAffine prev;
				// partition change: the old part that held the representative group gives anim[0] (3.3)
				const CollMeshTpl *ot = prevTpl[m].get();
				if (!rebuilt[m] && ot) {
					uint32_t op = P.rep < ot->map.part.size() ? ot->map.part[P.rep] : ~0u;
					if (op != ~0u && op < prevPart[m].size() && prevPart[m][op] < parts.size()) { md = INHERIT; prev = parts[prevPart[m][op]].anim[1]; }
				}
				np.push_back (std::move (P));
				mode.push_back (md); inh.push_back (prev);
				e.part[p] = (uint32_t)np.size() - 1;
				anyNew = true;
			}
			if (rebuilt[m]) flags |= everBuilt ? COLLSH_REBUILT : COLLSH_BUILT;
			else flags |= COLLSH_PARTITION;
		}
		if (!anyNew && everBuilt) flags |= COLLSH_REBUILT; // only drops
		parts.swap (np);
	}
	if (!parts.empty()) everBuilt = true;
	replaced.assign (nmesh, 0);
	for (uint32_t m = 0; m < nmesh; m++) replaced[m] = ((rebuilt[m] || retpl[m]) && (had[m] || !slots[m].part.empty())) ? 1 : 0;

	// step 3: current matrices of every pose class; anim[0] = previous anim[1]
	for (uint32_t m = 0; m < nmesh; m++) {
		MeshEntry &e = slots[m];
		if (!e.tpl) continue;
		for (uint32_t c = 0; c < e.clsRep.size(); c++) anim.GroupTransform (m, e.clsRep[c], e.clsF[c]);
	}
	bool jump = jumpPending;
	jumpPending = false;
	if (jump) flags |= COLLSH_JUMP;
	bound[0] = bound[1] = 0;
	std::vector<uint8_t> sing (parts.size(), 0);
	for (uint32_t i = 0; i < parts.size(); i++) {
		CollPart &P = parts[i];
		const MeshEntry &e = slots[P.mesh];
		const CollAffine &F = e.clsF[e.partCls[e.tpl->map.part[P.rep]]];
		uint8_t md = i < mode.size() ? mode[i] : (uint8_t)CARRY;
		if (md == CARRY) P.anim[0] = P.anim[1];
		else if (md == INHERIT) P.anim[0] = inh[i];
		else P.anim[0] = F;
		P.anim[1] = F;
		if (jump) P.anim[0] = P.anim[1];  // step 5
		// step 4: both poses on the current offset
		CollAffine T = CollTranslate (e.ofs);
		const CollGeom &G = P.Geom ();
		for (int k = 0; k < 2; k++) {
			P.pose[k] = CollCompose (T, P.anim[k]);
			P.sc[k] = CollApply (P.pose[k], G.bsCentre);
			P.sr[k] = G.bsRadius * (CollIsRigid (P.pose[k]) ? 1.0 : Frobenius (P.pose[k].A));
			bound[k] = std::max (bound[k], P.sc[k].length () + P.sr[k]);
		}
		P.motion = SameAffine (P.pose[0], P.pose[1]) ? 0.0 : CollPoseMotion (G, P.pose[0], P.pose[1]);
		if (P.motion > 0) flags |= COLLSH_MOVED;
		// scale to zero: like the client, never scales back; out of queries until its mesh is re-inserted
		if (!(fabs (Det (P.anim[0].A)) > SINGULAR_DET) || !(fabs (Det (P.anim[1].A)) > SINGULAR_DET)) {
			sing[i] = 1;
			if (!loggedSingular) { loggedSingular = true; CollLog (COLLLOG_WARN, "Collider: mesh %u group %u has a singular animation matrix (scaled to zero), left out of collisions", P.mesh, P.rep); }
		}
	}
	if (restructure || sing != singular) {
		singular.swap (sing);
		UpdateMasks ();
	}
	if (parts.empty()) {
		flags |= COLLSH_NONE;
		if (!loggedNone) { loggedNone = true; CollLog (COLLLOG_INFO, "Collider: vessel has no collision geometry"); }
	}
	return flags;
}

bool CollShape::Replaced (uint32_t mesh) const
{
	return mesh < replaced.size() && replaced[mesh];
}

void CollShape::MarkJump ()
{
	jumpPending = true;
}

const Vector &CollShape::OffsetChange (uint32_t mesh) const
{
	return mesh < slots.size() ? slots[mesh].dofs : zero;
}

uint32_t CollShape::nPart () const
{
	return (uint32_t)parts.size();
}

const CollPart &CollShape::Part (uint32_t i) const
{
	return parts[i];
}

void CollShape::Bound (int slot, Vector &c, double &r) const
{
	c = Vector ();
	r = bound[slot ? 1 : 0];
}

int CollShape::PartOfTpl (const MeshEntry &e, uint32_t grp) const
{
	if (!e.tpl || grp >= e.tpl->map.part.size()) return -1;
	uint32_t tp = e.tpl->map.part[grp];
	if (tp == ~0u || tp >= e.part.size() || e.part[tp] == ~0u) return -1;
	return (int)e.part[tp];
}

int CollShape::PartOf (uint32_t mesh, uint32_t grp) const
{
	return mesh < slots.size() ? PartOfTpl (slots[mesh], grp) : -1;
}

void CollShape::SetGroupHidden (uint32_t mesh, uint32_t grp, bool hide)
{
	if (mesh >= INDEX_MAX || grp >= INDEX_MAX) return;
	if (mesh >= hidden.size()) {
		if (!hide) return;
		hidden.resize (mesh + 1);
	}
	if (grp >= hidden[mesh].size()) {
		if (!hide) return;
		hidden[mesh].resize (grp + 1, 0);
	}
	hidden[mesh][grp] = hide ? 1 : 0;
	UpdateMasks ();
}

void CollShape::UpdateMasks ()
{
	masks.assign (parts.size(), std::vector<uint8_t> ());
	for (uint32_t i = 0; i < parts.size(); i++) {
		const CollPart &P = parts[i];
		const CollGeom &G = P.Geom ();
		if (i < singular.size() && singular[i]) { masks[i].assign (G.srcTab.size(), 1); continue; }
		if (P.mesh >= hidden.size()) continue;
		const std::vector<uint8_t> &h = hidden[P.mesh];
		std::vector<uint8_t> mk (G.srcTab.size(), 0);
		bool any = false;
		for (size_t k = 0; k < G.srcTab.size(); k++) {
			uint32_t g = G.srcTab[k].grp;
			if (g < h.size() && h[g]) { mk[k] = 1; any = true; }
		}
		if (any) masks[i].swap (mk);
	}
}

const uint8_t *CollShape::GroupMask (uint32_t part) const
{
	return (part < masks.size() && !masks[part].empty()) ? masks[part].data() : nullptr;
}

// damage API (D1 7.3)

CollGeom &CollShape::MakePrivate (uint32_t i)
{
	CollPart &P = parts[i];
	if (!P.own) {
		P.own.reset (new CollGeom (*P.tpl));
		P.own->KeepRest ();
	}
	return *P.own;
}

bool CollShape::RenderFeature (uint32_t i, uint32_t tri, uint32_t &mesh, uint32_t &grp, uint32_t &otri) const
{
	if (i >= parts.size()) return false;
	const CollPart &P = parts[i];
	const CollGeom &G = P.Geom ();
	if (tri >= G.tri.size()) return false;
	const CollTri &t = G.tri[tri];
	uint32_t g = G.srcTab[t.src].grp;
	const MeshEntry &e = slots[P.mesh];
	mesh = P.mesh;
	if (e.coll) { grp = g < e.follow.size() ? e.follow[g] : ~0u; otri = ~0u; } // MESH replacement (R16)
	else { grp = g; otri = t.otri; }
	return true;
}

bool CollShape::GroupPose (uint32_t mesh, uint32_t grp, CollAffine &F) const
{
	if (mesh >= slots.size() || grp >= slots[mesh].cls.size()) { F = CollAffine (); return false; }
	const MeshEntry &e = slots[mesh];
	F = e.clsF[e.cls[grp]];
	return true;
}

uint32_t CollShape::PoseGroup (uint32_t i) const
{
	if (i >= parts.size()) return COLL_STATIC_REP;
	const CollPart &P = parts[i];
	const MeshEntry &e = slots[P.mesh];
	return e.clsRep[e.partCls[e.tpl->map.part[P.rep]]];
}

bool CollShape::CollMesh (uint32_t mesh) const
{
	return mesh < slots.size() && slots[mesh].coll != nullptr;
}

double CollShape::PartRadius (uint32_t mesh, uint32_t grp) const
{
	int p = PartOf (mesh, grp);
	return p < 0 ? -1.0 : parts[p].Geom ().bsRadius;
}

uint16_t CollShape::Material (uint32_t mesh, uint32_t grp) const
{
	if (mesh >= slots.size() || !slots[mesh].tpl) return 0;
	const CollMeshTpl &t = *slots[mesh].tpl;
	return grp < t.mat.size() ? t.mat[grp] : 0;
}

bool CollShape::RayRest (uint32_t mesh, uint32_t grp, const Vector &o, const Vector &d,
	double tmin, double tmax, CollRayHit &hit) const
{
	CollAffine P;
	int ip = PartOf (mesh, grp);
	if (ip >= 0) P = parts[ip].pose[1];
	else {
		CollAffine F;
		if (!GroupPose (mesh, grp, F)) return false;
		P = CollCompose (CollTranslate (slots[mesh].ofs), F);
	}
	if (!(fabs (Det (P.A)) > 1e-12)) return false;
	CollAffine Pinv = CollInverse (P);
	bool any = false;
	CollRayHit best;
	best.tri = ~0u; best.t = tmax;
	for (uint32_t i = 0; i < parts.size(); i++) {
		CollRayHit h;
		if (CollRayCast (parts[i].Geom (), CollCompose (Pinv, parts[i].pose[1]), o, d, tmin, any ? best.t : tmax, h, GroupMask (i)))
			if (!any || h.t < best.t) { best = h; any = true; }
	}
	if (any) hit = best;
	return any;
}

size_t CollShape::ApplyDent (uint32_t mesh, const uint32_t *grp, size_t ngrp, CollDisplaceFn fn, const void *ctx)
{
	if (!fn) return 0;
	return ApplyAny (mesh, grp, ngrp, fn, nullptr, ctx);
}

size_t CollShape::ApplyMap (uint32_t mesh, const uint32_t *grp, size_t ngrp, CollMapFn fn, const void *ctx)
{
	if (!fn) return 0;
	return ApplyAny (mesh, grp, ngrp, nullptr, fn, ctx);
}

size_t CollShape::ApplyAny (uint32_t mesh, const uint32_t *grp, size_t ngrp, CollDisplaceFn fn, CollMapFn mfn, const void *ctx)
{
	if (mesh >= slots.size() || (!fn && !mfn)) return 0;
	bool all = (ngrp == 0 || !grp || slots[mesh].coll); // MESH replacement: by position over all groups (R16)
	size_t moved = 0;
	std::vector<uint8_t> sel;
	std::vector<std::pair<uint32_t, Vector>> dv;
	for (uint32_t i = 0; i < parts.size(); i++) {
		if (parts[i].mesh != mesh) continue;
		const CollGeom &G = parts[i].Geom ();
		sel.assign (G.srcTab.size(), all ? 1 : 0);
		if (!all)
			for (size_t k = 0; k < G.srcTab.size(); k++)
				for (size_t j = 0; j < ngrp; j++) if (grp[j] == G.srcTab[k].grp) { sel[k] = 1; break; }
		dv.clear ();
		for (uint32_t v = 0; v < G.vtx.size(); v++) {
			bool listed = false;
			for (uint32_t r = G.refOfs[v]; r < G.refOfs[v+1] && !listed; r++) listed = G.ref[r].src < sel.size() && sel[G.ref[r].src];
			if (!listed) continue;
			Vector x = fn ? fn (ctx, G.RestPos (v)) : mfn (ctx, G.RestPos (v), G.Pos (v)) - G.Pos (v);  // field on rest positions (7.3)
			Vector p = G.Pos (v) + x;
			if (!std::isfinite (p.x) || !std::isfinite (p.y) || !std::isfinite (p.z)) {
				if (!loggedBadDent) { loggedBadDent = true; CollLog (COLLLOG_WARN, "Collider: dent field of mesh %u gives a non-finite position, vertex left in place", mesh); }
				continue;
			}
			if (x.x != 0 || x.y != 0 || x.z != 0) dv.push_back (std::make_pair (v, x));
		}
		if (dv.empty()) continue;
		CollGeom &W = MakePrivate (i);
		for (auto &q : dv) W.vtx[q.first] += q.second;
		W.Refit ();  // also the bounding radius
		moved += dv.size();
		CollPart &P = parts[i];
		for (int k = 0; k < 2; k++) {  // part spheres as Update step 4
			P.sc[k] = CollApply (P.pose[k], W.bsCentre);
			P.sr[k] = W.bsRadius * (CollIsRigid (P.pose[k]) ? 1.0 : Frobenius (P.pose[k].A));
		}
		P.motion = SameAffine (P.pose[0], P.pose[1]) ? 0.0 : CollPoseMotion (W, P.pose[0], P.pose[1]);
	}
	if (moved) {
		bound[0] = bound[1] = 0;
		for (const CollPart &P : parts)
			for (int k = 0; k < 2; k++) bound[k] = std::max (bound[k], P.sc[k].length () + P.sr[k]);
	}
	return moved;
}

void CollShape::ResetDents (uint32_t mesh)
{
	for (CollPart &P : parts) if (P.mesh == mesh) P.own.reset ();
}

// building collider (D1 5.3-5.5)

bool CollBaseShape::MoveObject (CollBaseObj &o, const CollGroupData *grp, size_t ngrp)
{
	CollGeom &G = o.geom;
	if (G.refOfs.size() != G.vtx.size() + 1) return false;
	bool moved = false;
	for (uint32_t v = 0; v < G.vtx.size(); v++) {
		if (G.refOfs[v] >= G.refOfs[v+1] || G.refOfs[v] >= G.ref.size()) return false;
		const CollRef &r = G.ref[G.refOfs[v]];  // the representative render vertex (D1 2)
		if (r.src >= G.srcTab.size()) return false;
		uint32_t s = G.srcTab[r.src].grp;
		if (s >= ngrp || r.vtx >= grp[s].vtx.size()) return false;
		const CollVtx &c = grp[s].vtx[r.vtx];
		Vector p (c.x, c.y, c.z);
		if (!std::isfinite (p.x) || !std::isfinite (p.y) || !std::isfinite (p.z)) {
			if (!loggedBad) { loggedBad = true; CollLog (COLLLOG_WARN, "Collider: base object %u has a non-finite vertex, left in place", o.obj); }
			continue;
		}
		if (p.x != G.vtx[v].x || p.y != G.vtx[v].y || p.z != G.vtx[v].z) { G.vtx[v] = p; moved = true; }
	}
	if (moved) G.Refit ();
	return true;
}

bool CollBaseShape::SetObject (uint32_t i, uint32_t obj, const CollGroupData *grp, size_t ngrp, uint16_t mat, bool topoChanged)
{
	if (i >= INDEX_MAX) return false;
	if (i >= objs.size()) objs.resize (i + 1);
	CollBaseObj &o = objs[i];
	bool fresh = o.geom.tri.empty() || o.obj != obj;
	o.obj = obj;
	o.mat = mat;
	if (!topoChanged && !fresh && MoveObject (o, grp, ngrp)) {
		for (CollSrc &s : o.geom.srcTab) s.mat = mat;
		return true;
	}
	if (!topoChanged && !fresh) CollLog (COLLLOG_WARN, "Collider: base object %u changed topology without notice, rebuilt", obj);
	std::vector<CollSrcGroup> src;
	for (size_t g = 0; g < ngrp; g++) {
		if (grp[g].undersh || grp[g].idx.size() < 3) continue; // R25
		src.push_back (CollSrcGroup { &grp[g], CollSrc { obj, (uint32_t)g, mat, 0, obj } });
	}
	CollBuildStats st;
	bool ok = o.geom.Build (src.data(), src.size(), COLL_WELD_DEFAULT, &st);
	if (st.dropIndex || st.dropInvalid)
		CollLog (COLLLOG_WARN, "Collider: base object %u: %u triangles with an index out of range, %u with an invalid vertex (non-finite or beyond 1e6 m): dropped",
			obj, st.dropIndex, st.dropInvalid);
	built = true;
	return ok;
}

void CollBaseShape::Follow (uint32_t i, uint32_t geomVersion, uint32_t topo)
{
	if (i >= objs.size()) return;
	objs[i].geomVersion = geomVersion;
	objs[i].topo = topo;
}

void CollBaseShape::Finish (const Vector &rpos, const Matrix &rrot, double rPlanet)
{
	rmax = 0;
	double h = 0;
	bool any = false;
	std::vector<uint8_t> seen;
	for (const CollBaseObj &o : objs) {
		seen.assign (o.geom.vtx.size(), 0);
		for (const CollTri &t : o.geom.tri) for (int j = 0; j < 3; j++) seen[t.v[j]] = 1;
		for (size_t v = 0; v < o.geom.vtx.size(); v++) {
			if (!seen[v]) continue;  // collider points only
			const Vector &p = o.geom.vtx[v];
			double r = p.length ();
			double hp = (rpos + mul (rrot, p)).length () - rPlanet; // exact radius per vertex (5.5)
			if (!any || hp > h) h = hp;
			if (r > rmax) rmax = r;
			any = true;
		}
	}
	htop = any ? h : rpos.length () - rPlanet;
	if (built) version++;
	built = false;
}

uint32_t CollBaseShape::nObj () const
{
	return (uint32_t)objs.size();
}

const CollBaseObj &CollBaseShape::Obj (uint32_t i) const
{
	return objs[i];
}

bool CollBaseShape::RenderFeature (uint32_t i, uint32_t tri, uint32_t &obj, uint32_t &grp, uint32_t &otri) const
{
	if (i >= objs.size() || tri >= objs[i].geom.tri.size()) return false;
	const CollBaseObj &o = objs[i];
	const CollTri &t = o.geom.tri[tri];
	obj = o.obj;
	grp = o.geom.srcTab[t.src].grp;
	otri = t.otri;
	return true;
}

uint16_t CollBaseShape::Material (uint32_t i) const
{
	return i < objs.size() ? objs[i].mat : 0;
}
