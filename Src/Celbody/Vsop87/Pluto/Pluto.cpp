// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from Jupiter.cpp; Pluto from series fitted to JPL's DE441 and plu060, 1800-01-02 to 2199-12-30

#define ORBITER_MODULE

#include "Pluto.h"

// the Pluto system's barycentre wrt the Sun (GM of the Sun and the system), then Pluto wrt the barycentre (GM_Charon^3/(GM_Pluto+GM_Charon)^2)
static const PsrPart PARTS[] = {
	{"Config/Pluto/Data/PlutoB.psr", 132712441016.779419},
	{"Config/Pluto/Data/PlutoOff.psr", 1.255364767398755},
};

// the N2 atmosphere from New Horizons (Gladstone et al. 2016, Hinson et al. 2017): layers in geopotential altitude [m], base temperature [K], lapse rate [K/m]
static const struct { double z, T, a; } ATM[] = {
	{  0.0,  38.0,  0.0},    // boundary layer
	{  4e3,  38.0,  6.2e-3}, // inversion
	{ 14e3, 100.0,  0.5e-3},
	{ 30e3, 108.0, -0.2e-3}, // above the stratopause
	{200e3,  74.0,  0.0},
};
static const int NATM = sizeof (ATM)/sizeof (ATM[0]);
static const double ATM_P0 = 1.15;                              // surface pressure [Pa]
static const double ATM_R = 296.8;                              // specific gas constant of N2 [J/(kg K)]
static const double ATM_RAD = 1.1883e6;                         // Size in Pluto.cfg [m]
static const double ATM_G0 = 869.3261e9/(ATM_RAD*ATM_RAD);      // surface gravity from GM [m/s^2]

// pressure at geopotential altitude z in layer i from the layer's base pressure; the temperature in T
static double LayerP (int i, double pbase, double z, double &T)
{
	double dz = z - ATM[i].z;
	T = ATM[i].T + ATM[i].a*dz;
	if (ATM[i].a == 0.0) return pbase*exp (-ATM_G0/(ATM_R*ATM[i].T)*dz);
	return pbase*pow (T/ATM[i].T, -ATM_G0/(ATM_R*ATM[i].a));
}

// ======================================================================
// class Pluto: implementation
// ======================================================================

Pluto::Pluto (): PsrModule (PARTS, 2, nullptr, -21503.0, 124591.0, 128.0/1440.0)
{
	static_assert (NATM == sizeof (pb)/sizeof (pb[0]), "one base pressure per layer");
	double T;
	pb[0] = ATM_P0;
	for (int i = 1; i < NATM; i++) pb[i] = LayerP (i-1, pb[i-1], ATM[i].z, T);
}

bool Pluto::clbkAtmParam (double alt, ATMPARAM *prm)
{
	double z = alt*ATM_RAD/(ATM_RAD + alt); // geopotential altitude
	int i = NATM - 1;
	while (i > 0 && z < ATM[i].z) i--;
	prm->p = LayerP (i, pb[i], z, prm->T);
	prm->rho = prm->p/(ATM_R*prm->T);
	return true;
}

// ======================================================================
// API interface
// ======================================================================

DLLCLBK void InitModule (void *hModule)
{}

DLLCLBK void ExitModule (void *hModule)
{}

DLLCLBK CELBODY *InitInstance (OBJHANDLE hBody)
{
	return new Pluto ();
}

DLLCLBK void ExitInstance (CELBODY *body)
{
	delete (Pluto*)body;
}
