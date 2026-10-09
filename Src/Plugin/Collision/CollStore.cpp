// not upstream: collision addon, E3 persistence: keys, plugin block text, matching, recorder side file (Design CA E3 7, 8)
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "CollStore.h"

namespace {

char Lower (char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

std::vector<std::string> Tokens (const std::string &s)
{
	std::vector<std::string> t;
	size_t i = 0, n = s.size ();
	while (i < n) {
		while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
		size_t b = i;
		while (i < n && s[i] != ' ' && s[i] != '\t') i++;
		if (i > b) t.push_back (s.substr (b, i - b));
	}
	return t;
}

std::string First (const std::string &s)
{
	std::vector<std::string> t = Tokens (s);
	return t.empty () ? std::string () : t[0];
}

std::string Trim (const std::string &s)
{
	size_t b = 0, e = s.size ();
	while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) b++;
	while (e > b && (s[e-1] == ' ' || s[e-1] == '\t' || s[e-1] == '\r')) e--;
	return s.substr (b, e - b);
}

template <class T> bool Num (const std::string &s, T &v, int base = 10)
{
	auto r = std::from_chars (s.data (), s.data () + s.size (), v, base);
	return r.ec == std::errc () && r.ptr == s.data () + s.size ();
}

bool Real (const std::string &s, double &v)
{
	auto r = std::from_chars (s.data (), s.data () + s.size (), v);
	return r.ec == std::errc () && r.ptr == s.data () + s.size () && std::isfinite (v);
}

size_t TokStart (const std::string &s, int k) // index of token k
{
	size_t i = 0, n = s.size ();
	for (int j = 0;; j++) {
		while (i < n && (s[i] == ' ' || s[i] == '\t')) i++;
		if (j == k || i >= n) return i;
		while (i < n && s[i] != ' ' && s[i] != '\t') i++;
	}
}

bool Kw (const std::string &t, const char *k) { return CollKey::IEqual (t, k); }

bool IsHeader (const std::string &l, int &ver)
{
	std::vector<std::string> t = Tokens (l);
	return t.size () >= 2 && Kw (t[0], "COLLA") && Num (t[1], ver);
}

}

// keys (7.3)

std::string CollKey::Escape (const std::string &s)
{
	if (s.empty ()) return "%-";
	std::string o;
	for (unsigned char c : s) {
		if (c <= 0x20 || c == 0x7F || c == '%' || c == ';') {
			char b[4];
			std::snprintf (b, sizeof b, "%%%02X", c);
			o += b;
		} else o += (char)c;
	}
	return o;
}

bool CollKey::Unescape (const std::string &s, std::string &out)
{
	out.clear ();
	if (s == "%-") return true;
	for (size_t i = 0; i < s.size (); i++) {
		if (s[i] != '%') { out += s[i]; continue; }
		unsigned v = 0;
		if (i + 2 >= s.size () || !Num (s.substr (i + 1, 2), v, 16)) return false;
		out += (char)v;
		i += 2;
	}
	return true;
}

std::string CollKey::Hex8 (uint32_t h)
{
	char b[12];
	std::snprintf (b, sizeof b, "%08x", h);
	return b;
}

bool CollKey::IEqual (const std::string &a, const std::string &b)
{
	if (a.size () != b.size ()) return false;
	for (size_t i = 0; i < a.size (); i++) if (Lower (a[i]) != Lower (b[i])) return false;
	return true;
}

uint32_t CollKey::Hash (const std::string &s) { return DentMath::Fnv1a (s.data (), s.size ()); }

std::string CollKey::VesselLine (const char *kw, uint32_t occ, const std::string &name, const std::string &cls, size_t indent)
{
	std::string l = std::string (kw) + " " + std::to_string (occ) + " " + Escape (name) + " " + Escape (cls);
	if (indent + l.size () <= (size_t)DENT_LINE_MAX) return l;
	return std::string (kw) + "H " + std::to_string (occ) + " " + Hex8 (Hash (name)) + " " + Hex8 (Hash (cls));
}

bool CollKey::ParseVesselLine (const std::string &line, const char *kw, uint32_t &occ, std::string &name, std::string &cls, bool &hashed, uint32_t &hName, uint32_t &hClass, size_t first)
{
	std::vector<std::string> t = Tokens (line);
	if (t.size () < first + 4) return false;
	std::string kh = std::string (kw) + "H";
	hashed = Kw (t[first], kh.c_str ());
	if (!hashed && !Kw (t[first], kw)) return false;
	if (!Num (t[first + 1], occ)) return false;
	if (hashed) {
		name.clear (), cls.clear ();
		return t[first + 2].size () <= 8 && t[first + 3].size () <= 8 && Num (t[first + 2], hName, 16) && Num (t[first + 3], hClass, 16);
	}
	if (!Unescape (t[first + 2], name) || !Unescape (t[first + 3], cls)) return false;
	hName = Hash (name), hClass = Hash (cls);
	return true;
}

