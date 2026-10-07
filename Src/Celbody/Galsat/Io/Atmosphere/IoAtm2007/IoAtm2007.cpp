// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.cpp

// ======================================================================
// class IoAtmosphere_2007
// Io atmosphere from the SO2 atmosphere review of Lellouch et al. 2007: about 1 nbar on the day side
// ======================================================================

#define ORBITER_MODULE
#include "IoAtm2007.h"
#include "../../../../common/layeratm.h"

// geometric altitude [m], temperature [K]
static const AtmNode NODE[] = {
	{0.0, 115.0}, // isothermal
};
static const double ATM_P0  = 0.0001; // surface pressure [Pa]
static const double ATM_R   = 129.8; // SO2 [J/(kg K)]
static const double ATM_RAD = 1.821e6; // Size in Io.cfg [m]
static const LayerAtm atm (NODE, 1, ATM_P0, ATM_R, ATM_RAD, 8.933e22*GGRAV/(ATM_RAD*ATM_RAD)); // Mass in Io.cfg [kg]

bool IoAtmosphere_2007::clbkConstants (ATMCONST *atmc) const
{
	atmc->p0       = ATM_P0;
	atmc->rho0     = ATM_P0/(ATM_R*NODE[0].T);
	atmc->R        = ATM_R;
	atmc->gamma    = 1.29;
	atmc->altlimit = 120e3;
	return true;
}

const char *IoAtmosphere_2007::clbkName () const
{
	return "2007 SO2 model";
}

bool IoAtmosphere_2007::clbkParams (const PRM_IN *prm_in, PRM_OUT *prm)
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
	return new IoAtmosphere_2007 (cbody);
}

DLLCLBK void DeleteAtmosphere (ATMOSPHERE *atm)
{
	delete (IoAtmosphere_2007*)atm;
}

DLLCLBK char *ModelName ()
{
	return (char*)"Io 2007 SO2 atmosphere model";
}

DLLCLBK char *ModelDesc ()
{
	return (char*)"A static Io atmosphere model in layers, from the SO2 atmosphere review of Lellouch et al. 2007: about 1 nbar on the day side.";
}
