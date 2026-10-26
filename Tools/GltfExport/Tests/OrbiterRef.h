// Copyright (c) 2026 racerx2
// Licensed under the MIT License
// GltfExport tests: the client's vVessel::Animate and AnimateComponent (VVessel.cpp) on plain data, as the reference

#pragma once
#include "GxTypes.h"
#include <cmath>
#include <vector>

struct RefMat { double m[4][4]; }; // D3DXMATRIX: row vectors, _41.._43 the translation

static inline RefMat RefIdentity ()
{
	RefMat r{};
	for (int i = 0; i < 4; i++) r.m[i][i] = 1;
	return r;
}

static inline RefMat RefRotationFromAxis (const GxVec3 &axis, double angle) // D3DMAT_RotationFromAxis
{
	angle *= 0.5;
	double w = std::cos (angle), sina = std::sin (angle);
	double x = sina * axis.x, y = sina * axis.y, z = sina * axis.z;
	double xx = x * x, yy = y * y, zz = z * z, xy = x * y, xz = x * z, yz = y * z, wx = w * x, wy = w * y, wz = w * z;
	RefMat r = RefIdentity ();
	r.m[0][0] = 1 - 2 * (yy + zz); r.m[0][1] = 2 * (xy + wz); r.m[0][2] = 2 * (xz - wy);
	r.m[1][0] = 2 * (xy - wz); r.m[1][1] = 1 - 2 * (xx + zz); r.m[1][2] = 2 * (yz + wx);
	r.m[2][0] = 2 * (xz + wy); r.m[2][1] = 2 * (yz - wx); r.m[2][2] = 1 - 2 * (xx + yy);
	return r;
}

static inline GxVec3 RefPoint (const GxVec3 &p, const RefMat &T) // TransformPoint
{
	return {p.x * T.m[0][0] + p.y * T.m[1][0] + p.z * T.m[2][0] + T.m[3][0],
		p.x * T.m[0][1] + p.y * T.m[1][1] + p.z * T.m[2][1] + T.m[3][1],
		p.x * T.m[0][2] + p.y * T.m[1][2] + p.z * T.m[2][2] + T.m[3][2]};
}

static inline GxVec3 RefDir (const GxVec3 &d, const RefMat &T, bool normalise) // TransformDirection
{
	GxVec3 r{d.x * T.m[0][0] + d.y * T.m[1][0] + d.z * T.m[2][0],
		d.x * T.m[0][1] + d.y * T.m[1][1] + d.z * T.m[2][1],
		d.x * T.m[0][2] + d.y * T.m[1][2] + d.z * T.m[2][2]};
	if (normalise) {
		double l = std::sqrt (r.x * r.x + r.y * r.y + r.z * r.z);
		if (l > 0) { r.x /= l; r.y /= l; r.z /= l; }
	}
	return r;
}

struct RefVessel {
	GxAnimSet live;                                   // parameters as the visual leaves them
	std::vector<std::vector<std::vector<GxVec3>>> v;  // [mesh][group][vertex], mesh local
	std::vector<double> current;                      // the visual's currentstate

	void Start ()
	{
		current.clear ();
		for (const GxAnim &a : live.anims) current.push_back (a.defstate);
	}

	void AnimateComponent (int c, const RefMat &T)
	{
		GxComp &k = live.comps[c];
		if (k.mesh >= 0 && k.mesh < (int)v.size ()) {
			auto &mesh = v[k.mesh];
			if (!k.allGroups) {
				for (int g : k.groups) if (g >= 0 && g < (int)mesh.size ()) for (GxVec3 &p : mesh[g]) p = RefPoint (p, T);
			} else {
				for (auto &grp : mesh) for (GxVec3 &p : grp) p = RefPoint (p, T);
			}
		}
		for (int ch : k.children) {
			AnimateComponent (ch, T);
			GxComp &c2 = live.comps[ch];
			switch (c2.type) {
			case GxType::Rotate: c2.ref = RefPoint (c2.ref, T); c2.axis = RefDir (c2.axis, T, true); break;
			case GxType::Translate: c2.shift = RefDir (c2.shift, T, false); break;
			case GxType::Scale: c2.ref = RefPoint (c2.ref, T); break;
			default: break;
			}
		}
	}

	void Animate (int an, double state)
	{
		GxAnim &A = live.anims[an];
		A.state = state;
		size_t n = A.comps.size ();
		for (size_t ii = 0; ii < n; ii++) {
			size_t i = (A.state > current[an] ? ii : n - ii - 1);
			int c = A.comps[i];
			GxComp &AC = live.comps[c];
			double s0 = current[an];
			if (s0 < AC.state0) s0 = AC.state0; else if (s0 > AC.state1) s0 = AC.state1;
			double s1 = A.state;
			if (s1 < AC.state0) s1 = AC.state0; else if (s1 > AC.state1) s1 = AC.state1;
			double ds = s1 - s0;
			if (ds == 0) continue;
			ds /= (AC.state1 - AC.state0);
			RefMat T = RefIdentity ();
			switch (AC.type) {
			case GxType::Rotate: {
				T = RefRotationFromAxis (AC.axis, ds * AC.angle);
				double dx = AC.ref.x, dy = AC.ref.y, dz = AC.ref.z;
				T.m[3][0] = dx - T.m[0][0] * dx - T.m[1][0] * dy - T.m[2][0] * dz;
				T.m[3][1] = dy - T.m[0][1] * dx - T.m[1][1] * dy - T.m[2][1] * dz;
				T.m[3][2] = dz - T.m[0][2] * dx - T.m[1][2] * dy - T.m[2][2] * dz;
			} break;
			case GxType::Translate:
				T.m[3][0] = ds * AC.shift.x; T.m[3][1] = ds * AC.shift.y; T.m[3][2] = ds * AC.shift.z;
				break;
			case GxType::Scale: {
				s0 = (s0 - AC.state0) / (AC.state1 - AC.state0);
				s1 = (s1 - AC.state0) / (AC.state1 - AC.state0);
				T.m[0][0] = (s1 * (AC.scale.x - 1) + 1) / (s0 * (AC.scale.x - 1) + 1);
				T.m[1][1] = (s1 * (AC.scale.y - 1) + 1) / (s0 * (AC.scale.y - 1) + 1);
				T.m[2][2] = (s1 * (AC.scale.z - 1) + 1) / (s0 * (AC.scale.z - 1) + 1);
				T.m[3][0] = AC.ref.x * (1 - T.m[0][0]);
				T.m[3][1] = AC.ref.y * (1 - T.m[1][1]);
				T.m[3][2] = AC.ref.z * (1 - T.m[2][2]);
			} break;
			default:
				break;
			}
			AnimateComponent (c, T);
		}
		current[an] = state;
	}
};