// block (7.1-7.5, 7.8)

bool CollStore::Line200 (const std::string &l) { return l.size () <= (size_t)DENT_LINE_MAX && l.find (';') == std::string::npos; }

bool CollStore::Parse (const LineIn &in, CollStoreBlock &out)
{
	out = CollStoreBlock ();
	std::string l;
	if (!in (l)) return false;
	int ver = 0;
	if (!IsHeader (l, ver)) { // a foreign block found by prefix: scan on to our own BEGIN line
		int misses = 0;
		for (;;) {
			if (!in (l)) { if (++misses > 256) return false; continue; } // END of another block, or end of file
			misses = 0;
			if (CollKey::IEqual (Trim (l), "BEGIN_Collision")) break;
		}
		if (!in (l) || !IsHeader (l, ver)) return false;
	}
	std::vector<std::string> body;
	while (in (l)) body.push_back (l);
	ParseBody (body, out);
	out.found = true;
	out.version = ver;
	return true;
}

void CollStore::ParseBody (const std::vector<std::string> &lines, CollStoreBlock &out)
{
	for (size_t i = 0; i < lines.size (); i++) {
		std::string l = Trim (lines[i]);
		std::vector<std::string> t = Tokens (l);
		if (t.empty ()) continue;
		if (Kw (t[0], "COLLA")) continue;
		if (Kw (t[0], "RECID")) {
			double t0;
			if (t.size () >= 3 && Real (t[2], t0)) out.recId = t[1], out.recT0 = t0;
			else out.skipped++;
			continue;
		}
		if (Kw (t[0], "VESSEL") || Kw (t[0], "VESSELH")) {
			CollStoreVessel v;
			bool ok = CollKey::ParseVesselLine (l, "VESSEL", v.occ, v.name, v.cls, v.hashed, v.hName, v.hClass);
			v.raw.push_back (l);
			DentVesselParser p;
			for (i++; i < lines.size (); i++) {
				std::string s = Trim (lines[i]);
				v.raw.push_back (s);
				if (Kw (First (s), "END_VESSEL")) break;
				if (!p.Line (s.c_str ())) v.skipped++;
			}
			if (v.raw.size () < 2 || !Kw (First (v.raw.back ()), "END_VESSEL")) v.raw.push_back ("END_VESSEL");
			p.Finish (v.d);
			v.skipped += p.Skipped ();
			if (p.Capped ()) { // a dormant write-back keeps the capped text, not every line read
				v.raw.resize (1);
				std::vector<std::string> f;
				DentMath::FormatVessel (v.d, "  ", f); // the save indent, so every line still fits once written back
				for (const std::string &x : f) v.raw.push_back (x.compare (0, 2, "  ") == 0 ? x.substr (2) : x);
				v.raw.push_back ("END_VESSEL");
			}
			if (ok) out.vessel.push_back (std::move (v));
			else out.skipped++;
			continue;
		}
		if (Kw (t[0], "BEGIN_XDMG_BASES")) {
			DentBasesParser p;
			for (i++; i < lines.size (); i++) if (!p.Line (Trim (lines[i]).c_str ())) break;
			std::vector<DentBaseText> b;
			p.Finish (b);
			out.skipped += p.Skipped ();
			out.base.insert (out.base.end (), b.begin (), b.end ());
			continue;
		}
		if (Kw (t[0], "TESTREPAIR")) {
			CollTestRepair r;
			if (t.size () >= 4 && Real (t[1], r.simt) && Num (t[2], r.occ) && CollKey::Unescape (t[3], r.name)) out.testRepair.push_back (r);
			else out.skipped++;
			continue;
		}
		if (Line200 (l)) out.unknown.push_back (l);
		else out.skipped++;
	}
}

uint32_t CollStore::Occ (const std::vector<CollLiveVessel> &live, size_t i, bool icase)
{
	uint32_t k = 0;
	for (size_t j = 0; j < i && j < live.size (); j++) {
		bool eq = icase ? (CollKey::IEqual (live[j].name, live[i].name) && CollKey::IEqual (live[j].cls, live[i].cls))
			: (live[j].name == live[i].name && live[j].cls == live[i].cls);
		if (eq) k++;
	}
	return k;
}

