// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the look passes (flare, haze, sharpen, filmic), unit tested (design-visual-batch 13)

#ifndef __LOOKMATH_H
#define __LOOKMATH_H

namespace LookMath {

double Filmic (double x); // Scatter.glsl's _FILMIC curve: ACES fit with the input scaled by 0.568

}

#endif // !__LOOKMATH_H
