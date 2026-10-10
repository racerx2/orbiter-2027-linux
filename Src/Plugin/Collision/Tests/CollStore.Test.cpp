// not upstream: E3-U1 to E3-U6: keys, block text, prefix scan, matching, side file (Design CA E3 12.1)
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "CollStore.h"

namespace {

std::string TrimLikeCore (const std::string &s) // trim_string replica: cut at ';', trim spaces, tabs, CR
{
	std::string t = s.substr (0, s.find (';'));
	size_t b = t.find_first_not_of (" \t"), e = t.find_last_not_of (" \t\r");
	return b == std::string::npos ? std::string () : t.substr (b, e - b + 1);
}

CollStore::LineIn Reader (const std::vector<std::string> &l)
{
	auto pos = std::make_shared<size_t> (0);
	return [l, pos] (std::string &out) {
		while (*pos < l.size ()) {
			std::string s = TrimLikeCore (l[(*pos)++]);
			if (CollKey::IEqual (s, "END")) return false;
			out = s;
			return true;
		}
		return false;
	};
}

DentRecord Rec (uint32_t slot, double cx)
{
	DentRecord r {};
	r.p.c = Vector (cx, 0.2, 9.1), r.p.n = Vector (0, 0, 1), r.p.R = 0.81, r.p.h = 0.12, r.p.T = 2.4;
	r.slot = slot, r.key = 0x3c1f0a27, r.ngrp = 122, r.nvtx = 14516, r.grp = { 13, 14, 15 };
	return r;
}

}

TEST_CASE ("E3-U1 vessel key lines")
{
	std::mt19937 rng (7);
	for (int k = 0; k < 20000; k++) {
		std::string name, cls;
		int ln = rng () % 300, lc = rng () % 40;
		for (int i = 0; i < ln; i++) name += (char)(rng () % 256);
		for (int i = 0; i < lc; i++) cls += (char)(rng () % 256);
		uint32_t occ = rng () % 5;
		std::string l = CollKey::VesselLine ("VESSEL", occ, name, cls, 0);
		REQUIRE (l.size () <= (size_t)DENT_LINE_MAX);
		REQUIRE (l.find (';') == std::string::npos);
		REQUIRE (TrimLikeCore (l) == l);
		uint32_t o; std::string n2, c2; bool hashed; uint32_t hn, hc;
		REQUIRE (CollKey::ParseVesselLine (l, "VESSEL", o, n2, c2, hashed, hn, hc));
		REQUIRE (o == occ);
		if (!hashed) { REQUIRE (n2 == name); REQUIRE (c2 == cls); }
		else { REQUIRE (hn == DentMath::Fnv1a (name.data (), name.size ())); REQUIRE (hc == DentMath::Fnv1a (cls.data (), cls.size ())); }
	}
	CHECK (CollKey::Escape ("") == "%-");
	CHECK (CollKey::Escape ("a b;%") == "a%20b%3B%25");
}

TEST_CASE ("E3-U2 block write and read")
{
	DentVesselText v;
	v.eabs = 132104.5, v.flags = 1;
	v.rec = { Rec (0, -0.3), Rec (0, 1.5) };
	v.slotName = { "deltaglider" };
	std::vector<std::string> body = { "COLLA 1", "RECID 20261007-153012-1 1234.5", "VESSEL 0 GL-01 DeltaGlider" };
	std::vector<std::string> vl;
	DentMath::FormatVessel (v, "  ", vl);
	body.insert (body.end (), vl.begin (), vl.end ());
	body.push_back ("END_VESSEL");
	body.push_back ("VESSELH 1 0badf00d 12345678");
	body.push_back ("  XDMG 1 5 0");
	body.push_back ("END_VESSEL");
	body.push_back ("BEGIN_XDMG_BASES");
	body.push_back ("BASE Moon:Brighton Beach");
	body.push_back ("OBJ 1 BLOCK -60.6 -35 293900 0");
	body.push_back ("END_BASE");
	body.push_back ("END_XDMG_BASES");
	body.push_back ("SOMEKEY from another version");
	body.push_back ("TESTREPAIR 12.5 0 GL-01");
	body.push_back ("END");
	for (const std::string &l : body) CHECK (TrimLikeCore (l).size () <= (size_t)DENT_LINE_MAX);
	CollStoreBlock b;
	REQUIRE (CollStore::Parse (Reader (body), b));
	CHECK (b.version == 1);
	CHECK (b.recId == "20261007-153012-1");
	CHECK (b.recT0 == 1234.5);
	REQUIRE (b.vessel.size () == 2);
	CHECK (b.vessel[0].name == "GL-01");
	CHECK (b.vessel[0].cls == "DeltaGlider");
	CHECK (b.vessel[0].d.eabs == 132104.5);
	CHECK (b.vessel[0].d.flags == 1);
	REQUIRE (b.vessel[0].d.rec.size () == 2);
	CHECK (b.vessel[0].d.rec[1].p.c.x == 1.5);
	CHECK (b.vessel[0].d.rec[0].grp == std::vector<uint16_t> ({ 13, 14, 15 }));
	CHECK (b.vessel[1].hashed);
	CHECK (b.vessel[1].hName == 0x0badf00d);
	CHECK (b.vessel[1].raw.size () == 3);
	REQUIRE (b.base.size () == 1);
	CHECK (b.base[0].name == "Brighton Beach");
	CHECK (b.base[0].obj[0].eabs == 293900);
	REQUIRE (b.unknown.size () == 1);
	CHECK (b.unknown[0] == "SOMEKEY from another version");
	REQUIRE (b.testRepair.size () == 1);
	CHECK (b.testRepair[0].simt == 12.5);
	// format (parse (x)) equals x
	std::vector<std::string> again;
	DentMath::FormatVessel (b.vessel[0].d, "  ", again);
	CHECK (again == vl);
}

