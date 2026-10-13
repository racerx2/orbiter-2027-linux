// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2026 racerx2
// ==============================================================
// not upstream: pure maths of the TAA pass, unit tested (design-visual-batch 13)

#ifndef __POSTMATH_H
#define __POSTMATH_H

namespace PostMath {

double Halton (int i, int b); // radical inverse of i in base b, i >= 1

}

#endif // !__POSTMATH_H
