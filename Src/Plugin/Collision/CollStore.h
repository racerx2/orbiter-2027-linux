// not upstream: collision addon, E3 persistence: vessel and base keys, the plugin scenario block, recorder side file (Design CA E3 7, 8)
#ifndef COLLSTORE_H
#define COLLSTORE_H
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "DentMath.h"

constexpr int COLL_STORE_VERSION = 1;        // COLLA <version> header

namespace CollKey {
	std::string Escape (const std::string &s);                    // bytes <= 0x20, 0x7F, '%', ';' as %XX; "" as %-
	bool Unescape (const std::string &s, std::string &out);
	std::string Hex8 (uint32_t h);
	bool IEqual (const std::string &a, const std::string &b);    // ASCII case-insensitive
	uint32_t Hash (const std::string &s);                         // DentMath::Fnv1a of the bytes
	std::string VesselLine (const char *kw, uint32_t occ, const std::string &name, const std::string &cls, size_t indent); // "<kw> occ name class" or "<kw>H occ hash hash" within DENT_LINE_MAX
	bool ParseVesselLine (const std::string &line, const char *kw, uint32_t &occ, std::string &name, std::string &cls, bool &hashed, uint32_t &hName, uint32_t &hClass, size_t first = 0); // tokens from index first
}

struct CollStoreVessel {                     // one saved VESSEL section
	uint32_t occ = 0; bool hashed = false;
	std::string name, cls; uint32_t hName = 0, hClass = 0;
	DentVesselText d;
	std::vector<std::string> raw;            // section lines as read, the VESSEL line first, END_VESSEL last (dormant write-back)
	int skipped = 0;
};
struct CollTestRepair { double simt = 0; uint32_t occ = 0; std::string name; bool done = false; }; // TESTREPAIR, never written back
struct CollStoreBlock {                      // the parsed plugin block: E4 keeps it as the pending store until Begin
	bool found = false;                      // a COLLA header was read
	int version = 0;
	std::string recId; double recT0 = 0;     // loaded RECID: the playback link only
	std::vector<CollStoreVessel> vessel;
	std::vector<DentBaseText> base;          // BEGIN_XDMG_BASES content
	std::vector<std::string> unknown;        // unknown top-level lines, kept raw
	std::vector<CollTestRepair> testRepair;
	int skipped = 0;
};

struct CollLiveVessel { std::string name, cls; };                       // vessel index order
struct CollLiveObj { std::string planet, base, type; uint32_t obj; double x, z; };

namespace CollStore {
	typedef std::function<bool (std::string &line)> LineIn;            // false at END or end of file (oapiReadScenario_nextline)
	bool Parse (const LineIn &in, CollStoreBlock &out);                 // header first, else a forward scan past a foreign block (7.8)
	void ParseBody (const std::vector<std::string> &lines, CollStoreBlock &out); // lines after the header
	uint32_t Occ (const std::vector<CollLiveVessel> &live, size_t i, bool icase); // rank among equal name and class, index order
	std::vector<int> MatchVessels (const std::vector<CollStoreVessel> &saved, const std::vector<CollLiveVessel> &live); // live index per section, -1 dormant
	int MatchObj (const DentBaseObjText &o, const std::vector<CollLiveObj> &objs, const std::vector<uint8_t> &taken); // 7.4; -1 dormant
	bool SameBase (const DentBaseText &b, const std::string &planet, const std::string &base);
	bool Line200 (const std::string &l);                                // within DENT_LINE_MAX and free of ';'
}

// recorder side file (8.3)
struct CollSideAlias { uint32_t alias = 0, occ = 0; bool hashed = false; std::string name, cls; uint32_t hName = 0, hClass = 0; };
struct CollSideEvent {
	double t = 0; char kind = 0;             // D, S, R, B
	uint32_t alias = 0, recidx = 0;
	DentRecord rec {};                       // D
	double eabs = 0; uint32_t flags = 0;     // S, B
	uint32_t obj = 0; std::string base;      // B: object index, planet:base
};
struct CollSideFile { std::string id; std::vector<CollSideAlias> alias; std::vector<CollSideEvent> ev; int skipped = 0; };

namespace CollSide {
	std::string Header (const std::string &id);
	std::string Vdef (uint32_t alias, uint32_t occ, const std::string &name, const std::string &cls);
	void Dent (double t, uint32_t alias, uint32_t recidx, const DentRecord &r, std::vector<std::string> &lines);
	std::string State (double t, uint32_t alias, double eabs, uint32_t flags);
	std::string Repair (double t, uint32_t alias);
	std::string Building (double t, uint32_t alias, uint32_t obj, double eabs, uint32_t flags, const std::string &planetBase);
	bool Parse (const std::string &text, CollSideFile &out);            // a truncated last line is skipped
	std::string Fmt17 (double v);
}
#endif
