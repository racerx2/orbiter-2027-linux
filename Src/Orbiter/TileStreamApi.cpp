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
