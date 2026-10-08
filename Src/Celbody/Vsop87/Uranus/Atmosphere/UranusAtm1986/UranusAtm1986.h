// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.h

#ifndef __URANUSATM1986_H
#define __URANUSATM1986_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class UranusAtmosphere_1986
// Uranus atmosphere: 1986 Voyager 2 model
// ======================================================================

class UranusAtmosphere_1986: public ATMOSPHERE {
public:
	UranusAtmosphere_1986 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__URANUSATM1986_H
