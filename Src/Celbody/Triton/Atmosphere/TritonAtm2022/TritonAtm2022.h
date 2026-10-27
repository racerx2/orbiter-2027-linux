// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from TitanAtm2005.h

#ifndef __TRITONATM2022_H
#define __TRITONATM2022_H

#include "OrbiterAPI.h"
#include "CelBodyAPI.h"

// ======================================================================
// class TritonAtmosphere_2022
// Triton atmosphere model, the one the Triton module has built in
// ======================================================================

class TritonAtmosphere_2022: public ATMOSPHERE {
public:
	TritonAtmosphere_2022 (CELBODY2 *body): ATMOSPHERE (body) {}
	const char *clbkName () const;
	bool clbkConstants (ATMCONST *atmc) const;
	bool clbkParams (const PRM_IN *prm_in, PRM_OUT *prm);
};

#endif // !__TRITONATM2022_H