TEST_CASE ("E3-U3 prefix scan")
{
	std::vector<std::string> f = { "SOMETHING 1", "END", "BEGIN_Collision", "COLLA 1", "VESSEL 0 A B", "XDMG 1 7 0", "END_VESSEL", "END" };
	CollStoreBlock b;
	REQUIRE (CollStore::Parse (Reader (f), b));
	REQUIRE (b.vessel.size () == 1);
	CHECK (b.vessel[0].d.eabs == 7);
	std::vector<std::string> g = { "SOMETHING 1", "END", "OTHER", "END" };
	CHECK_FALSE (CollStore::Parse (Reader (g), b));
}

TEST_CASE ("E3-U4 vessel matching")
{
	std::vector<CollStoreVessel> s (5);
	s[0].occ = 1, s[0].name = "PB", s[0].cls = "ShuttlePB";
	s[1].occ = 0, s[1].name = "pb", s[1].cls = "shuttlepb";
	s[2].occ = 0, s[2].name = "gone", s[2].cls = "X";
	s[3].occ = 0, s[3].name = "GL", s[3].cls = "Other";
	s[4].occ = 0, s[4].hashed = true, s[4].hName = CollKey::Hash ("GL"), s[4].hClass = CollKey::Hash ("DeltaGlider");
	std::vector<CollLiveVessel> live = { { "PB", "ShuttlePB" }, { "PB", "ShuttlePB" }, { "GL", "DeltaGlider" } };
	std::vector<int> m = CollStore::MatchVessels (s, live);
	CHECK (m[0] == 1);
	CHECK (m[1] == 0); // case-only difference, rank 0
	CHECK (m[2] == -1);
	CHECK (m[3] == -1); // class changed: dormant
	CHECK (m[4] == 2);
}

TEST_CASE ("E3-U5 base matching")
{
	std::vector<CollLiveObj> objs = { { "Moon", "BB", "BLOCK", 0, 10, 10 }, { "Moon", "BB", "BLOCK", 1, -60.6, -35 }, { "Moon", "BB", "TANK", 2, 5, 5 } };
	std::vector<uint8_t> taken (3, 0);
	DentBaseObjText o { 1, "BLOCK", -60.6, -35, 1, 0 };
	CHECK (CollStore::MatchObj (o, objs, taken) == 1);
	o.index = 7;
	CHECK (CollStore::MatchObj (o, objs, taken) == 1); // by TYPE and position
	o.x = 100;
	CHECK (CollStore::MatchObj (o, objs, taken) == -1);
	DentBaseText b;
	b.planet = "Moon", b.name = "Brighton Beach";
	CHECK (CollStore::SameBase (b, "moon", "brighton beach"));
	b.name.clear (), b.nameHash = CollKey::Hash ("Brighton Beach");
	CHECK (CollStore::SameBase (b, "Moon", "Brighton Beach"));
}

