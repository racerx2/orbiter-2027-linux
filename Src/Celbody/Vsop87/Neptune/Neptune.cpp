// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// Neptune system barycentre: JPL Horizons #8 / Sun #10, J2000 ecliptic, TDB.
// Orbiter derives the physical centre from its existing moon models.
#define ORBITER_MODULE

#include "Neptune.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr double TransitionDays = 365.25;

// VSOP87B polar AU/radians -> Orbiter Cartesian metres.
void polarToCartesian(const double *polar, double *out)
{
    const double lambda=polar[0], latitude=polar[1];
    const double c=std::cos(lambda), s=std::sin(lambda);
    const double cb=std::cos(latitude), sb=std::sin(latitude);
    const double r=polar[2]*AU, dr=polar[5]*AU;
    const double lambdaRate=polar[3], latitudeRate=polar[4];

    const double rcos=r*cb;
    out[0]=rcos*c;
    out[1]=r*sb;
    out[2]=rcos*s;
    out[3]=cb*c*dr - r*sb*c*latitudeRate - rcos*s*lambdaRate;
    out[4]=sb*dr + r*cb*latitudeRate;
    out[5]=cb*s*dr - r*sb*s*latitudeRate + rcos*c*lambdaRate;
}
}

Neptune::Neptune(OBJHANDLE body)
    : VSOPOBJ(body), jpl_("Config/Neptune/Data/neptune.cheb")
{
    a0=30.1;  // mean semi-major axis [AU] used by VSOP87 truncation
}

void Neptune::clbkInit(FILEHANDLE cfg)
{
    VSOPOBJ::clbkInit(cfg);
    ReadData("Neptune"); // fallback; also provides ephemerides outside 1800-2200
    jpl_.load();
}

int Neptune::tableOrVsop(double mjd, double *ret)
{
    double state[12];
    if (!jpl_.sample(mjd, state)) {
        VsopEphem(mjd, ret+6);
        return fmtflag | EPHEM_BARYPOS | EPHEM_BARYVEL;
    }

    // Quintic blend preserves position and velocity at table edges.
    const double edge0=(mjd-jpl_.startMjd())/TransitionDays;
    const double edge1=(jpl_.endMjd()-mjd)/TransitionDays;
    const double edge=std::min(edge0,edge1);
    if (edge < 1.0) {
        const double e=std::max(0.0,edge);
        const double w=e*e*e*(10.0+e*(-15.0+6.0*e));
        const double dwde=30.0*e*e*(1.0-e)*(1.0-e);
        const double de_dt=(edge0 <= edge1 ? 1.0 : -1.0) /
                           (TransitionDays*86400.0);
        const double wdot=dwde*de_dt;

        double pol[6], vsop[6];
        VsopEphem(mjd,pol);
        polarToCartesian(pol,vsop);
        for (int j=0;j<3;++j) {
            const double difference=state[j]-vsop[j];
            ret[j+6]=vsop[j]+w*difference;
            ret[j+9]=(1.0-w)*vsop[j+3]+w*state[j+3]+wdot*difference;
        }
    } else {
        std::memcpy(ret+6,state,6*sizeof(double));
    }

    // Return the system barycentre; core handles moon-mass offset.
    return EPHEM_BARYPOS | EPHEM_BARYVEL;
}

int Neptune::clbkEphemeris(double mjd, int req, double *ret)
{
    return tableOrVsop(mjd,ret);
}

int Neptune::clbkFastEphemeris(double simt, int req, double *ret)
{
    const double mjd=oapiTime2MJD(simt);
    if (jpl_.covers(mjd))
        return tableOrVsop(mjd,ret);
    VsopFastEphem(simt,ret+6);
    return fmtflag | EPHEM_BARYPOS | EPHEM_BARYVEL;
}

DLLCLBK void InitModule(void *hModule) {}
DLLCLBK void ExitModule(void *hModule) {}

DLLCLBK CELBODY *InitInstance(OBJHANDLE hBody)
{
    return new Neptune(hBody);
}

DLLCLBK void ExitInstance(CELBODY *body)
{
    delete (Neptune*)body;
}
