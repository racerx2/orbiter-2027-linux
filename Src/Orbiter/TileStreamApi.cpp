// not upstream: OAPI entry points of the planet tile stream (TileStream.h)

#include "Orbiter.h"
#include "Body.h"
#include "TileStream.h"
#include "OrbiterAPI.h"
#include <fstream>
#include <iterator>

extern Orbiter *g_pOrbiter;

static TileStream *Stream ()
{
	return g_pOrbiter ? g_pOrbiter->Stream() : nullptr;
}

DLLEXPORT bool oapiTileStreamEnabled (OBJHANDLE hBody)
{
	TileStream *ts = Stream();
	return ts && hBody && ts->Enabled (((Body*)hBody)->Name());
}

DLLEXPORT int oapiTileStreamWant (OBJHANDLE hBody, int lvl, int ilat, int ilng, int flags)
{
	TileStream *ts = Stream();
	return ts && hBody ? ts->Want (((Body*)hBody)->Name(), lvl, ilat, ilng, flags) : -1;
}

// from the cfg, not the running stream: elevmgr decides before the stream starts, and both must agree
int TsSessionElev (const std::string &body)
{
	if (!g_pOrbiter || !g_pOrbiter->GetGraphicsClient() || !g_pOrbiter->Cfg()->bTileStream) return 0;
	return TsCfgElev (oapiResolvePath (g_pOrbiter->ConfigPath ("TileStream")), body);
}

DLLEXPORT int oapiTileStreamElev (OBJHANDLE hBody)
{
	return hBody ? TsSessionElev (((Body*)hBody)->Name()) : 0;
}

DLLEXPORT int oapiTileStreamDone (OBJHANDLE hBody, int *keys, int nmax)
{
	TileStream *ts = Stream();
	return ts && hBody ? ts->Done (((Body*)hBody)->Name(), keys, nmax) : 0;
}

DLLEXPORT int oapiTileStreamBldgWant (OBJHANDLE hBody, int x, int y)
{
	TileStream *ts = Stream();
	return ts && hBody ? ts->WantBldg (((Body*)hBody)->Name(), x, y) : -1;
}

namespace {
struct BldgHold { TSBLDGMESH m; TsBldg::Mesh mesh; std::vector<TSBLDG> b; };
}

DLLEXPORT TSBLDGMESH *oapiTileStreamBldgLoad (OBJHANDLE hBody, int x, int y)
{
	TileStream *ts = Stream();
	if (!ts || !hBody) return nullptr;
	std::string path = ts->BldgPath (((Body*)hBody)->Name(), x, y);
	std::ifstream f (path, std::ios::binary);
	if (path.empty() || !f) return nullptr;
	std::string bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
	auto h = new BldgHold;
	if (!TsBldg::Read (bytes, h->mesh)) { delete h; return nullptr; }
	for (const TsBldg::Building &s : h->mesh.b) h->b.push_back ({s.lat, s.lng, s.hmin, s.hmax, s.v0, s.nv, s.i0, s.ni});
	h->m = {h->mesh.z, h->mesh.x, h->mesh.y, (DWORD)h->b.size(), (DWORD)(h->mesh.vtx.size() / 6), (DWORD)h->mesh.idx.size(),
		h->b.data(), h->mesh.vtx.data(), (const DWORD*)h->mesh.idx.data()};
	return &h->m;
}

DLLEXPORT void oapiTileStreamBldgFree (TSBLDGMESH *mesh)
{
	delete (BldgHold*)mesh; // m is the first member
}

// not upstream (design-ways)
#define WAYKIND(n) static_assert ((int)TsWay::n == (int)TSWAY_##n, "TsWay::Kind and the SDK's TSWAY kinds must match");
WAYKIND(NONE) WAYKIND(MOTORWAY) WAYKIND(TRUNK) WAYKIND(PRIMARY) WAYKIND(SECONDARY) WAYKIND(TERTIARY) WAYKIND(MINOR) WAYKIND(SERVICE)
WAYKIND(DRIVEWAY) WAYKIND(PARKING) WAYKIND(TRACK) WAYKIND(BUSWAY) WAYKIND(RACEWAY) WAYKIND(FOOTWAY) WAYKIND(CYCLEWAY) WAYKIND(PATH)
WAYKIND(STEPS) WAYKIND(PEDESTRIAN) WAYKIND(PLATFORM) WAYKIND(PIER) WAYKIND(RAIL) WAYKIND(TRAM) WAYKIND(RUNWAY) WAYKIND(TAXIWAY)
WAYKIND(APRON) WAYKIND(HELIPAD) WAYKIND(RIVER) WAYKIND(CANAL) WAYKIND(STREAM) WAYKIND(DRAIN) WAYKIND(DITCH) WAYKIND(NKIND)
#undef WAYKIND
static_assert (TsWay::BRIDGE == TSWAY_BRIDGE && TsWay::UNPAVED == TSWAY_UNPAVED && TsWay::ONEWAY == TSWAY_ONEWAY && TsWay::RAMP == TSWAY_RAMP && TsWay::AREA == TSWAY_AREA, "TsWay::Flag and the SDK's TSWAY flags must match");

DLLEXPORT int oapiTileStreamWayWant (OBJHANDLE hBody, int x, int y)
{
	TileStream *ts = Stream();
	return ts && hBody ? ts->WantWay (((Body*)hBody)->Name(), x, y) : -1;
}

namespace {
struct WayHold { TSWAYTILE t; std::vector<TSWAY> f; std::vector<DWORD> part; std::vector<short> pt; };
}

DLLEXPORT TSWAYTILE *oapiTileStreamWayLoad (OBJHANDLE hBody, int x, int y)
{
	TileStream *ts = Stream();
	if (!ts || !hBody) return nullptr;
	std::string path = ts->WayPath (((Body*)hBody)->Name(), x, y);
	std::ifstream f (path, std::ios::binary);
	if (path.empty() || !f) return nullptr;
	std::string bytes ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
	TsWay::Tile t;
	if (!TsWay::Read (bytes, t) || t.z != 14 || t.x != x || t.y != y) return nullptr;
	auto h = new WayHold;
	for (const TsWay::Feature &F : t.f) {
		TSWAY w {};
		w.kind = F.kind;
		w.flags = F.flags;
		w.layer = F.layer;
		F.ref.copy (w.ref, sizeof w.ref - 1);
		w.p0 = (DWORD)h->part.size();
		w.np = (DWORD)F.parts.size();
		for (const auto &p : F.parts) {
			h->part.push_back ((DWORD)(h->pt.size() / 2));
			h->pt.insert (h->pt.end(), p.begin(), p.end());
		}
		h->f.push_back (w);
	}
	DWORD np = (DWORD)h->part.size();
	h->part.push_back ((DWORD)(h->pt.size() / 2));
	h->t = {t.z, t.x, t.y, (DWORD)h->f.size(), np, (DWORD)(h->pt.size() / 2), h->f.data(), h->part.data(), h->pt.data()};
	return &h->t;
}

DLLEXPORT void oapiTileStreamWayFree (TSWAYTILE *tile)
{
	delete (WayHold*)tile; // t is the first member
}
