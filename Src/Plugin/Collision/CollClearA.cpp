// not upstream: collision addon, clearance queries of the exported API (design autodock-clearance 1)
#include <algorithm>
#include <cmath>
#include "CollDamageA.h"
#include "CollisionAPI.h"

namespace {

bool Finite (const Vector &v) { return std::isfinite (v.x) && std::isfinite (v.y) && std::isfinite (v.z); }

// a rotation within 1e-6 (callers build it from vessel axes in double)
bool Rotation (const Matrix &R)
{
	for (int i = 0; i < 9; i++) if (!std::isfinite (R.data[i])) return false;
	const double *a = R.data;
	for (int i = 0; i < 3; i++)
		for (int j = 0; j < 3; j++) {
			double d = a[i]*a[j] + a[3+i]*a[3+j] + a[6+i]*a[6+j] - (i == j ? 1.0 : 0.0);
			if (std::fabs (d) > 1e-6) return false;
		}
	double det = a[0]*(a[4]*a[8] - a[5]*a[7]) - a[1]*(a[3]*a[8] - a[5]*a[6]) + a[2]*(a[3]*a[7] - a[4]*a[6]);
	return det > 0.0;
}

// len and clearance usable, dir made unit; with len 0 dir is not used and may be zero
bool Args (const Vector &dir, double len, double clearance, Vector &u)
{
	if (!std::isfinite (len) || !(len >= 0.0) || !std::isfinite (clearance) || !(clearance >= 0.0) || !Finite (dir)) return false;
	double l = dir.length ();
	if (l > 0.0 && std::isfinite (l)) { u = dir / l; return true; }
	u = Vector ();
	return len == 0.0;
}

}

const CollShape *CollDmgSession::ClearShape (CollH h)
{
	if (!h || !sdk.IsVessel (h)) return nullptr;
	const CollShape *s = host.Shape (host.IdOf (h));
	return s && s->nPart () ? s : nullptr;
}

int CollDmgSession::SweepVessel (CollH hTarget, CollH hMover, const Vector &pos, const Matrix &rot, const Vector &dir, double len, double clearance, double &dist)
{
	dist = 0.0;
	Vector u;
	if (hTarget == hMover || !Finite (pos) || !Rotation (rot) || !Args (dir, len, clearance, u)) return -1;
	const CollShape *T = ClearShape (hTarget), *M = ClearShape (hMover);
	if (!T || !M) return -1;
	CollAffine X;
	X.A = rot;
	auto gap = [&](double s, double cap) { X.t = pos + u * s; return T->Gap (*M, X, cap, clearScratch); };
	return CollSweep (gap, len, clearance, COLLA_SWEEP_STEP, dist) ? 1 : 0;
}

int CollDmgSession::SweepSphere (CollH hTarget, const Vector &centre, double radius, const Vector &dir, double len, double clearance, double &dist)
{
	dist = 0.0;
	Vector u;
	if (!Finite (centre) || !std::isfinite (radius) || !(radius >= 0.0) || !Args (dir, len, clearance, u)) return -1;
	const CollShape *T = ClearShape (hTarget);
	if (!T) return -1;
	auto gap = [&](double s, double cap) { return std::max (T->PointGap (centre + u * s, cap + radius) - radius, 0.0); };
	return CollSweep (gap, len, clearance, COLLA_SWEEP_STEP, dist) ? 1 : 0;
}

int CollDmgSession::VesselRadius (CollH h, double &radius)
{
	radius = 0.0;
	const CollShape *s = ClearShape (h);
	if (!s) return -1;
	Vector c;
	s->Bound (1, c, radius);
	return 1;
}
