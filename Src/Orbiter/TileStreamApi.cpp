// not upstream: OAPI entry points of the planet tile stream (TileStream.h)

#include "Orbiter.h"
#include "Body.h"
#include "TileStream.h"
#include "OrbiterAPI.h"

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
