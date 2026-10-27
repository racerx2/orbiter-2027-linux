// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from TitanAtm2005.h

#ifndef __PLUTOATM2017_H
#define __PLUTOATM2017_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class PlutoAtmosphere_2017
// Pluto atmosphere model, the one the Pluto module has built in
// ======================================================================

class PlutoAtmosphere_2017: public ATMOSPHERE {
public:
	PlutoAtmosphere_2017 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__PLUTOATM2017_H
