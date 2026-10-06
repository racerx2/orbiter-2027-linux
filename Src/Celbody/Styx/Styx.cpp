// not upstream: Styx (Pluto V) ephemeris module: a series fitted to JPL's plu060, 1800-01-02 to 2199-12-30
// Model: Config/Styx/Data/Styx.psr (../common/psrseries.h); outside the span two-body motion about the system barycentre
#include "../common/psrmodule.h"

static const PsrPart PART = {"Config/Styx/Data/Styx.psr", 975.4309}; // GM of the Pluto system [km^3/s^2], about its barycentre
static const PsrPart REF = {"Config/Pluto/Data/PlutoOff.psr", 1.255364767398755}; // Pluto about the barycentre

class Styx final : public PsrModule { public: Styx() : PsrModule(&PART, 1, &REF, -21503.0, 124591.0, 128.0/1440.0) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Styx(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Styx*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20261006; }
