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

static const LayerAtm atm (PLUTO_ATM, PLUTO_ATM_N, PLUTO_ATM_P0, PLUTO_ATM_R, PLUTO_ATM_RAD, PLUTO_ATM_G0); // PlutoAtm.h, shared with PlutoAtm2017

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
