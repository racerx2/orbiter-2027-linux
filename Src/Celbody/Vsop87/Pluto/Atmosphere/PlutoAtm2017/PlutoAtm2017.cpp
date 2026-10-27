// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from TitanAtm2005.cpp

// ======================================================================
// class PlutoAtmosphere_2017
// Pluto atmosphere for ephemeris modules without one of their own (spice)
// ======================================================================

#define ORBITER_MODULE
#include "PlutoAtm2017.h"
#include "../../PlutoAtm.h"

static const LayerAtm atm (PLUTO_ATM, PLUTO_ATM_N, PLUTO_ATM_P0, PLUTO_ATM_R, PLUTO_ATM_RAD, PLUTO_ATM_G0);

bool PlutoAtmosphere_2017::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = PLUTO_ATM_P0;
	atmc->rho0     = PLUTO_ATM_P0/(PLUTO_ATM_R*PLUTO_ATM[0].T);
	atmc->R        = PLUTO_ATM_R;
	atmc->gamma    = 1.4;
	atmc->altlimit = 600e3;
	return true;
}

const char *PlutoAtmosphere_2017::clbkName () const
{
	return "2017 Pluto model";
}

bool PlutoAtmosphere_2017::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
{
	double alt = (prm_in->flag & PRM_ALT ? prm_in->alt : 0.0);
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

DLLCLBK ATMOSPHERE *CreateAtmosphere (CELBODY2 *cbody)
{
	return new PlutoAtmosphere_2017 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (PlutoAtmosphere_2017*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Pluto 2017 atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Pluto atmosphere model in layers, from the New Horizons radio and solar occultations.";
}
