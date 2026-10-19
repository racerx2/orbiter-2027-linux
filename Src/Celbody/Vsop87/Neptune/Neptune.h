// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// Neptune: JPL Horizons barycentric Chebyshev table, with VSOP87 fallback.

#ifndef __VSOP87_NEPTUNE
#define __VSOP87_NEPTUNE

#include "Vsop87.h"
#include "../../common/chebtable.h"

class Neptune : public VSOPOBJ {
    ChebTable jpl_;

    int tableOrVsop(double mjd, double *ret);

public:
    Neptune(OBJHANDLE hCBody);
    void clbkInit(FILEHANDLE cfg) override;
    int clbkEphemeris(double mjd, int req, double *ret) override;
    int clbkFastEphemeris(double simt, int req, double *ret) override;
};

#endif // !__VSOP87_NEPTUNE
