// not upstream: Charon (Pluto I) ephemeris module: a series fitted to JPL's plu060, 1800-01-02 to 2199-12-30
// Model: Config/Charon/Data/Charon.psr (../common/psrseries.h); outside the span its two-body orbit about Pluto
#include "../common/psrmodule.h"

static const PsrPart PART = {"Config/Charon/Data/Charon.psr", 975.4272}; // GM of Pluto and Charon [km^3/s^2], its relative orbit

class Charon final : public PsrModule { public: Charon() : PsrModule(&PART, 1, nullptr, -21503.0, 124591.0, 64.0/1440.0) {} };

MODEXPORT void     InitModule(void *hModule)  { (void)hModule; }
MODEXPORT void     ExitModule(void *hModule)  { (void)hModule; }
MODEXPORT CELBODY* InitInstance(void)         { return new Charon(); }
MODEXPORT void     ExitInstance(void *body)   { delete static_cast<Charon*>((CELBODY*)body); }
MODEXPORT int      GetModuleVersion(void)     { return 20261006; }
