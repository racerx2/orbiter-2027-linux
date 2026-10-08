// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from MarsAtm2006.h

#ifndef __SATURNATM2017_H
#define __SATURNATM2017_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class SaturnAtmosphere_2017
// Saturn atmosphere: 2017 Cassini model
// ======================================================================

class SaturnAtmosphere_2017: public ATMOSPHERE {
public:
	SaturnAtmosphere_2017 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__SATURNATM2017_H