std::vector<int> CollStore::MatchVessels (const std::vector<CollStoreVessel> &saved, const std::vector<CollLiveVessel> &live)
{
	std::vector<int> m (saved.size (), -1);
	std::vector<uint8_t> taken (live.size (), 0);
	for (int pass = 0; pass < 2; pass++) // exact bytes first, then ASCII case-insensitive
		for (size_t s = 0; s < saved.size (); s++) {
			if (m[s] >= 0) continue;
			const CollStoreVessel &v = saved[s];
			if (v.hashed && pass) continue;
			for (size_t i = 0; i < live.size (); i++) {
				if (taken[i]) continue;
				bool eq;
				if (v.hashed) eq = CollKey::Hash (live[i].name) == v.hName && CollKey::Hash (live[i].cls) == v.hClass && Occ (live, i, false) == v.occ;
				else if (!pass) eq = live[i].name == v.name && live[i].cls == v.cls && Occ (live, i, false) == v.occ;
				else eq = CollKey::IEqual (live[i].name, v.name) && CollKey::IEqual (live[i].cls, v.cls) && Occ (live, i, true) == v.occ;
				if (eq) { m[s] = (int)i; taken[i] = 1; break; }
			}
		}
	return m;
}

int CollStore::MatchObj (const DentBaseObjText &o, const std::vector<CollLiveObj> &objs, const std::vector<uint8_t> &taken)
{
	auto near = [&] (const CollLiveObj &l) { double dx = l.x - o.x, dz = l.z - o.z; return std::sqrt (dx * dx + dz * dz) < 0.5; };
	for (size_t i = 0; i < objs.size (); i++)
		if (!taken[i] && objs[i].obj == o.index && CollKey::IEqual (objs[i].type, o.type) && near (objs[i])) return (int)i;
	for (size_t i = 0; i < objs.size (); i++)
		if (!taken[i] && CollKey::IEqual (objs[i].type, o.type) && near (objs[i])) return (int)i;
	return -1;
}

bool CollStore::SameBase (const DentBaseText &b, const std::string &planet, const std::string &base)
{
	if (!CollKey::IEqual (b.planet, planet)) return false;
	if (b.name.empty () && b.nameHash) return CollKey::Hash (base) == b.nameHash;
	return CollKey::IEqual (b.name, base);
}

// side file (8.3)

std::string CollSide::Fmt17 (double v)
{
	char b[40];
	std::snprintf (b, sizeof b, "%.17g", v);
	return b;
}

std::string CollSide::Header (const std::string &id) { return "COLLAREC 1 " + id; }

std::string CollSide::Vdef (uint32_t alias, uint32_t occ, const std::string &name, const std::string &cls)
{
	std::string l = "VDEF " + std::to_string (alias) + " " + std::to_string (occ) + " " + CollKey::Escape (name) + " " + CollKey::Escape (cls);
	if (l.size () < 256) return l;
	return "VDEFH " + std::to_string (alias) + " " + std::to_string (occ) + " " + CollKey::Hex8 (CollKey::Hash (name)) + " " + CollKey::Hex8 (CollKey::Hash (cls));
}

void CollSide::Dent (double t, uint32_t alias, uint32_t recidx, const DentRecord &r, std::vector<std::string> &lines)
{
	std::vector<std::string> pay;
	DentMath::FormatDentEvent (r, pay);
	for (const std::string &p : pay) lines.push_back (Fmt17 (t) + " D " + std::to_string (alias) + " " + std::to_string (recidx) + " " + p);
}

std::string CollSide::State (double t, uint32_t alias, double eabs, uint32_t flags)
{
	return Fmt17 (t) + " S " + std::to_string (alias) + " " + DentMath::FormatStateEvent (eabs, flags);
}

std::string CollSide::Repair (double t, uint32_t alias) { return Fmt17 (t) + " R " + std::to_string (alias); }

std::string CollSide::Ext (double t, uint32_t alias, uint32_t recidx, const DentParams &p, double E, double vn, double vt)
{
	return Fmt17 (t) + " X " + std::to_string (alias) + " " + DentMath::FormatExtEvent (recidx, p, E, vn, vt);
}

void CollSide::Torn (double t, uint32_t alias, const DentTorn &tr, std::vector<std::string> &lines)
{
	std::vector<std::string> pay;
	DentMath::FormatTornEvent (tr, pay);
	for (const std::string &p : pay) lines.push_back (Fmt17 (t) + " T " + std::to_string (alias) + " " + p);
}

std::string CollSide::Building (double t, uint32_t alias, uint32_t obj, double eabs, uint32_t flags, const std::string &planetBase)
{
	return Fmt17 (t) + " B " + std::to_string (alias) + " " + std::to_string (obj) + " " + DentMath::FormatStateEvent (eabs, flags) + " " + CollKey::Escape (planetBase);
}