TEST_CASE ("E3-U6 side file round trip")
{
	std::vector<std::string> l = { CollSide::Header ("X1"), CollSide::Vdef (0, 0, "GL-01", "DeltaGlider") };
	DentRecord r = Rec (0, -0.3);
	for (uint16_t g = 0; g < 100; g++) r.grp.push_back (g); // continued payloads
	CollSide::Dent (12.483333333333333, 0, 0, r, l);
	l.push_back (CollSide::State (12.483333333333333, 0, 132104.5, 0));
	l.push_back (CollSide::Building (12.483333333333333, 0, 1, 293900, 0, "Moon:Brighton Beach"));
	l.push_back (CollSide::Repair (40.016666666666666, 0));
	std::string text;
	for (const std::string &s : l) { CHECK (s.size () < 256); text += s + "\n"; }
	CollSideFile f;
	REQUIRE (CollSide::Parse (text + "41 S 0 12", f)); // truncated last line skipped
	CHECK (f.id == "X1");
	REQUIRE (f.alias.size () == 1);
	CHECK (f.alias[0].name == "GL-01");
	REQUIRE (f.ev.size () == 4);
	CHECK (f.ev[0].kind == 'D');
	CHECK (f.ev[0].t == 12.483333333333333);
	CHECK (f.ev[0].rec.grp == r.grp);
	CHECK (f.ev[0].rec.p.c.x == r.p.c.x);
	CHECK (f.ev[1].kind == 'S');
	CHECK (f.ev[1].eabs == 132104.5);
	CHECK (f.ev[2].base == "Moon:Brighton Beach");
	CHECK (f.ev[3].kind == 'R');
	CHECK (f.ev[3].t == 40.016666666666666);
	CHECK (f.skipped == 1);
}

TEST_CASE ("fix1 M8 hostile block and side file are bounded")
{
	// the review's 200,001-record block (rev-dmg/hostile.cpp)
	std::vector<std::string> L { "VESSEL 0 PB-A ShuttlePB", "XDMG 1 100 0", "XDMGM 0 0 deadbeef 7 140" };
	for (int i = 0; i < 200000; i++) L.push_back ("XDMGD 0 0 0 3 0 0 1 1 0.1 0 *");
	for (int i = 0; i < 20000; i++) L.push_back ("XDMGD 0 1 1 1 0 0 1 1 0.1 0 1,2,3,4,5,6,7,8,9,10,");
	L.push_back ("XDMGD 0 1 1 1 0 0 1 1 0.1 0 11");
	L.push_back ("END_VESSEL");
	CollStoreBlock b;
	CollStore::ParseBody (L, b);
	REQUIRE (b.vessel.size () == 1);
	CHECK (b.vessel[0].d.rec.size () == DENT_MAX_VESSEL);
	for (const DentRecord &r : b.vessel[0].d.rec) CHECK (r.grp.size () <= DENT_MAX_GRPLIST);
	CHECK (b.vessel[0].skipped >= 200000 - (int)DENT_MAX_VESSEL);
	CHECK (b.vessel[0].raw.size () == 4 + DENT_MAX_VESSEL); // dormant write-back: VESSEL, XDMG, XDMGM, records, END_VESSEL
	CHECK (b.vessel[0].raw.front () == "VESSEL 0 PB-A ShuttlePB");
	CHECK (b.vessel[0].raw.back () == "END_VESSEL");
	std::vector<std::string> out;
	DentMath::FormatVessel (b.vessel[0].d, "  ", out);
	CHECK (out.size () <= 2 + DENT_MAX_VESSEL);
	// side file: a D event whose continued list passes the limit is dropped, the next one kept
	DentRecord r {};
	r.p.c = Vector (1, 2, 3), r.p.n = Vector (0, 0, 1), r.p.R = 1, r.p.h = 0.1, r.slot = 0;
	for (int i = 0; i < 70000; i++) r.grp.push_back ((uint16_t)(i % 60000));
	std::vector<std::string> l;
	CollSide::Dent (1, 0, 0, r, l);
	REQUIRE (l.size () > 1);
	r.grp = { 3 };
	CollSide::Dent (2, 0, 1, r, l);
	std::string text = CollSide::Header ("X") + "\n" + CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") + "\n";
	for (auto &x : l) text += x + "\n";
	CollSideFile f;
	REQUIRE (CollSide::Parse (text, f));
	REQUIRE (f.ev.size () == 1);
	CHECK (f.ev[0].recidx == 1);
	CHECK (f.ev[0].rec.grp == std::vector<uint16_t> { 3 });
	CHECK (f.skipped == 1);
}

