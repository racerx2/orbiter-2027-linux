// not upstream (head-track): ported from linuxtrack's pose.c, tracking.c and pref_global.c's models (MIT, LICENSE-linuxtrack); the iterative solver and the new rotation, as linuxtrack's defaults

#include "HtPose.h"
#include "HtMath.h"
#include <cmath>
#include <utility>

namespace ht {

Model CapModel (double x, double y, double z, double hy, double hz)
{
	Model m;
	m.type = Model::CAP;
	m.p0[0] = 0, m.p0[1] = y, m.p0[2] = 0;
	m.p1[0] = -x/2, m.p1[1] = 0, m.p1[2] = -z;
	m.p2[0] = +x/2, m.p2[1] = 0, m.p2[2] = -z;
	m.hc[0] = 0.0, m.hc[1] = -hy, m.hc[2] = hz;
	return m;
}

Model ClipModel (double y1, double y2, double z1, double z2, double hx, double hy, double hz)
{
	Model m; // y1, z1: the top point to the middle one; y2, z2: the top point to the bottom one
	m.type = Model::CLIP;
	m.p0[0] = 0, m.p0[1] = y1, m.p0[2] = -z1;
	m.p1[0] = 0, m.p1[1] = 0, m.p1[2] = 0;
	m.p2[0] = 0, m.p2[1] = y1 - y2, m.p2[2] = -z1 - z2;
	m.hc[0] = hx, m.hc[1] = hy, m.hc[2] = hz;
	return m;
}

bool PoseSolver::Init (const Model &rm, double pfocal)
{
	type = rm.type;
	focal = pfocal;
	MakeVec (rm.p0, rm.hc, mp0);
	MakeVec (rm.p1, rm.hc, mp1);
	MakeVec (rm.p2, rm.hc, mp2);
	double vec1[3], vec2[3], base[3][3];
	MakeVec (mp1, mp0, vec1);
	MakeVec (mp2, mp0, vec2);
	MakeBase (vec1, vec2, base);
	if (!IsMatrixFinite (base)) return false; // dimensions that make no base
	return GetCbase (rm.p0, rm.p1, rm.p2, rm.hc, c_base) && Center (rm.p0, rm.p1, rm.p2, c_base, tr_center, tr_rot);
}

void PoseSolver::RemoveCameraRotation (std::vector<Blob> &blobs, int orientation) const
{
	for (Blob &b : blobs) {
		float x = b.x, y = b.y;
		if (orientation & ORIENT_XCHG_XY) std::swap (x, y);
		b.x = (orientation & ORIENT_FLIP_X) ? -x : x;
		b.y = (orientation & ORIENT_FLIP_Y) ? -y : y;
	}
}

void PoseSolver::SortBlobs (std::vector<Blob> &bl) const
{
	if (bl.size () < 3) return;
	if (type == Model::CAP) {
		int top = 0; // the topmost first
		if (bl[1].y > bl[0].y) {
			top = 1;
			if (bl[2].y > bl[1].y) top = 2;
		} else if (bl[2].y > bl[0].y) top = 2;
		if (top) std::swap (bl[0], bl[top]);
		if (bl[2].x < bl[1].x) std::swap (bl[1], bl[2]); // then the leftmost
	} else { // by height
		if (bl[1].y > bl[0].y) std::swap (bl[0], bl[1]);
		if (bl[2].y > bl[1].y) std::swap (bl[1], bl[2]);
		if (bl[1].y > bl[0].y) std::swap (bl[0], bl[1]);
	}
}

void PoseSolver::IterPose (const std::vector<Blob> &blobs, double points[3][3])
{
	double tmp[3], pp0[3], pp1[3], pp2[3];
	double d, e, f;
	if (type == Model::CAP) {
		pp0[0] = blobs[0].x; pp0[1] = blobs[0].y; pp0[2] = focal;
		pp1[0] = blobs[1].x; pp1[1] = blobs[1].y; pp1[2] = focal;
		pp2[0] = blobs[2].x; pp2[1] = blobs[2].y; pp2[2] = focal;
		MakeVec (mp1, mp2, tmp); d = VecSize (tmp);
		MakeVec (mp0, mp2, tmp); e = VecSize (tmp);
		MakeVec (mp0, mp1, tmp); f = VecSize (tmp);
	} else {
		pp0[0] = blobs[1].x; pp0[1] = blobs[1].y; pp0[2] = focal;
		pp1[0] = blobs[0].x; pp1[1] = blobs[0].y; pp1[2] = focal;
		pp2[0] = blobs[2].x; pp2[1] = blobs[2].y; pp2[2] = focal;
		MakeVec (mp0, mp2, tmp); d = VecSize (tmp);
		MakeVec (mp1, mp2, tmp); e = VecSize (tmp);
		MakeVec (mp1, mp0, tmp); f = VecSize (tmp);
	}
	NormalizeVec (pp0);
	NormalizeVec (pp1);
	NormalizeVec (pp2);

	double uv = Dot (pp0, pp1), uw = Dot (pp0, pp2), vw = Dot (pp1, pp2);
	double d2 = d*d, e2 = e*e, f2 = f*f;
	double h = uw * uw - 1.0, j = uv * uv - 1.0;
	Cross (pp0, pp1, tmp);
	double uxv = f / VecSize (tmp);
	Cross (pp0, pp2, tmp);
	double uxw = e / VecSize (tmp);
	double a_min = e < f ? f : e;
	double a_max = uxv < uxw ? uxv : uxw;

	double a = a_min + (a_min + a_max) / 2; // the first guess
	double b = 0, c = 0;
	for (int i = 0; i < 20; ++i) {
		if (a < a_min + 0.1) a = a_min + 0.1;
		if (a > a_max - 0.1) a = a_max - 0.1;
		double a2 = a * a;
		double k = std::sqrt (a2 * j + f2);
		double m = std::sqrt (a2 * h + e2);
		double n = a * h / m;
		double o = a * j / k;
		double p = a * uv - k;
		double q = a * uw - m;
		b = a * uv - k;
		c = a * uw - m;
		double i_d = b * b + c * c - 2 * b * c * vw - d2;
		a -= i_d / (2*((uv-o)*(p-q*vw)+(uw-n)*(q-p*vw)));
		if (std::fabs (i_d) < 1e-2) break;
	}
	if (type == Model::CAP) {
		MulVec (pp0, a, points[0]);
		MulVec (pp1, b, points[1]);
	} else {
		MulVec (pp0, a, points[1]);
		MulVec (pp1, b, points[0]);
	}
	MulVec (pp2, c, points[2]);
}

bool PoseSolver::Process (const std::vector<Blob> &blobs, bool centering, RawPose &pose)
{
	if (blobs.size () < 3) return false;
	for (int i = 0; i < 3; i++) if (!IsFinite (blobs[i].x) || !IsFinite (blobs[i].y)) return false;
	double points[3][3];
	IterPose (blobs, points);
	if (centering && !Center (points[0], points[1], points[2], c_base, tr_center, tr_rot)) return false;
	double angles[3], displacement[3];
	if (!GetPose (points[0], points[1], points[2], c_base, tr_center, tr_rot, angles, displacement)) return false;
	MulVec (angles, 180.0 / M_PI, angles);
	if (!IsVectorFinite (angles) || !IsVectorFinite (displacement)) return false;
	pose.pitch = angles[0];
	pose.yaw = angles[1];
	pose.roll = angles[2];
	pose.tx = displacement[0];
	pose.ty = displacement[1];
	pose.tz = displacement[2];
	return true;
}

bool PoseSolver::GetCbase (const double p0[3], const double p1[3], const double p2[3], const double c[3], double cbase[3])
{
	double R[3][3], base[3][3];
	MakeVec (p0, c, R[0]); // the centre of rotation at 0
	MakeVec (p1, c, R[1]);
	MakeVec (p2, c, R[2]);
	double b1[3], b2[3];
	MakeVec (R[1], R[0], b1);
	MakeVec (R[2], R[0], b2);
	MakeBase (b1, b2, base);
	if (!IsMatrixFinite (base)) return false;
	double zero[3] = {0, 0, 0}, tmp[3];
	MakeVec (zero, R[0], tmp); // from the first point to the centre
	MatrixTimesVec (base, tmp, cbase);
	return true;
}

bool PoseSolver::Center (const double rp0[3], const double rp1[3], const double rp2[3], const double cntr_base[3], double center[3], double tr[3][3])
{
	double rb0[3], rb1[3], rb[3][3], rc[3], RR[3][3];
	MakeVec (rp1, rp0, rb0);
	MakeVec (rp2, rp0, rb1);
	MakeBase (rb0, rb1, rb);
	TransposeInPlace (rb);
	if (!IsMatrixFinite (rb)) return false;
	MatrixTimesVec (rb, cntr_base, rc); // the centre in camera coordinates, from p0
	AddVecs (rc, rp0, center);
	MakeVec (rp0, center, RR[0]);
	MakeVec (rp1, center, RR[1]);
	MakeVec (rp2, center, RR[2]);
	TransposeInPlace (RR);
	double tmp_tr[3][3];
	InvertMatrix (RR, tmp_tr); // not orthonormal
	if (!IsMatrixFinite (tmp_tr)) return false;
	AssignMatrix (tmp_tr, tr);
	return true;
}

bool PoseSolver::GetPose (const double rp0[3], const double rp1[3], const double rp2[3], const double cntr_base[3],
	const double center[3], const double tr[3][3], double angles[3], double trans[3])
{
	double rb0[3], rb1[3], rb[3][3], rc[3], RR[3][3];
	MakeVec (rp1, rp0, rb0);
	MakeVec (rp2, rp0, rb1);
	MakeBase (rb0, rb1, rb);
	TransposeInPlace (rb);
	if (!IsMatrixFinite (rb)) return false;
	MatrixTimesVec (rb, cntr_base, rc);
	double current_center[3];
	AddVecs (rc, rp0, current_center);
	MakeVec (current_center, center, trans);
	double rot[3][3];
	MakeVec (rp0, current_center, RR[0]);
	MakeVec (rp1, current_center, RR[1]);
	MakeVec (rp2, current_center, RR[2]);
	TransposeInPlace (RR);
	MulMatrix (RR, tr, rot);
	if (!IsMatrixFinite (rot)) return false;
	MatrixToEuler (rot, &angles[0], &angles[1], &angles[2]);
	return true;
}

void PosePost::Reset ()
{
	for (int i = 0; i < 3; i++) fa[i] = ft[i] = 0.0f;
}

bool PosePost::Apply (const AxisDef axes[AX_COUNT], const RawPose &raw, RawPose &out, bool align)
{
	double ra[3];
	ra[0] = ValOnAxis (axes[AX_PITCH], (float)raw.pitch);
	ra[1] = ValOnAxis (axes[AX_YAW], (float)raw.yaw);
	ra[2] = ValOnAxis (axes[AX_ROLL], (float)raw.roll);
	if (!IsVectorFinite (ra)) return false;
	out.pitch = ClampAngle (FilterAxis (axes[AX_PITCH], AX_PITCH, (float)ra[0], &fa[0]));
	out.yaw = ClampAngle (FilterAxis (axes[AX_YAW], AX_YAW, (float)ra[1], &fa[1]));
	out.roll = ClampAngle (FilterAxis (axes[AX_ROLL], AX_ROLL, (float)ra[2], &fa[2]));

	double d[3], t[3];
	d[0] = ValOnAxis (axes[AX_TX], (float)raw.tx);
	d[1] = ValOnAxis (axes[AX_TY], (float)raw.ty);
	d[2] = ValOnAxis (axes[AX_TZ], (float)raw.tz);
	if (align) { // the moves in the head's own directions
		double m[3][3];
		EulerToMatrix (out.pitch / 180.0 * M_PI, out.yaw / 180.0 * M_PI, out.roll / 180.0 * M_PI, m);
		MatrixTimesVec (m, d, t);
		if (!IsVectorFinite (t)) return false;
	} else t[0] = d[0], t[1] = d[1], t[2] = d[2];
	out.tx = FilterAxis (axes[AX_TX], AX_TX, (float)t[0], &ft[0]);
	out.ty = FilterAxis (axes[AX_TY], AX_TY, (float)t[1], &ft[1]);
	out.tz = FilterAxis (axes[AX_TZ], AX_TZ, (float)t[2], &ft[2]);
	return true;
}

} // namespace ht
