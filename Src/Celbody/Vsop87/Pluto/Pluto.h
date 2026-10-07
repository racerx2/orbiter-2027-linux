// Copyright (c) Martin Schweiger
// Licensed under the MIT License
// not upstream: converted from Jupiter.h for the Pluto system

#ifndef __VSOP87_PLUTO
#define __VSOP87_PLUTO

#include "../../common/psrmodule.h"
#include "../../common/layeratm.h"

// ======================================================================
// class Pluto: interface
// ======================================================================

class Pluto: public PsrModule {
public:
	Pluto ();
	bool clbkAtmParam (double alt, ATMPARAM *prm) override;
};

#endif // !__VSOP87_PLUTO
