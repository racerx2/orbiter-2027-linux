// not upstream: unit tests of the Collision.cfg reader (Design CA E3-U13: defaults, keys and unknown keys from P4; E3 adds the clamps)
#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>
#include "CollCfg.h"

namespace {

char Fold (char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }

bool SameNoCase (const std::string &a, const std::string &b)
{
	if (a.size () != b.size ()) return false;
	for (size_t i = 0; i < a.size (); i++) if (Fold (a[i]) != Fold (b[i])) return false;
	return true;
}

// the core's item lookup (Src/Orbiter/Config.cpp GetItemString): ';' comment, trimmed, first case-insensitive key match, empty value = missing
bool ItemString (const std::string &text, const char *label, std::string &val)
{
	std::istringstream is (text);
	std::string line;
	while (std::getline (is, line)) {
		size_t c = line.find (';');
		if (c != std::string::npos) line.erase (c);
		while (!line.empty () && (line.back () == ' ' || line.back () == '\t' || line.back () == '\r')) line.pop_back ();
		size_t b = line.find_first_not_of (" \t");
		line = b == std::string::npos ? std::string () : line.substr (b);
		if (SameNoCase (line, "END_PARSE")) return false;
		size_t e = line.find ('=');
		std::string key = line.substr (0, e), v = e == std::string::npos ? std::string () : line.substr (e + 1);
		while (!key.empty () && (key.back () == ' ' || key.back () == '\t')) key.pop_back ();
		if (!SameNoCase (key, label)) continue;
		size_t f = v.find_first_not_of (" \t");
		if (f == std::string::npos) return false;
		val = v.substr (f);
		return true;
	}
	return false;
}

CollCfgValues ReadText (const std::string &text, std::vector<std::string> *bad = nullptr)
{
	return CollCfg::Read ([&] (const char *item, std::string &val) { return ItemString (text, item, val); }, bad);
}

std::vector<std::string> Flat (CollCfgValues v) // every field as text, in key order
{
	std::vector<std::string> out;
	CollCfgFields (v, [&] (const char *key, auto &f) {
		std::ostringstream os;
		os.precision (17);
		using T = std::decay_t<decltype (f)>;
		if constexpr (std::is_same_v<T, std::vector<std::string>>) { for (auto &w : f) os << '[' << w << ']'; }
		else os << f;
		out.push_back (std::string (key) + "=" + os.str ());
	});
	return out;
}

}

TEST_CASE ("E3-U13 defaults")
{
	int asked = 0;
	CollCfgValues v = CollCfg::Read ([&] (const char *, std::string &) { asked++; return false; });
	CHECK (asked == 20);
	CHECK (v.model == 1);
	CHECK (v.response);
	CHECK_FALSE (v.check);
	CHECK (v.dockZone);
	CHECK (v.attachZone);
	CHECK (v.logLevel == 1);
	CHECK (v.clientCheck == 0);
	CHECK (v.meshProbeOnce.empty ());
	CHECK (v.destroyEnergy == 1000.0);
	CHECK (v.buildingDestroyEnergy == 1000.0);
	CHECK (v.thrustCut);
	CHECK (v.visuals);
	CHECK (v.keyPass);
	CHECK (v.cullFix);
	CHECK (v.notify == 1);
	CHECK (v.recorder);
	CHECK (v.testRecId.empty ());
	CHECK (v.testKick.empty ());
	CHECK (v.testModelAt.empty ());
	CHECK (v.testSlotCheck == 0);
}

TEST_CASE ("E3-U13 keys")
{
	const std::vector<std::string> want = { "CollisionModel", "CollisionResponse", "CollisionCheck", "CollisionDockZone", "CollisionAttachZone",
		"CollisionLog", "ClientCheck", "MeshProbe", "DestroyEnergy", "BuildingDestroyEnergy", "CollisionThrustCut", "CollisionVisuals",
		"CollisionKeyPass", "CollisionCullFix", "CollisionNotify", "CollisionRecorder", "CollisionTestRecId", "CollisionTestKick",
		"CollisionTestModelAt", "CollisionTestSlotCheck" };
	std::vector<std::string> keys;
	for (const char *k : CollCfg::Keys ()) keys.push_back (k);
	CHECK (keys == want);
	int recIds = 0;
	for (size_t i = 0; i < keys.size (); i++) {
		for (size_t j = i + 1; j < keys.size (); j++) CHECK_FALSE (SameNoCase (keys[i], keys[j]));
		std::string u;
		for (char c : keys[i]) u += Fold (c);
		if (u.find ("RECID") != std::string::npos) recIds++;
	}
	CHECK (recIds == 1); // CollisionTestRecId is the one recorder test key
}

