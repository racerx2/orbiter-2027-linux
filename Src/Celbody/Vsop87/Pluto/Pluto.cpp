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

// the N2 atmosphere from New Horizons (Gladstone et al. 2016, Hinson et al. 2017): geometric altitude [m], temperature [K]
static const AtmNode ATM[] = {
	{  0.0,  38.0}, // boundary layer
	{  4e3,  38.0}, // inversion
	{ 14e3, 100.0},
	{ 30e3, 108.0}, // stratopause
	{200e3,  74.0},
};
static const double ATM_RAD = 1.1883e6; // Size in Pluto.cfg [m]
static const LayerAtm atm (ATM, 5, 1.15, 296.8, ATM_RAD, 869.3261e9/(ATM_RAD*ATM_RAD)); // surface pressure [Pa], N2 [J/(kg K)], GM [m^3/s^2]

// ======================================================================
// class Pluto: implementation
// ======================================================================

Pluto::Pluto (): PsrModule (PARTS, 2, nullptr, -21503.0, 124591.0, 128.0/1440.0)
{
}

bool Pluto::clbkAtmParam (double alt, ATMPARAM *prm)
{
	atm.Get (alt, prm->T, prm->p, prm->rho);
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
