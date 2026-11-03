// not upstream (head-track): ported from linuxtrack's axis.c and spline.c (MIT, LICENSE-linuxtrack)

#include "HtAxis.h"
#include "HtMath.h"
#include <cmath>

namespace ht {

namespace {

struct SplinePts { float x0, y0, x1, y1, x2, y2; };
struct Splines { SplinePts left, right; };

void Curve2Pts (const AxisDef &a, Splines &pts)
{
	pts.left.x0 = pts.right.x0 = a.deadzone;
	pts.left.y0 = pts.right.y0 = 0.0f;
	pts.left.x1 = a.deadzone + (1.0f - a.deadzone) * (1.0f - a.lcurv);
	pts.right.x1 = a.deadzone + (1.0f - a.deadzone) * (1.0f - a.rcurv);
	pts.left.y1 = a.lcurv;
	pts.right.y1 = a.rcurv;
	pts.left.x2 = pts.right.x2 = 1.0f;
	pts.left.y2 = pts.right.y2 = 1.0f;
}

float SplinePoint (const Splines &splns, float x) // x in -1..1
{
	const SplinePts *pts;
	float factor;
	if (x < 0.0f) {
		pts = &splns.left;
		factor = -1.0f;
		x *= -1.0f;
	} else {
		pts = &splns.right;
		factor = 1.0f;
	}
	float a = pts->x0;
	if (x < a) return 0.0f;
	float b = pts->x1, c = pts->x2;
	float tmp = c - 2 * b + a;
	if (std::fabs (tmp) < 1e-3f) return factor * (x - a) * ((pts->y2 - pts->y0) / (c - a)); // straight
	float t = (std::sqrt (tmp * x - a * c + b * b) - b + a) / tmp;
	float b02 = (1 - t) * (1 - t);
	float b12 = 2 * t * (1 - t);
	float b22 = t * t;
	return factor * ((b02 * pts->y0) + (b12 * pts->y1) + (b22 * pts->y2));
}

} // namespace

const char *AxisName (int axis)
{
	static const char *n[AX_COUNT] = {"Pitch", "Yaw", "Roll", "X", "Y", "Z"};
	return axis >= 0 && axis < AX_COUNT ? n[axis] : "";
}

AxisDef DefaultAxis (int axis)
{
	AxisDef a;
	switch (axis) {
	case AX_PITCH: a.factor = 5.0f, a.llimit = a.rlimit = 80.0f; break;
	case AX_YAW:   a.factor = 5.0f, a.llimit = a.rlimit = 130.0f; break;
	case AX_ROLL:  a.factor = 1.5f, a.llimit = a.rlimit = 45.0f; break;
	case AX_TX: case AX_TY: a.factor = 5.0f, a.llimit = a.rlimit = 300.0f; break;
	case AX_TZ:    a.factor = 2.0f, a.llimit = 300.0f, a.rlimit = 1.0f, a.filter = 0.5f; break;
	}
	return a;
}

float ValOnAxis (const AxisDef &a, float x)
{
	if (!a.enabled) return 0.0f;
	if (a.inverted) x = -x;
	Splines curves;
	Curve2Pts (a, curves);
	float lim = x < 0 ? a.llimit : a.rlimit;
	if (lim < 1e-5f) return 0.0f;
	x *= a.factor; // sensitivity
	x /= lim;      // the curve's travel
	if (x < -1.0f) x = -1.0f;
	if (x > 1.0f) x = 1.0f;
	return SplinePoint (curves, x) * lim;
}

float FilterAxis (const AxisDef &a, int axis, float x, float *y_minus_1)
{
	if (!a.enabled) return 0.0f;
	float trans_koef = axis == AX_TZ ? 10.0f : 1.0f;
	float ff = trans_koef * a.filter * (a.llimit > a.rlimit ? a.llimit : a.rlimit);
	return *y_minus_1 = NonLinFilt (x, *y_minus_1, ff);
}

} // namespace ht