TEST_CASE ("E3-U13 template file")
{
	std::ifstream f (COLL_ADDON_SRC "/Config/Collision.cfg", std::ios::binary);
	REQUIRE (f);
	std::stringstream ss;
	ss << f.rdbuf ();
	std::string text = ss.str ();
	for (const char *k : CollCfg::Keys ()) { // every key has its own line, empty values included
		bool found = false;
		std::istringstream is (text);
		std::string line;
		while (std::getline (is, line) && !found) {
			size_t e = line.find ('=');
			if (e == std::string::npos || line[0] == ';') continue;
			std::string key = line.substr (0, e);
			while (!key.empty () && key.back () == ' ') key.pop_back ();
			found = key == k;
		}
		CHECK (found);
		if (!found) WARN ("missing key " << k);
	}
	std::vector<std::string> bad;
	CHECK (Flat (ReadText (text, &bad)) == Flat (CollCfgValues ())); // the template holds the defaults
	CHECK (bad.empty ());
}

TEST_CASE ("E3-U13 unknown keys and lookup")
{
	const std::string text =
		"; CollisionLog = 4\n"
		"CollisionModelX = 1\n"
		"Collision = 2\n"
		"collisionmodel = 1 ; case-insensitive\r\n"
		"CollisionModel = 0\n"
		"CollisionFuture = TRUE\n"
		"  CollisionNotify\t=\t2\n"
		"MeshProbe = ShuttleA  DeltaGlider\tAtlantis\n"
		"CollisionTestKick = PB-A 12.5 0 1 0 0 0 0\n"
		"TestRecId = old\n"
		"END_PARSE\n"
		"CollisionCheck = TRUE\n";
	std::vector<std::string> bad;
	CollCfgValues v = ReadText (text, &bad);
	CHECK (bad.empty ());
	CHECK (v.model == 1);
	CHECK (v.logLevel == 1);
	CHECK (v.notify == 2);
	CHECK (v.meshProbeOnce == std::vector<std::string> { "ShuttleA", "DeltaGlider", "Atlantis" });
	CHECK (v.testKick == "PB-A 12.5 0 1 0 0 0 0");
	CHECK (v.testRecId.empty ());
	CHECK_FALSE (v.check);
	CollCfgValues d;
	v.model = d.model; v.notify = d.notify; v.meshProbeOnce = d.meshProbeOnce; v.testKick = d.testKick;
	CHECK (Flat (v) == Flat (d)); // nothing else changed
}

TEST_CASE ("E3-U13 values")
{
	std::map<std::string, std::string> file = {
		{ "CollisionResponse", "false" }, { "CollisionCheck", "1" }, { "CollisionDockZone", "0" }, { "CollisionAttachZone", "yes" },
		{ "CollisionModel", "+1" }, { "CollisionLog", "2.5" }, { "ClientCheck", "x" }, { "CollisionNotify", " 2 " },
		{ "DestroyEnergy", "1e3" }, { "BuildingDestroyEnergy", "nan" }, { "CollisionTestSlotCheck", "1" },
		{ "CollisionThrustCut", "TRUE" }, { "CollisionVisuals", "FaLsE" }, { "CollisionTestRecId", "E3H7 " } };
	std::vector<std::string> bad;
	CollCfgValues v = CollCfg::Read ([&] (const char *item, std::string &val) {
		auto it = file.find (item);
		if (it == file.end ()) return false;
		val = it->second;
		return true;
	}, &bad);
	CHECK_FALSE (v.response);
	CHECK (v.check);
	CHECK_FALSE (v.dockZone);
	CHECK (v.attachZone);
	CHECK (v.model == 1);
	CHECK (v.logLevel == 1);
	CHECK (v.clientCheck == 0);
	CHECK (v.notify == 2);
	CHECK (v.destroyEnergy == 1000.0);
	CHECK (v.buildingDestroyEnergy == 1000.0);
	CHECK (v.testSlotCheck == 1);
	CHECK (v.thrustCut);
	CHECK_FALSE (v.visuals);
	CHECK (v.testRecId == "E3H7");
	CHECK (bad == std::vector<std::string> { "CollisionAttachZone", "CollisionLog", "ClientCheck", "BuildingDestroyEnergy" });
	double d = 0;
	CHECK_FALSE (CollCfg::Parse ("inf", d));
	CHECK_FALSE (CollCfg::Parse ("", d));
	CHECK_FALSE (CollCfg::Parse ("1 2", d));
	CHECK (CollCfg::Parse ("-0.25", d));
	CHECK (d == -0.25);
	int i = 7;
	CHECK_FALSE (CollCfg::Parse ("99999999999", i));
	CHECK_FALSE (CollCfg::Parse ("+-1", i));
	CHECK_FALSE (CollCfg::Parse ("++1", i));
	CHECK_FALSE (CollCfg::Parse ("+", i));
	CHECK (i == 7);
	CHECK (CollCfg::Parse ("+0", i));
	CHECK (i == 0);
	d = 7;
	CHECK_FALSE (CollCfg::Parse ("+-2.5", d));
	CHECK_FALSE (CollCfg::Parse ("+", d));
	CHECK (d == 7);
	CHECK (CollCfg::Parse ("+2.5", d));
	CHECK (d == 2.5);
	std::vector<std::string> w = { "x" };
	CHECK (CollCfg::Parse ("   ", w));
	CHECK (w.empty ());
}