TEST_CASE ("fix1 review: an over-long D event cut off by the end of the side file is dropped")
{
	DentRecord r {};
	r.p.c = Vector (1, 2, 3), r.p.n = Vector (0, 0, 1), r.p.R = 1, r.p.h = 0.1, r.slot = 0;
	for (int i = 0; i < 70000; i++) r.grp.push_back ((uint16_t)(i % 60000));
	std::vector<std::string> l;
	CollSide::Dent (1, 0, 0, r, l);
	REQUIRE (l.size () > 2);
	l.pop_back (); // the list's last line never written (crash while recording)
	std::string text = CollSide::Header ("X") + "\n" + CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") + "\n";
	for (auto &x : l) text += x + "\n";
	CollSideFile f;
	REQUIRE (CollSide::Parse (text, f));
	CHECK (f.ev.empty ()); // not kept with an empty list (= all groups)
}

TEST_CASE ("fix2 M5 a capped dormant vessel keeps every line through a save")
{
	std::string name (190, 'm');
	std::vector<std::string> L { "VESSEL 0 PB-A ShuttlePB", "XDMG 1 100 0", "XDMGM 0 0 deadbeef 7 140 " + name };
	std::string g = "1";
	for (int i = 2; i < 200; i++) g += "," + std::to_string (i);
	L.push_back ("XDMGD 0 0 0 3 0 0 1 1 0.1 0 " + g);
	for (uint32_t i = 0; i < DENT_MAX_VESSEL + 10; i++) L.push_back ("XDMGD 0 " + std::to_string (i + 1) + " 0 3 0 0 1 1 0.1 0 *");
	L.push_back ("END_VESSEL");
	CollStoreBlock b;
	CollStore::ParseBody (L, b);
	REQUIRE (b.vessel.size () == 1);
	const CollStoreVessel &v = b.vessel[0];
	REQUIRE (v.d.rec.size () == DENT_MAX_VESSEL);
	std::vector<std::string> saved; // the save writes inner lines with two spaces and drops lines over 200
	for (size_t i = 0; i < v.raw.size (); i++) {
		std::string s = (i == 0 || i + 1 == v.raw.size ()) ? v.raw[i] : "  " + v.raw[i];
		CHECK (CollStore::Line200 (s));
		saved.push_back (s);
	}
	CollStoreBlock b2;
	CollStore::ParseBody (saved, b2);
	REQUIRE (b2.vessel.size () == 1);
	const DentVesselText &d = b2.vessel[0].d;
	REQUIRE (d.rec.size () == DENT_MAX_VESSEL);
	CHECK (d.rec[0].grp == v.d.rec[0].grp);
	CHECK (d.rec[0].grp.size () == 199);
	REQUIRE (!d.slotName.empty ());
	CHECK (name.compare (0, d.slotName[0].size (), d.slotName[0]) == 0); // cut to fit the indented line, never dropped
	CHECK (d.slotName[0].size () == 200 - 27);
}

TEST_CASE ("dmg3 side file: X after its D event, T with continuation, unknown kinds skipped")
{
	DentRecord r {};
	r.p.c = Vector (1, 2, 3), r.p.n = Vector (0, 0, 1), r.p.R = 1, r.p.h = 0.1, r.slot = 0, r.grp = { 2, 4 };
	DentParams x = r.p;
	x.mode = DENTM_CRUSH, x.P = 0.25, x.seed = 0xdeadbeef, x.t = Vector (1, 0, 0), x.bits = 0;
	std::string text = CollSide::Header ("X") + "\n" + CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") + "\n";
	std::vector<std::string> l;
	CollSide::Dent (1.5, 0, 0, r, l);
	l.push_back (CollSide::Ext (1.5, 0, 0, x, 12345.5, 30.25, 1.75));
	l.push_back (CollSide::Ext (1.5, 0, 7, x, 1, 1, 1)); // no D event of record 7 before it: skipped
	DentTorn t;
	t.kind = 2, t.slot = 0, t.key = 0x1234, t.ngrp = 3, t.nvtx = 99, t.simt = 1.5, t.debris = "Deb-1";
	for (int g = 0; g < 200; g++) t.grp.push_back ((uint16_t)(g * 11));
	CollSide::Torn (1.6, 0, t, l);
	CHECK (l.size () > 4); // the T list continues over lines
	l.push_back ("1.7 Z 0 future kind");
	for (auto &s : l) { CHECK (s.size () < 256); text += s + "\n"; }
	CollSideFile f;
	REQUIRE (CollSide::Parse (text, f));
	REQUIRE (f.ev.size () == 3);
	CHECK (f.ev[0].kind == 'D');
	CHECK (f.ev[1].kind == 'X');
	CHECK (f.ev[1].recidx == 0);
	CHECK (f.ev[1].h8 == DentMath::ParamsHash (r.p));
	CHECK (f.ev[1].rec.p.mode == DENTM_CRUSH);
	CHECK (f.ev[1].rec.p.seed == 0xdeadbeef);
	CHECK (f.ev[1].E == 12345.5);
	CHECK (f.ev[1].vn == 30.25);
	CHECK (f.ev[1].vt == 1.75);
	CHECK (f.ev[2].kind == 'T');
	CHECK (f.ev[2].torn.grp == t.grp);
	CHECK (f.ev[2].torn.debris == "Deb-1");
	CHECK (f.skipped == 2);
}

