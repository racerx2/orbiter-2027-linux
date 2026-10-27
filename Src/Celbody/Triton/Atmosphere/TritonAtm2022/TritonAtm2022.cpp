// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from TitanAtm2005.cpp

// ======================================================================
// class TritonAtmosphere_2022
// Triton atmosphere for ephemeris modules without one of their own (spice)
// ======================================================================

#define ORBITER_MODULE
#include "TritonAtm2022.h"
#include "../../TritonAtm.h"

static const LayerAtm atm (TRITON_ATM, TRITON_ATM_N, TRITON_ATM_P0, TRITON_ATM_R, TRITON_ATM_RAD, TRITON_ATM_G0);

bool TritonAtmosphere_2022::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = TRITON_ATM_P0;
	atmc->rho0     = TRITON_ATM_P0/(TRITON_ATM_R*TRITON_ATM[0].T);
	atmc->R        = TRITON_ATM_R;
	atmc->gamma    = 1.4;
	atmc->altlimit = 600e3;
	return true;
}

const char *TritonAtmosphere_2022::clbkName () const
{
	return "2022 Triton model";
}

bool TritonAtmosphere_2022::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
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
	return new TritonAtmosphere_2022 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (TritonAtmosphere_2022*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Triton 2022 atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Triton atmosphere model in layers, from Voyager 2 and the stellar occultations of 2017-2022.";
}
