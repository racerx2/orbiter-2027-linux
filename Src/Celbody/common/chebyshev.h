// Orbiter 2027: Chebyshev-based moon module, with analytic fitted-orbit fallback.
// Existing Nereid and Vesta interface remains unchanged.
#pragma once
#include "chebtable.h"

class ChebModule : public MoonModule {
    ChebTable table_;
public:
    ChebModule(const char *file, const eph::Elem &el, const eph::Planet &pl,
               const eph::Term *terms, int count)
        : MoonModule(el, pl, terms, count), table_(file) {}
    ChebModule(const char *file, const eph::Elem &el, const eph::Planet &pl)
        : MoonModule(el, pl), table_(file) {}

    void clbkInit(FILEHANDLE cfg) override
    {
        CELBODY::clbkInit(cfg);
        table_.load();
    }
    int clbkEphemeris(double mjd, int req, double *ret) override
    {
        return table_.sample(mjd, ret) ? 0x1f :
               MoonModule::clbkEphemeris(mjd, req, ret);
    }
    int clbkFastEphemeris(double simt, int req, double *ret) override
    {
        return clbkEphemeris(oapiTime2MJD(simt), req, ret);
    }
};