TEST_CASE ("dmg3 a capped dormant rewrite keeps the v2 rows: torn T, debris B and Q, XDMGD B; lines <= 200; re-parse equal")
{
	DentVesselText v;
	v.eabs = 7.5e5;
	for (uint32_t i = 0; i < DENT_MAX_VESSEL; i++) {
		DentRecord r {};
		r.slot = 0, r.key = 5, r.ngrp = 300, r.nvtx = 4000;
		r.p.c = Vector (0.01 * i, 1, 2), r.p.n = Vector (0, 1, 0), r.p.R = 1.5, r.p.h = 0.2;
		if (i == 0) r.p.mode = DENTM_CRUSH, r.p.P = 0.4, r.p.seed = 0x1234, r.p.t = Vector (0, 0, 1);
		if (i == 1) r.p.mode = DENTM_HINGE, r.p.P = 0.2, r.p.t = Vector (1, 0, 0), r.p.hd = 0.75, r.p.hz = 0.1;
		DentMath::Quantise (r.p);
		r.grp = { 1, 2 };
		v.rec.push_back (r);
	}
	DentTorn t;
	t.kind = 2, t.slot = 0, t.key = 5, t.ngrp = 300, t.nvtx = 4000, t.simt = 12.5, t.debris = "Deb-1";
	for (int g = 0; g < 180; g++) t.grp.push_back ((uint16_t)(g + 10));
	v.torn.push_back (t);
	DentDebris d;
	d.id = 3, d.slot = 0, d.key = 5, d.ngrp = 300, d.nvtx = 4000, d.simt = 12.5, d.name = "Deb-1";
	DentDebrisPose q;
	q.p = Vector (0.5, -1.25, 7.0), q.q[0] = 0.1, q.q[1] = 0.2, q.q[2] = 0.3, q.q[3] = 0.927;
	for (int g = 0; g < 150; g++) q.grp.push_back ((uint16_t)(g + 10));
	d.pose.push_back (q);
	d.rec.push_back (v.rec[0]), d.rec.push_back (v.rec[1]);
	v.debris.push_back (d);
	std::vector<std::string> f;
	DentMath::FormatVessel (v, "  ", f);
	std::vector<std::string> L { "VESSEL 0 PB-A ShuttlePB" };
	for (const std::string &x : f) L.push_back (x.substr (2));
	size_t at = 0;
	while (at < L.size () && L[at].compare (0, 7, "XDMG 2 ") != 0) at++;
	REQUIRE (at < L.size ());
	for (uint32_t i = 0; i < 10; i++) L.insert (L.begin () + at, "XDMGD 0 9 0 3 0 0 1 1 0.1 0 *"); // version-1 records past the cap
	L.push_back ("END_VESSEL");
	CollStoreBlock b;
	CollStore::ParseBody (L, b);
	REQUIRE (b.vessel.size () == 1);
	const CollStoreVessel &sv = b.vessel[0];
	REQUIRE (sv.d.rec.size () == DENT_MAX_VESSEL);
	REQUIRE (sv.d.torn.size () == 1);
	REQUIRE (sv.d.debris.size () == 1);
	std::vector<std::string> saved;
	bool hasT = false, hasB = false, hasDB = false;
	for (size_t i = 0; i < sv.raw.size (); i++) {
		std::string s = (i == 0 || i + 1 == sv.raw.size ()) ? sv.raw[i] : "  " + sv.raw[i];
		CHECK (CollStore::Line200 (s));
		CHECK (s.size () <= 200);
		hasT |= s.compare (0, 10, "  XDMGM T ") == 0;
		hasB |= s.compare (0, 10, "  XDMGM B ") == 0;
		hasDB |= s.compare (0, 10, "  XDMGD B ") == 0;
		saved.push_back (s);
	}
	CHECK (sv.raw.size () < L.size ()); // the over-cap lines are gone, the rest kept
	CHECK (hasT);
	CHECK (hasB);
	CHECK (hasDB);
	CollStoreBlock b2;
	CollStore::ParseBody (saved, b2);
	REQUIRE (b2.vessel.size () == 1);
	const DentVesselText &w = b2.vessel[0].d;
	CHECK (b2.vessel[0].skipped == 0);
	REQUIRE (w.rec.size () == DENT_MAX_VESSEL);
	CHECK (std::memcmp (&w.rec[0].p, &v.rec[0].p, sizeof v.rec[0].p) == 0);
	CHECK (std::memcmp (&w.rec[1].p, &v.rec[1].p, sizeof v.rec[1].p) == 0);
	CHECK (w.rec[0].p.mode == DENTM_CRUSH);
	CHECK (w.rec[1].p.mode == DENTM_HINGE);
	REQUIRE (w.torn.size () == 1);
	CHECK (w.torn[0].kind == t.kind);
	CHECK (w.torn[0].grp == t.grp);
	CHECK (w.torn[0].debris == t.debris);
	CHECK (w.torn[0].simt == t.simt);
	REQUIRE (w.debris.size () == 1);
	CHECK (w.debris[0].name == d.name);
	REQUIRE (w.debris[0].pose.size () == 1);
	CHECK (w.debris[0].pose[0].grp == q.grp);
	CHECK (w.debris[0].pose[0].q[3] == 0.927);
	REQUIRE (w.debris[0].rec.size () == 2);
	CHECK (std::memcmp (&w.debris[0].rec[0].p, &v.rec[0].p, sizeof v.rec[0].p) == 0);
	std::vector<std::string> f2, f3;
	DentMath::FormatVessel (sv.d, "", f2);
	DentMath::FormatVessel (w, "", f3);
	CHECK (f2 == f3);
}