bool CollSide::Parse (const std::string &text, CollSideFile &out)
{
	out = CollSideFile ();
	size_t pos = 0;
	bool head = false, open = false, over = false; // over: the open D event's group list passed DENT_MAX_GRPLIST, dropped at its end
	bool topen = false;                            // dmg3: the last T event's group list continues
	while (pos < text.size ()) {
		size_t e = text.find ('\n', pos);
		if (e == std::string::npos) { out.skipped++; break; } // truncated last line
		std::string l = Trim (text.substr (pos, e - pos));
		pos = e + 1;
		std::vector<std::string> t = Tokens (l);
		if (t.empty ()) continue;
		if (!head) {
			if (t.size () < 3 || !Kw (t[0], "COLLAREC")) return false;
			out.id = t[2], head = true;
			continue;
		}
		if (Kw (t[0], "VDEF") || Kw (t[0], "VDEFH")) {
			CollSideAlias a;
			if (t.size () >= 5 && Num (t[1], a.alias) && CollKey::ParseVesselLine (t[0] + " " + t[2] + " " + t[3] + " " + t[4], "VDEF", a.occ, a.name, a.cls, a.hashed, a.hName, a.hClass))
				out.alias.push_back (a);
			else out.skipped++;
			continue;
		}
		CollSideEvent ev;
		if (t.size () < 3 || !Real (t[0], ev.t) || t[1].size () != 1 || !Num (t[2], ev.alias)) { out.skipped++; continue; }
		ev.kind = t[1][0];
		bool ok = false;
		if (ev.kind == 'D' && t.size () >= 5 && Num (t[3], ev.recidx)) {
			bool more = false;
			ok = DentMath::ParseDentEvent (l.c_str () + TokStart (l, 4), ev.rec, more);
			if (ok && open && !out.ev.empty () && out.ev.back ().kind == 'D' && out.ev.back ().alias == ev.alias && out.ev.back ().recidx == ev.recidx) {
				std::vector<uint16_t> &g = out.ev.back ().rec.grp;
				if (g.size () + ev.rec.grp.size () > DENT_MAX_GRPLIST) over = true;
				else if (!over) g.insert (g.end (), ev.rec.grp.begin (), ev.rec.grp.end ());
				open = more;
				if (!open && over) { out.ev.pop_back (); out.skipped++; over = false; }
				continue;
			}
			if (open && over) { out.ev.pop_back (); out.skipped++; } // the dropped list never ended
			over = false;
			if (ok && ev.rec.grp.size () > DENT_MAX_GRPLIST) {
				if (more) ev.rec.grp.clear (), over = true;
				else ok = false;
			}
			open = more;
		} else if (ev.kind == 'S' && t.size () >= 5) {
			ok = DentMath::ParseStateEvent ((t[3] + " " + t[4]).c_str (), ev.eabs, ev.flags);
		} else if (ev.kind == 'R') {
			ok = true;
		} else if (ev.kind == 'X') { // dmg3: extension of the D event just before it
			uint32_t k = 0;
			ok = DentMath::ParseExtEvent (l.c_str () + TokStart (l, 3), k, ev.h8, ev.rec.p, ev.E, ev.vn, ev.vt);
			ev.recidx = k;
			ok = ok && !out.ev.empty () && out.ev.back ().kind == 'D' && out.ev.back ().alias == ev.alias && out.ev.back ().recidx == k && !open;
		} else if (ev.kind == 'T') { // dmg3: torn groups
			bool more = false;
			ok = DentMath::ParseTornEvent (l.c_str () + TokStart (l, 3), ev.torn, more);
			if (ok && topen && !out.ev.empty () && out.ev.back ().kind == 'T' && out.ev.back ().alias == ev.alias && out.ev.back ().torn.slot == ev.torn.slot
				&& out.ev.back ().torn.key == ev.torn.key && out.ev.back ().torn.kind == ev.torn.kind) {
				std::vector<uint16_t> &g = out.ev.back ().torn.grp;
				if (g.size () + ev.torn.grp.size () <= DENT_MAX_GRPLIST) g.insert (g.end (), ev.torn.grp.begin (), ev.torn.grp.end ());
				topen = more;
				continue;
			}
			topen = ok && more;
		} else if (ev.kind == 'B' && t.size () >= 7 && Num (t[3], ev.obj)) {
			ok = DentMath::ParseStateEvent ((t[4] + " " + t[5]).c_str (), ev.eabs, ev.flags) && CollKey::Unescape (t[6], ev.base);
		}
		if (ev.kind != 'D') {
			if (open && over) { out.ev.pop_back (); out.skipped++; }
			open = over = false;
		}
		if (ev.kind != 'T') topen = false;
		if (ok) out.ev.push_back (ev);
		else out.skipped++;
	}
	if (open && over && !out.ev.empty ()) { out.ev.pop_back (); out.skipped++; } // the dropped list never ended (file ends)
	return head;
}
