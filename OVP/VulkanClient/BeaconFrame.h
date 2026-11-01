// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream (stream-rwylights): the frame a beacon array's lights are set on the terrain in, a base's or a streamed runway's

#ifndef __BEACONFRAME_H
#define __BEACONFRAME_H

#include "OrbiterAPI.h"
#include "D3DXMath.h"

class BeaconFrame {
public:
	virtual ~BeaconFrame () {}
	virtual VECTOR3 ToLocal (VECTOR3 pos, double *lng = NULL, double *lat = NULL) const = 0; // to the planet's time-invariant frame
	virtual void FromLocal (VECTOR3 pos, D3DXVECTOR3 *pTgt) const = 0;                         // and back
};

#endif // !__BEACONFRAME_H