TEST_CASE ("blast recorder: V sites and K bonds events round trip; a cut V list is dropped; S stays the state event", "[blast]")
{
	DentSites st; st.slot = 2, st.key = 0x89abcdef;
	for (int i = 0; i < 30; i++) st.s.push_back (Vector (100.5 + i, -0.25 * i, 7.75));
	std::vector<uint32_t> b;
	for (uint32_t i = 0; i < 90; i++) b.push_back (i * 65537u);
	std::vector<std::string> l;
	l.push_back (CollSide::Header ("BL1"));
	l.push_back (CollSide::Vdef (0, 0, "PB-A", "ShuttlePB"));
	CollSide::Sites (1.5, 0, st, l);
	CollSide::Bonds (1.5, 0, 2, b, l);
	l.push_back (CollSide::State (1.5, 0, 12.5, 4));
	DentRecord r {};
	r.p.mode = DENTM_VCUT, r.p.c = st.s[3], r.p.n = Vector (0, 0, 1), r.p.t = Vector (1, 0, 0), r.p.R = 0.5, r.p.h = 0, r.p.P = 3, r.p.seed = 30, r.p.hd = 0.2, r.p.hz = 0.02;
	r.slot = 2, r.grp = { 0, 4 };
	CollSide::Dent (1.5, 0, 0, r, l);
	l.push_back (CollSide::Ext (1.5, 0, 0, r.p, 0, 0, 0, 0, 0u));
	std::string text;
	size_t nv = 0;
	for (auto &x : l) { CHECK (x.size () < 256); if (x.find (" V 0 ") != std::string::npos) nv++; text += x + "\n"; }
	CHECK (nv > 1);
	CollSideFile f;
	REQUIRE (CollSide::Parse (text, f));
	CHECK (f.skipped == 0);
	REQUIRE (f.ev.size () == 5);
	CHECK (f.ev[0].kind == 'V');
	REQUIRE (f.ev[0].sites.s.size () == 30);
	CHECK (f.ev[0].sites.slot == 2); CHECK (f.ev[0].sites.key == st.key);
	CHECK (std::memcmp (f.ev[0].sites.s.data (), st.s.data (), 30 * sizeof (Vector)) == 0);
	CHECK (f.ev[1].kind == 'K'); CHECK (f.ev[1].slot == 2); CHECK (f.ev[1].bonds == b);
	CHECK (f.ev[2].kind == 'S'); CHECK (f.ev[2].eabs == 12.5); CHECK (f.ev[2].flags == 4);
	CHECK (f.ev[3].kind == 'D'); CHECK (f.ev[4].kind == 'X'); CHECK (f.ev[4].rec.p.mode == DENTM_VCUT); CHECK (f.ev[4].rec.p.P == 3.0); CHECK (f.ev[4].rec.p.seed == 30);
	std::string cut;
	bool dropped = false;
	for (auto &x : l) { if (!dropped && x.find (" V 0 ") != std::string::npos && x.find (" V 0 2 89abcdef 30 0 ") == std::string::npos) { dropped = true; continue; } cut += x + "\n"; }
	REQUIRE (dropped);
	CollSideFile g;
	REQUIRE (CollSide::Parse (cut, g));
	CHECK (g.skipped > 0);
	for (auto &e : g.ev) CHECK (e.kind != 'V');
}

TEST_CASE ("blast a capped dormant rewrite keeps the S and K rows", "[blast]")
{
	DentVesselText v;
	for (uint32_t i = 0; i < DENT_MAX_VESSEL; i++) {
		DentRecord r {};
		r.slot = 0, r.key = 5, r.ngrp = 300, r.nvtx = 4000;
		r.p.c = Vector (0.01 * i, 1, 2), r.p.n = Vector (0, 1, 0), r.p.R = 1.5, r.p.h = 0.2;
		if (i == 1) r.p.mode = DENTM_VCUT, r.p.h = 0, r.p.P = 7, r.p.seed = 20, r.p.t = Vector (1, 0, 0), r.p.hd = 0.2, r.p.hz = 0.02;
		DentMath::Quantise (r.p);
		v.rec.push_back (r);
	}
	DentSites st; st.slot = 0, st.key = 5;
	for (int i = 0; i < 20; i++) st.s.push_back (Vector (1.5 * i, -2.25, 0.125 * i));
	v.sites.push_back (st);
	std::vector<uint32_t> bonds;
	for (uint32_t i = 0; i < 80; i++) bonds.push_back (i * 3);
	v.brokenBonds.push_back ({ 0, bonds });
	std::vector<std::string> f;
	DentMath::FormatVessel (v, "  ", f);
	std::vector<std::string> L { "VESSEL 0 PB-A ShuttlePB" };
	for (const std::string &x : f) L.push_back (x.substr (2));
	size_t at = 0;
	while (at < L.size () && L[at].compare (0, 7, "XDMG 2 ") != 0) at++;
	REQUIRE (at < L.size ());
	for (uint32_t i = 0; i < 10; i++) L.insert (L.begin () + at, "XDMGD 0 9 0 3 0 0 1 1 0.1 0 *");
	L.push_back ("END_VESSEL");
	CollStoreBlock b;
	CollStore::ParseBody (L, b);
	REQUIRE (b.vessel.size () == 1);
	const CollStoreVessel &sv = b.vessel[0];
	REQUIRE (sv.d.rec.size () == DENT_MAX_VESSEL);
	CHECK (sv.d.rec[1].p.mode == DENTM_VCUT);
	REQUIRE (sv.d.sites.size () == 1);
	REQUIRE (sv.d.brokenBonds.size () == 1);
	std::vector<std::string> saved;
	bool hasS = false, hasK = false;
	for (size_t i = 0; i < sv.raw.size (); i++) {
		std::string s = (i == 0 || i + 1 == sv.raw.size ()) ? sv.raw[i] : "  " + sv.raw[i];
		CHECK (CollStore::Line200 (s));
		hasS |= s.compare (0, 10, "  XDMGM S ") == 0;
		hasK |= s.compare (0, 10, "  XDMGM K ") == 0;
		saved.push_back (s);
	}
	CHECK (sv.raw.size () < L.size ());
	CHECK (hasS); CHECK (hasK);
	CollStoreBlock b2;
	CollStore::ParseBody (saved, b2);
	REQUIRE (b2.vessel.size () == 1);
	REQUIRE (b2.vessel[0].d.sites.size () == 1);
	CHECK (std::memcmp (b2.vessel[0].d.sites[0].s.data (), st.s.data (), st.s.size () * sizeof (Vector)) == 0);
	REQUIRE (b2.vessel[0].d.brokenBonds.size () == 1);
	CHECK (b2.vessel[0].d.brokenBonds[0].second == bonds);
}

TEST_CASE ("blast fix m10: K events keep 32-bit chunk pairs a * 65536 + b", "[blast]")
{
	std::vector<uint32_t> b;
	for (uint32_t a = 0; a < 60; a++) b.push_back (a * 65536u + a + 1u);
	b.push_back (0xfffeffffu);
	std::vector<std::string> l { CollSide::Header ("BL2"), CollSide::Vdef (0, 0, "PB-A", "ShuttlePB") };
	CollSide::Bonds (2.0, 0, 7, b, l);
	std::string text;
	for (auto &x : l) text += x + "\n";
	CHECK (l.size () > 3);
	CollSideFile f;
	REQUIRE (CollSide::Parse (text, f));
	CHECK (f.skipped == 0);
	REQUIRE (f.ev.size () == 1);
	CHECK (f.ev[0].kind == 'K'); CHECK (f.ev[0].slot == 7); CHECK (f.ev[0].bonds == b);
}

TEST_CASE ("custom-fix2 B4 D2: a blast cell debris row in the side file: its K payload, then cell payloads, each a whole T event within the payload limit")
{
	DentTorn t;
	t.kind = 5, t.slot = 0, t.key = 0x1234, t.ngrp = 7, t.nvtx = 303, t.simt = 0.30000000000000004, t.debris = "A-long-parent-vessel-name-of-the-recording_D1";
	t.kin = true, t.dv = Vector (0.812345678, -2.50000001, 3.75), t.dw = Vector (0.0123456789, -0.25, 1.5), t.mass = 91.2345678;
	for (uint32_t c = 0; c < 64; c += 2) t.cells.push_back (c);
	t.pieces = { 1 }, t.c = Vector (0.712345678, 0.698765432, 2.0123456), t.crushed = true;
	DentTorn p = t;                                                  // the part row of the same debris after it
	p.kind = 0, p.grp = { 6 }, p.cells.clear (), p.pieces.clear (), p.crushed = false;
	std::string text = CollSide::Header ("X") + "\n" + CollSide::Vdef (0, 0, "A-long-parent-vessel-name-of-the-recording", "ShuttlePB") + "\n";
	std::vector<std::string> l;
	CollSide::Torn (0.3, 0, t, l);
	size_t nCell = l.size ();
	CollSide::Torn (0.3, 0, p, l);
	REQUIRE (nCell >= 3);                                            // K, then the cells over more than one payload
	for (auto &s : l) {
		INFO (s);
		std::string pre = CollSide::Fmt17 (0.3) + " T 0 ";
		REQUIRE (s.compare (0, pre.size (), pre) == 0);
		CHECK (s.size () - pre.size () <= (size_t)DENT_EVENT_MAX);
		text += s + "\n";
	}
	CollSideFile f;
	REQUIRE (CollSide::Parse (text, f));
	CHECK (f.skipped == 0);
	REQUIRE (f.ev.size () == l.size ());                             // whole events: never joined as a continuation
	std::vector<uint32_t> cells, pieces;
	for (size_t i = 0; i < f.ev.size (); i++) {
		const DentTorn &o = f.ev[i].torn;
		CHECK (f.ev[i].kind == 'T');
		CHECK (o.debris == t.debris);
		CHECK (o.kind == (i < nCell ? 5 : 0));                        // CBRK_CELL, then CBRK_PART
		CHECK (o.kin == (i == 0 || i == nCell));
		if (i > 0 && i < nCell) { CHECK (o.crushed); CHECK (o.c.x == t.c.x); CHECK (o.c.y == t.c.y); CHECK (o.c.z == t.c.z); cells.insert (cells.end (), o.cells.begin (), o.cells.end ()); pieces.insert (pieces.end (), o.pieces.begin (), o.pieces.end ()); }
	}
	CHECK (f.ev[0].torn.mass == t.mass); CHECK (f.ev[0].torn.dv.y == t.dv.y); CHECK (f.ev[0].torn.dw.x == t.dw.x);
	CHECK (cells == t.cells); CHECK (pieces == t.pieces);
	CHECK (f.ev[nCell].torn.grp == p.grp); CHECK (f.ev[nCell].torn.mass == t.mass);
}
